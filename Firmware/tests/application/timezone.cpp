/*
 * ===========================================================================
 * File:      tests/application/timezone.cpp
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
#include "SystemConfig.h"
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
time_t fakeTime(time_t* value) { if (value) *value = utc; return utc; }

static void expectOffset(int32_t offset) {
 const time_t shifted = utc + offset;
 struct tm expected{}, actual{};
 assert(gmtime_r(&shifted, &expected));
 assert(localtime_r(&utc, &actual));
 assert(actual.tm_year == expected.tm_year && actual.tm_yday == expected.tm_yday);
 assert(actual.tm_hour == expected.tm_hour && actual.tm_min == expected.tm_min && actual.tm_sec == expected.tm_sec);
}

// Verify timezone updates use accepted cache changes even with diagnostics disabled.
static void checkWeatherWithoutStatusCallback() {
 MDS_Config config;
 assert(config.setSettings(SystemConfig::Settings));

 JsonDocument edits;
 edits["owAppKey"] = "TESTKEY";
 edits["latitude"] = "42.5";
 edits["longitude"] = "-71.3";
 edits["timeZone"] = "UTC0";
 assert(config.set(edits));

 MDS_Weather weather;
 weather.setNetworkReadyCallback([]() {
     // Isolate timezone adaptation from connection management.
     return true;
 });
 weather.setClockReadyCallback([]() {
     // Provide a usable TLS clock for these weather-result scenarios.
     return true;
 });
 WeatherController controller(weather, config);
 weather.setStatusCallback(nullptr);

 // Complete a request without advancing UTC, including successive offset changes.
 auto finishQuery = [&](int32_t offset, int status = 200) {
  httpState.status = status;
  httpState.payload = String(std::string("{\"main\":{\"temp\":70.25},\"timezone\":")
   + std::to_string(offset) + ",\"dt\":1792944000,\"weather\":[{\"main\":\"Clear\"}]}");
  controller.tick();
  assert(tasks.size() == 1);
  runWorker();
  controller.tick();
 };

 finishQuery(-14400);
 assert(String(config.get("timeZone").as<const char*>()) == "UTC+04:00:00");
 nowMillis += 600000;
 finishQuery(20700);
 assert(String(config.get("timeZone").as<const char*>()) == "UTC-05:45:00");

 // An idle tick or failed request must not reapply an older cached timezone.
 JsonDocument manual;
 manual["timeZone"] = "UTC0";
 assert(config.set(manual));
 controller.tick();
 assert(String(config.get("timeZone").as<const char*>()) == "UTC0");
 nowMillis += 600000;
 finishQuery(3600, 500);
 assert(String(config.get("timeZone").as<const char*>()) == "UTC0");
 assert(weather.available());

 // An identical result in the same UTC second must still reapply its timezone.
 nowMillis += 60000;
 finishQuery(20700);
 assert(String(config.get("timeZone").as<const char*>()) == "UTC-05:45:00");
 assert(tasks.empty());
 nowMillis = 0;
}

// Exercise application timezone replacement alongside the callback-independent adapter.
int main() {
 checkWeatherWithoutStatusCallback();
 TwoWire wire;
 MatrixDriver matrix(wire, 21);
 MDS_Display oled;
 Button button(9);
 button.begin();
 Application app(Application::Mode::Clock, matrix, oled, button);
 assert(app.begin("test", false, true, true));
 auto& server = *WebServer::current;
 auto config = [&](std::vector<std::pair<String, String>> values) {
  server.request("/api/config", values);
  assert(server.code == 200);
 };
 auto zone = [&]() {
  server.request("/api/config");
  JsonDocument response;
  assert(!deserializeJson(response, static_cast<const std::string&>(server.body)));
  return String(response["timeZone"].as<const char*>());
 };
 config({{"timeZone", "UTC0"}});
 app.tick();
 expectOffset(0);
 // NTP supplies UTC; configuration supplies local conversion until weather succeeds.
 timeval event{};
 event.tv_sec = utc;
 assert(sntp.callback);
 sntp.callback(&event);
 nowMillis += Display::LampDurationMs;
 app.tick();
 config({{"owAppKey", "TESTKEY"}, {"latitude", "42.5"}, {"longitude", "-71.3"}});
 const String saved = LittleFS.files["/config.json"];
 auto finishQuery = [&](const String& payload, int status = 200) {
  httpState.status = status;
  httpState.payload = payload;
  app.tick();
  assert(tasks.size() == 1);
  runWorker();
  app.tick(); // Collect weather and let the controller update the configured timezone.
  app.tick(); // TimeController applies the new setting.
 };
 auto sample = [](int32_t offset) {
  return String(std::string("{\"main\":{\"temp\":70.25},\"timezone\":") + std::to_string(offset)
   + ",\"dt\":1792944000,\"weather\":[{\"main\":\"Clear\"}]}");
 };
 // Positive, negative, zero, and fractional-hour offsets all replace the setting.
 for (const auto offset : {20700, -12600, 0, -14400}) {
  finishQuery(sample(offset));
  expectOffset(offset);
  assert(zone() != "UTC0");
  const int starts = sntp.starts;
  for (int pass = 0; pass < 3; ++pass) app.tick();
  assert(sntp.starts == starts); // An unchanged result does not repeatedly restart SNTP.
  nowMillis += 600000;
 }
 assert(zone() == "UTC+04:00:00");
 // Failed or malformed weather queries cannot replace the last successful setting.
 finishQuery(sample(3600), 500);
 expectOffset(-14400);
 nowMillis += 60000;
 finishQuery("{\"main\":{\"temp\":70.25},\"dt\":1792944000,\"weather\":[{\"main\":\"Clear\"}]}");
 expectOffset(-14400);
 assert(zone() == "UTC+04:00:00");
 assert(LittleFS.files["/config.json"] == saved); // Weather updates do not write flash.
 // Disabling weather does not restore an older timezone.
 config({{"owAppKey", ""}});
 app.tick();
 expectOffset(-14400);
 assert(zone() == "UTC+04:00:00");
 assert(tasks.empty());
 puts("PASS: successful OpenWeather timezone replacement, fractional-hour signs, failed/missing timezone retention, no repeated SNTP restart, and no fallback restoration.");
}
