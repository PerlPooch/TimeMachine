#pragma once

#include <MDS_Weather.h>
#include <MDS_Config.h>

// Application-owned config adapter; weather does not read global settings itself.
class WeatherController {
public:
	// Borrow settings and the weather service; Application installs the status callback.
	WeatherController(MDS_Weather& weather, MDS_Config& config) : weather_(weather), config_(config) {}

	// Apply current settings and service weather retrieval without blocking the display.
	void tick(bool networkReady, bool clockReady);

	// Called after a successful weather update to replace the configured timezone.
	void applyTimezone();

private:
	MDS_Weather& weather_;
	MDS_Config& config_;
};
