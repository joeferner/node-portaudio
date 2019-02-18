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

#ifndef AUDIOIO_H
#define AUDIOIO_H

#include <napi.h>
#include "Memory.h"

namespace streampunk {

class PaContext;

class AudioIO : public Napi::ObjectWrap<AudioIO> {
public:
  static void Init(Napi::Env env, Napi::Object exports);
  AudioIO(const Napi::CallbackInfo& info);
  ~AudioIO();

private:
  static Napi::FunctionReference constructor;

  Napi::Value Start(const Napi::CallbackInfo& info);
  Napi::Value Read(const Napi::CallbackInfo& info);
  Napi::Value Write(const Napi::CallbackInfo& info);
  Napi::Value Quit(const Napi::CallbackInfo& info);

  std::shared_ptr<PaContext> mPaContext;
};

} // namespace streampunk

#endif
