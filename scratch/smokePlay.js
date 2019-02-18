/* Copyright 2019 Streampunk Media Ltd.

  Licensed under the Apache License, Version 2.0 (the "License");
  you may not use this file except in compliance with the License.
  You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

  Unless required by applicable law or agreed to in writing, software
  distributed under the License is distributed on an "AS IS" BASIS,
  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
  See the License for the specific language governing permissions and
  limitations under the License.
*/

// Plays the sound of a steam train from file 'test.wav'.

var portAudio = require('../index.js');
var fs = require('fs');
var rs = fs.createReadStream('test.wav');

var sampleRate = 48000;

console.log(portAudio.getDevices());

var ao = new portAudio.AudioIO({
  outOptions: {
    channelCount: 2,
    sampleFormat: portAudio.SampleFormat16Bit,
    sampleRate: sampleRate,
    deviceId: -1
  }
});

console.log('ao', ao);

ao.on('error', console.error);

// rs.on('end', ao.end);

rs.pipe(ao);

ao.once('finish', () => { console.log("Finish called."); });
ao.start();

process.on('SIGINT', ao.quit);
