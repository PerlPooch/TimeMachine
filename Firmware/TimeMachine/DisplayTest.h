/*
 * ===========================================================================
 * File:      TimeMachine/DisplayTest.h
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

enum class DisplayTest {
	StartupShow,  // 2-second ramp, all-on hold, rolling alphabet/count
	Fade,         // Repeating gamma fade on all mounted digits
	Characters,   // DS1 A-Z/0-9 with DS4/DS5 counting 00-99
	SegmentWalk,  // Walk all 122 populated channels, then blank
	Thermal,      // 30s enabled/blank, 60s all-on, 60s single row, then clock
	PseudoClock   // OCT 24 2026 10:00:00, one second per real second
};

#ifndef DISPLAY_TEST_MODE
#define DISPLAY_TEST_MODE DisplayTest::StartupShow
#endif

constexpr DisplayTest ACTIVE_TEST = DISPLAY_TEST_MODE;
