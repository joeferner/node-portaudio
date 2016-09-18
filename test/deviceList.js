'use strict';

var portAudio = require('../portAudio.js');

module.exports = {
  "getDevices": function(test) {
    portAudio.getDevices(function(err, devices) {
      if (err) {
        return test.done(err);
      }
      console.log(devices);
      return test.done();
    });
  }
};
