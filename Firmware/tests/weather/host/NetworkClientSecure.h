/*
 * ===========================================================================
 * File:      tests/weather/host/NetworkClientSecure.h
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
#include "Arduino.h"
extern bool bundleUsed;
class NetworkClientSecure {
public:
 void useBuiltinCACertBundle(){bundleUsed=true;}
 void setHandshakeTimeout(int){}
};
