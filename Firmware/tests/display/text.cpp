/*
 * ===========================================================================
 * File:      tests/display/text.cpp
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

#include "Display.h"
#include "DisplayGamma.h"
#include "fonts/SixteenSegmentAlphanumeric.h"
#include "fonts/SevenSegmentNumeric.h"
#include <cassert>
#include <array>
#include <memory>
Stream Serial;int enableLevel=0;
uint32_t millis(){return 0;}
using Snapshot=std::array<uint8_t,DisplayMap::ChannelSpan>;
Snapshot snapshot(MatrixDriver& matrix){Snapshot result{};for(size_t i=0;i<result.size();++i)result[i]=matrix.brightness(i);return result;}
void expectDigit(MatrixDriver& matrix,int index,char character,uint8_t brightness=255){auto digit=DisplayMap::Digits[index];uint16_t mask=digit.segmentCount==16?SixteenSegment::Fonts::glyph(character):SevenSegment::Fonts::glyph(character);for(int segment=0;segment<digit.segmentCount;++segment)assert(matrix.brightness(digit.firstAddress+segment)==((mask&(1<<segment))?brightness:0));}
int main(){
 TwoWire wire;MatrixDriver matrix(wire,21,8);Display display(matrix);
 assert(!display.showText("ABC"));assert(display.begin()&&matrix.globalCurrent()==8);
 // Correct populated-channel enable map, including all four discretes.
 for(int address=0;address<192;++address){bool expected=false;for(auto d:DisplayMap::Digits)if(address>=d.firstAddress&&address<d.firstAddress+d.segmentCount)expected=true;for(const auto& discrete:DisplayMap::DiscreteMap)if(address==discrete.address)expected=true;assert(bool(wire.regs[0][address/8]&(1<<(address%8)))==expected);}
 Display::Frame composed;int calls=wire.calls;
 display.drawText(composed,"OCT2520261200");display.setIndicator(composed,Display::Indicator::PM,92);display.setIndicator(composed,Display::Indicator::Colon1,33);assert(wire.calls==calls);
 assert(display.show(composed));auto original=snapshot(matrix);
 assert(display.showAlpha("RST"));for(int d=0;d<3;++d)expectDigit(matrix,d,"RST"[d]);for(int i=0;i<0x50;++i)assert(matrix.brightness(i)==original[i]);
 assert(display.showAlpha("r"));expectDigit(matrix,0,'r');expectDigit(matrix,1,' ');expectDigit(matrix,2,' ');for(int i=0;i<0x50;++i)assert(matrix.brightness(i)==original[i]);
 assert(display.showText("ABC0123456789"));auto full=snapshot(matrix);assert(display.showText("ABC0123456789ignored"));assert(snapshot(matrix)==full);
 assert(display.showAlpha("RSTignored"));auto alpha=snapshot(matrix);assert(display.showAlpha("RST"));assert(snapshot(matrix)==alpha);
 // A heap string of only two bytes catches accidental indexing past its terminator.
 auto shortText=std::make_unique<char[]>(2);shortText[0]='A';shortText[1]=0;assert(display.showText(shortText.get()));expectDigit(matrix,0,'A');for(int d=1;d<13;++d)expectDigit(matrix,d,' ');
 assert(matrix.brightness(DisplayMap::discreteAddress(DisplayMap::DiscreteIndex::PM))==92&&matrix.brightness(DisplayMap::discreteAddress(DisplayMap::DiscreteIndex::Colon1))==33);
 assert(display.showText(nullptr));for(int d=0;d<13;++d)expectDigit(matrix,d,' ');assert(matrix.brightness(DisplayMap::discreteAddress(DisplayMap::DiscreteIndex::PM))==92);
 // Expanded sixteen-segment font preserves lowercase forms and renders symbols.
 static_assert(SixteenSegment::Fonts::glyph('a')==(SixteenSegment::G1|SixteenSegment::E|SixteenSegment::BOTTOM|SixteenSegment::L));
 static_assert(SixteenSegment::Fonts::glyph('r')==(SixteenSegment::E|SixteenSegment::G1));
 static_assert(SixteenSegment::Fonts::glyph('f')==SixteenSegment::Fonts::glyph('F'));
 static_assert(SixteenSegment::Fonts::glyph('+')==(SixteenSegment::MID|SixteenSegment::J|SixteenSegment::M));
 static_assert(SixteenSegment::Fonts::glyph('\\')==(SixteenSegment::H|SixteenSegment::L));
 static_assert(SixteenSegment::Fonts::glyph('{')==0);
 assert(display.showText("a+?"));for(int d=0;d<3;++d)expectDigit(matrix,d,"a+?"[d]);
 assert(display.showText("{}:"));for(int d=0;d<13;++d)expectDigit(matrix,d,' ');
 // User's seven-segment font includes letters and symbols; unsupported glyphs stay blank.
 using namespace SevenSegment;
 static_assert(Fonts::glyph('6')==(C|D|E|F|G));
 static_assert(Fonts::glyph('9')==(A|B|C|F|G));
 static_assert(Fonts::glyph('A')==(A|B|C|E|F|G));
 static_assert(Fonts::glyph('b')==(C|D|E|F|G));
 static_assert(Fonts::glyph('-')==G);
 static_assert(Fonts::glyph('*')==(A|B|F|G));
 static_assert(Fonts::glyph('x')==0&&Fonts::glyph('@')==0&&Fonts::glyph(' ')==0);
 assert(display.showText("RSTabcdefghij"));for(int d=3;d<13;++d)expectDigit(matrix,d,"abcdefghij"[d-3]);
 assert(display.showText("TXTAbCd-_*=90"));for(int d=3;d<13;++d)expectDigit(matrix,d,"AbCd-_*=90"[d-3]);
 assert(matrix.setBrightnessLimit(90));assert(display.showAlpha("RST",73));for(int d=0;d<3;++d)expectDigit(matrix,d,"RST"[d],73);
 auto digit=DisplayMap::Digits[0];auto mask=SixteenSegment::Fonts::glyph('R');for(int segment=0;segment<16;++segment)assert(wire.regs[1][digit.firstAddress+segment]==((mask&(1<<segment))?DisplayGamma::toPWM(73,matrix.effectiveMaximumPWM()):0));
 // Snapshots include raw diagnostic writes; text never applies gamma a second time.
 assert(matrix.setBrightness(DisplayMap::discreteAddress(DisplayMap::DiscreteIndex::AM),41));auto current=display.frame();assert(current.channels[DisplayMap::discreteAddress(DisplayMap::DiscreteIndex::AM)]==41);assert(display.showAlpha(nullptr));assert(matrix.brightness(DisplayMap::discreteAddress(DisplayMap::DiscreteIndex::AM))==41);
 assert(display.clear());for(auto value:snapshot(matrix))assert(value==0);
 wire.failAt=wire.calls+1;assert(!display.showText("ERR"));assert(!matrix.ready()&&enableLevel==LOW);
 puts("PASS: shared layout/font selection, channel map, bounded short/null strings, padding/truncation, alpha-only replacement, indicator/frame preservation, logical brightness/gamma, full clear, and I2C failure propagation (ASan/UBSan).");
}
