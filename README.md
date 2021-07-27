# node-udev - list devices in system and detect changes on them

This library attempts to follow the libudev where it makes sense. I only needed some usb input device detection so I was happy with quite few features.

Requires node-v10.0.0, nan and libudev.

## Installation

    npm install udev

### Installation on debian/ubuntu

    sudo apt-get install libudev-dev
    npm install udev

## How to Use

The example below lists devices and monitors udev events on the ipnut subsystem until receiving an add event. The code is separately listed in `samples/howto.js`.

    var udev = require("udev");

    console.log(udev.list()); // this is a long list :)

    var monitor = udev.monitor("input");
    monitor.on('add', function (device) {
        console.log('added ' + device);
        monitor.close() // this closes the monitor.
    });
    monitor.on('remove', function (device) {
        console.log('removed ' + device);
    });
    monitor.on('change', function (device) {
        console.log('changed ' + device);
    });
    
The example below lists devices belonging to subsystem "tty" i.e. various serial ports.

    var udev = require('udev');
    console.log(udev.list('tty'));
