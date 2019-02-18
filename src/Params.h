/* Copyright 2019 Streampunk Media Ltd.

  Licensed under the Apache License, Version 2.0 (the "License");
  you may not use this file except in compliance with the License.
  You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

  Unless required by applicable law or agreed to in writing, software
  distributed under the License is distributed on an "AS IS" BASIS,
  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
  See the License for the specific language governing permissions and
  limitations under the License.
*/

#ifndef PARAMS_H
#define PARAMS_H

#include <napi.h>
#include <sstream>

using namespace Napi;

namespace streampunk {

class Params {
protected:
  Params() {}
  virtual ~Params() {}

  Napi::Value getKey(Napi::Env env, Napi::Object tags, const std::string& key) {
    Napi::Value val = env.Null();
    Napi::String keyStr = Napi::String::New(env, key);
    if ((tags).Has(keyStr))
      val = (tags).Get(keyStr);
    return val;
  }

  bool unpackBool(Napi::Env env, Napi::Object tags, const std::string& key, bool dflt) {
    bool result = dflt;
    Napi::Value val = getKey(env, tags, key);
    if ((env.Null() != val) && val.IsBoolean())
      result = val.As<Napi::Boolean>().Value();
    return result;
  }

  uint32_t unpackNum(Napi::Env env, Napi::Object tags, const std::string& key, uint32_t dflt) {
    uint32_t result = dflt;
    Napi::Value val = getKey(env, tags, key);
    if ((env.Null() != val) && val.IsNumber())
      result = val.As<Napi::Number>().Uint32Value();
    return result;
  } 

  std::string unpackStr(Napi::Env env, Napi::Object tags, const std::string& key, std::string dflt) {
    std::string result = dflt;
    Napi::Value val = getKey(env, tags, key);
    if ((env.Null() != val) && val.IsString())
      result = val.As<Napi::String>().Utf8Value();
    return result;
  } 

private:
  Params(const Params &);
};


class AudioOptions : public Params {
public:
  AudioOptions(Napi::Env env, Napi::Object tags)
    : mDeviceID(unpackNum(env, tags, "deviceId", 0xffffffff)),
      mSampleRate(unpackNum(env, tags, "sampleRate", 44100)),
      mChannelCount(unpackNum(env, tags, "channelCount", 2)),
      mSampleFormat(unpackNum(env, tags, "sampleFormat", 8)),
      mMaxQueue(unpackNum(env, tags, "maxQueue", 2))
  {}
  ~AudioOptions() {}

  uint32_t deviceID() const  { return mDeviceID; }
  uint32_t sampleRate() const  { return mSampleRate; }
  uint32_t channelCount() const  { return mChannelCount; }
  uint32_t sampleFormat() const  { return mSampleFormat; }
  uint32_t maxQueue() const  { return mMaxQueue; }

  std::string toString() const  { 
    std::stringstream ss;
    ss << "audio options: ";
    if (mDeviceID == 0xffffffff)
      ss << "default device, ";
    else
      ss << "device " << mDeviceID << ", ";
    ss << "sample rate " << mSampleRate << ", ";
    ss << "channels " << mChannelCount << ", ";
    ss << "bits per sample " << mSampleFormat << ", ";
    ss << "max queue " << mMaxQueue;
    return ss.str();
  }

private:
  uint32_t mDeviceID;
  uint32_t mSampleRate;
  uint32_t mChannelCount;
  uint32_t mSampleFormat;
  uint32_t mMaxQueue;
};

} // namespace streampunk

#endif
