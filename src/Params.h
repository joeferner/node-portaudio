/* Copyright 2017 Streampunk Media Ltd.

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

#include <nan.h>
#include <sstream>

using namespace v8;

namespace streampunk {

class Params {
protected:
  Params() {}
  virtual ~Params() {}

  Local<Value> getKey(Local<Object> tags, const std::string& key) {
    Local<Value> val = Nan::Null();
    Local<String> keyStr = Nan::New<String>(key).ToLocalChecked();
    if (Nan::Has(tags, keyStr).FromJust())
      val = Nan::Get(tags, keyStr).ToLocalChecked();
    return val;
  }

  std::string unpackValue(Local<Value> val) {
    Local<Array> valueArray = Local<Array>::Cast(val);
    return *String::Utf8Value(valueArray->Get(0));
  }

  bool unpackBool(Local<Object> tags, const std::string& key, bool dflt) {
    bool result = dflt;
    Local<Value> val = getKey(tags, key);
    if (Nan::Null() != val)
      result = Nan::To<bool>(val).FromJust();
    return result;
  }

  uint32_t unpackNum(Local<Object> tags, const std::string& key, uint32_t dflt) {
    uint32_t result = dflt;
    Local<Value> val = getKey(tags, key);
    if (Nan::Null() != val)
      result = Nan::To<uint32_t>(val).FromJust();
    return result;
  } 

  std::string unpackStr(Local<Object> tags, const std::string& key, std::string dflt) {
    std::string result = dflt;
    Local<Value> val = getKey(tags, key);
    if (Nan::Null() != val)
      result = *String::Utf8Value(val);
    return result;
  } 

private:
  Params(const Params &);
};


class AudioOptions : public Params {
public:
  AudioOptions(Local<Object> tags)
    : mDeviceID(unpackNum(tags, "deviceId", 0xffffffff)),
      mSampleRate(unpackNum(tags, "sampleRate", 44100)),
      mChannelCount(unpackNum(tags, "channelCount", 2)),
      mSampleFormat(unpackNum(tags, "sampleFormat", 8)),
      mMaxQueue(unpackNum(tags, "maxQueue", 2))
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
