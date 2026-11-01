/*
 * ===========================================================================
 * File:      TimeMachine/MatrixDriver.h
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

#include <Arduino.h>
#include <Wire.h>
#include "DisplayConfig.h"

// IS31FL3733B support for Rev A display diagnostics.
class MatrixDriver {
public:
	static constexpr uint32_t PWMFrequencyHz = 26700;
	static constexpr uint8_t DefaultGlobalCurrent = 0x02;

	// Bind the bus/SDB GPIO and application GCC; default to conservative current 0x02.
	explicit MatrixDriver(TwoWire& wire, uint8_t enablePin, uint8_t globalCurrent = DefaultGlobalCurrent)
		: wire_(wire), enablePin_(enablePin), initialCurrent_(globalCurrent), configuredCurrent_(globalCurrent), globalCurrent_(globalCurrent) {}

	// Initialize enabled channels using the configured GCC.
	bool begin(const uint8_t* enabledChannels, uint8_t channelCount);

	// Retain explicit-current initialization for existing callers and diagnostics.
	bool begin(uint8_t globalCurrent, const uint8_t* enabledChannels, uint8_t channelCount);

	// Set normal current; cache before initialization, apply immediately when ready.
	bool setConfiguredCurrent(uint8_t globalCurrent);

	// Temporary hardware override; leaves the configured current unchanged.
	bool setGlobalCurrent(uint8_t globalCurrent);

	// Apply an overall PWM ceiling (0-255), immediately refreshing the current frame.
	bool setBrightnessLimit(uint8_t maximumPWM);

	// Read cached logical brightness for temporary overlays; invalid addresses are dark.
	uint8_t brightness(uint8_t address) const {
		return address < sizeof(logicalBrightness_) ? logicalBrightness_[address] : 0;
	}

	// Report the runtime brightness ceiling independently of the output profile.
	uint8_t brightnessLimit() const { return brightnessLimit_; }

	// Report the active hardware current, or the selected value before initialization.
	uint8_t globalCurrent() const { return globalCurrent_; }

	// Report the constructor current value independently of later setting changes.
	uint8_t initialCurrent() const { return initialCurrent_; }

	// Report normal configured current independently of temporary hardware adjustments.
	uint8_t configuredCurrent() const { return configuredCurrent_; }

	// Report the output profile ceiling before the runtime brightness limit is applied.
	uint8_t maximumPWM() const { return maximumPWM_; }

	// Return the smaller ceiling actually used for gamma-corrected PWM output.
	uint8_t effectiveMaximumPWM() const {
		return maximumPWM_ < brightnessLimit_ ? maximumPWM_ : brightnessLimit_;
	}

	// Report whether initialization succeeded and no driver error has disabled output.
	bool ready() const { return ready_; }

	// Set maximumPWM (0-255) as the output ceiling used to scale gamma-corrected brightness.
	void setMaximumPWM(uint8_t maximumPWM) {
		maximumPWM_ = maximumPWM;
	}

	// All output values are logical brightness; correction happens once here.
	bool setBrightness(uint8_t address, uint8_t brightness);
	bool writeBrightness(uint8_t firstAddress, const uint8_t* brightness, uint8_t count);
	bool fillBrightness(uint8_t firstAddress, uint8_t count, uint8_t brightness);

	// Return the last I2C status or internal error code; zero means no recorded error.
	uint8_t lastError() const {
		return error_;
	}

private:
	bool write(uint8_t reg, const uint8_t* data, size_t count);
	bool writeByte(uint8_t reg, uint8_t value);
	bool page(uint8_t number);
	bool fail(uint8_t error);

	TwoWire& wire_;
	uint8_t enablePin_;
	uint8_t error_ = 0;
	bool ready_ = false;
	uint8_t maximumPWM_ = DISPLAY_DEV_MODE ? DISPLAY_DEV_MAX_PWM : 255;
	uint8_t brightnessLimit_ = 255;
	const uint8_t initialCurrent_;
	uint8_t configuredCurrent_;
	uint8_t globalCurrent_;
	uint8_t logicalBrightness_[192] = {};
	static constexpr uint8_t Address = 0x50;
};
