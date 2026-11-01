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

// Report framing receipt independently of payload validation or command execution.
enum class Status : uint8_t {
	Ok,
	ErrorLength,
	ErrorTerminator,
	ErrorTimeout
};
constexpr size_t StatusBufferBytes = sizeof("{\"status\":\"error-terminator\"}") + 2;

// Keep protocol status spelling in one place for framing and diagnostic output.
inline const char* statusName(Status status) {
	switch (status) {
		case Status::Ok: return "ok";
		case Status::ErrorLength: return "error-length";
		case Status::ErrorTerminator: return "error-terminator";
		case Status::ErrorTimeout: return "error-timeout";
	}
	return nullptr;
}

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

// Frame a protocol acknowledgment without implying that its payload action succeeded.
inline size_t encodeStatus(Status status, uint8_t* output, size_t capacity) {
	const char* name = statusName(status);
	if (!name) return 0;

	char payload[StatusBufferBytes];
	const int length = snprintf(payload, sizeof(payload), "{\"status\":\"%s\"}", name);

	return length > 0 ? encode(payload, static_cast<size_t>(length), output, capacity) : 0;
}

constexpr size_t IPBufferBytes = sizeof("{\"ip\":\"255.255.255.255\"}") + 2;

// Accept IPv4 addresses or an empty disconnect notice, rejecting embedded NULs and zero addresses.
inline bool validIPAddress(const char* address, size_t length) {
	if (!address || length > 15) return false;
	if (!length) return true;

	unsigned octet = 0, digits = 0, groups = 0;
	bool nonzero = false;

	for (size_t index = 0; index < length; ++index) {
		const char character = address[index];

		if (character >= '0' && character <= '9') {
			octet = octet * 10 + unsigned(character - '0');
			if (++digits > 3 || octet > 255) return false;
		} else if (character == '.' && digits && groups < 3) {
			nonzero = nonzero || octet != 0;
			++groups;
			octet = digits = 0;
		} else {
			return false;
		}
	}

	return groups == 3 && digits && (nonzero || octet != 0);
}

// Announce only the local address; port identity comes from the receiving physical link.
inline size_t encodeIPAddress(const char* address, uint8_t* output, size_t capacity) {
	if (!address) return 0;
	const size_t size = strlen(address);
	if (!validIPAddress(address, size)) return 0;

	char payload[IPBufferBytes];
	const int length = snprintf(payload, sizeof(payload), "{\"ip\":\"%s\"}", address);

	return length > 0 ? encode(payload, static_cast<size_t>(length), output, capacity) : 0;
}

// Timestamped ping remains human-readable on the USB diagnostics console.
constexpr size_t MaxPingPayloadBytes = 15; // "ping " + ten decimal digits.
constexpr size_t PingBufferBytes = MaxPingPayloadBytes + 3;

// Frame a primary timestamp for display timing synchronization.
inline size_t encodePing(uint32_t timestamp, uint8_t* output, size_t capacity) {
	char payload[MaxPingPayloadBytes + 1];
	const int length = snprintf(payload, sizeof(payload), "ping %lu", static_cast<unsigned long>(timestamp));
	return length > 0 ? encode(payload, static_cast<size_t>(length), output, capacity) : 0;
}

// Validate the timestamp text without accepting overflow or trailing characters.
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

	// Abandon incomplete reception after an idle timeout, even when no new byte arrives.
	Result expire(uint32_t now) {
		if (state_ != State::Length && uint32_t(now - lastByteAt_) >= TimeoutMs) {
			// A discarded frame already produced its length or terminator error.
			const bool discarded = state_ == State::Discard;
			reset();
			return discarded ? Result::None : Result::Timeout;
		}
		return Result::None;
	}

	// Advance framing by one byte and report a complete message or a framing error.
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

	// Restart framing while retaining completed payload data for immediate dispatch.
	void reset() { state_ = State::Length; }

	// Expose payload bytes for application dispatch using length(), rather than a delimiter.
	const uint8_t* payload() const { return payload_; }

	// Report the exact payload length, including any embedded binary bytes.
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
