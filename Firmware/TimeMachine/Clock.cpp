#include "Clock.h"
#include <MDS_Display.h>

Clock::Clock(MDS_Time& time, Display::CommandQueue& commands, MDS_Display& oled)
	: time_(time), commands_(commands), oled_(oled) {}

bool Clock::begin() {
	enabled_ = true;

	timeConfiguration_ = time_.configurationVersion();

	lastPublished_ = lastLoggedMinute_ = 0;
	published_ = publishedValid_ = false;

	if (publish(nullptr)) published_ = true;

	oled_.D("Clock: waiting for NTP");

	return true;
}

// Publish calendar and indicator state through the shared display queue.
bool Clock::publish(const struct tm* local) {
	Display::DisplayCommand command;

	command.kind = Display::DisplayCommand::Kind::Clock;
	command.timeValid = local != nullptr;

	if (local) {
		command.am = local->tm_hour < 12;
		command.pm = !command.am;
		command.colonsBlink = true;

		command.dateTime.year = local->tm_year + 1900;
		command.dateTime.month = local->tm_mon + 1;
		command.dateTime.day = local->tm_mday;
		command.dateTime.hour = local->tm_hour;
		command.dateTime.minute = local->tm_min;
		command.dateTime.second = local->tm_sec;
	}

	return commands_.post(command);
}

void Clock::tick() {
	if (!enabled_) return;
	
	if (timeConfiguration_ != time_.configurationVersion()) {
		timeConfiguration_ = time_.configurationVersion();
		published_ = false;
		lastLoggedMinute_ = 0;
	}

	if (!time_.valid()) {
		if ((!published_ || publishedValid_) && publish(nullptr)) {
			published_ = true;
			publishedValid_ = false;
			lastPublished_ = 0;
		}
		return;
	}

	const time_t now = time_.utcNow();

	if (published_ && publishedValid_ && now == lastPublished_) return;

	struct tm local;
	if (!time_.localTime(local) || !publish(&local)) return;

	published_ = publishedValid_ = true;
	lastPublished_ = now;

	if (now / 60 != lastLoggedMinute_) {
		char message[48];
		strftime(message, sizeof(message), "%b %d %Y %I:%M:%S %p %Z", &local);
		Serial.println(message);
		lastLoggedMinute_ = now / 60;
	}
}
