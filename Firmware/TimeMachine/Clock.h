#pragma once

#include <MDS_Time.h>
#include "Display.h"

class MDS_Display;

// Publish time snapshots to the same command queue used by HTTP and serial.
class Clock {
public:
	Clock(MDS_Time& time, Display::CommandQueue& commands, MDS_Display& oled);
	bool begin();
	void tick();
private:
	bool publish(const struct tm* local);
	MDS_Time& time_;
	Display::CommandQueue& commands_;
	MDS_Display& oled_;
	uint32_t timeConfiguration_ = 0;
	time_t lastPublished_ = 0;
	time_t lastLoggedMinute_ = 0;
	bool enabled_ = false;
	bool published_ = false;
	bool publishedValid_ = false;
};
