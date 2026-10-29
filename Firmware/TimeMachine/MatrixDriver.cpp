#include "MatrixDriver.h"
#include "DisplayGamma.h"

// Record error and disable the chip; error is the I2C status or internal error code.
bool MatrixDriver::fail(uint8_t error) {
	error_ = error;
	ready_ = false;
	digitalWrite(enablePin_, LOW);
	return false;
}

// Write count bytes from data starting at reg on the selected page; fail safely on I2C errors.
bool MatrixDriver::write(uint8_t reg, const uint8_t* data, size_t count) {
	wire_.beginTransmission(Address);
	const bool queued = wire_.write(reg) == 1 && wire_.write(data, count) == count;
	const uint8_t status = wire_.endTransmission();

	if (!queued || status) {
		return fail(status ? status : 1);
	}

	return true;
}

// Write value to reg on the currently selected register page.
bool MatrixDriver::writeByte(uint8_t reg, uint8_t value) {
	return write(reg, &value, 1);
}

// Unlock page selection and select register page number (0-3).
bool MatrixDriver::page(uint8_t number) {
	return writeByte(0xFE, 0xC5) && writeByte(0xFD, number);
}

// Initialize with the configured normal current.
bool MatrixDriver::begin(const uint8_t* enabledChannels, uint8_t channelCount) {
	return begin(configuredCurrent_, enabledChannels, channelCount);
}

// Initialize at globalCurrent (0-255), enabling channelCount addresses from enabledChannels (0-191).
bool MatrixDriver::begin(uint8_t globalCurrent, const uint8_t* enabledChannels, uint8_t channelCount) {
	ready_ = false;
	error_ = 0;
	digitalWrite(enablePin_, LOW);
	pinMode(enablePin_, OUTPUT);

	if (channelCount > 192 || (channelCount && !enabledChannels)) {
		return fail(0xFF);
	}

	uint8_t enableMask[24] = {};

	for (uint8_t index = 0; index < channelCount; ++index) {
		const uint8_t channel = enabledChannels[index];

		if (channel >= 192) {
			return fail(0xFF);
		}

		enableMask[channel / 8] |= uint8_t(1) << (channel % 8);
	}

	// Function registers are accessible in hardware shutdown. Blank the
	// driver before releasing SDB, including after a controller-only reset.
	if (!page(3) || !writeByte(0x00, 0x00) || !writeByte(0x01, globalCurrent)) {
		return false;
	}

	globalCurrent_ = globalCurrent;

	digitalWrite(enablePin_, HIGH);
	delay(10);

	// Clear all channels, including the unpopulated and unused ones.
	const uint8_t zeros[16] = {};

	if (!page(0) || !write(0x00, zeros, 16) || !write(0x10, zeros, 8)) {
		return false;
	}

	if (!page(1)) {
		return false;
	}

	for (uint16_t offset = 0; offset < 192; offset += 16) {
		if (!write(static_cast<uint8_t>(offset), zeros, 16)) {
			return false;
		}
	}

	for (uint8_t& brightness : logicalBrightness_) {
		brightness = 0;
	}

	// The application supplies the populated channels; unused outputs stay off.
	if (!page(0) || !write(0x00, enableMask, 16) || !write(0x10, enableMask + 16, 8)) {
		return false;
	}

	// Independent clock, 26.7 kHz PWM (PFS=010), normal operation.
	if (!page(3) || !writeByte(0x00, 0x11)) {
		return false;
	}

	ready_ = true;

	return true;
}

// Set channel address (0-191) to logical brightness (0-255), applying gamma and the PWM limit.
bool MatrixDriver::setBrightness(uint8_t address, uint8_t brightness) {
	if (!ready_) {
		return false;
	}

	if (address >= 192) {
		return fail(0xFF);
	}

	if (!page(1) || !writeByte(address, DisplayGamma::toPWM(brightness, effectiveMaximumPWM()))) {
		return false;
	}

	logicalBrightness_[address] = brightness;
	return true;
}

// Set count channels from firstAddress to brightness (0-255), applying gamma and the PWM limit.
bool MatrixDriver::fillBrightness(uint8_t firstAddress, uint8_t count, uint8_t brightness) {
	if (!ready_) {
		return false;
	}

	if (firstAddress >= 192 || count > 192 - firstAddress) {
		return fail(0xFF);
	}

	if (count == 0) {
		return true;
	}

	if (!page(1)) {
		return false;
	}

	const uint8_t pwm = DisplayGamma::toPWM(brightness, effectiveMaximumPWM());
	uint8_t values[16];

	for (uint8_t& item : values) {
		item = pwm;
	}

	for (uint16_t offset = 0; offset < count; offset += 16) {
		const uint8_t remaining = count - offset;
		const uint8_t chunk = remaining < 16 ? remaining : 16;

		if (!write(firstAddress + offset, values, chunk)) {
			return false;
		}
	}

	for (uint16_t index = firstAddress; index < firstAddress + count; ++index) {
		logicalBrightness_[index] = brightness;
	}

	return true;
}

// Write count logical brightness values (0-255) from brightness, starting at firstAddress.
bool MatrixDriver::writeBrightness(uint8_t firstAddress, const uint8_t* brightness, uint8_t count) {
	if (!ready_) {
		return false;
	}

	if (firstAddress >= 192 || count > 192 - firstAddress || (count && !brightness)) {
		return fail(0xFF);
	}

	if (count == 0) {
		return true;
	}

	if (!page(1)) {
		return false;
	}

	for (uint16_t offset = 0; offset < count; offset += 16) {
		const uint8_t remaining = count - offset;
		const uint8_t chunk = remaining < 16 ? remaining : 16;
		uint8_t values[16];

		for (uint8_t index = 0; index < chunk; ++index) {
			values[index] = DisplayGamma::toPWM(brightness[offset + index], effectiveMaximumPWM());
		}

		if (!write(firstAddress + offset, values, chunk)) {
			return false;
		}

		for (uint8_t index = 0; index < chunk; ++index) {
			logicalBrightness_[firstAddress + offset + index] = brightness[offset + index];
		}
	}

	return true;
}

// Keep configuration separate from temporary diagnostic overrides.
bool MatrixDriver::setConfiguredCurrent(uint8_t globalCurrent) {
	if (ready_ && !setGlobalCurrent(globalCurrent)) {
		return false;
	}
	configuredCurrent_ = globalCurrent;
	if (!ready_) {
		globalCurrent_ = globalCurrent;
	}
	return true;
}

// Set peak-current scaling to globalCurrent (0-255), without changing configuration bits.
bool MatrixDriver::setGlobalCurrent(uint8_t globalCurrent) {
	if (!ready_) {
		return false;
	}

	if (!page(3) || !writeByte(0x01, globalCurrent)) {
		return false;
	}

	globalCurrent_ = globalCurrent;
	return true;
}

// Preserve logical levels so changing the ceiling never applies gamma twice.
bool MatrixDriver::setBrightnessLimit(uint8_t maximumPWM) {
	if (brightnessLimit_ == maximumPWM) {
		return true;
	}

	brightnessLimit_ = maximumPWM;

	if (!ready_) {
		return true;
	}

	return writeBrightness(0, logicalBrightness_, sizeof(logicalBrightness_));
}
