#pragma once

#include <HardwareSerial.h>
#include "SerialProtocol.h"
#include <MDS_Config.h>
#include <MDS_WiFi.h>
#include <MDS_HTTP.h>
#include <MDS_Time.h>
#include <MDS_Weather.h>
#include "ApplicationApi.h"
#include "TimeController.h"
#include "WeatherController.h"
#include "Clock.h"
#include "Tests.h"
#include "Display.h"
#include "Button.h"

class MDS_Display;

// Own application services and modes; borrow hardware constructed by the sketch.
class Application {
public:
	enum class Mode {
		Tests,
		Clock
	};

	// Lifecycle is separate from the selected display mode and hardware capabilities.
	enum class State : uint8_t {
		Uninitialized,
		Starting,
		SegmentTest,
		Running,
		DisplayFault,
		ConfigFault,
		ResetHeld,
		Resetting,
		ResetFailed,
		Rebooting
	};

	Application(Mode mode, MatrixDriver& matrix, MDS_Display& oled, Button& button);
	Application(const Application&) = delete;
	Application& operator=(const Application&) = delete;

	// Hardware readiness comes from the harness; services can run without a display.
	// Success starts the asynchronous lamp test; the selected mode begins afterward.
	bool begin(const char* version, const char* boardRole, bool busReady, bool oledReady);
	void tick();

	// Report the current lifecycle state without advancing application work.
	State state() const { return state_; }

private:
	void transitionTo(State next);
	static const char* stateName(State state);
	bool beginConfig(const char* version);
	bool beginMode();
	void serviceHttp();
	void serviceSerial();
	void serviceIPAddresses(uint32_t now);
	bool receiveIPAddress(const char* address, size_t length, size_t index);
	void queueWiFiCredentials(uint32_t now);
	void receiveWiFiCredentials(const uint8_t* payload, size_t length, const char* portName);
	bool routeDisplayCommand(const Display::DisplayCommand& command);
	bool routeSerialPayload(uint8_t destinations, const char* payload, size_t length);
	void receiveSerial(HardwareSerial& port, SerialProtocol::Receiver& receiver, size_t index, const char* name);
	void serviceButton();
	void showReset();
	void factoryReset();

	const Mode mode_;
	MatrixDriver& matrix_;
	MDS_Display& oled_;
	Button& button_;
	static constexpr uint32_t ResetHoldMs = 5000;
	State state_ = State::Uninitialized;
	State resumeState_ = State::DisplayFault; // Destination after reset-button release.
	uint32_t resetPressedAt_ = 0;
	Display::Frame resetFrame_; // Output to restore if the reset is canceled.
	bool oledReady_ = false;

	// Port A / J2: RX GPIO0, TX GPIO1; USB CDC remains the debug console.
	HardwareSerial serialA_{1};
	// Port B / J3 uses UART0 on RX GPIO20 and TX GPIO18.
	HardwareSerial serialB_{0};
	struct SerialTransmit {
		uint8_t frame[SerialProtocol::BufferBytes] = {};
		size_t length = 0;
		size_t sent = 0;
		bool credentials = false;
		bool ipAddress = false;
	};
	SerialTransmit serialTransmit_[2];
	struct SerialResponse {
		SerialProtocol::Status status = SerialProtocol::Status::Ok;
		size_t sent = 0;
		bool pending = false;
	};
	SerialResponse serialResponse_[2];
	SerialProtocol::Receiver receiverA_;
	SerialProtocol::Receiver receiverB_;
	static constexpr uint32_t PingIntervalMs = 1000;
	uint32_t lastPingAt_[2] = {};
	static constexpr uint32_t WiFiBroadcastIntervalMs = 10000;
	uint32_t lastWiFiAt_[2] = {};
	bool primaryActive_ = false;
	static constexpr uint32_t IPAnnouncementIntervalMs = 10000;
	static constexpr uint32_t PeerIPExpiryMs = 30000;
	char localIPAddress_[16] = {};
	bool ipAnnouncementDue_[2] = {};
	uint32_t lastIPAt_[2] = {};
	uint32_t peerIPAt_[2] = {};
	bool peerIPKnown_[2] = {};

	Display display_;
	MDS_Config config_;
	MDS_WiFi wifi_;
	MDS_HTTP http_;
	MDS_Time time_;
	MDS_Weather weather_;
	ApplicationApi api_;
	TimeController timeController_;
	WeatherController weatherController_;
	Clock clock_;
	Tests tests_;
};
