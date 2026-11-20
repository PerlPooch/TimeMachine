/*
 * ===========================================================================
 * File:      tests/application/host/ArduinoString.h
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
#include <iomanip>
#include <sstream>
#include <string>
#include <strings.h>

class String : public std::string {
public:
 using std::string::string;
 using std::string::operator=;

 // Start with an empty Arduino-style string.
 String() = default;

 // Assign a possibly null pointer from an Arduino API.
 String& operator=(const char* text) {
  assign(text ? text : "");
  return *this;
 }

 // Adapt standard-library strings used by the host mocks.
 String(const std::string& text) : std::string(text) {}

 // Format decimal values with the requested precision.
 String(double value, int precision) {
  std::ostringstream out;
  out << std::fixed << std::setprecision(precision) << value;
  assign(out.str());
 }

 // Append a byte for APIs that print into Strings.
 size_t write(uint8_t value) {
  push_back(char(value));
  return 1;
 }

 // Append a buffer for APIs that print into Strings.
 size_t write(const uint8_t* values, size_t count) {
  append(reinterpret_cast<const char*>(values), count);
  return count;
 }

 // Return Arduino's sentinel when the separator does not occur.
 int indexOf(char character, size_t offset = 0) const {
  const auto index = find(character, offset);
  return index == npos ? -1 : static_cast<int>(index);
 }

 // Return a half-open substring for the upload path checks.
 String substring(size_t start, size_t end = npos) const {
  return substr(start, end - start);
 }

 // Match the supplied prefix from the start of the string.
 bool startsWith(const char* prefix) const {
  return find(prefix) == 0;
 }

 // Match the supplied suffix at the end of the string.
 bool endsWith(const char* suffix) const {
  const std::string text(suffix);
  return size() >= text.size() && compare(size() - text.size(), text.size(), text) == 0;
 }

 // Discard the suffix beginning at the supplied index.
 void remove(size_t start) {
  erase(start);
 }

 // Append a path fragment without simulating allocation failure.
 bool concat(const String& text) {
  append(text);
  return true;
 }

 // Append an individual path separator.
 bool concat(char character) {
  push_back(character);
  return true;
 }

 // Compare reserved filenames case-insensitively.
 bool equalsIgnoreCase(const char* text) const {
  return strcasecmp(c_str(), text) == 0;
 }
};
