/*
 * ===========================================================================
 * File:      TimeMachine/Tests.h
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

#include "MatrixDriver.h"
#include "Display.h"
#include "ClockCalendar.h"
#include "DisplayMap.h"

class MDS_Display;

// Owns display diagnostics; the application initializes the bus and services the OLED.
class Tests {
public:
	// Bind the matrix driver and OLED used for test output and status messages.
	Tests(MatrixDriver& matrix, Display& display, MDS_Display& oled);

	// Initialize the matrix and start the selected diagnostic; report failures.
	bool begin();

	// Print the selected diagnostic and its current/brightness/timing settings.
	void printConfig(Stream& output = Serial) const;

	// Advance the active diagnostic when its next frame is due.
	void tick();

private:
	enum class ThermalPhase {
		Blank,
		FullLoad,
		SingleRow,
		Clock
	};

	enum class AnimationPhase {
		Stopped,
		Ramp,
		AllOn,
		Count
	};

	void matrixFailure();
	bool showAllSegments(uint8_t brightness);
	bool showCount(uint32_t step);
	void animationTick();
	bool showText(const char* text);
	bool showClock();
	bool showCharacters(uint32_t step);
	bool showWalk(uint32_t step);
	bool showThermalRow();
	void thermalTick();
	bool startTest();

	MatrixDriver& matrix_;
	Display& display_;
	MDS_Display& oled_;

	bool testRunning = false;
	uint32_t lastTestTick = 0;
	uint32_t testStep = 0;
	ClockCalendar::DateTime pseudoTime;

	ThermalPhase thermalPhase = ThermalPhase::Blank;
	uint32_t thermalStarted = 0;

	AnimationPhase animationPhase = AnimationPhase::Stopped;
	uint32_t phaseStarted = 0;
	uint32_t lastFrame = 0;
	uint32_t countStep = 0;
	uint8_t previousBrightness = 0;
};
