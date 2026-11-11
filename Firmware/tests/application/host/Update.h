/*
 * ===========================================================================
 * File:      tests/application/host/Update.h
 * Firmware:  0.6.0
 * Git base:  4967da86d1b1
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

#include <cstddef>
#include <cstdint>

constexpr size_t UPDATE_SIZE_UNKNOWN = size_t(-1);

struct FakeUpdate {
 bool beginOK = true;
 bool writeOK = true;
 bool endOK = true;
 bool active = false;
 int commits = 0;
 int aborts = 0;

 // Begin a simulated write to the inactive firmware slot.
 bool begin(size_t) {
  active = beginOK;
  return beginOK;
 }

 // Model a full chunk write or a short write failure.
 size_t write(uint8_t*, size_t size) {
  return writeOK ? size : 0;
 }

 // Only a successfully validated image becomes a committed firmware update.
 bool end(bool) {
  active = false;
  if (endOK) {
   ++commits;
  }
  return endOK;
 }

 // Discard a partial firmware image.
 void abort() {
  active = false;
  ++aborts;
 }
};

inline FakeUpdate Update;
