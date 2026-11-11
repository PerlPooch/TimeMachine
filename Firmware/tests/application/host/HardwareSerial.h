/*
 * ===========================================================================
 * File:      tests/application/host/HardwareSerial.h
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

#pragma once
#include "Arduino.h"
#include <vector>
constexpr uint32_t SERIAL_8N1 = 0;
class HardwareSerial {
public:
 inline static HardwareSerial* current=nullptr;
 inline static HardwareSerial* ports[2]={};
 int uart, rx=-1, tx=-1, room=128; uint32_t baud=0;
 std::vector<uint8_t> bytes;
 std::vector<uint8_t> incoming; size_t readAt=0;
 explicit HardwareSerial(uint8_t number):uart(number){current=this;ports[number]=this;}
 void begin(uint32_t rate,uint32_t,int8_t input,int8_t output){baud=rate;rx=input;tx=output;}
 int available(){return incoming.size()-readAt;}
 int read(){return available()?incoming[readAt++]:-1;}
 int availableForWrite(){return room;}
 size_t write(const uint8_t* data,size_t length){bytes.insert(bytes.end(),data,data+length);return length;}
};
