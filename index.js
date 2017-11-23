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

// var SegfaultHandler = require('segfault-handler');
// SegfaultHandler.registerHandler("crash.log");

exports.SampleFormat8Bit = 8;
exports.SampleFormat16Bit = 16;
exports.SampleFormat24Bit = 24;
exports.SampleFormat32Bit = 32;

exports.getDevices = portAudioBindings.getDevices;

function AudioInput(options) {
  if (!(this instanceof AudioInput))
    return new AudioInput(options);

  this.AudioInAdon = new portAudioBindings.AudioIn(options);
  Readable.call(this, {
    highWaterMark: 16384,
    objectMode: false,
    read: size => {
      this.AudioInAdon.read(size, (err, buf) => {
        if (err)
          console.error(err);
          // this.emit('error', err); // causes Streampunk Microphone node to exit early...
        else
          this.push(buf);
      });
    }
  });

  this.start = () => this.AudioInAdon.start();
  this.quit = cb => {
    const quitCb = arguments[0];
    this.AudioInAdon.quit(() => {
      if (typeof quitCb === 'function')
        quitCb();
    });
  }
}
util.inherits(AudioInput, Readable);
exports.AudioInput = AudioInput;

function AudioOutput(options) {
  if (!(this instanceof AudioOutput))
    return new AudioOutput(options);

  let Active = true;
  this.AudioOutAdon = new portAudioBindings.AudioOut(options);
  Writable.call(this, {
    highWaterMark: 16384,
    decodeStrings: false,
    objectMode: false,
    write: (chunk, encoding, cb) => this.AudioOutAdon.write(chunk, cb)
  });

  this.start = () => this.AudioOutAdon.start();
  this.quit = cb => {
    Active = false;
    const quitCb = arguments[0];
    this.AudioOutAdon.quit(() => {
      if (typeof quitCb === 'function')
        quitCb();
    });
  }
  this.on('finish', () => { if (Active) this.quit(); });
}
util.inherits(AudioOutput, Writable);
exports.AudioOutput = AudioOutput;
