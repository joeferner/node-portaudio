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

#ifndef MEMORY_H
#define MEMORY_H

#include <memory>

namespace streampunk {

class Memory {
public:
  static std::shared_ptr<Memory> makeNew(uint32_t srcBytes) {
    return std::make_shared<Memory>(srcBytes);
  }
  static std::shared_ptr<Memory> makeNew(uint8_t *buf, uint32_t srcBytes) {
    return std::make_shared<Memory>(buf, srcBytes);
  }

  Memory(uint32_t numBytes) 
    : mOwnAlloc(true), mNumBytes(numBytes), mBuf(new uint8_t[mNumBytes]) {}
  Memory(uint8_t *buf, uint32_t numBytes) 
    : mOwnAlloc(false), mNumBytes(numBytes), mBuf(buf) {}
  ~Memory() { if (mOwnAlloc) delete[] mBuf; }

  uint32_t numBytes() const { return mNumBytes; }
  uint8_t *buf() const { return mBuf; }

private:
  const bool mOwnAlloc;
  const uint32_t mNumBytes;
  uint8_t *const mBuf;
};

} // namespace streampunk

#endif