#pragma once

#include <MDS_Time.h>
#include <MDS_Config.h>

class MDS_Display;

// Apply application time settings and service NTP independently of display modes.
class TimeController {
public:
	TimeController(MDS_Time& time, MDS_Config& config, MDS_Display& oled)
		: time_(time), config_(config), oled_(oled) {}

	bool begin(bool oledReady);
	void tick(bool networkReady);

private:
	bool configure();

	MDS_Time& time_;
	MDS_Config& config_;
	MDS_Display& oled_;
	String reportedError_;
	bool oledReady_ = false;
};
