/*
 * ===========================================================================
 * File:      TimeMachine/TimeController.h
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

#include <MDS_Time.h>
#include <MDS_Config.h>

class MDS_Display;

// Apply application time settings and service NTP independently of display modes.
class TimeController {
public:
	// Borrow settings, the time service, and status output without starting synchronization.
	TimeController(MDS_Time& time, MDS_Config& config, MDS_Display& oled)
		: time_(time), config_(config), oled_(oled) {}

	bool begin(bool oledReady);
	void tick(bool networkReady);

private:
	bool configure();

	MDS_Time& time_;
	MDS_Config& config_;
	MDS_Display& oled_;
	String reportedError_;
	bool oledReady_ = false;
};
