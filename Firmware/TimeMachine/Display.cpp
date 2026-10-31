// A Frame holds logical brightness for each display channel, including digits and indicators.
// Display uses the map to locate each digit's channels and the appropriate font to select its segments.
// MatrixDriver writes those brightness values to hardware, applying gamma, PWM limits, and global current.

#include "Display.h"
#include <cstring>
#include <utility>
#include "fonts/SixteenSegmentAlphanumeric.h"
#include "fonts/SevenSegmentNumeric.h"

// Store the offset modulo 2^32 so timer rollover needs no special case.
void Display::synchronizeMillis(uint32_t primaryMillis) {
	setMillisDelta(uint32_t(primaryMillis - millis()));
}

void Display::setMillisDelta(uint32_t delta) {
	const uint32_t shift = delta - millisDelta_;
	millisDelta_ = delta;
	// Preserve elapsed durations across initial sync, corrections, and role changes.
	fadeStarted_ += shift;
	lampStarted_ += shift;
	lastFrameAt_ += shift;
	scrollLastAt_ += shift;
}

// Enable the mounted channels using the application-selected current.
bool Display::begin() {
	// Application initializes the hardware before the startup animation; modes reuse it.
	if (initialized_) {
		return matrix_.ready();
	}
	uint8_t channels[192];
	uint8_t count = 0;

	for (const auto& digit : DisplayMap::Digits) {
		for (uint8_t segment = 0; segment < digit.segmentCount; ++segment) {
			channels[count++] = digit.firstAddress + segment;
		}
	}

	for (uint8_t address : DisplayMap::Discretes) {
		channels[count++] = address;
	}

	initialized_ = matrix_.begin(channels, count);
	return initialized_;
}

// Commit an empty frame to blank digits and indicators.
bool Display::clear() {
	messageHeld_ = restoreMessage_ = false;
	colonsBlink_ = false;

	return show(Frame{});
}

// Map characters into logical segment brightness using each digit's font.
void Display::draw(Frame& frame, const char* text, uint8_t firstDigit, uint8_t count, uint8_t brightness) const {
	for (uint8_t index = firstDigit; index < firstDigit + count; ++index) {
		// Stop advancing at the terminator; short/null strings safely pad with blanks.
		const char character = text && *text ? *text++ : ' ';
		const auto& digit = DisplayMap::Digits[index];
		const uint16_t mask = digit.segmentCount == 16
			? SixteenSegment::Fonts::glyph(character) : SevenSegment::Fonts::glyph(character);

		for (uint8_t segment = 0; segment < digit.segmentCount; ++segment) {
			frame.channels[digit.firstAddress + segment] = mask & (uint16_t(1) << segment) ? brightness : 0;
		}
	}
}

// Replace all character positions in the frame, leaving indicators untouched.
void Display::drawText(Frame& frame, const char* text, uint8_t brightness) const {
	draw(frame, text, 0, DisplayMap::DigitCount, brightness);
}

// Set one discrete indicator in the frame without writing hardware.
void Display::setIndicator(Frame& frame, Indicator indicator, uint8_t brightness) const {
	frame.channels[static_cast<uint8_t>(indicator)] = brightness;
}

// Read the driver cache so snapshots include raw diagnostic output.
Display::Frame Display::frame() const {
	Frame result;

	for (uint8_t address = 0; address < sizeof(result.channels); ++address) {
		result.channels[address] = matrix_.brightness(address);
	}

	return result;
}

// Replace target content; lamp-test output is independent until its animation finishes.
bool Display::show(const Frame& frame) {
	// A temporary raw write (such as RST) may interrupt motion over a held message.
	restoreMessage_ = restoreMessage_ || (scrolling_ && messageHeld_);
	stopScroll();
	target_ = frame;
	return segmentTesting_ ? matrix_.ready() : flush();
}

// Keep raw diagnostic edits compatible at normal intensity; during fades edit the target.
Display::Frame Display::editableFrame() const {
	return segmentTesting_ || fading_ || level_ != 255 ? target_ : frame();
}

// Smoothstep in logical intensity; MatrixDriver still applies gamma exactly once.
uint8_t Display::easedLevel(uint32_t elapsed, uint32_t duration, uint8_t from, uint8_t to) {
	if (elapsed >= duration) {
		return to;
	}
	const float t = float(elapsed) / float(duration);
	const float eased = t * t * (3.0f - 2.0f * t);
	return uint8_t(float(from) + (int(to) - int(from)) * eased + 0.5f);
}

// Commit the logical frame, applying active overlays and global intensity.
bool Display::flush() {
	if (!matrix_.ready()) {
		return false;
	}

	if (colonsBlink_ && !segmentTesting_) drawColons();

	output_ = segmentTesting_ ? Frame{} : target_;
	if (segmentTesting_) {
		for (const auto& digit : DisplayMap::Digits) {
			for (uint8_t segment = 0; segment < digit.segmentCount; ++segment) {
				output_.channels[digit.firstAddress + segment] = 255;
			}
		}
		for (uint8_t address : DisplayMap::Discretes) {
			output_.channels[address] = 255;
		}
	}

	bool changed = false;
	for (size_t address = 0; address < sizeof(output_.channels); ++address) {
		output_.channels[address] = (uint16_t(output_.channels[address]) * level_ + 127) / 255;
		changed = changed || output_.channels[address] != matrix_.brightness(address);
	}

	// Avoid redundant I2C traffic when rounding or a hold leaves the frame unchanged.
	return !changed || matrix_.writeBrightness(0, output_.channels, sizeof(output_.channels));
}

bool Display::fadeTo(uint8_t level, uint32_t durationMs) {
	if (!matrix_.ready() || (segmentTesting_ && !cancelLampTest())) {
		return false;
	}
	if (!fading_ && level_ == 255) {
		target_ = frame();
	}
	fadeFrom_ = level_;
	fadeTo_ = level;
	fadeStarted_ = lastFrameAt_ = animationMillis();
	fadeDuration_ = durationMs;
	fading_ = durationMs && level_ != level;
	if (!fading_) {
		level_ = level;
		return flush();
	}
	return true;
}

bool Display::lampTest() {
	if (!matrix_.ready()) {
		return false;
	}
	if (!segmentTesting_ && !fading_ && level_ == 255) {
		target_ = frame(); // Include any initial output written by raw diagnostics.
	}
	fading_ = false;
	segmentTesting_ = true;
	lampStarted_ = lastFrameAt_ = animationMillis();
	level_ = 0;
	return flush();
}

bool Display::cancelLampTest() {
	if (!segmentTesting_) {
		return matrix_.ready();
	}
	segmentTesting_ = false;
	scrollLastAt_ = animationMillis(); // Resume scrolling after the lamp overlay.
	level_ = 255;
	return flush();
}

// Apply independent indicator choices using the common logical brightness.
void Display::drawIndicators(Frame& frame, const DisplayCommand& command) const {
	constexpr uint8_t brightness = 92;

	setIndicator(frame, Indicator::AM, command.am ? brightness : 0);
	setIndicator(frame, Indicator::PM, command.pm ? brightness : 0);
	setIndicator(frame, Indicator::Colon1, command.colonTop ? brightness : 0);
	setIndicator(frame, Indicator::Colon2, command.colonBottom ? brightness : 0);
}

// Only Display interprets local display commands and writes their frames.
bool Display::execute(const DisplayCommand& command) {
	if (command.kind == DisplayCommand::Kind::Clock) {
		clockEnabled_ = true;
		clockTime_ = command.dateTime;
		clockValid_ = command.timeValid && clockTime_.month >= 1 && clockTime_.month <= 12
			&& clockTime_.day >= 1 && clockTime_.day <= 31 && clockTime_.year <= 9999
			&& clockTime_.hour < 24 && clockTime_.minute < 60;

		// Background clock snapshots retain their own indicators during overrides.
		drawIndicators(clockIndicators_, command);
		clockColonsBlink_ = command.colonsBlink;

		return true;
	}

	colonsBlink_ = command.colonsBlink;
	bool shown = false;

	switch (command.kind) {
		case DisplayCommand::Kind::Message:
			shown = setMessage(command.text.c_str());
			break;

		case DisplayCommand::Kind::Scroll:
			shown = show(Frame{}) && scrollText(command.text.c_str(), command.speed, command.direction);
			break;

		case DisplayCommand::Kind::Slide:
			shown = show(Frame{}) && slideText(command.text.c_str(), command.speed, command.direction);
			break;

		case DisplayCommand::Kind::Appear:
			shown = show(Frame{}) && appearText(command.text.c_str(), command.speed, command.direction);
			break;

		case DisplayCommand::Kind::Clock:
			break;
	}

	if (!shown) return false;

	drawIndicators(target_, command);

	if (messageHeld_ && !scrolling_) {
		heldMessage_ = target_;
		heldColonsBlink_ = colonsBlink_;
	}

	return segmentTesting_ ? matrix_.ready() : flush();
}

// Compose calendar text and select the clock mode's indicator behavior.
void Display::drawClock() {
	Frame frame = clockValid_ ? clockIndicators_ : Frame{};
	char text[14] = "-------------";
	if (clockValid_) {
		const unsigned hour = clockTime_.hour % 12 ? clockTime_.hour % 12 : 12;
		snprintf(text, sizeof(text), "%s%02u%04u%02u%02u",
			ClockCalendar::Months[clockTime_.month - 1], unsigned(clockTime_.day),
			unsigned(clockTime_.year), hour, unsigned(clockTime_.minute));
	}
	colonsBlink_ = clockValid_ && clockColonsBlink_;

	drawText(frame, text);
	target_ = frame;
}

// Select colon blinking independently of character content or display mode.
bool Display::setColonsBlink(bool enabled) {
	colonsBlink_ = enabled;

	if (messageHeld_ && !scrolling_) heldColonsBlink_ = enabled;

	if (!enabled) {
		setIndicator(target_, Indicator::Colon1, 0);
		setIndicator(target_, Indicator::Colon2, 0);
	}

	return segmentTesting_ ? matrix_.ready() : flush();
}

// Apply the shared one-second on/off phase to both colon indicators.
bool Display::drawColons() {
	const uint8_t brightness = (animationMillis() / 1000u) % 2u == 0 ? 92 : 0;
	const bool changed = target_.channels[DisplayMap::Colon1] != brightness
		|| target_.channels[DisplayMap::Colon2] != brightness;

	setIndicator(target_, Indicator::Colon1, brightness);
	setIndicator(target_, Indicator::Colon2, brightness);

	return changed;
}

// Consume pending commands and advance active display effects.
bool Display::tick() {
	if (!matrix_.ready()) {
		fading_ = segmentTesting_ = false;
		stopScroll();
		return false;
	}
	DisplayCommand command;
	if (commands_.take(command) && !execute(command)) return false;
	const uint32_t now = animationMillis();
	bool scrollChanged = false;

	if (restoreMessage_ && !scrolling_) {
		target_ = heldMessage_;
		colonsBlink_ = heldColonsBlink_;
		restoreMessage_ = false;
		scrollChanged = true;
	}

	if (scrolling_ && !segmentTesting_) {
		const uint32_t steps = uint32_t(now - scrollLastAt_) / scrollDelayMs_;
		if (steps) {
			const size_t end = appearing_ ? 5 : sliding_ ? DisplayMap::DigitCount : scrollText_.length() + DisplayMap::DigitCount;
			const size_t remaining = end - scrollStep_;
			scrollStep_ += steps >= remaining ? remaining : steps;
			scrollLastAt_ += steps * scrollDelayMs_;
			drawScroll();
			scrollChanged = true;
			if (scrollStep_ == end) {
				if (sliding_) {
					// The final aligned frame is the held message; no second renderer is needed.
					heldMessage_ = target_;
					messageHeld_ = true;
					heldColonsBlink_ = colonsBlink_;
				} else if (messageHeld_) {
					target_ = heldMessage_;
					colonsBlink_ = heldColonsBlink_;
				}

				stopScroll();
			}
		}
	}
	if (clockEnabled_ && !hasOverride() && !segmentTesting_) {
		const Frame previous = target_;
		drawClock();
		scrollChanged = scrollChanged || std::memcmp(previous.channels, target_.channels, sizeof(target_.channels)) != 0;
	}

	if (colonsBlink_ && !segmentTesting_) {
		scrollChanged = drawColons() || scrollChanged;
	}

	if (segmentTesting_) {
		const uint32_t elapsed = now - lampStarted_;
		if (elapsed >= LampDurationMs) {
			return cancelLampTest();
		}
		const bool boundary = elapsed == LampFadeMs || elapsed == LampFadeMs + LampHoldMs;
		if (uint32_t(now - lastFrameAt_) < 16 && !boundary) {
			return true;
		}
		if (elapsed < LampFadeMs) {
			level_ = easedLevel(elapsed, LampFadeMs, 0, 255);
		} else if (elapsed < LampFadeMs + LampHoldMs) {
			level_ = 255;
		} else {
			level_ = easedLevel(elapsed - LampFadeMs - LampHoldMs, LampFadeMs, 255, 0);
		}
	} else if (fading_) {
		const uint32_t elapsed = now - fadeStarted_;
		if (!scrollChanged && uint32_t(now - lastFrameAt_) < 16 && elapsed < fadeDuration_) {
			return true;
		}
		level_ = easedLevel(elapsed, fadeDuration_, fadeFrom_, fadeTo_);
		if (elapsed >= fadeDuration_) {
			fading_ = false;
		}
	} else if (!scrollChanged) {
		return true;
	}
	lastFrameAt_ = now;
	if (!flush()) {
		fading_ = segmentTesting_ = false;
		stopScroll();
		return false;
	}
	return true;
}

// Release stored text when a scroll ends or a normal content write replaces it.
void Display::stopScroll() {
	scrolling_ = false;
	sliding_ = false;
	appearing_ = false;
	scrollText_ = static_cast<const char*>(nullptr);
}

// Compose a character window; rendering again selects the font at each destination.
void Display::drawScroll() {
	if (appearing_) {
		drawAppear();
		return;
	}
	char window[DisplayMap::DigitCount + 1];
	const size_t length = scrollText_.length();
	
	for (size_t digit = 0; digit < DisplayMap::DigitCount; ++digit) {
		const size_t offset = scrollDirection_ == Direction::Left
			? scrollStep_ + digit : length + digit;
		const size_t origin = scrollDirection_ == Direction::Left
			? DisplayMap::DigitCount : scrollStep_;
		window[digit] = offset >= origin && offset - origin < length
			? scrollText_.c_str()[offset - origin] : ' ';
	}
	window[DisplayMap::DigitCount] = '\0';
	drawText(target_, window);
}

// Start a single pass immediately; the application continues servicing tick().
bool Display::scrollText(const char* text, uint32_t delayMs, Direction direction) {
	return startTextMotion(text, delayMs, direction, false);
}

// Use the same character motion, stopping when it reaches the far display edge.
bool Display::slideText(const char* text, uint32_t delayMs, Direction direction) {
	char visibleText[DisplayMap::DigitCount + 1] = {};
	size_t length = 0;

	while (text && text[length] && length < DisplayMap::DigitCount) {
		visibleText[length] = text[length];
		++length;
	}

	return startTextMotion(visibleText, delayMs, direction, true);
}

// Reveal text in place rather than moving it between digits.
bool Display::appearText(const char* text, uint32_t delayMs, Direction direction) {
	char visibleText[DisplayMap::DigitCount + 1] = {};
	for (size_t index = 0; text && text[index] && index < DisplayMap::DigitCount; ++index) {
		visibleText[index] = text[index];
	}
	return startTextMotion(visibleText, delayMs, direction, true, true);
}

// Group boundaries are fixed to the physical month/day/year/hour/minute fields.
void Display::drawAppear() {
	static constexpr uint8_t boundaries[] = {0, 3, 5, 9, 11, 13};
	const size_t first = scrollDirection_ == Direction::Left ? 0 : boundaries[5 - scrollStep_];
	const size_t end = scrollDirection_ == Direction::Left ? boundaries[scrollStep_] : DisplayMap::DigitCount;
	char window[DisplayMap::DigitCount + 1];
	for (size_t digit = 0; digit < DisplayMap::DigitCount; ++digit) {
		window[digit] = digit >= first && digit < end && digit < scrollText_.length()
			? scrollText_.c_str()[digit] : ' ';
	}
	window[DisplayMap::DigitCount] = '\0';
	drawText(target_, window);
}

bool Display::startTextMotion(const char* text, uint32_t delayMs, Direction direction, bool slide, bool appear) {
	if (!matrix_.ready()) {
		return false;
	}
	if (!text || !*text) {
		stopScroll();
		restoreMessage_ = false;

		if (slide) {
			messageHeld_ = false;
		}

		if (messageHeld_) {
			target_ = heldMessage_;
			return segmentTesting_ || flush();
		}

		return showText("");
	}
	String tempString(text);
	if (tempString.length() != std::strlen(text)) {
		return false;
	}
	target_ = editableFrame();
	scrollText_ = std::move(tempString);
	scrollDelayMs_ = delayMs ? delayMs : 1;
	scrollDirection_ = direction;
	scrollStep_ = 1;
	scrollLastAt_ = animationMillis();
	scrolling_ = true;
	sliding_ = slide;
	appearing_ = appear;
	restoreMessage_ = false;
	drawScroll();
	if (segmentTesting_ || flush()) {
		return true;
	}
	stopScroll();
	return false;
}

// Own the static override as a frame, independent of caller text lifetime or Clock.
bool Display::setMessage(const char* text) {
	Frame message;
	drawText(message, text);

	stopScroll();
	restoreMessage_ = false;
	messageHeld_ = text && *text;
	heldMessage_ = message;
	heldColonsBlink_ = colonsBlink_;
	target_ = message;

	return segmentTesting_ ? matrix_.ready() : flush();
}

// Replace all 13 characters while retaining the current indicators.
bool Display::showText(const char* text, uint8_t brightness) {
	Frame output = editableFrame();
	drawText(output, text, brightness);
	return show(output);
}

// Replace the three alpha characters while retaining numbers and indicators.
bool Display::showAlpha(const char* text, uint8_t brightness) {
	Frame output = editableFrame();
	draw(output, text, 0, 3, brightness);
	return show(output);
}
