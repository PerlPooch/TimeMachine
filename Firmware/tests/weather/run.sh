#!/bin/sh
# ===========================================================================
# File:      tests/weather/run.sh
# Firmware:  0.6.0
# Git base:  4967da86d1b1
#
# Copyright (c) 2026 Marc D. Spencer.
# All Rights Reserved under the applicable license.
# SPDX-License-Identifier: MPL-2.0
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.
# ===========================================================================

set -eu
TEST_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
: "${ARDUINOJSON_SRC:?Set ARDUINOJSON_SRC to ArduinoJson 7 src}"
MDS_LIBRARIES=${MDS_LIBRARIES:-"$TEST_DIR/../../../../MDS-Libraries"}
TIME_MACHINE_SKETCH=${TIME_MACHINE_SKETCH:-"$TEST_DIR/../../TimeMachine"}
TEST_BUILD=$(mktemp -d)
trap 'rm -rf "$TEST_BUILD"' EXIT HUP INT TERM
c++ -std=c++17 -Wall -Wextra -I"$TEST_DIR/host" -I"$MDS_LIBRARIES/MDS_Config/src" -I"$MDS_LIBRARIES/MDS_HTTP/src" -I"$MDS_LIBRARIES/MDS_Weather/src" -I"$TIME_MACHINE_SKETCH" -I"$ARDUINOJSON_SRC" "$TEST_DIR/api.cpp" "$TIME_MACHINE_SKETCH/ApplicationApi.cpp" "$TIME_MACHINE_SKETCH/MatrixDriver.cpp" "$MDS_LIBRARIES/MDS_Config/src/MDS_Config.cpp" "$MDS_LIBRARIES/MDS_HTTP/src/MDS_HTTP.cpp" "$MDS_LIBRARIES/MDS_Weather/src/MDS_Weather.cpp" -o "$TEST_BUILD/api"
"$TEST_BUILD/api"
