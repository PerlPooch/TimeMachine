/*
 * ===========================================================================
 * File:      tests/application/host/MDS_WiFi.h
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
#include "Arduino.h"
#include <functional>
#include <esp_wifi.h>
class MDS_WiFi {
public:
 enum class State {
  Stopped, Starting, WaitingForCredentials, Provisioning,
  Connecting, WaitingToRetry, Ready, RebootRequested
 };
 State state_ = State::Stopped;

 // Expose the startup snapshot used by application diagnostics.
 State state() const {
  return state_;
 }

 // Match the library's diagnostic names without requiring platform Wi-Fi services.
 static const char* stateName(State state) {
  switch (state) {
   case State::Stopped:
    return "Stopped";
   case State::Starting:
    return "Starting";
   case State::WaitingForCredentials:
    return "WaitingForCredentials";
   case State::Provisioning:
    return "Provisioning";
   case State::Connecting:
    return "Connecting";
   case State::WaitingToRetry:
    return "WaitingToRetry";
   case State::Ready:
    return "Ready";
   case State::RebootRequested:
    return "RebootRequested";
  }

  return "Unknown";
 }

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
 // Preserve boolean startup semantics while publishing the observed lifecycle state.
 bool begin(const Options& options) {
  policy = options.connectionPolicy;
  ++begins;
  portal = !online && !savedWiFi.sta.ssid[0]
   && policy == ConnectionPolicy::RetrySavedNetworkAsync;
  state_ = online ? State::Ready : (portal ? State::Provisioning : State::Connecting);

  return online || portal;
 }
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
