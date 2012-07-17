'use strict';

var portAudio = require('../');

exports.sine = {
  "play 8 bit, single channel": function (test) {
    var buffer = new Buffer(44100 * 5);
    for (var i = 0; i < 44100 * 5; i++) {
      buffer[i] = Math.sin((i / 200) * 3.1415 * 2.0) * 254;
    }

    portAudio.open({
      channelCount: 1,
      sampleFormat: portAudio.SampleFormat8Bit,
      sampleRate: 44100
    }, function (err, pa) {
      if (err) {
        return test.fail(err);
      }
      pa.write(buffer);
      pa.start();
      setTimeout(function () {
        pa.stop();
        test.done();
      }, 4 * 1000);
    });
  }
};
