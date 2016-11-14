/* Copyright 2016 Streampunk Media Ltd.

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

#include "AudioInput.h"

#define FRAMES_PER_BUFFER  (256)

using namespace std;

int paInputInitialized = false;
int portAudioInputStreamInitialized = false;
static Nan::Persistent<v8::Function> streamConstructor;
queue<string> bufferStack;
Nan::Persistent<v8::Function> pushCallback;
pthread_mutex_t lock;

static int nodePortAudioInputCallback(
  const void *inputBuffer,
  void *outputBuffer,
  unsigned long framesPerBuffer,
  const PaStreamCallbackTimeInfo* timeInfo,
  PaStreamCallbackFlags statusFlags,
  void *userData);

NAN_METHOD(InputStreamStart);
NAN_METHOD(InputStreamStop);
NAN_METHOD(ReadableRead);
NAN_METHOD(ItemsAvailable);
NAN_METHOD(InputSetCallback);

NAN_METHOD(OpenInput) {
  PaError err;
  PaStreamParameters inputParameters;
  Nan::MaybeLocal<v8::Object> v8Buffer;
  Nan::MaybeLocal<v8::Object> v8Stream;
  PortAudioData* data;
  int sampleRate;
  char str[1000];
  Nan::MaybeLocal<v8::Value> v8Val;

  Nan::MaybeLocal<v8::Object> options = Nan::To<v8::Object>(info[0].As<v8::Object>());

  err = EnsureInitialized();
  if(err != paNoError) {
     sprintf(str, "Could not initialize PortAudio: %s", Pa_GetErrorText(err));
    return Nan::ThrowError(str);
  }

  if(!portAudioInputStreamInitialized) {
    v8::Local<v8::FunctionTemplate> t = Nan::New<v8::FunctionTemplate>();
    t->InstanceTemplate()->SetInternalFieldCount(1);
    t->SetClassName(Nan::New("PortAudioStream").ToLocalChecked());
    streamConstructor.Reset(Nan::GetFunction(t).ToLocalChecked());

    portAudioInputStreamInitialized = true;
  }

  memset(&inputParameters, 0, sizeof(PaStreamParameters));

  v8Val = Nan::Get(options.ToLocalChecked(), Nan::New("deviceId").ToLocalChecked());
  int deviceId = (v8Val.ToLocalChecked()->IsUndefined()) ? -1 :
    Nan::To<int32_t>(v8Val.ToLocalChecked()).FromMaybe(-1);
  if ((deviceId >= 0) && (deviceId < Pa_GetDeviceCount())) {
    inputParameters.device = (PaDeviceIndex) deviceId;
  } else {
    inputParameters.device = Pa_GetDefaultInputDevice();
  }
  if (inputParameters.device == paNoDevice) {
    sprintf(str, "No default input device");
    return Nan::ThrowError(str);
  }
  printf("Input device name is %s.\n", Pa_GetDeviceInfo(inputParameters.device)->name);

  v8Val = Nan::Get(options.ToLocalChecked(), Nan::New("channelCount").ToLocalChecked());
  inputParameters.channelCount = Nan::To<int32_t>(v8Val.ToLocalChecked()).FromMaybe(2);

  if (inputParameters.channelCount > Pa_GetDeviceInfo(inputParameters.device)->maxInputChannels) {
    return Nan::ThrowError("Channel count exceeds maximum number of input channels for device.");
  }

  v8Val = Nan::Get(options.ToLocalChecked(), Nan::New("sampleFormat").ToLocalChecked());
  switch(Nan::To<int32_t>(v8Val.ToLocalChecked()).FromMaybe(0)) {
  case 8:
    inputParameters.sampleFormat = paInt8;
    break;
  case 16:
    inputParameters.sampleFormat = paInt16;
    break;
  case 24:
    inputParameters.sampleFormat = paInt24;
    break;
  case 32:
    inputParameters.sampleFormat = paInt32;
    break;
  default:
    return Nan::ThrowError("Invalid sampleFormat.");
  }

  inputParameters.suggestedLatency = 0;
  inputParameters.hostApiSpecificStreamInfo = NULL;

  v8Val = Nan::Get(options.ToLocalChecked(), Nan::New("sampleRate").ToLocalChecked());
  sampleRate = Nan::To<int32_t>(v8Val.ToLocalChecked()).FromMaybe(44100);

  data = new PortAudioData();
  data->channelCount = inputParameters.channelCount;
  data->sampleFormat = inputParameters.sampleFormat;

  
  v8Stream = Nan::New(streamConstructor)->NewInstance();
  Nan::SetInternalFieldPointer(v8Stream.ToLocalChecked(), 0, data);

  data->v8Stream.Reset(v8Stream.ToLocalChecked());
  data->v8Stream.SetWeak(data, CleanupStreamData, Nan::WeakCallbackType::kParameter);
  data->v8Stream.MarkIndependent();

  err = Pa_OpenStream(
    &data->stream,
    &inputParameters,
    NULL,//No output
    sampleRate,
    FRAMES_PER_BUFFER,
    0,//No flags being used
    nodePortAudioInputCallback,
    data);
  if(err != paNoError) {
    sprintf(str, "Could not open stream %s", Pa_GetErrorText(err));
    return Nan::ThrowError(str);
  }
  data->bufferLen = 0;

  Nan::Set(v8Stream.ToLocalChecked(), Nan::New("inputStart").ToLocalChecked(),
    Nan::GetFunction(Nan::New<v8::FunctionTemplate>(InputStreamStart)).ToLocalChecked());
  Nan::Set(v8Stream.ToLocalChecked(), Nan::New("inputStop").ToLocalChecked(),
    Nan::GetFunction(Nan::New<v8::FunctionTemplate>(InputStreamStop)).ToLocalChecked());
  Nan::Set(v8Stream.ToLocalChecked(), Nan::New("inputRead").ToLocalChecked(),
    Nan::GetFunction(Nan::New<v8::FunctionTemplate>(ReadableRead)).ToLocalChecked());
  Nan::Set(v8Stream.ToLocalChecked(), Nan::New("inputItemsAvailable").ToLocalChecked(),
    Nan::GetFunction(Nan::New<v8::FunctionTemplate>(ItemsAvailable)).ToLocalChecked());
  Nan::Set(v8Stream.ToLocalChecked(), Nan::New("inputSetCallback").ToLocalChecked(),
    Nan::GetFunction(Nan::New<v8::FunctionTemplate>(InputSetCallback)).ToLocalChecked());
  
  info.GetReturnValue().Set(v8Stream.ToLocalChecked());
}



#define INPUT_STREAM_DATA \
  PortAudioData* data = (PortAudioData*) Nan::GetInternalFieldPointer(info.This(), 0);

#define EMIT_BUFFER_OVERRUN \
  v8::Local<v8::Value> emitArgs[1]; \
  emitArgs[0] = Nan::New("overrun").ToLocalChecked(); \
  Nan::Call(Nan::Get(info.This(), Nan::New("emit").ToLocalChecked()).ToLocalChecked().As<v8::Function>(), \
    info.This(), 1, emitArgs);

NAN_METHOD(InputSetCallback){
  v8::Local<v8::Function> callback = info[0].As<v8::Function>();
  pushCallback.Reset(callback);
}

NAN_METHOD(InputStreamStop) {
  INPUT_STREAM_DATA;

  if (data != NULL) {
    PaError err = Pa_CloseStream(data->stream);
    if(err != paNoError) {
      char str[1000];
      sprintf(str, "Could not start stream %d", err);
      Nan::ThrowError(str);
    }
  }

  info.GetReturnValue().SetUndefined();
}

NAN_METHOD(InputStreamStart) {
  INPUT_STREAM_DATA;

  if (data != NULL) {
    PaError err = Pa_StartStream(data->stream);
    if(err != paNoError) {
      char str[1000];
      sprintf(str, "Could not close stream %s", Pa_GetErrorText(err));
      Nan::ThrowError(str);
    }
  }

  info.GetReturnValue().SetUndefined();
}

NAN_METHOD(ReadableRead) {
  //If the buffer is empty return null
  if(bufferStack.size() == 0){
    info.GetReturnValue().SetUndefined();
    return;
  }
  
  //Calculate memory required to transfer entire buffer stack
  size_t totalMem = bufferStack.front().size();

  /*
    This memory allocation is not freed in this script, as once
    this data is passed into nodejs by Nan responsibility for
    freeing it is passed to nodejs, as per Nan buffer documentation
  */
  char * nanTransferBuffer;
  pthread_mutex_lock(&lock);
  string pulledBuffer = bufferStack.front();
  bufferStack.pop();
  pthread_mutex_unlock(&lock);
  nanTransferBuffer = (char *)calloc(pulledBuffer.size(),sizeof(char));
  memcpy(nanTransferBuffer,pulledBuffer.data(),pulledBuffer.size());

  //Create the Nan object to be returned
  info.GetReturnValue().Set(Nan::NewBuffer(nanTransferBuffer,totalMem).ToLocalChecked());
  
}

NAN_METHOD(ItemsAvailable){
  int toReturn = bufferStack.size();
  info.GetReturnValue().Set(toReturn);
}

void ReadableCallback(uv_work_t* req) {

}

//Push data onto the readable stream on the node thread
void ReadableCallbackAfter(uv_work_t* req) {
  Nan::HandleScope scope;
  if(pushCallback.IsEmpty()){
    char str[1000];
    sprintf(str,"Push callback returned empty");
    Nan::ThrowError(str);
    return;
  }
  Nan::Callback * callback = new Nan::Callback(Nan::New(pushCallback).As<v8::Function>());
  callback->Call(0,0);
}

//Port audio calls tis every time it has new data to give us
static int nodePortAudioInputCallback(
  const void *inputBuffer,
  void *outputBuffer,
  unsigned long framesPerBuffer,
  const PaStreamCallbackTimeInfo* timeInfo,
  PaStreamCallbackFlags statusFlags,
  void *userData) {

  PortAudioData* data = (PortAudioData*)userData;

  //Calculate size of returned data
  int multiplier = 1;
  switch(data->sampleFormat) {
  case paInt8:
    multiplier = 1;
    break;
  case paInt16:
    multiplier = 2;
    break;
  case paInt24:
    multiplier = 3;
    break;
  case paInt32:
    multiplier = 4;
    break;
  }

  //Add the frame of audio to the queue
  multiplier = multiplier * data->channelCount;
  int bytesDelivered = ((int) framesPerBuffer) * multiplier;
  string buffer((char *)inputBuffer,bytesDelivered);
  pthread_mutex_lock(&lock);
  bufferStack.push(buffer);
  pthread_mutex_unlock(&lock);
  //Schedule a callback to nodejs
  uv_work_t * req = new uv_work_t();
  uv_queue_work(uv_default_loop(),req, ReadableCallback,
		(uv_after_work_cb) ReadableCallbackAfter);
  
  return paContinue;
}
