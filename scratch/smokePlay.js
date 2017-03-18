/* Copyright 2017 Streampunk Media Ltd.

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

var portAudio = require('../index.js');
var fs = require('fs');
var rs = fs.createReadStream('test.wav');

var sampleRate = 48000;

console.log(portAudio.getDevices());

var pw = new portAudio.AudioWriter({
  channelCount: 2,
  sampleFormat: portAudio.SampleFormat16Bit,
  sampleRate: sampleRate });

console.log('pw', pw);

rs.on('close', () => {
  console.log('Input stream closing.');
  pw.pa.stop();
  clearTimeout(to);
});


var to = setTimeout(function () { }, 12345678);

pw.once('audio_ready', function (pa) {
  console.log('Received audio ready.');
  rs.pipe(pw);
  pw.pa.start();
});

pw.once('finish', function () { console.log("Finish called."); clearTimeout(to); });
