/*
 * ===========================================================================
 * File:      tests/http/offset.cpp
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

// Exercise strict input parsing, synchronization gating, atomic edits, and volatile storage.
int main() {
 using Type = MDS_ConfigType;
 using Access = MDS_ConfigAccess;
 const MDS_ConfigSetting settings[] = {
  {"name", Type::Text, Access::ReadWrite, true, "Clock", 0, 1, 32},
  {"timeOffset", Type::Signed, Access::ReadWrite, false, nullptr, 0, 0, 0},
  {"signed", Type::Signed, Access::ReadWrite, false, nullptr, 0, 0, 0},
  {"pwm", Type::Unsigned, Access::ReadWrite, true, nullptr, 255, 0, 255},
  {"gcc", Type::Unsigned, Access::ReadWrite, true, nullptr, 4, 0, 255}
 };
 MDS_Config config;
 assert(config.setSettings(settings) && config.begin());
 TwoWire wire;
 MatrixDriver matrix(wire, 21);
 Display display(matrix);
 assert(display.begin() && display.setMessage("UNCHANGED"));
 const auto original = display.frame();
 MDS_HTTP http;
 ApplicationApi api(http, config, matrix, display.commands());
 api.begin();
 auto& server = http.server();
 auto set = [&](const char* text, int code = 200) {
  server.request("/api/config", {{"timeOffset", text}});
  assert(server.code == code);
 };
 auto value = [&]() { return config.get("timeOffset").as<int64_t>(); };
 assert(config.get("timeOffset").is<int64_t>() && value() == 0);

 for (const auto& entry : std::initializer_list<std::pair<const char*, int64_t>>{
  {"+5d",432000}, {"-2h",-7200}, {"+90m",5400}, {"-30s",-30}, {"+0s",0}, {"0",0},
  {"-0d",0}, {"+9223372036854775807s",INT64_MAX}, {"-9223372036854775808s",INT64_MIN},
  {"+106751991167300d",9223372036854720000LL}, {"-106751991167300d",-9223372036854720000LL}}) {
  set(entry.first);
  assert(value() == entry.second);
  auto json = response(server.body);
  assert(json["timeOffset"].is<int64_t>() && json["timeOffset"].as<int64_t>() == entry.second);
 }
 set("+5d"); set("+5d"); assert(value() == 432000); // Sets the delta; does not accumulate.
 assert(LittleFS.files.empty());

 for (const char* invalid : {"", "00", "0s", "5d", "+5", "+d", "+1.5h", "+5D", "+5w", "+1h30m",
  " 5d", " +5d", "+5d ", "--5d", "+-5d", "+9223372036854775808s", "-9223372036854775809s",
  "+106751991167301d", "-106751991167301d", "2026-1-01T00:00:00Z", "2026-01-01 00:00:00Z",
  "2026-01-01T00:00:00", "2026-01-01T00:00:00+00:00", "2026-01-01t00:00:00z",
  "2026-01-01T00:00:00.000Z", "2026-02-29T00:00:00Z", "1900-02-29T00:00:00Z",
  "0000-01-01T00:00:00Z", "2026-00-01T00:00:00Z", "2026-13-01T00:00:00Z",
  "2026-01-00T00:00:00Z", "2026-04-31T00:00:00Z", "2026-01-01T24:00:00Z",
  "2026-01-01T00:60:00Z", "2026-01-01T00:00:60Z"}) {
  const auto before = value();
  set(invalid, 400);
  assert(value() == before);
 }
 set("2026-10-31T12:00:00Z", 503); // No time provider installed.
 bool synchronized = false;
 int64_t utc = 0;
 int timeReads = 0;
 api.setTimeSource([&](int64_t& current) {
  ++timeReads;
  current = utc;
  return synchronized;
 });
 set("2026-10-31T12:00:00Z", 503);
 const int beforeRelative = timeReads;
 set("-1s"); set("0"); assert(value() == 0 && timeReads == beforeRelative); // Relative input requires no clock.
 synchronized = true;
 setenv("TZ", "UTC-14", 1); tzset(); // Absolute dates are UTC regardless of local conversion.
 for (const auto& date : std::initializer_list<std::pair<const char*, int64_t>>{
  {"1970-01-01T00:00:00Z",0}, {"2000-02-29T12:34:56Z",951827696},
  {"1900-03-01T00:00:00Z",-2203891200LL}, {"0001-01-01T00:00:00Z",-62135596800LL},
  {"9999-12-31T23:59:59Z",253402300799LL}}) {
  set(date.first); assert(value() == date.second);
 }
 utc = 1793448000;
 set("2026-10-31T12:00:00Z"); assert(value() == 0);
 set("2026-10-31T11:59:59Z"); assert(value() == -1);
 set("2026-10-31T12:00:01Z"); assert(value() == 1);
 utc = INT64_MIN; set("9999-12-31T23:59:59Z",400);
 utc = INT64_MAX; set("0001-01-01T00:00:00Z",400);

 server.request("/api/config",{{"name","BAD"},{"timeOffset","+5d"},{"timeOffset","-1s"}});
 assert(server.code == 400 && String(config.get("name").as<const char*>()) == "Clock");
 server.request("/api/config",{{"name","BAD"},{"timeOffset","+1.5d"}});
 assert(server.code == 400 && String(config.get("name").as<const char*>()) == "Clock");
 LittleFS.writeOK = false;
 set("+5d"); assert(value() == 432000 && LittleFS.files.empty());
 LittleFS.writeOK = true;
 server.request("/api/config",{{"name","Saved"},{"timeOffset","-1h"}});
 assert(server.code == 200 && value() == -3600);
 assert(LittleFS.files["/config.json"].find("timeOffset") == String::npos);
 MDS_Config rebooted;
 assert(rebooted.setSettings(settings) && rebooted.begin() && rebooted.load());
 assert(rebooted.get("timeOffset").as<int64_t>() == 0);
 server.request("/api/config",{{"signed","-9223372036854775808"}});
 assert(server.code == 200 && config.get("signed").as<int64_t>() == INT64_MIN);

 assert(display.tick());
 const auto after = display.frame();
 assert(memcmp(original.channels, after.channels, sizeof(original.channels)) == 0);
 Display::DisplayCommand pending;
 assert(!display.commands().take(pending));
 puts("PASS: signed relative offsets, strict UTC dates, leap-year/range/overflow rejection, synchronized UTC callback, atomic edits, volatile persistence, and unchanged rendering.");
}
