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

#include "PaContext.h"
#include "Params.h"
#include "Chunks.h"
#include <portaudio.h>

namespace streampunk {

int PaCallback(const void *input, void *output, unsigned long frameCount, 
               const PaStreamCallbackTimeInfo *timeInfo, 
               PaStreamCallbackFlags statusFlags, void *userData) {
  PaContext *paContext = (PaContext *)userData;
  paContext->checkStatus(statusFlags);
  int inRetCode = paContext->hasInput() && paContext->readPaBuffer(input, frameCount) ? paContinue : paComplete;
  int outRetCode = paContext->hasOutput() && paContext->fillPaBuffer(output, frameCount) ? paContinue : paComplete;
  return ((inRetCode == paComplete) && (outRetCode == paComplete)) ? paComplete : paContinue;
}

PaContext::PaContext(Napi::Env env, Napi::Object inOptions, Napi::Object outOptions)
  : mInOptions(inOptions.IsEmpty() ? std::shared_ptr<AudioOptions>() : std::make_shared<AudioOptions>(env, inOptions)), 
    mOutOptions(outOptions.IsEmpty() ? std::shared_ptr<AudioOptions>() : std::make_shared<AudioOptions>(env, outOptions)),
    mInChunks(new Chunks(mInOptions ? mInOptions->maxQueue() : 0)),
    mOutChunks(new Chunks(mOutOptions ? mOutOptions->maxQueue() : 0)),
    mStream(nullptr) {

  PaError errCode = Pa_Initialize();
  if (errCode != paNoError) {
    std::string err = std::string("Could not initialize PortAudio: ") + Pa_GetErrorText(errCode);
    throw Napi::Error::New(env, err.c_str());
  }

  if (!mInOptions && !mOutOptions)
    throw Napi::Error::New(env, "Input and/or Output options must be specified");

  if (mInOptions && mOutOptions &&
      (mInOptions->sampleRate() != mOutOptions->sampleRate()))
    throw Napi::Error::New(env, "Input and Output sample rates must match");

  printf("%s\n", Pa_GetVersionInfo()->versionText);
  if (mInOptions)
    printf("Input %s\n", mInOptions->toString().c_str());
  if (mOutOptions)
    printf("Output %s\n", mOutOptions->toString().c_str());

  double sampleRate;
  PaStreamParameters inParams;
  memset(&inParams, 0, sizeof(PaStreamParameters));
  if (mInOptions)
    setParams(env, /*isInput*/true, mInOptions, inParams, sampleRate);

  PaStreamParameters outParams;
  memset(&outParams, 0, sizeof(PaStreamParameters));
  if (mOutOptions)
    setParams(env, /*isInput*/false, mOutOptions, outParams, sampleRate);

  uint32_t framesPerBuffer = paFramesPerBufferUnspecified;

  errCode = Pa_OpenStream(&mStream,
                          mInOptions ? &inParams : NULL,
                          mOutOptions ? &outParams : NULL,
                          sampleRate, framesPerBuffer,
                          paNoFlag, PaCallback, this);
  if (errCode != paNoError) {
    std::string err = std::string("Could not open stream: ") + Pa_GetErrorText(errCode);
    throw Napi::Error::New(env, err.c_str());
  }
}

PaContext::~PaContext() {
  Pa_AbortStream(mStream);
  Pa_CloseStream(mStream);
  Pa_Terminate();
}

void PaContext::start(Napi::Env env) {
  PaError errCode = Pa_StartStream(mStream);
  if (errCode != paNoError) {
    std::string err = std::string("Could not start stream: ") + Pa_GetErrorText(errCode);
    throw Napi::Error::New(env, err.c_str());
  }
}

void PaContext::stop(eStopFlag flag) {
  if (eStopFlag::ABORT == flag)
    Pa_AbortStream(mStream);
  else
    Pa_StopStream(mStream);
  Pa_CloseStream(mStream);
  Pa_Terminate();
}

std::shared_ptr<Chunk> PaContext::pullInChunk(uint32_t numBytes) {
  std::shared_ptr<Memory> result = Memory::makeNew(numBytes);
  bool finished = false;
  uint32_t bytesRead = fillBuffer(result->buf(), numBytes, mInChunks, finished);
  if (bytesRead != numBytes) {
    if (0 == bytesRead)
      result = std::shared_ptr<Memory>();
    else {
      std::shared_ptr<Memory> trimResult = Memory::makeNew(bytesRead);
      memcpy(trimResult->buf(), result->buf(), bytesRead);
      result = trimResult;
    }
  }

  return std::make_shared<Chunk>(result);
}

void PaContext::pushOutChunk(std::shared_ptr<Chunk> chunk) {
  mOutChunks->push(chunk);
}

void PaContext::checkStatus(uint32_t statusFlags) {
  if (statusFlags) {
    std::string err = std::string("portAudio status - ");
    if (statusFlags & paInputUnderflow)
      err += "input underflow ";
    if (statusFlags & paInputOverflow)
      err += "input overflow ";
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

bool PaContext::getErrStr(std::string& errStr) {
  std::lock_guard<std::mutex> lk(m);
  errStr = mErrStr;
  mErrStr.clear();
  return !errStr.empty();
}

void PaContext::quit() {
  if (mInOptions)
    mInChunks->quit();
  if (mOutOptions)
    mOutChunks->quit();
}

bool PaContext::readPaBuffer(const void *srcBuf, uint32_t frameCount) {
  uint32_t bytesAvailable = frameCount * mInOptions->channelCount() * mInOptions->sampleFormat() / 8;
  std::shared_ptr<Memory> chunk = Memory::makeNew(bytesAvailable);
  memcpy(chunk->buf(), srcBuf, bytesAvailable);
  mInChunks->push(std::make_shared<Chunk>(chunk));
  return true;
}

bool PaContext::fillPaBuffer(void *dstBuf, uint32_t frameCount) {
  uint32_t bytesRemaining = frameCount * mOutOptions->channelCount() * mOutOptions->sampleFormat() / 8;
  bool finished = false;
  fillBuffer((uint8_t *)dstBuf, bytesRemaining, mOutChunks, finished);
  return !finished;
}

// private
uint32_t PaContext::fillBuffer(uint8_t *buf, uint32_t numBytes,
                               std::shared_ptr<Chunks> chunks,
                               bool &finished) {
  uint32_t bufOff = 0;
  while (numBytes) {
    if (!chunks->curBuf() || (chunks->curBuf() && (chunks->curBytes() == chunks->curOffset()))) {
      chunks->waitNext();
      if (!chunks->curBuf()) {
        printf("Finishing - %d bytes not available to fill the last buffer\n", numBytes);
        memset(buf + bufOff, 0, numBytes);
        finished = true;
        break;
      }
    }

    uint32_t curBytes = std::min<uint32_t>(numBytes, chunks->curBytes() - chunks->curOffset());
    void *srcBuf = chunks->curBuf() + chunks->curOffset();
    memcpy(buf + bufOff, srcBuf, curBytes);

    bufOff += curBytes;
    chunks->incOffset(curBytes);
    numBytes -= curBytes;
  }

  return bufOff;
}

void PaContext::setParams(Napi::Env env, bool isInput, 
                          std::shared_ptr<AudioOptions> options, 
                          PaStreamParameters &params, double &sampleRate) {
  int32_t deviceID = (int32_t)options->deviceID();
  if ((deviceID >= 0) && (deviceID < Pa_GetDeviceCount()))
    params.device = (PaDeviceIndex)deviceID;
  else
    params.device = isInput ? Pa_GetDefaultInputDevice() : Pa_GetDefaultOutputDevice();
  if (params.device == paNoDevice)
    throw Napi::Error::New(env, "No default device");

  printf("%s device name is %s\n", isInput?"Input":"Output", Pa_GetDeviceInfo(params.device)->name);

  params.channelCount = options->channelCount();
  int maxChannels = isInput ? Pa_GetDeviceInfo(params.device)->maxInputChannels : Pa_GetDeviceInfo(params.device)->maxOutputChannels;
  if (params.channelCount > maxChannels)
    throw Napi::Error::New(env, "Channel count exceeds maximum number of channels for device");

  uint32_t sampleFormat = options->sampleFormat();
  switch(sampleFormat) {
  case 8: params.sampleFormat = paInt8; break;
  case 16: params.sampleFormat = paInt16; break;
  case 24: params.sampleFormat = paInt24; break;
  case 32: params.sampleFormat = paInt32; break;
  default: throw Napi::Error::New(env, "Invalid sampleFormat");
  }

  params.suggestedLatency = isInput ? Pa_GetDeviceInfo(params.device)->defaultLowInputLatency : 
                                      Pa_GetDeviceInfo(params.device)->defaultLowOutputLatency;
  params.hostApiSpecificStreamInfo = NULL;

  sampleRate = (double)options->sampleRate();

  #ifdef __arm__
  framesPerBuffer = 256;
  params.suggestedLatency = isInput ? Pa_GetDeviceInfo(params.device)->defaultHighOutputLatency : 
                                      Pa_GetDeviceInfo(params.device)->defaultHighOutputLatency;
  #endif
}

} // namespace streampunk