/*
 * ===========================================================================
 * File:      TimeMachine/ApplicationApi.h
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

#include <functional>
#include <utility>
#include <MDS_HTTP.h>
#include <MDS_Config.h>
#include <MDS_Weather.h>
#include "MatrixDriver.h"
#include "Display.h"

// Application endpoint owner; dependencies and this object must outlive registered handlers.
class ApplicationApi {
public:
	// Borrow the HTTP server, application settings, and matrix driver.
	ApplicationApi(MDS_HTTP& http, MDS_Config& config, MatrixDriver& matrix, Display::CommandQueue& commands, MDS_Weather* weather = nullptr);
	ApplicationApi(const ApplicationApi&) = delete;
	ApplicationApi& operator=(const ApplicationApi&) = delete;

	using DisplayRouter = std::function<bool(const Display::DisplayCommand&)>;
	using SerialRouter = std::function<bool(uint8_t, const char*, size_t)>;
	using TimeSource = std::function<bool(int64_t&)>;

	// Install the callback that asks Application to accept selected serial destinations.
	void setDisplayRouter(DisplayRouter router) { displayRouter_ = std::move(router); }

	// Queue prepared configuration payloads through the shared serial router.
	void setSerialRouter(SerialRouter router) { serialRouter_ = std::move(router); }

	// Apply received configuration locally, without routing it onward.
	bool receiveConfigCommand(const uint8_t* payload, size_t length);

	// Supply synchronized, unshifted UTC for converting absolute dates to stored offsets.
	void setTimeSource(TimeSource source) { timeSource_ = std::move(source); }
	static size_t encodeDisplayCommand(const Display::DisplayCommand& command, char* output, size_t capacity);
	bool receiveDisplayCommand(const uint8_t* payload, size_t length);

	// Register config, display-action, and optional weather routes once.
	void begin();

private:
	// Access the existing HTTP server used for handler registration and responses.
	MDS_HTTPServer& server() { return http_.server(); }
	void on(const char* path, HTTPMethod method, MDS_HTTPServer::THandlerFunction handler);
	bool syncDisplaySettings(bool applyCurrent = false);
	void configRequest();
	bool applyConfig(const JsonDocument& updates);
	static bool addDestination(const String& value, uint8_t& destinations, bool& selected);
	void displayRequest();
	void weatherRequest();
	void sendConfig();
	void sendError(int code, const char* message, bool applied = false);
	const MDS_ConfigSetting* findSetting(const char* key) const;
	static bool parseUnsigned(const String& text, uint32_t& value);
	static bool parseSigned(const char* text, size_t length, int64_t& value);
	static bool parseRelativeOffset(const String& text, int64_t& value);
	static bool parseUTCDateTime(const String& text, int64_t& value);

	MDS_HTTP& http_;
	MDS_Config& config_;
	MatrixDriver& matrix_;
	MDS_Weather* weather_;
	DisplayRouter displayRouter_;
	TimeSource timeSource_;
	SerialRouter serialRouter_;
	Display::CommandQueue& commands_;
	bool registered_ = false;
};
