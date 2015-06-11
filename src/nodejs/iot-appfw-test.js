// IoT Application Framework Bindings test

var iot = require('./build/Release/iot-appfw');

function event_cb(event, data) {
    console.log('***** got event ' + event)
}

iot.SetDebug(['*']);

iot.BridgeSignals();
iot.SubscribeEvents([ 'hello', 'ahoy', 'goodbye' ], event_cb)
iot.user_data = [ 'foo', 'bar', 'foobar' ]

app = { user: 'kli' };

iot.SendEvent('hello', app, { count: 1 });

