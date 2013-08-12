
#include "nodePortAudio.h"
#include <portaudio.h>

#define FRAMES_PER_BUFFER  (128)

int g_initialized = false;
int g_portAudioStreamInitialized = false;
v8::Persistent<v8::Function> g_streamConstructor;

struct PortAudioData {
  unsigned char* buffer;
  int bufferLen;
  int readIdx;
  int writeIdx;
  int sampleFormat;
  int channelCount;
  PaStream* stream;
  v8::Persistent<v8::Object> v8Stream;
};

static int nodePortAudioCallback(
  const void *inputBuffer,
  void *outputBuffer,
  unsigned long framesPerBuffer,
  const PaStreamCallbackTimeInfo* timeInfo,
  PaStreamCallbackFlags statusFlags,
  void *userData);

v8::Handle<v8::Value> stream_writeByte(const v8::Arguments& args);
v8::Handle<v8::Value> stream_write(const v8::Arguments& args);
v8::Handle<v8::Value> stream_start(const v8::Arguments& args);
v8::Handle<v8::Value> stream_stop(const v8::Arguments& args);
void CleanupStreamData(v8::Persistent<v8::Value> obj, void *parameter);

PaError EnsureInitialized() {
  PaError err;

  if(!g_initialized) {
    err = Pa_Initialize();
    if(err != paNoError) {
      return err;
    };
    g_initialized = true;
  }
  return paNoError;
}

v8::Handle<v8::Value> Open(const v8::Arguments& args) {
  v8::HandleScope scope;
  PaError err;
  PaStreamParameters outputParameters;
  v8::Local<v8::Object> v8Buffer;
  v8::Local<v8::Object> v8Stream;
  PortAudioData* data;
  int sampleRate;
  char str[1000];
  v8::Handle<v8::Value> initArgs[1];
  v8::Handle<v8::Value> toEventEmitterArgs[1];
  v8::Local<v8::Value> v8Val;

  v8::Handle<v8::Value> argv[2];
  argv[0] = v8::Undefined();
  argv[1] = v8::Undefined();

  // options
  if(!args[0]->IsObject()) {
    return scope.Close(v8::ThrowException(v8::Exception::TypeError(v8::String::New("First argument must be an object"))));
  }
  v8::Local<v8::Object> options = args[0]->ToObject();

  // callback
  if(!args[1]->IsFunction()) {
    return scope.Close(v8::ThrowException(v8::Exception::TypeError(v8::String::New("Second argument must be a function"))));
  }
  v8::Local<v8::Value> callback = args[1];

  err = EnsureInitialized();
  if(err != paNoError) {
    sprintf(str, "Could not initialize PortAudio %d", err);
    argv[0] = v8::Exception::TypeError(v8::String::New(str));
    goto openDone;
  }

  if(!g_portAudioStreamInitialized) {
    v8::Local<v8::FunctionTemplate> t = v8::FunctionTemplate::New();
    t->InstanceTemplate()->SetInternalFieldCount(1);
    t->SetClassName(v8::String::NewSymbol("PortAudioStream"));
    g_streamConstructor = v8::Persistent<v8::Function>::New(t->GetFunction());

    toEventEmitterArgs[0] = g_streamConstructor;
    v8Val = options->Get(v8::String::New("toEventEmitter"));
    v8::Function::Cast(*v8Val)->Call(options, 1, toEventEmitterArgs);

    g_portAudioStreamInitialized = true;
  }

  memset(&outputParameters, 0, sizeof(PaStreamParameters));

  outputParameters.device = Pa_GetDefaultOutputDevice();
  if (outputParameters.device == paNoDevice) {
    sprintf(str, "No default output device");
    argv[0] = v8::Exception::TypeError(v8::String::New(str));
    goto openDone;
  }

  v8Val = options->Get(v8::String::New("channelCount"));
  outputParameters.channelCount = v8Val->ToInt32()->Value();

  v8Val = options->Get(v8::String::New("sampleFormat"));
  switch(v8Val->ToInt32()->Value()) {
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
    argv[0] = v8::Exception::TypeError(v8::String::New("Invalid sampleFormat"));
    goto openDone;
  }
  outputParameters.suggestedLatency = Pa_GetDeviceInfo( outputParameters.device )->defaultLowOutputLatency;
  outputParameters.hostApiSpecificStreamInfo = NULL;

  v8Val = options->Get(v8::String::New("sampleRate"));
  sampleRate = v8Val->ToInt32()->Value();

  data = new PortAudioData();
  data->readIdx = 0;
  data->writeIdx = 1;
  data->channelCount = outputParameters.channelCount;
  data->sampleFormat = outputParameters.sampleFormat;

  v8Stream = g_streamConstructor->NewInstance();
  v8Stream->SetPointerInInternalField(0, data);
  v8Val = options->Get(v8::String::New("streamInit"));
  initArgs[0] = v8Stream;
  v8::Function::Cast(*v8Val)->Call(v8Stream, 1, initArgs);
  data->v8Stream = v8::Persistent<v8::Object>::New(v8Stream);
  data->v8Stream.MakeWeak(data, CleanupStreamData);
  data->v8Stream.MarkIndependent();

  v8Buffer = v8Stream->Get(v8::String::New("buffer"))->ToObject();
  data->buffer = (unsigned char*)node::Buffer::Data(v8Buffer);
  data->bufferLen = node::Buffer::Length(v8Buffer);

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
    sprintf(str, "Could not open stream %d", err);
    argv[0] = v8::Exception::TypeError(v8::String::New(str));
    goto openDone;
  }

  v8Stream->Set(v8::String::New("write"), v8::FunctionTemplate::New(stream_write)->GetFunction());
  v8Stream->Set(v8::String::New("writeByte"), v8::FunctionTemplate::New(stream_writeByte)->GetFunction());
  v8Stream->Set(v8::String::New("start"), v8::FunctionTemplate::New(stream_start)->GetFunction());
  v8Stream->Set(v8::String::New("stop"), v8::FunctionTemplate::New(stream_stop)->GetFunction());

  argv[1] = v8Stream;

openDone:
  v8::Function::Cast(*callback)->Call(v8::Context::GetCurrent()->Global(), 2, argv);
  return scope.Close(v8::Undefined());
}

v8::Handle<v8::Value> GetDevices(const v8::Arguments& args) {
  v8::HandleScope scope;
  char str[1000];
  int numDevices;
  v8::Local<v8::Array> result;

  v8::Handle<v8::Value> argv[2];
  argv[0] = v8::Undefined();
  argv[1] = v8::Undefined();

  // callback
  if(!args[0]->IsFunction()) {
    return scope.Close(v8::ThrowException(v8::Exception::TypeError(v8::String::New("First argument must be a function"))));
  }
  v8::Local<v8::Value> callback = args[0];

  PaError err = EnsureInitialized();
  if(err != paNoError) {
    sprintf(str, "Could not initialize PortAudio %d", err);
    argv[0] = v8::Exception::TypeError(v8::String::New(str));
    goto getDevicesDone;
  }

  numDevices = Pa_GetDeviceCount();
  result = v8::Array::New(numDevices);
  argv[1] = result;
  printf("numDevices %d\n", numDevices);
  for(int i = 0; i < numDevices; i++) {
    const PaDeviceInfo* deviceInfo = Pa_GetDeviceInfo(i);
    v8::Local<v8::Object> v8DeviceInfo = v8::Object::New();
    v8DeviceInfo->Set(v8::String::New("id"), v8::Integer::New(i));
    v8DeviceInfo->Set(v8::String::New("name"), v8::String::New(deviceInfo->name));
    result->Set(i, v8DeviceInfo);
  }

getDevicesDone:
  v8::Function::Cast(*callback)->Call(v8::Context::GetCurrent()->Global(), 2, argv);
  return scope.Close(v8::Undefined());
}

void CleanupStreamData(v8::Persistent<v8::Value> obj, void *parameter) {
  PortAudioData* data = (PortAudioData*)parameter;
  data->v8Stream->SetPointerInInternalField(0, NULL);
  delete data;
}

#define STREAM_DATA \
  PortAudioData* data = (PortAudioData*)args.This()->GetPointerFromInternalField(0); \
  if(data == NULL) { \
    return scope.Close(v8::Undefined()); \
  }

#define EMIT_BUFFER_OVERRUN \
  v8::Handle<v8::Value> emitArgs[1]; \
  emitArgs[0] = v8::String::New("overrun"); \
  v8::Function::Cast(*args.This()->Get(v8::String::New("emit")))->Call(args.This(), 1, emitArgs);

v8::Handle<v8::Value> stream_stop(const v8::Arguments& args) {
  v8::HandleScope scope;
  STREAM_DATA;

  PaError err = Pa_CloseStream(data->stream);
  if(err != paNoError) {
    char str[1000];
    sprintf(str, "Could not start stream %d", err);
    return scope.Close(v8::ThrowException(v8::Exception::TypeError(v8::String::New(str))));
  }

  return scope.Close(v8::Undefined());
}

v8::Handle<v8::Value> stream_start(const v8::Arguments& args) {
  v8::HandleScope scope;
  STREAM_DATA;

  PaError err = Pa_StartStream(data->stream);
  if(err != paNoError) {
    char str[1000];
    sprintf(str, "Could not start stream %d", err);
    return scope.Close(v8::ThrowException(v8::Exception::TypeError(v8::String::New(str))));
  }

  return scope.Close(v8::Undefined());
}

v8::Handle<v8::Value> stream_writeByte(const v8::Arguments& args) {
  v8::HandleScope scope;
  STREAM_DATA;

  if(data->writeIdx == data->readIdx) {
    EMIT_BUFFER_OVERRUN;
    return scope.Close(v8::Undefined());
  }

  int val = args[0]->ToInt32()->Value();
  data->buffer[data->writeIdx++] = val;
  if(data->writeIdx >= data->bufferLen) {
    data->writeIdx = 0;
  }

  return scope.Close(v8::Undefined());
}

v8::Handle<v8::Value> stream_write(const v8::Arguments& args) {
  v8::HandleScope scope;
  STREAM_DATA;

  v8::Local<v8::Object> buffer = args[0]->ToObject();
  int bufferLen = node::Buffer::Length(buffer);
  unsigned char* p = (unsigned char*)node::Buffer::Data(buffer);
  for(int i=0; i<bufferLen; i++) {
    if(data->writeIdx == data->readIdx) {
      EMIT_BUFFER_OVERRUN;
      return scope.Close(v8::Undefined());
    }

    data->buffer[data->writeIdx++] = *p++;
    if(data->writeIdx >= data->bufferLen) {
      data->writeIdx = 0;
    }
  }

  return scope.Close(v8::Undefined());
}

void EIO_EmitUnderrun(uv_work_t* req) {

}

void EIO_EmitUnderrunAfter(uv_work_t* req) {
  v8::HandleScope scope;
  PortAudioData* request = (PortAudioData*)req->data;

  v8::Handle<v8::Value> emitArgs[1];
  emitArgs[0] = v8::String::New("underrun");
  v8::Function::Cast(*request->v8Stream->Get(v8::String::New("emit")))->Call(request->v8Stream, 1, emitArgs);
}

static int nodePortAudioCallback(
  const void *inputBuffer,
  void *outputBuffer,
  unsigned long framesPerBuffer,
  const PaStreamCallbackTimeInfo* timeInfo,
  PaStreamCallbackFlags statusFlags,
  void *userData)
{
  PortAudioData* data = (PortAudioData*)userData;
  unsigned long i;

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

  unsigned char* out = (unsigned char*)outputBuffer;
  for(i = 0; i < framesPerBuffer * multiplier; i++) {
    if(data->readIdx == data->writeIdx) {
      uv_work_t* req = new uv_work_t();
      req->data = data;
      uv_queue_work(uv_default_loop(), req, EIO_EmitUnderrun, (uv_after_work_cb) EIO_EmitUnderrunAfter);
      return paContinue;
    }
    *out++ = data->buffer[data->readIdx++];
    if(data->readIdx >= data->bufferLen) {
      data->readIdx = 0;
    }
  }

  return paContinue;
}
