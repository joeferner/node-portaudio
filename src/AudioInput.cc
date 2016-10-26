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
#include "common.h"
#include <portaudio.h>

#define FRAMES_PER_BUFFER  (256)

int paInputInitialized = false;
int portAudioInputStreamInitialized = false;
static Nan::Persistent<v8::Function> streamConstructor;
unsigned char * buffer[2];

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

//This method runs on normal program termination, and frees the buffer memory
void sig_handler(int signo){
  if (signo == SIGINT || signo == SIGTERM){
    free(buffer[0]);
    free(buffer[1]);
    printf("Freed buffers");
  }
}

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

  //set up termination signal handling to ensure buffers are free
  signal(SIGINT,sig_handler);
  signal(SIGTERM,sig_handler);
  
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
  data->bufferIdx = 0;
  data->nextIdx = 0;
  data->writeIdx = 1;
  data->channelCount = inputParameters.channelCount;
  data->sampleFormat = inputParameters.sampleFormat;
  data->writeCallback = new Nan::Callback(info[1].As<v8::Function>());

  v8Stream = Nan::New(streamConstructor)->NewInstance();
  // printf("Internal field count is %i\n", argv[1].As<v8::Object>()->InternalFieldCount());
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
  Nan::Set(v8Stream.ToLocalChecked(), Nan::New("_Read").ToLocalChecked(),
    Nan::GetFunction(Nan::New<v8::FunctionTemplate>(ReadableRead)).ToLocalChecked());
  
  info.GetReturnValue().Set(v8Stream.ToLocalChecked());
  printf("Input stream opened OK.\n");
}



#define INPUT_STREAM_DATA \
  PortAudioData* data = (PortAudioData*) Nan::GetInternalFieldPointer(info.This(), 0);

#define EMIT_BUFFER_OVERRUN \
  v8::Local<v8::Value> emitArgs[1]; \
  emitArgs[0] = Nan::New("overrun").ToLocalChecked(); \
  Nan::Call(Nan::Get(info.This(), Nan::New("emit").ToLocalChecked()).ToLocalChecked().As<v8::Function>(), \
    info.This(), 1, emitArgs);

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
      sprintf(str, "Could not start stream %s", Pa_GetErrorText(err));
      Nan::ThrowError(str);
    }
  }

  info.GetReturnValue().SetUndefined();
}

void ReadableCallback(uv_work_t* req) {
  
}

//Push data onto the readable stream on the node thread
void ReadableCallbackAfter(uv_work_t* req) {
  Nan::HandleScope scope;
  PortAudioData* request = (PortAudioData*)req->data;
  v8::Local<v8::Value> argv[] = {Nan::NewBuffer((char*)request->buffer,request->bufferLen).ToLocalChecked()};
  request->writeCallback->Call(1,argv);
}

NAN_METHOD(ReadReadable) {
  info.GetReturnValue().SetUndefined();
}

//Port audio calls this every time it has new data to give us
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

  multiplier = multiplier * data->channelCount;
  int bytesDelivered = ((int) framesPerBuffer) * multiplier;

  //First time round, allocate memory to buffers - freed on termination
  if(data->buffer == NULL){
    buffer[0] = (unsigned char *)calloc(1,bytesDelivered);
    buffer[1] = (unsigned char *)calloc(1,bytesDelivered);
    data->buffer = buffer[0];
    data->nextBuffer = buffer[1];
  }

  //Copy input buffer into local buffers
  memcpy(data->buffer,inputBuffer,bytesDelivered);
  data->bufferLen = bytesDelivered;
  data->writeIdx = 0;

  //Schedule output to nodejs stream (node is on wrong thread to do it here)
  uv_work_t* req = new uv_work_t();
  req->data = data;
  uv_queue_work(uv_default_loop(),req, ReadableCallback,
		(uv_after_work_cb) ReadableCallbackAfter);

  return paContinue;
}

