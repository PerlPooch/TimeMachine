/*
 * ===========================================================================
 * File:      tests/application/host/MDS_WiFi.h
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
#include <functional>
#include <esp_wifi.h>
class MDS_WiFi {
public:
 enum class ConnectionPolicy {Legacy,RetrySavedNetwork,RetrySavedNetworkAsync};
 struct Options {const char* accessPointPrefix="";const char* portalTitle="";ConnectionPolicy connectionPolicy=ConnectionPolicy::Legacy;};
 inline static MDS_WiFi* current=nullptr;
 ConnectionPolicy policy=ConnectionPolicy::Legacy;
 bool eraseOK=true;int erases=0;
 bool eraseCredentials(){++erases;return eraseOK;}
 bool online=true,reboot=false;int begins=0,ticks=0;
 bool portal=false,acceptCredentials=true;
 int credentialCalls=0,credentialChanges=0;
 String receivedSSID,receivedPassword;
 std::function<void(const String&)> callback;
 MDS_WiFi(){current=this;}
 void setStatusCallback(std::function<void(const String&)> value){callback=value;}
 bool begin(const Options& options){policy=options.connectionPolicy;++begins;portal=!online&&!savedWiFi.sta.ssid[0]&&policy==ConnectionPolicy::RetrySavedNetworkAsync;return online||portal;}
 // Model library acceptance and idempotence while retaining observable call counts.
 bool setCredentials(const char* ssid,const char* password){
  ++credentialCalls;
  if(!acceptCredentials)return false;
  receivedSSID=ssid;receivedPassword=password;
  wifi_config_t replacement{};
  memcpy(replacement.sta.ssid,ssid,strlen(ssid));
  memcpy(replacement.sta.password,password,strlen(password));
  if(memcmp(&savedWiFi,&replacement,sizeof(savedWiFi))||portal)++credentialChanges;
  savedWiFi=replacement;portal=false;
  return true;
 }
 bool ready() const{return online;}
 bool rebootRequested() const{return reboot;}
 void tick(){++ticks;}
};
