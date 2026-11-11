/*
 * ===========================================================================
 * File:      tests/application/lifecycle.cpp
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
#include <esp_sntp.h>
#include <HTTPClient.h>
#include <freertos/task.h>
#include <LittleFS.h>
#include <cassert>
#include <algorithm>
#include <type_traits>
#include "fonts/SixteenSegmentAlphanumeric.h"
#include "fonts/SevenSegmentNumeric.h"
Stream Serial;FakeFS LittleFS;FakeSNTP sntp;FakeESP ESP;
int buttonLevel=HIGH;
uint32_t nowMillis=0;int enableLevel=0;time_t utc=1792900800;
bool failQueue=false,failTask=false,bundleUsed=false;
std::vector<PendingTask> tasks;HttpState httpState;
time_t fakeTime(time_t* value){if(value)*value=utc;return utc;}
static_assert(!std::is_default_constructible<Application>::value);
static_assert(!std::is_copy_constructible<Application>::value);
static_assert(!std::is_move_constructible<Application>::value);
void synchronizedTick(Application& app){timeval event{};event.tv_sec=utc;assert(sntp.callback);sntp.callback(&event);app.tick();}
void expectMessage(MatrixDriver& matrix,const char* text){
 for(int index=0;index<13;++index){auto digit=DisplayMap::Digits[index];uint16_t mask=digit.segmentCount==16?SixteenSegment::Fonts::glyph(text[index]):SevenSegment::Fonts::glyph(text[index]);for(int segment=0;segment<digit.segmentCount;++segment)assert(matrix.brightness(digit.firstAddress+segment)==((mask&(1<<segment))?255:0));}
 for(const auto& discrete:DisplayMap::DiscreteMap)assert(matrix.brightness(discrete.address)==0);
}
int main(){
 // Distinguish active provisioning from a saved-network attempt in startup diagnostics.
 struct WiFiStartupCase { bool online; bool saved; const char* expected; };
 for (const auto& startup : {
  WiFiStartupCase{true, false, "Ready"},
  WiFiStartupCase{false, false, "Provisioning"},
  WiFiStartupCase{false, true, "Connecting"}
 }) {
  LittleFS = FakeFS{};
  Serial.output.clear();
  savedWiFi = wifi_config_t{};
  if (startup.saved) {
   strcpy(reinterpret_cast<char*>(savedWiFi.sta.ssid), "Saved Network");
  }

  TwoWire wire;
  MatrixDriver matrix(wire, 21);
  MDS_Display oled;
  Button button(9);
  button.begin();
  Application app(Application::Mode::Clock, matrix, oled, button);
  MDS_WiFi::current->online = startup.online;
  assert(app.begin("test", false, true, true));
  const std::string expected = std::string("Startup: WiFi ") + startup.expected + "\n";
  assert(Serial.output.find(expected) != std::string::npos);
 }
 savedWiFi = wifi_config_t{};

 // Startup seeds primary from the jumper, then loads saved settings; API edits remain writable.
 struct RoleCase {bool jumper;const char* saved;bool expected;};
 for (const auto& role : {
   RoleCase{false,nullptr,false}, RoleCase{true,nullptr,true},
   RoleCase{false,"{\"schema\":1}",false}, RoleCase{true,"{\"schema\":1}",true},
   RoleCase{false,"{\"schema\":1,\"primary\":false}",false},
   RoleCase{false,"{\"schema\":1,\"primary\":true}",true},
   RoleCase{true,"{\"schema\":1,\"primary\":false}",false},
   RoleCase{true,"{\"schema\":1,\"primary\":true}",true},
   RoleCase{true,"{broken",true},
   RoleCase{true,"{\"schema\":1,\"primary\":\"invalid\"}",true}
 }) {
  LittleFS=FakeFS{};Serial.output.clear();
  if(role.saved)LittleFS.files["/config.json"]=role.saved;
  TwoWire wire;MatrixDriver matrix(wire,21);MDS_Display oled;Button button(9);button.begin();
  Application app(Application::Mode::Clock,matrix,oled,button);
  assert(app.begin("test",role.jumper,true,true));
  // Detailed Wi-Fi messages reach Serial without displacing concise OLED status.
  size_t oledCount=oled.messages.size();
  MDS_WiFi::current->callback("WiFi: Retrying saved network.");
  assert(Serial.output.find("WiFi: Retrying saved network.")!=std::string::npos);
  assert(oled.messages.size()==oledCount);
  MDS_WiFi::current->callback("WiFi: Available");
  assert(oled.messages.size()==oledCount+1&&oled.messages.back()=="WiFi: Available");
  auto& server=*WebServer::current;server.request("/api/config");assert(server.code==200);
  JsonDocument response;assert(!deserializeJson(response,static_cast<const std::string&>(server.body)));
  assert(response["primary"].is<bool>()&&response["primary"].as<bool>()==role.expected);
  const char* label=role.expected?"Primary":"Secondary";
  assert(std::find(oled.messages.begin(),oled.messages.end(),String(label))!=oled.messages.end());
  assert(Serial.output.find(std::string("Board: Rev A / ")+label)!=std::string::npos);
  if(role.saved)assert(LittleFS.files["/config.json"]==role.saved);
  else assert(!LittleFS.exists("/config.json"));
  for(bool primary : {false,true}) {
   server.request("/api/config",{{"primary",primary?"true":"false"}});assert(server.code==200);
   JsonDocument saved;assert(!deserializeJson(saved,LittleFS.files["/config.json"]));
   assert(saved["primary"].as<bool>()==primary);
   server.request("/api/config");assert(server.code==200);
   assert(!deserializeJson(response,static_cast<const std::string&>(server.body)));
   assert(response["primary"].as<bool>()==primary);
  }
 }
 LittleFS=FakeFS{};Serial.output.clear();
 // Clock mode, API edits, and service recovery all use the real application/helper code.
 {
  TwoWire wire;MatrixDriver matrix(wire,21,8);MDS_Display oled;Button button(9);button.begin();
  Application app(Application::Mode::Clock,matrix,oled,button);assert(app.state()==Application::State::Uninitialized);app.tick();assert(wire.calls==0&&MDS_WiFi::current->begins==0);
  assert(app.begin("0.5.1",false,true,true));assert(matrix.ready()&&matrix.globalCurrent()==4&&app.state()==Application::State::SegmentTest);
  auto& wifi=*MDS_WiFi::current;auto& server=*WebServer::current;

  // Version is live RO status from the sketch, separate from the persisted schema.
  server.request("/api/config");assert(server.code==200);
  JsonDocument versionResponse;assert(!deserializeJson(versionResponse,static_cast<const std::string&>(server.body)));
  assert(String(versionResponse["version"].as<const char*>())=="0.5.1"&&versionResponse["schema"].as<int>()==1);
  server.request("/api/config",{{"version","old"}});assert(server.code==403&&!LittleFS.exists("/config.json"));
  server.request("/api/config",{{"timeOffset","+5d"}});assert(server.code==200&&!LittleFS.exists("/config.json"));
  server.request("/api/config",{{"timeOffset","1970-01-01T00:00:00Z"}});assert(server.code==503);
  assert(server.registrations==7&&server.starts==1&&wifi.begins==1);
  assert(wifi.policy==MDS_WiFi::ConnectionPolicy::RetrySavedNetworkAsync);
  int calls=wire.calls;app.tick();assert(wire.calls==calls);
  // The API and services remain live while the mode waits for the startup lamp test.
  nowMillis+=Display::LampFadeMs;app.tick();
  for(auto digit:DisplayMap::Digits)for(int s=0;s<digit.segmentCount;++s)assert(matrix.brightness(digit.firstAddress+s)==255);
  for(const auto& discrete:DisplayMap::DiscreteMap)assert(matrix.brightness(discrete.address)==255);
  server.request("/api/display",{{"message","BOOT123"}});assert(server.code==202);
  nowMillis+=Display::LampHoldMs;app.tick();assert(matrix.brightness(DisplayMap::discreteAddress(DisplayMap::DiscreteIndex::AM))==255);
  nowMillis+=Display::LampFadeMs;app.tick();expectMessage(matrix,"BOOT123      ");assert(app.state()==Application::State::Running);
  server.request("/api/display",{{"message",""}});app.tick();
  server.request("/api/display",{{"message","WAI123"}});assert(server.code==202);app.tick();expectMessage(matrix,"WAI123       ");
  server.request("/api/display",{{"message",""}});assert(server.code==202);app.tick();expectMessage(matrix,"-------------");
  // Only a successful sync appends the live IP, after the synchronization notice.
  time_t savedUtc=utc;utc=0;synchronizedTick(app);assert(oled.messages.back()=="Time: invalid NTP timestamp");utc=savedUtc;
  calls=wire.calls;synchronizedTick(app);assert(wire.calls>calls);
  assert(oled.messages[oled.messages.size()-2]=="Time: synchronized"&&oled.messages.back()==WiFi.address);
  // Absolute offsets shift the displayed clock while leaving synchronized system UTC unchanged.
  uint8_t beforeOffset[DisplayMap::ChannelSpan];
  for(size_t index=0;index<sizeof(beforeOffset);++index)beforeOffset[index]=matrix.brightness(index);
  server.request("/api/config",{{"timeOffset","1970-01-01T00:00:00Z"}});assert(server.code==200);
  JsonDocument offsetResponse;assert(!deserializeJson(offsetResponse,static_cast<const std::string&>(server.body)));
  assert(offsetResponse["timeOffset"].as<int64_t>()==-int64_t(utc));
  app.tick();
  bool changed=false;
  for(size_t index=0;index<sizeof(beforeOffset);++index)changed=changed || matrix.brightness(index)!=beforeOffset[index];
  assert(changed && utc==savedUtc);
  server.request("/api/config",{{"timeOffset","+0s"}});assert(server.code==200);app.tick();
  auto syncNotices=oled.messages.size();calls=wire.calls;++utc;nowMillis+=1000;app.tick();assert(wire.calls>calls&&oled.messages.size()==syncNotices);
  WiFi.address="10.0.0.42";synchronizedTick(app);assert(oled.messages.back()=="10.0.0.42");

  // A backward clock correction is a new sync; recovery can reuse its timestamp.
  const time_t beforeCorrection = utc;
  utc -= 60;
  synchronizedTick(app);
  assert(oled.messages.back() == WiFi.address);
  const time_t correctedUtc = utc;
  utc = 0;
  synchronizedTick(app);
  assert(oled.messages.back() == "Time: invalid NTP timestamp");
  utc = correctedUtc;
  synchronizedTick(app);
  assert(oled.messages.back() == WiFi.address);
  utc = beforeCorrection;

  server.request("/api/config",{{"ntpServer1","test.pool.example"},{"pwm","100"}});assert(server.code==200&&matrix.brightnessLimit()==100);
  assert(LittleFS.files["/config.json"].find("\"version\"")==String::npos);
  server.request("/api/config",{{"gcc","12"}});assert(server.code==200&&matrix.globalCurrent()==12&&wire.regs[3][1]==12);
  app.tick();assert(sntp.servers[0]=="test.pool.example");
  server.request("/api/display",{{"message","ABC1234567890"}});assert(server.code==202);app.tick();expectMessage(matrix,"ABC1234567890");
  assert(LittleFS.files["/config.json"].find("message")==String::npos);
  calls=wire.calls;++utc;app.tick();assert(wire.calls==calls);
  auto notices=oled.messages.size();server.request("/api/display",{{"message","ERR1x3@5~7!90"}});assert(server.code==202);app.tick();expectMessage(matrix,"ERR1 3 5 7 90");assert(oled.messages.size()==notices);
  calls=wire.calls;app.tick();assert(wire.calls==calls&&oled.messages.size()==notices);
  server.request("/api/display",{{"message","TXTAbCd-_*=90"}});assert(server.code==202);app.tick();expectMessage(matrix,"TXTAbCd-_*=90");assert(oled.messages.size()==notices);
  server.request("/api/display",{{"message","ABC "}});assert(server.code==202);app.tick();expectMessage(matrix,"ABC          ");assert(oled.messages.size()==notices);
  server.request("/api/display",{{"message","a+?AbCd-_*=90"}});assert(server.code==202);app.tick();expectMessage(matrix,"a+?AbCd-_*=90");assert(oled.messages.size()==notices);
  server.request("/api/display",{{"message","{}:xxxxxxxxxx"}});assert(server.code==202);app.tick();expectMessage(matrix,"             ");assert(oled.messages.size()==notices);
  server.request("/api/display",{{"message","HI"}});assert(server.code==202);app.tick();expectMessage(matrix,"HI           ");
  server.request("/api/display",{{"message","12345678901234"}});assert(server.code==400);app.tick();expectMessage(matrix,"HI           ");
  server.request("/api/display",{{"message","ABC"},{"pwm","bad"}});assert(server.code==400);app.tick();expectMessage(matrix,"HI           ");
  server.request("/api/display",{{"message",""}});assert(server.code==202);calls=wire.calls;app.tick();assert(wire.calls>calls&&matrix.brightness(DisplayMap::discreteAddress(DisplayMap::DiscreteIndex::AM))==92);
  wifi.online=false;app.tick();assert(server.stops==1);synchronizedTick(app);assert(oled.messages.back()=="Time: synchronized");app.tick();assert(server.stops==1);
  wifi.online=true;app.tick();assert(server.starts==2);
  calls=wire.calls;wire.failAt=calls+1;++utc;nowMillis+=1000;app.tick();assert(!matrix.ready()&&app.state()==Application::State::DisplayFault);
  int ticks=server.ticks;++utc;app.tick();assert(server.ticks==ticks+1);
  wifi.reboot=true;ticks=server.ticks;app.tick();assert(ESP.restarts==1&&server.ticks==ticks&&app.state()==Application::State::Rebooting);int restarts=ESP.restarts;app.tick();assert(ESP.restarts==restarts&&server.ticks==ticks);
 }
 // Saved GCC is applied before the startup lamp test.
 {
  LittleFS=FakeFS{};LittleFS.files["/config.json"]="{\"schema\":1,\"name\":\"Saved\",\"gcc\":7,\"version\":\"obsolete\",\"timeOffset\":123}";
  TwoWire wire;MatrixDriver matrix(wire,21);MDS_Display oled;Button button(9);button.begin();
  Application app(Application::Mode::Clock,matrix,oled,button);assert(app.begin("test",true,true,true));
  assert(matrix.globalCurrent()==7&&matrix.configuredCurrent()==7&&wire.regs[3][1]==7);
  WebServer::current->request("/api/config");assert(WebServer::current->body.find("obsolete")==String::npos);
  JsonDocument restored;assert(!deserializeJson(restored,static_cast<const std::string&>(WebServer::current->body)));
  assert(restored["timeOffset"].as<int64_t>()==0);
 }
 // Display commands wait for startup, scroll without clock overwrite, and restore content.
 {
  LittleFS=FakeFS{};TwoWire wire;MatrixDriver matrix(wire,21);MDS_Display oled;Button button(9);button.begin();
  Application app(Application::Mode::Clock,matrix,oled,button);assert(app.begin("test",true,true,true));auto& server=*WebServer::current;
  server.request("/api/display",{{"scroll","123"},{"speed","10"},{"direction","right"}});assert(server.code==202&&app.state()==Application::State::SegmentTest);
  nowMillis+=Display::LampDurationMs;app.tick();assert(app.state()==Application::State::Running);expectMessage(matrix,"3            ");
  int calls=wire.calls;nowMillis+=9;app.tick();assert(wire.calls==calls);nowMillis+=1;app.tick();expectMessage(matrix,"23           ");
  server.request("/api/config");assert(server.code==200);++utc;app.tick();expectMessage(matrix,"23           ");
  nowMillis+=140;app.tick();expectMessage(matrix,"-------------");assert(!LittleFS.exists("/config.json"));
  server.request("/api/display",{{"message","HOME"}});assert(server.code==202);app.tick();expectMessage(matrix,"HOME         ");
  server.request("/api/display",{{"scroll","123"},{"speed","10"}});app.tick();expectMessage(matrix,"            1");
  nowMillis+=10;app.tick();expectMessage(matrix,"           12");
  server.request("/api/display",{{"scroll","123"},{"speed","10"}});app.tick();expectMessage(matrix,"            1"); // Identical request restarts.
  nowMillis+=150;app.tick();expectMessage(matrix,"HOME         ");
  server.request("/api/display",{{"scroll","123"}});app.tick();
  server.request("/api/display",{{"message","HOME"}});app.tick();expectMessage(matrix,"HOME         "); // Same message cancels scroll.
  server.request("/api/display",{{"scroll",""}});app.tick();expectMessage(matrix,"HOME         ");
  server.request("/api/display",{{"message",""}});app.tick();expectMessage(matrix,"-------------");
  server.request("/api/display",{{"slide","123"},{"speed","10"}});assert(server.code==202);app.tick();expectMessage(matrix,"            1");
  nowMillis+=120;app.tick();expectMessage(matrix,"123          ");
  nowMillis+=10000;++utc;app.tick();expectMessage(matrix,"123          "); // Settled slide remains visible.
  server.request("/api/display",{{"scroll","456"},{"speed","10"}});app.tick();nowMillis+=150;app.tick();expectMessage(matrix,"123          ");
  server.request("/api/display",{{"slide","456"},{"speed","10"},{"direction","right"}});app.tick();expectMessage(matrix,"6            ");
  nowMillis+=120;app.tick();expectMessage(matrix,"          456");
  nowMillis+=10000;app.tick();expectMessage(matrix,"          456");
  server.request("/api/display",{{"message",""}});app.tick();expectMessage(matrix,"-------------");
  assert(!LittleFS.exists("/config.json"));
 }
 // Tests mode advances diagnostics without servicing HTTP or running clock rendering.
 {
  LittleFS=FakeFS{};TwoWire wire;MatrixDriver matrix(wire,21,8);MDS_Display oled;Button button(9);button.begin();
  Application app(Application::Mode::Tests,matrix,oled,button);assert(app.begin("test",true,true,true));
  nowMillis+=Display::LampDurationMs;app.tick();synchronizedTick(app);int calls=wire.calls;++utc;app.tick();assert(wire.calls==calls);
  nowMillis+=150;app.tick();assert(wire.calls>calls);
  assert(WebServer::current->ticks==0&&oled.ticks>=3);
 }
 // Lamp-test I2C failure stops startup rendering while services remain available.
 {
  LittleFS=FakeFS{};TwoWire wire;MatrixDriver matrix(wire,21,8);MDS_Display oled;Button button(9);button.begin();
  Application app(Application::Mode::Clock,matrix,oled,button);assert(app.begin("test",false,true,true));
  wire.failAt=wire.calls+1;nowMillis+=175;app.tick();assert(!matrix.ready()&&enableLevel==LOW&&app.state()==Application::State::DisplayFault);
  int calls=wire.calls;int ticks=WebServer::current->ticks;nowMillis+=2000;app.tick();assert(wire.calls==calls&&WebServer::current->ticks==ticks+1);
  WebServer::current->request("/api/display",{{"message","FAIL"}});assert(WebServer::current->code==202);
 }
 // Missing I2C/OLED must leave API, network, time, and configuration recovery alive.
 {
  LittleFS=FakeFS{};LittleFS.mountOK=false;TwoWire wire;MatrixDriver matrix(wire,21,8);MDS_Display oled;Button button(9);button.begin();
  Application app(Application::Mode::Clock,matrix,oled,button);assert(!app.begin("test",false,false,false));
  assert(LittleFS.formatted&&wire.calls==0&&app.state()==Application::State::DisplayFault);app.tick();synchronizedTick(app);
  assert(WebServer::current->starts==1&&WebServer::current->ticks==2&&wire.calls==0&&oled.ticks==0&&oled.messages.empty());
  WebServer::current->request("/api/config",{{"name","Recovered"}});assert(WebServer::current->code==200);
 }
 assert(tasks.empty());
 puts("PASS: explicit/noncopyable application, clock/test dispatch, shared NTP/API services, config edits, filesystem recovery, HTTP reconnect, display failure isolation, and reboot dispatch.");
}
