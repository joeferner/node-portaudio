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

#ifndef CHUNKQUEUE_H
#define CHUNKQUEUE_H

#include <queue>
#include <mutex>
#include <condition_variable>

namespace streampunk {

template <class T>
class ChunkQueue {
public:
  ChunkQueue(uint32_t maxQueue) : mActive(true), mMaxQueue(maxQueue), qu(), m(), cv() {}
  ~ChunkQueue() {}
  
  void enqueue(T t) {
    std::unique_lock<std::mutex> lk(m);
    while(mActive && (qu.size() >= mMaxQueue)) {
      cv.wait(lk);
    }
    qu.push(t);
    cv.notify_one();
  }
  
  T dequeue() {
    std::unique_lock<std::mutex> lk(m);
    while(mActive && qu.empty()) {
      cv.wait(lk);
    }
    T val = 0;
    if (!qu.empty()) {
      val = qu.front();
      qu.pop();
      cv.notify_one();
    }
    return val;
  }

  size_t size() const {
    std::lock_guard<std::mutex> lk(m);
    return qu.size();
  }

  void quit() {
    std::lock_guard<std::mutex> lk(m);
    mActive = false;
    if ((0 == qu.size()) || (qu.size() >= mMaxQueue)) {
      // ensure release of any blocked thread
      cv.notify_all();
    }
  }

private:
  bool mActive;
  uint32_t mMaxQueue;
  std::queue<T> qu;
  mutable std::mutex m;
  std::condition_variable cv;
};

} // namespace streampunk

#endif
