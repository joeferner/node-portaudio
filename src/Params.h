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
    if (Nan::Null() != val) {
      if (val->IsArray()) {
        std::string valStr = unpackValue(val);
        if (!valStr.empty()) {
          if ((0==valStr.compare("1")) || (0==valStr.compare("true")))
            result = true;
          else if ((0==valStr.compare("0")) || (0==valStr.compare("false")))
            result = false;
        }
      } else
        result = Nan::To<bool>(val).FromJust();
    }
    return result;
  }

  uint32_t unpackNum(Local<Object> tags, const std::string& key, uint32_t dflt) {
    uint32_t result = dflt;
    Local<Value> val = getKey(tags, key);
    if (Nan::Null() != val) {
      if (val->IsArray()) {
        std::string valStr = unpackValue(val);
        result = valStr.empty()?dflt:std::stoi(valStr);
      } else
        result = Nan::To<uint32_t>(val).FromJust();
    }
    return result;
  } 

  std::string unpackStr(Local<Object> tags, const std::string& key, std::string dflt) {
    std::string result = dflt;
    Local<Value> val = getKey(tags, key);
    if (Nan::Null() != val) {
      if (val->IsArray()) {
        std::string valStr = unpackValue(val);
        result = valStr.empty()?dflt:valStr;
      } else
        result = *String::Utf8Value(val);
    }
    return result;
  } 

private:
  Params(const Params &);
};

} // namespace streampunk

#endif
