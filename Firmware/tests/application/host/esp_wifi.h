/*
 * ===========================================================================
 * File:      tests/application/host/esp_wifi.h
 * Firmware:  0.7.0
 * Git base:  b4e2060f2874
 *
 * Copyright (c) 2026 Marc D. Spencer.
 * All Rights Reserved under the applicable license.
 * SPDX-License-Identifier: MPL-2.0
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 * ===========================================================================
 */

#pragma once

#include <cstdint>
#include <cstring>

constexpr int ESP_OK = 0;
constexpr int WIFI_IF_STA = 0;

struct wifi_config_t {
	struct {
		uint8_t ssid[32] = {};
		uint8_t password[64] = {};
	} sta;
};

inline wifi_config_t savedWiFi;
inline bool readableWiFi = true;

// Read the saved configuration even when the fake station is offline.
inline int esp_wifi_get_config(int, wifi_config_t* config) {
	if (!readableWiFi) return -1;
	*config = savedWiFi;
	return ESP_OK;
}
