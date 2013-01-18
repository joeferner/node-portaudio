# portaudio

Node wrapper around [PortAudio](http://www.portaudio.com/)

**Note: This module has not been tested on windows. If you would like to submit a pull request that would be great.**

**Note: You must install PortAudio libs first.**

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

portAudio.getDevices(function(err, devices) {
  console.log(devices);
});

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

## Troubleshooting

### error: 'PaStreamCallbackFlags' has not been declared

Try installing "libasound-dev" package. See (http://portaudio.com/docs/v19-doxydocs/compile_linux.html). Then try rebuilding.

If that doesn't fix it try building and installing portaudio from source (http://www.portaudio.com/download.html). I've compiled
node-portaudio with pa_stable_v19_20111121.tgz and was successful on Ubunutu.

### No Default Device Found

Ensure that when you compile portaudio that the configure scripts says "ALSA" yes.
