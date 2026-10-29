#pragma once

#include <stdint.h>

namespace ClockCalendar {
	struct DateTime {
		uint16_t year = 2026;
		uint8_t month = 10;
		uint8_t day = 24;
		uint8_t hour = 10;
		uint8_t minute = 0;
		uint8_t second = 0;
	};

	constexpr char Months[12][4] = {
		"JAN", "FEB", "MAR", "APR", "MAY", "JUN",
		"JUL", "AUG", "SEP", "OCT", "NOV", "DEC"
	};

	inline uint8_t daysInMonth(uint16_t year, uint8_t month) {
		constexpr uint8_t days[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
		const bool leap = year % 4 == 0 && (year % 100 != 0 || year % 400 == 0);
		return days[month - 1] + (month == 2 && leap ? 1 : 0);
	}

	inline void advanceSeconds(DateTime& time, uint32_t seconds) {
		const uint64_t total = uint64_t(time.hour) * 3600
			+ uint32_t(time.minute) * 60 + time.second + uint64_t(seconds);
		uint32_t days = total / 86400;
		const uint32_t withinDay = total % 86400;
		time.hour = withinDay / 3600;
		time.minute = (withinDay / 60) % 60;
		time.second = withinDay % 60;

		while (days > 0) {
			const uint8_t remaining = daysInMonth(time.year, time.month) - time.day;

			if (days <= remaining) {
				time.day += days;
				break;
			}

			days -= remaining + 1;
			time.day = 1;

			if (++time.month > 12) {
				time.month = 1;
				++time.year;
			}
		}
	}
}
