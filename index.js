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

var util = require("util");
var EventEmitter = require("events");
const Writable = require("stream").Writable;
const Readable = require("stream").Readable;
var portAudioBindings = require("bindings")("naudiodon.node");

//var SegfaultHandler = require('../node-segfault-handler');
//SegfaultHandler.registerHandler("crash.log");

exports.SampleFormat8Bit = 8;
exports.SampleFormat16Bit = 16;
exports.SampleFormat24Bit = 24;
exports.SampleFormat32Bit = 32;

function AudioWriter (opts) {
  Writable.call(this);
  opts.channelCount = opts.channelCount || 2;
  opts.sampleFormat = opts.sampleFormat || exports.SampleFormat8Bit;
  opts.sampleRate = opts.sampleRate || 44100;
  var paud = portAudioBindings.openOutput(opts);
  this.pa = paud;
  this._write = paud._write.bind(paud);
  setImmediate(this.emit.bind(this, 'audio_ready', this.pa));
  this.on('finish', function () {
    console.log("Closing output stream.");
    this.pa.stop();
  });
}
util.inherits(AudioWriter, Writable);
exports.AudioWriter = AudioWriter;

function AudioReader (opts) {
  Readable.call(this);
  opts.channelCount = opts.channelCount || 2;
  opts.sampleFormat = opts.sampleFormat || exports.SampleFormat8Bit;
  opts.sampleRate = opts.sampleRate || 44100;
  opts.device = opts.device || -1;
  var localPush = this.push;
  var reader = this;
  var paud = portAudioBindings.openInput(opts);
  this.pa = paud;
  var callback = function(){
    pushAccepted = true;
    while(pushAccepted && paud.inputItemsAvailable() > 0){
      pushAccepted = reader.push(paud.inputRead());
    }
    //Prevent any further pushes until _read() is next called
    if(!pushAccepted){
      paud.disablePush();
    }
  };
  this._read = function(){
    callback.bind(this);
    this.pa.inputSetCallback(callback);
  }.bind(this);
  setImmediate(this.emit.bind(this, 'audio_ready', this.pa));
  this.on('finish', function () {
    console.log("Closing input stream.");
    this.pa.inputStop();
  });
}

util.inherits(AudioReader, Readable);
exports.AudioReader = AudioReader;

exports.getDevices = portAudioBindings.getDevices;
