/*
 * ===========================================================================
 * File:      TimeMachine/DisplayMap.h
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

#include <stdint.h>

namespace DisplayMap {
	struct Digit {
		uint8_t firstAddress;
		uint8_t segmentCount;
	};

	// DS1-DS13, left to right; segment order matches the included fonts.
	constexpr Digit Digits[] = {
		{0x50, 16},
		{0x60, 16},
		{0x70, 16},
		{0x00, 7},
		{0x07, 7},
		{0x10, 7},
		{0x17, 7},
		{0x20, 7},
		{0x27, 7},
		{0x30, 7},
		{0x37, 7},
		{0x40, 7},
		{0x47, 7}
	};

	// D1/D2 are AM/PM; D3/D4 form the hours:minutes colon.
	constexpr uint8_t AM =		0x0E;
	constexpr uint8_t PM =		0x0F;
	constexpr uint8_t Colon1 =	0x1E;
	constexpr uint8_t Colon2 =	0x1F;

	constexpr uint8_t Discretes[] = {
		AM,
		PM,
		Colon1,
		Colon2
	};

	constexpr uint8_t DigitCount = sizeof(Digits) / sizeof(Digits[0]);
	constexpr uint8_t ChannelSpan = 0x80;
}
