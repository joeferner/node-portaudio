'use strict';

var util = require("util");
var EventEmitter = require("events");
const Writable = require("stream").Writable;
var portAudioBindings = require("bindings")("portAudio.node");

console.log('WRITEable', Writable);

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
  // this.on('newListener', console.log.bind(null, 'New listener'));
  // this.on('audio_ready', console.log.bind(null, 'AUDIO IS READY!!!'));
  // console.log('SMELLY EVENTS', this._events);
  portAudioBindings.open(opts, function (err, pa) {
    console.log('Port audio bindings open callback.', err, pa);
    if (err) return console.error(err);
    this.pa = pa;
    this._write = pa._write.bind(pa);
    // console.log('SETTING PLEASE', this);
    setImmediate(this.emit.bind(this, 'audio_ready', pa));
  }.bind(this));
  this.pa = null;
}
util.inherits(AudioWriter, Writable);

exports.AudioWriter = AudioWriter;

// exports.open = function(opts, callback) {
//   // We add one so that we know when we are full or empty: http://en.wikipedia.org/wiki/Circular_buffer#Always_Keep_One_Slot_Open
//   opts.bufferSize = (opts.bufferSize || 1 * 1024 * 1024 /* 1MB */) + 1;
//   opts.channelCount = opts.channelCount || 2;
//   opts.sampleFormat = opts.sampleFormat || exports.SampleFormat8Bit;
//   opts.sampleRate = opts.sampleRate || 44100;
//   opts.toEventEmitter = function(clazz) {
//     console.log('Calling toEventEmitter.', Writable);
//     for ( var k in Writable.prototype) {
//       console.log('Copying over prototype', k);
//       if (k !== '_write') clazz.prototype[k] = Writable.prototype[k];
//     }
//     // util.inherits(clazz, Writable);
//     console.log('Wobbly!', clazz.toString(), clazz.prototype._write);
//   };
//   opts.streamInit = function(stream) {
//     console.log('Wibbly foggy!', stream._write);
//     Writable.call(stream);
//     console.log("I want to SCREAM!", stream.constructor.prototype);
//     stream.buffer = new Buffer(opts.bufferSize);
//   };
//   return portAudioBindings.open(opts, callback);
// };

exports.getDevices = function(callback) {
  return portAudioBindings.getDevices(callback);
};
