
# portaudio

Node wrapper around [PortAudio](http://www.portaudio.com/)

*NOTE:

## Example

```javascript
var portAudio = require('portaudio');

// create a sine wave lookup table
var sampleRate = 44100;
var tableSize = 200;
var buffer = new Buffer(tableSize);
for (var i = 0; i < tableSize; i++) {
  buffer[i] = (Math.sin((i / tableSize) * 3.1415 * 2.0) * 127) + 127;
}

portAudio.open({
  channelCount: 1,
  sampleFormat: portAudio.SampleFormat8Bit,
  sampleRate: sampleRate
}, function (err, pa) {
  // send samples to be played
  for (var i = 0; i < 5 * sampleRate / tableSize; i++) {
    pa.write(buffer);
  }

  // start playing
  pa.start();

  // stop playing 1 second later
  setTimeout(function () {
    pa.stop();
  }, 1 * 1000);
});
```
