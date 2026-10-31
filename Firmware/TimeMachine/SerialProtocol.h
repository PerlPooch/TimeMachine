#pragma once

#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>

// v1: one raw payload-length byte, payload bytes, LF. No checksum or wire NUL.
namespace SerialProtocol {
constexpr size_t MaxFrameBytes = 250;
constexpr size_t ReservedChecksumBytes = 2;
constexpr size_t MaxPayloadBytes = MaxFrameBytes - 1 - ReservedChecksumBytes - 1;
constexpr size_t BufferBytes = MaxFrameBytes + 1; // Optional local NUL, never transmitted.

// Return wire length; reject oversized data or insufficient destination space.
inline size_t encode(const char* payload, size_t length, uint8_t* output, size_t capacity) {
	if (!payload || !output || length > MaxPayloadBytes || capacity < length + 3) {
		return 0;
	}
	output[0] = static_cast<uint8_t>(length);
	memcpy(output + 1, payload, length);
	output[length + 1] = '\n';
	output[length + 2] = 0;
	return length + 2;
}

// Timestamped ping remains human-readable on the USB diagnostics console.
constexpr size_t MaxPingPayloadBytes = 15; // "ping " + ten decimal digits.
constexpr size_t PingBufferBytes = MaxPingPayloadBytes + 3;
inline size_t encodePing(uint32_t timestamp, uint8_t* output, size_t capacity) {
	char payload[MaxPingPayloadBytes + 1];
	const int length = snprintf(payload, sizeof(payload), "ping %lu", static_cast<unsigned long>(timestamp));
	return length > 0 ? encode(payload, static_cast<size_t>(length), output, capacity) : 0;
}

inline bool decodePing(const uint8_t* payload, size_t length, uint32_t& timestamp) {
	if (!payload || length < 6 || length > MaxPingPayloadBytes || memcmp(payload, "ping ", 5) != 0) return false;
	uint32_t value = 0;
	for (size_t index = 5; index < length; ++index) {
		if (payload[index] < '0' || payload[index] > '9') return false;
		const uint32_t digit = payload[index] - '0';
		if (value > (UINT32_MAX - digit) / 10u) return false;
		value = value * 10u + digit;
	}
	timestamp = value;
	return true;
}

// Fixed-size local strings; lengths on the wire exclude their terminating NULs.
struct WiFiCredentials {
	char ssid[33] = {};
	char password[65] = {};
};

constexpr size_t WiFiHeaderBytes = 7; // "wifi1", SSID length, password length.
constexpr size_t WiFiBufferBytes = WiFiHeaderBytes + 32 + 64 + 3;

// Reserve the wifi prefix for sensitive messages, including malformed versions.
inline bool isWiFiCredentials(const uint8_t* payload, size_t length) {
	return payload && length >= 4 && memcmp(payload, "wifi", 4) == 0;
}

// Encode credentials without JSON escaping, preserving the full SSID/password limits.
inline size_t encodeWiFiCredentials(const WiFiCredentials& credentials, uint8_t* output, size_t capacity) {
	const size_t ssidLength = strnlen(credentials.ssid, sizeof(credentials.ssid));
	const size_t passwordLength = strnlen(credentials.password, sizeof(credentials.password));
	if (!ssidLength || ssidLength > 32 || passwordLength > 64) {
		return 0;
	}

	char payload[WiFiHeaderBytes + 32 + 64];
	memcpy(payload, "wifi1", 5);
	payload[5] = static_cast<char>(ssidLength);
	payload[6] = static_cast<char>(passwordLength);
	memcpy(payload + WiFiHeaderBytes, credentials.ssid, ssidLength);
	memcpy(payload + WiFiHeaderBytes + ssidLength, credentials.password, passwordLength);
	return encode(payload, WiFiHeaderBytes + ssidLength + passwordLength, output, capacity);
}

// Reject malformed lengths and embedded NULs before exposing strings to MDS_WiFi.
inline bool decodeWiFiCredentials(const uint8_t* payload, size_t length, WiFiCredentials& credentials) {
	if (!payload || length < WiFiHeaderBytes || memcmp(payload, "wifi1", 5) != 0) {
		return false;
	}

	const size_t ssidLength = payload[5];
	const size_t passwordLength = payload[6];
	if (!ssidLength || ssidLength > 32 || passwordLength > 64
		|| length != WiFiHeaderBytes + ssidLength + passwordLength
		|| memchr(payload + WiFiHeaderBytes, 0, ssidLength + passwordLength)) {
		return false;
	}

	credentials = WiFiCredentials{};
	memcpy(credentials.ssid, payload + WiFiHeaderBytes, ssidLength);
	memcpy(credentials.password, payload + WiFiHeaderBytes + ssidLength, passwordLength);
	return true;
}

// Separate instances keep fragmented/interleaved traffic on A and B independent.
class Receiver {
public:
	enum class Result { None, Message, InvalidLength, InvalidTerminator, Timeout };
	static constexpr uint32_t TimeoutMs = 100;

	Result expire(uint32_t now) {
		if (state_ != State::Length && uint32_t(now - lastByteAt_) >= TimeoutMs) {
			reset();
			return Result::Timeout;
		}
		return Result::None;
	}

	Result feed(uint8_t byte, uint32_t now) {
		lastByteAt_ = now;
		switch (state_) {
			case State::Length:
				if (byte > MaxPayloadBytes) {
					state_ = State::Discard;
					return Result::InvalidLength;
				}
				length_ = byte;
				received_ = 0;
				state_ = length_ ? State::Payload : State::Terminator;
				break;
			case State::Payload:
				payload_[received_++] = byte;
				if (received_ == length_) {
					state_ = State::Terminator;
				}
				break;
			case State::Terminator:
				payload_[length_] = 0; // Local convenience; wire framing uses length.
				if (byte != '\n') {
					state_ = State::Discard;
					return Result::InvalidTerminator;
				}
				reset();
				return Result::Message;
			case State::Discard:
				// After a malformed frame, recover at LF or after an idle timeout.
				if (byte == '\n') {
					reset();
				}
				break;
		}
		return Result::None;
	}

	void reset() { state_ = State::Length; }
	const uint8_t* payload() const { return payload_; }
	size_t length() const { return length_; }

private:
	enum class State { Length, Payload, Terminator, Discard };
	State state_ = State::Length;
	uint8_t payload_[MaxPayloadBytes + 1] = {};
	size_t length_ = 0;
	size_t received_ = 0;
	uint32_t lastByteAt_ = 0;
};

}
