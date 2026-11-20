/*
 * ===========================================================================
 * File:      tests/http/api.cpp
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

#include "ApplicationApi.h"
#include <LittleFS.h>
#include "DisplayGamma.h"
#include <cassert>
Stream Serial;
uint32_t millis(){return 0;}
int enableLevel=0;
FakeFS LittleFS;
JsonDocument json(const String& text){JsonDocument doc;assert(!deserializeJson(doc,static_cast<const std::string&>(text)));return doc;}
int main(){
 using Type=MDS_ConfigType;using Access=MDS_ConfigAccess;
 const MDS_ConfigSetting settings[]={
  {"schema",Type::Unsigned,Access::ReadOnly,true,nullptr,1,1,1,true},
  {"primary",Type::Boolean,Access::ReadWrite,true,nullptr,0,0,1},
  {"name",Type::Text,Access::ReadWrite,true,"Time Machine",0,1,32},
  {"rate",Type::Unsigned,Access::ReadWrite,true,nullptr,5,0,UINT32_MAX},
  {"runtime",Type::Unsigned,Access::ReadWrite,false,nullptr,0,0,100},
  {"password",Type::Text,Access::ReadWrite,true,"secret",0,1,64,false,true},
  {"pwm",Type::Unsigned,Access::ReadWrite,true,nullptr,255,0,255},
  {"gcc",Type::Unsigned,Access::ReadWrite,true,nullptr,4,0,255}
 };
 MDS_Config config;assert(config.setSettings(settings));assert(config.begin());
 TwoWire wire;MatrixDriver matrix(wire,21);uint8_t channels[]={0,1};assert(matrix.begin(4,channels,2));assert(matrix.setBrightness(0,255));assert(matrix.setBrightness(1,128));
 Display display(matrix);MDS_HTTP http;
 http.setNetworkReadyCallback([]() {
     // Supply a connected network for direct API lifecycle tests.
     return true;
 });
 ApplicationApi api(http,config,matrix,display.commands());auto& server=http.server();
 assert(server.port==80&&!http.running());http.tick();assert(server.ticks==0);
 api.begin();api.begin();assert(server.registrations==7);
 // The exact home route overrides the static directory handler's default index.
 assert(server.handlers.count({"/", HTTP_GET}) == 1);
 assert(server.handlers.count({"/upload-file", HTTP_GET}) == 1);
 assert(server.handlers.count({"/upload-file", HTTP_POST}) == 1);
 assert(server.staticRoutes.size() == 2);
 assert(server.staticRoutes[0].uri == "/assets/" && server.staticRoutes[0].path == "/www/assets/");
 assert(server.staticRoutes[1].uri == "/" && server.staticRoutes[1].path == "/www/");
 for (const auto& route : server.staticRoutes) {
  assert(route.filesystem == &LittleFS);
 }
 http.begin();http.begin();assert(http.running()&&server.starts==1);http.tick();assert(server.ticks==1);
 server.request("/api/config");assert(server.headers["Cache-Control"]=="no-store");assert(server.code==200&&server.contentType=="application/json");auto initial=json(server.body);assert(initial["schema"].as<int>()==1&&initial.size()==7);assert(initial["password"].isNull());assert(initial["gcc"].as<int>()==4&&initial["pwm"].as<int>()==255);assert(!config.exists());
 // Display actions never save config; defaults apply separately to every request.
 Display::DisplayCommand command;
 assert(!display.commands().take(command));
 // Routing defaults, repeated destinations, strict validation, and wire round trips.
 uint8_t routed=0;bool acceptRoute=true;int routeCalls=0;
 api.setDisplayRouter([&](const Display::DisplayCommand& cmd){++routeCalls;routed=cmd.destinations;return acceptRoute;});
 server.request("/api/display",{{"message","ABC"},{"to","a"},{"to","b"},{"to","a"}});
 assert(server.code==202&&routed==(Display::PortA|Display::PortB)&&!display.commands().take(command)&&routeCalls==1);
 server.request("/api/display",{{"message","ABC"},{"to","local"},{"to","a"}});
 assert(server.code==202&&display.commands().take(command)&&routed==(Display::Local|Display::PortA));
 char encoded[247];auto wireLength=ApplicationApi::encodeDisplayCommand(command,encoded,sizeof(encoded));assert(wireLength);
 assert(api.receiveDisplayCommand(reinterpret_cast<const uint8_t*>(encoded),wireLength));
 assert(display.commands().take(command)&&command.destinations==Display::Local&&command.text=="ABC");assert(routeCalls==2);
 for(const char* target:{"","A","a,b","ALL","unknown"}){server.request("/api/display",{{"message","ABC"},{"to",target}});assert(server.code==400&&!display.commands().take(command)&&routeCalls==2);}

 // All selects local and both ports for every action, without duplicating delivery.
 for(const char* action:{"message","scroll","slide","appear","fade"}){
  const int before=routeCalls;
  server.request("/api/display",{{action,strcmp(action,"fade")==0?"64":"ABC"},{"to","all"},{"to","a"},{"to","all"},{"to","local"}});
  assert(server.code==202&&routeCalls==before+1);
  assert(routed==(Display::Local|Display::PortA|Display::PortB));
  assert(display.commands().take(command)&&command.destinations==routed);
  assert(!display.commands().take(command));
 }

 acceptRoute=false;
 server.request("/api/display",{{"message","FAIL"},{"to","all"}});assert(server.code==503&&!display.commands().take(command));acceptRoute=true;
 server.request("/api/display",{{"scroll",String(512,'x')},{"to","all"}});assert(server.code==413&&!display.commands().take(command));
 server.request("/api/display",{{"scroll",String(512,'x')},{"to","a"}});assert(server.code==413);
 server.request("/api/display",{{"scroll",String(512,'x')}});assert(server.code==202&&display.commands().take(command)&&command.text.length()==512&&command.destinations==Display::Local);
 // Size checks include JSON escaping, not just raw text length.
 server.request("/api/display",{{"scroll",String(120,'"')},{"to","b"}});assert(server.code==413);
 for(const char* action:{"scroll","slide","appear","message"}){
  server.request("/api/display",{{action,"A\"B\\C"},{"to","a"}});assert(server.code==202&&!display.commands().take(command));
 }
 for(const char* bad:{"[]","[\"display1\",\"message\",\"ABC\",0,\"left\"]","[\"display1\",\"bad\",\"ABC\",150,\"left\"]","[\"display1\",\"message\",\"ABC\",150,\"up\"]","[\"display1\",\"message\",\"ABC\",150,\"left\",\"a\"]"}){
  assert(!api.receiveDisplayCommand(reinterpret_cast<const uint8_t*>(bad),strlen(bad))&&!display.commands().take(command));
 }

 server.request("/api/display",{{"scroll","12345678901234567890"}});assert(server.code==202&&!config.exists());
 assert(display.commands().take(command)&&command.kind==Display::DisplayCommand::Kind::Scroll&&command.speed==150&&command.direction==Display::Direction::Left&&command.text=="12345678901234567890");assert(!display.commands().take(command));
 server.request("/api/display",{{"speed","7"},{"direction","right"},{"scroll","123"}});assert(server.code==202);
 assert(display.commands().take(command)&&command.speed==7&&command.direction==Display::Direction::Right);
 server.request("/api/display",{{"message","ABC"}});assert(server.code==202&&display.commands().take(command)&&command.kind==Display::DisplayCommand::Kind::Message&&command.text=="ABC");
 server.request("/api/display",{{"scroll","again"}});server.request("/api/display",{{"scroll","again"}});assert(server.code==202&&display.commands().take(command)&&command.speed==150&&command.direction==Display::Direction::Left);
 server.request("/api/display",{{"slide","123"}});assert(server.code==202&&display.commands().take(command)&&command.kind==Display::DisplayCommand::Kind::Slide&&command.speed==150&&command.direction==Display::Direction::Left);
 server.request("/api/display",{{"direction","right"},{"speed","20"},{"slide","123"}});assert(server.code==202&&display.commands().take(command)&&command.speed==20&&command.direction==Display::Direction::Right);
 server.request("/api/display",{{"slide","12345678901234"}});assert(server.code==400&&!display.commands().take(command));
 server.request("/api/display",{{"slide","123"},{"scroll","456"}});assert(server.code==400&&!display.commands().take(command));
 server.request("/api/display",{{"slide","123"},{"direction","up"}});assert(server.code==400&&!display.commands().take(command));
 server.request("/api/config",{{"slide","123"}});assert(server.code==400);
 for(const char* speed:{"0","60001","-1","1.5","abc","4294967296",""}){server.request("/api/display",{{"scroll","bad"},{"speed",speed}});assert(server.code==400&&!display.commands().take(command));}
 for(const char* direction:{"LEFT","up",""}){server.request("/api/display",{{"scroll","bad"},{"direction",direction}});assert(server.code==400&&!display.commands().take(command));}
 server.request("/api/display");assert(server.code==400);
 server.request("/api/display",{{"direction","left"}});assert(server.code==400);
 server.request("/api/display",{{"message","ABC"},{"speed","10"}});assert(server.code==400);
 server.request("/api/display",{{"message","ABC"},{"scroll","123"}});assert(server.code==400);
 server.request("/api/display",{{"scroll","1"},{"scroll","2"}});assert(server.code==400);
 server.request("/api/display",{{"scroll","1"},{"speed","10"},{"speed","20"}});assert(server.code==400);
 server.request("/api/display",{{"scroll","1"},{"direction","left"},{"direction","right"}});assert(server.code==400);
 server.request("/api/display",{{"message","12345678901234"}});assert(server.code==400);
 server.request("/api/display",{{"scroll",String(513,'1')}});assert(server.code==400);
 server.request("/api/display",{{"scroll","123"},{"pwm","10"}});assert(server.code==400&&!display.commands().take(command)&&config.get("pwm").as<int>()==255);
 server.request("/api/config",{{"message","ABC"}});assert(server.code==400);
 server.request("/api/config",{{"scroll","123"}});assert(server.code==400);
 server.request("/api/display",{{"message",""}});assert(server.code==202&&display.commands().take(command)&&command.text.length()==0&&!config.exists());
 // Every action independently controls all four indicators; defaults stay off.
 for(const char* action:{"message","scroll","slide","appear"}){
  for(int flags=0;flags<16;++flags){
   server.request("/api/display",{{action,"123"},{"am",flags&1?"true":"false"},{"pm",flags&2?"true":"false"},{"colontop",flags&4?"true":"false"},{"colonbottom",flags&8?"true":"false"}});
   assert(server.code==202&&display.commands().take(command));
   assert(command.am==bool(flags&1)&&command.pm==bool(flags&2)&&command.colonTop==bool(flags&4)&&command.colonBottom==bool(flags&8)&&!command.colonsBlink);
   auto length=ApplicationApi::encodeDisplayCommand(command,encoded,sizeof(encoded));assert(length);
   assert(api.receiveDisplayCommand(reinterpret_cast<const uint8_t*>(encoded),length)&&display.commands().take(command));
   assert(command.am==bool(flags&1)&&command.pm==bool(flags&2)&&command.colonTop==bool(flags&4)&&command.colonBottom==bool(flags&8)&&!command.colonsBlink);
  }
  server.request("/api/display",{{action,"123"},{"am","true"},{"pm","true"},{"colons","blink"}});
  assert(server.code==202&&display.commands().take(command)&&command.am&&command.pm&&command.colonsBlink);
  auto length=ApplicationApi::encodeDisplayCommand(command,encoded,sizeof(encoded));assert(length);
  assert(api.receiveDisplayCommand(reinterpret_cast<const uint8_t*>(encoded),length)&&display.commands().take(command)&&command.am&&command.pm&&command.colonsBlink);
  server.request("/api/display",{{action,"123"}});assert(server.code==202&&display.commands().take(command)&&!command.am&&!command.pm&&!command.colonTop&&!command.colonBottom&&!command.colonsBlink);
 }
 for(const char* key:{"am","pm","colontop","colonbottom"}){
  for(const char* value:{"","1","0","TRUE","yes"}){server.request("/api/display",{{"message","ABC"},{key,value}});assert(server.code==400&&!display.commands().take(command));}
  server.request("/api/display",{{"message","ABC"},{key,"true"},{key,"false"}});assert(server.code==400&&!display.commands().take(command));
 }
 for(const char* value:{"","true","false","BLINK","on"}){server.request("/api/display",{{"message","ABC"},{"colons",value}});assert(server.code==400&&!display.commands().take(command));}
 server.request("/api/display",{{"message","ABC"},{"colons","blink"},{"colons","blink"}});assert(server.code==400);
 for(const char* key:{"colontop","colonbottom"}){
  for(const char* value:{"true","false"}){
   server.request("/api/display",{{"message","ABC"},{key,value},{"colons","blink"}});assert(server.code==400);
   server.request("/api/display",{{"message","ABC"},{"colons","blink"},{key,value}});assert(server.code==400);
  }
 }
 server.request("/api/display",{{"message","ABC"},{"blink","true"}});assert(server.code==400);
 // Remote routing preserves all indicator choices, including multiple destinations.
 api.setDisplayRouter([&](const Display::DisplayCommand& cmd){routed=cmd.destinations;auto length=ApplicationApi::encodeDisplayCommand(cmd,encoded,sizeof(encoded));assert(length);return true;});
 server.request("/api/display",{{"appear","123"},{"am","true"},{"pm","true"},{"colons","blink"},{"to","a"},{"to","b"}});
 assert(server.code==202&&routed==(Display::PortA|Display::PortB)&&!display.commands().take(command));
 assert(api.receiveDisplayCommand(reinterpret_cast<const uint8_t*>(encoded),strlen(encoded))&&display.commands().take(command)&&command.colonsBlink&&command.am&&command.pm);
 for(const char* legacyEnvelope:{"[\"display1\",\"message\",\"ABC\",150,\"left\"]","[\"display1\",\"message\",\"ABC\",150,\"left\",true]"}){
  assert(api.receiveDisplayCommand(reinterpret_cast<const uint8_t*>(legacyEnvelope),strlen(legacyEnvelope))&&display.commands().take(command)&&!command.am&&!command.pm&&!command.colonTop&&!command.colonBottom);
 }
 for(const char* bad:{"[\"display2\",\"message\",\"ABC\",150,\"left\",true]","[\"display2\",\"message\",\"ABC\",150,\"left\",-1]","[\"display2\",\"message\",\"ABC\",150,\"left\",32]","[\"display2\",\"message\",\"ABC\",150,\"left\",20]","[\"display2\",\"message\",\"ABC\",150,\"left\",24]","[\"display2\",\"message\",\"ABC\",150,\"left\"]"}){
  assert(!api.receiveDisplayCommand(reinterpret_cast<const uint8_t*>(bad),strlen(bad))&&!display.commands().take(command));
 }
 assert(initial["primary"].is<bool>()&&!initial["primary"].as<bool>());
 for(const char* value:{"true","false"}){
  server.request("/api/config",{{"primary",value}});assert(server.code==200);
  auto response=json(server.body);assert(response["primary"].is<bool>()&&response["primary"].as<bool>()==(String(value)=="true"));
  MDS_Config loaded;assert(loaded.setSettings(settings)&&loaded.begin()&&loaded.load());
  assert(loaded.get("primary").is<bool>()&&loaded.get("primary").as<bool>()==(String(value)=="true"));
 }
 for(const char* value:{"1","0","TRUE","","yes"}){server.request("/api/config",{{"primary",value}});assert(server.code==400&&!config.get("primary").as<bool>());}
 JsonDocument invalid;invalid["primary"]=1;assert(!config.set(invalid));invalid["primary"]="true";assert(!config.set(invalid));
 auto beforePrimary=LittleFS.files["/config.json"];
 LittleFS.files["/config.json"]="{\"schema\":1}";
 MDS_Config legacy;assert(legacy.setSettings(settings)&&legacy.begin()&&legacy.load());assert(legacy.get("primary").is<bool>()&&!legacy.get("primary").as<bool>());
 LittleFS.files["/config.json"]="{\"schema\":1,\"primary\":1}";assert(!legacy.load());
 LittleFS.files["/config.json"]=beforePrimary;
 server.request("/api/config",{{"name","Destination"},{"rate","123"}});assert(server.code==200);auto changed=json(server.body);assert(String(changed["name"].as<const char*>())=="Destination"&&changed["rate"].as<int>()==123);assert(config.exists());auto stored=LittleFS.files["/config.json"];
 server.request("/api/config",{{"name","Partial"},{"rate","bad"}});assert(server.code==400);assert(String(config.get("name").as<const char*>())=="Destination");assert(LittleFS.files["/config.json"]==stored);
 for(const char* invalid:{"-1","+1","1.5","12junk"," 5","4294967296",""}){server.request("/api/config",{{"rate",invalid}});assert(server.code==400&&config.get("rate").as<int>()==123);}
 server.request("/api/config",{{"rate","4294967295"}});assert(server.code==200&&config.get("rate").as<uint32_t>()==UINT32_MAX);
 server.request("/api/config",{{"schema","1"}});assert(server.code==403);
 server.request("/api/config",{{"unknown","x"}});assert(server.code==400);
 server.request("/api/config",{{"name","A"},{"name","B"}});assert(server.code==400&&String(config.get("name").as<const char*>())=="Destination");
 server.request("/api/config",{{"name",""}});assert(server.code==400);
 server.request("/api/config",{{"name","123456789012345678901234567890123"}});assert(server.code==400);
 server.request("/api/config",{{"name","Should not apply"},{"schema","1"}});assert(server.code==403&&String(config.get("name").as<const char*>())=="Destination");
 server.request("/api/config",{{"password","new-secret"},{"runtime","42"}});assert(server.code==200&&server.body.find("secret")==String::npos);assert(config.get("runtime").as<int>()==42);auto persisted=json(LittleFS.files["/config.json"]);assert(persisted["runtime"].isNull()&&String(persisted["password"].as<const char*>())=="new-secret");
 stored=LittleFS.files["/config.json"];LittleFS.renameOK=false;server.request("/api/config",{{"name","In RAM"}});assert(server.code==500);auto error=json(server.body);assert(error["applied"].as<bool>()&&!error["saved"].as<bool>());assert(String(config.get("name").as<const char*>())=="In RAM");assert(LittleFS.files["/config.json"]==stored);
 LittleFS.renameOK=true;
 server.request("/api/config",{{"pwm","64"}});assert(server.code==200);assert(config.get("pwm").as<int>()==64&&matrix.brightnessLimit()==64);assert(wire.regs[1][0]==64&&wire.regs[1][1]==DisplayGamma::toPWM(128,64));
 auto persistedBrightness=json(LittleFS.files["/config.json"]);assert(persistedBrightness["pwm"].as<int>()==64&&persistedBrightness["gcc"].as<int>()==4);
 server.request("/api/config",{{"pwm","0"}});assert(server.code==200&&wire.regs[1][0]==0&&wire.regs[1][1]==0);
 server.request("/api/config",{{"pwm","255"}});assert(server.code==200&&wire.regs[1][0]==255&&wire.regs[1][1]==DisplayGamma::toPWM(128,255));
 server.request("/api/config",{{"pwm","256"}});assert(server.code==400&&config.get("pwm").as<int>()==255);
 server.request("/api/config",{{"pwm","64"},{"gcc","8"}});assert(server.code==200&&config.get("pwm").as<int>()==64&&matrix.globalCurrent()==8&&matrix.configuredCurrent()==8&&wire.regs[3][1]==8);
 assert(json(LittleFS.files["/config.json"])["gcc"].as<int>()==8);
 MDS_Config restored;assert(restored.setSettings(settings)&&restored.begin()&&restored.load());assert(restored.get("gcc").as<int>()==8);
 for(const char* invalid:{"256","-1","1.5","0x04"}){server.request("/api/config",{{"gcc",invalid}});assert(server.code==400&&matrix.globalCurrent()==8);}
 for(const char* value:{"0","255","4"}){server.request("/api/config",{{"gcc",value}});assert(server.code==200&&matrix.globalCurrent()==config.get("gcc").as<int>());}
 stored=LittleFS.files["/config.json"];LittleFS.renameOK=false;
 server.request("/api/config",{{"gcc","6"}});assert(server.code==500&&matrix.globalCurrent()==6&&matrix.configuredCurrent()==6&&json(server.body)["applied"].as<bool>()&&!json(server.body)["saved"].as<bool>()&&LittleFS.files["/config.json"]==stored);
 LittleFS.renameOK=true;server.request("/api/config",{{"gcc","4"}});assert(server.code==200);

 assert(matrix.setGlobalCurrent(8));server.request("/api/config");assert(server.code==200&&json(server.body)["gcc"].as<int>()==4&&matrix.globalCurrent()==8);
 stored=LittleFS.files["/config.json"];wire.failAt=wire.calls+1;server.request("/api/config",{{"gcc","9"}});assert(server.code==500&&matrix.globalCurrent()==8&&matrix.configuredCurrent()==4&&LittleFS.files["/config.json"]==stored);
 assert(matrix.begin(channels,2));
 wire.failAt=wire.calls+1;server.request("/api/config",{{"pwm","60"}});assert(server.code==500&&json(server.body)["applied"].as<bool>()&&enableLevel==LOW);
 http.stop();http.stop();assert(!http.running()&&server.stops==1);http.tick();assert(server.ticks==1);http.begin();assert(server.starts==2);server.request("/api/config");assert(server.code==200);
 puts("PASS: HTTP lifecycle/route ownership, config reads and saved edits, RW enforcement, strict parsing, atomic rejection, secret filtering, persistence flags, and explicit save failures.");
}
