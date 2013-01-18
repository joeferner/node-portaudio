'use strict';

var util = require("util");
var events = require("events");
var portAudioBindings = require("bindings")("portAudio.node");

exports.SampleFormat8Bit = 8;
exports.SampleFormat16Bit = 16;
exports.SampleFormat24Bit = 24;
exports.SampleFormat32Bit = 32;

exports.open = function(opts, callback) {
  // We add one so that we know when we are full or empty: http://en.wikipedia.org/wiki/Circular_buffer#Always_Keep_One_Slot_Open
  opts.bufferSize = (opts.bufferSize || 1 * 1024 * 1024 /* 1MB */) + 1;
  opts.channelCount = opts.channelCount || 2;
  opts.sampleFormat = opts.sampleFormat || exports.SampleFormat8Bit;
  opts.sampleRate = opts.sampleRate || 44100;
  opts.toEventEmitter = function(clazz) {
    util.inherits(clazz, events.EventEmitter);
  };
  opts.streamInit = function(stream) {
    events.EventEmitter.call(stream);
    stream.buffer = new Buffer(opts.bufferSize);
  };
  return portAudioBindings.open(opts, callback);
};

exports.getDevices = function(callback) {
  return portAudioBindings.getDevices(callback);
};
