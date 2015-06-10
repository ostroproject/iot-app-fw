#include <stdio.h>
#include <unistd.h>

#include <node.h>
#include <string.h>
#include <v8.h>
#include <uv.h>

#include <iot/common/uv-glue.h>
#include <iot/app.h>

using namespace v8;

static iot_app_t *iot;

#define ARG_ERROR(msg)                          \
    return ThrowException(Exception::TypeError(String::New(msg)))

#define THROW(type, msg)                                        \
    ThrowException(Exception::type(String::New(msg)))

Handle<Value> Hello(const Arguments &args)
{
    printf("Ahoy, almighty pirates...\n");
    return Undefined();
}


Handle<Value> BridgeSignals(const Arguments &args)
{
    printf("BridgeSignals()...\n");

    if (args.Length() != 0)
        ARG_ERROR("BridgeSignal takes no arguments.");

    return Undefined();
}


Handle<Value> SubscribeEvents(const Arguments &args)
{
    printf("SubscribeEvents()...\n");

    if (args.Length() != 2)
        ARG_ERROR("SubscribeEvents takes 2 arguments.");

    if (!args[0]->IsArray())
        ARG_ERROR("SubscribeEvents expects array of event names.");

    return Undefined();
}


Handle<Value> SendEvent(const Arguments &args)
{
    iot_app_id_t id;

    printf("SendEvent()...\n");

    id.label   = NULL;
    id.appid   = NULL;
    id.binary  = NULL;
    id.user    = 1000;
    id.process = 0;

    iot_app_event_send(iot, "hello", NULL, &id, NULL, NULL);

    return Undefined();
}


void init(Handle<Object> exports, Handle<Object> module)
{
    iot_mainloop_t *ml;

    (void)module;

    ml = iot_mainloop_uv_get(uv_default_loop());

    if (ml == NULL)
        THROW(Error, "Failed to create IoT/UV mainloop.");

    iot = iot_app_create(ml, NULL);

    if (iot == NULL)
        THROW(Error, "Failed to create IoT application context.");

    exports->Set(String::NewSymbol("BridgeSignals"),
                 FunctionTemplate::New(BridgeSignals)->GetFunction());
    exports->Set(String::NewSymbol("SubscribeEvents"),
                 FunctionTemplate::New(SubscribeEvents)->GetFunction());
    exports->Set(String::NewSymbol("SendEvent"),
                 FunctionTemplate::New(SendEvent)->GetFunction());

    exports->Set(String::NewSymbol("Hello"),
                 FunctionTemplate::New(Hello)->GetFunction());
}


NODE_MODULE(iot_appfw, init)
