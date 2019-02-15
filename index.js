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

const util = require("util");
const EventEmitter = require("events");
const { Readable, Writable } = require('stream');
const portAudioBindings = require("bindings")("naudiodon.node");

var SegfaultHandler = require('segfault-handler');
SegfaultHandler.registerHandler("crash.log");

exports.SampleFormat8Bit = 8;
exports.SampleFormat16Bit = 16;
exports.SampleFormat24Bit = 24;
exports.SampleFormat32Bit = 32;

exports.getDevices = portAudioBindings.getDevices;

function AudioInput(options) {
  const audioInAdon = new portAudioBindings.AudioIn(options);

  const audioInStream = new Readable({
    highWaterMark: options.highwaterMark || 16384,
    objectMode: false,
    read: size => {
      audioInAdon.read(size, (err, buf) => {
        if (err)
          process.nextTick(() => audioInStream.emit('error', err));
        audioInStream.push(buf);
        if (buf && buf.length < size)
          audioInStream.push(null);
      });
    }
  });

  audioInStream.start = () => audioInAdon.start();

  audioInStream.quit = cb => {
    audioInAdon.quit('WAIT', () => {
      if (typeof cb === 'function')
        cb();
    });
  }

  audioInStream.abort = cb => {
    audioInAdon.quit('ABORT', () => {
      if (typeof cb === 'function')
        cb();
    });
  }

  audioInStream.on('close', () => {
    console.log('AudioIn close');
    audioInStream.quit();
  });
  audioInStream.on('end', () => console.log('AudioIn end'));
  audioInStream.on('error', err => console.error('AudioIn:', err));

  return audioInStream;
}
exports.AudioInput = AudioInput;

function AudioOutput(options) {
  audioOutAdon = new portAudioBindings.AudioOut(options);

  const audioOutStream = new Writable({
    highWaterMark: options.highwaterMark || 16384,
    decodeStrings: false,
    objectMode: false,
    write: (chunk, encoding, cb) => {
      audioOutAdon.write(chunk, err => {
        if (err)
          process.nextTick(() => audioOutStream.emit('error', err));
        cb();
      });
    }
  });

  audioOutStream.start = () => audioOutAdon.start();

  audioOutStream.quit = cb => {
    audioOutAdon.quit('WAIT', () => {
      if (typeof cb === 'function')
        cb();
    });
  }

  audioOutStream.abort = cb => {
    audioOutAdon.quit('ABORT', () => {
      if (typeof cb === 'function')
        cb();
    });
  }

  audioOutStream.on('close', () => console.log('AudioOut close'));
  audioOutStream.on('finish', () => {
    console.log('AudioOut finish');
    audioOutStream.quit();
  });
  audioOutStream.on('error', err => console.error('AudioOut:', err));

  return audioOutStream;
}
exports.AudioOutput = AudioOutput;
