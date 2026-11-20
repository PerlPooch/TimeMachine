/*
 * ===========================================================================
 * File:      tests/application/upload.cpp
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

#include "Application.h"
#include <MDS_Display.h>
#include <esp_sntp.h>
#include <HTTPClient.h>
#include <freertos/task.h>
#include <LittleFS.h>
#include <Update.h>
#include <cassert>

Stream Serial;
FakeFS LittleFS;
FakeSNTP sntp;
FakeESP ESP;
int buttonLevel = HIGH;
int enableLevel = 0;
uint32_t nowMillis = 0;
bool failQueue = false, failTask = false, bundleUsed = false;
std::vector<PendingTask> tasks;
HttpState httpState;

// Keep time deterministic without requiring NTP or network access.
time_t fakeTime(time_t* value) {
 const time_t utc = 1792900800;
 if (value) {
  *value = utc;
 }
 return utc;
}

// Verify that website uploads stay live and firmware alone requests a reboot.
int main() {
 TwoWire wire;
 MatrixDriver matrix(wire, 21);
 MDS_Display oled;
 Button button(9);
 button.begin();
 Application app(Application::Mode::Clock, matrix, oled, button);
 assert(app.begin("0.6.0", false, true, true));
 auto& server = *WebServer::current;

 server.request("/upload");
 assert(server.code == 200 && server.body.find("multipart/form-data") != String::npos);
 server.request("/upload-file");
 assert(server.code == 200 && server.contentType == "text/html; charset=utf-8");
 assert(server.body.find("Destination path") != String::npos);

 // A board without index.html returns 404 even if the old index.htm exists.
 LittleFS.files["/www/index.htm"] = "old default";
 server.request("/");
 assert(server.code == 404);

 server.request("/api/config", {{"name", "Keep My Settings"}});
 assert(server.code == 200);
 const auto savedConfig = LittleFS.files.at("/config.json");

 // Website uploads preserve the display and publish only after the POST callback.
 Display renderer(matrix);
 const auto beforeWebsiteUpload = renderer.frame();
 server.incomingUpload.name = "file";
 server.incomingUpload.filename = "assets/style.css";
 server.incomingUpload.buf[0] = 'x';
 server.chunk(UPLOAD_FILE_START, 0, "/upload-file");
 server.chunk(UPLOAD_FILE_WRITE, 1, "/upload-file");
 server.chunk(UPLOAD_FILE_END, 1, "/upload-file");
 assert(!LittleFS.exists("/www/assets/style.css"));
 server.request("/upload-file", {}, HTTP_POST);
 assert(server.code == 200 && LittleFS.files.at("/www/assets/style.css") == "x");
 assert(LittleFS.files.at("/config.json") == savedConfig);
 assert(Update.commits == 0 && ESP.restarts == 0);
 for (size_t channel = 0; channel < DisplayMap::ChannelSpan; ++channel) {
  assert(matrix.brightness(channel) == beforeWebsiteUpload.channels[channel]);
 }

 // The root route discovers index.html uploaded after the listener started.
 server.incomingUpload.filename = "index.html";
 server.incomingUpload.buf[0] = 'H';
 server.chunk(UPLOAD_FILE_START, 0, "/upload-file");
 server.chunk(UPLOAD_FILE_WRITE, 1, "/upload-file");
 server.chunk(UPLOAD_FILE_END, 1, "/upload-file");
 server.request("/upload-file", {}, HTTP_POST);
 assert(server.code == 200);
 server.request("/");
 assert(server.code == 200 && server.contentType == "text/html" && server.body == "H");

 // Reject a path outside /www and leave the previous asset intact on abort.
 server.incomingUpload.filename = "../config.json";
 server.chunk(UPLOAD_FILE_START, 0, "/upload-file");
 server.request("/upload-file", {}, HTTP_POST);
 assert(server.code == 500 && LittleFS.files.at("/config.json") == savedConfig);

 server.incomingUpload.filename = "assets/style.css";
 server.incomingUpload.buf[0] = 'y';
 server.chunk(UPLOAD_FILE_START, 0, "/upload-file");
 server.chunk(UPLOAD_FILE_WRITE, 1, "/upload-file");
 server.chunk(UPLOAD_FILE_ABORTED, 0, "/upload-file");
 server.request("/upload-file", {}, HTTP_POST);
 assert(server.code == 500 && LittleFS.files.at("/www/assets/style.css") == "x");
 assert(!LittleFS.exists("/www/.mds-http-upload.tmp"));
 server.incomingUpload.name = "firmware";
 const auto savedFiles = LittleFS.files;

 // Missing, aborted, and invalid images must leave the application running.
 server.request("/upload", {}, HTTP_POST);
 assert(server.code == 500);
 app.tick();
 assert(ESP.restarts == 0);

 server.chunk(UPLOAD_FILE_START);
 // Upload start must replace the startup overlay before any loop or firmware write.
 Display::Frame uploadFrame;
 renderer.drawText(uploadFrame, "    UPLOAD");
 for (size_t channel = 0; channel < DisplayMap::ChannelSpan; ++channel) {
  assert(matrix.brightness(channel) == uploadFrame.channels[channel]);
 }
 server.chunk(UPLOAD_FILE_WRITE, 16);
 server.chunk(UPLOAD_FILE_ABORTED);
 server.request("/upload", {}, HTTP_POST);
 assert(server.code == 500 && Update.aborts == 1);
 app.tick();
 assert(ESP.restarts == 0);

 Update.endOK = false;
 server.chunk(UPLOAD_FILE_START);
 server.chunk(UPLOAD_FILE_WRITE, 16);
 server.chunk(UPLOAD_FILE_END, 16);
 server.request("/upload", {}, HTTP_POST);
 assert(server.code == 500 && Update.commits == 0);
 app.tick();
 assert(ESP.restarts == 0);

 // Keep the response socket alive for one second, including across millis rollover.
 nowMillis = UINT32_MAX - 500;
 Update.endOK = true;
 server.chunk(UPLOAD_FILE_START);
 server.chunk(UPLOAD_FILE_WRITE, 16);
 server.chunk(UPLOAD_FILE_END, 16);
 server.request("/upload", {}, HTTP_POST);
 assert(server.code == 200 && Update.commits == 1);
 assert(ESP.restarts == 0 && app.state() != Application::State::Rebooting);
 assert(LittleFS.files == savedFiles);
 app.tick();
 assert(ESP.restarts == 0 && server.stops == 0);
 nowMillis += 999;
 app.tick();
 assert(ESP.restarts == 0 && server.stops == 0);
 ++nowMillis;
 app.tick();
 assert(ESP.restarts == 1 && app.state() == Application::State::Rebooting);
 assert(server.stops == 1);
 app.tick();
 assert(ESP.restarts == 1 && LittleFS.files == savedFiles);

 puts("PASS: browser forms, live index alias, website publishing and abort, settings and display preservation, firmware response-before-reboot, and single reboot.");
}
