/*
 * ===========================================================================
 * File:      tests/weather/api.cpp
 * Firmware:  0.7.0
 * Git base:  b4e2060f2874
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
#include <HTTPClient.h>
#include <freertos/task.h>
#include <LittleFS.h>
#include <cassert>
Stream Serial;FakeFS LittleFS;uint32_t nowMillis=0;int enableLevel=0;bool failQueue=false,failTask=false,bundleUsed=false;
std::vector<PendingTask> tasks;HttpState httpState;
time_t fakeTime(time_t* value){time_t now=1792944000+nowMillis/1000;if(value)*value=now;return now;}
JsonDocument json(const String& body){JsonDocument doc;assert(!deserializeJson(doc,static_cast<const std::string&>(body)));return doc;}
int main(){
 using Type=MDS_ConfigType;using Access=MDS_ConfigAccess;
 const MDS_ConfigSetting settings[]={
  {"schema",Type::Unsigned,Access::ReadOnly,true,nullptr,1,1,1,true},
  {"pwm",Type::Unsigned,Access::ReadWrite,true,nullptr,255,0,255},
  {"gcc",Type::Unsigned,Access::ReadWrite,true,nullptr,4,0,255},
  {"owAppKey",Type::Text,Access::ReadWrite,true,"",0,0,64,false,true,MDS_Weather::validApiKey},
  {"latitude",Type::Text,Access::ReadWrite,true,"",0,0,24,false,false,MDS_Weather::validLatitude},
  {"longitude",Type::Text,Access::ReadWrite,true,"",0,0,24,false,false,MDS_Weather::validLongitude}
 };
 MDS_Config config;assert(config.setSettings(settings));assert(config.begin());TwoWire wire;MatrixDriver matrix(wire,21,4);uint8_t channel[]={0};assert(matrix.begin(channel,1));bool clockReady = false;
 MDS_Weather weather;
 weather.setNetworkReadyCallback([]() {
     // Simulate a connected station for cache API tests.
     return true;
 });
 weather.setClockReadyCallback([&]() {
     // Delay HTTPS until the scenario supplies a usable clock.
     return clockReady;
 });MDS_HTTP http;
 http.setNetworkReadyCallback([]() {
     // Supply a connected network for direct API lifecycle tests.
     return true;
 });
 Display display(matrix);ApplicationApi api(http,config,matrix,display.commands(),&weather);api.begin();http.begin();auto& server=http.server();
 server.request("/api/weather");assert(server.code==200&&String(json(server.body)["state"].as<const char*>())=="disabled"&&!json(server.body)["available"].as<bool>());
 server.request("/api/config",{{"owAppKey","TESTKEY"},{"latitude","42.5"},{"longitude","-71.3"}});assert(server.code==200&&server.body.find("TESTKEY")==String::npos);assert(LittleFS.files["/config.json"].find("TESTKEY")!=String::npos);
 server.request("/api/config",{{"latitude","95"},{"owAppKey","CHANGED"}});assert(server.code==400&&String(config.get("owAppKey").as<const char*>())=="TESTKEY");
 MDS_Weather::Options options;options.apiKey=config.get("owAppKey").as<const char*>();options.latitude=config.get("latitude").as<const char*>();options.longitude=config.get("longitude").as<const char*>();assert(weather.begin(options));clockReady = false;
 weather.tick();
 server.request("/api/weather");assert(String(json(server.body)["state"].as<const char*>())=="waiting_time"&&tasks.empty());
 httpState.payload=R"({"main":{"temp":70.25},"timezone":-14400,"dt":1792944000,"weather":[{"main":"Clear","description":"clear sky"}]})";clockReady = true;
 weather.tick();
 runWorker();clockReady = true;
 weather.tick();
 
 server.request("/api/weather");auto response=json(server.body);assert(server.code==200&&response["fresh"].as<bool>()&&response["temperatureF"].as<float>()==70.25f&&response["timezoneOffsetSeconds"].as<int>()==-14400);assert(server.body.find("TESTKEY")==String::npos);
 server.request("/api/config",{{"owAppKey",""}});assert(server.code==200&&String(config.get("owAppKey").as<const char*>()).empty());
 puts("PASS: optional weather endpoint, background cache reads, hidden key persistence, coordinate validation, atomic rejection, TLS clock gate, and temperature/offset/status JSON.");
}
