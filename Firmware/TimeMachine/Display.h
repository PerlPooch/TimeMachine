#pragma once

#include "MatrixDriver.h"
#include "DisplayMap.h"
#include "ClockCalendar.h"
#include <utility>

// Physical digit layout and font selection above the channel-oriented driver.
class Display {
public:
	struct Frame {
		uint8_t channels[DisplayMap::ChannelSpan] = {};
	};

	enum class Direction : uint8_t { Left, Right };

	enum Destination : uint8_t { Local = 1, PortA = 2, PortB = 4 };

	struct DisplayCommand {
		enum class Kind { Message, Scroll, Slide, Appear, Clock, Fade };
		Kind kind = Kind::Message;
		String text;
		uint8_t destinations = Local;
		uint32_t speed = 150;
		uint8_t level = 255; // Fade target; speed is total duration in milliseconds.
		Direction direction = Direction::Left;
		ClockCalendar::DateTime dateTime;
		bool timeValid = false;
		bool am = false;
		bool pm = false;
		bool colonTop = false;
		bool colonBottom = false;
		bool colonsBlink = false;
	};

	// Existing latest-pending-command mailbox, shared by all producers.
	// Background clock updates never replace a waiting explicit command.
	class CommandQueue {
	public:
		bool post(const DisplayCommand& command) {
			DisplayCommand copy = command;
			if (copy.text.length() != command.text.length()) return false;
			return post(std::move(copy));
		}
		bool post(DisplayCommand&& command) {
			if (command.kind == DisplayCommand::Kind::Clock && pending_
				&& command_.kind != DisplayCommand::Kind::Clock) return false;
			command_ = std::move(command);
			pending_ = true;
			return true;
		}
		bool take(DisplayCommand& command) {
			if (!pending_) return false;
			command = std::move(command_);
			pending_ = false;
			return true;
		}
	private:
		DisplayCommand command_;
		bool pending_ = false;
	};
	CommandQueue& commands() { return commands_; }

	enum class Indicator : uint8_t {
		AM = DisplayMap::AM,
		PM = DisplayMap::PM,
		Colon1 = DisplayMap::Colon1,
		Colon2 = DisplayMap::Colon2
	};

	explicit Display(MatrixDriver& matrix) : matrix_(matrix) {}

	// Shared animation clock; native millis() remains unchanged for other services.
	uint32_t animationMillis() const { return uint32_t(millis() + millisDelta_); }
	void synchronizeMillis(uint32_t primaryMillis);
	void resetMillisDelta() { setMillisDelta(0); }

	// Enable all mounted digit segments and the four discrete indicators.
	bool begin();

	// Blank every mapped digit and discrete indicator.
	bool clear();

	// Enable synchronized colon blinking in any mode; disabling blanks both colons.
	bool setColonsBlink(bool enabled);

	// Hold a static message with blank indicators unless colon blinking is enabled.
	// Empty text releases the override.
	// Scrolls temporarily replace it; settled slides become the new held message.
	bool setMessage(const char* text);
	bool hasOverride() const { return messageHeld_ || scrolling_; }

	// Replace 13 characters, padding/truncating text; preserve indicators.
	bool showText(const char* text, uint8_t brightness = 255);

	// Scroll once across all digits, entering from the opposite edge and exiting fully.
	// delayMs is per character step (0 is treated as 1 ms); tick() advances it.
	// Copy the text, preserve indicators, and use each destination digit's font.
	bool scrollText(const char* text, uint32_t delayMs, Direction direction = Direction::Left);
	bool scrolling() const { return scrolling_ && !sliding_; }

	// Slide up to 13 characters in, stop at the far edge, and retain the final frame.
	// Left motion settles left-aligned; right motion settles right-aligned.
	bool slideText(const char* text, uint32_t delayMs, Direction direction = Direction::Left);
	bool sliding() const { return scrolling_ && sliding_ && !appearing_; }

	// Reveal fixed display groups (3, 2, 4, 2, 2) from the specified edge.
	// Left-align and pad/truncate to 13 characters; show the first group immediately.
	// tick() reveals subsequent groups at delayMs intervals (0 means 1 ms).
	// Preserve indicators and hold the completed text, like slideText().
	bool appearText(const char* text, uint32_t delayMs, Direction direction = Direction::Left);
	bool appearing() const { return scrolling_ && appearing_; }

	// Replace only the three alpha characters; preserve numbers and indicators.
	bool showAlpha(const char* text, uint8_t brightness = 255);

	// Compose text and indicators into one logical frame, then commit it once.
	void drawText(Frame& frame, const char* text, uint8_t brightness = 255) const;
	void setIndicator(Frame& frame, Indicator indicator, uint8_t brightness) const;
	bool show(const Frame& frame);

	// Animate a global logical intensity without changing individual target intensities.
	// Call tick() from the application loop. Zero duration applies immediately.
	bool fadeTo(uint8_t level, uint32_t durationMs);
	bool fadeUp(uint32_t durationMs = 350) { return fadeTo(255, durationMs); }
	bool fadeDown(uint32_t durationMs = 350) { return fadeTo(0, durationMs); }
	bool fading() const { return fading_; }

	// Fade every fitted segment/indicator up, hold one second, then fade down.
	// Retain target edits made during the test and restore them on completion/cancel.
	static constexpr uint32_t LampFadeMs = 750;
	static constexpr uint32_t LampHoldMs = 2000;
	static constexpr uint32_t LampDurationMs = 2 * LampFadeMs + LampHoldMs;
	bool lampTest();
	bool cancelLampTest();
	bool segmentTesting() const { return segmentTesting_; }
	bool tick();

	// Full-intensity content, independent of the animated output level.
	Frame targetFrame() const { return target_; }

	// Snapshot current logical output, including changes made by raw diagnostics.
	Frame frame() const;

private:
	bool execute(const DisplayCommand& command);
	void drawClock();
	void drawIndicators(Frame& frame, const DisplayCommand& command) const;
	bool drawColons();
	void setMillisDelta(uint32_t delta);
	void draw(Frame& frame, const char* text, uint8_t firstDigit, uint8_t count, uint8_t brightness) const;

	bool startTextMotion(const char* text, uint32_t delayMs, Direction direction, bool slide, bool appear = false);
	void drawScroll();
	void drawAppear();
	void stopScroll();
	bool flush();
	Frame editableFrame() const;
	static uint8_t easedLevel(uint32_t elapsed, uint32_t duration, uint8_t from, uint8_t to);

	CommandQueue commands_;
	ClockCalendar::DateTime clockTime_;
	bool clockEnabled_ = false;
	bool clockValid_ = false;
	Frame clockIndicators_;
	bool clockColonsBlink_ = false;
	bool colonsBlink_ = false;
	uint32_t millisDelta_ = 0;
	MatrixDriver& matrix_;
	Frame target_;
	Frame output_;
	Frame heldMessage_;
	bool heldColonsBlink_ = false;
	bool messageHeld_ = false;
	bool restoreMessage_ = false;
	String scrollText_;
	size_t scrollStep_ = 0;
	uint32_t scrollDelayMs_ = 1;
	uint32_t scrollLastAt_ = 0;
	Direction scrollDirection_ = Direction::Left;
	bool scrolling_ = false;
	bool sliding_ = false;
	bool appearing_ = false;
	bool initialized_ = false;
	bool fading_ = false;
	bool segmentTesting_ = false;
	uint8_t level_ = 255;
	uint8_t fadeFrom_ = 255;
	uint8_t fadeTo_ = 255;
	uint32_t fadeStarted_ = 0;
	uint32_t fadeDuration_ = 0;
	uint32_t lampStarted_ = 0;
	uint32_t lastFrameAt_ = 0;
};
