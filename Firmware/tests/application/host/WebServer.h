/*
 * ===========================================================================
 * File:      tests/application/host/WebServer.h
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
#include <Arduino.h>
#include <LittleFS.h>
#include <functional>
#include <map>
#include <vector>

#define PROGMEM
#define PSTR(text) (text)

enum HTTPMethod {HTTP_GET,HTTP_POST};
enum HTTPUploadStatus {
 UPLOAD_FILE_START, UPLOAD_FILE_WRITE, UPLOAD_FILE_END, UPLOAD_FILE_ABORTED
};
struct HTTPUpload {
 HTTPUploadStatus status = UPLOAD_FILE_START;
 String name = "firmware";
 String filename;
 size_t currentSize = 0;
 size_t totalSize = 0;
 uint8_t buf[16]{};
};
class WebServer {
public:
 using THandlerFunction = std::function<void()>;
 inline static WebServer* current=nullptr;
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
 std::map<String, THandlerFunction> uploadHandlers;
 String currentUri;
 HTTPUpload incomingUpload;
 explicit WebServer(uint16_t value):port(value){current=this;}
 // Retain both the completion route and the optional multipart stream handler.
 void on(const char* path, HTTPMethod method, THandlerFunction handler, THandlerFunction upload = nullptr) {
  ++registrations;
  handlers[{path, method}] = handler;
  if (upload) {
   uploadHandlers[path] = upload;
  }
 }

 // Borrow the current simulated multipart upload chunk.
 HTTPUpload& upload() {
  return incomingUpload;
 }

 // Deliver one multipart event without dispatching the request completion handler.
 void chunk(HTTPUploadStatus status, size_t size = 0, const char* path = "/upload") {
  currentUri = path;
  incomingUpload.status = status;
  incomingUpload.currentSize = size;
  incomingUpload.totalSize = size;
  uploadHandlers.at(path)();
 }
 // Report the active request URI during multipart callbacks.
 const String& uri() const { return currentUri; }

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
 // Treat flash-resident response bodies as ordinary memory on the host.
 void send_P(int status, const char* type, const char* value) {
  send(status, type, value);
 }

 // Capture a streamed file as the response body for website route tests.
 size_t streamFile(File& file, const String& type) {
  String content;
  content.resize(file.size());
  const size_t length = file.readBytes(content.data(), content.size());
  content.resize(length);
  send(200, type.c_str(), content);
  return length;
 }

 // Dispatch a request directly so tests can inspect response and reboot ordering.
 void request(const char* path, std::vector<std::pair<String,String>> args = {}, HTTPMethod method = HTTP_GET) {
  currentUri = path;
  parameters = args;
  handlers.at({path, method})();
 }
};
