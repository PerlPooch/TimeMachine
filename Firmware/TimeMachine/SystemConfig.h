/*
 * ===========================================================================
 * File:      TimeMachine/SystemConfig.h
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

#include <MDS_Config.h>
#include <MDS_Time.h>
#include <MDS_Weather.h>
#include "DisplayGamma.h"
#include "MatrixDriver.h"

namespace SystemConfig {
	using Type = MDS_ConfigType;
	using Access = MDS_ConfigAccess;

	// Application definitions only; MDS_Config owns values, validation, and storage.
	constexpr MDS_ConfigSetting Settings[] = {
		{"schema", Type::Unsigned, Access::ReadOnly,  true, nullptr,        1, 1,  1, true},
		{"version", Type::Text, Access::ReadOnly, false, "", 0, 0, 32},
		{"name",   Type::Text,     Access::ReadWrite, true, "Time Machine", 0, 1, 32},
		{"primary", Type::Boolean, Access::ReadWrite, true, nullptr, 0, 0, 1},
		{"ipA", Type::Text, Access::ReadOnly, false, "", 0, 0, 15},
		{"ipB", Type::Text, Access::ReadOnly, false, "", 0, 0, 15},
		{"pwm",    Type::Unsigned, Access::ReadWrite, true, nullptr, DisplayGamma::DefaultMaximumPWM, 0, 255},
		{"gcc",    Type::Unsigned, Access::ReadWrite, true, nullptr, 0x04, 0, 255},
		{"timeZone",   Type::Text, Access::ReadWrite, true, MDS_Time::DefaultTimeZone, 0, 1, 128},
		{"timeOffset", Type::Signed, Access::ReadWrite, false, nullptr, 0, 0, 0},
		{"ntpServer1", Type::Text, Access::ReadWrite, true, MDS_Time::DefaultServer1,  0, 1,  64},
		{"ntpServer2", Type::Text, Access::ReadWrite, true, MDS_Time::DefaultServer2,  0, 0,  64},
		{"ntpInterval", Type::Unsigned, Access::ReadWrite, true, nullptr, MDS_Time::DefaultInterval, 60, 86400},
		{"owAppKey", Type::Text, Access::ReadWrite, true, "", 0, 0, 64, false, true, MDS_Weather::validApiKey},
		{"latitude", Type::Text, Access::ReadWrite, true, "", 0, 0, 24, false, false, MDS_Weather::validLatitude},
		{"longitude", Type::Text, Access::ReadWrite, true, "", 0, 0, 24, false, false, MDS_Weather::validLongitude},
		{"weatherInterval", Type::Unsigned, Access::ReadWrite, true, nullptr, 600, 60, 86400}
	};
}
