var portAudio = require('../');

exports.sine = {
  "play 8 bit, single channel": function (test) {
    var sampleRate = 44100;
    var tableSize = 200;
    var buffer = new Buffer(tableSize);
    for (var i = 0; i < tableSize; i++) {
      buffer[i] = (Math.sin((i / tableSize) * 3.1415 * 2.0) * 0x7f) + 0x7f;
    }

    portAudio.open({
      channelCount: 1,
      sampleFormat: portAudio.SampleFormat8Bit,
      sampleRate: sampleRate
    }, function (err, pa) {
      if (err) {
        return test.done(err);
      }
      pa.on("underrun", function () {
        test.fail("underrun shouldn't be called.");
      });
      pa.on("overrun", function () {
        test.fail("overrun shouldn't be called.");
      });
      for (var i = 0; i < 5 * sampleRate / tableSize; i++) {
        pa.write(buffer);
      }
      pa.start();
      setTimeout(function () {
        pa.stop();
        test.done();
      }, 1 * 1000);
    });
  },

  "play 16 bit, stereo channel": function (test) {
    var sampleRate = 44100;
    var tableSize = 2 * 2 * 200;
    var buffer = new Buffer(tableSize);
    for (var i = 0; i < tableSize; ) {
      var a = (Math.sin((i / tableSize) * 3.1415 * 2.0) * 0x7fff) + 0x7fff;
      buffer[i++] = a >> 8;
      buffer[i++] = a >> 0;
      var b = (Math.sin((i / tableSize) * 3.1415 * 2.0) * 0x7fff) + 0x7fff;
      buffer[i++] = b >> 8;
      buffer[i++] = b >> 0;
    }

    portAudio.open({
      channelCount: 2,
      sampleFormat: portAudio.SampleFormat16Bit,
      sampleRate: sampleRate
    }, function (err, pa) {
      if (err) {
        return test.done(err);
      }
      pa.on("underrun", function () {
        test.fail("underrun shouldn't be called.");
      });
      pa.on("overrun", function () {
        test.fail("overrun shouldn't be called.");
      });
      for (var i = 0; i < 5 * sampleRate / tableSize; i++) {
        pa.write(buffer);
      }
      pa.start();
      setTimeout(function () {
        pa.stop();
        test.done();
      }, 1 * 1000);
    });
  },

  "play 24 bit, mono": function (test) {
    var sampleRate = 44100;
    var tableSize = 3 * 1 * 200;
    var buffer = new Buffer(tableSize);
    for (var i = 0; i < tableSize; ) {
      var a = (Math.sin((i / tableSize) * 3.1415 * 2.0) * 0x7fffff) + 0x7fffff;
      buffer[i++] = (a >> 16) & 0xff;
      buffer[i++] = (a >> 8) & 0xff;
      buffer[i++] = (a >> 0) & 0xff;
    }

    portAudio.open({
      channelCount: 1,
      sampleFormat: portAudio.SampleFormat32Bit,
      sampleRate: sampleRate
    }, function (err, pa) {
      if (err) {
        return test.done(err);
      }
      pa.on("underrun", function () {
        test.fail("underrun shouldn't be called.");
      });
      pa.on("overrun", function () {
        test.fail("overrun shouldn't be called.");
      });
      for (var i = 0; i < 5 * sampleRate / tableSize; i++) {
        pa.write(buffer);
      }
      pa.start();
      setTimeout(function () {
        pa.stop();
        test.done();
      }, 1 * 1000);
    });
  },

  "buffer overrun": function (test) {
    portAudio.open({
      bufferSize: 5
    }, function (err, pa) {
      if (err) {
        return test.done(err);
      }
      pa.on("overrun", function () {
        test.fail("shouldn't be called yet.");
      });
      pa.writeByte(1);
      pa.writeByte(2);
      pa.writeByte(3);
      pa.writeByte(4);
      pa.writeByte(5);

      pa.removeAllListeners("overrun");
      pa.on("overrun", function () {
        test.done();
      });
      pa.writeByte(6);
    });
  },

  "buffer underrun": function (test) {
    portAudio.open({
      bufferSize: 5
    }, function (err, pa) {
      if (err) {
        return test.done(err);
      }
      var doneCalled = false;
      pa.on("underrun", function () {
        if (!doneCalled) {
          test.done();
          doneCalled = true;
        }
      });
      pa.writeByte(1);
      pa.start();
    });
  }
};
