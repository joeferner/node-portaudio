
#include "nodePortAudio.h"
#include <portaudio.h>

#define FRAMES_PER_BUFFER  (256)

int paInitialized = false;
int portAudioStreamInitialized = false;
static Nan::Persistent<v8::Function> streamConstructor;

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

void CleanupStreamData(const Nan::WeakCallbackInfo<PortAudioData> &data) {
  printf("Cleaning up stream data.\n");
  PortAudioData *pad = data.GetParameter();
  Nan::SetInternalFieldPointer(Nan::New(pad->v8Stream), 0, NULL);
  delete pad;
}

static int nodePortAudioCallback(
  const void *inputBuffer,
  void *outputBuffer,
  unsigned long framesPerBuffer,
  const PaStreamCallbackTimeInfo* timeInfo,
  PaStreamCallbackFlags statusFlags,
  void *userData);

NAN_METHOD(StreamWriteByte);
NAN_METHOD(StreamWrite);
NAN_METHOD(StreamStart);
NAN_METHOD(StreamStop);
NAN_METHOD(WritableWrite);

PaError EnsureInitialized() {
  PaError err;

  if(!paInitialized) {
    err = Pa_Initialize();
    if(err != paNoError) {
      return err;
    };
    paInitialized = true;
  }
  return paNoError;
}

NAN_METHOD(Open) {
  PaError err;
  PaStreamParameters outputParameters;
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

  if(!portAudioStreamInitialized) {
    v8::Local<v8::FunctionTemplate> t = Nan::New<v8::FunctionTemplate>();
    t->InstanceTemplate()->SetInternalFieldCount(1);
    t->SetClassName(Nan::New("PortAudioStream").ToLocalChecked());
    streamConstructor.Reset(Nan::GetFunction(t).ToLocalChecked());

    portAudioStreamInitialized = true;
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

  v8Stream = Nan::New(streamConstructor)->NewInstance();
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
    nodePortAudioCallback,
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

NAN_METHOD(GetDevices) {
  char str[1000];
  int numDevices;

  PaError err = EnsureInitialized();
  if(err != paNoError) {
    sprintf(str, "Could not initialize PortAudio %d", err);
    Nan::ThrowError(str);
  }

  numDevices = Pa_GetDeviceCount();
  v8::Local<v8::Array> result = Nan::New<v8::Array>(numDevices);

  for ( int i = 0 ; i < numDevices ; i++) {
    const PaDeviceInfo* deviceInfo = Pa_GetDeviceInfo(i);
    v8::Local<v8::Object> v8DeviceInfo = Nan::New<v8::Object>();
    Nan::Set(v8DeviceInfo, Nan::New("id").ToLocalChecked(), Nan::New(i));
    Nan::Set(v8DeviceInfo, Nan::New("name").ToLocalChecked(),
      Nan::New(deviceInfo->name).ToLocalChecked());
    Nan::Set(v8DeviceInfo, Nan::New("maxInputChannels").ToLocalChecked(),
      Nan::New(deviceInfo->maxInputChannels));
    Nan::Set(v8DeviceInfo, Nan::New("maxOutputChannels").ToLocalChecked(),
      Nan::New(deviceInfo->maxOutputChannels));
    Nan::Set(v8DeviceInfo, Nan::New("defaultSampleRate").ToLocalChecked(),
      Nan::New(deviceInfo->defaultSampleRate));
    Nan::Set(v8DeviceInfo, Nan::New("defaultLowInputLatency").ToLocalChecked(),
      Nan::New(deviceInfo->defaultLowInputLatency));
    Nan::Set(v8DeviceInfo, Nan::New("defaultLowOutputLatency").ToLocalChecked(),
      Nan::New(deviceInfo->defaultLowOutputLatency));
    Nan::Set(v8DeviceInfo, Nan::New("defaultHighInputLatency").ToLocalChecked(),
      Nan::New(deviceInfo->defaultHighInputLatency));
    Nan::Set(v8DeviceInfo, Nan::New("defaultHighOutputLatency").ToLocalChecked(),
      Nan::New(deviceInfo->defaultHighOutputLatency));
    Nan::Set(v8DeviceInfo, Nan::New("hostAPIName").ToLocalChecked(),
      Nan::New(Pa_GetHostApiInfo(deviceInfo->hostApi)->name).ToLocalChecked());

    Nan::Set(result, i, v8DeviceInfo);
  }

  info.GetReturnValue().Set(result);
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

void WriteableCallback(uv_work_t* req) {
}

void WriteableCallbackAfter(uv_work_t* req) {
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

  data->writeCallback = new Nan::Callback(info[2].As<v8::Function>());
  if (data->buffer == NULL) { // Bootstrap
    v8::Local<v8::Object> chunk = info[0].As<v8::Object>();
    int chunkLen = node::Buffer::Length(chunk);
    unsigned char* p = (unsigned char*) node::Buffer::Data(chunk);
    data->bufferLen = chunkLen;
    data->buffer = p;
    data->bufferIdx = 0;
    data->protectBuffer.Reset(chunk);
    uv_work_t* req = new uv_work_t();
    req->data = data;
    uv_queue_work(uv_default_loop(), req, WriteableCallback,
      (uv_after_work_cb) WriteableCallbackAfter);
  } else {
    v8::Local<v8::Object> chunk = info[0].As<v8::Object>();
    int chunkLen = node::Buffer::Length(chunk);
    unsigned char* p = (unsigned char*) node::Buffer::Data(chunk);
    data->nextLen = chunkLen;
    data->nextBuffer = p;
    data->nextIdx = 0;
    data->protectNext.Reset(chunk);
  }

  info.GetReturnValue().SetUndefined();
}

static int nodePortAudioCallback(
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

  unsigned char* out = (unsigned char*) outputBuffer;
  if (data->bufferIdx >= data->bufferLen ) { //read from next
    // printf("Reading from next %i.\n", data->nextIdx);
    memcpy(out, data->nextBuffer + data->nextIdx, bytesRequested);
    data->nextIdx += bytesRequested;
  } else if (data->bufferIdx + bytesRequested >= data->bufferLen) {
    uv_work_t* req = new uv_work_t();
    req->data = data;
    int bytesRemaining = data->bufferLen - data->bufferIdx;
    if (data->nextLen > 0) {
      uv_queue_work(uv_default_loop(), req, WriteableCallback,
        (uv_after_work_cb) WriteableCallbackAfter);
      memcpy(out, data->buffer + data->bufferIdx, bytesRemaining);
      memcpy(out + bytesRemaining, data->nextBuffer, bytesRequested - bytesRemaining);
      data->bufferIdx += bytesRequested;
      data->nextIdx = bytesRequested - bytesRemaining;
    } else {
      printf("Reached the end of the stream - closing.");
      memcpy(out, data->buffer + data->bufferIdx, bytesRemaining);
      return paComplete;
    }
  } else { // read from buffer
    memcpy(out, data->buffer + data->bufferIdx, bytesRequested);
    data->bufferIdx += framesPerBuffer * multiplier;
  }

  return paContinue;
}
