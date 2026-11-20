/*
 * ===========================================================================
 * File:      tests/time/host/Wire.h
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
#include <cassert>
class TwoWire {
public:
	uint8_t regs[4][256]{};
	int selected=0, calls=0, failAt=-1;
	bool unlocked=false;
	std::vector<uint8_t> tx;
	void beginTransmission(uint8_t address){ assert(address==0x50); tx.clear(); }
	size_t write(uint8_t b){tx.push_back(b);return 1;}
	size_t write(const uint8_t* data,size_t n){tx.insert(tx.end(),data,data+n);return n;}
	uint8_t endTransmission(){
		if (++calls==failAt)return 2;
		assert(tx.size()>=2 && tx.size()<=17);
		if(tx[0]==0xFE){assert(tx[1]==0xC5);unlocked=true;}
		else if(tx[0]==0xFD){assert(unlocked && tx[1]<4);selected=tx[1];unlocked=false;}
		else for(size_t i=1;i<tx.size();i++)regs[selected][tx[0]+i-1]=tx[i];
		return 0;
	}
};
