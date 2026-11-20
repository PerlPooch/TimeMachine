/*
 * ===========================================================================
 * File:      tests/time/time.cpp
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

#include <MDS_Time.h>
#include <esp_sntp.h>
#include <cassert>
#include <vector>
#include "fixtures.h"
FakeSNTP sntp;
Stream Serial;
int enableLevel=0;
time_t utc=0;
time_t fakeTime(time_t* result){if(result)*result=utc;return utc;}
void notify(time_t value){utc=value;timeval event{};event.tv_sec=value;assert(sntp.callback);sntp.callback(&event);}
int main(){
 bool networkReady = false;
 MDS_Time clock;
 clock.setNetworkReadyCallback([&]() {
     // Expose each scenario's current connection state.
     return networkReady;
 });MDS_Time::Options options;std::vector<String> messages;
 clock.setStatusCallback([&](const String& s){messages.push_back(s);});
 assert(clock.begin(options));assert(!clock.valid());struct tm local;assert(!clock.localTime(local));
 networkReady = false;
 clock.tick();
 assert(sntp.starts==0);networkReady = true;
 clock.tick();
 assert(sntp.starts==1&&sntp.interval==3600000&&sntp.servers[0]=="pool.ntp.org");
 auto before=messages.size();notify(fixtures[0].utc);assert(messages.size()==before&&!clock.valid());networkReady = true;
 clock.tick();
 assert(clock.valid()&&clock.lastSyncUtc()==utc);
 for(const auto& fixture:fixtures){
  utc=fixture.utc;assert(clock.localTime(local));
  assert(local.tm_year+1900==fixture.year&&local.tm_mon+1==fixture.month&&local.tm_mday==fixture.day);
  assert(local.tm_hour==fixture.hour&&local.tm_min==fixture.minute&&local.tm_sec==fixture.second&&local.tm_isdst==fixture.dst);
 }
 auto sync=clock.lastSyncUtc();networkReady = false;
 clock.tick();
 utc+=3600;networkReady = false;
 clock.tick();
 assert(clock.valid()&&clock.lastSyncUtc()==sync&&clock.utcNow()==utc);
 int starts=sntp.starts;networkReady = true;
 clock.tick();
 assert(sntp.starts==starts+1);
 uint32_t version=clock.configurationVersion();assert(clock.begin(options));assert(clock.configurationVersion()==version);
 options.timeZone="EST5";assert(clock.begin(options));assert(clock.configurationVersion()==version+1);utc=1784552400;assert(clock.localTime(local));struct tm utcParts;gmtime_r(&utc,&utcParts);assert(local.tm_hour==(utcParts.tm_hour+19)%24&&local.tm_isdst==0);
 options.timeZone="UTC0";assert(clock.begin(options));assert(clock.localTime(local));assert(local.tm_hour==utcParts.tm_hour&&local.tm_mday==utcParts.tm_mday);
 options.intervalSeconds=1;assert(!clock.begin(options));assert(String(clock.timeZone())=="UTC0"&&clock.valid());options.intervalSeconds=3600;
 options.server1="custom.pool.example";options.server2="";assert(clock.begin(options));networkReady = true;
 clock.tick();
 assert(sntp.servers[0]=="custom.pool.example"&&sntp.servers[1].empty());
 notify(0);networkReady = true;
 clock.tick();
 assert(!clock.valid());notify(fixtures[0].utc);networkReady = true;
 clock.tick();
 assert(clock.valid());
 // Existing four-field aggregate initializers retain POSIX mode by default.
 MDS_Time::Options legacy{"UTC0", "pool.ntp.org", "", 3600};
 assert(legacy.timeZoneMode == MDS_Time::TimeZoneMode::Posix);
 assert(legacy.utcOffsetSeconds == 0);
 assert(clock.begin(legacy));

 // Fixed offsets use OpenWeather's sign convention, including fractional hours.
 options = legacy;
 options.timeZoneMode = MDS_Time::TimeZoneMode::FixedOffset;
 options.timeZone = nullptr;  // Ignored in fixed-offset mode.
 const int32_t offsets[] = {0, -14400, 19800, 20700, -12601, -86400, 86400};

 for (const int32_t offset : offsets) {
  options.utcOffsetSeconds = offset;
  assert(clock.begin(options));

  for (const auto& fixture : fixtures) {
   utc = fixture.utc;
   const time_t shifted = utc + offset;
   struct tm expected;
   gmtime_r(&shifted, &expected);
   assert(clock.localTime(local));
   assert(local.tm_year == expected.tm_year && local.tm_mon == expected.tm_mon);
   assert(local.tm_mday == expected.tm_mday && local.tm_hour == expected.tm_hour);
   assert(local.tm_min == expected.tm_min && local.tm_sec == expected.tm_sec);
   assert(local.tm_isdst == 0 && clock.utcNow() == utc);
  }
 }

 version = clock.configurationVersion();
 starts = sntp.starts;
 assert(clock.begin(options));
 networkReady = true;
 clock.tick();
 
 assert(clock.configurationVersion() == version && sntp.starts == starts + 1);
 assert(clock.begin(options));
 networkReady = true;
 clock.tick();
 
 assert(sntp.starts == starts + 1);  // Unchanged options do not restart SNTP.

 const String previousZone = clock.timeZone();
 for (const int32_t offset : {int32_t(-86401), int32_t(86401), INT32_MIN, INT32_MAX}) {
  options.utcOffsetSeconds = offset;
  assert(!clock.begin(options));
  assert(String(clock.timeZone()) == previousZone);
  assert(clock.configurationVersion() == version && clock.valid());
 }

 options.timeZoneMode = static_cast<MDS_Time::TimeZoneMode>(99);
 assert(!clock.begin(options));
 assert(String(clock.timeZone()) == previousZone);

 // Switching back restores the original DST rules; unused offsets are ignored.
 options.timeZoneMode = MDS_Time::TimeZoneMode::Posix;
 options.timeZone = MDS_Time::DefaultTimeZone;
 assert(clock.begin(options));
 for (const auto& fixture : fixtures) {
  utc = fixture.utc;
  assert(clock.localTime(local));
  assert(local.tm_hour == fixture.hour && local.tm_isdst == fixture.dst);
 }
 options.timeZone = nullptr;
 assert(!clock.begin(options));
 assert(String(clock.timeZone()) == MDS_Time::DefaultTimeZone);
 puts("PASS: background SNTP lifecycle, callback deferral, sync validity, UTC/fixed timezone, DST transitions, midnight/leap/year dates, option changes, and offline continuity.");
}
