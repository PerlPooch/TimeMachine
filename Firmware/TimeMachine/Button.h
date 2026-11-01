/*
 * ===========================================================================
 * File:      TimeMachine/Button.h
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

// Active-low hardware input; application policy decides what a press means.
class Button {
public:
	// Bind the input pin while leaving hardware initialization to begin().
	explicit Button(uint8_t pin) : pin_(pin) {}

	// Initialize the pull-up input and capture the starting physical and debounced states.
	void begin() {
		pinMode(pin_, INPUT_PULLUP);
		pressed_ = rawPressed_ = digitalRead(pin_) == LOW;
		changedAt_ = millis();
	}

	// Report one transition after the input has been stable for the debounce period.
	bool tick() {
		const uint32_t now = millis();
		const bool pressed = digitalRead(pin_) == LOW;

		if (pressed != rawPressed_) {
			rawPressed_ = pressed;
			changedAt_ = now;
		}

		if (pressed_ != rawPressed_ && uint32_t(now - changedAt_) >= DebounceMs) {
			pressed_ = rawPressed_;
			return true;
		}

		return false;
	}

	// Report the stable input state used for press and hold decisions.
	bool pressed() const { return pressed_; }

	// Expose the most recent physical sample for release checks during debounce.
	bool rawPressed() const { return rawPressed_; }

private:
	static constexpr uint32_t DebounceMs = 30;

	const uint8_t pin_;
	bool pressed_ = false;
	bool rawPressed_ = false;
	uint32_t changedAt_ = 0;
};
