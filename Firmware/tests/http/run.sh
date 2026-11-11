#!/bin/sh
# ===========================================================================
# File:      tests/http/run.sh
# Firmware:  0.5.1
# Git base:  1e4e91f4dbce
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
for test in api fade offset routing; do
c++ -std=c++17 -Wall -Wextra -I"$TEST_DIR/host" -I"$MDS_LIBRARIES/MDS_Config/src" -I"$MDS_LIBRARIES/MDS_HTTP/src" -I"$TIME_MACHINE_SKETCH" -I"$ARDUINOJSON_SRC" "$TEST_DIR/$test.cpp" "$TIME_MACHINE_SKETCH/ApplicationApi.cpp" "$TIME_MACHINE_SKETCH/MatrixDriver.cpp" "$TIME_MACHINE_SKETCH/Display.cpp" "$MDS_LIBRARIES/MDS_HTTP/src/MDS_HTTP.cpp" "$MDS_LIBRARIES/MDS_Config/src/MDS_Config.cpp" -o "$TEST_BUILD/$test"
"$TEST_BUILD/$test"
done
c++ -std=c++17 -Wall -Wextra -I"$TEST_DIR/host" -I"$TIME_MACHINE_SKETCH" "$TEST_DIR/driver.cpp" "$TIME_MACHINE_SKETCH/MatrixDriver.cpp" "$TIME_MACHINE_SKETCH/Display.cpp" -o "$TEST_BUILD/driver"
"$TEST_BUILD/driver"
c++ -std=c++17 -Wall -Wextra -DDISPLAY_TEST_MODE=DisplayTest::Thermal -I"$TEST_DIR/host" -I"$TIME_MACHINE_SKETCH" "$TEST_DIR/thermal.cpp" "$TIME_MACHINE_SKETCH/Tests.cpp" "$TIME_MACHINE_SKETCH/MatrixDriver.cpp" "$TIME_MACHINE_SKETCH/Display.cpp" -o "$TEST_BUILD/thermal"
"$TEST_BUILD/thermal"
