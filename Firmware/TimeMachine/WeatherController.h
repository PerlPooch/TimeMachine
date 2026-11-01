/*
 * ===========================================================================
 * File:      TimeMachine/WeatherController.h
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

#include <MDS_Weather.h>
#include <MDS_Config.h>

// Application-owned config adapter; weather does not read global settings itself.
class WeatherController {
public:
	// Borrow settings and the weather service; Application installs the status callback.
	WeatherController(MDS_Weather& weather, MDS_Config& config) : weather_(weather), config_(config) {}

	// Apply current settings and service weather retrieval without blocking the display.
	void tick(bool networkReady, bool clockReady);

	// Called after a successful weather update to replace the configured timezone.
	void applyTimezone();

private:
	MDS_Weather& weather_;
	MDS_Config& config_;
};
