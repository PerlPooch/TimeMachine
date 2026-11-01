/*
 * ===========================================================================
 * File:      TimeMachine/TimeMachine.ino
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

#define VERSION				"0.5.1"

#include <Arduino.h>
#include <Wire.h>
#include <MDS_Display.h>
#include "MatrixDriver.h"
#include "Application.h"
#include "Button.h"

namespace Board {
	constexpr uint8_t SDA_PIN = 22;
	constexpr uint8_t SCL_PIN = 23;
	constexpr uint8_t DISPLAY_ENABLE = 21;
	constexpr uint8_t MASTER_JUMPER = 19;
	constexpr uint8_t BUTTON_PIN = 9;
}

MDS_Display oled(Wire, 0x3C, 2, 100000);
MatrixDriver matrix(Wire, Board::DISPLAY_ENABLE);
Button button(Board::BUTTON_PIN);
Application application(Application::Mode::Clock, matrix, oled, button);

// Own board setup and pass initialized hardware to the application.
void setup() {
	// Keep the matrix blank until checked initialization completes.
	digitalWrite(Board::DISPLAY_ENABLE, LOW);
	pinMode(Board::DISPLAY_ENABLE, OUTPUT);
	pinMode(Board::MASTER_JUMPER, INPUT_PULLUP);
	Serial.begin(115200);

	// Allow USB CDC to attach without requiring a connected serial monitor.
	const uint32_t serialStarted = millis();

	while (!Serial && uint32_t(millis() - serialStarted) < 1500) {
		delay(10);
	}

	const char* boardRole = digitalRead(Board::MASTER_JUMPER) == LOW ? "Primary" : "Secondary";
	Serial.println();
	Serial.println("Time Machine, V " VERSION);
	Serial.print("Board: Rev A / ");
	Serial.println(boardRole);
	button.begin();
//	Serial.println(button.pressed() ? "Button: pressed" : "Button: released");

	const bool busReady = Wire.begin(Board::SDA_PIN, Board::SCL_PIN, 100000);
	const bool oledReady = busReady && oled.setup();
	application.begin(VERSION, boardRole, busReady, oledReady);
}

// Advance application work and yield briefly between service passes.
void loop() {
	application.tick();
	delay(1);
}
