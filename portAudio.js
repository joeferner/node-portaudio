'use strict';

var util = require("util");
var events = require("events");
var portAudioBindings = require("bindings")("portAudio.node");

exports.SampleFormat8Bit = 8;

function PortAudioStream() {
  events.EventEmitter.call(this);
}
util.inherits(PortAudioStream, events.EventEmitter);

PortAudioStream.prototype.stop = function () {
  throw new Error("Not implemented");
};

exports.open = function (opts, callback) {
  opts.stream = new PortAudioStream();
  opts.stream.buffer = new Buffer(1 * 1024 * 1024); // 1MB
  opts.channelCount = opts.channelCount || 2;
  opts.sampleFormat = opts.sampleFormat || portAudio.SampleFormat8Bit;
  opts.sampleRate = opts.sampleRate || 44100;
  portAudioBindings.open(opts, callback);
};
