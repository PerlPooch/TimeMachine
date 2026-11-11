/*
 * ===========================================================================
 * File:      tests/application/acknowledgments.cpp
 * Firmware:  0.5.1
 * Git base:  1e4e91f4dbce
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

// Supply deterministic time to services without making network requests.
time_t fakeTime(time_t* value) {
	if (value) *value = utc;
	return utc;
}

// Decode actual transmitted bytes and reject interleaved or partial wire frames.
std::vector<std::string> payloads(const HardwareSerial& port) {
	std::vector<std::string> result;

	for (size_t offset = 0; offset < port.bytes.size();) {
		const size_t length = port.bytes[offset++];
		assert(offset + length < port.bytes.size());
		assert(port.bytes[offset + length] == '\n');
		result.emplace_back(reinterpret_cast<const char*>(port.bytes.data() + offset), length);
		offset += length + 1;
	}

	return result;
}

// Append a framed payload for receipt through the normal application path.
void inject(HardwareSerial& port, const char* payload) {
	uint8_t frame[SerialProtocol::BufferBytes];
	const size_t length = SerialProtocol::encode(payload, strlen(payload), frame, sizeof(frame));
	assert(length);
	port.incoming.insert(port.incoming.end(), frame, frame + length);
}

// Verify framing statuses independently of message meaning, role, and transmit readiness.
int main() {
	// Focus this fixture on responses; discovery is covered by the address fixture.
	WiFi.address = "0.0.0.0";

	using Status = SerialProtocol::Status;

	for (const auto& entry : std::vector<std::pair<Status, const char*>>{
		{Status::Ok, "ok"}, {Status::ErrorLength, "error-length"},
		{Status::ErrorTerminator, "error-terminator"}, {Status::ErrorTimeout, "error-timeout"}}) {
		uint8_t frame[SerialProtocol::StatusBufferBytes];
		const size_t length = SerialProtocol::encodeStatus(entry.first, frame, sizeof(frame));
		const std::string expected = std::string("{\"status\":\"") + entry.second + "\"}";
		assert(length == expected.size() + 2 && frame[0] == expected.size());
		assert(memcmp(frame + 1, expected.data(), expected.size()) == 0 && frame[length - 1] == '\n');
		assert(!SerialProtocol::encodeStatus(entry.first, frame, length)); // Room for local NUL required.
	}

	TwoWire wire;
	MatrixDriver matrix(wire, 21);
	MDS_Display oled;
	Button button(9);
	button.begin();
	Application app(Application::Mode::Clock, matrix, oled, button);
	assert(app.begin("test", false, true, true));
	auto& a = *HardwareSerial::ports[1];
	auto& b = *HardwareSerial::ports[0];
	const std::string ok = "{\"status\":\"ok\"}";

	for (auto* port : {&a, &b}) {
		a.bytes.clear(); b.bytes.clear();
		inject(*port, "unrecognized payload");
		app.tick();
		assert(payloads(*port) == std::vector<std::string>{ok});
		assert((port == &a ? b : a).bytes.empty());

		// Valid framing still receives ok when application validation rejects the payload.
		port->bytes.clear();
		inject(*port, "[\"display2\",\"unknown-action\"]");
		app.tick();
		assert(payloads(*port) == std::vector<std::string>{ok});

		// Status envelopes do not generate acknowledgment loops, even with whitespace.
		port->bytes.clear();
		for (const char* status : {"ok", "error-length", "error-terminator", "error-timeout", "future-status"}) {
			const std::string response = std::string(" \n{ \"status\" : \"") + status + "\" }";
			inject(*port, response.c_str());
			app.tick();
			assert(port->bytes.empty());
		}

		// An invalid length produces one response; discard recovery does not add a timeout.
		port->incoming.insert(port->incoming.end(), {247, 'x'});
		app.tick();
		assert(payloads(*port) == std::vector<std::string>{"{\"status\":\"error-length\"}"});
		app.tick(); // Drain remaining discarded bytes before measuring the idle timeout.
		nowMillis += SerialProtocol::Receiver::TimeoutMs;
		app.tick();
		assert(payloads(*port).size() == 1);

		port->bytes.clear();
		port->incoming.insert(port->incoming.end(), {1, 'x', '!', '\n'});
		app.tick(); app.tick();
		assert(payloads(*port) == std::vector<std::string>{"{\"status\":\"error-terminator\"}"});

		port->bytes.clear();
		port->incoming.insert(port->incoming.end(), {2, 'x'});
		app.tick();
		assert(port->bytes.empty());
		nowMillis += SerialProtocol::Receiver::TimeoutMs;
		app.tick();
		assert(payloads(*port) == std::vector<std::string>{"{\"status\":\"error-timeout\"}"});
	}

	// A busy output preserves each acknowledgment while the other port remains usable.
	a.bytes.clear(); b.bytes.clear(); a.room = 0;
	inject(a, "first"); inject(a, "second"); inject(b, "independent");
	app.tick();
	assert(a.bytes.empty() && a.available() > 0);
	assert(payloads(b) == std::vector<std::string>{ok});
	a.room = 1;
	for (int pass = 0; pass < 100; ++pass) app.tick();
	assert((payloads(a) == std::vector<std::string>{ok, ok}));

	// An acknowledgment waits until an already-started display frame finishes intact.
	a.bytes.clear(); b.bytes.clear(); a.room = 3;
	WebServer::current->request("/api/display", {{"message", "OUT"}, {"to", "a"}});
	assert(WebServer::current->code == 202);
	app.tick();
	assert(a.bytes.size() == 3);
	inject(a, "inbound");
	for (int pass = 0; pass < 100; ++pass) app.tick();
	const auto frames = payloads(a);
	assert(frames.size() == 2 && frames[0].find("display2") != std::string::npos && frames[1] == ok);
	assert(b.bytes.empty());

	puts("PASS: enum status encoding, both-port framing acknowledgments, payload-independent ok, loop prevention, error recovery, backpressure, and noninterleaved partial writes.");
}
