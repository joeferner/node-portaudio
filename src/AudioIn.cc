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

#include "AudioIn.h"
#include "Persist.h"
#include "Params.h"
#include "ChunkQueue.h"
#include <mutex>
#include <condition_variable>
#include <map>
#include <portaudio.h>

namespace streampunk {

Napi::FunctionReference AudioIn::constructor;

static std::map<char*, std::shared_ptr<Memory> > outstandingAllocs;
static void freeAllocCb(Napi::Env env, char* data) {
  std::map<char*, std::shared_ptr<Memory> >::iterator it = outstandingAllocs.find(data);
  if (it != outstandingAllocs.end())
    outstandingAllocs.erase(it);
}

class InContext {
public:
  InContext(Napi::Env env, std::shared_ptr<AudioOptions> audioOptions, PaStreamCallback *cb)
    : mActive(true), mAudioOptions(audioOptions), mChunkQueue(mAudioOptions->maxQueue()) {

    PaError errCode = Pa_Initialize();
    if (errCode != paNoError) {
      std::string err = std::string("Could not initialize PortAudio: ") + Pa_GetErrorText(errCode);
      throw Napi::Error::New(env, err.c_str());
    }

    printf("Input %s\n", mAudioOptions->toString().c_str());

    PaStreamParameters inParams;
    memset(&inParams, 0, sizeof(PaStreamParameters));

    int32_t deviceID = (int32_t)mAudioOptions->deviceID();
    if ((deviceID >= 0) && (deviceID < Pa_GetDeviceCount()))
      inParams.device = (PaDeviceIndex)deviceID;
    else
      inParams.device = Pa_GetDefaultInputDevice();
    if (inParams.device == paNoDevice)
      throw Napi::Error::New(env, "No default input device");

    printf("Input device name is %s\n", Pa_GetDeviceInfo(inParams.device)->name);

    inParams.channelCount = mAudioOptions->channelCount();
    if (inParams.channelCount > Pa_GetDeviceInfo(inParams.device)->maxInputChannels)
      throw Napi::Error::New(env, "Channel count exceeds maximum number of input channels for device");

    uint32_t sampleFormat = mAudioOptions->sampleFormat();
    switch(sampleFormat) {
    case 8: inParams.sampleFormat = paInt8; break;
    case 16: inParams.sampleFormat = paInt16; break;
    case 24: inParams.sampleFormat = paInt24; break;
    case 32: inParams.sampleFormat = paInt32; break;
    default: throw Napi::Error::New(env, "Invalid sampleFormat");
    }

    inParams.suggestedLatency = Pa_GetDeviceInfo(inParams.device)->defaultLowInputLatency;
    inParams.hostApiSpecificStreamInfo = NULL;

    double sampleRate = (double)mAudioOptions->sampleRate();
    uint32_t framesPerBuffer = paFramesPerBufferUnspecified;

    #ifdef __arm__
    framesPerBuffer = 256;
    inParams.suggestedLatency = Pa_GetDeviceInfo(inParams.device)->defaultHighInputLatency;
    #endif

    errCode = Pa_OpenStream(&mStream, &inParams, NULL, sampleRate,
                            framesPerBuffer, paNoFlag, cb, this);
    if (errCode != paNoError) {
      std::string err = std::string("Could not open stream: ") + Pa_GetErrorText(errCode);
      throw Napi::Error::New(env, err.c_str());
    }
  }
  
  ~InContext() {
    Pa_StopStream(mStream);
    Pa_Terminate();
  }

  void start(Napi::Env env) {
    PaError errCode = Pa_StartStream(mStream);
    if (errCode != paNoError) {
      std::string err = std::string("Could not start input stream: ") + Pa_GetErrorText(errCode);
      throw Napi::Error::New(env, err.c_str());
    }
  }

  void stop() {
    Pa_StopStream(mStream);
    Pa_Terminate();
  }

  std::shared_ptr<Memory> readChunk() {
    return mChunkQueue.dequeue();
  }

  bool readBuffer(const void *srcBuf, uint32_t frameCount) {
    const uint8_t *src = (uint8_t *)srcBuf;
    uint32_t bytesAvailable = frameCount * mAudioOptions->channelCount() * mAudioOptions->sampleFormat() / 8;
    std::shared_ptr<Memory> dstBuf = Memory::makeNew(bytesAvailable);
    memcpy(dstBuf->buf(), src, bytesAvailable);
    mChunkQueue.enqueue(dstBuf);
    return mActive;
  }

  void checkStatus(uint32_t statusFlags) {
    if (statusFlags) {
      std::string err = std::string("portAudio status - ");
      if (statusFlags & paInputUnderflow)
        err += "input underflow ";
      if (statusFlags & paInputOverflow)
        err += "input overflow ";

      std::lock_guard<std::mutex> lk(m);
      mErrStr = err;
    }
  }

  bool getErrStr(std::string& errStr) {
    std::lock_guard<std::mutex> lk(m);
    errStr = mErrStr;
    mErrStr = std::string();
    return errStr != std::string();
  }

  void quit() {
    std::unique_lock<std::mutex> lk(m);
    mActive = false;
    mChunkQueue.quit();
  }

private:
  bool mActive;
  std::shared_ptr<AudioOptions> mAudioOptions;
  ChunkQueue<std::shared_ptr<Memory> > mChunkQueue;
  PaStream* mStream;
  std::string mErrStr;
  mutable std::mutex m;
  std::condition_variable cv;
};

int InCallback(const void *input, void *output, unsigned long frameCount, 
               const PaStreamCallbackTimeInfo *timeInfo, 
               PaStreamCallbackFlags statusFlags, void *userData) {
  InContext *context = (InContext *)userData;
  context->checkStatus(statusFlags);
  return context->readBuffer(input, frameCount) ? paContinue : paComplete;
}

class InWorker : public Napi::AsyncWorker {
  public:
    InWorker(std::shared_ptr<InContext> InContext, const Napi::Function& callback)
      : AsyncWorker(callback), mInContext(InContext)
    { }
    ~InWorker() {}

    void Execute() {
      mInChunk = mInContext->readChunk();
    }

    void OnOK() {
      Napi::HandleScope scope(Env());

      std::string errStr;
      if (mInContext->getErrStr(errStr)) {
        std::vector<napi_value> args;
        args.push_back(Napi::String::New(Env(), errStr.c_str()));
        Callback().Call(args);
      }

      std::vector<napi_value> args;
      args.push_back(Env().Null());
      if (mInChunk) {
        outstandingAllocs.insert(make_pair((char*)mInChunk->buf(), mInChunk));
        Napi::Object buf = Napi::Buffer<char>::New(Env(), (char*)mInChunk->buf(), mInChunk->numBytes(), freeAllocCb);
        args.push_back(buf);
        Callback().Call(args);
      } else {
        args.push_back(Env().Null());
        Callback().Call(args);
      }
    }

  private:
    std::shared_ptr<InContext> mInContext;
    std::shared_ptr<Memory> mInChunk;
};

class QuitInWorker : public Napi::AsyncWorker {
  public:
    QuitInWorker(std::shared_ptr<InContext> InContext, const Napi::Function& callback)
      : AsyncWorker(callback), mInContext(InContext)
    { }
    ~QuitInWorker() {}

    void Execute() {
      mInContext->quit();
    }

    void OnOK() {
      Napi::HandleScope scope(Env());
      mInContext->stop();
      Callback().Call(std::vector<napi_value>());
    }

  private:
    std::shared_ptr<InContext> mInContext;
};

AudioIn::AudioIn(const Napi::CallbackInfo& info) 
  : Napi::ObjectWrap<AudioIn>(info) {
  Napi::Env env = info.Env();
  Napi::HandleScope scope(env);

  if ((info.Length() != 1) || !info[0].IsObject())
    throw Napi::Error::New(env, "AudioIn constructor expects an options object argument");

  Napi::Object options = info[0].As<Napi::Object>();
  mInContext = std::make_shared<InContext>(env, std::make_shared<AudioOptions>(env, options), InCallback);
}
AudioIn::~AudioIn() {}

Napi::Value AudioIn::Start(const Napi::CallbackInfo& info) {
  Napi::Env env = info.Env();
  Napi::HandleScope scope(env);
  mInContext->start(env);
  return env.Undefined();
}

Napi::Value AudioIn::Read(const Napi::CallbackInfo& info) {
  Napi::Env env = info.Env();
  Napi::HandleScope scope(env);
  if (info.Length() != 2)
    throw Napi::Error::New(env, "AudioIn Read expects 2 arguments");
  if (!info[0].IsNumber())
    throw Napi::TypeError::New(env, "AudioIn Read expects a valid advisory size as the first parameter");
  if (!info[1].IsFunction())
    throw Napi::TypeError::New(env, "AudioIn Read expects a valid callback as the second parameter");

  Napi::Function callback = info[1].As<Napi::Function>();
  napi_queue_async_work(env, napi_async_work(*new InWorker(mInContext, callback)));
  return env.Undefined();
}

Napi::Value AudioIn::Quit(const Napi::CallbackInfo& info) {
  Napi::Env env = info.Env();
  Napi::HandleScope scope(env);
  if (info.Length() != 1)
    throw Napi::Error::New(env, "AudioIn Quit expects 1 argument");
  if (!info[0].IsFunction())
    throw Napi::TypeError::New(env, "AudioIn Quit expects a valid callback as the parameter");

  Napi::Function callback = info[0].As<Napi::Function>();
  napi_queue_async_work(env, napi_async_work(*new QuitInWorker(mInContext, callback)));
  return env.Undefined();
}

void AudioIn::Init(Napi::Env env, Napi::Object exports) {
  Napi::HandleScope scope(env);

  Napi::Function func = DefineClass(env, "AudioIn", {
    InstanceMethod("start", &AudioIn::Start),
    InstanceMethod("read", &AudioIn::Read),
    InstanceMethod("quit", &AudioIn::Quit)
  });

  constructor = Napi::Persistent(func);
  constructor.SuppressDestruct();

  exports.Set("AudioIn", func);
}

} // namespace streampunk