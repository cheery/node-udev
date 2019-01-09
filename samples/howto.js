// The code listed in 'How to Use' -section
//
var udev = require("../udev");

// Lists every device in the system.
console.log(udev.list()); // this is a long list :)

// Monitor events on the input subsystem until a device is added.
var monitor = udev.monitor("input");
monitor.on('add', function (device) {
    console.log('added ' + device);
    monitor.close() // this closes the monitor
});
monitor.on('remove', function (device) {
    console.log('removed ' + device);
});
monitor.on('change', function (device) {
    console.log('changed ' + device);
});
