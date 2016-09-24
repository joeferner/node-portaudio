'use strict';

var util = require("util");
var EventEmitter = require("events");
const Writable = require("stream").Writable;
var portAudioBindings = require("bindings")("portAudio.node");

var SegfaultHandler = require('../node-segfault-handler');
SegfaultHandler.registerHandler("crash.log");

exports.SampleFormat8Bit = 8;
exports.SampleFormat16Bit = 16;
exports.SampleFormat24Bit = 24;
exports.SampleFormat32Bit = 32;

function AudioWriter (opts) {
  Writable.call(this);
  opts.channelCount = opts.channelCount || 2;
  opts.sampleFormat = opts.sampleFormat || exports.SampleFormat8Bit;
  opts.sampleRate = opts.sampleRate || 44100;
  var paud = portAudioBindings.open(opts);
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

exports.getDevices = portAudioBindings.getDevices;
