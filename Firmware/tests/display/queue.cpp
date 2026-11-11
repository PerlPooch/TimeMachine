/*
 * ===========================================================================
 * File:      tests/display/queue.cpp
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
#include <cstdio>
Stream Serial;int enableLevel=0;uint32_t now=0;
uint32_t millis(){return now;}
void expect(Display& display,const char* text){
 auto expected=display.targetFrame();display.drawText(expected,text);auto actual=display.frame();
 for(size_t i=0;i<sizeof(actual.channels);++i)assert(expected.channels[i]==actual.channels[i]);
}
int main(){
 TwoWire wire;MatrixDriver matrix(wire,21);Display display(matrix);assert(display.begin());
 Display::DisplayCommand clock;clock.kind=Display::DisplayCommand::Kind::Clock;clock.timeValid=true;clock.am=true;clock.colonsBlink=true;
 clock.dateTime.year=2026;clock.dateTime.month=10;clock.dateTime.day=28;clock.dateTime.hour=9;clock.dateTime.minute=25;
 int calls=wire.calls;assert(display.commands().post(clock)&&wire.calls==calls);assert(display.tick());expect(display,"OCT2820260925");
 assert(matrix.brightness(DisplayMap::discreteAddress(DisplayMap::DiscreteIndex::AM))==92&&matrix.brightness(DisplayMap::discreteAddress(DisplayMap::DiscreteIndex::Colon1))==92);
 // Colon phase advances inside Display even with no producer activity.
 now=1000;assert(display.tick()&&matrix.brightness(DisplayMap::discreteAddress(DisplayMap::DiscreteIndex::Colon1))==0);
 display.synchronizeMillis(2000);assert(display.tick()&&matrix.brightness(DisplayMap::discreteAddress(DisplayMap::DiscreteIndex::Colon1))==92);
 Display::DisplayCommand message;message.text="HOME";calls=wire.calls;
 assert(display.commands().post(message)&&wire.calls==calls);message.text="EDIT";
 assert(!display.commands().post(clock));assert(display.tick());expect(display,"HOME         ");
 // Background snapshots update without interrupting held content.
 clock.dateTime.minute=26;assert(display.commands().post(clock));assert(display.tick());expect(display,"HOME         ");
 message.text="";assert(display.commands().post(message));assert(display.tick());expect(display,"OCT2820260926");
 // Explicit latest-command semantics are retained.
 message.text="FIRST";assert(display.commands().post(message));message.text="LAST";assert(display.commands().post(message));assert(display.tick());expect(display,"LAST         ");
 message.text="";assert(display.commands().post(message));assert(display.tick());
 message.kind=Display::DisplayCommand::Kind::Scroll;message.text="123";message.speed=10;
 assert(display.commands().post(message));assert(display.tick()&&display.scrolling());now+=200;assert(display.tick()&&!display.scrolling());expect(display,"OCT2820260926");
 // Hardware failure is returned from the consumer, never from a producer write.
 message.kind=Display::DisplayCommand::Kind::Message;message.text="FAIL";calls=wire.calls;assert(display.commands().post(message)&&wire.calls==calls);wire.failAt=calls+1;assert(!display.tick()&&!matrix.ready());
 puts("PASS: Display-owned shared queue, deferred hardware writes, owned/latest commands, clock priority, held-message restoration, autonomous synchronized colons, scroll return, and consumer error propagation.");
}
