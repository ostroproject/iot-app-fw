// IoT Application Framework Bindings test

var iot = require('./build/Release/iot-appfw');

function event_cb(event, data) {
    console.log('got event ' + event)
}

iot.Hello();
iot.BridgeSignals();
iot.SubscribeEvents([ 'hello', 'ahoy', 'goodbye' ], event_cb)

app = { user: 'kli' };

iot.SendEvent('hello', app, { count: 1 });

