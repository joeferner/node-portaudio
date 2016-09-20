var portAudio = require('../portAudio.js');
var fs = require('fs');
var rs = fs.createReadStream('../media/sound/Steam_Engine-John-1826274710.wav');

// create a sine wave lookup table
var sampleRate = 44100;

portAudio.getDevices(function(err, devices) {
  console.log(devices);
});

var pw = new portAudio.AudioWriter({
  channelCount: 2,
  sampleFormat: portAudio.SampleFormat16Bit,
  sampleRate: sampleRate});

// console.log('pw', pw);

pw.once('audio_ready', function (pa) {
  console.log('Received Audio ready.', this.pa);
  rs.pipe(pw);
  pa.start();
});

setTimeout(function () {
  pw.pa.stop();
  rs.close();
}, 2000);
