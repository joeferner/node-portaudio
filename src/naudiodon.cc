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

#include <napi.h>
#include "GetDevices.h"
#include "GetHostAPIs.h"
#include "AudioIO.h"

Napi::Object InitAll(Napi::Env env, Napi::Object exports) {
  exports.Set(Napi::String::New(env, "getDevices"), Napi::Function::New(env, streampunk::GetDevices));
  exports.Set(Napi::String::New(env, "getHostAPIs"), Napi::Function::New(env, streampunk::GetHostAPIs));
  streampunk::AudioIO::Init(env, exports);
  return exports;
}

NODE_API_MODULE(NODE_GYP_MODULE_NAME, InitAll);
