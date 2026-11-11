/*
 * ===========================================================================
 * File:      tests/http/driver.cpp
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

#include "MatrixDriver.h"
#include "DisplayGamma.h"
#include <cassert>
int enableLevel=0;
Stream Serial;
uint32_t millis(){return 0;}
int main(){
 {
  uint8_t channel[]={0};TwoWire conservativeWire;MatrixDriver conservative(conservativeWire,21);
  assert(conservative.initialCurrent()==0x02&&conservative.globalCurrent()==0x02&&!conservative.ready());
  assert(conservative.begin(channel,1)&&conservativeWire.regs[3][1]==0x02);
  assert(conservative.setGlobalCurrent(8));assert(conservative.begin(channel,1));assert(conservative.globalCurrent()==2);
  TwoWire applicationWire;MatrixDriver application(applicationWire,21,9);
  assert(application.initialCurrent()==9&&application.globalCurrent()==9);
  assert(application.begin(channel,1)&&applicationWire.regs[3][1]==9);
  assert(application.begin(4,channel,1)&&application.globalCurrent()==4&&application.initialCurrent()==9);
  assert(application.begin(channel,1)&&application.globalCurrent()==9);
 }
 TwoWire wire;MatrixDriver matrix(wire,21);uint8_t channels[192];for(int i=0;i<192;++i)channels[i]=i;
 assert(!matrix.ready());assert(matrix.setBrightnessLimit(64));assert(matrix.begin(4,channels,192));assert(matrix.globalCurrent()==4&&matrix.ready());assert(matrix.brightnessLimit()==64&&matrix.effectiveMaximumPWM()==64);
 assert(matrix.setBrightness(0,255));assert(matrix.fillBrightness(16,19,128));uint8_t values[]={0,64,128,255};assert(matrix.writeBrightness(63,values,4));
 uint8_t logical[192]={};logical[0]=255;for(int i=16;i<35;++i)logical[i]=128;for(int i=0;i<4;++i)logical[63+i]=values[i];
 for(uint8_t ceiling:{uint8_t(0),uint8_t(255),uint8_t(32),uint8_t(64)}){
  assert(matrix.setBrightnessLimit(ceiling));
  for(int i=0;i<192;++i)assert(wire.regs[1][i]==DisplayGamma::toPWM(logical[i],ceiling));
 }
 matrix.setMaximumPWM(16);assert(matrix.maximumPWM()==16&&matrix.effectiveMaximumPWM()==16);assert(matrix.setBrightnessLimit(63));for(int i=0;i<192;++i)assert(wire.regs[1][i]==DisplayGamma::toPWM(logical[i],16));
 assert(matrix.setGlobalCurrent(8));assert(matrix.globalCurrent()==8);wire.failAt=wire.calls+1;assert(!matrix.setGlobalCurrent(16));assert(matrix.globalCurrent()==8&&enableLevel==LOW&&!matrix.ready());
 puts("PASS: constructor GCC/default 0x02/legacy begin compatibility, immediate refresh of all brightness APIs, zero-to-full restoration, gamma applied once, profile cap respected, and GCC tracks successful writes.");
}
