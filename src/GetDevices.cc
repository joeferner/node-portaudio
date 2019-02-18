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
#include <portaudio.h>

namespace streampunk {

Napi::Value GetDevices(const Napi::CallbackInfo& info) {
  Napi::Env env = info.Env();
  uint32_t numDevices;

  PaError errCode = Pa_Initialize();
  if (errCode != paNoError) {
    std::string err = std::string("Could not initialize PortAudio: ") + Pa_GetErrorText(errCode);
    throw Napi::Error::New(env, err.c_str());
  }

  numDevices = Pa_GetDeviceCount();
  Napi::Array result = Napi::Array::New(env, numDevices);

  for (uint32_t i = 0; i < numDevices; ++i) {
    const PaDeviceInfo* deviceInfo = Pa_GetDeviceInfo(i);
    Napi::Object v8DeviceInfo = Napi::Object::New(env);
    v8DeviceInfo.Set(Napi::String::New(env, "id"), Napi::Number::New(env, i));
    v8DeviceInfo.Set(Napi::String::New(env, "name"), Napi::String::New(env, deviceInfo->name));
    v8DeviceInfo.Set(Napi::String::New(env, "maxInputChannels"), Napi::Number::New(env, deviceInfo->maxInputChannels));
    v8DeviceInfo.Set(Napi::String::New(env, "maxOutputChannels"), Napi::Number::New(env, deviceInfo->maxOutputChannels));
    v8DeviceInfo.Set(Napi::String::New(env, "defaultSampleRate"), Napi::Number::New(env, deviceInfo->defaultSampleRate));
    v8DeviceInfo.Set(Napi::String::New(env, "defaultLowInputLatency"), Napi::Number::New(env, deviceInfo->defaultLowInputLatency));
    v8DeviceInfo.Set(Napi::String::New(env, "defaultLowOutputLatency"), Napi::Number::New(env, deviceInfo->defaultLowOutputLatency));
    v8DeviceInfo.Set(Napi::String::New(env, "defaultHighInputLatency"), Napi::Number::New(env, deviceInfo->defaultHighInputLatency));
    v8DeviceInfo.Set(Napi::String::New(env, "defaultHighOutputLatency"), Napi::Number::New(env, deviceInfo->defaultHighOutputLatency));
    v8DeviceInfo.Set(Napi::String::New(env, "hostAPIName"), Napi::String::New(env, Pa_GetHostApiInfo(deviceInfo->hostApi)->name));
    result.Set(i, v8DeviceInfo);
  }

  Pa_Terminate();
  return result;
}

} // namespace streampunk
