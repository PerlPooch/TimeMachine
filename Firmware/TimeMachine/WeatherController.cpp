#include "WeatherController.h"

void WeatherController::tick(bool networkReady, bool clockReady) {
	MDS_Weather::Options options;
	options.apiKey = config_.get("owAppKey").as<const char*>();
	options.latitude = config_.get("latitude").as<const char*>();
	options.longitude = config_.get("longitude").as<const char*>();
	options.intervalSeconds = config_.get("weatherInterval").as<uint32_t>();
	weather_.begin(options);
	weather_.tick(networkReady, clockReady);
}
