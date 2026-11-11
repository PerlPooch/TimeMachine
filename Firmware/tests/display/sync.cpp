/*
 * ===========================================================================
 * File:      tests/display/sync.cpp
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

#include "Display.h"
#include <cassert>
#include <cstring>
#include <cstdio>
Stream Serial;int enableLevel=0;uint32_t now=0;
uint32_t millis(){return now;}
void expect(Display& display,const char* text){
 auto expected=display.targetFrame();display.drawText(expected,text);auto actual=display.frame();assert(!memcmp(expected.channels,actual.channels,sizeof(actual.channels)));
}
int main(){
 TwoWire wire;MatrixDriver matrix(wire,21);Display display(matrix);assert(display.begin());
 Display::Frame frame;frame.channels[0]=255;assert(display.show(frame));assert(display.fadeDown(1000));
 now=500;assert(display.tick()&&matrix.brightness(0)==128);
 display.synchronizeMillis(0xfffffff0U);assert(display.animationMillis()==0xfffffff0U&&millis()==500);
 assert(display.tick()&&matrix.brightness(0)==128&&display.fading());
 now+=32;assert(display.animationMillis()==16);now=750;assert(display.tick()&&matrix.brightness(0)==40);
 display.synchronizeMillis(100);assert(display.tick()&&matrix.brightness(0)==40);
 now=1000;assert(display.tick()&&!display.fading()&&matrix.brightness(0)==0);
 assert(display.fadeUp(0));assert(display.appearText("ABC1234567890",50));expect(display,"ABC          ");
 now=1025;display.synchronizeMillis(200);assert(display.tick());expect(display,"ABC          ");
 now=1050;assert(display.tick());expect(display,"ABC12        ");
 now=1075;display.synchronizeMillis(100000);now=1100;assert(display.tick());expect(display,"ABC123456    ");
 display.resetMillisDelta();assert(display.animationMillis()==millis());assert(display.tick());expect(display,"ABC123456    ");
 assert(display.lampTest());now+=500;assert(display.tick()&&display.segmentTesting());
 display.synchronizeMillis(5);assert(display.tick()&&display.segmentTesting());
 now+=Display::LampDurationMs-500;assert(display.tick()&&!display.segmentTesting());
 puts("PASS: adjusted animation millis, native clock isolation, large/backward offset corrections, rollover, fade/appear/lamp continuity, and reset to primary clock.");
}
