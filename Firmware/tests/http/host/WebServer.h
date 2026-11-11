/*
 * ===========================================================================
 * File:      tests/http/host/WebServer.h
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
#include <LittleFS.h>
#include <functional>
#include <map>
#include <vector>
enum HTTPMethod {HTTP_GET,HTTP_POST};
class WebServer {
public:
 using THandlerFunction = std::function<void()>;
 struct StaticRoute {
  String uri;
  String path;
  FakeFS* filesystem;
 };
 std::vector<StaticRoute> staticRoutes;
 uint16_t port;
 int starts=0,stops=0,ticks=0,registrations=0,code=0;
 String body,contentType;
 std::map<String,String> headers;
 std::vector<std::pair<String,String>> parameters;
 std::map<std::pair<String,HTTPMethod>,std::function<void()>> handlers;
 explicit WebServer(uint16_t value):port(value){}
 void on(const char* path,HTTPMethod method,std::function<void()> handler){++registrations;handlers[{path,method}]=handler;}
 // Record native static handlers; file streaming belongs to the ESP32 core.
 void serveStatic(const char* uri, FakeFS& fs, const char* path) {
  ++registrations;
  staticRoutes.push_back({uri, path, &fs});
 }
 void begin(){++starts;}
 void stop(){++stops;}
 void handleClient(){++ticks;}
 int args() const{return parameters.size();}
 String argName(int index) const{return parameters[index].first;}
 String arg(int index) const{return parameters[index].second;}
 void sendHeader(const char* name,const char* value){headers[name]=value;}
 void send(int status,const char* type,const String& value){code=status;contentType=type;body=value;}
 void request(const char* path,std::vector<std::pair<String,String>> args={}){parameters=args;handlers.at({path,HTTP_GET})();}
};
