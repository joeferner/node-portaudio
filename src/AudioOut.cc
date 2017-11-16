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
#include <sstream>
#include <queue>
#include <mutex>
#include <condition_variable>
#include "AudioOut.h"
#include "Persist.h"
#include "Params.h"
#include <portaudio.h>

using namespace v8;

namespace streampunk {

template <class T>
class ChunkQueue {
public:
  ChunkQueue(uint32_t maxQueue) : mMaxQueue(maxQueue), qu(), m(), cv() {}
  ~ChunkQueue() {}
  
  void enqueue(T t) {
    std::unique_lock<std::mutex> lk(m);
    while(qu.size() >= mMaxQueue) {
      cv.wait(lk);
    }
    qu.push(t);
    cv.notify_one();
  }
  
  T dequeue() {
    std::unique_lock<std::mutex> lk(m);
    while(qu.empty()) {
      cv.wait(lk);
    }
    T val = qu.front();
    qu.pop();
    cv.notify_one();
    return val;
  }

  size_t size() const {
    std::lock_guard<std::mutex> lk(m);
    return qu.size();
  }

private:
  uint32_t mMaxQueue;
  std::queue<T> qu;
  mutable std::mutex m;
  std::condition_variable cv;
};

class AudioChunk {
public:
  AudioChunk (Local<Object> chunk)
    : mPersistentChunk(new Persist(chunk)),
      mChunk(Memory::makeNew((uint8_t *)node::Buffer::Data(chunk), (uint32_t)node::Buffer::Length(chunk)))
    { }
  ~AudioChunk() { }
  
  std::shared_ptr<Memory> chunk() const { return mChunk; }

private:
  std::unique_ptr<Persist> mPersistentChunk;
  std::shared_ptr<Memory> mChunk;
};

class AudioOptions : public Params {
public:
  AudioOptions(Local<Object> tags)
    : mDeviceID(unpackNum(tags, "deviceId", 0xffffffff)),
      mSampleRate(unpackNum(tags, "sampleRate", 48000)),
      mChannelCount(unpackNum(tags, "channelCount", 2)),
      mSampleFormat(unpackNum(tags, "sampleFormat", 24))
  {}
  ~AudioOptions() {}

  uint32_t deviceID() const  { return mDeviceID; }
  uint32_t sampleRate() const  { return mSampleRate; }
  uint32_t channelCount() const  { return mChannelCount; }
  uint32_t sampleFormat() const  { return mSampleFormat; }

  std::string toString() const  { 
    std::stringstream ss;
    ss << "Audio output options: ";
    if (mDeviceID != 0xffffffff)
      ss << "device " << std::hex << mDeviceID << std::dec << ", ";
    ss << "sample rate " << mSampleRate << ", ";
    ss << "channels " << mChannelCount << ", ";
    ss << "bits per sample " << mSampleFormat;
    return ss.str();
  }

private:
  uint32_t mDeviceID;
  uint32_t mSampleRate;
  uint32_t mChannelCount;
  uint32_t mSampleFormat;
};

class PaContext {
public:
  PaContext(std::shared_ptr<AudioOptions> audioOptions, PaStreamCallback *cb, uint32_t maxQueue)
    : mAudioOptions(audioOptions), mChunkQueue(maxQueue), mCurOffset(0), mActive(true), mFinished(false) {

    PaError errCode = Pa_Initialize();
    if (errCode != paNoError) {
      std::string err = std::string("Could not initialize PortAudio: ") + Pa_GetErrorText(errCode);
      Nan::ThrowError(err.c_str());
    }

    PaStreamParameters outParams;
    memset(&outParams, 0, sizeof(PaStreamParameters));
    int32_t deviceID = (int32_t)mAudioOptions->deviceID();
    if ((deviceID >= 0) && (deviceID < Pa_GetDeviceCount())) {
      outParams.device = (PaDeviceIndex)deviceID;
    } else {
      outParams.device = Pa_GetDefaultOutputDevice();
    }
    if (outParams.device == paNoDevice) {
      Nan::ThrowError("No default output device");
    }
    printf("Output device name is %s\n", Pa_GetDeviceInfo(outParams.device)->name);

    outParams.channelCount = mAudioOptions->channelCount();
    if (outParams.channelCount > Pa_GetDeviceInfo(outParams.device)->maxOutputChannels) {
      Nan::ThrowError("Channel count exceeds maximum number of output channels for device");
    }

    uint32_t sampleFormat = mAudioOptions->sampleFormat();
    switch(sampleFormat) {
    case 8:
      outParams.sampleFormat = paInt8;
      break;
    case 16:
      outParams.sampleFormat = paInt16;
      break;
    case 24:
      outParams.sampleFormat = paInt24;
      break;
    case 32:
      outParams.sampleFormat = paInt32;
      break;
    default:
      Nan::ThrowError("Invalid sampleFormat");
    }

    outParams.suggestedLatency = Pa_GetDeviceInfo(outParams.device)->defaultLowOutputLatency;
    outParams.hostApiSpecificStreamInfo = NULL;

    double sampleRate = (double)mAudioOptions->sampleRate();

    errCode = Pa_OpenStream(&mStream, NULL, &outParams, sampleRate,
                            paFramesPerBufferUnspecified, paNoFlag, cb, this);
    if (errCode != paNoError) {
      std::string err = std::string("Could not open stream: ") + Pa_GetErrorText(errCode);
      Nan::ThrowError(err.c_str());
    }
  }
  
  ~PaContext() {
    Pa_CloseStream(mStream);
    Pa_Terminate();
  }

  void start() {
    PaError errCode = Pa_StartStream(mStream);
    if (errCode != paNoError) {
      std::string err = std::string("Could not start output stream: ") + Pa_GetErrorText(errCode);
      return Nan::ThrowError(err.c_str());
    }
  }

  void addChunk(std::shared_ptr<AudioChunk> audioChunk) {
    mChunkQueue.enqueue(audioChunk);
  }

  bool fillBuffer(void *buf, uint32_t frameCount) {
    uint8_t *dst = (uint8_t *)buf;
    uint32_t bytesRemaining = frameCount * mAudioOptions->channelCount() * mAudioOptions->sampleFormat() / 8;

    if (!mActive && !mCurChunk)
      return false;

    if (!mActive && (0 == mChunkQueue.size()) && (bytesRemaining >= mCurChunk->chunk()->numBytes() - mCurOffset)) {
      uint32_t bytesCopied = doCopy(mCurChunk->chunk(), dst, bytesRemaining);
      if (bytesRemaining - bytesCopied > 0)
        printf("Finishing - %d bytes not available for the last output buffer\n", bytesRemaining - bytesCopied);

      std::lock_guard<std::mutex> lk(m);
      mFinished = true;
      cv.notify_one();
    } else {
      while (bytesRemaining) {
        if (!(mCurChunk && (mCurOffset < mCurChunk->chunk()->numBytes()))) {
          mCurChunk = mChunkQueue.dequeue();
          mCurOffset = 0;
        }
        uint32_t bytesCopied = doCopy(mCurChunk->chunk(), dst, bytesRemaining);
        bytesRemaining -= bytesCopied;
        dst += bytesCopied;
        mCurOffset += bytesCopied;
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
    errStr = mErrStr.c_str();
    mErrStr = std::string();
    return errStr != std::string();
  }

  void quit() {
    mActive = false;
    std::unique_lock<std::mutex> lk(m);
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
  std::mutex m;
  std::condition_variable cv;

  uint32_t doCopy(std::shared_ptr<Memory> chunk, void *dst, uint32_t numBytes) {
    uint32_t curChunkBytes = chunk->numBytes() - mCurOffset;
    uint32_t thisChunkBytes = std::min<uint32_t>(curChunkBytes, numBytes);
    memcpy(dst, chunk->buf() + mCurOffset, thisChunkBytes);
    return thisChunkBytes;
  }
};

int paCallback(const void *input, void *output, unsigned long frameCount, 
               const PaStreamCallbackTimeInfo *timeInfo, 
               PaStreamCallbackFlags statusFlags, void *userData) {
  PaContext *context = (PaContext *)userData;
  context->checkStatus(statusFlags);
  return context->fillBuffer(output, frameCount) ? paContinue : paComplete;
}

class OutWorker : public Nan::AsyncWorker {
  public:
    OutWorker(std::shared_ptr<PaContext> paContext, Nan::Callback *callback, std::shared_ptr<AudioChunk> audioChunk)
      : AsyncWorker(callback),
        mPaContext(paContext),
        mAudioChunk(audioChunk)
      { }
    ~OutWorker() {}

    void Execute() {
      mPaContext->addChunk(mAudioChunk);
    }

    void HandleOKCallback () {
      Nan::HandleScope scope;
      std::string errStr;
      if (mPaContext->getErrStr(errStr)) {
        Local<Value> argv[] = { Nan::Error(errStr.c_str()) };
        callback->Call(1, argv);
      } else {
        callback->Call(0, NULL);
      }
    }

  private:
    std::shared_ptr<PaContext> mPaContext;
    std::shared_ptr<AudioChunk> mAudioChunk;
};

class QuitWorker : public Nan::AsyncWorker {
  public:
    QuitWorker(std::shared_ptr<PaContext> paContext, Nan::Callback *callback)
      : AsyncWorker(callback),
        mPaContext(paContext)
      { }
    ~QuitWorker() {}

    void Execute() {
      mPaContext->quit();
    }

    void HandleOKCallback () {
      Nan::HandleScope scope;
      callback->Call(0, NULL);
    }

  private:
    std::shared_ptr<PaContext> mPaContext;
};

AudioOut::AudioOut(Local<Object> options) { 
  std::shared_ptr<AudioOptions> audioOptions = std::make_shared<AudioOptions>(options); 
  printf("%s\n", audioOptions->toString().c_str());

  mPaContext = std::make_shared<PaContext>(audioOptions, paCallback, 10);
}
AudioOut::~AudioOut() {}

NAN_METHOD(AudioOut::Start) {
  AudioOut* obj = Nan::ObjectWrap::Unwrap<AudioOut>(info.Holder());

  obj->mPaContext->start();
  info.GetReturnValue().SetUndefined();
}

NAN_METHOD(AudioOut::Write) {
  if (info.Length() != 2)
    return Nan::ThrowError("AudioOut Write expects 2 arguments");
  if (!info[0]->IsObject())
    return Nan::ThrowError("AudioOut Write requires a valid chunk buffer as the first parameter");
  if (!info[1]->IsFunction())
    return Nan::ThrowError("AudioOut Write requires a valid callback as the second parameter");

  Local<Object> chunkObj = Local<Object>::Cast(info[0]);
  Nan::Callback *callback = new Nan::Callback(Local<Function>::Cast(info[1]));
  AudioOut* obj = Nan::ObjectWrap::Unwrap<AudioOut>(info.Holder());

  AsyncQueueWorker(new OutWorker(obj->mPaContext, callback, std::make_shared<AudioChunk>(chunkObj)));
  info.GetReturnValue().SetUndefined();
}

NAN_METHOD(AudioOut::Quit) {
  if (info.Length() != 1)
    return Nan::ThrowError("AudioOut Quit expects 1 argument");
  if (!info[0]->IsFunction())
    return Nan::ThrowError("AudioOut Quit requires a valid callback as the parameter");

  Nan::Callback *callback = new Nan::Callback(Local<Function>::Cast(info[0]));
  AudioOut* obj = Nan::ObjectWrap::Unwrap<AudioOut>(info.Holder());

  AsyncQueueWorker(new QuitWorker(obj->mPaContext, callback));
  obj->mPaContext.reset();
  info.GetReturnValue().SetUndefined();
}

NAN_MODULE_INIT(AudioOut::Init) {
  Local<FunctionTemplate> tpl = Nan::New<FunctionTemplate>(New);
  tpl->SetClassName(Nan::New("AudioOut").ToLocalChecked());
  tpl->InstanceTemplate()->SetInternalFieldCount(1);

  SetPrototypeMethod(tpl, "start", Start);
  SetPrototypeMethod(tpl, "write", Write);
  SetPrototypeMethod(tpl, "quit", Quit);

  constructor().Reset(Nan::GetFunction(tpl).ToLocalChecked());
  Nan::Set(target, Nan::New("AudioOut").ToLocalChecked(),
    Nan::GetFunction(tpl).ToLocalChecked());
}

} // namespace streampunk