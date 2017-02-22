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

// create a sine wave lookup table
var sampleRate = 44100;
var tableSize = 200;
var buffer = Buffer.allocUnsafe(tableSize * 4);
for (var i = 0; i < tableSize * 4; i++) {
  buffer[i] = (Math.sin((i / tableSize) * 3.1415 * 2.0) * 127) + 127;
}

var pw = new portAudio.AudioWriter({
  channelCount: 1,
  sampleFormat: portAudio.SampleFormat8Bit,
  sampleRate: sampleRate
});

pw.once('audio_ready', function (pa) {
  function writeOneMillionTimes(writer, data, callback) {
    var i = 1000000;
    write();
    function write() {
      var ok = true;
      do {
        i -= 1;
        if (i === 0) {
          // last time!
          writer.write(data, callback);
        } else {
          // see if we should continue, or wait
          // don't pass the callback, because we're not done yet.
          ok = writer.write(data);
          // console.log('Writing data', ok);
        }
      } while (i > 0 && ok);
      if (i > 0) {
        // had to stop early!
        // write some more once it drains
        // console.log("So draining.");
        writer.once('drain', write);
      }
    }
  }
  writeOneMillionTimes(pw, buffer, console.log.bind(null, "Done!"));
  pw.pa.start();
});

// setTimeout(pw.pa.start, 100);

setTimeout(function () { pw.pa.stop(); }, 10000);
