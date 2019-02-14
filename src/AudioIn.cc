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
#include <map>
#include <portaudio.h>

namespace streampunk {

Napi::FunctionReference AudioIn::constructor;

static std::map<uint8_t*, std::shared_ptr<Memory> > sOutstandingAllocs;
static class AllocFinalizer {
public:
  void operator()(Napi::Env env, uint8_t* data) { printf("Finalize %p\n", data); sOutstandingAllocs.erase(data); }
} sAllocFinalizer;

class InContext {
public:
  InContext(Napi::Env env, std::shared_ptr<AudioOptions> audioOptions, PaStreamCallback *cb)
    : mAudioOptions(audioOptions), mChunkQueue(mAudioOptions->maxQueue()), mCurOffset(0) {

    PaError errCode = Pa_Initialize();
    if (errCode != paNoError) {
      std::string err = std::string("Could not initialize PortAudio: ") + Pa_GetErrorText(errCode);
      throw Napi::Error::New(env, err.c_str());
    }

    printf("%s\n", Pa_GetVersionInfo()->versionText);
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
    Pa_CloseStream(mStream);
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
    Pa_CloseStream(mStream);
    Pa_Terminate();
  }

  std::shared_ptr<Memory> readChunk(uint32_t numBytes) {
    std::shared_ptr<Memory> result = Memory::makeNew(numBytes);
    uint32_t bytesRead = fillChunk(result->buf(), numBytes);
    if (bytesRead != numBytes) {
      if (0 == bytesRead)
        result = std::shared_ptr<Memory>();
      else {
        std::shared_ptr<Memory> trimResult = Memory::makeNew(bytesRead);
        memcpy(trimResult->buf(), result->buf(), bytesRead);
        result = trimResult;
      }
    }

    return result;
  }

  bool readBuffer(const void *srcBuf, uint32_t frameCount) {
    uint32_t bytesAvailable = frameCount * mAudioOptions->channelCount() * mAudioOptions->sampleFormat() / 8;
    std::shared_ptr<Memory> dstBuf = Memory::makeNew(bytesAvailable);
    memcpy(dstBuf->buf(), srcBuf, bytesAvailable);
    mChunkQueue.enqueue(dstBuf);
    return true;
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
    mErrStr.clear();
    return !errStr.empty();
  }

  void quit() { mChunkQueue.quit(); }

private:
  std::shared_ptr<AudioOptions> mAudioOptions;
  ChunkQueue<std::shared_ptr<Memory> > mChunkQueue;
  PaStream* mStream;
  std::string mErrStr;
  std::shared_ptr<Memory> mCurChunk;
  uint32_t mCurOffset;
  mutable std::mutex m;

  uint32_t fillChunk(uint8_t *buf, uint32_t numBytes) {
    uint32_t bufOff = 0;
    while (numBytes) {
      if (!mCurChunk || (mCurChunk && (mCurChunk->numBytes() == mCurOffset))) {
        mCurChunk = mChunkQueue.dequeue();
        mCurOffset = 0;
        if (!mCurChunk)
          break;
      }

      uint32_t curBytes = std::min<uint32_t>(numBytes, mCurChunk->numBytes() - mCurOffset);
      void *srcBuf = mCurChunk->buf() + mCurOffset;
      memcpy(buf + bufOff, srcBuf, curBytes);

      bufOff += curBytes;
      mCurOffset += curBytes;
      numBytes -= curBytes;
    }

    return bufOff;
  }
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
    InWorker(std::shared_ptr<InContext> InContext, uint32_t numBytes, const Napi::Function& callback)
      : AsyncWorker(callback, "AudioIn"), mNumBytes(numBytes), mInContext(InContext)
    { }
    ~InWorker() {}

    void Execute() {
      mInChunk = mInContext->readChunk(mNumBytes);
    }

    void OnOK() {
      Napi::HandleScope scope(Env());

      std::string errStr;
      if (mInContext->getErrStr(errStr))
        Callback().Call({Napi::String::New(Env(), errStr), Napi::Buffer<uint8_t>::New(Env(),0)});
      else if (mInChunk) {
        sOutstandingAllocs.emplace(mInChunk->buf(), mInChunk);
        Napi::Object buf = Napi::Buffer<uint8_t>::New(Env(), mInChunk->buf(), mInChunk->numBytes(), sAllocFinalizer);
        Callback().Call({Env().Null(), buf});
      } else
        Callback().Call({Env().Null(), Env().Null()});
    }

  private:
    std::shared_ptr<InContext> mInContext;
    uint32_t mNumBytes;
    std::shared_ptr<Memory> mInChunk;
};

class QuitInWorker : public Napi::AsyncWorker {
  public:
    QuitInWorker(std::shared_ptr<InContext> InContext, const Napi::Function& callback)
      : AsyncWorker(callback, "AudioQuitIn"), mInContext(InContext)
    { }
    ~QuitInWorker() {}

    void Execute() {
      mInContext->stop();
      mInContext->quit();
    }

    void OnOK() {
      Napi::HandleScope scope(Env());
      Callback().Call({});
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

  uint32_t numBytes = info[0].As<Napi::Number>().Uint32Value();
  Napi::Function callback = info[1].As<Napi::Function>();

  InWorker *inWork = new InWorker(mInContext, numBytes, callback);
  inWork->Queue();
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
  QuitInWorker *quitWork = new QuitInWorker(mInContext, callback);
  quitWork->Queue();
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