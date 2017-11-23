var portAudio = require('../index.js');
var fs = require('fs');

//Create a new instance of Audio Input, which is a ReadableStream
var ai = new portAudio.AudioInput({
  channelCount: 2,
  sampleFormat: portAudio.SampleFormat16Bit,
  sampleRate: 44100,
  deviceId: 3
});
ai.on('error', console.error);

//Create a write stream to write out to a raw audio file
var ws = fs.createWriteStream('rawAudio.raw');

//Start streaming
ai.pipe(ws);
ai.start();

process.once('SIGINT', ai.quit);
