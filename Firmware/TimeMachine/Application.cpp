#include "Application.h"
#include <MDS_Display.h>
#include "SystemConfig.h"
#include <LittleFS.h>
#include <WiFi.h>
#include <esp_wifi.h>
#include "SerialProtocol.h"

// Wire owned services to the hardware and command queue supplied at construction.
Application::Application(Mode mode, MatrixDriver& matrix, MDS_Display& oled, Button& button):
	mode_(mode),
	matrix_(matrix),
	oled_(oled),
	button_(button),
	display_(matrix),
	api_(http_, config_, matrix_, display_.commands(), &weather_),
	timeController_(time_, config_, oled_), weatherController_(weather_, config_),
	clock_(time_, display_.commands(), oled_), tests_(matrix_, display_, oled_) {}

// Provide readable lifecycle names for transition reports.
const char* Application::stateName(State state) {
	switch (state) {
		case State::Uninitialized: return "Uninitialized";
		case State::Starting: return "Starting";
		case State::SegmentTest: return "SegmentTest";
		case State::Running: return "Running";
		case State::DisplayFault: return "DisplayFault";
		case State::ConfigFault: return "ConfigFault";
		case State::ResetHeld: return "ResetHeld";
		case State::Resetting: return "Resetting";
		case State::ResetFailed: return "ResetFailed";
		case State::Rebooting: return "Rebooting";
	}
	return "Unknown";
}

// All lifecycle changes and their one-time entry actions pass through this method.
void Application::transitionTo(State next) {
	if (state_ == next) {
		return;
	}
	// Entry actions may transition again if initialization fails.
	const State previous = state_;
	state_ = next;
	Serial.printf("Application: %s -> %s\n", stateName(previous), stateName(next));

	switch (next) {
		case State::SegmentTest:
			if (!display_.begin() || !display_.lampTest()) {
				Serial.println("Startup: Matrix initialization failed");
				transitionTo(State::DisplayFault);
			} else {
				Serial.println("Startup: Segment test");
			}
			break;

		case State::Running:
			// A reset release resumes the already initialized mode and its timers.
			if (previous == State::SegmentTest && !beginMode()) {
				transitionTo(State::DisplayFault);
			}
			break;

		case State::DisplayFault:
			Serial.println("Display: unavailable; application services remain active");
			if (oledReady_) {
				oled_.D("Display unavailable");
			}
			break;

		case State::ResetHeld:
			resumeState_ = previous;
			// A physical reset press takes priority over the startup animation.
			if (previous == State::SegmentTest) {
				// Resolve where a short press will return before showing RST.
				resumeState_ = display_.cancelLampTest() && beginMode()
					? State::Running : State::DisplayFault;
			}
			resetPressedAt_ = millis();
			showReset();
			Serial.println("Reset: Hold for 5 seconds to clear all settings");
			break;

		case State::Resetting:
			factoryReset();
			break;

		case State::Rebooting:
			Serial.println("Rebooting.");
			// Leave the reset confirmation visible; network reboots are immediate.
			if (previous == State::Resetting) {
				delay(2000);
			}
			ESP.restart();
			break;

		case State::Uninitialized:
		case State::Starting:
		case State::ConfigFault:
		case State::ResetFailed:
			break;
	}
}

// Load settings and select display limits before matrix initialization.
bool Application::beginConfig() {
	if (!config_.setSettings(SystemConfig::Settings)) {
		Serial.println(config_.lastError());
		return false;
	}

	// Recover an uninitialized or damaged config filesystem; Wi-Fi NVS is separate.
	bool configReady = config_.setup();

	if (!configReady) {
		Serial.println("Config: mount failed; formatting config filesystem");
		configReady = config_.setup(true);
		Serial.println(configReady ? "Config: filesystem recovered; using defaults" : config_.lastError());
	}

	// A load failure leaves the application using its in-memory settings.
	if (configReady && config_.exists() && !config_.load()) {
		Serial.println(config_.lastError());
	}

	// Select persisted current before Display initializes the matrix.
	if (!matrix_.setConfiguredCurrent(config_.get("gcc").as<uint8_t>())) {
		return false;
	}
	matrix_.setBrightnessLimit(config_.get("pwm").as<uint8_t>());

	return true;
}

// Start shared services and the lamp animation using reported hardware readiness.
bool Application::begin(const char* version, const char* boardRole, bool busReady, bool oledReady) {
	// Guard against calls to begin() except from Uninitialized or ConfigFault
	if (state_ != State::Uninitialized && state_ != State::ConfigFault) {
		// A paused running mode still counts as initialized.
		return state_ == State::SegmentTest || state_ == State::Running
			|| ((state_ == State::ResetHeld || state_ == State::ResetFailed)
				&& resumeState_ == State::Running);
	}

	oledReady_ = oledReady;
	transitionTo(State::Starting);

	if (!beginConfig()) {
		transitionTo(State::ConfigFault);
		return false;
	}

	serialA_.begin(115200, SERIAL_8N1, 0, 1);
	serialB_.begin(115200, SERIAL_8N1, 20, 18);
	config_.dump();
	Serial.println(mode_ == Mode::Clock ? "Application: Clock" : "Application: Tests");

	if (mode_ == Mode::Tests) {
		tests_.printConfig();
	}

	if (oledReady_) {
		oled_.D(config_.get("name").as<const char*>());
		oled_.D(String("V ") + version + " / Rev A");
		oled_.D(boardRole);
		oled_.D("Starting WiFi...");
	} else {
		Serial.println("OLED unavailable");
	}

	if (!busReady) {
		Serial.println("I2C initialization failed; matrix disabled");
	}

	wifi_.setStatusCallback([this](const String& message) {
		if (oledReady_) {
			oled_.D(message);
		}
	});

	MDS_WiFi::Options wifiOptions;
	wifiOptions.accessPointPrefix = "Time Machine";
	wifiOptions.portalTitle = "Time Machine Configuration";
	// Keep A/B reception active during both saved-network retries and provisioning.
	wifiOptions.connectionPolicy = MDS_WiFi::ConnectionPolicy::RetrySavedNetworkAsync;
	Serial.println("Startup: beginning WiFi");
	const bool wifiStarted = wifi_.begin(wifiOptions);
	Serial.println(wifi_.ready() ? "Startup: WiFi online" :
		(wifiStarted ? "Startup: WiFi waiting for station/IP" : "Startup: WiFi retrying in loop"));

	api_.setDisplayRouter([this](const Display::DisplayCommand& command) { return routeDisplayCommand(command); });
	api_.begin();
	serviceHttp();

	time_.setStatusCallback([this](const String& message) {
		Serial.println(message);
		if (oledReady_) {
			oled_.D(message);
			// Repeat the current IP after NTP's startup messages.
			if (message == "Time: synchronized" && wifi_.ready()) {
				oled_.D(WiFi.localIP().toString());
			}
		}
	});
	timeController_.begin(oledReady_);

	weather_.setStatusCallback([this](const String& message) {
		Serial.println(message);

		// Apply timezone only after the weather service accepts a successful result.
		if (message == "Weather: updated") {
			weatherController_.applyTimezone();
		}
	});

	weatherController_.tick(wifi_.ready(), time_.valid());

	// A missing display still permits network and configuration services.
	transitionTo(busReady ? State::SegmentTest : State::DisplayFault);
	Serial.println("Startup: Setup complete");
	return state_ == State::SegmentTest;
}

// Start the selected display behavior after the lamp animation completes.
bool Application::beginMode() {
	switch (mode_) {
		case Mode::Tests:
			Serial.println("Startup: Beginning display test");
			return tests_.begin();
		case Mode::Clock:
			Serial.println("Startup: Clock");
			return clock_.begin();
	}
	return false;
}

// Start or stop the HTTP listener as station connectivity changes.
void Application::serviceHttp() {
	if (wifi_.ready()) {
		if (!http_.running()) {
			http_.begin();
			Serial.println("HTTP: listening on port 80");
		}
	} else if (http_.running()) {
		http_.stop();
	}
}

// Dispatch sensitive frames before logging other length-delimited payloads.
void Application::receiveSerial(HardwareSerial& port, SerialProtocol::Receiver& receiver, const char* name) {
	using Result = SerialProtocol::Receiver::Result;
	auto report = [&](Result result) {
		if (result == Result::None) {
			return;
		}

		if (result == Result::Message && SerialProtocol::isWiFiCredentials(receiver.payload(), receiver.length())) {
			receiveWiFiCredentials(receiver.payload(), receiver.length(), name);
			return;
		}

		uint32_t primaryMillis = 0;
		const bool timestampedPing = result == Result::Message
			&& SerialProtocol::decodePing(receiver.payload(), receiver.length(), primaryMillis);
		// Capture the offset before USB diagnostic output can delay this service pass.
		if (timestampedPing && !config_.get("primary").as<bool>()) {
			display_.synchronizeMillis(primaryMillis);
		}
		Serial.print("Serial ");
		Serial.print(name);
		if (result == Result::Message) {
			Serial.print(" RX: ");
			Serial.write(receiver.payload(), receiver.length());
			Serial.println();
			if (!timestampedPing && receiver.length() >= 5 && memcmp(receiver.payload(), "ping ", 5) == 0) {
				Serial.print("Serial "); Serial.print(name); Serial.println(" error: invalid ping timestamp");
			}
			if (receiver.length() && receiver.payload()[0] == '['
				&& !api_.receiveDisplayCommand(receiver.payload(), receiver.length())) {
				Serial.print("Serial "); Serial.print(name); Serial.println(" error: invalid display command");
			}
		} else {
			Serial.print(" error: ");
			Serial.println(result == Result::InvalidLength ? "invalid length" :
				result == Result::InvalidTerminator ? "expected newline terminator" : "frame timeout");
		}
	};
	report(receiver.expire(millis()));
	// Bound work per port so continuous traffic cannot monopolize the main loop.
	for (size_t count = 0; count < SerialProtocol::MaxFrameBytes && port.available(); ++count) {
		const int byte = port.read();
		if (byte < 0) {
			break;
		}
		report(receiver.feed(static_cast<uint8_t>(byte), millis()));
	}
}

// A non-master adopts credentials from either physical link, even while offline.
void Application::receiveWiFiCredentials(const uint8_t* payload, size_t length, const char* portName) {
	Serial.print("Serial ");
	Serial.print(portName);

	if (config_.get("primary").as<bool>()) {
		Serial.println(" WiFi credentials ignored by primary");
		return;
	}

	SerialProtocol::WiFiCredentials credentials;
	if (!SerialProtocol::decodeWiFiCredentials(payload, length, credentials)) {
		Serial.println(" error: invalid WiFi credentials");
		return;
	}

	const bool accepted = wifi_.setCredentials(credentials.ssid, credentials.password);
	Serial.println(accepted ? " WiFi credentials accepted" : " error: WiFi credentials rejected");
}

// Read saved station settings independently of connectivity and queue each idle port.
void Application::queueWiFiCredentials(uint32_t now) {
	bool due[2] = {};
	for (size_t index = 0; index < 2; ++index) {
		due[index] = !serialTransmit_[index].length
			&& uint32_t(now - lastWiFiAt_[index]) >= WiFiBroadcastIntervalMs;
	}
	if (!due[0] && !due[1]) {
		return;
	}

	wifi_config_t saved = {};
	SerialProtocol::WiFiCredentials credentials;
	const bool readable = esp_wifi_get_config(WIFI_IF_STA, &saved) == ESP_OK;
	if (readable) {
		memcpy(credentials.ssid, saved.sta.ssid, 32);
		memcpy(credentials.password, saved.sta.password, 64);
	}

	for (size_t index = 0; index < 2; ++index) {
		if (!due[index]) {
			continue;
		}

		lastWiFiAt_[index] = now;
		if (!readable || !credentials.ssid[0]) {
			continue; // Try again in ten seconds, including after a manual portal save.
		}

		auto& pending = serialTransmit_[index];
		pending.length = SerialProtocol::encodeWiFiCredentials(credentials, pending.frame, sizeof(pending.frame));
		pending.sent = 0;
		pending.credentials = pending.length != 0;
	}
}

// Reserve every selected queue before changing any queue or applying locally.
bool Application::routeDisplayCommand(const Display::DisplayCommand& command) {
	for (size_t index = 0; index < 2; ++index) {
		if ((command.destinations & (index == 0 ? Display::PortA : Display::PortB))
			&& serialTransmit_[index].length) return false;
	}
	char payload[SerialProtocol::MaxPayloadBytes + 1];
	const size_t length = ApplicationApi::encodeDisplayCommand(command, payload, sizeof(payload));
	if (!length) return false;
	for (size_t index = 0; index < 2; ++index) {
		if (command.destinations & (index == 0 ? Display::PortA : Display::PortB)) {
			auto& pending = serialTransmit_[index];
			pending.length = SerialProtocol::encode(payload, length, pending.frame, sizeof(pending.frame));
			pending.sent = 0;
			pending.credentials = false;
		}
	}
	return true;
}

// Both ports receive regardless of role. Primary boards transmit independently.
void Application::serviceSerial() {
	if (config_.get("primary").as<bool>()) display_.resetMillisDelta();
	receiveSerial(serialA_, receiverA_, "A");
	receiveSerial(serialB_, receiverB_, "B");

	const bool primary = config_.get("primary").as<bool>();
	const uint32_t now = millis();
	if (primary && !primaryActive_) {
		primaryActive_ = true;
		lastPingAt_[0] = lastPingAt_[1] = now - PingIntervalMs;
		lastWiFiAt_[0] = lastWiFiAt_[1] = now - WiFiBroadcastIntervalMs;
	}
	if (primary) {
		queueWiFiCredentials(now);
	}

	HardwareSerial* ports[] = {&serialA_, &serialB_};
	bool frameSent[2] = {};
	for (size_t index = 0; index < 2; ++index) {
		auto& pending = serialTransmit_[index];
		// Drop an unsent broadcast after demotion; finish partial frames intact.
		if (!primary && pending.credentials && !pending.sent) {
			pending = SerialTransmit{};
		}
		if (!pending.length) continue;
		frameSent[index] = true;
		const int room = ports[index]->availableForWrite();
		if (room <= 0) continue;
		const size_t remaining = pending.length - pending.sent;
		const size_t count = remaining < static_cast<size_t>(room) ? remaining : static_cast<size_t>(room);
		pending.sent += ports[index]->write(pending.frame + pending.sent, count);
		if (pending.sent == pending.length) pending = SerialTransmit{};
	}
	if (!primary) {
		primaryActive_ = false;
		return;
	}
	uint8_t frame[SerialProtocol::PingBufferBytes];
	const size_t length = SerialProtocol::encodePing(now, frame, sizeof(frame));
	for (size_t index = 0; index < 2; ++index) {
		if (!frameSent[index] && uint32_t(now - lastPingAt_[index]) >= PingIntervalMs
			&& ports[index]->availableForWrite() >= static_cast<int>(length)
			&& ports[index]->write(frame, length) == length) {
			lastPingAt_[index] = now;
		}
	}
}

// Temporarily replace the three alpha digits, retaining the mode's logical frame.
void Application::showReset() {
	resetFrame_ = display_.frame();

	if (!display_.showAlpha("RST")) {
		// Do not resume rendering after an overlay write fails.
		resumeState_ = State::DisplayFault;
		Serial.println("Reset: cannot display RST; button remains active");
	}
}

// Clear application settings and Wi-Fi credentials, rebooting only after both succeed.
void Application::factoryReset() {
	if (!display_.clear()) {
		Serial.println("Reset: cannot blank display; continuing reset");
	}

	Serial.println("Reset: Clearing settings...");
	if (oledReady_) {
		oled_.D("Clearing settings...");
	}

	http_.stop();
	LittleFS.end();

	if (!LittleFS.format()) {
		// Remount so services can resume when the button is released.
		config_.setup();
		Serial.println("Reset: filesystem erase failed");
		transitionTo(State::ResetFailed);
		return;
	}

	if (!wifi_.eraseCredentials()) {
		// The filesystem was erased; remount it before waiting for release.
		config_.setup();
		Serial.println("Reset: Wi-Fi erase failed");
		transitionTo(State::ResetFailed);
		return;
	}

	Serial.println("Reset: settings cleared");
	transitionTo(State::Rebooting);
}

// Handle reset hold, cancellation, and retry without restarting a resumed mode.
void Application::serviceButton() {
	if (button_.tick()) {
		Serial.println(button_.pressed() ? "Reset: pressed" : "Reset: released");
		if (button_.pressed() && oledReady_) {
			oled_.D(WiFi.localIP().toString());
		}
	}

	switch (state_) {
		case State::SegmentTest:
		case State::Running:
		case State::DisplayFault:
			if (button_.pressed()) {
				transitionTo(State::ResetHeld);
			}
			break;

		case State::ResetHeld:
		case State::ResetFailed:
			// A failed erase waits for release; retry requires a fresh press.
			if (!button_.pressed()) {
				if (!matrix_.ready() || !display_.show(resetFrame_)) {
					resumeState_ = State::DisplayFault;
				}
				transitionTo(resumeState_);
			} else if (state_ == State::ResetHeld && button_.rawPressed()
				&& uint32_t(millis() - resetPressedAt_) >= ResetHoldMs) {
				// Raw release cancels at the deadline, even during release debounce.
				transitionTo(State::Resetting);
			}
			break;

		default:
			break;
	}
}

// Service shared work in active states and advance rendering when the lifecycle permits it.
void Application::tick() {
	// These states are handled by begin(), entry actions, or the hardware restart.
	switch (state_) {
		case State::Uninitialized:
		case State::Starting:
		case State::ConfigFault:
		case State::Resetting:
		case State::Rebooting:
			return;
		default:
			break;
	}

	serviceButton();
	// ResetFailed polls only the button; reset/reboot paths do no further work.
	if (state_ == State::Resetting || state_ == State::ResetFailed || state_ == State::Rebooting) {
		return;
	}

	wifi_.tick();
	if (wifi_.rebootRequested()) {
		transitionTo(State::Rebooting);
		return;
	}

	// Shared services keep running during startup, reset holds, and display faults.
	serviceHttp();
	http_.tick();
	serviceSerial();
	timeController_.tick(wifi_.ready());
	weatherController_.tick(wifi_.ready(), time_.valid());

	// Startup completion enables the mode; an animation failure disables rendering.
	switch (state_) {
		case State::SegmentTest:
			if (!display_.tick()) {
				transitionTo(State::DisplayFault);
			} else if (!display_.segmentTesting()) {
				transitionTo(State::Running);
			}
			break;

		default:
			break;
	}

	if (state_ == State::Running) {
		switch (mode_) {
			case Mode::Tests:
				tests_.tick();

				break;

			case Mode::Clock:
				clock_.tick();
				break;
		}
		// Display owns queue consumption, rendering, animation, and hardware failures.
		if (!display_.tick() || !matrix_.ready()) {
			transitionTo(State::DisplayFault);
		}
	}

	if (oledReady_) {
		oled_.tick();
	}
}
