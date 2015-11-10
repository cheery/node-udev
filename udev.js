var udev = require('./build/Release/udev'),
    EventEmitter = require('events').EventEmitter;

udev.Monitor.prototype.__proto__ = EventEmitter.prototype;

module.exports = {
    monitor: function() { return new udev.Monitor(); },
    list: udev.list, 
    getNodeParentBySyspath: udev.getNodeParentBySyspath,
}
