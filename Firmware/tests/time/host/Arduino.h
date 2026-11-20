/*
 * ===========================================================================
 * File:      tests/time/host/Arduino.h
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
#include <cstdio>
#include <ctime>
#include <string>
class String : public std::string {
public:
 using std::string::string;
 using std::string::operator=;
 String()=default;
 String(const std::string& text):std::string(text){}
 String& operator=(const char* text){if(text)assign(text);else std::string().swap(*this);return *this;}
};
extern time_t fakeTime(time_t*);
#define time fakeTime
constexpr int LOW=0,HIGH=1,OUTPUT=1;
extern int enableLevel;
inline void digitalWrite(uint8_t,int value){enableLevel=value;}
inline void pinMode(uint8_t,int){}
inline void delay(int){}
class Stream {
public:
 virtual ~Stream()=default;
 virtual size_t write(uint8_t){return 1;}
 size_t write(const uint8_t* data,size_t count){for(size_t i=0;i<count;++i)write(data[i]);return count;}
 void print(const char* text){while(*text)write(uint8_t(*text++));}
 void print(const String& text){print(text.c_str());}
 void println(){write(uint8_t('\n'));}
 void println(const char* text){print(text);println();}
 void println(const String& text){print(text);println();}
};
extern Stream Serial;

inline uint32_t nowMillis=0;
inline uint32_t millis(){return nowMillis;}
