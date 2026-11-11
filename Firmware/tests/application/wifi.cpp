/*
 * ===========================================================================
 * File:      tests/application/wifi.cpp
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
#include <vector>

Stream Serial;
FakeFS LittleFS;
FakeSNTP sntp;
FakeESP ESP;
int buttonLevel = HIGH;
uint32_t nowMillis = 0;
int enableLevel = 0;
time_t utc = 1792900800;
bool failQueue = false, failTask = false, bundleUsed = false;
std::vector<PendingTask> tasks;
HttpState httpState;

// Supply a deterministic clock for application services.
time_t fakeTime(time_t* value) {
	if (value) *value = utc;
	return utc;
}

// Isolate application settings, saved Wi-Fi, and both UARTs for each scenario.
struct Fixture {
	TwoWire wire;
	MatrixDriver matrix{wire, 21, 8};
	MDS_Display oled;
	Button button{9};
	Application app{Application::Mode::Clock, matrix, oled, button};

	explicit Fixture(bool online = true) {
		nowMillis = 0;
		// Keep discovery traffic separate from credential scheduling assertions.
		WiFi.address = "0.0.0.0";
		buttonLevel = HIGH;
		LittleFS = FakeFS{};
		savedWiFi = wifi_config_t{};
		readableWiFi = true;
		Serial.output.clear();
		MDS_WiFi::current->online = online;
		button.begin();
		assert(app.begin("test", false, true, true));
	}

	// Use the same live role setting that controls timestamped pings.
	void primary(bool enabled) {
		WebServer::current->request("/api/config", {{"primary", enabled ? "true" : "false"}});
		assert(WebServer::current->code == 200);
	}

	HardwareSerial& a() { return *HardwareSerial::ports[1]; }
	HardwareSerial& b() { return *HardwareSerial::ports[0]; }
};

// Prepare saved platform credentials without making the station online.
void saveNetwork(const char* ssid, const char* password) {
	savedWiFi = wifi_config_t{};
	memcpy(savedWiFi.sta.ssid, ssid, strlen(ssid));
	memcpy(savedWiFi.sta.password, password, strlen(password));
}

// Return complete wire frames, asserting that pings never interleave their bytes.
std::vector<std::vector<uint8_t>> payloads(const HardwareSerial& port) {
	std::vector<std::vector<uint8_t>> result;
	for (size_t offset = 0; offset < port.bytes.size();) {
		const size_t length = port.bytes[offset++];
		assert(offset + length < port.bytes.size());
		assert(port.bytes[offset + length] == '\n');
		result.emplace_back(port.bytes.begin() + offset, port.bytes.begin() + offset + length);
		offset += length + 1;
	}
	return result;
}

// Count broadcasts while validating every other frame as a timestamped ping.
size_t credentialCount(const HardwareSerial& port, const char* ssid, const char* password) {
	size_t count = 0;
	for (const auto& payload : payloads(port)) {
		if (SerialProtocol::isWiFiCredentials(payload.data(), payload.size())) {
			SerialProtocol::WiFiCredentials decoded;
			assert(SerialProtocol::decodeWiFiCredentials(payload.data(), payload.size(), decoded));
			assert(strcmp(decoded.ssid, ssid) == 0 && strcmp(decoded.password, password) == 0);
			++count;
		} else {
			uint32_t timestamp;
			assert(SerialProtocol::decodePing(payload.data(), payload.size(), timestamp));
		}
	}
	return count;
}

// Encode a credential frame for receive and fragmentation scenarios.
std::vector<uint8_t> credentialFrame(const char* ssid, const char* password) {
	SerialProtocol::WiFiCredentials credentials;
	strlcpy(credentials.ssid, ssid, sizeof(credentials.ssid));
	strlcpy(credentials.password, password, sizeof(credentials.password));
	uint8_t frame[SerialProtocol::WiFiBufferBytes];
	const size_t length = SerialProtocol::encodeWiFiCredentials(credentials, frame, sizeof(frame));
	assert(length);
	return {frame, frame + length};
}

// Inject an already framed payload on either UART.
void inject(Fixture& fixture, HardwareSerial& port, const std::vector<uint8_t>& frame) {
	port.incoming.insert(port.incoming.end(), frame.begin(), frame.end());
	fixture.app.tick();
}

// Exercise real application dispatch, serial scheduling, bounds, and log redaction.
int main() {
#ifdef DEBUG_SERIAL_FRAMES
	constexpr bool traceFrames = true;
#else
	constexpr bool traceFrames = false;
#endif

	// Full-sized strings, raw special characters, and open networks fit existing framing.
	for (const auto& network : std::vector<std::pair<std::string, std::string>>{
		{std::string(32, 's'), std::string(64, 'a')},
		{"Quotes\"\\\nSSID", "secret\"\\\npassword"}, {"Open network", ""}}) {
		const auto frame = credentialFrame(network.first.c_str(), network.second.c_str());
		assert(frame.size() <= SerialProtocol::WiFiBufferBytes);
		SerialProtocol::WiFiCredentials decoded;
		assert(SerialProtocol::decodeWiFiCredentials(frame.data() + 1, frame[0], decoded));
		assert(decoded.ssid == network.first && decoded.password == network.second);
	}
	SerialProtocol::WiFiCredentials invalid;
	uint8_t buffer[SerialProtocol::WiFiBufferBytes];
	assert(!SerialProtocol::encodeWiFiCredentials(invalid, buffer, sizeof(buffer)));
	memset(invalid.ssid, 'x', sizeof(invalid.ssid));
	assert(!SerialProtocol::encodeWiFiCredentials(invalid, buffer, sizeof(buffer)));
	strlcpy(invalid.ssid, "Network", sizeof(invalid.ssid));
	assert(!SerialProtocol::encodeWiFiCredentials(invalid, buffer, 5));

	// Broadcast immediately on promotion, then every ten seconds, even while offline.
	{
		Fixture fixture;
		fixture.primary(true);
		saveNetwork("Master Network", "master-password");
		MDS_WiFi::current->online = false;
		nowMillis = 0xfffffff0U;
		fixture.app.tick();
		assert(credentialCount(fixture.a(), "Master Network", "master-password") == 1);
		assert(fixture.a().bytes == fixture.b().bytes);
		nowMillis += 9999; fixture.app.tick();
		assert(credentialCount(fixture.a(), "Master Network", "master-password") == 1);
		nowMillis += 1; fixture.app.tick();
		assert(credentialCount(fixture.a(), "Master Network", "master-password") == 2);
		nowMillis += 60000; fixture.app.tick();
		assert(credentialCount(fixture.a(), "Master Network", "master-password") == 3);
		assert(fixture.a().bytes == fixture.b().bytes); // No catch-up burst after delays.
	}

	// A blocked port retains a partial credential frame; B continues independently.
	{
		Fixture fixture;
		fixture.primary(true);
		saveNetwork("Network", "password");
		fixture.a().room = 3;
		fixture.app.tick();
		assert(fixture.a().bytes.size() == 3);
		assert(credentialCount(fixture.b(), "Network", "password") == 1);
		WebServer::current->request("/api/display", {{"message", "BUSY"}, {"to", "a"}});
		assert(WebServer::current->code == 503);
		fixture.a().room = 0;
		nowMillis = 10000; fixture.app.tick();
		assert(fixture.a().bytes.size() == 3);
		assert(credentialCount(fixture.b(), "Network", "password") == 2);
		fixture.a().room = 4;
		for (int pass = 0; pass < 40; ++pass) fixture.app.tick();
		assert(credentialCount(fixture.a(), "Network", "password") == 2);
	}

	// No SSID or failed reads send no credential frame; a later save is picked up.
	{
		Fixture fixture;
		fixture.primary(true);
		fixture.app.tick();
		assert(credentialCount(fixture.a(), "", "") == 0);
		saveNetwork("Later Network", "later-password");
		readableWiFi = false;
		nowMillis = 10000; fixture.app.tick();
		assert(credentialCount(fixture.a(), "", "") == 0);
		readableWiFi = true;
		nowMillis = 20000; fixture.app.tick();
		assert(credentialCount(fixture.a(), "Later Network", "later-password") == 1);
	}

	// Busy display queues retain their frame ahead of the due credential broadcast.
	{
		Fixture fixture;
		fixture.primary(true);
		saveNetwork("Network", "password");
		WebServer::current->request("/api/display", {{"message", "FIRST"}, {"to", "a"}});
		assert(WebServer::current->code == 202);
		fixture.app.tick();
		fixture.app.tick();
		const auto frames = payloads(fixture.a());
		assert(frames.size() == 2 && frames[0][0] == '[');
		assert(SerialProtocol::isWiFiCredentials(frames[1].data(), frames[1].size()));
	}

	// Both receive ports accept credentials while an unconfigured slave's portal is open.
	for (bool useA : {true, false}) {
		Fixture fixture(false);
		auto& wifi = *MDS_WiFi::current;
		assert(wifi.portal && wifi.policy == MDS_WiFi::ConnectionPolicy::RetrySavedNetworkAsync);
		auto& port = useA ? fixture.a() : fixture.b();
		const auto frame = credentialFrame("Private SSID", "private-password");
		Serial.output.clear();
		port.incoming.insert(port.incoming.end(), frame.begin(), frame.begin() + 8);
		fixture.app.tick(); assert(wifi.credentialCalls == 0);
		port.incoming.insert(port.incoming.end(), frame.begin() + 8, frame.end());
		fixture.app.tick();
		assert(wifi.credentialCalls == 1 && wifi.credentialChanges == 1 && !wifi.portal);
		assert(wifi.receivedSSID == "Private SSID" && wifi.receivedPassword == "private-password");
		inject(fixture, port, frame);
		assert(wifi.credentialCalls == 2 && wifi.credentialChanges == 1);
		// Receipt is acknowledged on the input port; credentials are never forwarded.
		const auto replies = payloads(port);
		assert(replies.size() == 2);
		for (const auto& reply : replies) {
			assert(std::string(reply.begin(), reply.end()) == "{\"status\":\"ok\"}");
		}
		assert((useA ? fixture.b() : fixture.a()).bytes.empty());
		assert((Serial.output.find("private-password") != std::string::npos) == traceFrames);
		assert((Serial.output.find("Private SSID") != std::string::npos) == traceFrames);
		assert(Serial.output.find("WiFi credentials accepted") != std::string::npos);

		// Existing credentials for an unavailable AP are replaced by a different master.
		inject(fixture, port, credentialFrame("Replacement", "replacement-password"));
		assert(wifi.credentialChanges == 2 && wifi.receivedSSID == "Replacement");
	}

	// Malformed or unsupported credential frames are rejected regardless of tracing.
	{
		Fixture fixture(false);
		auto valid = credentialFrame("Private SSID", "private-password");
		for (int corruption = 0; corruption < 5; ++corruption) {
			auto frame = valid;
			if (corruption == 0) frame[5] = '2'; // Unsupported version.
			if (corruption == 1) frame[6] = 0; // Empty SSID.
			if (corruption == 2) frame[7] = 65; // Password length outside bounds.
			if (corruption == 3) frame[8] = 0; // Embedded NUL.
			if (corruption == 4) frame[6] = 32; // Lengths disagree with envelope.
			inject(fixture, fixture.a(), frame);
		}
		assert(MDS_WiFi::current->credentialCalls == 0);
		assert((Serial.output.find("private-password") != std::string::npos) == traceFrames);
		assert((Serial.output.find("Private SSID") != std::string::npos) == traceFrames);
		assert(Serial.output.find("invalid WiFi credentials") != std::string::npos);
		MDS_WiFi::current->acceptCredentials = false;
		inject(fixture, fixture.b(), valid);
		assert(Serial.output.find("WiFi credentials rejected") != std::string::npos);
	}

	// Masters ignore incoming credentials; demotion drops unsent broadcasts.
	{
		Fixture fixture;
		fixture.primary(true);
		saveNetwork("Own Network", "own-password");
		fixture.a().room = 0;
		fixture.app.tick();
		inject(fixture, fixture.b(), credentialFrame("Other Network", "other-password"));
		assert(MDS_WiFi::current->credentialCalls == 0);
		assert((Serial.output.find("other-password") != std::string::npos) == traceFrames);
		fixture.primary(false);
		fixture.a().room = 128;
		fixture.app.tick();
		assert(fixture.a().bytes.empty());
		nowMillis += 20000; fixture.app.tick();
		assert(fixture.a().bytes.empty());
	}

	puts("PASS: A/B Wi-Fi credentials, offline master broadcasts, ten-second timing/rollover, bounded framing, partial writes, queue coexistence, role changes, slave portal/retry replacement, and opt-in credential tracing.");
}
