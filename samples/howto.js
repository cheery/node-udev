// The code listed in 'How to Use' -section
//
var udev = require("../udev");

// Lists every device in the system.
console.log(udev.list()); // this is a long list :)

// Opens a monitor that closes when it receives an add -event.
var monitor = udev.monitor();
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
