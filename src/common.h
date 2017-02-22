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

#include <nan.h>
#include <node_buffer.h>
#include <cstring>
#include <portaudio.h>

#ifndef NAUDIODON_COMMON_H
#define NAUDIODON_COMMON_H

#define FRAMES_PER_BUFFER  (256)

struct PortAudioData {
  unsigned char* buffer;
  unsigned char* nextBuffer;
  int bufferLen;
  int nextLen;
  int bufferIdx;
  int nextIdx;
  int writeIdx;
  int sampleFormat;
  int channelCount;
  PaStream* stream;
  Nan::Persistent<v8::Object> v8Stream;
  Nan::Persistent<v8::Object> protectBuffer;
  Nan::Persistent<v8::Object> protectNext;
  Nan::Callback *writeCallback;
};

void CleanupStreamData(const Nan::WeakCallbackInfo<PortAudioData> &data);

#endif
