#include <stdio.h>
#include <unistd.h>

#include <node.h>
#include <string.h>
#include <v8.h>
#include <uv.h>

#include <iot/common/debug.h>
#include <iot/common/uv-glue.h>
#include <iot/app.h>

using namespace v8;

/*
 * Helper macros for throwing an exception.
 */
#define EXCEPTION(_type, _msg) Exception::_type(String::New(_msg))
#define THROW(_type, _msg) ThrowException(EXCEPTION(_type, _msg))

/*
 * Check that _args passed to _fn has _expected number of arguments.
 * If not throw an exception with _msg or a default message if _msg
 * is empty.
 */
#define CHECK_ARGC(_fn, _args, _expected, _msg) do {                    \
        if (_args.Length() == _expected)                                \
            break;                                                      \
                                                                        \
        if (!*_msg)                                                     \
            return THROW(TypeError, #_fn": expects "#_expected          \
                         " arguments");                                 \
        else                                                            \
            return THROW(TypeError, #_fn": "_msg);                      \
    } while (0)

/*
 * Check that _args[_idx] passed to _fn has the right _type or null
 * if that is ok. If type checking fails throw an exception with
 * _msg or a default message if _msg is empty.
 */
#define CHECK_ARGV(_fn, _args, _idx, _type, _nullok, _msg) do {         \
        if (!_args[_idx]->Is##_type()) {                                \
            if (_nullok && _args[_idx]->IsNull())                       \
                break;                                                  \
            if (!*_msg)                                                 \
                return THROW(TypeError, #_fn": expects "#_type          \
                             " as "#_idx" argument");                   \
            else                                                        \
                return THROW(TypeError, #_fn": "_msg);                  \
        }                                                               \
    } while (0)

/*
 * Macros to indicate if it is valid for an argument to be null.
 */
#define NONULL false
#define NULLOK true

/*
 * our singleton objects
 */
static iot_app_t *iot;                   // IoT app framework context
static Persistent<Object> js_iot;        // JS IoT object


/*
 * Helper function to fetch a string member from a Javascript object.
 */
int GetStringMember(Local<Object> &obj, const char *key, char *buf, size_t size,
                    const char *defval)
{
    Local<Value> v;
    int n;

    v = obj->Get(Handle<Value>(String::New(key)));

    if (v->IsUndefined()) {
        if (defval)
            n = snprintf(buf, size, "%s", defval);
        else {
            n = 0;
            *buf = '\0';
        }
    }
    else if (v->IsString())
        n = snprintf(buf, size, "%s",
                     (char *)*String::Utf8Value(Local<String>::Cast(v)));
    else
        return -1;

    return n;
}


/*
 * Helper function to fetch an integer member from a Javascript object.
 */
bool GetIntMember(Local<Object>&obj, const char *key, int defval, int &val)
{
    Local<Value> v;

    v = obj->Get(Handle<Value>(String::New(key)));

    if (v->IsUndefined())
        val = defval;
    else if (v->IsNumber())
        val = (int)v->ToInteger()->IntegerValue();
    else
        return false;

    return true;
}


/*
 * Helper funciton to fetch a function member from a Javascript object.
 */
bool GetFunctionMember(Persistent<Object>&obj, const char *key, Local<Value>&fn)
{
    Local<Value> v;

    fn = obj->Get(Handle<Value>(String::New(key)));

    if (fn->IsFunction())
        return true;
    else
        return false;
}


/*
 * Javascript BridgeSignal function.
 */
Handle<Value> BridgeSystemSignals(const Arguments &args)
{
    CHECK_ARGC(BridgeSystemSignals, args, 0, "");

    iot_debug("setting up bridging of system signals (SIGHUP, SIGTERM)");
    iot_app_bridge_signals(iot);

    js_iot = Persistent<Object>::New(args.This());

    return Undefined();
}


/*
 * Helper functions to convert JSON to JS Objects.
 */
Local<Value> JsonToJSArray(iot_json_t *a);

Local<Value> JsonToJSObject(iot_json_t *o)
{
    if (o == NULL)
        goto out;

    switch (iot_json_get_type(o)) {
    case IOT_JSON_STRING:
        return String::New(iot_json_string_value(o));
    case IOT_JSON_INTEGER:
        return Number::New(iot_json_integer_value(o));
    case IOT_JSON_BOOLEAN:
        return BooleanObject::New(iot_json_boolean_value(o));
    case IOT_JSON_DOUBLE:
        return Number::New(iot_json_double_value(o));
    case IOT_JSON_ARRAY:
        return JsonToJSArray(o);
    case IOT_JSON_OBJECT:
        const char *key;
        iot_json_t *val;
        iot_json_iter_t it;

        Local<Object> js_o = Object::New();

        iot_json_foreach_member(o, key, val, it) {
            switch (iot_json_get_type(val)) {
            case IOT_JSON_STRING:
                js_o->Set(String::New(key),
                          String::New(iot_json_string_value(val)));
                break;

            case IOT_JSON_INTEGER:
                js_o->Set(String::New(key),
                          Integer::New(iot_json_integer_value(val)));
                break;

            case IOT_JSON_BOOLEAN:
                js_o->Set(String::New(key),
                          Boolean::New(iot_json_boolean_value(val)?true:false));
            break;

            case IOT_JSON_DOUBLE:
                js_o->Set(String::New(key),
                          Number::New(iot_json_double_value(val)));
                break;

            case IOT_JSON_ARRAY:
                js_o->Set(String::New(key), JsonToJSArray(val));
                break;

            case IOT_JSON_OBJECT:
                js_o->Set(String::New(key), JsonToJSObject(val));
                break;

            default:
                break;
            }
        }

        return js_o;
    }

 out:
    return Object::New();
}


Local<Value> JsonToJSArray(iot_json_t *a)
{
    iot_json_t *e;
    int i, n;

    Local<Array> js_a = Array::New();

    n = iot_json_array_length(a);

    for (i = 0; i < n; i++) {
        e = iot_json_array_get(a, i);

        switch (iot_json_get_type(e)) {
        case IOT_JSON_STRING:
            js_a->Set((uint32_t)i,
                      String::New(iot_json_string_value(e)));
            break;

        case IOT_JSON_INTEGER:
            js_a->Set((uint32_t)i,
                      Integer::New(iot_json_integer_value(e)));
            break;

        case IOT_JSON_BOOLEAN:
            js_a->Set((uint32_t)i,
                      Boolean::New(iot_json_boolean_value(e) ? true : false));
            break;

        case IOT_JSON_DOUBLE:
            js_a->Set((uint32_t)i,
                      Number::New(iot_json_double_value(e)));
            break;

        case IOT_JSON_ARRAY:
            js_a->Set(i, JsonToJSArray(e));
            break;

        case IOT_JSON_OBJECT:
            js_a->Set((uint32_t)i, JsonToJSObject(e));
            break;

        default:
            break;
        }
    }

    return js_a;
}


/*
 * Callback we use to dispatch events to Javascript.
 */
static void dispatch_event(iot_app_t *iot, const char *event, iot_json_t *data)
{
    int js_argc = 2;
    Handle<Value> js_argv[js_argc] = {
        Handle<Value>(String::New(event)),
        JsonToJSObject(data)
    };
    Local<Value> js_fn, js_data;

    iot_debug("received event <%s> with data %s", event,
              iot_json_object_to_string(data));

    if (GetFunctionMember(js_iot, "onIOTEvent", js_fn))
        Local<Function>::Cast(js_fn)->Call(js_iot, js_argc, js_argv);
}


/*
 * Javascript SubscribeEvents function.
 */
Handle<Value> SubscribeEvents(const Arguments &args)
{
    CHECK_ARGC(SubscribeEvents, args, 1, "");
    CHECK_ARGV(SubscribeEvents, args, 0, Array, NONULL, "");

    Local<Array> names = Local<Array>::Cast(args[0]);
    int          nname = names->Length();
    char        *events[nname], *e;
    char         namebuf[nname * 64], *p;
    int          i, n, l;

    p = namebuf;
    l = sizeof(namebuf) - 1;
    for (i = 0; i < nname; i++) {
        Local<Value> name = names->Get(i);

        n = snprintf(e = p, l, "%s", (char *)*String::Utf8Value(name));

        if (n < 0 || n >= l)
            return THROW(RangeError, "Out of event name buffer space.");

        p += n + 1;
        l -= n + 1;

        iot_debug("event <%s>", e);
        events[i] = e;
    }
    events[i] = NULL;

    if (iot_app_event_subscribe(iot, (char **)events, NULL, NULL) < 0)
        return THROW(Error, "Failed to subscribe for events.");

    js_iot = Persistent<Object>::New(args.This());

    return Undefined();
}


/*
 * Helper functions to convert JS to JSON Objects.
 */
iot_json_t *ArrayToJson(Local<Array> &js_arr);

iot_json_t *ObjectToJson(Local<Object> &js_obj)
{
    Local<Array> members = js_obj->GetOwnPropertyNames();
    iot_json_t *o;
    uint32_t n;

    o = iot_json_create(IOT_JSON_OBJECT);

    if (o == NULL)
        return NULL;

    n = members->Length();

    for (uint32_t i = 0; i < n; i++) {
        Local<Value> k, v;
        char key[256];

        k = members->Get(i);

        if (!k->IsString())
            continue;

        snprintf(key, sizeof(key),
                 *String::AsciiValue(Local<String>::Cast(k)));

        v = js_obj->Get(Handle<Value>(String::New(key)));

        if (v->IsString()) {
            iot_json_add_string(o, key,
                                *String::AsciiValue(Local<String>::Cast(v)));
        }
        else if (v->IsNumber()) {
            if (v->NumberValue() == v->IntegerValue())
                iot_json_add_integer(o, key, v->IntegerValue());
            else
                iot_json_add_double(o, key, v->NumberValue());
        }
        else if (v->IsBoolean()) {
            iot_json_add_boolean(o, key, v->BooleanValue() ? 1 : 0);
        }
        else if (v->IsObject() && !v->IsArray()) {
            Local<Object> js_o = Local<Object>::Cast(v);
            iot_json_add(o, key, ObjectToJson(js_o));
        }
        else if (v->IsArray()) {
            Local<Array> js_a = Local<Array>::Cast(v);
            iot_json_add(o, key, ArrayToJson(js_a));
        }
    }

    return o;
}

iot_json_t *ArrayToJson(Local<Array>&js_arr)
{
    uint32_t i, n;
    iot_json_t *a;

    a = iot_json_create(IOT_JSON_ARRAY);

    if (a == NULL)
        return NULL;

    n = js_arr->Length();

    for (i = 0; i < n; i++) {
        Local<Value> v;
        iot_json_t *e;

        v = js_arr->Get(i);

        if (v->IsString()) {
            e = iot_json_create(IOT_JSON_STRING,
                                *String::AsciiValue(Local<String>::Cast(v)),
                                -1);
        }
        else if (v->IsNumber()) {
            if (v->NumberValue() == v->IntegerValue())
                e = iot_json_create(IOT_JSON_INTEGER, v->IntegerValue());
            else
                e = iot_json_create(IOT_JSON_DOUBLE, v->NumberValue());
        }
        else if (v->IsBoolean()) {
            e = iot_json_create(IOT_JSON_BOOLEAN, v->BooleanValue() ? 1 : 0);
        }
        else if (v->IsArray()) {
            Local<Array> js_a = Local<Array>::Cast(v);
            e = ArrayToJson(js_a);
        }
        else if (v->IsObject()) {
            Local<Object> js_o = Local<Object>::Cast(v);
            e = ObjectToJson(js_o);
        }
        else
            continue;

        iot_json_array_append(a, e);
    }

    return a;
}


/*
 * Helper function to covert a JS object to an iot_app_id_t.
 */
Handle<Value> ObjectToAppId(Local<Object> &js_obj, iot_app_id_t *id,
                            char *buf, size_t size)
{
    char *p, user[256];
    int n;

    p = buf;

    n = GetStringMember(js_obj, "label", p, size, NULL);

    if (n < 0 || n >= (int)size)
        return THROW(TypeError, "target <label> must be a string");

    id->label = n ? p : NULL;
    p += n + 1;
    size -= (size_t)(n + 1);

    n = GetStringMember(js_obj, "appid", p, size, NULL);

    if (n < 0 || n >= (int)size)
        return THROW(TypeError, "target <appid> must be a string");

    id->appid = n ? p : NULL;
    p += n + 1;
    size -= (size_t)(n + 1);

    n = GetStringMember(js_obj, "binary", p, size, NULL);

    if (n < 0 || n >= (int)size)
        return THROW(TypeError, "target <binary> must be a string");

    id->binary = n ? p : NULL;
    p += n + 1;
    size -= (size_t)(n + 1);

    if (!GetIntMember(js_obj, "process", 0, id->process))
        return THROW(TypeError, "target <process> must be an integer");

    n = GetStringMember(js_obj, "user", user, sizeof(user), NULL);

    if (n < 0 || n >= (int)sizeof(user))
        return THROW(TypeError, "target <user> must be a string");

    if (n) {
        struct passwd *pw = getpwnam(user);

        if (pw == NULL)
            return THROW(TypeError, "unknown user");
        else
            id->user = pw->pw_uid;
    }
    else
        id->user = (uid_t)-1;

    return Undefined();
}


/*
 * Javascript SendEvent function.
 */
Handle<Value> SendEvent(const Arguments &args)
{
    int argc = args.Length();

    if (argc != 3 && argc != 2)
        return THROW(TypeError, "SendEvent expects 2 or 3 arguments.");

    CHECK_ARGV(SendEvent, args, 0, String, NONULL, "");
    CHECK_ARGV(SendEvent, args, 1, Object, NULLOK, "");
    if (argc > 2)
        CHECK_ARGV(SendEvent, args, 2, Object, NULLOK, "");

    char event[256], strbuf[2048];
    iot_app_id_t dst = { NULL, NULL, NULL, (uid_t)-1, (pid_t)0 };

    snprintf(event, sizeof(event), "%s",
             (char *)*String::Utf8Value(Local<String>::Cast(args[0])));

    if (args[1]->IsObject()) {
        Local<Object>id = Local<Object>::Cast(args[1]);
        Handle<Value>h = ObjectToAppId(id, &dst, strbuf, sizeof(strbuf));

        if (!h->IsUndefined())
            return h;
    }

    iot_debug("sending event <%s> to { %s, %s, %s, %d, %d }", event,
              dst.label ? dst.label : "*", dst.appid ? dst.appid : "*",
              dst.binary ? dst.binary : "*", dst.user, dst.process);

    Local<Object>data = Local<Object>::Cast(args[2]);
    iot_json_t *json = ObjectToJson(data);

    iot_app_event_send(iot, event, iot_json_ref(json), &dst, NULL, NULL);

    return Undefined();
}


/*
 * Javascript SetDebug function.
 */
Handle<Value> SetDebug(const Arguments &args)
{
    CHECK_ARGC(DebugSet, args, 1, "");
    CHECK_ARGV(DebugSet, args, 0, Array, NONULL, "");

    Local<Array> sites = Local<Array>::Cast(args[0]);
    int nsite = sites->Length();
    int i, n;

    if (nsite > 0) {
        iot_log_enable(IOT_LOG_MASK_DEBUG);
        iot_debug_enable(true);
    }

    for (i = 0; i < nsite; i++) {
        Local<Value> s = sites->Get(i);
        char site[256];

        n = snprintf(site, sizeof(site), "%s", (char *)*String::Utf8Value(s));

        if (n < 0 || n >= (int)sizeof(site))
            return THROW(RangeError, "Debug site name too long.");

        iot_debug_set_config(site);
    }


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

    iot_app_event_set_handler(iot, dispatch_event);

    exports->Set(String::NewSymbol("BridgeSystemSignals"),
                 FunctionTemplate::New(BridgeSystemSignals)->GetFunction());
    exports->Set(String::NewSymbol("SubscribeEvents"),
                 FunctionTemplate::New(SubscribeEvents)->GetFunction());
    exports->Set(String::NewSymbol("SendEvent"),
                 FunctionTemplate::New(SendEvent)->GetFunction());
    exports->Set(String::NewSymbol("SetDebug"),
                 FunctionTemplate::New(SetDebug)->GetFunction());
}


NODE_MODULE(iot_appfw, init)
