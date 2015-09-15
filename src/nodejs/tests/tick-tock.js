// destination application we send events to
var dst = { user: process.env['USER'] };

// data attached to events
var data = {
    yep: true, nope: false,
    one: 1, two: 2, pi: 3.141,
    string: 'foobar',
    strings: [ 'a', 'b', 'c', 'd' ],
    ints: [ 1, 2, 3, 4, 5],
    doubles: [ 1.2, 3.4, 4.5 ],
    nested: [ [ [ 1, 2, 3 ], [ { one: 1, two: 2 }, { three: 3 } ], [ 'a','b' ]]],
    count: 10
};

// function to dump an object
function dump (o) {
    var dumper = {};

    dumper.init = function () {
        this.buf = "";
        this.nl = false;
        this.depth = 0;

        return this;
    }

    dumper.indent = function (o) {
        var s = ""; i;

        if (this.nl) {
            for (var i = 0; i < this.depth; i++)
                s += "  ";
        }

        return s + o;
    }

    dumper.append = function (str) {
        var n = str.length;
        var nl = n ? str[n - 1] == '\n' : false;

        this.buf += str;
        this.nl = nl;
    }

    dumper.dump = function (o) {
        var k, v, e;

        switch (typeof(o)) {
        case typeof('string'):
        case typeof(1):
        case typeof(1.2):
        case typeof(true):
            this.append(this.indent(o) + '\n');
            break;

        case typeof({}):
            if (o.constructor != Array) {
                this.append(this.indent('{\n'));
                this.depth++;
                for (k in o) {
                    v = o[k];

                    this.append(this.indent(k + ': '));
                    this.dump(v);
                }
                this.depth--;
                this.append(this.indent('\}\n'));
            }
            else {
                this.append(this.indent('[\n'));
                this.depth++;
                for (e = 0; e < o.length; e++)
                    this.dump(o[e]);
                this.depth--;
                this.append(this.indent('\]\n'));
            }
            break;

        default:
            this.append(this.indent('???' + o));
            break;
        }
    }

    dumper.init();
    dumper.dump(o);

    return dumper.buf;
}

// process a tick event
iot.tick = function (count) {
    if (this.type == 'tock') {
        data.count = count;
        this.SendEvent('tock', dst, data);
    }
}

// process a tock event
function timer_cb () {
    iot.SendEvent('tick', dst, data);
}

iot.tock = function (count) {
    if (this.type == 'tick') {
        if (count <= 0) {
            this.SendEvent('ka-boom', dst, data);
        }
        else {
            data.count = count;
            setTimeout(timer_cb, this.msec);
        }
    }
}

// process a ka-boom event
iot.kaboom = function () {
    if (iot.type == 'tock') {
        this.SendEvent('ka-boom', dst, data);
    }
    process.exit(0);
}


// IoT event handler
iot.onIOTEvent = function (event, event_data) {
    console.log('IoT event <' + event + ' #' + event_data.count + '>');

    if (this.dump)
        console.log('IoT event data: ' + dump(event_data));
    
    switch (event) {
    case 'tick':
        this.tick(--event_data.count);
        break;
    case 'tock':
        this.tock(event_data.count);
        break;
    case 'ka-boom':
        this.kaboom();
        break;
    default:
        break;
    }
}

///////////////////////////////
// main script
//

// set up defaults
iot.type = 'tock';
iot.dump = false;
iot.msec = 1000;
data.count = 10;
libdir = "";

// parse command line options
for (i = 1; i < process.argv.length; i++) {
    switch (process.argv[i]) {
    case '--tick':
        iot.type = 'tick';
        break;
    case '--tock':
        iot.type = 'tock';
        break;
    case '--cnt':
    case '--count':
        data.count = process.argv[i + 1];
        break;
    case '--debug':
    case '-d':
        iot.SetDebug([process.argv[i + 1]]);
        break;
    case '--dump':
        iot.dump = true;
        break;
    case '--delay':
        iot.msec = process.argv[i + 1];
        break;
    case "--libdir":
    case "-L":
        libdir = optarg;
        i += 2;
        break;
    default:
        break;
    }
}

iot = require(libdir ? libdir + "/iot-appfw.node" : "iot-appfw.node");


// ask the IoT app framework to bridge system signals as pseudo-events
iot.BridgeSystemSignals();

// subscribe for events
if (iot.type == 'tick')
    iot.SubscribeEvents([ 'tock', 'ka-boom' ]);
else
    iot.SubscribeEvents([ 'tick', 'ka-boom' ]);

// set thing in motion if we're the ticker
if (iot.type == 'tick') {
    setTimeout(timer_cb, iot.msec);
}


/*
console.log('[1, 2, 3, 5]: ' + dump([1, 2, 3, 4, 5], 0, ""));
console.log('data: ' + dump(data));
*/
