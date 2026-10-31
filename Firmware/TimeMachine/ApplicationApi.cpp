#include "ApplicationApi.h"
#include "SerialProtocol.h"
#include <string.h>
#include <utility>

ApplicationApi::ApplicationApi(MDS_HTTP& http, MDS_Config& config, MatrixDriver& matrix, Display::CommandQueue& commands, MDS_Weather* weather):
	http_(http), 
	config_(config), 
	matrix_(matrix), 
	weather_(weather), 
	commands_(commands) {}

// Forward native route registration without repeating the wrapper plumbing.
void ApplicationApi::on(const char* path, HTTPMethod method, MDS_HTTPServer::THandlerFunction handler) {
	server().on(path, method, handler);
}

// Apply PWM and explicit GCC edits; reads preserve temporary diagnostic current.
bool ApplicationApi::syncDisplaySettings(bool applyCurrent) {
	if (!matrix_.setBrightnessLimit(config_.get("pwm").as<uint8_t>())) {
		return false;
	}

	return !applyCurrent || matrix_.setConfiguredCurrent(config_.get("gcc").as<uint8_t>());
}

void ApplicationApi::begin() {
	if (!registered_) {
		on("/",				HTTP_GET, [this]() { configRequest(); });
		on("/api/config",	HTTP_GET, [this]() { configRequest(); });
		on("/api/display",	HTTP_GET, [this]() { displayRequest(); });
		if (weather_) {
			on("/api/weather", HTTP_GET, [this]() { weatherRequest(); });
		}

		registered_ = true;
	}
}

const MDS_ConfigSetting* ApplicationApi::findSetting(const char* key) const {
	size_t count;
	const auto* settings = config_.settings(count);

	for (size_t index = 0; index < count; ++index) {
		if (strcmp(settings[index].key, key) == 0) {
			return &settings[index];
		}
	}

	return nullptr;
}

bool ApplicationApi::parseUnsigned(const String& text, uint32_t& value) {
	if (!text.length()) {
		return false;
	}

	value = 0;

	for (size_t index = 0; index < text.length(); ++index) {
		const char character = text[index];

		if (character < '0' || character > '9') {
			return false;
		}

		const uint8_t digit = character - '0';

		if (value > (UINT32_MAX - digit) / 10) {
			return false;
		}

		value = value * 10 + digit;
	}

	return true;
}

void ApplicationApi::sendError(int code, const char* message, bool applied) {
	JsonDocument response;
	response["error"] = message;
	response["applied"] = applied;
	response["saved"] = false;
	String body;
	serializeJson(response, body);
	server().send(code, "application/json", body);
}

void ApplicationApi::sendConfig() {
	JsonDocument response;
	response.to<JsonObject>();
	size_t count;
	const auto* settings = config_.settings(count);

	for (size_t index = 0; index < count; ++index) {
		if (!settings[index].sensitive) {
			response[settings[index].key] = config_.get(settings[index].key);
		}
	}

	if (response.overflowed()) {
		sendError(500, "Cannot allocate configuration response");
		return;
	}

	String body;
	serializeJsonPretty(response, body);
	server().send(200, "application/json", body);
}

void ApplicationApi::configRequest() {
	server().sendHeader("Cache-Control", "no-store");

	if (!server().args()) {
		if (!syncDisplaySettings()) {
			sendError(500, "Cannot synchronize display settings");
			return;
		}

		sendConfig();
		return;
	}

	JsonDocument updates;
	updates.to<JsonObject>();

	for (int index = 0; index < server().args(); ++index) {
		const String key = server().argName(index);
		const auto* setting = findSetting(key.c_str());

		if (!setting) {
			sendError(400, "Unknown setting");
			return;
		}

		if (setting->access == MDS_ConfigAccess::ReadOnly) {
			sendError(403, "Setting is read-only");
			return;
		}

		if (!updates[key].isUnbound()) {
			sendError(400, "Duplicate setting");
			return;
		}

		const String text = server().arg(index);

		if (setting->type == MDS_ConfigType::Text) {
			updates[key] = text;
		} else if (setting->type == MDS_ConfigType::Boolean) {
			if (text != "true" && text != "false") {
				sendError(400, "Expected true or false");
				return;
			}
			updates[key] = text == "true";
		} else {
			uint32_t value;

			if (!parseUnsigned(text, value)) {
				sendError(400, "Expected an unsigned decimal integer");
				return;
			}

			updates[key] = value;
		}
	}

	if (updates.overflowed()) {
		sendError(500, "Cannot allocate setting updates");
		return;
	}

	if (!config_.set(updates)) {
		sendError(400, config_.lastError());
		return;
	}

	if (!syncDisplaySettings(!updates["gcc"].isUnbound())) {
		sendError(500, "Cannot apply display settings", true);
		return;
	}

	if (!config_.save()) {
		// Keep the validated runtime edit and explicitly report persistence failure.
		sendError(500, config_.lastError(), true);
		return;
	}

	sendConfig();
}

// Keep actions out of the settings registry and filesystem persistence path.
void ApplicationApi::displayRequest() {
	Display::DisplayCommand command;

	bool hasText = false;
	bool hasSpeed = false;
	bool hasDirection = false;
	bool hasDestination = false;
	uint8_t indicatorKeys = 0;
	bool hasColons = false;

	server().sendHeader("Cache-Control", "no-store");

	for(int index = 0; index < server().args(); ++index) {
		const String key = server().argName(index);
		const String value = server().arg(index);
		
		if (key == "to") {
			if (!hasDestination) {
				command.destinations = 0;
				hasDestination = true;
			}
			if (value == "local") command.destinations |= Display::Local;
			else if (value == "a") command.destinations |= Display::PortA;
			else if (value == "b") command.destinations |= Display::PortB;
			else { sendError(400, "Destination must be local, a, or b"); return; }
		} else if (key == "message" || key == "scroll" || key == "slide" || key == "appear") {
			if (hasText) {
				sendError(400, "Provide exactly one message, scroll, appear, or slide");
				return;
			}
			hasText = true;
			command.kind = key == "scroll" ? Display::DisplayCommand::Kind::Scroll
				: key == "slide" ? Display::DisplayCommand::Kind::Slide
				: key == "appear" ? Display::DisplayCommand::Kind::Appear
				: Display::DisplayCommand::Kind::Message;

			if (value.length() > (command.kind == Display::DisplayCommand::Kind::Scroll ? 512u : 13u)) {
				sendError(400, "Display text is too long");
				return;
			}
			command.text = value;
		} else if (key == "am" || key == "pm" || key == "colontop" || key == "colonbottom") {
			const uint8_t flag = key == "am" ? 1 : key == "pm" ? 2 : key == "colontop" ? 4 : 8;

			if ((indicatorKeys & flag) || (value != "true" && value != "false")) {
				sendError(400, "Indicators must each be one true or false value");
				return;
			}

			indicatorKeys |= flag;
			const bool enabled = value == "true";

			if (key == "am") command.am = enabled;
			else if (key == "pm") command.pm = enabled;
			else if (key == "colontop") command.colonTop = enabled;
			else command.colonBottom = enabled;

		} else if (key == "colons") {
			if (hasColons || value != "blink") {
				sendError(400, "Colons must be one blink value");
				return;
			}

			hasColons = true;
			command.colonsBlink = true;
		} else if (key == "speed") {
			if (hasSpeed || !parseUnsigned(value, command.speed) || command.speed < 1 || command.speed > 60000) {
				sendError(400, "Speed must be one integer from 1 to 60000 ms");
				return;
			}
			hasSpeed = true;
		} else if (key == "direction") {
			if (hasDirection || (value != "left" && value != "right")) {
				sendError(400, "Direction must be left or right");
				return;
			}
			hasDirection = true;
			command.direction = value == "left" ? Display::Direction::Left : Display::Direction::Right;
		} else {
			sendError(400, "Unknown display parameter");
			return;
		}
	}

	// Reject ambiguous static/blinking colon choices before routing anything.
	if (hasColons && (indicatorKeys & 12)) {
		sendError(400, "Use colons=blink or individual colon values, not both");
		return;
	}

	if (!hasText || (command.kind == Display::DisplayCommand::Kind::Message && (hasSpeed || hasDirection))) {
		sendError(400, "message | slide [speed] [direction] | scroll [speed] [direction] | appear [speed] [direction]; all accept [am] [pm] [colontop] [colonbottom] or [colons=blink], and [to]");
		return;
	}

	if (command.destinations & (Display::PortA | Display::PortB)) {
		char payload[SerialProtocol::MaxPayloadBytes + 1];
		if (!encodeDisplayCommand(command, payload, sizeof(payload))) {
			sendError(413, "Display command exceeds serial payload limit");
			return;
		}
		if (!displayRouter_ || !displayRouter_(command)) {
			sendError(503, "Serial transmit queue busy or unavailable");
			return;
		}
	}
	if (command.destinations & Display::Local) {
		if (!commands_.post(std::move(command))) {
			sendError(500, "Cannot allocate display command");
			return;
		}
	}
	server().send(202, "application/json", "{\"accepted\":true}");
}

// Expose cache/status only; the HTTP request never waits for a weather fetch.
void ApplicationApi::weatherRequest() {
	server().sendHeader("Cache-Control", "no-store");
	JsonDocument response;
	response["state"] = weather_->state();
	response["enabled"] = weather_->enabled();
	response["configured"] = weather_->configured();
	response["fetching"] = weather_->fetching();
	response["available"] = weather_->available();
	response["fresh"] = weather_->fresh();
	response["httpStatus"] = weather_->httpStatus();
	response["lastSuccessUtc"] = weather_->lastSuccessUtc();
	response["nextUpdateSeconds"] = weather_->nextUpdateSeconds();
	response["error"] = weather_->lastError();
	response["units"] = "imperial";

	if (weather_->available()) {
		const auto& sample = weather_->sample();
		response["ageSeconds"] = weather_->ageSeconds();
		response["temperatureF"] = sample.temperatureF;
		response["latitude"] = sample.latitude;
		response["longitude"] = sample.longitude;
		response["condition"] = sample.condition;
		response["description"] = sample.description;
		response["timezoneOffsetSeconds"] = sample.timezoneOffsetSeconds;
		response["observedUtc"] = sample.observedUtc;
	}

	if (response.overflowed()) {
		sendError(500, "Cannot allocate weather response");
		return;
	}

	String body;
	serializeJsonPretty(response, body);
	server().send(200, "application/json", body);
}

// Compact, versioned display envelope; routing is deliberately absent on the wire.
size_t ApplicationApi::encodeDisplayCommand(const Display::DisplayCommand& command, char* output, size_t capacity) {
	if (command.kind == Display::DisplayCommand::Kind::Clock) return 0;
	JsonDocument document;
	auto fields = document.to<JsonArray>();
	fields.add("display2");
	fields.add(command.kind == Display::DisplayCommand::Kind::Scroll ? "scroll" :
		command.kind == Display::DisplayCommand::Kind::Slide ? "slide" :
		command.kind == Display::DisplayCommand::Kind::Appear ? "appear" : "message");
	fields.add(command.text);
	fields.add(command.speed);
	fields.add(command.direction == Display::Direction::Right ? "right" : "left");
	// Indicator bits: AM, PM, top colon, bottom colon, blinking colons.
	const uint8_t indicators = (command.am ? 1 : 0) | (command.pm ? 2 : 0)
		| (command.colonTop ? 4 : 0) | (command.colonBottom ? 8 : 0)
		| (command.colonsBlink ? 16 : 0);

	if (command.colonsBlink && (command.colonTop || command.colonBottom)) return 0;

	fields.add(indicators);

	const size_t length = measureJson(document);
	if (document.overflowed() || length > SerialProtocol::MaxPayloadBytes || capacity <= length) return 0;
	return serializeJson(document, output, capacity);
}

// Validate a serial display envelope and post it for local execution only.
bool ApplicationApi::receiveDisplayCommand(const uint8_t* payload, size_t length) {
	JsonDocument document;
	if (length > SerialProtocol::MaxPayloadBytes || deserializeJson(document, payload, length)
		|| !document.is<JsonArray>() || (document.size() != 5 && document.size() != 6)) return false;
	auto fields = document.as<JsonArrayConst>();

	if (!fields[0].is<const char*>() || !fields[1].is<const char*>()
		|| !fields[2].is<const char*>() || !fields[3].is<uint32_t>()
		|| !fields[4].is<const char*>()) return false;

	Display::DisplayCommand command;
	const String version = fields[0].as<const char*>();

	if (version == "display2") {
		if (fields.size() != 6 || !fields[5].is<uint8_t>()) return false;

		const uint8_t indicators = fields[5].as<uint8_t>();
		if (indicators > 31 || ((indicators & 16) && (indicators & 12))) return false;

		command.am = indicators & 1;
		command.pm = indicators & 2;
		command.colonTop = indicators & 4;
		command.colonBottom = indicators & 8;
		command.colonsBlink = indicators & 16;

	} else if (version == "display1") {
		// Older envelopes had no indicators, or only a colon blink boolean.
		if (fields.size() == 6 && !fields[5].is<bool>()) return false;
		command.colonsBlink = fields.size() == 6 ? fields[5].as<bool>() : false;

	} else {
		return false;
	}

	const String kind = fields[1].as<const char*>();
	if (kind == "message") command.kind = Display::DisplayCommand::Kind::Message;
	else if (kind == "scroll") command.kind = Display::DisplayCommand::Kind::Scroll;
	else if (kind == "slide") command.kind = Display::DisplayCommand::Kind::Slide;
	else if (kind == "appear") command.kind = Display::DisplayCommand::Kind::Appear;
	else return false;
	const JsonString text = fields[2].as<JsonString>();
	if (strlen(text.c_str()) != text.size() || text.size() > (kind == "scroll" ? 512u : 13u)) return false;
	command.text = text.c_str();
	if (command.text.length() != text.size()) return false;
	command.speed = fields[3].as<uint32_t>();
	if (command.speed < 1 || command.speed > 60000) return false;
	const String direction = fields[4].as<const char*>();
	if (direction != "left" && direction != "right") return false;
	command.direction = direction == "right" ? Display::Direction::Right : Display::Direction::Left;
	return commands_.post(std::move(command)); // Local only; never invoke the serial router.
}
