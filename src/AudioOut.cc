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
#include "PaContext.h"
#include "Chunks.h"

namespace streampunk {

Napi::FunctionReference AudioOut::constructor;

class OutWorker : public Napi::AsyncWorker {
  public:
    OutWorker(std::shared_ptr<PaContext> paContext, std::shared_ptr<Chunk> chunk, const Napi::Function& callback)
      : AsyncWorker(callback, "AudioOut"), mPaContext(paContext), mChunk(chunk)
    { }
    ~OutWorker() {}

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

class QuitOutWorker : public Napi::AsyncWorker {
  public:
    QuitOutWorker(std::shared_ptr<PaContext> paContext, PaContext::eStopFlag stopFlag, const Napi::Function& callback)
      : AsyncWorker(callback, "AudioQuitOut"), mPaContext(paContext), mStopFlag(stopFlag)
    { }
    ~QuitOutWorker() {}

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

AudioOut::AudioOut(const Napi::CallbackInfo& info)
  : Napi::ObjectWrap<AudioOut>(info) {
  Napi::Env env = info.Env();

  if ((info.Length() != 1) || !info[0].IsObject())
    throw Napi::TypeError::New(env, "AudioOut constructor expects an options object argument");

  mPaContext = std::make_shared<PaContext>(env, Napi::Object::Object(), info[0].As<Napi::Object>());
}
AudioOut::~AudioOut() {}

Napi::Value AudioOut::Start(const Napi::CallbackInfo& info) {
  Napi::Env env = info.Env();
  mPaContext->start(env);
  return env.Undefined();
}

Napi::Value AudioOut::Write(const Napi::CallbackInfo& info) {
  Napi::Env env = info.Env();
  if (info.Length() != 2)
    throw Napi::Error::New(env, "AudioOut Write expects 2 arguments");
  if (!info[0].IsObject())
    throw Napi::TypeError::New(env, "AudioOut Write expects a valid chunk buffer as the first parameter");
  if (!info[1].IsFunction())
    throw Napi::TypeError::New(env, "AudioOut Write expects a valid callback as the second parameter");

  Napi::Object chunkObj = info[0].As<Napi::Object>();
  Napi::Function callback = info[1].As<Napi::Function>();

  OutWorker *outWork = new OutWorker(mPaContext, std::make_shared<Chunk>(chunkObj), callback);
  outWork->Queue();
  return env.Undefined();
}

Napi::Value AudioOut::Quit(const Napi::CallbackInfo& info) {
  Napi::Env env = info.Env();
  if (info.Length() != 2)
    throw Napi::Error::New(env, "AudioOut Quit expects 2 arguments");
  if (!info[0].IsString())
    throw Napi::TypeError::New(env, "AudioOut Quit expects a valid string as the first parameter");
  if (!info[1].IsFunction())
    throw Napi::TypeError::New(env, "AudioOut Quit expects a valid callback as the parameter");

  std::string stopFlagStr = info[0].As<Napi::String>().Utf8Value();
  if ((0 != stopFlagStr.compare("WAIT")) && (0 != stopFlagStr.compare("ABORT")))
    throw Napi::Error::New(env, "AudioOut Quit expects \'WAIT\' or \'ABORT\' as the first argument");
  PaContext::eStopFlag stopFlag = (0 == stopFlagStr.compare("WAIT")) ?
    PaContext::eStopFlag::WAIT : PaContext::eStopFlag::ABORT;

  Napi::Function callback = info[1].As<Napi::Function>();
  QuitOutWorker *quitWork = new QuitOutWorker(mPaContext, stopFlag, callback);
  quitWork->Queue();
  return env.Undefined();
}

void AudioOut::Init(Napi::Env env, Napi::Object exports) {
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