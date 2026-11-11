/*
 * ===========================================================================
 * File:      tests/time/host/esp_sntp.h
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
#include <sys/time.h>
#include <string>
using sntp_sync_time_cb_t=void(*)(struct timeval*);
constexpr int ESP_SNTP_OPMODE_POLL=0;
struct FakeSNTP {
 sntp_sync_time_cb_t callback=nullptr;
 std::string servers[3];
 const char* borrowed[3]{};
 uint32_t interval=0;
 int starts=0,stops=0;
 bool running=false;
};
extern FakeSNTP sntp;
inline void esp_sntp_set_time_sync_notification_cb(sntp_sync_time_cb_t cb){sntp.callback=cb;}
inline void esp_sntp_stop(){for(int i=0;i<3;++i)if(sntp.borrowed[i])sntp.servers[i]=sntp.borrowed[i];sntp.running=false;++sntp.stops;}
inline void esp_sntp_setoperatingmode(int){}
inline void esp_sntp_setservername(int index,const char* name){sntp.servers[index]=name?name:"";sntp.borrowed[index]=name;}
inline void esp_sntp_set_sync_interval(uint32_t interval){sntp.interval=interval;}
inline void esp_sntp_init(){sntp.running=true;++sntp.starts;}
