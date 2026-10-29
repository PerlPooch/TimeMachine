#pragma once

enum class DisplayTest {
	StartupShow,  // 2-second ramp, all-on hold, rolling alphabet/count
	Fade,         // Repeating gamma fade on all mounted digits
	Characters,   // DS1 A-Z/0-9 with DS4/DS5 counting 00-99
	SegmentWalk,  // Walk all 122 populated channels, then blank
	Thermal,      // 30s enabled/blank, 60s all-on, 60s single row, then clock
	PseudoClock   // OCT 24 2026 10:00:00, one second per real second
};

#ifndef DISPLAY_TEST_MODE
#define DISPLAY_TEST_MODE DisplayTest::StartupShow
#endif

constexpr DisplayTest ACTIVE_TEST = DISPLAY_TEST_MODE;
