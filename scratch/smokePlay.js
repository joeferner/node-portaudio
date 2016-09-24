var portAudio = require('../index.js');
var fs = require('fs');
var rs = fs.createReadStream('../media/sound/steam_48000.wav');

// create a sine wave lookup table
var sampleRate = 48000;

console.log(portAudio.getDevices());

var pw;

pw = new portAudio.AudioWriter({
  channelCount: 2,
  sampleFormat: portAudio.SampleFormat16Bit,
  sampleRate: sampleRate });

// console.log('pw', pw);

rs.on('close', console.log.bind(null, 'Input stream closing.'));

var to = setTimeout(function () { }, 12345678);

pw.once('audio_ready', function (pa) {
  rs.pipe(pw);
  pw.pa.start();
});

pw.once('finish', function () { clearTimeout(to); });
