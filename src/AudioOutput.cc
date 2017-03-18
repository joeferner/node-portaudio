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

#include "common.h"
#include "AudioOutput.h"
#include <portaudio.h>

using namespace std;
int portAudioOutputStreamInitialized = false;
static Nan::Persistent<v8::Function> streamConstructor;
queue<string> outBufferStack;
string currentChunk;
unsigned int currentChunkIdx;
uv_mutex_t outlock;
uv_async_t *outReq;

static int nodePortAudioOutputCallback(
  const void *inputBuffer,
  void *outputBuffer,
  unsigned long framesPerBuffer,
  const PaStreamCallbackTimeInfo* timeInfo,
  PaStreamCallbackFlags statusFlags,
  void *userData);

void WriteableCallback(uv_async_t* req);

NAN_METHOD(StreamStart);
NAN_METHOD(StreamStop);
NAN_METHOD(WritableWrite);

NAN_METHOD(OpenOutput) {
  PaError err;
  PaStreamParameters outputParameters;
  Nan::MaybeLocal<v8::Object> v8Buffer;
  Nan::MaybeLocal<v8::Object> v8Stream;
  PortAudioData* data;
  int sampleRate;
  char str[1000];
  Nan::MaybeLocal<v8::Value> v8Val;

  Nan::MaybeLocal<v8::Object> options = Nan::To<v8::Object>(info[0].As<v8::Object>());

  outReq = new uv_async_t;
  uv_async_init(uv_default_loop(),outReq,WriteableCallback);
  uv_mutex_init(&outlock);

  err = EnsureInitialized();
  if(err != paNoError) {
    sprintf(str, "Could not initialize PortAudio: %s", Pa_GetErrorText(err));
    return Nan::ThrowError(str);
  }

  if(!portAudioOutputStreamInitialized) {
    v8::Local<v8::FunctionTemplate> t = Nan::New<v8::FunctionTemplate>();
    t->InstanceTemplate()->SetInternalFieldCount(1);
    t->SetClassName(Nan::New("PortAudioStream").ToLocalChecked());
    streamConstructor.Reset(Nan::GetFunction(t).ToLocalChecked());

    portAudioOutputStreamInitialized = true;
  }

  memset(&outputParameters, 0, sizeof(PaStreamParameters));

  v8Val = Nan::Get(options.ToLocalChecked(), Nan::New("deviceId").ToLocalChecked());
  int deviceId = (v8Val.ToLocalChecked()->IsUndefined()) ? -1 :
    Nan::To<int32_t>(v8Val.ToLocalChecked()).FromMaybe(-1);
  if ((deviceId >= 0) && (deviceId < Pa_GetDeviceCount())) {
    outputParameters.device = (PaDeviceIndex) deviceId;
  } else {
    outputParameters.device = Pa_GetDefaultOutputDevice();
  }
  if (outputParameters.device == paNoDevice) {
    sprintf(str, "No default output device");
    return Nan::ThrowError(str);
  }
  printf("Output device name is %s.\n", Pa_GetDeviceInfo(outputParameters.device)->name);

  v8Val = Nan::Get(options.ToLocalChecked(), Nan::New("channelCount").ToLocalChecked());
  outputParameters.channelCount = Nan::To<int32_t>(v8Val.ToLocalChecked()).FromMaybe(2);

  if (outputParameters.channelCount > Pa_GetDeviceInfo(outputParameters.device)->maxOutputChannels) {
    return Nan::ThrowError("Channel count exceeds maximum number of output channels for device.");
  }

  v8Val = Nan::Get(options.ToLocalChecked(), Nan::New("sampleFormat").ToLocalChecked());
  switch(Nan::To<int32_t>(v8Val.ToLocalChecked()).FromMaybe(0)) {
  case 8:
    outputParameters.sampleFormat = paInt8;
    break;
  case 16:
    outputParameters.sampleFormat = paInt16;
    break;
  case 24:
    outputParameters.sampleFormat = paInt24;
    break;
  case 32:
    outputParameters.sampleFormat = paInt32;
    break;
  default:
    return Nan::ThrowError("Invalid sampleFormat.");
  }

  outputParameters.suggestedLatency = Pa_GetDeviceInfo( outputParameters.device )->defaultLowOutputLatency;
  outputParameters.hostApiSpecificStreamInfo = NULL;

  v8Val = Nan::Get(options.ToLocalChecked(), Nan::New("sampleRate").ToLocalChecked());
  sampleRate = Nan::To<int32_t>(v8Val.ToLocalChecked()).FromMaybe(44100);

  data = new PortAudioData();
  data->bufferIdx = 0;
  data->nextIdx = 0;
  data->writeIdx = 1;
  data->channelCount = outputParameters.channelCount;
  data->sampleFormat = outputParameters.sampleFormat;

  v8Stream = Nan::New(streamConstructor)->NewInstance(Nan::GetCurrentContext()).ToLocalChecked();
  // printf("Internal field count is %i\n", argv[1].As<v8::Object>()->InternalFieldCount());
  Nan::SetInternalFieldPointer(v8Stream.ToLocalChecked(), 0, data);

  data->v8Stream.Reset(v8Stream.ToLocalChecked());
  data->v8Stream.SetWeak(data, CleanupStreamData, Nan::WeakCallbackType::kParameter);
  data->v8Stream.MarkIndependent();

  err = Pa_OpenStream(
    &data->stream,
    NULL, // no input
    &outputParameters,
    sampleRate,
    FRAMES_PER_BUFFER,
    paClipOff, // we won't output out of range samples so don't bother clipping them
    nodePortAudioOutputCallback,
    data);
  if(err != paNoError) {
    sprintf(str, "Could not open stream %s", Pa_GetErrorText(err));
    return Nan::ThrowError(str);
  }
  data->bufferLen = 0;

  Nan::Set(v8Stream.ToLocalChecked(), Nan::New("start").ToLocalChecked(),
    Nan::GetFunction(Nan::New<v8::FunctionTemplate>(StreamStart)).ToLocalChecked());
  Nan::Set(v8Stream.ToLocalChecked(), Nan::New("stop").ToLocalChecked(),
    Nan::GetFunction(Nan::New<v8::FunctionTemplate>(StreamStop)).ToLocalChecked());
  Nan::Set(v8Stream.ToLocalChecked(), Nan::New("_write").ToLocalChecked(),
    Nan::GetFunction(Nan::New<v8::FunctionTemplate>(WritableWrite)).ToLocalChecked());

  info.GetReturnValue().Set(v8Stream.ToLocalChecked());
  // printf("Stream opened OK.\n");
}

#define STREAM_DATA \
  PortAudioData* data = (PortAudioData*) Nan::GetInternalFieldPointer(info.This(), 0);

#define EMIT_BUFFER_OVERRUN \
  v8::Local<v8::Value> emitArgs[1]; \
  emitArgs[0] = Nan::New("overrun").ToLocalChecked(); \
  Nan::Call(Nan::Get(info.This(), Nan::New("emit").ToLocalChecked()).ToLocalChecked().As<v8::Function>(), \
    info.This(), 1, emitArgs);

NAN_METHOD(StreamStop) {
  STREAM_DATA;

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

NAN_METHOD(StreamStart) {
  STREAM_DATA;

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

NAN_METHOD(SetCallback){

}

void WriteableCallback(uv_async_t* req) {
  Nan::HandleScope scope;
  PortAudioData* request = (PortAudioData*)req->data;
  if (request->nextBuffer != NULL) {
    request->buffer = request->nextBuffer;
    request->bufferIdx = request->nextIdx;
    request->protectBuffer.Reset(request->protectNext);
    request->bufferLen = request->nextLen;
    request->nextLen = 0;
  }
  // printf("CALLBACK!!! %i %i\n", request->writeCallback, request->nextLen);
  request->writeCallback->Call(0, 0);
}

NAN_METHOD(WritableWrite) {
  STREAM_DATA;
  Nan::Callback *writeCallback = new Nan::Callback(info[2].As<v8::Function>());
  v8::Local<v8::Object> chunk = info[0].As<v8::Object>();
  int chunkLen = (int)node::Buffer::Length(chunk);
  string buffer((char *)node::Buffer::Data(chunk),chunkLen);
  // printf("Writeable write length %i.", chunkLen);
  uv_mutex_lock(&outlock);
  outBufferStack.push(buffer);
  uv_mutex_unlock(&outlock);
  data->writeCallback = writeCallback;

  info.GetReturnValue().SetUndefined();
}

static int nodePortAudioOutputCallback(
  const void *inputBuffer,
  void *outputBuffer,
  unsigned long framesPerBuffer,
  const PaStreamCallbackTimeInfo* timeInfo,
  PaStreamCallbackFlags statusFlags,
  void *userData) {

  PortAudioData* data = (PortAudioData*)userData;

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
  int bytesRequested = ((int) framesPerBuffer) * multiplier;
  // printf("Bytes requested %i multiplier %i currentChunkIdx %i.\n", bytesRequested, multiplier, currentChunkIdx);

  unsigned char* out = (unsigned char*) outputBuffer;
  if (currentChunkIdx >= currentChunk.length() ) { //read from next
    uv_mutex_lock(&outlock);
    // printf("Read from next.\n");
    if(outBufferStack.size() > 0){
      currentChunk = outBufferStack.front();
      outBufferStack.pop();
    }
    outReq->data = data;
    uv_mutex_unlock(&outlock);
    uv_async_send(outReq);
    currentChunkIdx = bytesRequested;
    memcpy(out, currentChunk.data() + bytesRequested, bytesRequested);
  } else if (currentChunkIdx + bytesRequested >= currentChunk.length()) {
    // printf("Read current chunk.\n");
    outReq->data = data;
    uv_async_send(outReq);
    int bytesRemaining = (int)currentChunk.length() - currentChunkIdx;
    uv_mutex_lock(&outlock);
    if (outBufferStack.size() > 0) {
      memcpy(out, currentChunk.data() + currentChunkIdx, bytesRemaining);
      currentChunk = outBufferStack.front();
      outBufferStack.pop();
      memcpy(out + bytesRemaining, currentChunk.data(), bytesRequested - bytesRemaining);
      currentChunkIdx = bytesRequested - bytesRemaining;
    } else {
      printf("Reached the end of the stream - closing.");
      memcpy(out, currentChunk.data() + currentChunkIdx, bytesRemaining);
      uv_mutex_unlock(&outlock);
      return paComplete;
    }
    uv_mutex_unlock(&outlock);
  } else { // read from buffer
    // printf("Read from buffer.\n");
    memcpy(out, currentChunk.data() + currentChunkIdx, bytesRequested);
    currentChunkIdx += framesPerBuffer * multiplier;
  }

  return paContinue;
}
