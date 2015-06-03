# Demonstrates how to detect some non-edible mice.
#
# The code is written in coffeescript. I prefer it over javascript.
udev = require('../udev')

ismouse = (device) ->
    ok = true
    ok &&= device.syspath.match(/event[0-9]+$/)
    ok &&= device.SUBSYSTEM == "input"
    ok &&= device.ID_INPUT == "1"
    ok &&= device.ID_INPUT_MOUSE == "1"
    return ok

for device in udev.list()
    if ismouse device
        console.log "#{device.ID_SERIAL} present at #{device.ID_PATH}"

console.log "this program should halt when you remove one of your mice"

monitor = udev.monitor()

# 'add' and 'change' events produce the same device structure.
monitor.on 'remove', (device) ->
    if ismouse device
        console.log "#{device.ID_SERIAL} unplugged from #{device.ID_PATH}"
        monitor.close()
