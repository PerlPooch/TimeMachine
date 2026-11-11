/*
 * ===========================================================================
 * File:      tests/application/configrouting.cpp
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
#include <type_traits>
#include "fonts/SixteenSegmentAlphanumeric.h"
#include "fonts/SevenSegmentNumeric.h"
Stream Serial;FakeFS LittleFS;FakeSNTP sntp;FakeESP ESP;
int buttonLevel=HIGH;
uint32_t nowMillis=0;int enableLevel=0;time_t utc=1792900800;
bool failQueue=false,failTask=false,bundleUsed=false;
std::vector<PendingTask> tasks;HttpState httpState;
time_t fakeTime(time_t* value){if(value)*value=utc;return utc;}
#include "SerialProtocol.h"
// Exercise HTTP-to-wire configuration delivery and receive dispatch through both actual port queues.
int main() {
#ifdef DEBUG_SERIAL_FRAMES
	constexpr bool traceFrames = true;
#else
	constexpr bool traceFrames = false;
#endif

 WiFi.address="0.0.0.0";
 TwoWire wire; MatrixDriver matrix(wire,21,8); MDS_Display oled; Button button(9); button.begin();
 Application app(Application::Mode::Clock,matrix,oled,button);
 assert(app.begin("test",false,true,true));
 auto& a=*HardwareSerial::ports[1]; auto& b=*HardwareSerial::ports[0];
 auto& server=*WebServer::current;
 a.room=0; b.room=0;
 server.request("/api/config",{{"timeOffset","+5d"},{"to","b"}});
 assert(server.code==202);
 server.request("/api/config",{{"timeOffset","-1s"},{"to","all"}});
 assert(server.code==503);
 server.request("/api/config");
 assert(server.body.find("432000")==String::npos);
 a.room=128; b.room=128; app.tick();
 const char* payload="[\"config1\",{\"timeOffset\":432000}]";
 uint8_t frame[SerialProtocol::BufferBytes];
 size_t length=SerialProtocol::encode(payload,strlen(payload),frame,sizeof(frame));
 assert(a.bytes.empty() && b.bytes.size()==length && !memcmp(b.bytes.data(),frame,length));
 assert((Serial.output.find(std::string("Serial B TX: ") + payload) != std::string::npos) == traceFrames);
 b.bytes.clear();
 for (auto* input : {&a,&b}) {
  Serial.output.clear();
  input->incoming.insert(input->incoming.end(),frame,frame+length);
  for(int pass=0;pass<8;++pass) app.tick();
  server.request("/api/config"); assert(server.body.find("432000")!=String::npos);
  assert((Serial.output.find("432000") != std::string::npos) == traceFrames);
  auto& other=input==&a?b:a;
  assert(other.bytes.empty()); // No forwarded configuration, only a framing response on input.
  const char* status="{\"status\":\"ok\"}";
  uint8_t response[SerialProtocol::BufferBytes];
  size_t responseLength=SerialProtocol::encode(status,strlen(status),response,sizeof(response));
  assert(input->bytes.size()==responseLength && !memcmp(input->bytes.data(),response,responseLength));
  input->bytes.clear();
 }
 server.request("/api/config",{{"timeOffset","-2h"},{"to","all"}});
 assert(server.code==200); app.tick();
 assert(!a.bytes.empty() && a.bytes==b.bytes);
 assert(server.body.find("-7200")!=String::npos && LittleFS.files.empty());
 puts("PASS: configuration HTTP-to-A/B routing, busy rejection, normalized receive, framing-only response, opt-in payload tracing, and no forwarding.");
}
