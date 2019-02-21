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
#include "GetHostAPIs.h"
#include <portaudio.h>

namespace streampunk {

Napi::Object GetHostAPIs(const Napi::CallbackInfo& info) {
  Napi::Env env = info.Env();

  PaError errCode = Pa_Initialize();
  if (errCode != paNoError) {
    std::string err = std::string("Could not initialize PortAudio: ") + Pa_GetErrorText(errCode);
    throw Napi::Error::New(env, err.c_str());
  }

  Napi::Object result = Napi::Object::New(env);

  uint32_t numHostApis = Pa_GetHostApiCount();
  Napi::Array hostApiArr = Napi::Array::New(env, numHostApis);

  uint32_t defaultHostApi = Pa_GetDefaultHostApi();
  result.Set(Napi::String::New(env, "defaultHostAPI"), Napi::Number::New(env, defaultHostApi));

  for (uint32_t i = 0; i < numHostApis; ++i) {
    const PaHostApiInfo *hostApi = Pa_GetHostApiInfo(i);
    Napi::Object hostInfo = Napi::Object::New(env);
    hostInfo.Set(Napi::String::New(env, "id"), Napi::Number::New(env, i));
    hostInfo.Set(Napi::String::New(env, "name"), Napi::String::New(env, hostApi->name));
    switch(hostApi->type) {
      case paInDevelopment:
        hostInfo.Set(Napi::String::New(env, "type"), Napi::String::New(env, "InDevelopment"));
        break;
      case paDirectSound:
        hostInfo.Set(Napi::String::New(env, "type"), Napi::String::New(env, "DirectSound"));
        break;
      case paMME:
        hostInfo.Set(Napi::String::New(env, "type"), Napi::String::New(env, "MME"));
        break;
      case paASIO:
        hostInfo.Set(Napi::String::New(env, "type"), Napi::String::New(env, "ASIO"));
        break;
      case paSoundManager:
        hostInfo.Set(Napi::String::New(env, "type"), Napi::String::New(env, "SoundManager"));
        break;
      case paCoreAudio:
        hostInfo.Set(Napi::String::New(env, "type"), Napi::String::New(env, "CoreAudio"));
        break;
      case paOSS:
        hostInfo.Set(Napi::String::New(env, "type"), Napi::String::New(env, "OSS"));
        break;
      case paALSA:
        hostInfo.Set(Napi::String::New(env, "type"), Napi::String::New(env, "ALSA"));
        break;
      case paAL:
        hostInfo.Set(Napi::String::New(env, "type"), Napi::String::New(env, "AL"));
        break;
      case paBeOS:
        hostInfo.Set(Napi::String::New(env, "type"), Napi::String::New(env, "BeOS"));
        break;
      case paWDMKS:
        hostInfo.Set(Napi::String::New(env, "type"), Napi::String::New(env, "WDMKS"));
        break;
      case paJACK:
        hostInfo.Set(Napi::String::New(env, "type"), Napi::String::New(env, "JACK"));
        break;
      case paWASAPI:
        hostInfo.Set(Napi::String::New(env, "type"), Napi::String::New(env, "WASAPI"));
        break;
      case paAudioScienceHPI:
        hostInfo.Set(Napi::String::New(env, "type"), Napi::String::New(env, "AudioScienceHPI"));
        break;
      default:
        hostInfo.Set(Napi::String::New(env, "type"), Napi::String::New(env, "Unknown"));
    }
    hostInfo.Set(Napi::String::New(env, "deviceCount"), Napi::Number::New(env, hostApi->deviceCount));
    hostInfo.Set(Napi::String::New(env, "defaultInput"), Napi::Number::New(env, hostApi->defaultInputDevice));
    hostInfo.Set(Napi::String::New(env, "defaultOutput"), Napi::Number::New(env, hostApi->defaultOutputDevice));
    hostApiArr.Set(i, hostInfo);
  }
  result.Set(Napi::String::New(env, "HostAPIs"), hostApiArr);

  Pa_Terminate();
  return result;
}

} // namespace streampunk
