/*
 * ===========================================================================
 * File:      tests/display/appear.cpp
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

#include "Display.h"
#include "fonts/SixteenSegmentAlphanumeric.h"
#include "fonts/SevenSegmentNumeric.h"
#include <cassert>
#include <cstdio>
Stream Serial; int enableLevel=0; uint32_t now=0;
uint32_t millis(){return now;}
void expect(Display& display,const char* text){
 Display::Frame expected=display.targetFrame();display.drawText(expected,text);
 auto actual=display.frame();
 for(size_t i=0;i<sizeof(actual.channels);++i)assert(actual.channels[i]==expected.channels[i]);
}
int main(){
 TwoWire wire;MatrixDriver matrix(wire,21);Display display(matrix);
 assert(!display.appearText("ABC",50));assert(display.begin());
 Display::Frame initial;display.setIndicator(initial,Display::Indicator::PM,92);assert(display.show(initial));
 const char* text="OCT2820260925";
 const char* right[]={"           25","         0925","     20260925","   2820260925","OCT2820260925"};
 const char* left[]={"OCT          ","OCT28        ","OCT282026    ","OCT28202609  ","OCT2820260925"};
 for(auto direction:{Display::Direction::Right,Display::Direction::Left}){
  now=0;assert(display.appearText(text,50,direction));
  auto frames=direction==Display::Direction::Right?right:left;
  expect(display,frames[0]);assert(display.appearing()&&!display.scrolling()&&!display.sliding());
  int calls=wire.calls;now=49;assert(display.tick()&&wire.calls==calls);
  for(int step=1;step<5;++step){now=step*50;assert(display.tick());expect(display,frames[step]);assert(display.appearing()==(step<4));assert(matrix.brightness(DisplayMap::discreteAddress(DisplayMap::DiscreteIndex::PM))==92);}
  assert(display.hasOverride());calls=wire.calls;now+=1000;assert(display.tick()&&wire.calls==calls);
 }
 char caller[]="ABC1234567890extra";now=0xfffffff0U;assert(display.appearText(caller,10));caller[0]='Z';now+=40;assert(display.tick()&&!display.appearing());expect(display,"ABC1234567890");
 assert(display.appearText("ABC",0));now+=4;assert(display.tick()&&!display.appearing());expect(display,"ABC");
 assert(display.appearText("ABC",10));assert(display.appearText(nullptr,10)&&!display.hasOverride());expect(display,"");
 assert(display.appearText("ABC",10));assert(display.showText("RST")&&!display.appearing());expect(display,"RST");
 assert(display.clear());assert(display.appearText("ABC1234567890",10));assert(display.lampTest());now+=Display::LampDurationMs;assert(display.tick()&&display.appearing());expect(display,"ABC");now+=40;assert(display.tick()&&!display.appearing());expect(display,"ABC1234567890");
 assert(display.appearText("ABC1234567890",10));wire.failAt=wire.calls+1;now+=10;assert(!display.tick()&&!display.appearing());
 puts("PASS: chunk reveal directions, exact boundaries/timing, ownership, padding/truncation, rollover, hold, cancellation, lamp pause, and I2C failure (ASan/UBSan).");
}
