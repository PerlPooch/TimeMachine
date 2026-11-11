/*
 * ===========================================================================
 * File:      tests/http/thermal.cpp
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

#include "Tests.h"
#include <MDS_Display.h>
#include <cassert>
Stream Serial;
int enableLevel=0;
uint32_t now=0;
uint32_t millis(){return now;}
int main(){
 for(uint8_t applicationGCC:{uint8_t(2),uint8_t(9)}){
  now=0;TwoWire wire;MatrixDriver matrix(wire,21);assert(matrix.setConfiguredCurrent(applicationGCC));MDS_Display oled;Display display(matrix);Tests tests(matrix,display,oled);
  assert(tests.begin());assert(matrix.globalCurrent()==applicationGCC&&wire.regs[3][1]==applicationGCC);
  now=10000;tests.tick();assert(matrix.globalCurrent()==4&&wire.regs[3][1]==4);
  assert(matrix.setConfiguredCurrent(applicationGCC+1));assert(matrix.setGlobalCurrent(4));
  now=40000;tests.tick();assert(matrix.globalCurrent()==4);
  now=42000;tests.tick();assert(matrix.globalCurrent()==applicationGCC+1&&wire.regs[3][1]==applicationGCC+1);
 }
 puts("PASS: diagnostics inherit configured GCC, thermal overrides apply, and completion restores the latest config edit.");
}
