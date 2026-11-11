/*
 * ===========================================================================
 * File:      tests/display/animation.cpp
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
#include <cassert>
#include <cstring>
#include <cstdio>
Stream Serial;int enableLevel=0;uint32_t now=0;
uint32_t millis(){return now;}
bool mapped(int address){
 for(auto digit:DisplayMap::Digits)if(address>=digit.firstAddress&&address<digit.firstAddress+digit.segmentCount)return true;
 for(const auto& discrete:DisplayMap::DiscreteMap)if(address==discrete.address)return true;
 return false;
}
void allLamp(MatrixDriver& matrix,uint8_t level){for(int address=0;address<192;++address)assert(matrix.brightness(address)==(mapped(address)?level:0));}
int main(){
 TwoWire wire;MatrixDriver matrix(wire,21,8);Display display(matrix);
 assert(!display.lampTest()&&!display.fadeUp()&&!display.tick());
 assert(display.begin());int calls=wire.calls;assert(display.begin()&&wire.calls==calls);
 Display::Frame content;content.channels[0]=255;content.channels[1]=128;content.channels[DisplayMap::discreteAddress(DisplayMap::DiscreteIndex::PM)]=92;
 assert(display.show(content));assert(display.fadeDown(1000));
 now=500;assert(display.tick());assert(matrix.brightness(0)==128&&matrix.brightness(1)==64&&matrix.brightness(DisplayMap::discreteAddress(DisplayMap::DiscreteIndex::PM))==46);
 assert(display.targetFrame().channels[1]==128);
 assert(wire.regs[1][1]==DisplayGamma::toPWM(64)); // Scaling precedes the driver's single gamma correction.
 calls=wire.calls;assert(display.tick()&&wire.calls==calls);
 // A mid-fade text edit preserves the unscaled target indicators, not the dimmed snapshot.
 assert(display.showText("ABC",200));assert(display.targetFrame().channels[DisplayMap::discreteAddress(DisplayMap::DiscreteIndex::PM)]==92);
 assert(matrix.brightness(DisplayMap::discreteAddress(DisplayMap::DiscreteIndex::PM))==46);
 now=1000;assert(display.tick()&&!display.fading());for(int i=0;i<128;++i)assert(matrix.brightness(i)==0);
 assert(display.targetFrame().channels[DisplayMap::discreteAddress(DisplayMap::DiscreteIndex::PM)]==92);
 assert(display.fadeUp(1000));now=1500;assert(display.tick());assert(matrix.brightness(DisplayMap::discreteAddress(DisplayMap::DiscreteIndex::PM))==46);
 now=2000;assert(display.tick()&&!display.fading());assert(matrix.brightness(DisplayMap::discreteAddress(DisplayMap::DiscreteIndex::PM))==92);
 assert(display.fadeDown(0));assert(matrix.brightness(DisplayMap::discreteAddress(DisplayMap::DiscreteIndex::PM))==0);assert(display.fadeUp(0));assert(matrix.brightness(DisplayMap::discreteAddress(DisplayMap::DiscreteIndex::PM))==92);
 // Reversing a fade starts at its current level; unsigned timing handles millis rollover.
 now=0xfffffff0U;assert(display.fadeDown(100));now+=50;assert(display.tick());assert(matrix.brightness(DisplayMap::discreteAddress(DisplayMap::DiscreteIndex::PM))==46);
 assert(display.fadeUp(100));assert(matrix.brightness(DisplayMap::discreteAddress(DisplayMap::DiscreteIndex::PM))==46);now+=100;assert(display.tick());assert(matrix.brightness(DisplayMap::discreteAddress(DisplayMap::DiscreteIndex::PM))==92);
 // Clear changes content while retaining the selected fade level.
 assert(display.fadeDown(0));assert(display.clear());assert(display.fadeUp(0));for(int i=0;i<128;++i)assert(matrix.brightness(i)==0);
 now=0xfffffff0U;assert(display.lampTest()&&display.segmentTesting());allLamp(matrix,0);
 now+=Display::LampFadeMs/2;assert(display.tick());allLamp(matrix,128);
 now+=Display::LampFadeMs-Display::LampFadeMs/2;assert(display.tick());allLamp(matrix,255);
 auto start=now;
 content.channels[0]=64;content.channels[1]=32;assert(display.show(content));allLamp(matrix,255);
 now=start+Display::LampHoldMs-1;assert(display.tick());allLamp(matrix,255);
 now=start+Display::LampHoldMs;assert(display.tick());allLamp(matrix,255);
 now=start+Display::LampHoldMs+Display::LampFadeMs/2;assert(display.tick());allLamp(matrix,128);
 now=start+Display::LampHoldMs+Display::LampFadeMs-1;assert(display.tick()&&display.segmentTesting());allLamp(matrix,0);
 now=start+Display::LampHoldMs+Display::LampFadeMs;assert(display.tick()&&!display.segmentTesting());assert(matrix.brightness(0)==64&&matrix.brightness(1)==32&&matrix.brightness(DisplayMap::discreteAddress(DisplayMap::DiscreteIndex::PM))==92);
 assert(display.lampTest());now+=Display::LampFadeMs;assert(display.tick());allLamp(matrix,255);assert(display.cancelLampTest());assert(!display.segmentTesting()&&matrix.brightness(0)==64);
 assert(matrix.setBrightness(2,73));assert(display.lampTest());assert(display.cancelLampTest());assert(matrix.brightness(2)==73);
 assert(display.lampTest());wire.failAt=wire.calls+1;now+=175;assert(!display.tick()&&!matrix.ready()&&!display.segmentTesting());calls=wire.calls;now+=1000;assert(!display.tick()&&wire.calls==calls);
 puts("PASS: target/output buffering, independent segment intensities, gamma once, mid-fade edits, smooth fades/reversal, zero duration, rollover, mapped lamp coverage, full one-second hold, restoration/cancellation, raw diagnostics, and I2C failure.");
}
