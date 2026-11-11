/*
 * ===========================================================================
 * File:      tests/time/display.cpp
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

#include "Clock.h"
#include "TimeController.h"
#include <MDS_Display.h>
#include <esp_sntp.h>
#include <LittleFS.h>
#include "DisplayMap.h"
#include "fonts/SixteenSegmentAlphanumeric.h"
#include "fonts/SevenSegmentNumeric.h"
#include "DisplayGamma.h"
#include <cassert>
FakeSNTP sntp;FakeFS LittleFS;Stream Serial;int enableLevel=0;time_t utc=0;
time_t fakeTime(time_t* result){if(result)*result=utc;return utc;}
void expect(TwoWire& wire,const char* text,bool am,bool colon,bool valid=true){
 uint8_t expected[192]{};
 for(int index=0;index<13;++index){auto digit=DisplayMap::Digits[index];uint16_t mask=digit.segmentCount==16?SixteenSegment::Fonts::glyph(text[index]):SevenSegment::Fonts::glyph(text[index]);for(int segment=0;segment<digit.segmentCount;++segment)expected[digit.firstAddress+segment]=mask&(1<<segment)?DisplayGamma::toPWM(255):0;}
 // Preserve the owner-tuned logical indicator brightness (COLON_BRIGHTNESS=92).
 if(valid && am)expected[DisplayMap::discreteAddress(DisplayMap::DiscreteIndex::AM)]=DisplayGamma::toPWM(92);else if(valid)expected[DisplayMap::discreteAddress(DisplayMap::DiscreteIndex::PM)]=DisplayGamma::toPWM(92);
 if(colon){expected[DisplayMap::discreteAddress(DisplayMap::DiscreteIndex::Colon1)]=expected[DisplayMap::discreteAddress(DisplayMap::DiscreteIndex::Colon2)]=DisplayGamma::toPWM(92);}
 for(int i=0;i<192;++i)assert(wire.regs[1][i]==expected[i]);
}
int main(){
 using Type=MDS_ConfigType;using Access=MDS_ConfigAccess;
 const MDS_ConfigSetting settings[]={
  {"timeOffset",Type::Signed,Access::ReadWrite,false,nullptr,0,0,0},
  {"gcc",Type::Unsigned,Access::ReadWrite,true,nullptr,4,0,255},
  {"timeZone",Type::Text,Access::ReadWrite,true,MDS_Time::DefaultTimeZone,0,1,128},
  {"ntpServer1",Type::Text,Access::ReadWrite,true,MDS_Time::DefaultServer1,0,1,64},
  {"ntpServer2",Type::Text,Access::ReadWrite,true,MDS_Time::DefaultServer2,0,0,64},
  {"ntpInterval",Type::Unsigned,Access::ReadWrite,true,nullptr,3600,60,86400}
 };
 MDS_Config config;assert(config.setSettings(settings));TwoWire wire;MatrixDriver matrix(wire,21,4);MDS_Display oled;MDS_Time time;
 TimeController controller(time,config,oled);assert(controller.begin(true));
 Display mainDisplay(matrix);assert(mainDisplay.begin());int beforeClock=wire.calls;Clock display(time,config,mainDisplay.commands(),oled);assert(display.begin());assert(wire.calls==beforeClock);assert(mainDisplay.tick());assert(matrix.ready()&&matrix.globalCurrent()==4);
 bool networkReady = false;
 time.setNetworkReadyCallback([&]() {
     // Control connectivity independently of display servicing.
     return networkReady;
 });
 auto tick = [&](bool online) {
     // Service the configured time source and display for this scenario.
     networkReady = online;
     controller.tick();
     display.tick();
     mainDisplay.tick();
 };
 // Before synchronization the unavailable-time pattern has no indicators.
 expect(wire,"-------------",false,false,false);
 int calls=wire.calls;tick(false);tick(true);assert(wire.calls==calls&&!time.valid());
 timeval event{};utc=1792900800;event.tv_sec=utc;sntp.callback(&event);tick(true);assert(time.valid());expect(wire,"OCT2520261200",true,true);
 calls=wire.calls;auto oledMessages=oled.messages.size();tick(true);assert(wire.calls==calls&&oled.messages.size()==oledMessages);++utc;nowMillis=1000;tick(false);expect(wire,"OCT2520261200",true,false);assert(oled.messages.size()==oledMessages);
 utc=1792944000;nowMillis=2000;tick(false);expect(wire,"OCT2520261200",false,true);
 // A timezone change redraws immediately, even within the same UTC second.
 JsonDocument edit;edit["zone"]="UTC0";assert(config.set("timeZone",edit["zone"]));tick(false);expect(wire,"OCT2520260400",false,true);
 // Shared millis drives colons even while the UTC second stays unchanged.
 mainDisplay.synchronizeMillis(3000);tick(false);expect(wire,"OCT2520260400",false,false);
 nowMillis+=1000;tick(false);expect(wire,"OCT2520260400",false,true);

 // Offset edits redraw in the same UTC second and preserve real system time and timezone.
 auto offset=[&](int64_t seconds){JsonDocument value;value["seconds"]=seconds;assert(config.set("timeOffset",value["seconds"]));tick(false);};
 const time_t realUtc=utc; const auto timeVersion=time.configurationVersion();
 offset(5*86400);expect(wire,"OCT3020260400",false,true);
 assert(time.utcNow()==realUtc && time.configurationVersion()==timeVersion);
 offset(-12*3600);expect(wire,"OCT2520260400",true,true);
 offset(0);expect(wire,"OCT2520260400",false,true);

 // Positive and negative offsets cross year, month, leap-day, noon, and midnight boundaries.
 utc=1798761599;offset(1);expect(wire,"JAN0120271200",true,true);
 utc=1798761600;offset(-1);expect(wire,"DEC3120261159",false,true);
 utc=1835481600;offset(-1);expect(wire,"FEB2920281159",false,true);
 utc=1835352000;offset(86400);expect(wire,"FEB2920281200",false,true);
 utc=1793447999;offset(1);expect(wire,"OCT3120261200",false,true);

 // DST is evaluated at the shifted instant, in both transition directions.
 edit["zone"]=MDS_Time::DefaultTimeZone;assert(config.set("timeZone",edit["zone"]));
 utc=1772953199;offset(1);expect(wire,"MAR0820260300",true,true);
 utc=1772953200;offset(-1);expect(wire,"MAR0820260159",true,true);
 utc=1793512799;offset(1);expect(wire,"NOV0120260100",true,true);
 utc=1793512800;offset(-1);expect(wire,"NOV0120260159",true,true);

 // Extreme accepted offsets cannot wrap calendar fields; restoring zero recovers immediately.
 offset(INT64_MAX);expect(wire,"-------------",false,false,false);
 offset(INT64_MIN);expect(wire,"-------------",false,false,false);
 offset(253402300800LL);expect(wire,"-------------",false,false,false);
 offset(0);expect(wire,"NOV0120260100",true,true);

 // An explicit command keeps priority; a blocked clock snapshot is retried at the same instant.
 Display::DisplayCommand explicitCommand;explicitCommand.text="PRIORITY";
 assert(mainDisplay.commands().post(explicitCommand));
 JsonDocument changed;changed["seconds"]=3600;assert(config.set("timeOffset",changed["seconds"]));
 display.tick();Display::DisplayCommand taken;assert(mainDisplay.commands().take(taken));
 assert(taken.kind==Display::DisplayCommand::Kind::Message);
 display.tick();assert(mainDisplay.commands().take(taken));
 assert(taken.kind==Display::DisplayCommand::Kind::Clock && taken.timeValid && taken.dateTime.hour==2);
 offset(0);
 calls=wire.calls;Clock diagnostic(time,config,mainDisplay.commands(),oled);++utc;networkReady = false;
 controller.tick();
 diagnostic.tick();assert(wire.calls==calls);
 wire.failAt=wire.calls+1;nowMillis+=1000;tick(false);assert(!matrix.ready()&&enableLevel==LOW);calls=wire.calls;++utc;tick(false);assert(wire.calls==calls);
 puts("PASS: NTP waiting frame, real calendar/12-hour clock, AM/PM, blinking colon, offline ticking, immediate timezone/offset edits, shifted DST/calendar rollovers, overflow recovery, queue retry, diagnostic isolation, and matrix failure shutdown.");
}
