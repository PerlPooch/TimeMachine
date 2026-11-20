/*
 * ===========================================================================
 * File:      tests/weather/host/LittleFS.h
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
#include <map>
#include <algorithm>
#include <cstring>
class File {
 std::string* data_=nullptr;
 size_t position_=0;
 bool writable_=false;
 bool shortWrite_=false;
public:
 File()=default;
 File(std::string* data,bool writable,bool shortWrite):data_(data),writable_(writable),shortWrite_(shortWrite){}
 explicit operator bool() const{return data_!=nullptr;}
 size_t size() const{return data_?data_->size():0;}
 int available() const{return data_&&position_<data_->size();}
 int read(){return available()?uint8_t((*data_)[position_++]):-1;}
 size_t readBytes(char* buffer,size_t count){size_t n=std::min(count,size()-position_);if(n)memcpy(buffer,data_->data()+position_,n);position_+=n;return n;}
 size_t write(uint8_t byte){return write(&byte,1);}
 size_t write(const uint8_t* buffer,size_t count){if(!data_||!writable_)return 0;size_t n=shortWrite_&&count?count-1:count;data_->append(reinterpret_cast<const char*>(buffer),n);return n;}
 void flush(){}
 void close(){}
};
class FakeFS {
public:
 std::map<std::string,std::string> files;
 bool mountOK=true,renameOK=true,writeOK=true,readOK=true,shortWrite=false;
 bool formatted=false;
 bool begin(bool format){formatted=format;return mountOK;}
 bool exists(const String& path){return files.count(path)>0;}
 File open(const String& path,const char* mode){bool writable=*mode=='w';if(writable){if(!writeOK)return {};files[path].clear();}else if(!readOK||!exists(path))return {};return File(&files[path],writable,shortWrite);}
 bool remove(const String& path){return files.erase(path)>0;}
 bool rename(const String& from,const String& to){if(!renameOK||!exists(from))return false;files[to]=files[from];files.erase(from);return true;}
};
extern FakeFS LittleFS;
