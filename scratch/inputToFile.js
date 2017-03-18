var portAudio = require('../index.js');
var fs = require('fs');

//Create a new instance of Audio Reader, which is a ReadableStream
var pr = new portAudio.AudioReader({
  channelCount: 2,
  sampleFormat: portAudio.SampleFormat16Bit,
  sampleRate: 44100,
  deviceId: 0
});

//Create a write stream to write out to a raw audio file
var ws = fs.createWriteStream('rawAudio.raw');

//Set a timeout
var to = setTimeout(function(){ },12345678);

//Start streaming
pr.once('audio_ready', function(pa) {
  pr.pipe(ws);
  pr.pa.start();
});

//Clear timeout
pr.once('finish',function() {clearTimeout(to); });

process.on('SIGINT', () => {
  console.log('Received SIGINT. Stopping recording.');
  ws.close();
  pr.pa.stop();
  clearTimeout(to);
  process.exit();
});
