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

// Plays a since wave for approx 10 seconds

const portAudio = require('../index.js');

// create a sine wave lookup table
var sampleRate = 44100;
var tableSize = 200;
var buffer = Buffer.allocUnsafe(tableSize * 4);
for (var i = 0; i < tableSize * 4; i++) {
  buffer[i] = (Math.sin((i / tableSize) * 3.1415 * 2.0) * 127);
}

var ao = new portAudio.AudioIO({
  outOptions: {
    channelCount: 1,
    sampleFormat: portAudio.SampleFormat8Bit,
    sampleRate: sampleRate,
    deviceId: -1
  }
});

function tenSecondsIsh(writer, data, callback) {
  this.i = 552;
  const write = () => {
    var ok = true;
    do {
      this.i -= 1;
      if (this.i === 0) {
        // last time!
        writer.end(data, callback);
      } else {
        // see if we should continue, or wait
        // don't pass the callback, because we're not done yet.
        ok = writer.write(data);
        // console.log('Writing data', ok);
      }
    } while (this.i > 0 && ok);
    if (this.i > 0) {
      // had to stop early!
      // write some more once it drains
      // console.log("So draining.");
      writer.once('drain', write);
    }
  }
  write();
  return this;
}
tenSecondsIsh.prototype.quit = function() { this.i = 1; }

let tsi = new tenSecondsIsh(ao, buffer, () => console.log.bind(null, "Done!"));
process.once('SIGINT', () => tsi.quit());

ao.start();
