/*
 * ===========================================================================
 * File:      tests/display/scroll.cpp
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

#include "Display.h"
#include "DisplayGamma.h"
#include "fonts/SixteenSegmentAlphanumeric.h"
#include "fonts/SevenSegmentNumeric.h"
#include <cassert>
#include <cstring>
#include <cstdio>
Stream Serial;int enableLevel=0;uint32_t now=0;
uint32_t millis(){return now;}
void expectDigit(MatrixDriver& matrix,int index,char character,uint8_t level=255){
 auto digit=DisplayMap::Digits[index];auto mask=digit.segmentCount==16?SixteenSegment::Fonts::glyph(character):SevenSegment::Fonts::glyph(character);
 for(int segment=0;segment<digit.segmentCount;++segment)assert(matrix.brightness(digit.firstAddress+segment)==((mask&(1<<segment))?level:0));
}
void expectWindow(MatrixDriver& matrix,const char* text,int step,Display::Direction direction,uint8_t level=255){
 int length=std::strlen(text);
 int position=direction==Display::Direction::Left?DisplayMap::DigitCount-step:step-length;
 for(int digit=0;digit<DisplayMap::DigitCount;++digit){int source=digit-position;expectDigit(matrix,digit,source>=0&&source<length?text[source]:' ',level);}
}
int main(){
 TwoWire wire;MatrixDriver matrix(wire,21);Display display(matrix);
 assert(!display.scrollText("ABC",100)&&!display.scrolling());assert(display.begin());
 Display::Frame content;display.setIndicator(content,Display::Indicator::PM,92);display.setIndicator(content,Display::Indicator::Colon1,33);assert(display.show(content));
 // Every position in both directions, including alpha/numeric font boundaries and exit.
 for(auto direction:{Display::Direction::Left,Display::Direction::Right}){
  const char* text="AbC0123456789-+0123";int end=std::strlen(text)+DisplayMap::DigitCount;
  now=0;assert(display.scrollText(text,100,direction));expectWindow(matrix,text,1,direction);assert(display.scrolling());
  int calls=wire.calls;now=99;assert(display.tick()&&wire.calls==calls);
  for(int step=2;step<=end;++step){now=(step-1)*100;assert(display.tick());expectWindow(matrix,text,step,direction);assert(display.scrolling()==(step!=end));assert(matrix.brightness(DisplayMap::discreteAddress(DisplayMap::DiscreteIndex::PM))==92&&matrix.brightness(DisplayMap::discreteAddress(DisplayMap::DiscreteIndex::Colon1))==33);}
  calls=wire.calls;now+=1000;assert(display.tick()&&wire.calls==calls);
 }
 // Copy caller storage; delayed ticks catch up without drift, across millis rollover.
 char caller[]="123";now=0xfffffff0U;assert(display.scrollText(caller,20));caller[0]='9';
 now+=65;assert(display.tick());expectWindow(matrix,"123",4,Display::Direction::Left);
 now+=14;int calls=wire.calls;assert(display.tick()&&wire.calls==calls);
 now+=1;assert(display.tick());expectWindow(matrix,"123",5,Display::Direction::Left);
 now+=10000;assert(display.tick()&&!display.scrolling());expectWindow(matrix,"",1,Display::Direction::Left);
 // New scroll restarts immediately; ordinary full/partial writes cancel it.
 assert(display.scrollText("old",100));assert(display.scrollText("AB",5,Display::Direction::Right));expectWindow(matrix,"AB",1,Display::Direction::Right);
 assert(display.showAlpha("RST")&&!display.scrolling());for(int d=0;d<3;++d)expectDigit(matrix,d,"RST"[d]);
 assert(display.scrollText("123",10));assert(display.showText("ABC")&&!display.scrolling());
 assert(display.scrollText("123",10));assert(display.show(content)&&!display.scrolling());
 assert(display.scrollText("123",10));assert(display.clear()&&!display.scrolling());
 assert(display.scrollText("123",10));assert(display.scrollText(nullptr,10)&&!display.scrolling());
 for(int d=0;d<DisplayMap::DigitCount;++d)expectDigit(matrix,d,' ');
 assert(display.scrollText("",10)&&!display.scrolling());
 // Zero delay is one ms; no fade frame throttle delays a scroll step.
 now=0;assert(display.scrollText("123",0));now=1;assert(display.tick());expectWindow(matrix,"123",2,Display::Direction::Left);
 now+=100;assert(display.tick()&&!display.scrolling());
 // Fades scale scroll frames once, preserve target indicator intensity, and run concurrently.
 assert(display.show(content));assert(display.fadeDown(0));now=0;assert(display.scrollText("123456",50));assert(display.fadeUp(100));
 now=50;assert(display.tick());expectWindow(matrix,"123456",2,Display::Direction::Left,128);assert(matrix.brightness(DisplayMap::discreteAddress(DisplayMap::DiscreteIndex::PM))==46&&display.targetFrame().channels[DisplayMap::discreteAddress(DisplayMap::DiscreteIndex::PM)]==92);
 now=100;assert(display.tick());expectWindow(matrix,"123456",3,Display::Direction::Left);assert(!display.fading()&&display.scrolling());
 // Lamp overlay pauses the scroll; completion and explicit cancellation resume its timing.
 assert(display.lampTest());now+=Display::LampDurationMs;assert(display.tick()&&!display.segmentTesting()&&display.scrolling());expectWindow(matrix,"123456",3,Display::Direction::Left);
 now+=49;assert(display.tick());expectWindow(matrix,"123456",3,Display::Direction::Left);now+=1;assert(display.tick());expectWindow(matrix,"123456",4,Display::Direction::Left);
 assert(display.lampTest());assert(display.scrollText("12",10,Display::Direction::Right));now+=500;assert(display.cancelLampTest());expectWindow(matrix,"12",1,Display::Direction::Right);now+=10;assert(display.tick());expectWindow(matrix,"12",2,Display::Direction::Right);
 // Slides use the same entry frames but stop at the far edge and hold indefinitely.
 assert(display.show(content));
 for(auto direction:{Display::Direction::Left,Display::Direction::Right}){
  for(const char* text:{"123","AbC0123456789"}){
   now=0;assert(display.slideText(text,10,direction)&&display.sliding()&&!display.scrolling());expectWindow(matrix,text,1,direction);
   for(int step=2;step<=DisplayMap::DigitCount;++step){now=(step-1)*10;assert(display.tick());expectWindow(matrix,text,step,direction);assert(display.sliding()==(step!=DisplayMap::DigitCount));assert(matrix.brightness(DisplayMap::discreteAddress(DisplayMap::DiscreteIndex::PM))==92);}
   calls=wire.calls;now+=100000;assert(display.tick()&&wire.calls==calls);expectWindow(matrix,text,DisplayMap::DigitCount,direction);
  }
 }
 assert(display.slideText("AbC0123456789ignored",10));now+=120;assert(display.tick()&&!display.sliding());expectWindow(matrix,"AbC0123456789",13,Display::Direction::Left);
 char slideCaller[]="123";now=0xfffffff0U;assert(display.slideText(slideCaller,10));slideCaller[0]='9';now+=120;assert(display.tick()&&!display.sliding());expectWindow(matrix,"123",13,Display::Direction::Left);
 assert(display.slideText("123",10));assert(display.showText("RST")&&!display.sliding());
 assert(display.slideText("123",10));assert(display.scrollText("456",10)&&!display.sliding()&&display.scrolling());
 assert(display.slideText("123",10));assert(display.slideText(nullptr,10)&&!display.sliding());
 assert(display.slideText("",10)&&!display.sliding());
 assert(display.slideText("123",0));now+=12;assert(display.tick()&&!display.sliding());expectWindow(matrix,"123",13,Display::Direction::Left);
 assert(display.slideText("123",10));
 // Override ownership and restoration work in Display alone, with no Clock instance.
 assert(display.clear()&&!display.hasOverride());
 assert(display.setMessage("HOME")&&display.hasOverride());expectWindow(matrix,"HOME",13,Display::Direction::Left);
 assert(display.scrollText("123",10));now+=150;assert(display.tick()&&!display.scrolling()&&display.hasOverride());expectWindow(matrix,"HOME",13,Display::Direction::Left);
 assert(display.slideText("456",10,Display::Direction::Right));now+=120;assert(display.tick()&&!display.sliding()&&display.hasOverride());expectWindow(matrix,"456",13,Display::Direction::Right);
 calls=wire.calls;now+=10000;assert(display.tick()&&wire.calls==calls);expectWindow(matrix,"456",13,Display::Direction::Right);
 assert(display.scrollText("789",10));now+=150;assert(display.tick()&&display.hasOverride());expectWindow(matrix,"456",13,Display::Direction::Right);
 assert(display.scrollText("123",10));assert(display.showAlpha("RST"));assert(display.tick());expectWindow(matrix,"456",13,Display::Direction::Right); // Interrupted scroll restores held content.
 assert(display.scrollText("123",10));assert(display.scrollText("",10)&&display.hasOverride());expectWindow(matrix,"456",13,Display::Direction::Right);
 assert(display.setMessage("")&&!display.hasOverride()&&!display.sliding()&&!display.scrolling());
 assert(display.setMessage("ABC"));assert(display.clear()&&!display.hasOverride());
 assert(display.scrollText("123",10)&&display.hasOverride());now+=150;assert(display.tick()&&!display.hasOverride());
 assert(display.slideText("123",10));
 // An I2C failure stops scrolling and propagates failure without further writes.
 wire.failAt=wire.calls+1;now+=10;assert(!display.tick()&&!display.scrolling()&&!matrix.ready());calls=wire.calls;now+=1000;assert(!display.tick()&&wire.calls==calls);
 TwoWire badWire;MatrixDriver badMatrix(badWire,21);Display badDisplay(badMatrix);assert(badDisplay.begin());badWire.failAt=badWire.calls+1;assert(!badDisplay.scrollText("1",100)&&!badDisplay.scrolling());
 puts("PASS: bidirectional scroll entry/exit, font mapping, owned/long text, speed/catch-up/rollover, cancellation, indicators, fade/lamp interaction, and I2C errors (ASan/UBSan).");
}
