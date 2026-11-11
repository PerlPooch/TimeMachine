/*
 * ===========================================================================
 * File:      tests/application/addresses.cpp
 * Firmware:  0.6.0
 * Git base:  4967da86d1b1
 *
 * Copyright (c) 2026 Marc D. Spencer.
 * All Rights Reserved under the applicable license.
 * SPDX-License-Identifier: MPL-2.0
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 * ===========================================================================
 */

#include "Application.h"
#include <MDS_Display.h>
#include <WiFi.h>
#include <esp_wifi.h>
#include <esp_sntp.h>
#include <HTTPClient.h>
#include <freertos/task.h>
#include <LittleFS.h>
#include <cassert>

Stream Serial;
FakeFS LittleFS;
FakeSNTP sntp;
FakeESP ESP;
int buttonLevel = HIGH, enableLevel = 0;
uint32_t nowMillis = 0;
time_t utc = 1792944000;
bool failQueue = false, failTask = false, bundleUsed = false;
std::vector<PendingTask> tasks;
HttpState httpState;

// Keep service time deterministic and independent of serial interval tests.
time_t fakeTime(time_t* value) {
	if (value) *value = utc;
	return utc;
}

// Return complete transmitted payloads, rejecting interleaved or truncated frames.
std::vector<std::string> payloads(const HardwareSerial& port) {
	std::vector<std::string> result;

	for (size_t offset = 0; offset < port.bytes.size();) {
		const size_t length = port.bytes[offset++];
		assert(offset + length < port.bytes.size() && port.bytes[offset + length] == '\n');
		result.emplace_back(reinterpret_cast<const char*>(port.bytes.data() + offset), length);
		offset += length + 1;
	}

	return result;
}

// Extract announcements while permitting independent pings and protocol responses.
std::vector<std::string> addresses(const HardwareSerial& port) {
	std::vector<std::string> result;

	for (const auto& payload : payloads(port)) {
		JsonDocument value;
		if (!deserializeJson(value, payload) && value["ip"].is<const char*>()) {
			result.emplace_back(value["ip"].as<const char*>());
		}
	}

	return result;
}

// Exercise the real application, JSON config routes, and both UARTs.
struct Fixture {
	TwoWire wire;
	MatrixDriver matrix{wire, 21};
	MDS_Display oled;
	Button button{9};
	Application app{Application::Mode::Clock, matrix, oled, button};

	// Initialize each scenario without carrying network or filesystem state between cases.
	explicit Fixture(const char* address = "192.168.1.10", const char* saved = nullptr) {
		nowMillis = 0;
		buttonLevel = HIGH;
		LittleFS = FakeFS{};
		if (saved) LittleFS.files["/config.json"] = saved;
		savedWiFi = wifi_config_t{};
		WiFi.address = address;
		button.begin();
		assert(app.begin("test", false, true, true));
	}

	// Read current settings through the same public interface used by clients.
	JsonDocument config() {
		auto& server = *WebServer::current;
		server.request("/api/config");
		assert(server.code == 200);
		JsonDocument result;
		assert(!deserializeJson(result, static_cast<const std::string&>(server.body)));
		return result;
	}

	// Deliver a normal framed message on the selected physical link.
	void receive(HardwareSerial& port, const char* payload) {
		uint8_t frame[SerialProtocol::BufferBytes];
		const size_t length = SerialProtocol::encode(payload, strlen(payload), frame, sizeof(frame));
		assert(length);
		port.incoming.insert(port.incoming.end(), frame, frame + length);
		app.tick();
	}

	// Access port A without relying on the order in which serial objects were constructed.
	HardwareSerial& a() { return *HardwareSerial::ports[1]; }

	// Access port B independently for receive and backpressure scenarios.
	HardwareSerial& b() { return *HardwareSerial::ports[0]; }
};

// Verify discovery, RO/runtime semantics, validation, timing, and per-port isolation.
int main() {
	uint8_t frame[SerialProtocol::IPBufferBytes];
	for (const char* valid : {"", "1.2.3.4", "192.168.1.10", "255.255.255.255"}) {
		assert(SerialProtocol::validIPAddress(valid, strlen(valid)));
		assert(SerialProtocol::encodeIPAddress(valid, frame, sizeof(frame)));
	}
	for (const char* invalid : {"0.0.0.0", "256.1.2.3", "1.2.3", "1.2.3.4.5", "1..2.3", "1.2.3.4x", "-1.2.3.4", "1.2.3.4\n"}) {
		assert(!SerialProtocol::validIPAddress(invalid, strlen(invalid)));
		assert(!SerialProtocol::encodeIPAddress(invalid, frame, sizeof(frame)));
	}

	// All roles announce immediately, then every ten seconds; local IP is not a peer IP.
	for (bool primary : {false, true}) {
		Fixture f;
		auto initial = f.config();
		assert(initial["ipA"].is<const char*>() && String(initial["ipA"].as<const char*>()) == "");
		assert(String(initial["ipB"].as<const char*>()) == "");
		WebServer::current->request("/api/config", {{"primary", primary ? "true" : "false"}});
		f.app.tick(); f.app.tick();
		assert(addresses(f.a()) == std::vector<std::string>{"192.168.1.10"});
		assert(addresses(f.b()) == addresses(f.a()));
		nowMillis = 9999; f.app.tick();
		assert(addresses(f.a()).size() == 1);
		nowMillis = 10000; f.app.tick(); f.app.tick();
		assert(addresses(f.a()).size() == 2 && addresses(f.b()).size() == 2);
		auto config = f.config();
		assert(String(config["ipA"].as<const char*>()) == "" && String(config["ipB"].as<const char*>()) == "");
	}

	// Peer fields are per-port, read-only, not persisted, and cannot be injected by a saved file.
	{
		Fixture f("0.0.0.0", "{\"schema\":1,\"ipA\":\"10.0.0.99\",\"ipB\":\"10.0.0.98\"}");
		assert(String(f.config()["ipA"].as<const char*>()) == "");
		f.receive(f.a(), "{\"ip\":\"10.0.0.2\"}");
		f.receive(f.b(), " { \"ip\" : \"10.0.0.3\" }");
		auto values = f.config();
		assert(String(values["ipA"].as<const char*>()) == "10.0.0.2");
		assert(String(values["ipB"].as<const char*>()) == "10.0.0.3");
		assert(payloads(f.a()) == std::vector<std::string>{"{\"status\":\"ok\"}"});
		assert(payloads(f.b()) == payloads(f.a())); // Only acknowledgments, never forwarded IPs.
		for (const char* key : {"ipA", "ipB"}) {
			WebServer::current->request("/api/config", {{"name", "BAD"}, {key, "10.0.0.9"}});
			assert(WebServer::current->code == 403);
		}
		WebServer::current->request("/api/config", {{"name", "Saved"}});
		assert(WebServer::current->code == 200);
		assert(LittleFS.files["/config.json"].find("ipA") == String::npos);
		assert(LittleFS.files["/config.json"].find("ipB") == String::npos);

		for (const char* bad : {"{\"ip\":\"999.1.2.3\"}", "{\"ip\":123}", "{\"ip\":null}",
			"{\"ip\":\"10.0.0.5\\u0000evil\"}", "{\"ip\":\"10.0.0.5\",\"other\":true}"}) {
			f.receive(f.a(), bad);
			assert(String(f.config()["ipA"].as<const char*>()) == "10.0.0.2");
		}
		f.receive(f.b(), "{\"ip\":\"\"}");
		assert(String(f.config()["ipB"].as<const char*>()) == "");
	}

	// Refresh and expiry remain independent across both ports and timer rollover.
	{
		Fixture f("0.0.0.0");
		nowMillis = 0xfffffff0U;
		f.receive(f.a(), "{\"ip\":\"10.0.0.2\"}");
		f.receive(f.b(), "{\"ip\":\"10.0.0.3\"}");
		nowMillis += 29999;
		f.receive(f.b(), "{\"ip\":\"10.0.0.3\"}");
		f.receive(f.a(), "{\"ip\":\"invalid\"}"); // Rejected data must not refresh expiry.
		assert(String(f.config()["ipA"].as<const char*>()) == "10.0.0.2");
		nowMillis += 1; f.app.tick();
		assert(String(f.config()["ipA"].as<const char*>()) == "");
		assert(String(f.config()["ipB"].as<const char*>()) == "10.0.0.3");
	}

	// Address changes replace unsent announcements; partial frames finish without interleaving.
	{
		Fixture f;
		f.a().room = 2; f.app.tick();
		assert(f.a().bytes.size() == 2 && addresses(f.b()).size() == 1);
		f.a().room = 0;
		WiFi.address = "192.168.1.11";
		f.app.tick();
		assert(addresses(f.b()).back() == "192.168.1.11");
		f.a().room = 3;
		for (int pass = 0; pass < 40; ++pass) f.app.tick();
		assert((addresses(f.a()) == std::vector<std::string>{"192.168.1.10", "192.168.1.11"}));
		MDS_WiFi::current->online = false;
		f.app.tick();
		for (int pass = 0; pass < 20; ++pass) f.app.tick();
		assert(addresses(f.a()).back().empty() && addresses(f.b()).back().empty());
	}

	// An obsolete unsent announcement is replaced without sending the old address.
	{
		Fixture f;
		f.a().room = 0;
		f.app.tick();
		WiFi.address = "192.168.1.12";
		f.a().room = 128;
		f.app.tick();
		assert(addresses(f.a()) == std::vector<std::string>{"192.168.1.12"});
	}

	// Discovery waits behind an existing display command without overwriting its frame.
	{
		Fixture f;
		WebServer::current->request("/api/display", {{"message", "FIRST"}, {"to", "a"}});
		assert(WebServer::current->code == 202);
		f.app.tick(); f.app.tick();
		const auto frames = payloads(f.a());
		assert(frames.size() == 2 && frames[0].find("display2") != std::string::npos);
		assert(addresses(f.a()) == std::vector<std::string>{"192.168.1.10"});
	}

	puts("PASS: both-role IP announcements, periodic refresh, per-port read-only config, validation, expiry/rollover, runtime-only persistence, address changes, disconnect notices, and partial writes.");
}
