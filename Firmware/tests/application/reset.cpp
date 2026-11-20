/*
 * ===========================================================================
 * File:      tests/application/reset.cpp
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

#include "Application.h"
#include <MDS_Display.h>
#include <esp_sntp.h>
#include <HTTPClient.h>
#include <freertos/task.h>
#include <LittleFS.h>
#include "fonts/SixteenSegmentAlphanumeric.h"
#include <cassert>
Stream Serial;FakeFS LittleFS;FakeSNTP sntp;FakeESP ESP;
uint32_t nowMillis=0;int enableLevel=0,buttonLevel=HIGH;time_t utc=1792900800;
bool failQueue=false,failTask=false,bundleUsed=false;
std::vector<PendingTask> tasks;HttpState httpState;
time_t fakeTime(time_t* value){if(value)*value=utc;return utc;}
void at(Application& app,uint32_t when,int level){nowMillis=when;buttonLevel=level;app.tick();}
void press(Application& app,uint32_t when){at(app,when,LOW);at(app,when+30,LOW);}
void release(Application& app,uint32_t when){at(app,when,HIGH);at(app,when+30,HIGH);}
void rst(MatrixDriver& matrix){
 const char* text="RST";
 for(int d=0;d<3;++d){auto digit=DisplayMap::Digits[d];auto mask=SixteenSegment::Fonts::glyph(text[d]);for(int s=0;s<16;++s)assert(matrix.brightness(digit.firstAddress+s)==((mask&(1<<s))?255:0));}
}
void fresh(){LittleFS=FakeFS{};ESP.restarts=0;buttonLevel=HIGH;nowMillis=0;}

// Keep text, nested image assets, and an unrelated file byte-for-byte intact.
const std::map<std::string, std::string> Website = {
 {"/www/index.html", "<html>Time Machine</html>"},
 {"/www/assets/style.css", "body { color: green; }"},
 {"/www/assets/app.js", "console.log('ready');"},
 {"/www/assets/logo.png", std::string("\x89PNG\0\x01", 6)},
 {"/keep.txt", "unrelated file"}
};

void seedWebsite() {
 for (const auto& file : Website) {
  LittleFS.files[file.first] = file.second;
 }
}

void expectWebsite() {
 for (const auto& file : Website) {
  assert(LittleFS.files.at(file.first) == file.second);
 }
 assert(LittleFS.formats == 0 && !LittleFS.formatted);
}

int main(){
 fresh();{
  TwoWire wire;MatrixDriver matrix(wire,21,8);MDS_Display oled;Button button(9);button.begin();
  Application app(Application::Mode::Clock,matrix,oled,button);assert(app.begin("test",false,true,true));
  nowMillis+=Display::LampDurationMs;app.tick();nowMillis=0;
  timeval event{};event.tv_sec=utc;app.tick();sntp.callback(&event);app.tick();
  WebServer::current->request("/api/config",{{"name","Saved"}});assert(!LittleFS.files.empty());
  seedWebsite();
  LittleFS.files["/config.json.tmp"] = "unfinished save";
  uint8_t saved[128];for(int i=0;i<128;++i)saved[i]=matrix.brightness(i);
  int calls=wire.calls;at(app,100,LOW);at(app,110,HIGH);at(app,115,LOW);at(app,125,HIGH);at(app,160,HIGH);assert(wire.calls==calls&&LittleFS.formats==0);
  press(app,200);assert(app.state()==Application::State::ResetHeld);rst(matrix);int ticks=WebServer::current->ticks;
  at(app,5229,LOW);rst(matrix);assert(LittleFS.formats==0&&ESP.restarts==0&&WebServer::current->ticks>ticks);
  // Release just before five seconds; release debounce must not turn it into a reset.
  at(app,5229,HIGH);at(app,5230,HIGH);assert(LittleFS.formats==0);at(app,5259,HIGH);
  assert(app.state()==Application::State::Running);for(int i=0;i<128;++i)if(i!=DisplayMap::discreteAddress(DisplayMap::DiscreteIndex::Colon1)&&i!=DisplayMap::discreteAddress(DisplayMap::DiscreteIndex::Colon2))assert(saved[i]==matrix.brightness(i));assert(matrix.brightness(DisplayMap::discreteAddress(DisplayMap::DiscreteIndex::Colon1))==0&&matrix.brightness(DisplayMap::discreteAddress(DisplayMap::DiscreteIndex::Colon2))==0);assert(!LittleFS.files.empty());
  press(app,6000);rst(matrix);at(app,11029,LOW);assert(LittleFS.formats==0);at(app,11030,LOW);
  assert(app.state() == Application::State::Rebooting);
  assert(LittleFS.formats == 0 && LittleFS.removes == 2);
  assert(LittleFS.files == Website);
  assert(MDS_WiFi::current->erases == 1 && ESP.restarts == 1);
  for(int i=0;i<128;++i)assert(matrix.brightness(i)==0);
  at(app,20000,LOW);assert(LittleFS.removes==2&&ESP.restarts==1);
  expectWebsite();
 }
 // A fresh application loads defaults after reset; website files remain available.
 {
  buttonLevel = HIGH;
  TwoWire wire;
  MatrixDriver matrix(wire, 21, 8);
  MDS_Display oled;
  Button button(9);
  button.begin();

  Application app(Application::Mode::Clock, matrix, oled, button);
  assert(app.begin("test", false, true, true));
  WebServer::current->request("/api/config");

  JsonDocument response;
  assert(!deserializeJson(response, static_cast<const std::string&>(WebServer::current->body)));
  assert(String(response["name"].as<const char*>()) == "Time Machine");
  assert(!LittleFS.exists("/config.json") && !LittleFS.exists("/config.json.tmp"));
  expectWebsite();
 }
 // Both modes use the same gesture; elapsed hold timing survives millis wrap.
 fresh();{
  TwoWire wire;MatrixDriver matrix(wire,21,8);MDS_Display oled;Button button(9);button.begin();
  Application app(Application::Mode::Tests,matrix,oled,button);assert(app.begin("test",false,true,true));
  seedWebsite();
  uint32_t start=0xfffffff0U;press(app,start);rst(matrix);at(app,start+5029,LOW);assert(LittleFS.formats==0);
  at(app,start+5030,LOW);assert(LittleFS.removes==0&&MDS_WiFi::current->erases==1&&ESP.restarts==1);
  expectWebsite();
 }
 // Each deletion failure and Wi-Fi erase failure stops safely; release permits retry.
 for (const char* failure : {"/config.json.tmp", "/config.json", "wifi"}) {
  fresh();
  TwoWire wire;
  MatrixDriver matrix(wire, 21, 8);
  MDS_Display oled;
  Button button(9);
  button.begin();

  Application app(Application::Mode::Clock, matrix, oled, button);
  assert(app.begin("test", false, true, true));
  seedWebsite();
  LittleFS.files["/config.json"] = "{\"schema\":1,\"name\":\"Saved\"}";
  LittleFS.files["/config.json.tmp"] = "unfinished save";

  const bool wifiFailure = std::string(failure) == "wifi";
  LittleFS.removeFailurePath = wifiFailure ? "" : failure;
  MDS_WiFi::current->eraseOK = !wifiFailure;

  press(app, 100);
  at(app, 5130, LOW);
  assert(ESP.restarts == 0 && app.state() == Application::State::ResetFailed);
  assert(WebServer::current->stops == 1 && WebServer::current->starts == 1);
  assert(MDS_WiFi::current->erases == (wifiFailure ? 1 : 0));
  expectWebsite();

  // Holding longer does not repeat failed operations or resume HTTP handling.
  const int removes = LittleFS.removes;
  const int erases = MDS_WiFi::current->erases;
  const int ticks = WebServer::current->ticks;
  at(app, 6000, LOW);
  assert(LittleFS.removes == removes && MDS_WiFi::current->erases == erases);
  assert(WebServer::current->ticks == ticks);

  release(app, 6100);
  assert(app.state() == Application::State::Running);
  assert(WebServer::current->starts == 2);
  LittleFS.removeFailurePath.clear();
  MDS_WiFi::current->eraseOK = true;

  press(app, 7000);
  at(app, 12030, LOW);
  assert(app.state() == Application::State::Rebooting && ESP.restarts == 1);
  assert(LittleFS.files == Website);
  expectWebsite();
 }
 puts("PASS: reset gesture/cancellation, config and temporary-file deletion, website preservation, defaults after reboot, missing files, both modes, rollover, deletion/Wi-Fi failures, and retry.");
}
