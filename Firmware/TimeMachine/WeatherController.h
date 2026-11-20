/*
 * ===========================================================================
 * File:      TimeMachine/WeatherController.h
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

#pragma once

#include <MDS_Weather.h>
#include <MDS_Config.h>

// Application-owned config adapter; weather does not read global settings itself.
class WeatherController {
public:
	// Borrow settings and register accepted updates; Application owns diagnostic output.
	WeatherController(MDS_Weather& weather, MDS_Config& config);

	// Release the update callback before the adapter's captured references expire.
	~WeatherController();

	// Keep the registered callback bound to its original adapter.
	WeatherController(const WeatherController&) = delete;

	// Prevent assignment from replacing an adapter that owns callback registration.
	WeatherController& operator=(const WeatherController&) = delete;

	// Apply current settings and service weather retrieval without blocking the display.
	void tick();

private:
	// Replace the configured timezone after the service accepts a new cached sample.
	void applyTimezone(const MDS_Weather::Sample& sample);

	MDS_Weather& weather_;
	MDS_Config& config_;
};
