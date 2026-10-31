#include "WeatherController.h"
#include <stdio.h>

// Replace runtime timeZone from a successful weather result; normal time servicing applies it.
void WeatherController::applyTimezone() {
	const int32_t offset = weather_.sample().timezoneOffsetSeconds;
	const uint32_t seconds = offset < 0 ? uint32_t(-offset) : uint32_t(offset);

	char zone[20];

	// POSIX timezone signs are the reverse of UTC offsets from OpenWeather.
	snprintf(zone, sizeof(zone), "UTC%c%02lu:%02lu:%02lu", offset < 0 ? '+' : '-',
		static_cast<unsigned long>(seconds / 3600), static_cast<unsigned long>((seconds / 60) % 60),
		static_cast<unsigned long>(seconds % 60));

	JsonDocument update;
	update["timeZone"] = zone;

	if (!config_.set(update)) {
		Serial.println(config_.lastError());
	}
}

// Apply current weather settings and service retrieval when network and time are ready.
void WeatherController::tick(bool networkReady, bool clockReady) {
	MDS_Weather::Options options;
	options.apiKey = config_.get("owAppKey").as<const char*>();
	options.latitude = config_.get("latitude").as<const char*>();
	options.longitude = config_.get("longitude").as<const char*>();
	options.intervalSeconds = config_.get("weatherInterval").as<uint32_t>();

	weather_.begin(options);
	weather_.tick(networkReady, clockReady);
}
