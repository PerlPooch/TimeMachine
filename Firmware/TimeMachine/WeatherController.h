#pragma once

#include <MDS_Weather.h>
#include <MDS_Config.h>

// Application-owned config adapter; weather does not read global settings itself.
class WeatherController {
public:
	WeatherController(MDS_Weather& weather, MDS_Config& config) : weather_(weather), config_(config) {}
	void tick(bool networkReady, bool clockReady);

private:
	MDS_Weather& weather_;
	MDS_Config& config_;
};
