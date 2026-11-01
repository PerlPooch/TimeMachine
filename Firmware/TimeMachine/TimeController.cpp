/*
 * ===========================================================================
 * File:      TimeMachine/TimeController.cpp
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

#include "TimeController.h"
#include <MDS_Display.h>

// Apply current timezone and NTP settings, reporting only changed configuration errors.
bool TimeController::configure() {
	MDS_Time::Options options;
	options.timeZone = config_.get("timeZone").as<const char*>();
	options.server1 = config_.get("ntpServer1").as<const char*>();
	options.server2 = config_.get("ntpServer2").as<const char*>();
	options.intervalSeconds = config_.get("ntpInterval").as<uint32_t>();

	if (!time_.begin(options)) {
		if (reportedError_ != time_.lastError()) {
			reportedError_ = time_.lastError();
			Serial.println(reportedError_);
			if (oledReady_) {
				oled_.D(reportedError_);
			}
		}

		return false;
	}

	reportedError_ = "";

	return true;
}

// Record OLED availability and apply the initial configured or default timezone.
bool TimeController::begin(bool oledReady) {
	oledReady_ = oledReady;
	return configure();
}

// Apply setting changes, including weather updates, before servicing network time.
void TimeController::tick(bool networkReady) {
	configure();
	time_.tick(networkReady);
}
