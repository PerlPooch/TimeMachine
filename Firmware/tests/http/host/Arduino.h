/*
 * ===========================================================================
 * File:      tests/http/host/Arduino.h
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

#pragma once
#include <cstdint>
#include <cstddef>
#include <string>
#include <cstdio>
#include "../../application/host/ArduinoString.h"
class Stream {
public:
 virtual ~Stream() = default;
 virtual size_t write(uint8_t) {return 1;}
 size_t write(const uint8_t* data,size_t count){size_t written=0;for(size_t i=0;i<count;++i)written+=write(data[i]);return written;}
 template<typename... Args> void printf(const char*,Args...){}
 void print(uint32_t value){print(std::to_string(value).c_str());}
 void println(uint32_t value){print(value);println();}
 void print(const char* text){while(*text)write(uint8_t(*text++));}
 void println(){write(uint8_t('\n'));}
 void println(const char* text){print(text);println();}
};
extern Stream Serial;

constexpr int LOW=0,HIGH=1,OUTPUT=1;
extern int enableLevel;
inline void digitalWrite(uint8_t,int value){enableLevel=value;}
inline void pinMode(uint8_t,int){}
inline void delay(int){}

uint32_t millis();
