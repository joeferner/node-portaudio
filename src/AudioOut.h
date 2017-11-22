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

#ifndef AUDIOOUT_H
#define AUDIOOUT_H

#include "Memory.h"

namespace streampunk {

class OutContext;

class AudioOut : public Nan::ObjectWrap {
public:
  static NAN_MODULE_INIT(Init);

  std::shared_ptr<OutContext> getContext() const { return mOutContext; }
  void doStart();

private:
  explicit AudioOut(v8::Local<v8::Object> options);
  ~AudioOut();

  static NAN_METHOD(New) {
    if (info.IsConstructCall()) {
      if (!((info.Length() == 1) && (info[0]->IsObject())))
        return Nan::ThrowError("AudioOut constructor requires a valid options object as the parameter");
      v8::Local<v8::Object> options = v8::Local<v8::Object>::Cast(info[0]);
      AudioOut *obj = new AudioOut(options);
      obj->Wrap(info.This());
      info.GetReturnValue().Set(info.This());
    } else {
      const int argc = 1;
      v8::Local<v8::Value> argv[] = { info[0] };
      v8::Local<v8::Function> cons = Nan::New(constructor());
      info.GetReturnValue().Set(cons->NewInstance(Nan::GetCurrentContext(), argc, argv).ToLocalChecked());
    }
  }

  static inline Nan::Persistent<v8::Function> & constructor() {
    static Nan::Persistent<v8::Function> my_constructor;
    return my_constructor;
  }

  static NAN_METHOD(Start);
  static NAN_METHOD(Write);
  static NAN_METHOD(Quit);

  std::shared_ptr<OutContext> mOutContext;
};

} // namespace streampunk

#endif
