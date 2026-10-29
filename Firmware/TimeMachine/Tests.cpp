#include "Tests.h"
#include <MDS_Display.h>
#include "DisplayConfig.h"
#include "DisplayGamma.h"
#include "DisplayTest.h"

namespace {
	constexpr uint8_t TEST_CURRENT =		0x04;
	constexpr uint32_t THERMAL_BLANK_MS =	10000;
	constexpr uint32_t THERMAL_LOAD_MS =	30000;
	constexpr uint32_t THERMAL_ROW_MS =		2000;
	constexpr uint8_t THERMAL_ROW_SW = 		6;  // DS1: all 16 CS channels are wired.
	static_assert(THERMAL_ROW_SW >= 6 && THERMAL_ROW_SW <= 8,
		"Choose SW6-SW8 for a fully wired 16-segment row");

	constexpr uint8_t TEST_BRIGHTNESS =		0xFF;
	constexpr uint8_t TEST_MAX_PWM =		0xFF;
	constexpr uint32_t RAMP_MS =			2000;
	constexpr uint32_t ALL_ON_HOLD_MS =		10000;
	constexpr uint32_t FRAME_MS =			20;
	constexpr uint32_t COUNT_MS =			80;
	constexpr uint32_t WALK_MS =			50;
	constexpr uint8_t WALK_STEPS =		123;  // 118 segments, 4 discretes, then blank.
}

using DisplayMap::Digits;

Tests::Tests(MatrixDriver& matrix, Display& display, MDS_Display& oled)
	: matrix_(matrix), display_(display), oled_(oled) {}

bool Tests::begin() {
	testRunning = false;
	animationPhase = AnimationPhase::Stopped;
	matrix_.setMaximumPWM(DisplayGamma::DefaultMaximumPWM);

	if (!display_.begin()) {
		matrixFailure();
		return false;
	}

	Serial.printf("U1: all 13 digits and 4 discretes, GCC=0x%02X, gamma=%u, dev=%u\n",
		matrix_.globalCurrent(), DISPLAY_GAMMA_ENABLED, DISPLAY_DEV_MODE);
	oled_.D(DISPLAY_DEV_MODE ? "USB/dev brightness" : "Production brightness");

	if (!startTest()) {
		matrixFailure();
		return false;
	}

	return true;
}

// Print implementation settings without changing test state or touching the hardware.
void Tests::printConfig(Stream& output) const {
	const char* mode = "Unknown";

	switch (ACTIVE_TEST) {
		case DisplayTest::StartupShow: mode = "StartupShow"; break;
		case DisplayTest::Fade: mode = "Fade"; break;
		case DisplayTest::Characters: mode = "Characters"; break;
		case DisplayTest::SegmentWalk: mode = "SegmentWalk"; break;
		case DisplayTest::Thermal: mode = "Thermal"; break;
		case DisplayTest::PseudoClock: mode = "PseudoClock"; break;
	}

	output.print("Display: mode = ");
	output.println(mode);
	const auto printByte = [&output](const char* label, uint8_t value) {
		char text[5];
		snprintf(text, sizeof(text), "0x%02X", value);
		output.print(label);
		output.println(text);
	};

	printByte("Display: application GCC = ", matrix_.configuredCurrent());
	printByte("Display: thermal GCC = ", TEST_CURRENT);
	printByte("Display: logical brightness = ", TEST_BRIGHTNESS);
	printByte("Display: test maximum PWM = ", TEST_MAX_PWM);
	printByte("Display: normal maximum PWM = ", DisplayGamma::DefaultMaximumPWM);
	output.print("Display: gamma = ");
	output.println(DISPLAY_GAMMA_ENABLED ? "2.2" : "linear");
	output.print("Display: dev profile = ");
	output.println(DISPLAY_DEV_MODE ? "enabled" : "disabled");
	output.print("Display: PWM frequency Hz = ");
	output.println(MatrixDriver::PWMFrequencyHz);
	output.print("Display: segment walk ms = ");
	output.println(WALK_MS);
}

// Stop the active test and report the matrix driver error on Serial and OLED.
void Tests::matrixFailure() {
	testRunning = false;
	animationPhase = AnimationPhase::Stopped;

	char message[40];
	snprintf(message, sizeof(message), "U1 I2C error: %u", matrix_.lastError());
	Serial.println(message);
	oled_.D(message);
	oled_.D("Matrix disabled");
}

// Light every mapped digit segment and discrete at logical brightness (0-255).
bool Tests::showAllSegments(uint8_t brightness) {
	uint8_t frame[DisplayMap::ChannelSpan] = {};

	for (const auto& digit : Digits) {
		for (uint8_t segment = 0; segment < digit.segmentCount; ++segment) {
			frame[digit.firstAddress + segment] = brightness;
		}
	}

	for (uint8_t address : DisplayMap::Discretes) {
		frame[address] = brightness;
	}

	return matrix_.writeBrightness(0, frame, sizeof(frame));
}

// Render the rolling alphabet and numeric-wheel frame selected by animation step.
bool Tests::showCount(uint32_t step) {
	char text[DisplayMap::DigitCount + 1] = {};

	for (uint8_t index = 0; index < DisplayMap::DigitCount; ++index) {
		if (Digits[index].segmentCount == 16) {
			// Rolling alphabet triplet: ABC, BCD, ...; slower than the numbers.
			text[index] = 'A' + ((step / 3 + index) % 26);
		} else {
			// Staggered wheels: 0123456789, 1234567890, ...
			text[index] = '0' + ((step + index - 3) % 10);
		}
	}

	return showText(text);
}

// Advance the startup ramp, all-on hold, or rolling count when its next frame is due.
void Tests::animationTick() {
	if (animationPhase == AnimationPhase::Stopped) {
		return;
	}

	const uint32_t now = millis();

	if (animationPhase == AnimationPhase::Ramp) {
		const uint32_t elapsed = uint32_t(now - phaseStarted);

		if (uint32_t(now - lastFrame) < FRAME_MS && elapsed < RAMP_MS) {
			return;
		}

		lastFrame = now;
		const uint8_t brightness = elapsed >= RAMP_MS
			? TEST_BRIGHTNESS : uint32_t(TEST_BRIGHTNESS) * elapsed / RAMP_MS;

		if (brightness != previousBrightness && !showAllSegments(brightness)) {
			matrixFailure();
			return;
		}

		previousBrightness = brightness;

		if (elapsed >= RAMP_MS) {
			animationPhase = AnimationPhase::AllOn;
			phaseStarted = now;
		}

		return;
	}

	if (animationPhase == AnimationPhase::AllOn) {
		if (uint32_t(now - phaseStarted) < ALL_ON_HOLD_MS) {
			return;
		}

		if (!showCount(0)) {
			matrixFailure();
			return;
		}

		countStep = 0;
		lastFrame = now;
		animationPhase = AnimationPhase::Count;
		Serial.println("Rolling alphabet and numeric wheels: 80 ms/step");
		oled_.D("Rapid count / alphabet");
		return;
	}

	const uint32_t elapsed = uint32_t(now - lastFrame);

	if (elapsed < COUNT_MS) {
		return;
	}

	const uint32_t steps = elapsed / COUNT_MS;
	lastFrame += steps * COUNT_MS;
	// 780 steps is a common repeat period for numeric and alphabet wheels.
	countStep = (countStep + steps % 780) % 780;

	if (!showCount(countStep)) {
		matrixFailure();
	}
}

// Render the 13 characters in text, one per mapped digit, without an intervening blank frame.
bool Tests::showText(const char* text) {
	Display::Frame frame;

	display_.drawText(frame, text, TEST_BRIGHTNESS);
	return display_.show(frame);
}

// Render pseudoTime on the main display and report its seconds on Serial and OLED.
bool Tests::showClock() {
	char text[14];
	snprintf(text, sizeof(text), "%s%02u%04u%02u%02u",
		ClockCalendar::Months[pseudoTime.month - 1], pseudoTime.day,
		pseudoTime.year, pseudoTime.hour, pseudoTime.minute);

	if (!showText(text)) {
		return false;
	}

	char message[40];
	snprintf(message, sizeof(message), "%s %02u %04u %02u:%02u:%02u",
		ClockCalendar::Months[pseudoTime.month - 1], pseudoTime.day,
		pseudoTime.year, pseudoTime.hour, pseudoTime.minute, pseudoTime.second);
	Serial.println(message);
	oled_.D(message);
	return true;
}

// Use step to select the DS1 character and DS4/DS5 count; leave other digits dark.
bool Tests::showCharacters(uint32_t step) {
	constexpr char characters[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
	char text[14] = "             ";
	text[0] = characters[step % (sizeof(characters) - 1)];
	text[3] = '0' + (step / 10) % 10;
	text[4] = '0' + step % 10;

	if (!showText(text)) {
		return false;
	}

	char message[40];
	snprintf(message, sizeof(message), "DS1: %c  DS4/DS5: %c%c", text[0], text[3], text[4]);
	Serial.println(message);
	oled_.D(message);
	return true;
}

// Walk digit segments, AM, PM, and both colon LEDs, then show one blank step.
bool Tests::showWalk(uint32_t step) {
	uint8_t frame[DisplayMap::ChannelSpan] = {};
	uint8_t offset = 0;

	for (uint8_t index = 0; index < DisplayMap::DigitCount; ++index) {
		if (step >= offset && step < offset + Digits[index].segmentCount) {
			const uint8_t segment = step - offset;
			const uint8_t address = Digits[index].firstAddress + segment;
			frame[address] = TEST_BRIGHTNESS;
			char message[40];
			snprintf(message, sizeof(message), "DS%u segment %u 0x%02X", index + 1, segment + 1, address);
			Serial.println(message);
			oled_.D(message);
			return matrix_.writeBrightness(0, frame, sizeof(frame));
		}

		offset += Digits[index].segmentCount;
	}

	constexpr const char* names[] = {"AM", "PM", "Colon1", "Colon2"};

	for (uint8_t index = 0; index < sizeof(DisplayMap::Discretes); ++index) {
		if (step == offset + index) {
			const uint8_t address = DisplayMap::Discretes[index];
			frame[address] = TEST_BRIGHTNESS;
			char message[40];
			snprintf(message, sizeof(message), "%s 0x%02X", names[index], address);
			Serial.println(message);
			oled_.D(message);
			break;
		}
	}

	return matrix_.writeBrightness(0, frame, sizeof(frame));
}

// Light all 16 CS channels on THERMAL_ROW_SW and leave every other row dark.
bool Tests::showThermalRow() {
	uint8_t frame[DisplayMap::ChannelSpan] = {};
	const uint8_t firstAddress = 16 * (THERMAL_ROW_SW - 1);

	for (uint8_t cs = 0; cs < 16; ++cs) {
		frame[firstAddress + cs] = 255;
	}

	return matrix_.writeBrightness(0, frame, sizeof(frame));
}

// Advance the timed blank, all-on, single-row, and reduced-current clock phases.
void Tests::thermalTick() {
	const uint32_t now = millis();

	if (thermalPhase == ThermalPhase::Blank) {
		if (uint32_t(now - thermalStarted) < THERMAL_BLANK_MS) {
			return;
		}

		// Apply the selected test PWM ceiling to both thermal load phases.
		matrix_.setMaximumPWM(TEST_MAX_PWM);

		if (!matrix_.setGlobalCurrent(TEST_CURRENT) || !showAllSegments(255)) {
			matrixFailure();
			return;
		}

		thermalStarted = millis();
		thermalPhase = ThermalPhase::FullLoad;
		Serial.printf("THERMAL: 30s all digit segments, GCC/PWM=%02X/%02X\n",
			matrix_.globalCurrent(), matrix_.effectiveMaximumPWM());
		oled_.D("Thermal: load 30 sec");
		return;
	}

	if (thermalPhase == ThermalPhase::FullLoad) {
		if (uint32_t(now - thermalStarted) < THERMAL_LOAD_MS) {
			return;
		}

		if (!showThermalRow()) {
			matrixFailure();
			return;
		}

		thermalStarted = millis();
		thermalPhase = ThermalPhase::SingleRow;
		char message[40];
		snprintf(message, sizeof(message), "Thermal SW%u: 2s, GCC/PWM=%02X/%02X",
			THERMAL_ROW_SW, matrix_.globalCurrent(), matrix_.effectiveMaximumPWM());
		Serial.println(message);
		oled_.D(message);
		return;
	}

	if (thermalPhase == ThermalPhase::SingleRow) {
		if (uint32_t(now - thermalStarted) < THERMAL_ROW_MS) {
			return;
		}

		// Restore the application GCC before replacing the thermal load frame.
		if (!matrix_.setGlobalCurrent(matrix_.configuredCurrent())) {
			matrixFailure();
			return;
		}

		matrix_.setMaximumPWM(DisplayGamma::DefaultMaximumPWM);
		pseudoTime = ClockCalendar::DateTime();

		if (!showClock()) {
			matrixFailure();
			return;
		}

		lastTestTick = millis();
		thermalPhase = ThermalPhase::Clock;
		Serial.printf("Thermal complete: clock GCC=0x%02X; normal PWM limit restored\n", matrix_.globalCurrent());
		return;
	}

	const uint32_t elapsed = uint32_t(now - lastTestTick);

	if (elapsed < 1000) {
		return;
	}

	const uint32_t seconds = elapsed / 1000;
	lastTestTick += seconds * 1000;
	ClockCalendar::advanceSeconds(pseudoTime, seconds);

	if (!showClock()) {
		matrixFailure();
	}
}

// Service the selected diagnostic when due, catching up elapsed ticks without blocking.
void Tests::tick() {
	if (!testRunning) {
		return;
	}

	if (ACTIVE_TEST == DisplayTest::Thermal) {
		thermalTick();
		return;
	}

	if (ACTIVE_TEST == DisplayTest::StartupShow) {
		animationTick();
		return;
	}

	const uint32_t now = millis();
	const uint32_t elapsed = uint32_t(now - lastTestTick);
	const uint32_t interval = ACTIVE_TEST == DisplayTest::Fade ? FRAME_MS
		: (ACTIVE_TEST == DisplayTest::SegmentWalk ? WALK_MS : 1000);

	if (elapsed < interval) {
		return;
	}

	const uint32_t steps = elapsed / interval;
	lastTestTick += steps * interval;
	bool success = false;

	if (ACTIVE_TEST == DisplayTest::PseudoClock) {
		ClockCalendar::advanceSeconds(pseudoTime, steps);
		success = showClock();
	} else if (ACTIVE_TEST == DisplayTest::Characters) {
		testStep = (testStep + steps % 900) % 900;
		success = showCharacters(testStep);
	} else if (ACTIVE_TEST == DisplayTest::SegmentWalk) {
		testStep = (testStep + steps % WALK_STEPS) % WALK_STEPS;
		success = showWalk(testStep);
	} else if (ACTIVE_TEST == DisplayTest::Fade) {
		// Two seconds up, 0.5s hold, two seconds down, 0.5s off.
		testStep = (testStep + steps % 250) % 250;
		const uint32_t position = testStep * FRAME_MS;
		const uint8_t brightness = position < 2000 ? uint32_t(TEST_BRIGHTNESS) * position / 2000
			: (position < 2500 ? TEST_BRIGHTNESS
			: (position < 4500 ? uint32_t(TEST_BRIGHTNESS) * (4500 - position) / 2000 : 0));
		success = showAllSegments(brightness);
	}

	if (!success) {
		matrixFailure();
	}
}

// Reset test timing and render the first frame of the selected diagnostic.
bool Tests::startTest() {
	lastTestTick = millis();
	testStep = 0;
	testRunning = true;

	if (ACTIVE_TEST == DisplayTest::Thermal) {
		// U1 is already enabled (SDB HIGH, SSD=1); blank via zero PWM only.
		if (!showAllSegments(0)) {
			return false;
		}

		thermalPhase = ThermalPhase::Blank;
		thermalStarted = millis();
		Serial.println("THERMAL: 30s enabled, all PWM zero");
		oled_.D("Thermal: blank 30 sec");
		return true;
	}

	if (ACTIVE_TEST == DisplayTest::PseudoClock) {
		pseudoTime = ClockCalendar::DateTime();
		return showClock();
	}

	if (ACTIVE_TEST == DisplayTest::Characters) {
		return showCharacters(0);
	}

	if (ACTIVE_TEST == DisplayTest::SegmentWalk) {
		return showWalk(0);
	}

	if (!showAllSegments(0)) {
		return false;
	}

	if (ACTIVE_TEST == DisplayTest::StartupShow) {
		Serial.println("2-second all-segment ramp, then rolling alphabet/count");
		oled_.D("2s ramp / all segments");
		phaseStarted = lastTestTick;
		lastFrame = phaseStarted;
		previousBrightness = 0;
		animationPhase = AnimationPhase::Ramp;
	} else {
		Serial.println("Gamma fade test");
		oled_.D("Gamma fade test");
	}

	return true;
}

