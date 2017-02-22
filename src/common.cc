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

/*
   This file contains code that is common between audio input and output
 */

#include "common.h"

#ifndef NAUDIODON_COMMON
#define NAUDIODON_COMMON

void CleanupStreamData(const Nan::WeakCallbackInfo<PortAudioData> &data) {
  printf("Cleaning up stream data.\n");
  PortAudioData *pad = data.GetParameter();
  Nan::SetInternalFieldPointer(Nan::New(pad->v8Stream), 0, NULL);
  delete pad;
}

#endif
