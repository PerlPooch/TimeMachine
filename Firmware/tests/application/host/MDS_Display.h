/*
 * ===========================================================================
 * File:      tests/application/host/MDS_Display.h
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
#include <vector>
class MDS_Display {
public:
 std::vector<String> messages;
 int ticks=0;
 void D(const String& message){messages.push_back(message);}
 void tick(){++ticks;}
};
