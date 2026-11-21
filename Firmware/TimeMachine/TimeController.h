/*
 * ===========================================================================
 * File:      TimeMachine/TimeController.h
 * Firmware:  0.8.0
 * Git base:  e576c1146b7c
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

#include <MDS_Time.h>
#include <MDS_Config.h>

// Apply application time settings and service NTP independently of display modes.
class TimeController {
public:
	using StatusCallback = MDS_Time::StatusCallback;

	// Borrow settings and the time service without starting synchronization.
	TimeController(MDS_Time& time, MDS_Config& config)
		: time_(time), config_(config) {}

	// Let the application choose diagnostic outputs; nullptr silences reporting.
	void setStatusCallback(StatusCallback callback) {
		statusCallback_ = callback;
	}

	// Apply the initial time settings.
	bool begin();

	// Apply current settings and service the time library.
	void tick();

private:
	// Translate application settings into time service options.
	bool configure();

	MDS_Time& time_;
	MDS_Config& config_;
	StatusCallback statusCallback_;
	String reportedError_;
};
