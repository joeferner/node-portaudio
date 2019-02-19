/* Copyright 2019 Streampunk Media Ltd.

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

#include "AudioIO.h"
#include "PaContext.h"
#include "Chunks.h"
#include <map>

namespace streampunk {

Napi::FunctionReference AudioIO::constructor;

static std::map<uint8_t*, std::shared_ptr<Chunk> > sOutstandingAllocs;
static class AllocFinalizer {
public:
  void operator()(Napi::Env env, uint8_t* data) { sOutstandingAllocs.erase(data); }
} sAllocFinalizer;

class ReadWorker : public Napi::AsyncWorker {
  public:
    ReadWorker(std::shared_ptr<PaContext> paContext, uint32_t numBytes, const Napi::Function& callback)
      : AsyncWorker(callback, "AudioRead"), mPaContext(paContext), mNumBytes(numBytes)
    { }
    ~ReadWorker() {}

    void Execute() {
      mChunk = mPaContext->pullInChunk(mNumBytes);
    }

    void OnOK() {
      Napi::HandleScope scope(Env());

      std::string errStr;
      if (mPaContext->getErrStr(errStr))
        Callback().Call({Napi::String::New(Env(), errStr), Napi::Buffer<uint8_t>::New(Env(),0)});
      else if (mChunk) {
        sOutstandingAllocs.emplace(mChunk->buf(), mChunk);
        Napi::Object buf = Napi::Buffer<uint8_t>::New(Env(), mChunk->buf(), mChunk->numBytes(), sAllocFinalizer);
        Callback().Call({Env().Null(), buf});
      } else
        Callback().Call({Env().Null(), Env().Null()});
    }

  private:
    std::shared_ptr<PaContext> mPaContext;
    uint32_t mNumBytes;
    std::shared_ptr<Chunk> mChunk;
};

class WriteWorker : public Napi::AsyncWorker {
  public:
    WriteWorker(std::shared_ptr<PaContext> paContext, std::shared_ptr<Chunk> chunk, const Napi::Function& callback)
      : AsyncWorker(callback, "AudioWrite"), mPaContext(paContext), mChunk(chunk)
    { }
    ~WriteWorker() {}

    void Execute() {
      mPaContext->pushOutChunk(mChunk);
    }

    void OnOK() {
      Napi::HandleScope scope(Env());
      std::string errStr;
      if (mPaContext->getErrStr(errStr))
        Callback().Call({Napi::String::New(Env(), errStr)});
      else
        Callback().Call({Env().Null()});
    }

  private:
    std::shared_ptr<PaContext> mPaContext;
    std::shared_ptr<Chunk> mChunk;
};

class QuitWorker : public Napi::AsyncWorker {
  public:
    QuitWorker(std::shared_ptr<PaContext> paContext, PaContext::eStopFlag stopFlag, const Napi::Function& callback)
      : AsyncWorker(callback, "AudioIOQuit"), mPaContext(paContext), mStopFlag(stopFlag)
    { }
    ~QuitWorker() {}

    void Execute() {
      mPaContext->stop(mStopFlag);
      mPaContext->quit();
    }

    void OnOK() {
      Napi::HandleScope scope(Env());
      Callback().Call({});
    }

  private:
    std::shared_ptr<PaContext> mPaContext;
    const PaContext::eStopFlag mStopFlag;
};

AudioIO::AudioIO(const Napi::CallbackInfo& info) 
  : Napi::ObjectWrap<AudioIO>(info) {
  Napi::Env env = info.Env();

  if ((info.Length() != 1) || !info[0].IsObject())
    throw Napi::Error::New(env, "AudioIO constructor expects an options object argument");
  
  Napi::Object optionsObj = info[0].As<Napi::Object>();
  Napi::Object inOptions = Napi::Object();
  Napi::Object outOptions = Napi::Object();
  if (optionsObj.Has("inOptions"))
    inOptions = optionsObj.Get("inOptions").As<Napi::Object>();
  if (optionsObj.Has("outOptions"))
    outOptions = optionsObj.Get("outOptions").As<Napi::Object>();

  if (inOptions.IsEmpty() && outOptions.IsEmpty())
    throw Napi::Error::New(env, "AudioIO constructor expects an inOptions and/or an outOptions object argument");

  mPaContext = std::make_shared<PaContext>(env, inOptions, outOptions);
}
AudioIO::~AudioIO() {}

Napi::Value AudioIO::Start(const Napi::CallbackInfo& info) {
  Napi::Env env = info.Env();
  mPaContext->start(env);
  return env.Undefined();
}

Napi::Value AudioIO::Read(const Napi::CallbackInfo& info) {
  Napi::Env env = info.Env();
  if (info.Length() != 2)
    throw Napi::Error::New(env, "AudioIO Read expects 2 arguments");
  if (!info[0].IsNumber())
    throw Napi::TypeError::New(env, "AudioIO Read expects a valid advisory size as the first parameter");
  if (!info[1].IsFunction())
    throw Napi::TypeError::New(env, "AudioIO Read expects a valid callback as the second parameter");

  if (!mPaContext->hasInput())
    throw Napi::Error::New(env, "AudioIO Read - cannot read from a output-only stream");

  uint32_t numBytes = info[0].As<Napi::Number>().Uint32Value();
  Napi::Function callback = info[1].As<Napi::Function>();

  ReadWorker *readWork = new ReadWorker(mPaContext, numBytes, callback);
  readWork->Queue();
  return env.Undefined();
}

Napi::Value AudioIO::Write(const Napi::CallbackInfo& info) {
  Napi::Env env = info.Env();
  if (info.Length() != 2)
    throw Napi::Error::New(env, "AudioIO Write expects 2 arguments");
  if (!info[0].IsObject())
    throw Napi::TypeError::New(env, "AudioIO Write expects a valid chunk buffer as the first parameter");
  if (!info[1].IsFunction())
    throw Napi::TypeError::New(env, "AudioIO Write expects a valid callback as the second parameter");

  if (!mPaContext->hasOutput())
    throw Napi::Error::New(env, "AudioIO Write - cannot write to an input-only stream");

  Napi::Object chunkObj = info[0].As<Napi::Object>();
  Napi::Function callback = info[1].As<Napi::Function>();

  WriteWorker *writeWork = new WriteWorker(mPaContext, std::make_shared<Chunk>(chunkObj), callback);
  writeWork->Queue();
  return env.Undefined();
}

Napi::Value AudioIO::Quit(const Napi::CallbackInfo& info) {
  Napi::Env env = info.Env();
  if (info.Length() != 2)
    throw Napi::Error::New(env, "AudioIO Quit expects 2 arguments");
  if (!info[0].IsString())
    throw Napi::TypeError::New(env, "AudioIO Quit expects a valid string as the first parameter");
  if (!info[1].IsFunction())
    throw Napi::TypeError::New(env, "AudioIO Quit expects a valid callback as the second parameter");

  std::string stopFlagStr = info[0].As<Napi::String>().Utf8Value();
  if ((0 != stopFlagStr.compare("WAIT")) && (0 != stopFlagStr.compare("ABORT")))
    throw Napi::Error::New(env, "AudioIO Quit expects \'WAIT\' or \'ABORT\' as the first argument");
  PaContext::eStopFlag stopFlag = (0 == stopFlagStr.compare("WAIT")) ? 
    PaContext::eStopFlag::WAIT : PaContext::eStopFlag::ABORT;

  Napi::Function callback = info[1].As<Napi::Function>();
  QuitWorker *quitWork = new QuitWorker(mPaContext, stopFlag, callback);
  quitWork->Queue();
  return env.Undefined();
}

void AudioIO::Init(Napi::Env env, Napi::Object exports) {
  Napi::Function func = DefineClass(env, "AudioIO", {
    InstanceMethod("start", &AudioIO::Start),
    InstanceMethod("read", &AudioIO::Read),
    InstanceMethod("write", &AudioIO::Write),
    InstanceMethod("quit", &AudioIO::Quit)
  });

  constructor = Napi::Persistent(func);
  constructor.SuppressDestruct();

  exports.Set("AudioIO", func);
}

} // namespace streampunk