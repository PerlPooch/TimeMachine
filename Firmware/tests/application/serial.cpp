/*
 * ===========================================================================
 * File:      tests/application/serial.cpp
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
size_t pingCount(const HardwareSerial& port){
 size_t offset=0,count=0;
 while(offset<port.bytes.size()){
  size_t length=port.bytes[offset++];assert(offset+length<port.bytes.size());uint32_t timestamp;
  assert(SerialProtocol::decodePing(port.bytes.data()+offset,length,timestamp));assert(port.bytes[offset+length]=='\n');offset+=length+1;++count;
 }
 return count;
}
int main(){
 // This fixture exercises command routing; address announcements have separate coverage.
 WiFi.address="0.0.0.0";
 uint8_t frame[SerialProtocol::BufferBytes];
 assert(SerialProtocol::encode("ping",4,frame,sizeof(frame))==6);
 const uint8_t expected[]={4,'p','i','n','g','\n'};
 assert(!memcmp(frame,expected,6)&&frame[6]==0);
 for(uint32_t timestamp:{0u,1000u,UINT32_MAX}){
  auto length=SerialProtocol::encodePing(timestamp,frame,sizeof(frame));uint32_t decoded=0;
  assert(length&&SerialProtocol::decodePing(frame+1,frame[0],decoded)&&decoded==timestamp);
 }
 for(const char* invalid:{"ping ","ping -1","ping 4294967296","ping 12x","ping 1 2"}){
  uint32_t decoded=77;assert(!SerialProtocol::decodePing(reinterpret_cast<const uint8_t*>(invalid),strlen(invalid),decoded)&&decoded==77);
 }
 char maximum[SerialProtocol::MaxPayloadBytes+1];memset(maximum,'x',sizeof(maximum));
 assert(SerialProtocol::encode(maximum,246,frame,sizeof(frame))==248&&frame[248]==0);
 assert(!SerialProtocol::encode(maximum,247,frame,sizeof(frame)));
 assert(!SerialProtocol::encode("ping",4,frame,6));
 assert(!SerialProtocol::encode(nullptr,4,frame,sizeof(frame)));
 TwoWire wire;MatrixDriver matrix(wire,21,8);MDS_Display oled;Button button(9);button.begin();
 Application app(Application::Mode::Clock,matrix,oled,button);assert(app.begin("test",false,true,true));
 auto& port=*HardwareSerial::ports[1];
 auto& portB=*HardwareSerial::ports[0];assert(portB.baud==115200&&portB.rx==20&&portB.tx==18);assert(port.uart==1&&port.baud==115200&&port.rx==0&&port.tx==1);
 app.tick();assert(port.bytes.empty());
 auto& server=*WebServer::current;
 server.request("/api/config",{{"primary","true"}});assert(server.code==200);
 nowMillis=0xfffffff0U;app.tick();assert(pingCount(port)==1);
 nowMillis+=999;app.tick();assert(pingCount(port)==1);
 nowMillis+=1;app.tick();assert(pingCount(port)==2);
 nowMillis+=5000;app.tick();assert(pingCount(port)==3); // No catch-up burst.
 port.room=0;nowMillis+=1000;app.tick();assert(pingCount(port)==3&&pingCount(portB)==4);
 port.room=128;app.tick();assert(pingCount(port)==4);
 server.request("/api/config",{{"primary","false"}});assert(server.code==200);
 nowMillis+=1000;app.tick();assert(pingCount(port)==4);
 server.request("/api/config",{{"primary","true"}});app.tick();assert(pingCount(port)==5);
 assert(pingCount(portB)==5);
 // Frame tracing is optional on either port, for primary and secondary roles.
#ifdef DEBUG_SERIAL_FRAMES
 constexpr bool traceFrames = true;
#else
 constexpr bool traceFrames = false;
#endif
 for(const char* role:{"false","true"}){
  server.request("/api/config",{{"primary",role}});assert(server.code==200);
  for(auto input:{&port,&portB}){
   Serial.output.clear();input->incoming.insert(input->incoming.end(),expected,expected+3);app.tick();assert(Serial.output.find(" RX: ")==std::string::npos);
   input->incoming.insert(input->incoming.end(),expected+3,expected+6);app.tick();
   const char* rx = input == &port ? "Serial A RX: ping\n" : "Serial B RX: ping\n";
   const char* tx = input == &port ? "Serial A TX: {\"status\":\"ok\"}\n" : "Serial B TX: {\"status\":\"ok\"}\n";
   assert((Serial.output.find(rx) != std::string::npos) == traceFrames);
   assert((Serial.output.find(tx) != std::string::npos) == traceFrames);
   uint8_t other[]={3,'a',0,'b','\n'};Serial.output.clear();input->incoming.insert(input->incoming.end(),other,other+5);app.tick();
   assert((Serial.output.find(std::string(" RX: a\0b\n",9))!=std::string::npos) == traceFrames);
   uint8_t bad[]={247,'x','\n',4,'p','i','n','g','!','\n',4,'p','i','n','g','\n'};
   Serial.output.clear();input->incoming.insert(input->incoming.end(),bad,bad+sizeof(bad));for(int pass=0;pass<3;++pass)app.tick();
   assert(Serial.output.find("invalid length")!=std::string::npos&&Serial.output.find("expected newline terminator")!=std::string::npos);
   assert((Serial.output.find(rx) != std::string::npos) == traceFrames);
   Serial.output.clear();input->incoming.push_back(4);input->incoming.push_back('p');app.tick();nowMillis+=100;app.tick();assert(Serial.output.find("frame timeout")!=std::string::npos);
   input->incoming.insert(input->incoming.end(),expected,expected+6);app.tick();assert((Serial.output.find(rx) != std::string::npos) == traceFrames);
  }
 }
 // Parser boundaries: full payload, newline-valued length, timeout rollover.
 SerialProtocol::Receiver parser;using Result=SerialProtocol::Receiver::Result;
 assert(parser.feed(246,0)==Result::None);for(int i=0;i<246;++i)assert(parser.feed('x',0)==Result::None);
 assert(parser.feed('\n',0)==Result::Message&&parser.length()==246&&parser.payload()[246]==0);
 assert(parser.feed(10,0)==Result::None);for(int i=0;i<10;++i)assert(parser.feed('\n',0)==Result::None);assert(parser.feed('\n',0)==Result::Message);
 assert(parser.feed(4,0xfffffff0U)==Result::None);assert(parser.expire(0x53)==Result::None);assert(parser.expire(0x54)==Result::Timeout);

 // Remote-only HTTP commands queue both ports and leave the local display alone.
 server.request("/api/config",{{"primary","false"}});app.tick();
 nowMillis+=Display::LampDurationMs;app.tick();assert(app.state()==Application::State::Running);
 auto inject=[&](HardwareSerial& input,const char* text){
  uint8_t framed[SerialProtocol::BufferBytes];auto size=SerialProtocol::encode(text,strlen(text),framed,sizeof(framed));assert(size);
  input.incoming.insert(input.incoming.end(),framed,framed+size);app.tick();
 };
 auto expectText=[&](const char* text){
  for(int index=0;index<13;++index){auto digit=DisplayMap::Digits[index];uint16_t mask=digit.segmentCount==16?SixteenSegment::Fonts::glyph(text[index]):SevenSegment::Fonts::glyph(text[index]);for(int segment=0;segment<digit.segmentCount;++segment)assert(matrix.brightness(digit.firstAddress+segment)==((mask&(1<<segment))?255:0));}
 };
 server.request("/api/display",{{"message","HOME"}});app.tick();expectText("HOME         ");
 port.bytes.clear();portB.bytes.clear();port.room=8;portB.room=0;
 server.request("/api/display",{{"message","AWAY"},{"to","a"},{"to","b"}});assert(server.code==202);
 server.request("/api/display",{{"message","FAIL"},{"to","all"}});assert(server.code==503);
 app.tick();assert(port.bytes.size()==8&&portB.bytes.empty());expectText("HOME         ");
 // A can finish independently; B retains its pending frame without blocking.
 for(int i=0;i<40;++i)app.tick();assert(port.bytes.size()>8&&portB.bytes.empty());
 portB.room=128;app.tick();assert(port.bytes==portB.bytes);
 assert(port.bytes[0]+2u==port.bytes.size()&&port.bytes.back()=='\n');
 // Feeding that wire frame into either port executes locally and only acknowledges receipt.
 port.room=128;
 const auto routedFrame=port.bytes;
 uint8_t acknowledgment[SerialProtocol::StatusBufferBytes];
 const auto acknowledgmentLength=SerialProtocol::encodeStatus(SerialProtocol::Status::Ok,acknowledgment,sizeof(acknowledgment));
 for(auto input:{&port,&portB}){
  auto aCount=port.bytes.size(),bCount=portB.bytes.size();input->incoming.insert(input->incoming.end(),routedFrame.begin(),routedFrame.end());app.tick();expectText("AWAY         ");
  assert(port.bytes.size()==aCount+(input==&port?acknowledgmentLength:0));
  assert(portB.bytes.size()==bCount+(input==&portB?acknowledgmentLength:0));
  assert(std::equal(input->bytes.end()-acknowledgmentLength,input->bytes.end(),acknowledgment));
 }
 inject(port,"[\"display1\",\"message\",\"WRONG\",0,\"left\"]");expectText("AWAY         ");assert(Serial.output.find("error: invalid display command")!=std::string::npos);
 port.room=128;
 server.request("/api/display",{{"message","BOTH"},{"to","local"},{"to","b"}});assert(server.code==202);app.tick();expectText("BOTH         ");

 // The all alias renders locally and sends exactly one identical frame on each port.
 port.bytes.clear();portB.bytes.clear();
 server.request("/api/display",{{"message","ALL"},{"to","all"},{"to","b"},{"to","all"}});
 assert(server.code==202);app.tick();expectText("ALL          ");
 assert(!port.bytes.empty()&&port.bytes==portB.bytes);
 assert(port.bytes[0]+2u==port.bytes.size());

 // All animation options survive encoding and decoding.
 for(const char* kind:{"scroll","slide","appear"}){
  std::string payload=std::string("[\"display1\",\"")+kind+"\",\"123\",25,\"right\"]";
  inject(portB,payload.c_str());
  expectText(strcmp(kind,"appear")==0?"             ":"3            ");
 }

 // HTTP blink survives real framing, UART routing, and local receive execution.
 nowMillis=2000;app.tick();port.bytes.clear();portB.bytes.clear();
 server.request("/api/display",{{"message","BLINK"},{"am","true"},{"pm","true"},{"colons","blink"},{"to","a"}});assert(server.code==202);app.tick();
 assert(!port.bytes.empty()&&portB.bytes.empty());
 portB.incoming.insert(portB.incoming.end(),port.bytes.begin(),port.bytes.end());app.tick();expectText("BLINK        ");
 assert(matrix.brightness(DisplayMap::discreteAddress(DisplayMap::DiscreteIndex::AM))==92&&matrix.brightness(DisplayMap::discreteAddress(DisplayMap::DiscreteIndex::PM))==92);
 assert(matrix.brightness(DisplayMap::discreteAddress(DisplayMap::DiscreteIndex::Colon1))==92&&matrix.brightness(DisplayMap::discreteAddress(DisplayMap::DiscreteIndex::Colon2))==92);
 nowMillis+=1000;app.tick();assert(matrix.brightness(DisplayMap::discreteAddress(DisplayMap::DiscreteIndex::Colon1))==0&&matrix.brightness(DisplayMap::discreteAddress(DisplayMap::DiscreteIndex::Colon2))==0);
 nowMillis+=1000;app.tick();assert(matrix.brightness(DisplayMap::discreteAddress(DisplayMap::DiscreteIndex::Colon1))==92);
 server.request("/api/display",{{"message","BLINK"},{"am","false"},{"pm","true"},{"colontop","true"},{"colonbottom","false"}});assert(server.code==202);app.tick();assert(matrix.brightness(DisplayMap::discreteAddress(DisplayMap::DiscreteIndex::AM))==0&&matrix.brightness(DisplayMap::discreteAddress(DisplayMap::DiscreteIndex::PM))==92&&matrix.brightness(DisplayMap::discreteAddress(DisplayMap::DiscreteIndex::Colon1))==92&&matrix.brightness(DisplayMap::discreteAddress(DisplayMap::DiscreteIndex::Colon2))==0);
 // Timestamped pings from either port synchronize colon phase without UTC changes.
 server.request("/api/config",{{"primary","false"}});app.tick();
 server.request("/api/display",{{"message",""}});app.tick();
 timeval event{};event.tv_sec=utc;sntp.callback(&event);app.tick();
 nowMillis=500;inject(port,"ping 1000");assert(matrix.brightness(DisplayMap::discreteAddress(DisplayMap::DiscreteIndex::Colon1))==0);
 nowMillis=1499;app.tick();assert(matrix.brightness(DisplayMap::discreteAddress(DisplayMap::DiscreteIndex::Colon1))==0);
 nowMillis=1500;app.tick();assert(matrix.brightness(DisplayMap::discreteAddress(DisplayMap::DiscreteIndex::Colon1))==92);
 inject(portB,"ping 3000");assert(matrix.brightness(DisplayMap::discreteAddress(DisplayMap::DiscreteIndex::Colon1))==0);
 nowMillis+=1000;app.tick();assert(matrix.brightness(DisplayMap::discreteAddress(DisplayMap::DiscreteIndex::Colon1))==92);
 // Native primary clock remains authoritative; received timestamps are ignored.
 nowMillis=100;server.request("/api/config",{{"primary","true"}});app.tick();
 inject(port,"ping 1000");assert(matrix.brightness(DisplayMap::discreteAddress(DisplayMap::DiscreteIndex::Colon1))==92);
 // A routed fade traverses the actual UART frame and preserves local held content.
 server.request("/api/config",{{"primary","false"}});app.tick();
 server.request("/api/display",{{"message","FADE123"},{"pm","true"},{"colontop","true"}});app.tick();
 Display::Frame beforeFade;
 for(size_t index=0;index<sizeof(beforeFade.channels);++index)beforeFade.channels[index]=matrix.brightness(index);
 port.bytes.clear();portB.bytes.clear();
 server.request("/api/display",{{"fade","64"},{"speed","1000"},{"to","a"},{"to","b"}});
 assert(server.code==202);app.tick();
 assert(!port.bytes.empty()&&port.bytes==portB.bytes);
 for(size_t index=0;index<sizeof(beforeFade.channels);++index)assert(matrix.brightness(index)==beforeFade.channels[index]);
 port.incoming.insert(port.incoming.end(),port.bytes.begin(),port.bytes.end());app.tick();
 const auto sentA=port.bytes.size(),sentB=portB.bytes.size();
 nowMillis+=500;app.tick();
 for(size_t index=0;index<sizeof(beforeFade.channels);++index)assert(matrix.brightness(index)==(uint16_t(beforeFade.channels[index])*160+127)/255);
 nowMillis+=500;app.tick();
 for(size_t index=0;index<sizeof(beforeFade.channels);++index)assert(matrix.brightness(index)==(uint16_t(beforeFade.channels[index])*64+127)/255);
 assert(port.bytes.size()==sentA&&portB.bytes.size()==sentB); // Received fades are never forwarded.
 inject(portB,"[\"display2\",\"fade\",255,0]");
 for(size_t index=0;index<sizeof(beforeFade.channels);++index)assert(matrix.brightness(index)==beforeFade.channels[index]);
 puts("PASS: serial framing/bounds, both port pin mappings, receive fragmentation, binary payloads, malformed frames, recovery, receive role independence, primary gating, live edits, one-second timing, rollover, no catch-up burst, TX queue backpressure, remote/local routing, atomic busy rejection, partial writes, local receive execution, and no forwarding.");
}
