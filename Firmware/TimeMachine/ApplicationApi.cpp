#include "ApplicationApi.h"
#include "SerialProtocol.h"
#include "ClockCalendar.h"
#include <string.h>
#include <utility>

// Borrow application services and the shared queue without starting the HTTP listener.
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

// Register long-lived handlers once; callbacks return to this API instance.
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

// Resolve request keys through the application setting definitions.
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

// Accept decimal integers only and reject overflow before applying a request.
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

// Parse signed decimal text without floating point, truncation, or overflow at INT64_MIN.
bool ApplicationApi::parseSigned(const char* text, size_t length, int64_t& value) {
	if (!text || !length) return false;

	const bool negative = text[0] == '-';
	size_t index = text[0] == '-' || text[0] == '+' ? 1 : 0;
	if (index == length) return false;

	const uint64_t limit = uint64_t(INT64_MAX) + (negative ? 1u : 0u);
	uint64_t magnitude = 0;

	for (; index < length; ++index) {
		if (text[index] < '0' || text[index] > '9') return false;

		const uint8_t digit = text[index] - '0';
		if (magnitude > (limit - digit) / 10) return false;
		magnitude = magnitude * 10 + digit;
	}

	value = negative ? (magnitude == uint64_t(INT64_MAX) + 1u ? INT64_MIN : -int64_t(magnitude))
		: int64_t(magnitude);
	return true;
}

// Interpret a signed amount in seconds, minutes, hours, or 24-hour days as a replacement offset.
bool ApplicationApi::parseRelativeOffset(const String& text, int64_t& value) {
	const size_t length = text.length();
	if (length < 3 || (text[0] != '+' && text[0] != '-')) return false;

	int64_t factor;
	switch (text[length - 1]) {
		case 's': factor = 1; break;
		case 'm': factor = 60; break;
		case 'h': factor = 3600; break;
		case 'd': factor = 86400; break;
		default: return false;
	}

	int64_t amount;
	if (!parseSigned(text.c_str(), length - 1, amount)
		|| amount < INT64_MIN / factor || amount > INT64_MAX / factor) return false;

	value = amount * factor;
	return true;
}

// Convert exactly YYYY-MM-DDTHH:MM:SSZ to UTC seconds without using the configured timezone.
bool ApplicationApi::parseUTCDateTime(const String& text, int64_t& value) {
	if (text.length() != 20 || text[4] != '-' || text[7] != '-' || text[10] != 'T'
		|| text[13] != ':' || text[16] != ':' || text[19] != 'Z') return false;

	constexpr uint8_t starts[] = {0, 5, 8, 11, 14, 17};
	constexpr uint8_t lengths[] = {4, 2, 2, 2, 2, 2};
	int fields[6] = {};

	for (size_t field = 0; field < 6; ++field) {
		for (uint8_t digit = 0; digit < lengths[field]; ++digit) {
			const char character = text[starts[field] + digit];
			if (character < '0' || character > '9') return false;
			fields[field] = fields[field] * 10 + character - '0';
		}
	}

	const int year = fields[0], month = fields[1], day = fields[2];
	if (year < 1 || month < 1 || month > 12 || day < 1
		|| day > ClockCalendar::daysInMonth(year, month)
		|| fields[3] > 23 || fields[4] > 59 || fields[5] > 59) return false;

	// Count Gregorian days relative to 1970; 477 leap days precede that year.
	const int previousYear = year - 1;
	int64_t days = int64_t(year - 1970) * 365
		+ previousYear / 4 - previousYear / 100 + previousYear / 400 - 477;

	for (int previousMonth = 1; previousMonth < month; ++previousMonth) {
		days += ClockCalendar::daysInMonth(year, previousMonth);
	}

	value = (days + day - 1) * 86400 + fields[3] * 3600 + fields[4] * 60 + fields[5];
	return true;
}

// Report failure separately from whether a runtime change has already been applied.
void ApplicationApi::sendError(int code, const char* message, bool applied) {
	JsonDocument response;
	response["error"] = message;
	response["applied"] = applied;
	response["saved"] = false;
	String body;
	serializeJson(response, body);
	server().send(code, "application/json", body);
}

// Return current nonsensitive settings without exposing stored credentials or keys.
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

// Combine repeated destination selections; an explicit selection replaces the local default.
bool ApplicationApi::addDestination(const String& value, uint8_t& destinations, bool& selected) {
	uint8_t bits;
	if (value == "all") bits = Display::Local | Display::PortA | Display::PortB;
	else if (value == "local") bits = Display::Local;
	else if (value == "a") bits = Display::PortA;
	else if (value == "b") bits = Display::PortB;
	else return false;

	if (!selected) destinations = 0;
	selected = true;
	destinations |= bits;
	return true;
}

// Apply an already normalized group locally, honoring access, validation, and persistence flags.
bool ApplicationApi::applyConfig(const JsonDocument& updates) {
	if (!config_.set(updates)) return false;
	if (!syncDisplaySettings(!updates["gcc"].isUnbound())) return false;

	bool saveRequired = false;
	for (JsonPairConst item : updates.as<JsonObjectConst>()) {
		const auto* setting = findSetting(item.key().c_str());
		saveRequired = saveRequired || (setting && setting->persistent);
	}
	return !saveRequired || config_.save();
}

// Decode normalized configuration edits and execute locally; framing acknowledgment is separate.
bool ApplicationApi::receiveConfigCommand(const uint8_t* payload, size_t length) {
	JsonDocument document;
	if (length > SerialProtocol::MaxPayloadBytes || deserializeJson(document, payload, length)
		|| !document.is<JsonArray>() || document.size() != 2
		|| !document[0].is<const char*>() || strcmp(document[0].as<const char*>(), "config1")
		|| !document[1].is<JsonObject>() || !document[1].size()) return false;

	JsonDocument updates;
	updates.set(document[1]);
	return !updates.overflowed() && applyConfig(updates);
}

// Validate grouped edits, apply display settings, and save only accepted changes.
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
	uint8_t destinations = Display::Local;
	bool selected = false;

	for (int index = 0; index < server().args(); ++index) {
		const String key = server().argName(index);
		if (key == "to") {
			if (!addDestination(server().arg(index), destinations, selected)) {
				sendError(400, "Destination must be local, a, b, or all");
				return;
			}
			continue;
		}
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

		if (key == "timeOffset") {
			int64_t offset;

			// Bare zero clears the display offset without a unit or synchronized time.
			if (text == "0") {
				offset = 0;
			} else if (text.length() && (text[0] == '+' || text[0] == '-')) {
				if (!parseRelativeOffset(text, offset)) {
					sendError(400, "Expected signed integer with s, m, h, or d; value must fit signed 64-bit seconds");
					return;
				}
			} else {
				int64_t target, current;
				if (!parseUTCDateTime(text, target)) {
					sendError(400, "Expected 0, +5d, or strict UTC YYYY-MM-DDTHH:MM:SSZ");
					return;
				}

				if (!timeSource_ || !timeSource_(current)) {
					sendError(503, "Synchronized time required for an absolute timeOffset");
					return;
				}

				if ((current > 0 && target < INT64_MIN + current)
					|| (current < 0 && target > INT64_MAX + current)) {
					sendError(400, "Time offset exceeds signed 64-bit seconds");
					return;
				}

				offset = target - current;
			}

			updates[key] = offset;
		} else if (setting->type == MDS_ConfigType::Signed) {
			int64_t value;
			if (!parseSigned(text.c_str(), text.length(), value)) {
				sendError(400, "Expected a signed decimal integer");
				return;
			}
			updates[key] = value;
		} else if (setting->type == MDS_ConfigType::Text) {
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

	if (!updates.size()) {
		sendError(400, "Provide at least one setting");
		return;
	}

	// Validate with the same definitions without changing live settings before queue acceptance.
	size_t count;
	const auto* settings = config_.settings(count);
	MDS_Config validation;
	if (!validation.setSettings(settings, count) || !validation.set(updates)) {
		sendError(400, validation.lastError());
		return;
	}

	if (destinations & (Display::PortA | Display::PortB)) {
		JsonDocument document;
		auto envelope = document.to<JsonArray>();
		envelope.add("config1");
		envelope.add(updates.as<JsonObjectConst>());
		char payload[SerialProtocol::MaxPayloadBytes + 1];
		const size_t length = measureJson(document);
		if (document.overflowed() || length > SerialProtocol::MaxPayloadBytes) {
			sendError(413, "Configuration exceeds serial payload limit");
			return;
		}
		serializeJson(document, payload, sizeof(payload));
		if (!serialRouter_ || !serialRouter_(destinations, payload, length)) {
			sendError(503, "Serial transmit queue busy or unavailable");
			return;
		}
	}

	if (!(destinations & Display::Local)) {
		server().send(202, "application/json", "{\"accepted\":true}");
		return;
	}

	if (!config_.set(updates)) {
		sendError(500, config_.lastError());
		return;
	}
	if (!syncDisplaySettings(!updates["gcc"].isUnbound())) {
		sendError(500, "Cannot apply display settings", true);
		return;
	}
	bool saveRequired = false;
	for (JsonPairConst item : updates.as<JsonObjectConst>()) {
		saveRequired = saveRequired || findSetting(item.key().c_str())->persistent;
	}
	if (saveRequired && !config_.save()) {
		sendError(500, config_.lastError(), true);
		return;
	}

	sendConfig();
}

// Keep actions out of the settings registry and filesystem persistence path.
void ApplicationApi::displayRequest() {
	Display::DisplayCommand command;

	bool hasAction = false;
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
			if (!addDestination(value, command.destinations, hasDestination)) {
				sendError(400, "Destination must be local, a, b, or all");
				return;
			}
		} else if (key == "message" || key == "scroll" || key == "slide" || key == "appear" || key == "fade") {
			if (hasAction) {
				sendError(400, "Provide exactly one message, scroll, appear, slide, or fade");
				return;
			}
			hasAction = true;

			if (key == "fade") {
				uint32_t level;
				if (!parseUnsigned(value, level) || level > 255) {
					sendError(400, "Fade level must be an integer from 0 to 255");
					return;
				}

				command.kind = Display::DisplayCommand::Kind::Fade;
				command.level = static_cast<uint8_t>(level);
				continue;
			}

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
			if (hasSpeed || !parseUnsigned(value, command.speed) || command.speed > 60000) {
				sendError(400, "Speed must be one integer from 0 to 60000 ms");
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

	if (!hasAction || (command.kind == Display::DisplayCommand::Kind::Message && (hasSpeed || hasDirection))) {
		sendError(400, "message | slide [speed] [direction] | scroll [speed] [direction] | appear [speed] [direction] | fade=0..255 [speed]; all accept [to]; text actions also accept indicators");
		return;
	}

	if (command.kind == Display::DisplayCommand::Kind::Fade) {
		if (hasDirection || indicatorKeys || hasColons) {
			sendError(400, "Fade accepts only level, speed, and destinations");
			return;
		}

		if (!hasSpeed) command.speed = 350;
	} else if (!command.speed) {
		sendError(400, "Text animation speed must be from 1 to 60000 ms");
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
	if (command.kind == Display::DisplayCommand::Kind::Fade) {
		if (command.speed > 60000) return 0;
		fields.add("fade");
		fields.add(command.level);
		fields.add(command.speed);
	} else {
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
	}

	const size_t length = measureJson(document);
	if (document.overflowed() || length > SerialProtocol::MaxPayloadBytes || capacity <= length) return 0;
	return serializeJson(document, output, capacity);
}

// Validate a serial display envelope and post it for local execution only.
bool ApplicationApi::receiveDisplayCommand(const uint8_t* payload, size_t length) {
	JsonDocument document;
	if (length > SerialProtocol::MaxPayloadBytes || deserializeJson(document, payload, length)
		|| !document.is<JsonArray>()) return false;
	auto fields = document.as<JsonArrayConst>();

	if (!fields[0].is<const char*>() || !fields[1].is<const char*>()) return false;

	Display::DisplayCommand command;
	const String version = fields[0].as<const char*>();
	const String kind = fields[1].as<const char*>();

	// Fade has no text, direction, or indicator fields; it preserves existing content.
	if (kind == "fade") {
		if (version != "display2" || fields.size() != 4
			|| !fields[2].is<uint8_t>() || !fields[3].is<uint32_t>()) return false;

		command.kind = Display::DisplayCommand::Kind::Fade;
		command.level = fields[2].as<uint8_t>();
		command.speed = fields[3].as<uint32_t>();
		if (command.speed > 60000) return false;
		return commands_.post(std::move(command));
	}

	if ((fields.size() != 5 && fields.size() != 6)
		|| !fields[2].is<const char*>() || !fields[3].is<uint32_t>()
		|| !fields[4].is<const char*>()) return false;

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
