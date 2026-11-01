/*
 * ===========================================================================
 * File:      TimeMachine/fonts/SevenSegmentNumeric.h
 * Firmware:  0.5.1
 * Git base:  1e4e91f4dbce
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

#include <stdint.h>

// Logical segment order, independent of any driver's channel addresses.
//
//	 ___A___
//	|       |
//	F       B
//	|___G___|
//	|       |
//	E       C
//	|___D___|
//
namespace SevenSegment {
	constexpr uint8_t A = uint8_t(1) << 0;
	constexpr uint8_t B = uint8_t(1) << 1;
	constexpr uint8_t C = uint8_t(1) << 2;
	constexpr uint8_t D = uint8_t(1) << 3;
	constexpr uint8_t E = uint8_t(1) << 4;
	constexpr uint8_t F = uint8_t(1) << 5;
	constexpr uint8_t G = uint8_t(1) << 6;

	namespace Fonts {
		// Digits 0-9.
		constexpr uint8_t Numeric[10] = {
			A | B | C | D | E | F,          // 0
			B | C,                          // 1
			A | B | D | E | G,              // 2
			A | B | C | D | G,              // 3
			B | C | F | G,                  // 4
			A | C | D | F | G,              // 5
			    C | D | E | F | G,          // 6
			A | B | C,                      // 7
			A | B | C | D | E | F | G,      // 8
			A | B | C |     F | G,          // 9
		};

		// Letters and symbols. Space and unsupported characters are blank.
		constexpr uint8_t glyph(char character) {
			if (character >= '0' && character <= '9') {
				return Numeric[character - '0'];
			}

			switch (character) {
				// --- Letters (uppercase form) ---
				case 'A':           return A | B | C |     E | F | G;
				case 'C':           return A |         D | E | F;
				case 'E':           return A |         D | E | F | G;
				case 'F':           return A |             E | F | G;
				case 'G':           return A |     C | D | E | F;
				case 'H':           return     B | C |     E | F | G;
				case 'I':           return                 E | F;
				case 'J':           return     B | C | D | E;
				case 'L':           return             D | E | F;
				case 'O':           return A | B | C | D | E | F;
				case 'P':           return A | B |         E | F | G;
				case 'S':           return A |     C | D |     F | G;
				case 'U':           return     B | C | D | E | F;
				case 'Y':           return     B | C | D |     F | G;
				case 'Z':           return A | B |     D | E |     G;

				// --- Letters (lowercase form, where it differs) ---
				case 'a':           return A | B | C | D | E |     G;
				case 'b': case 'B': return         C | D | E | F | G;
				case 'c':           return             D | E |     G;
				case 'd': case 'D': return     B | C | D | E |     G;
				case 'e':           return A | B |     D | E | F | G;
				case 'g':           return A | B | C | D |     F | G;
				case 'h':           return         C |     E | F | G;
				case 'i':           return         C;
				case 'n': case 'N': return         C |     E |     G;
				case 'o':           return         C | D | E |     G;
				case 'q':           return A | B | C |         F | G;
				case 'r':           return                 E |     G;
				case 't':           return             D | E | F | G;
				case 'u':           return         C | D | E;

				// Lowercase letters that look the same as their uppercase form
				case 'f':           return glyph('F');
				case 'j':           return glyph('J');
				case 'l':           return glyph('L');
				case 'p':           return glyph('P');
				case 's':           return glyph('S');
				case 'y':           return glyph('Y');
				case 'z':           return glyph('Z');

				// --- Symbols ---
				case '_':           return             D;
				case '-':           return                         G;
				case '=':           return             D |         G;
				case '"':           return     B |             F;
				case '\'':          return                     F;
				case '`':           return     B;
				case '^':           return A;
				case '*':           return A | B |             F | G;  // degree sign
				case '[': case '(': return A |         D | E | F;
				case ']': case ')': return A | B | C | D;
				case '?':           return A | B |         E |     G;
				case '|':           return         C |     E | F;      // wide bar (both left/right-ish)
				case ',':           return         C;
				case '.':           return             D;

				case ' ':
				default:            return 0;
			}
		}
	}
}
