/*
 * ===========================================================================
 * File:      TimeMachine/WeatherController.cpp
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

#include "WeatherController.h"
#include <stdio.h>

// Replace runtime timeZone from a successful weather result; normal time servicing applies it.
void WeatherController::applyTimezone() {
	const int32_t offset = weather_.sample().timezoneOffsetSeconds;
	const uint32_t seconds = offset < 0 ? uint32_t(-offset) : uint32_t(offset);

	char zone[20];

	// POSIX timezone signs are the reverse of UTC offsets from OpenWeather.
	snprintf(zone, sizeof(zone), "UTC%c%02lu:%02lu:%02lu", offset < 0 ? '+' : '-',
		static_cast<unsigned long>(seconds / 3600), static_cast<unsigned long>((seconds / 60) % 60),
		static_cast<unsigned long>(seconds % 60));

	JsonDocument update;
	update["timeZone"] = zone;

	if (!config_.set(update)) {
		Serial.println(config_.lastError());
	}
}

// Apply current weather settings and service retrieval when network and time are ready.
void WeatherController::tick(bool networkReady, bool clockReady) {
	MDS_Weather::Options options;
	options.apiKey = config_.get("owAppKey").as<const char*>();
	options.latitude = config_.get("latitude").as<const char*>();
	options.longitude = config_.get("longitude").as<const char*>();
	options.intervalSeconds = config_.get("weatherInterval").as<uint32_t>();

	weather_.begin(options);
	weather_.tick(networkReady, clockReady);
}
