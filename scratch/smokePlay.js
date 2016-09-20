var portAudio = require('../portAudio.js');
var fs = require('fs');
var rs = fs.createReadStream('../media/sound/steam_48000.wav');

// create a sine wave lookup table
var sampleRate = 48000;

portAudio.getDevices(function(err, devices) {
  console.log(devices);
});

var pw = new portAudio.AudioWriter({
  channelCount: 2,
  sampleFormat: portAudio.SampleFormat16Bit,
  sampleRate: sampleRate});

// console.log('pw', pw);

pw.once('audio_ready', function (pa) {
  //console.log('Received Audio ready.', this);
  rs.pipe(pw);
  pa.start();
});

setTimeout(function () {
  pw.pa.stop();
  rs.close();
}, 30000);
