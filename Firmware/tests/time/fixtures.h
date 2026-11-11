/*
 * ===========================================================================
 * File:      tests/time/fixtures.h
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

struct Fixture {time_t utc;int year,month,day,hour,minute,second,dst;};
const Fixture fixtures[]={
{1772953199,2026,3,8,1,59,59,0},
{1772953200,2026,3,8,3,0,0,1},
{1793512799,2026,11,1,1,59,59,1},
{1793512800,2026,11,1,1,0,0,0},
{1792900799,2026,10,24,23,59,59,1},
{1792900800,2026,10,25,0,0,0,1},
{1792944000,2026,10,25,12,0,0,1},
{1798761600,2026,12,31,19,0,0,0},
{1835499599,2028,2,29,23,59,59,0},
{1835499600,2028,3,1,0,0,0,0}
};
