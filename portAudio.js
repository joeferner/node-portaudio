'use strict';

var util = require("util");
var events = require("events");
var portAudioBindings = require("bindings")("portAudio.node");

exports.SampleFormat8Bit = 8;

exports.open = function (opts, callback) {
  opts.bufferSize = opts.bufferSize || 1 * 1024 * 1024; // 1MB
  opts.channelCount = opts.channelCount || 2;
  opts.sampleFormat = opts.sampleFormat || portAudio.SampleFormat8Bit;
  opts.sampleRate = opts.sampleRate || 44100;
  opts.toEventEmitter = function (clazz) {
    util.inherits(clazz, events.EventEmitter);
  };
  opts.streamInit = function (stream) {
    events.EventEmitter.call(stream);
    stream.buffer = new Buffer(opts.bufferSize);
  };
  portAudioBindings.open(opts, callback);
};
