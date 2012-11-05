# node-udev - list devices in system and detect changes on them

This library attempts to follow the libudev where it makes sense. I only needed some usb input device detection so I was happy with quite few features.

Requires node-v0.8.0 and libudev.

## Installation

    npm install udev

## How to Use

    var udev = require("udev");

    console.log(udev.list()); // this is a long list :)

    var monitor = udev.monitor();
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
