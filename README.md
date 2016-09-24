# naudiodon

A Node.js native extension that provides a wrapper around the [PortAudio](http://portaudio.com/) library, enabling an application to record and play audio with cross platform support. With this library, you can create [node.js streams](https://nodejs.org/dist/latest-v6.x/docs/api/stream.html) that can be piped to or from other streams, such as files and network connections. This library supports back-pressure.

This is a fork of [node-portaudio](/joeferner/node-portaudio), refactored by:

* changing from an event model to a stream model;
* linking to the v8 libraries through the [Native Abstractions for Node.js (NAN)](/nodejs/nan) library to enable more portability between node versions.
* adding in local copies of libraries so that portaudio does not have to be installed preemptively.

Little of the original remains but I am very grateful for Joe Ferner for the inspiration and framework to get started.

Only audio writing is curently supported. This library has been tested on MacOS X 10.11, Windows 10 and Raspbian Jessie.

Note: This is a server side library. It is not intended as a means to play and record audio via a browser.

## Installation

Install [Node.js](http://nodejs.org/) for your platform. This software has been developed against the long term stable (LTS) release.

On Linux, please [buiold or install portaudio](http://portaudio.com/download.html) first. This is a temporary measure while the author is experimenting with Linux on different processor architectures. 

Codecadon is designed to be `require`d to use from your own application to provide async processing.

    npm install --save naudiodon

## Using naudiodon

### Listing devices

To get list of supported devices, call the `getDevices()` function.

```javascript
var portAudio = require('portaudio');

console.log(portAudio.getDevices());
```

An example of the output is:

```json
[ { id: 0,
    name: 'Built-in Microph',
    maxInputChannels: 2,
    maxOutputChannels: 0,
    defaultSampleRate: 44100,
    defaultLowInputLatency: 0.00199546485260771,
    defaultLowOutputLatency: 0.01,
    defaultHighInputLatency: 0.012154195011337868,
    defaultHighOutputLatency: 0.1,
    hostAPIName: 'Core Audio' },
  { id: 1,
    name: 'Built-in Input',
    maxInputChannels: 2,
    maxOutputChannels: 0,
    defaultSampleRate: 44100,
    defaultLowInputLatency: 0.00199546485260771,
    defaultLowOutputLatency: 0.01,
    defaultHighInputLatency: 0.012154195011337868,
    defaultHighOutputLatency: 0.1,
    hostAPIName: 'Core Audio' },
  { id: 2,
    name: 'Built-in Output',
    maxInputChannels: 0,
    maxOutputChannels: 2,
    defaultSampleRate: 44100,
    defaultLowInputLatency: 0.01,
    defaultLowOutputLatency: 0.002108843537414966,
    defaultHighInputLatency: 0.1,
    defaultHighOutputLatency: 0.012267573696145125,
    hostAPIName: 'Core Audio' } ]
```

Note that the device `id` parameter index value can be used as to specify which device to use for playback or recording with optional paramter `deviceId`.

### Playing audio

Playing audio involves writing or piping audio data to an instance of `AudioWriter`. 

```javascript
var portAudio = require('portaudio');
var fs = require('fs');

// Create an instance of an AudioWriter, which is a WritableStream
var pw = portAudio.AudioWriter({
  channelCount: 2,
  sampleFormat: portAudio.SampleFormat16Bit,
  sampleRate: sampleRate,
  deveiceId : 0 }); // Omit the deviceId to select the default device

// Create a stream to pipe into the AudioWriter  
var rs = fs.createReadStream('steam_48000.wav');

// Stop the Node.JS process from closing before the clip plays
var to = setTimeout(function () { }, 12345678);

// When the audio device signals that it is ready, start piping data and start streaming
pw.once('audio_ready', function (pa) {
  rs.pipe(pw);
  pw.pa.start();
});

// When the stream is finished, clear the timeout so the node process can complete
pw.once('finish', function () { clearTimeout(to); });
```

To stop the stream early, close the piped input or call `pw.pa.stop()`.

### Recording audio

To follow.

## Troubleshooting

### Linux - No Default Device Found

Ensure that when you compile portaudio that the configure scripts says "ALSA" yes.

### Mac - Carbon Component Manager

You may see the following message during initilisation of the audio library on MacOS:

```
WARNING:  140: This application, or a library it uses, is using the deprecated Carbon Component Manager 
for hosting Audio Units. Support for this will be removed in a future release. Also, this makes the host
incompatible with version 3 audio units. Please transition to the API's in AudioComponent.h.
```

Streampunk Media know how to fix this issue in PortAudio and intend to contact the authors of PortAudio
and provide them with a fix.

## Status, support and further development

Recording of audio is not yet supported. Optimisation is still required for use with lower specification devices, such as Raspberry Pis.

Although the architecture of naudiodon is such that it could be used at scale in production environments, development is not yet complete. In its current state, it is recommended that this software is used in development environments and for building prototypes. Future development will make this more appropriate for production use.

Contributions can be made via pull requests and will be considered by the author on their merits. Enhancement requests and bug reports should be raised as github issues. For support, please contact [Streampunk Media](http://www.streampunk.media/).

## License

This software is released under the Apache 2.0 license. Copyright 2016 Streampunk Media Ltd.

This software uses libraries from the PortAudio project. The [license terms for PortAudio](http://portaudio.com/license.html) are stated to be an [MIT license](http://opensource.org/licenses/mit-license.php). Streampunk Media are grateful to Ross Bencina and Phil Burk for their excellent library.
