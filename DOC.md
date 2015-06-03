# Reference Documentation for node-udev

## udev.list()

Returns a list of devices records provided by `udev_enumerate_scan_devices`.

## Device records

Every device record contains a device.syspath which holds the string for device provided by `udev_list_entry_get_name` or `udev_device_get_syspath`. The remaining names in the object are provided by `udev_device_get_properties_list_entry`.

## udev.monitor()

Creates a monitor you may use to track changes into devices. The presence of a monitor causes node.js to wait instead of exiting after executing your script.

The returned monitor object extends [EventEmitter](https://nodejs.org/api/events.html).

If you call this function like this: `var monitor = udev.monitor();`, you may then do some of the following:

### monitor.close()

Closes the monitor you created. It stops listening for new events.

### Events 'remove', 'add', 'change'

Indicates whether a device was removed, added or changed. Every event provides a device record. Example use:

    monitor.on('add', function(device) {
        console.log(device.syspath);
    });
