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

#include "AudioOut.h"
#include "Persist.h"
#include "Params.h"
#include "ChunkQueue.h"
#include <mutex>
#include <condition_variable>
#include <portaudio.h>

namespace streampunk {

Napi::FunctionReference AudioOut::constructor;

class AudioChunk {
public:
  AudioChunk (Napi::Object chunk)
    : mPersistentChunk(new Persist(chunk)),
      mChunk(Memory::makeNew((uint8_t *)chunk.As<Napi::Buffer<char>>().Data(), (uint32_t)chunk.As<Napi::Buffer<char>>().Length()))
    { }
  ~AudioChunk() { }
  
  std::shared_ptr<Memory> chunk() const { return mChunk; }

private:
  std::unique_ptr<Persist> mPersistentChunk;
  std::shared_ptr<Memory> mChunk;
};

class OutContext {
public:
  OutContext(Napi::Env env, std::shared_ptr<AudioOptions> audioOptions, PaStreamCallback *cb)
    : mAudioOptions(audioOptions), mChunkQueue(mAudioOptions->maxQueue()), 
      mCurOffset(0), mActive(true), mFinished(false) {

    PaError errCode = Pa_Initialize();
    if (errCode != paNoError) {
      std::string err = std::string("Could not initialize PortAudio: ") + Pa_GetErrorText(errCode);
      throw Napi::Error::New(env, err.c_str());
    }

    printf("Output %s\n", mAudioOptions->toString().c_str());

    PaStreamParameters outParams;
    memset(&outParams, 0, sizeof(PaStreamParameters));

    int32_t deviceID = (int32_t)mAudioOptions->deviceID();
    if ((deviceID >= 0) && (deviceID < Pa_GetDeviceCount()))
      outParams.device = (PaDeviceIndex)deviceID;
    else
      outParams.device = Pa_GetDefaultOutputDevice();
    if (outParams.device == paNoDevice)
      throw Napi::Error::New(env, "No default output device");

    printf("Output device name is %s\n", Pa_GetDeviceInfo(outParams.device)->name);

    outParams.channelCount = mAudioOptions->channelCount();
    if (outParams.channelCount > Pa_GetDeviceInfo(outParams.device)->maxOutputChannels)
      throw Napi::Error::New(env, "Channel count exceeds maximum number of output channels for device");


    uint32_t sampleFormat = mAudioOptions->sampleFormat();
    switch(sampleFormat) {
    case 8: outParams.sampleFormat = paInt8; break;
    case 16: outParams.sampleFormat = paInt16; break;
    case 24: outParams.sampleFormat = paInt24; break;
    case 32: outParams.sampleFormat = paInt32; break;
    default: throw Napi::Error::New(env, "Invalid sampleFormat");
    }

    outParams.suggestedLatency = Pa_GetDeviceInfo(outParams.device)->defaultLowOutputLatency;
    outParams.hostApiSpecificStreamInfo = NULL;

    double sampleRate = (double)mAudioOptions->sampleRate();
    uint32_t framesPerBuffer = paFramesPerBufferUnspecified;

    #ifdef __arm__
    framesPerBuffer = 256;
    outParams.suggestedLatency = Pa_GetDeviceInfo(outParams.device)->defaultHighOutputLatency;
    #endif

    errCode = Pa_OpenStream(&mStream, NULL, &outParams, sampleRate,
                            framesPerBuffer, paNoFlag, cb, this);
    if (errCode != paNoError) {
      std::string err = std::string("Could not open stream: ") + Pa_GetErrorText(errCode);
      throw Napi::Error::New(env, err.c_str());
    }
  }
  
  ~OutContext() {
    Pa_StopStream(mStream);
    Pa_Terminate();
  }

  void start(Napi::Env env) {
    PaError errCode = Pa_StartStream(mStream);
    if (errCode != paNoError) {
      std::string err = std::string("Could not start output stream: ") + Pa_GetErrorText(errCode);
      throw Napi::Error::New(env, err.c_str());
    }
  }

  void stop() {
    Pa_StopStream(mStream);
    Pa_Terminate();
  }

  void addChunk(std::shared_ptr<AudioChunk> audioChunk) {
    mChunkQueue.enqueue(audioChunk);
  }

  bool fillBuffer(void *buf, uint32_t frameCount) {
    uint8_t *dst = (uint8_t *)buf;
    uint32_t bytesRemaining = frameCount * mAudioOptions->channelCount() * mAudioOptions->sampleFormat() / 8;

    uint32_t active = isActive();
    if (!active && (0 == mChunkQueue.size()) && 
        (!mCurChunk || (mCurChunk && (bytesRemaining >= mCurChunk->chunk()->numBytes() - mCurOffset)))) {
      if (mCurChunk) {
        uint32_t bytesCopied = doCopy(mCurChunk->chunk(), dst, bytesRemaining);
        uint32_t missingBytes = bytesRemaining - bytesCopied;
        if (missingBytes > 0) {
          printf("Finishing - %d bytes not available for the last output buffer\n", missingBytes);
          memset(dst + bytesCopied, 0, missingBytes);
        }
      }
      std::lock_guard<std::mutex> lk(m);
      mFinished = true;
      cv.notify_one();
    } else {
      while (bytesRemaining) {
        if (!(mCurChunk && (mCurOffset < mCurChunk->chunk()->numBytes()))) {
          mCurChunk = mChunkQueue.dequeue();
          mCurOffset = 0;
        }
        if (mCurChunk) {
          uint32_t bytesCopied = doCopy(mCurChunk->chunk(), dst, bytesRemaining);
          bytesRemaining -= bytesCopied;
          dst += bytesCopied;
          mCurOffset += bytesCopied;
        } else { // Deal with termination case of ChunkQueue being kicked and returning null chunk
          std::lock_guard<std::mutex> lk(m);
          mFinished = true;
          cv.notify_one();
          break;
        }
      }
    }

    return !mFinished;
  }

  void checkStatus(uint32_t statusFlags) {
    if (statusFlags) {
      std::string err = std::string("portAudio status - ");
      if (statusFlags & paOutputUnderflow)
        err += "output underflow ";
      if (statusFlags & paOutputOverflow)
        err += "output overflow ";
      if (statusFlags & paPrimingOutput)
        err += "priming output ";

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
    while(!mFinished)
      cv.wait(lk);
  }

private:
  std::shared_ptr<AudioOptions> mAudioOptions;
  ChunkQueue<std::shared_ptr<AudioChunk> > mChunkQueue;
  std::shared_ptr<AudioChunk> mCurChunk;
  PaStream* mStream;
  uint32_t mCurOffset;
  bool mActive;
  bool mFinished;
  std::string mErrStr;
  mutable std::mutex m;
  std::condition_variable cv;

  bool isActive() const {
    std::unique_lock<std::mutex> lk(m);
    return mActive;
  }

  uint32_t doCopy(std::shared_ptr<Memory> chunk, void *dst, uint32_t numBytes) {
    uint32_t curChunkBytes = chunk->numBytes() - mCurOffset;
    uint32_t thisChunkBytes = std::min<uint32_t>(curChunkBytes, numBytes);
    memcpy(dst, chunk->buf() + mCurOffset, thisChunkBytes);
    return thisChunkBytes;
  }
};

int OutCallback(const void *input, void *output, unsigned long frameCount, 
               const PaStreamCallbackTimeInfo *timeInfo, 
               PaStreamCallbackFlags statusFlags, void *userData) {
  OutContext *context = (OutContext *)userData;
  context->checkStatus(statusFlags);
  return context->fillBuffer(output, frameCount) ? paContinue : paComplete;
}

class OutWorker : public Napi::AsyncWorker {
  public:
    OutWorker(std::shared_ptr<OutContext> OutContext, std::shared_ptr<AudioChunk> audioChunk, const Napi::Function& callback)
      : AsyncWorker(callback), mOutContext(OutContext), mAudioChunk(audioChunk) 
    { }
    ~OutWorker() {}

    void Execute() {
      mOutContext->addChunk(mAudioChunk);
    }

    void OnOK() {
      Napi::HandleScope scope(Env());
      std::string errStr;
      if (mOutContext->getErrStr(errStr)) {
        std::vector<napi_value> args;
        args.push_back(Napi::String::New(Env(), errStr.c_str()));
        Callback().Call(args);
      } else
        Callback().Call(std::vector<napi_value>());
    }

  private:
    std::shared_ptr<OutContext> mOutContext;
    std::shared_ptr<AudioChunk> mAudioChunk;
};

class QuitOutWorker : public Napi::AsyncWorker {
  public:
    QuitOutWorker(std::shared_ptr<OutContext> OutContext, const Napi::Function& callback)
      : AsyncWorker(callback), mOutContext(OutContext)
    { }
    ~QuitOutWorker() {}

    void Execute() {
      mOutContext->quit();
    }

    void OnOK() {
      Napi::HandleScope scope(Env());
      mOutContext->stop();
      Callback().Call(std::vector<napi_value>());
    }

  private:
    std::shared_ptr<OutContext> mOutContext;
};

AudioOut::AudioOut(const Napi::CallbackInfo& info)
  : Napi::ObjectWrap<AudioOut>(info) {
  Napi::Env env = info.Env();
  Napi::HandleScope scope(env);

  if ((info.Length() != 1) || !info[0].IsObject())
    throw Napi::TypeError::New(env, "AudioOut constructor expects an options object argument");

  Napi::Object options = info[0].As<Napi::Object>();
  mOutContext = std::make_shared<OutContext>(env, std::make_shared<AudioOptions>(env, options), OutCallback);
}
AudioOut::~AudioOut() {}

Napi::Value AudioOut::Start(const Napi::CallbackInfo& info) {
  Napi::Env env = info.Env();
  Napi::HandleScope scope(env);
  mOutContext->start(env);
  return env.Undefined();
}

Napi::Value AudioOut::Write(const Napi::CallbackInfo& info) {
  Napi::Env env = info.Env();
  Napi::HandleScope scope(env);
  if (info.Length() != 2)
    throw Napi::Error::New(env, "AudioOut Write expects 2 arguments");
  if (!info[0].IsObject())
    throw Napi::TypeError::New(env, "AudioOut Write expects a valid chunk buffer as the first parameter");
  if (!info[1].IsFunction())
    throw Napi::TypeError::New(env, "AudioOut Write expects a valid callback as the second parameter");

  Napi::Object chunkObj = info[0].As<Napi::Object>();
  Napi::Function callback = info[1].As<Napi::Function>();
  napi_queue_async_work(env, napi_async_work(*new OutWorker(mOutContext, std::make_shared<AudioChunk>(chunkObj), callback)));
  return env.Undefined();
}

Napi::Value AudioOut::Quit(const Napi::CallbackInfo& info) {
  Napi::Env env = info.Env();
  Napi::HandleScope scope(env);
  if (info.Length() != 1)
    throw Napi::Error::New(env, "AudioOut Quit expects 1 argument");
  if (!info[0].IsFunction())
    throw Napi::TypeError::New(env, "AudioOut Quit expects a valid callback as the parameter");

  Napi::Function callback = info[0].As<Napi::Function>();
  napi_queue_async_work(env, napi_async_work(*new QuitOutWorker(mOutContext, callback)));
  return env.Undefined();
}

void AudioOut::Init(Napi::Env env, Napi::Object exports) {
  Napi::HandleScope scope(env);

  Napi::Function func = DefineClass(env, "AudioOut", {
    InstanceMethod("start", &AudioOut::Start),
    InstanceMethod("write", &AudioOut::Write),
    InstanceMethod("quit", &AudioOut::Quit)
  });

  constructor = Napi::Persistent(func);
  constructor.SuppressDestruct();

  exports.Set("AudioOut", func);
}

} // namespace streampunk