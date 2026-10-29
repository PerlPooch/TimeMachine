#include "TimeController.h"
#include <MDS_Display.h>

bool TimeController::configure() {
	MDS_Time::Options options;
	options.timeZone = config_.get("timeZone").as<const char*>();
	options.server1 = config_.get("ntpServer1").as<const char*>();
	options.server2 = config_.get("ntpServer2").as<const char*>();
	options.intervalSeconds = config_.get("ntpInterval").as<uint32_t>();

	if (!time_.begin(options)) {
		if (reportedError_ != time_.lastError()) {
			reportedError_ = time_.lastError();
			Serial.println(reportedError_);
			if (oledReady_) {
				oled_.D(reportedError_);
			}
		}

		return false;
	}

	reportedError_ = "";

	return true;
}

bool TimeController::begin(bool oledReady) {
	oledReady_ = oledReady;
	return configure();
}

void TimeController::tick(bool networkReady) {
	configure();
	time_.tick(networkReady);
}
