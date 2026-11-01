/*
 * ===========================================================================
 * File:      TimeMachine/Clock.cpp
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

#include "Clock.h"
#include <MDS_Display.h>
#include <limits>

// Borrow time, configuration, status output, and command submission without access to the renderer.
Clock::Clock(MDS_Time& time, MDS_Config& config, Display::CommandQueue& commands, MDS_Display& oled)
	: time_(time), config_(config), commands_(commands), oled_(oled) {}

// Enable clock publishing and submit the initial waiting-for-time state.
bool Clock::begin() {
	enabled_ = true;

	timeConfiguration_ = time_.configurationVersion();
	timeOffset_ = config_.get("timeOffset").as<int64_t>();

	lastPublished_ = lastLoggedMinute_ = 0;
	published_ = publishedValid_ = false;

	if (publish(nullptr)) published_ = true;

	oled_.D("Clock: waiting for NTP");

	return true;
}

// Publish calendar and indicator state through the shared display queue.
bool Clock::publish(const struct tm* local) {
	Display::DisplayCommand command;

	command.kind = Display::DisplayCommand::Kind::Clock;
	command.timeValid = local != nullptr;

	if (local) {
		command.am = local->tm_hour < 12;
		command.pm = !command.am;
		command.colonsBlink = true;

		command.dateTime.year = local->tm_year + 1900;
		command.dateTime.month = local->tm_mon + 1;
		command.dateTime.day = local->tm_mday;
		command.dateTime.hour = local->tm_hour;
		command.dateTime.minute = local->tm_min;
		command.dateTime.second = local->tm_sec;
	}

	return commands_.post(command);
}

// Publish changed local time or configuration, retrying when a snapshot is not accepted.
void Clock::tick() {
	if (!enabled_) return;

	const int64_t offset = config_.get("timeOffset").as<int64_t>();
	if (timeConfiguration_ != time_.configurationVersion() || timeOffset_ != offset) {
		timeConfiguration_ = time_.configurationVersion();
		timeOffset_ = offset;
		published_ = false;
		lastLoggedMinute_ = 0;
	}

	const int64_t now = time_.utcNow();
	struct tm local;
	time_t shifted = 0;
	bool valid = time_.valid();

	// Add elapsed seconds to UTC before timezone conversion, guarding both arithmetic and calendar range.
	if (valid) {
		valid = !((offset > 0 && now > INT64_MAX - offset)
			|| (offset < 0 && now < INT64_MIN - offset));
		if (valid) {
			const int64_t seconds = now + offset;
			valid = seconds >= (std::numeric_limits<time_t>::min)()
				&& seconds <= (std::numeric_limits<time_t>::max)();
			if (valid) {
				shifted = static_cast<time_t>(seconds);
				valid = localtime_r(&shifted, &local) != nullptr
					&& local.tm_year >= -1899 && local.tm_year <= 8099;
			}
		}
	}

	if (!valid) {
		if ((!published_ || publishedValid_) && publish(nullptr)) {
			published_ = true;
			publishedValid_ = false;
			lastPublished_ = 0;
		}
		return;
	}

	if (published_ && publishedValid_ && shifted == lastPublished_) return;
	if (!publish(&local)) return;

	published_ = publishedValid_ = true;
	lastPublished_ = shifted;

	if (shifted / 60 != lastLoggedMinute_) {
		char message[48];
		strftime(message, sizeof(message), "%b %d %Y %I:%M:%S %p %Z", &local);
		Serial.println(message);
		lastLoggedMinute_ = shifted / 60;
	}
}
