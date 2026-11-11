/*
 * ===========================================================================
 * File:      tests/application/states.cpp
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
#include <esp_sntp.h>
#include <HTTPClient.h>
#include <freertos/task.h>
#include <LittleFS.h>
#include <cassert>
#include <array>
Stream Serial;FakeFS LittleFS;FakeSNTP sntp;FakeESP ESP;
uint32_t nowMillis=0;int enableLevel=0,buttonLevel=HIGH;time_t utc=1792900800;
bool failQueue=false,failTask=false,bundleUsed=false;
std::vector<PendingTask> tasks;HttpState httpState;
time_t fakeTime(time_t* value){if(value)*value=utc;return utc;}
using State=Application::State;
void fresh(){LittleFS=FakeFS{};ESP.restarts=0;buttonLevel=HIGH;nowMillis=0;}
void press(Application& app){buttonLevel=LOW;++nowMillis;app.tick();nowMillis+=30;app.tick();}
void release(Application& app){buttonLevel=HIGH;++nowMillis;app.tick();nowMillis+=30;app.tick();}
void finishStartup(Application& app){nowMillis+=Display::LampDurationMs;app.tick();assert(app.state()==State::Running);}
std::array<uint8_t,128> snapshot(MatrixDriver& matrix){std::array<uint8_t,128> result{};for(int i=0;i<128;++i)result[i]=matrix.brightness(i);return result;}
int main(){
 // begin() is idempotent during startup, running, and reset hold; it does not restart modes.
 fresh();{
  TwoWire wire;MatrixDriver matrix(wire,21,8);MDS_Display oled;Button button(9);button.begin();Application app(Application::Mode::Clock,matrix,oled,button);
  assert(app.state()==State::Uninitialized);app.tick();assert(MDS_WiFi::current->begins==0);
  assert(app.begin("test",false,true,true)&&app.state()==State::SegmentTest);
  int calls=wire.calls;assert(app.begin("test",false,true,true)&&wire.calls==calls&&MDS_WiFi::current->begins==1);
  finishStartup(app);auto saved=snapshot(matrix);calls=wire.calls;assert(app.begin("test",false,true,true)&&wire.calls==calls);
  press(app);assert(app.state()==State::ResetHeld);calls=wire.calls;assert(app.begin("test",false,true,true)&&wire.calls==calls);
  release(app);assert(app.state()==State::Running&&snapshot(matrix)==saved);
  MDS_WiFi::current->online=false;app.tick();assert(app.state()==State::Running);MDS_WiFi::current->online=true;app.tick();assert(app.state()==State::Running);
 }
 // A short press during the segment test cancels it and resumes the selected mode.
 fresh();{
  TwoWire wire;MatrixDriver matrix(wire,21,8);MDS_Display oled;Button button(9);button.begin();Application app(Application::Mode::Tests,matrix,oled,button);
  assert(app.begin("test",false,true,true));nowMillis+=Display::LampFadeMs;app.tick();press(app);assert(app.state()==State::ResetHeld);
  release(app);assert(app.state()==State::Running);nowMillis+=Display::LampDurationMs;app.tick();assert(app.state()==State::Running);
 }
 // Missing display capability keeps services and the reset gesture available; release returns to fault.
 fresh();{
  TwoWire wire;MatrixDriver matrix(wire,21,8);MDS_Display oled;Button button(9);button.begin();Application app(Application::Mode::Clock,matrix,oled,button);
  assert(!app.begin("test",false,false,true)&&app.state()==State::DisplayFault);
  int ticks=WebServer::current->ticks;press(app);assert(app.state()==State::ResetHeld&&WebServer::current->ticks>ticks);
  assert(!app.begin("test",false,true,true));release(app);assert(app.state()==State::DisplayFault&&wire.calls==0);
  ticks=WebServer::current->ticks;app.tick();assert(WebServer::current->ticks==ticks+1);
 }
 // Driver initialization failure and selected-mode initialization failure both settle in DisplayFault.
 for(bool initialFailure:{true,false}){
  fresh();TwoWire wire;MatrixDriver matrix(wire,21,8);MDS_Display oled;Button button(9);button.begin();Application app(Application::Mode::Clock,matrix,oled,button);
  if(initialFailure)wire.failAt=1;
  bool started=app.begin("test",false,true,true);assert(started!=initialFailure);
  if(!initialFailure){wire.failAt=wire.calls+1;nowMillis+=Display::LampDurationMs;app.tick();}
  assert(app.state()==State::DisplayFault&&!matrix.ready());int calls=wire.calls;int ticks=WebServer::current->ticks;app.tick();assert(wire.calls==calls&&WebServer::current->ticks==ticks+1);
 }
 // A mode write failure is reflected in the application state within the same tick.
 fresh();{
  TwoWire wire;MatrixDriver matrix(wire,21,8);MDS_Display oled;Button button(9);button.begin();Application app(Application::Mode::Clock,matrix,oled,button);
  assert(app.begin("test",false,true,true));finishStartup(app);wire.failAt=wire.calls+1;
  WebServer::current->request("/api/display",{{"message","ERR123"}});assert(WebServer::current->code==202);app.tick();assert(app.state()==State::DisplayFault);
 }
 // If canceling the startup overlay fails, a released reset hold returns to DisplayFault.
 fresh();{
  TwoWire wire;MatrixDriver matrix(wire,21,8);MDS_Display oled;Button button(9);button.begin();Application app(Application::Mode::Clock,matrix,oled,button);
  assert(app.begin("test",false,true,true));nowMillis+=Display::LampFadeMs;app.tick();wire.failAt=wire.calls+1;
  press(app);assert(app.state()==State::ResetHeld&&!matrix.ready());release(app);assert(app.state()==State::DisplayFault);
 }
 // A network reboot during a hold enters a terminal state, even if fake ESP.restart returns.
 fresh();{
  TwoWire wire;MatrixDriver matrix(wire,21,8);MDS_Display oled;Button button(9);button.begin();Application app(Application::Mode::Clock,matrix,oled,button);
  assert(app.begin("test",false,true,true));press(app);assert(app.state()==State::ResetHeld);
  MDS_WiFi::current->reboot=true;app.tick();assert(app.state()==State::Rebooting&&ESP.restarts==1);int ticks=WebServer::current->ticks;
  release(app);app.tick();assert(app.state()==State::Rebooting&&ESP.restarts==1&&WebServer::current->ticks==ticks);
 }
 puts("PASS: explicit lifecycle states, idempotent startup/resume, segment-test cancellation, display capability/init/mode/write faults, reset return states, service continuity, and terminal reboot dispatch.");
}
