/*
 * ===========================================================================
 * File:      tests/http/routing.cpp
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

#include "ApplicationApi.h"
#include <LittleFS.h>
#include <cassert>
#include <cstdlib>
#include <ctime>

Stream Serial;
FakeFS LittleFS;
int enableLevel = 0;

// Keep display servicing deterministic while requests only store offset values.
uint32_t millis() { return 0; }

// Decode the public response to inspect the numeric representation sent to clients.
JsonDocument response(const String& text) {
 JsonDocument result;
 assert(!deserializeJson(result, static_cast<const std::string&>(text)));
 return result;
}

// Check destination selection, normalization, validation before routing, and local-only receive behavior.
int main() {
 using Type = MDS_ConfigType;
 using Access = MDS_ConfigAccess;
 const MDS_ConfigSetting settings[] = {
  {"name", Type::Text, Access::ReadWrite, true, "Clock", 0, 1, 255},
  {"timeOffset", Type::Signed, Access::ReadWrite, false, nullptr, 0, 0, 0},
  {"version", Type::Text, Access::ReadOnly, false, "test", 0, 0, 32},
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
 bool accept = true;
 int calls = 0;
 uint8_t destination = 0;
 std::string wirePayload;
 api.setSerialRouter([&](uint8_t to, const char* payload, size_t length) {
  ++calls; destination = to; wirePayload.assign(payload, length); return accept;
 });
 server.request("/api/config", {{"timeOffset","+5d"},{"to","b"}});
 assert(server.code == 202 && calls == 1 && destination == Display::PortB);
 assert(config.get("timeOffset").as<int64_t>() == 0 && LittleFS.files.empty());
 assert(wirePayload == "[\"config1\",{\"timeOffset\":432000}]");
 assert(api.receiveConfigCommand(reinterpret_cast<const uint8_t*>(wirePayload.data()), wirePayload.size()));
 assert(config.get("timeOffset").as<int64_t>() == 432000 && calls == 1 && LittleFS.files.empty());
 api.setTimeSource([](int64_t& now) { now = 1793448000; return true; });
 server.request("/api/config", {{"timeOffset","2026-10-31T12:00:01Z"},{"to","all"},{"to","a"}});
 assert(server.code == 200 && destination == 7 && calls == 2);
 assert(config.get("timeOffset").as<int64_t>() == 1);
 assert(wirePayload == "[\"config1\",{\"timeOffset\":1}]");
 accept = false;
 server.request("/api/config", {{"timeOffset","-2h"},{"to","all"}});
 assert(server.code == 503 && calls == 3 && config.get("timeOffset").as<int64_t>() == 1);
 accept = true;
 for (auto args : std::initializer_list<std::vector<std::pair<String,String>>>{
  {{"timeOffset","+1h"},{"to","invalid"}},
  {{"timeOffset","+1h"},{"pwm","256"},{"to","all"}},
  {{"timeOffset","+1h"},{"timeOffset","-1s"},{"to","b"}},
  {{"to","b"}}}) {
  server.request("/api/config", args); assert(server.code == 400 && calls == 3);
 }
 server.request("/api/config", {{"version","x"},{"to","b"}});
 assert(server.code == 403 && calls == 3);
 String longName(std::string(245, 'x'));
 server.request("/api/config", {{"name",longName},{"to","all"}});
 assert(server.code == 413 && calls == 3 && String(config.get("name").as<const char*>()) == "Clock");
 server.request("/api/config", {{"name","Remote"},{"to","a"},{"to","b"}});
 assert(server.code == 202 && destination == 6 && calls == 4 && LittleFS.files.empty());
 assert(api.receiveConfigCommand(reinterpret_cast<const uint8_t*>(wirePayload.data()), wirePayload.size()));
 assert(String(config.get("name").as<const char*>()) == "Remote" && !LittleFS.files.empty() && calls == 4);
 for (const char* bad : {"[\"config1\",{}]", "[\"config1\",{\"to\":\"b\"}]", "[\"config1\",{\"version\":\"bad\"}]", "[\"config1\",{\"timeOffset\":\"+5d\"}]", "[\"config1\",{\"name\":\"bad\",\"pwm\":256}]", "[\"config1\",{},0]"}) {
  assert(!api.receiveConfigCommand(reinterpret_cast<const uint8_t*>(bad), strlen(bad)));
  assert(String(config.get("name").as<const char*>()) == "Remote" && calls == 4);
 }
 server.request("/api/config", {{"timeOffset","0"},{"to","local"}});
 assert(server.code == 200 && calls == 4 && config.get("timeOffset").as<int64_t>() == 0);
 puts("PASS: configuration routing, normalized offsets, grouped validation, size and busy rejection, local selection, persistence flags, and one-hop receive.");
}
