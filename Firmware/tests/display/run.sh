#!/bin/sh
# ===========================================================================
# File:      tests/display/run.sh
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
TIME_MACHINE_SKETCH=${TIME_MACHINE_SKETCH:-"$TEST_DIR/../../TimeMachine"}
TEST_BUILD=$(mktemp -d)
trap 'rm -rf "$TEST_BUILD"' EXIT HUP INT TERM
for test in text animation scroll appear sync queue colons; do
c++ -std=c++17 -Wall -Wextra -fsanitize=address,undefined -fno-omit-frame-pointer \
 -I"$TEST_DIR/../http/host" -I"$TIME_MACHINE_SKETCH" \
 "$TEST_DIR/$test.cpp" "$TIME_MACHINE_SKETCH/Display.cpp" "$TIME_MACHINE_SKETCH/MatrixDriver.cpp" -o "$TEST_BUILD/$test"
"$TEST_BUILD/$test"
done
c++ -std=c++17 -Wall -Wextra -fsanitize=address,undefined -fno-omit-frame-pointer \
 -DDISPLAY_TEST_MODE=DisplayTest::SegmentWalk \
 -I"$TEST_DIR/../application/host" -I"$TIME_MACHINE_SKETCH" \
 "$TEST_DIR/segmentwalk.cpp" "$TIME_MACHINE_SKETCH/Tests.cpp" \
 "$TIME_MACHINE_SKETCH/Display.cpp" "$TIME_MACHINE_SKETCH/MatrixDriver.cpp" -o "$TEST_BUILD/segmentwalk"
"$TEST_BUILD/segmentwalk"
