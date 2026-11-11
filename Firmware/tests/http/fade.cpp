/*
 * ===========================================================================
 * File:      tests/http/fade.cpp
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

#include "ApplicationApi.h"
#include <LittleFS.h>
#include <cassert>
#include <cstring>

Stream Serial;
FakeFS LittleFS;
int enableLevel = 0;
uint32_t now = 0;

// Control the animation clock without sleeping.
uint32_t millis() { return now; }

// Check logical channel scaling without depending on physical gamma output.
void expectScaled(const Display::Frame& target, MatrixDriver& matrix, uint8_t level) {
	for (size_t index = 0; index < sizeof(target.channels); ++index) {
		assert(matrix.brightness(index) == (uint16_t(target.channels[index]) * level + 127) / 255);
	}
}

// Exercise HTTP parsing, queued rendering, and the serial envelope together.
int main() {
	using Type = MDS_ConfigType;
	using Access = MDS_ConfigAccess;
	const MDS_ConfigSetting settings[] = {
		{"pwm", Type::Unsigned, Access::ReadWrite, true, nullptr, 255, 0, 255},
		{"gcc", Type::Unsigned, Access::ReadWrite, true, nullptr, 4, 0, 255}
	};
	MDS_Config config;
	assert(config.setSettings(settings) && config.begin());
	TwoWire wire;
	MatrixDriver matrix(wire, 21);
	Display display(matrix);
	assert(display.begin());
	MDS_HTTP http;
	ApplicationApi api(http, config, matrix, display.commands());
	api.begin();
	auto& server = http.server();
	Display::DisplayCommand command;

	server.request("/api/display", {{"message", "ABC123"}, {"am", "true"}, {"pm", "true"}, {"colontop", "true"}});
	assert(server.code == 202 && display.tick());
	const auto original = display.targetFrame();
	assert(original.channels[DisplayMap::discreteAddress(DisplayMap::DiscreteIndex::AM)] == 92 && original.channels[DisplayMap::discreteAddress(DisplayMap::DiscreteIndex::Colon1)] == 92);

	// The HTTP response queues the fade; hardware changes only as Display ticks.
	const int calls = wire.calls;
	server.request("/api/display", {{"fade", "128"}, {"speed", "1000"}});
	assert(server.code == 202 && server.headers["Cache-Control"] == "no-store");
	assert(wire.calls == calls && !config.exists());
	assert(display.tick() && display.fading());
	now = 500;
	assert(display.tick());
	expectScaled(original, matrix, 192);
	now = 1000;
	assert(display.tick() && !display.fading());
	expectScaled(original, matrix, 128);
	const auto target = display.targetFrame();
	assert(memcmp(target.channels, original.channels, sizeof(target.channels)) == 0);
	assert(display.hasOverride());

	// Immediate fades can black out and restore the same content and indicators.
	server.request("/api/display", {{"speed", "0"}, {"fade", "0"}});
	assert(server.code == 202 && display.tick() && !display.fading());
	expectScaled(original, matrix, 0);
	server.request("/api/display", {{"fade", "255"}, {"speed", "0"}});
	assert(server.code == 202 && display.tick());
	expectScaled(original, matrix, 255);

	// Default duration matches fadeUp/fadeDown; the maximum duration is accepted.
	server.request("/api/display", {{"fade", "0"}});
	assert(server.code == 202 && display.commands().take(command));
	assert(command.kind == Display::DisplayCommand::Kind::Fade && command.level == 0 && command.speed == 350);
	server.request("/api/display", {{"fade", "255"}, {"speed", "60000"}});
	assert(server.code == 202 && display.commands().take(command) && command.speed == 60000);

	// Failed validation leaves the command queue and persistent settings alone.
	for (const char* level : {"", "-1", "256", "1.5", "true", "4294967296"}) {
		server.request("/api/display", {{"fade", level}, {"speed", "1000"}});
		assert(server.code == 400 && !display.commands().take(command));
	}
	for (const char* speed : {"", "-1", "60001", "1.5", "4294967296"}) {
		server.request("/api/display", {{"fade", "128"}, {"speed", speed}});
		assert(server.code == 400 && !display.commands().take(command));
	}
	for (const auto& extra : std::vector<std::pair<String, String>>{
		{"message", "ABC"}, {"scroll", "123"}, {"slide", "123"}, {"appear", "123"},
		{"fade", "255"}, {"direction", "left"}, {"am", "false"}, {"pm", "true"},
		{"colontop", "false"}, {"colonbottom", "true"}, {"colons", "blink"}, {"pwm", "100"}}) {
		server.request("/api/display", {{"fade", "128"}, extra});
		assert(server.code == 400 && !display.commands().take(command));
	}
	server.request("/api/display", {{"fade", "128"}, {"speed", "1000"}, {"speed", "2000"}});
	assert(server.code == 400 && !display.commands().take(command));
	server.request("/api/config", {{"fade", "128"}});
	assert(server.code == 400);
	assert(!config.exists() && config.get("pwm").as<int>() == 255 && config.get("gcc").as<int>() == 4);

	// Routed fades contain only the version, action, target level, and duration.
	String encoded;
	uint8_t destinations = 0;
	bool acceptRoute = true;
	int routes = 0;
	api.setDisplayRouter([&](const Display::DisplayCommand& routed) {
		++routes;
		destinations = routed.destinations;
		char output[247];
		assert(ApplicationApi::encodeDisplayCommand(routed, output, sizeof(output)));
		encoded = output;
		return acceptRoute;
	});
	server.request("/api/display", {{"fade", "64"}, {"speed", "1000"}, {"to", "a"}, {"to", "b"}});
	assert(server.code == 202 && !display.commands().take(command));
	assert(destinations == (Display::PortA | Display::PortB) && encoded == "[\"display2\",\"fade\",64,1000]");
	assert(api.receiveDisplayCommand(reinterpret_cast<const uint8_t*>(encoded.c_str()), encoded.length()));
	assert(display.commands().take(command) && command.kind == Display::DisplayCommand::Kind::Fade);
	assert(command.level == 64 && command.speed == 1000 && command.destinations == Display::Local && routes == 1);
	acceptRoute = false;
	server.request("/api/display", {{"fade", "128"}, {"to", "a"}, {"to", "local"}});
	assert(server.code == 503 && !display.commands().take(command));
	acceptRoute = true;
	server.request("/api/display", {{"fade", "128"}, {"to", "b"}, {"to", "local"}});
	assert(server.code == 202 && display.commands().take(command) && command.level == 128);

	for (const char* invalid : {
		"[\"display1\",\"fade\",128,1000]", "[\"display2\",\"fade\",256,1000]",
		"[\"display2\",\"fade\",-1,1000]", "[\"display2\",\"fade\",1.5,1000]",
		"[\"display2\",\"fade\",\"128\",1000]", "[\"display2\",\"fade\",128,-1]",
		"[\"display2\",\"fade\",128,60001]", "[\"display2\",\"fade\",128,1.5]",
		"[\"display2\",\"fade\",128,\"1000\"]", "[\"display2\",\"fade\",128]",
		"[\"display2\",\"fade\",128,1000,\"left\"]"}) {
		assert(!api.receiveDisplayCommand(reinterpret_cast<const uint8_t*>(invalid), strlen(invalid)));
		assert(!display.commands().take(command));
	}
	const char* immediate = "[\"display2\",\"fade\",255,0]";
	assert(api.receiveDisplayCommand(reinterpret_cast<const uint8_t*>(immediate), strlen(immediate)));
	assert(display.commands().take(command) && command.level == 255 && command.speed == 0);

	// Fades leave scrolling and synchronized colon blinking active at the new level.
	server.request("/api/display", {{"scroll", "123"}, {"speed", "100"}, {"colons", "blink"}});
	assert(server.code == 202 && display.tick() && display.scrolling());
	server.request("/api/display", {{"fade", "128"}, {"speed", "0"}});
	assert(server.code == 202 && display.tick() && display.scrolling());
	now = 2000;
	assert(display.tick() && display.scrolling());
	assert(matrix.brightness(DisplayMap::discreteAddress(DisplayMap::DiscreteIndex::Colon1)) == 46 && matrix.brightness(DisplayMap::discreteAddress(DisplayMap::DiscreteIndex::Colon2)) == 46);
	assert(!config.exists());
	puts("PASS: queued HTTP fades, duration/defaults/bounds, content and indicator preservation, scrolling/blinking coexistence, no persistence, and strict serial fade round trips.");
}
