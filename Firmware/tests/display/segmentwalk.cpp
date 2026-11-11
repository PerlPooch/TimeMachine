/*
 * ===========================================================================
 * File:      tests/display/segmentwalk.cpp
 * Firmware:  0.6.0
 * Git base:  4967da86d1b1
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

#include "Tests.h"
#include <MDS_Display.h>
#include <cassert>
#include <cstdio>

Stream Serial;
int enableLevel = 0;
uint32_t nowMillis = 0;

// These assertions also require the map and its entries to be compile-time values.
static_assert(DisplayMap::SegmentMap.size() == 118);
static_assert(DisplayMap::SegmentMap[0].address == 0x50);
static_assert(DisplayMap::SegmentMap[47].address == 0x7F);
static_assert(DisplayMap::SegmentMap[48].address == 0x00);
static_assert(DisplayMap::SegmentMap[61].address == 0x0D);
static_assert(DisplayMap::SegmentMap[62].address == 0x10);
static_assert(DisplayMap::SegmentMap[117].address == 0x4D);
static_assert(DisplayMap::SegmentMap[0].digitIndex == 0);
static_assert(DisplayMap::SegmentMap[0].segmentIndex == 0);
static_assert(DisplayMap::SegmentMap[117].digitIndex == 12);
static_assert(DisplayMap::SegmentMap[117].segmentIndex == 6);
static_assert(DisplayMap::DiscreteMap.size() == 4);
static_assert(DisplayMap::discreteAddress(DisplayMap::DiscreteIndex::AM) == 0x0E);
static_assert(DisplayMap::discreteAddress(DisplayMap::DiscreteIndex::PM) == 0x0F);
static_assert(DisplayMap::discreteAddress(DisplayMap::DiscreteIndex::Colon1) == 0x1E);
static_assert(DisplayMap::discreteAddress(DisplayMap::DiscreteIndex::Colon2) == 0x1F);

int main() {
	TwoWire wire;
	MatrixDriver matrix(wire, 21);
	Display display(matrix);
	MDS_Display oled;
	Tests tests(matrix, display, oled);
	assert(tests.begin());

	// Independently check the physical address sequence, every frame, and diagnostic labels.
	// Include the blank step and the first step of the next cycle.
	for (unsigned step = 0; step <= 123; ++step) {
		const unsigned position = step % 123;
		int address = -1;
		char message[40] = {};

		if (position < 118) {
			address = position < 48 ? 0x50 + position
				: ((position - 48) / 14) * 0x10 + (position - 48) % 14;
			const unsigned digit = position < 48 ? position / 16 : 3 + (position - 48) / 7;
			const unsigned segment = position < 48 ? position % 16 : (position - 48) % 7;
			snprintf(message, sizeof(message), "DS%u segment %u 0x%02X", digit + 1, segment + 1, address);
		} else if (position < 122) {
			constexpr uint8_t addresses[] = {0x0E, 0x0F, 0x1E, 0x1F};
			constexpr const char* names[] = {"AM", "PM", "Colon1", "Colon2"};
			address = addresses[position - 118];
			snprintf(message, sizeof(message), "%s 0x%02X", names[position - 118], address);
		}

		const auto messagesBefore = oled.messages.size();
		if (step) {
			const int calls = wire.calls;
			nowMillis += 149;
			tests.tick();
			assert(wire.calls == calls);
			++nowMillis;
			tests.tick();
		}

		for (unsigned channel = 0; channel < DisplayMap::ChannelSpan; ++channel) {
			assert(matrix.brightness(channel) == (int(channel) == address ? 255 : 0));
		}
		if (address >= 0) {
			assert(oled.messages.back() == message);
			assert(Serial.output.find(std::string(message) + "\n") != std::string::npos);
		} else {
			assert(oled.messages.size() == messagesBefore);
		}
	}
	puts("PASS: compile-time segment map, complete walk order/labels, single-segment frames, indicators, blank, timing, and wraparound.");
}
