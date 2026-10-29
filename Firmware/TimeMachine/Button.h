#pragma once

#include <Arduino.h>

// Active-low hardware input; application policy decides what a press means.
class Button {
public:
	explicit Button(uint8_t pin) : pin_(pin) {}

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

	bool pressed() const { return pressed_; }
	bool rawPressed() const { return rawPressed_; }

private:
	static constexpr uint32_t DebounceMs = 30;

	const uint8_t pin_;
	bool pressed_ = false;
	bool rawPressed_ = false;
	uint32_t changedAt_ = 0;
};
