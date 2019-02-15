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
#include "PaContext.h"
#include "Chunks.h"
#include <map>

namespace streampunk {

Napi::FunctionReference AudioIn::constructor;

static std::map<uint8_t*, std::shared_ptr<Chunk> > sOutstandingAllocs;
static class AllocFinalizer {
public:
  void operator()(Napi::Env env, uint8_t* data) { sOutstandingAllocs.erase(data); }
} sAllocFinalizer;

class InWorker : public Napi::AsyncWorker {
  public:
    InWorker(std::shared_ptr<PaContext> paContext, uint32_t numBytes, const Napi::Function& callback)
      : AsyncWorker(callback, "AudioIn"), mNumBytes(numBytes), mPaContext(paContext)
    { }
    ~InWorker() {}

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

class QuitInWorker : public Napi::AsyncWorker {
  public:
    QuitInWorker(std::shared_ptr<PaContext> paContext, PaContext::eStopFlag stopFlag, const Napi::Function& callback)
      : AsyncWorker(callback, "AudioQuitIn"), mPaContext(paContext), mStopFlag(stopFlag)
    { }
    ~QuitInWorker() {}

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

AudioIn::AudioIn(const Napi::CallbackInfo& info) 
  : Napi::ObjectWrap<AudioIn>(info) {
  Napi::Env env = info.Env();

  if ((info.Length() != 1) || !info[0].IsObject())
    throw Napi::Error::New(env, "AudioIn constructor expects an options object argument");

  mPaContext = std::make_shared<PaContext>(env, info[0].As<Napi::Object>(), Napi::Object::Object());
}
AudioIn::~AudioIn() {}

Napi::Value AudioIn::Start(const Napi::CallbackInfo& info) {
  Napi::Env env = info.Env();
  mPaContext->start(env);
  return env.Undefined();
}

Napi::Value AudioIn::Read(const Napi::CallbackInfo& info) {
  Napi::Env env = info.Env();
  if (info.Length() != 2)
    throw Napi::Error::New(env, "AudioIn Read expects 2 arguments");
  if (!info[0].IsNumber())
    throw Napi::TypeError::New(env, "AudioIn Read expects a valid advisory size as the first parameter");
  if (!info[1].IsFunction())
    throw Napi::TypeError::New(env, "AudioIn Read expects a valid callback as the second parameter");

  uint32_t numBytes = info[0].As<Napi::Number>().Uint32Value();
  Napi::Function callback = info[1].As<Napi::Function>();

  InWorker *inWork = new InWorker(mPaContext, numBytes, callback);
  inWork->Queue();
  return env.Undefined();
}

Napi::Value AudioIn::Quit(const Napi::CallbackInfo& info) {
  Napi::Env env = info.Env();
  if (info.Length() != 2)
    throw Napi::Error::New(env, "AudioIn Quit expects 2 arguments");
  if (!info[0].IsString())
    throw Napi::TypeError::New(env, "AudioIn Quit expects a valid string as the first parameter");
  if (!info[1].IsFunction())
    throw Napi::TypeError::New(env, "AudioIn Quit expects a valid callback as the second parameter");

  std::string stopFlagStr = info[0].As<Napi::String>().Utf8Value();
  if ((0 != stopFlagStr.compare("WAIT")) && (0 != stopFlagStr.compare("ABORT")))
    throw Napi::Error::New(env, "AudioIn Quit expects \'WAIT\' or \'ABORT\' as the first argument");
  PaContext::eStopFlag stopFlag = (0 == stopFlagStr.compare("WAIT")) ? 
    PaContext::eStopFlag::WAIT : PaContext::eStopFlag::ABORT;

  Napi::Function callback = info[1].As<Napi::Function>();
  QuitInWorker *quitWork = new QuitInWorker(mPaContext, stopFlag, callback);
  quitWork->Queue();
  return env.Undefined();
}

void AudioIn::Init(Napi::Env env, Napi::Object exports) {
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