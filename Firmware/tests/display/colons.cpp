/*
 * ===========================================================================
 * File:      tests/display/colons.cpp
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
#include <cassert>
#include <cstdio>
Stream Serial;int enableLevel=0;uint32_t now=0;
uint32_t millis(){return now;}

// Check both colon channels at their logical output intensity.
void expectColons(MatrixDriver& matrix,uint8_t brightness){
 assert(matrix.brightness(DisplayMap::discreteAddress(DisplayMap::DiscreteIndex::Colon1))==brightness);
 assert(matrix.brightness(DisplayMap::discreteAddress(DisplayMap::DiscreteIndex::Colon2))==brightness);
}

// Exercise blinking without a clock command, including other active effects.
int main(){
 TwoWire wire;MatrixDriver matrix(wire,21);Display display(matrix);assert(display.begin());
 assert(display.setMessage("ABC"));assert(display.setColonsBlink(true));expectColons(matrix,92);
 int calls=wire.calls;now=999;assert(display.tick()&&wire.calls==calls);
 now=1000;assert(display.tick());expectColons(matrix,0);
 now=2000;assert(display.tick());expectColons(matrix,92);assert(display.hasOverride());
 assert(display.setColonsBlink(false));expectColons(matrix,0);now=4000;assert(display.tick());expectColons(matrix,0);
 // Queued mode commands can opt in without reaching into Display internals.
 Display::DisplayCommand command;command.text="HELLO";command.colonsBlink=true;
 calls=wire.calls;assert(display.commands().post(command)&&wire.calls==calls);assert(display.tick());expectColons(matrix,92);
 display.synchronizeMillis(5000);assert(display.tick());expectColons(matrix,0);
 display.synchronizeMillis(6000);assert(display.tick());expectColons(matrix,92);
 assert(display.fadeDown(0));expectColons(matrix,0);assert(display.fadeUp(0));expectColons(matrix,92);
 assert(display.appearText("ABC1234567890",50));now+=1000;assert(display.tick());expectColons(matrix,0);
 // Lamp tests own all channels temporarily, then restore the active blink phase.
 assert(display.lampTest());now+=Display::LampFadeMs;assert(display.tick());expectColons(matrix,255);
 assert(display.cancelLampTest());expectColons(matrix,0);
 command.colonsBlink=false;command.text="OFF";assert(display.commands().post(command));assert(display.tick());expectColons(matrix,0);
 assert(display.setColonsBlink(true));assert(display.clear());expectColons(matrix,0);now+=1000;assert(display.tick());expectColons(matrix,0);
 // Static indicator choices survive every text mode and remain independent.
 for(auto kind:{Display::DisplayCommand::Kind::Message,Display::DisplayCommand::Kind::Scroll,Display::DisplayCommand::Kind::Slide,Display::DisplayCommand::Kind::Appear}){
  for(int flags=0;flags<16;++flags){
   command.kind=kind;command.text="ABC1234567890";command.speed=10000;command.colonsBlink=false;
   command.am=flags&1;command.pm=flags&2;command.colonTop=flags&4;command.colonBottom=flags&8;
   assert(display.commands().post(command)&&display.tick());
   for(int pass=0;pass<2;++pass){
    assert(matrix.brightness(DisplayMap::discreteAddress(DisplayMap::DiscreteIndex::AM))==(flags&1?92:0));assert(matrix.brightness(DisplayMap::discreteAddress(DisplayMap::DiscreteIndex::PM))==(flags&2?92:0));
    assert(matrix.brightness(DisplayMap::discreteAddress(DisplayMap::DiscreteIndex::Colon1))==(flags&4?92:0));assert(matrix.brightness(DisplayMap::discreteAddress(DisplayMap::DiscreteIndex::Colon2))==(flags&8?92:0));
    now+=1000;assert(display.tick());
   }
  }
 }
 // A temporary scroll restores the held message's indicators and blink policy.
 assert(display.clear());command=Display::DisplayCommand{};command.text="HOME";command.am=true;command.colonsBlink=true;
 assert(display.commands().post(command)&&display.tick());
 Display::DisplayCommand scroll;scroll.kind=Display::DisplayCommand::Kind::Scroll;scroll.text="123";scroll.pm=true;scroll.colonBottom=true;scroll.speed=10;
 assert(display.commands().post(scroll)&&display.tick());assert(matrix.brightness(DisplayMap::discreteAddress(DisplayMap::DiscreteIndex::AM))==0&&matrix.brightness(DisplayMap::discreteAddress(DisplayMap::DiscreteIndex::PM))==92);
 now+=1000;assert(display.tick()&&!display.scrolling());assert(matrix.brightness(DisplayMap::discreteAddress(DisplayMap::DiscreteIndex::AM))==92&&matrix.brightness(DisplayMap::discreteAddress(DisplayMap::DiscreteIndex::PM))==0);
 display.synchronizeMillis(2000);assert(display.tick());expectColons(matrix,92);
 display.synchronizeMillis(3000);assert(display.tick());expectColons(matrix,0);
 puts("PASS: independent/queued colon blink, held text, shared clock corrections, fades, appear, lamp overlay, disable/clear, and idle write suppression.");
}
