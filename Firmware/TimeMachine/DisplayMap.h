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
#include <array>
#include <stddef.h>

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

	constexpr uint8_t DigitCount = sizeof(Digits) / sizeof(Digits[0]);

	// One digit segment; digit and segment indices are zero-based.
	struct SegmentMapEntry {
		uint8_t address;
		uint8_t digitIndex;
		uint8_t segmentIndex;
	};

	// Count digit segments, excluding the separate indicator LEDs.
	constexpr size_t countSegments() {
		size_t count = 0;

		for (const auto& digit : Digits) {
			count += digit.segmentCount;
		}

		return count;
	}

	// Expand each digit's consecutive addresses into a flat map at compile time.
	constexpr auto makeSegmentMap() {
		std::array<SegmentMapEntry, countSegments()> map{};
		size_t entry = 0;

		for (uint8_t digitIndex = 0; digitIndex < DigitCount; ++digitIndex) {
			const auto& digit = Digits[digitIndex];

			for (uint8_t segmentIndex = 0; segmentIndex < digit.segmentCount; ++segmentIndex) {
				map[entry++] = {
					uint8_t(digit.firstAddress + segmentIndex),
					digitIndex,
					segmentIndex
				};
			}
		}

		return map;
	}

	// Digit segments in left-to-right display order.
	inline constexpr auto SegmentMap = makeSegmentMap();

	// Map indices are distinct from LED-driver addresses.
	enum class DiscreteIndex : uint8_t {
		AM,
		PM,
		Colon1,
		Colon2,
		Count
	};

	// Keep each indicator's address beside its diagnostic label.
	struct DiscreteMapEntry {
		uint8_t address;
		const char* name;
	};

	// Entries follow DiscreteIndex order: AM, PM, then the two colon LEDs.
	inline constexpr std::array<DiscreteMapEntry, size_t(DiscreteIndex::Count)> DiscreteMap = {{
		{0x0E, "AM"},
		{0x0F, "PM"},
		{0x1E, "Colon1"},
		{0x1F, "Colon2"}
	}};

	// Resolve a named indicator to its driver address.
	constexpr uint8_t discreteAddress(DiscreteIndex index) {
		return DiscreteMap[static_cast<size_t>(index)].address;
	}

	constexpr uint8_t ChannelSpan = 0x80;
}
