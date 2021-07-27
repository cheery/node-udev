const EventEmitter = require('events').EventEmitter;
const udev = require('bindings')('udev');
const inherits = require('util').inherits

const Monitor = udev.Monitor;
inherits(Monitor, EventEmitter);


const getDetailedNodeChain = node => {
  let result = [], extra;
  while (node && node.hasOwnProperty('syspath')) {
    extra = udev.getSysattrBySyspath(node.syspath);
    for (let key in extra) {
      if (extra.hasOwnProperty(key)) {
        node[key] = extra[key];
      }
    }
    result.push(node);
    node = udev.getNodeParentBySyspath(node.syspath);
  }
  return result;
};


const getNodeDetailsSummary = node => {
  let result = {},
    nodes = getDetailedNodeChain(node),
    key;
  while (nodes.length != 0) {
    node = nodes.pop();
    for (let key in node) {
      if (node.hasOwnProperty(key)) {
        if (!result.hasOwnProperty(key)) {
          result[key] = [];
        }
        result[key].push(node[key]);
      }
    }
  }
  return result;
};




module.exports = {
  monitor: subsystem => new Monitor(subsystem),
  list: udev.list,
  getNodeParentBySyspath: udev.getNodeParentBySyspath,
  getSysattrBySyspath: udev.getSysattrBySyspath,
  getNodeChain: getDetailedNodeChain,
  getNodeDetails: getNodeDetailsSummary
};
