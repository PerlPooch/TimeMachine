#pragma once

#include <stdint.h>

// Logical segment order, independent of any driver's channel addresses.
//
//	   ___A1___ ___A2___
//	  |  \     |     /  |
//	F |   H    J    K   | B
//	  |    \   |   /    |
//	    __G1__ | __G2__
//	  |    /   |   \    |
//	E |   N    M    L   | C
//	  |  /     |     \  |
//	   ___D1___ ___D2___
//
namespace SixteenSegment {
	constexpr uint16_t A1 = uint16_t(1) <<  0;
	constexpr uint16_t A2 = uint16_t(1) <<  1;
	constexpr uint16_t B  = uint16_t(1) <<  2;
	constexpr uint16_t C  = uint16_t(1) <<  3;
	constexpr uint16_t D2 = uint16_t(1) <<  4;
	constexpr uint16_t D1 = uint16_t(1) <<  5;
	constexpr uint16_t E  = uint16_t(1) <<  6;
	constexpr uint16_t F  = uint16_t(1) <<  7;
	constexpr uint16_t G1 = uint16_t(1) <<  8;
	constexpr uint16_t G2 = uint16_t(1) <<  9;
	constexpr uint16_t H  = uint16_t(1) << 10;
	constexpr uint16_t J  = uint16_t(1) << 11;
	constexpr uint16_t K  = uint16_t(1) << 12;
	constexpr uint16_t L  = uint16_t(1) << 13;
	constexpr uint16_t M  = uint16_t(1) << 14;
	constexpr uint16_t N  = uint16_t(1) << 15;

	// Segment groups that light together in most glyphs.
	constexpr uint16_t TOP    = A1 | A2;
	constexpr uint16_t MID    = G1 | G2;
	constexpr uint16_t BOTTOM = D1 | D2;

	namespace Fonts {
		constexpr uint16_t Letters[26] = {
			TOP | B | C | E | F | MID,                  // A
			TOP | B | C | BOTTOM | G2 | J | M,          // B
			TOP | BOTTOM | E | F,                       // C
			TOP | B | C | BOTTOM | J | M,               // D
			TOP | BOTTOM | E | F | MID,                 // E
			TOP | E | F | MID,                          // F
			TOP | C | BOTTOM | E | F | G2,              // G
			B | C | E | F | MID,                        // H
			TOP | BOTTOM | J | M,                       // I
			B | C | BOTTOM | E,                         // J
			E | F | G1 | K | L,                         // K
			BOTTOM | E | F,                             // L
			B | C | E | F | H | K,                      // M
			B | C | E | F | H | L,                      // N
			TOP | B | C | BOTTOM | E | F,               // O
			TOP | B | E | F | MID,                      // P
			TOP | B | C | BOTTOM | E | F | L,           // Q
			TOP | B | E | F | MID | L,                  // R
			TOP | C | BOTTOM | F | MID,                 // S
			TOP | J | M,                                // T
			B | C | BOTTOM | E | F,                     // U
			E | F | K | N,                              // V
			B | C | E | F | L | N,                      // W
			H | K | L | N,                              // X
			H | K | M,                                  // Y
			TOP | BOTTOM | K | N,                       // Z
		};

		constexpr uint16_t Digits[10] = {
			TOP | B | C | BOTTOM | E | F | K | N,       // 0
			B | C,                                      // 1
			TOP | B | BOTTOM | E | MID,                 // 2
			TOP | B | C | BOTTOM | MID,                 // 3
			B | C | F | MID,                            // 4
			TOP | BOTTOM | F | G1 | L,                  // 5
			TOP | C | BOTTOM | E | F | MID,             // 6
			TOP | B | C,                                // 7
			TOP | B | C | BOTTOM | E | F | MID,         // 8
			TOP | B | C | BOTTOM | F | MID,             // 9
		};

		// Distinct lowercase forms. Returns 0 if a letter has no special
		// lowercase glyph, in which case glyph() folds it to uppercase.
		constexpr uint16_t lowercase(char c) {
			switch (c) {
				case 'a': return G1 | E | D1 | D2 | L;
				case 'b': return F | E | G1 | G2 | C | D1 | D2;
				case 'c': return G1 | E | D1 | G2 | D2;
				case 'd': return B | C | G1 | G2 | E | D1 | D2;
				case 'e': return A1 | A2 | B | F | E | G1 | G2 | D1 | D2;
				case 'g': return A1 | A2 | B | C | F | G1 | G2 | D1 | D2;
				case 'h': return F | E | G1 | G2 | C;
				case 'i': return M;
				case 'j': return B | C | D1 | D2;
				case 'l': return J | M;
				case 'm': return E | G1 | G2 | C | M;
				case 'n': return E | G1 | G2 | C;
				case 'o': return E | G1 | G2 | C | D1 | D2;
				case 'q': return A1 | A2 | B | C | F | G1 | G2;
				case 'r': return E | G1;
				case 't': return F | E | G1 | G2 | D1 | D2;
				case 'u': return E | D1 | D2 | C;
				case 'v': return N | L;
				case 'w': return E | C | M | D1 | D2;
				case 'y': return F | B | C | G1 | G2 | D1 | D2;
				default:  return 0;
			}
		}

		constexpr uint16_t glyph(char character) {
			if (character >= 'a' && character <= 'z') {
				const uint16_t lower = lowercase(character);
				if (lower != 0) {
					return lower;
				}
				character -= 'a' - 'A';
			}

			if (character >= 'A' && character <= 'Z') {
				return Letters[character - 'A'];
			}

			if (character >= '0' && character <= '9') {
				return Digits[character - '0'];
			}

			switch (character) {
				// --- Lines and bars ---
				case '_':           return BOTTOM;
				case '-':           return MID;
				case '=':           return MID | BOTTOM;
				case '+':           return MID | J | M;
				case '|':           return J | M;
				case '/':           return K | N;
				case '\\':          return H | L;
				case '*':           return MID | J | M | H | K | L | N;

				// --- Brackets and arrows ---
				case '(': case '[': return TOP | BOTTOM | E | F;
				case ')': case ']': return TOP | BOTTOM | B | C;
				case '<':           return K | L;
				case '>':           return H | N;
				case '^':           return H | K;

				// --- Punctuation ---
				case '.':           return M;
				case ',':           return M | N;
				case '\'':          return J;
				case '`':           return H;
				case '"':           return J | B;
				case '!':           return J | M;
				case '?':           return TOP | B | G2 | M;

				// --- Misc symbols ---
				case '%':           return F | C | K | N;
				case '$':           return TOP | C | BOTTOM | F | MID | J | M;
				case '@':           return TOP | B | BOTTOM | E | F | G2 | J;
				case '#':           return B | C | MID | J | M;
				case '~':           return F | G1 | H | K | G2 | B;
				case '\xB0':        return A1 | F | G1 | J;

				case ' ':
				default:            return 0;
			}
		}
	}
}
