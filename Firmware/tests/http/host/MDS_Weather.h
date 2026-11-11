/*
 * ===========================================================================
 * File:      tests/http/host/MDS_Weather.h
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

#pragma once
#include "Arduino.h"
#include <ctime>
// Existing HTTP tests keep weather absent; full weather/API integration has its own fixture.
class MDS_Weather {
public:
 struct Sample {float temperatureF=0;double latitude=0,longitude=0;int32_t timezoneOffsetSeconds=0;uint32_t observedUtc=0;char condition[32]{},description[96]{};};
 bool enabled()const{return false;}
 bool configured()const{return false;}
 bool fetching()const{return false;}
 bool available()const{return false;}
 bool fresh()const{return false;}
 const char* state()const{return "disabled";}
 int httpStatus()const{return 0;}
 time_t lastSuccessUtc()const{return 0;}
 uint32_t nextUpdateSeconds()const{return 0;}
 uint32_t ageSeconds()const{return 0;}
 const char* lastError()const{return "";}
 const Sample& sample()const{static Sample value;return value;}
};
