/*
 * ===========================================================================
 * File:      TimeMachine/Clock.h
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
#include "Display.h"

class MDS_Display;

// Publish time snapshots to the same command queue used by HTTP and serial.
class Clock {
public:
	Clock(MDS_Time& time, MDS_Config& config, Display::CommandQueue& commands, MDS_Display& oled);
	bool begin();
	void tick();
private:
	bool publish(const struct tm* local);
	MDS_Time& time_;
	MDS_Config& config_;
	Display::CommandQueue& commands_;
	MDS_Display& oled_;
	uint32_t timeConfiguration_ = 0;
	int64_t timeOffset_ = 0;
	time_t lastPublished_ = 0;
	time_t lastLoggedMinute_ = 0;
	bool enabled_ = false;
	bool published_ = false;
	bool publishedValid_ = false;
};
