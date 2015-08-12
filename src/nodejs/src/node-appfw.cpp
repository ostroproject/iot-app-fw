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

#define NULLOK false
#define NONULL true

#if NODE_MODULE_VERSION <= 11
#    define V8IOT_RETTYPE Handle<Value>
#    define V8IOT_ARGTYPE const Arguments
#    define V8IOT_HANDLE_SCOPE() HandleScope scope
#    define V8IOT_RETURN_DEFAULT() return scope.Close(Undefined())

#    define V8IOT_THROW(_type, _msg) do {                               \
        v8::Local<v8::Value> e = Exception::_type(String::New(_msg));   \
        ThrowException(e);                                              \
    } while (0)

#    define V8IOT_CHECK_ARGC(_fn, _args, _expected, _msg) do {          \
        if ((_args).Length() == (_expected))                            \
            break;                                                      \
                                                                        \
        if (!*(_msg)) {                                                 \
            V8IOT_THROW(TypeError, #_fn": expects "#_expected           \
                        " arguments");                                  \
        }                                                               \
        else {                                                          \
            V8IOT_THROW(TypeError, #_fn": "_msg);                       \
        }                                                               \
                                                                        \
        V8IOT_RETURN_DEFAULT();                                         \
    } while (0)

#    define V8IOT_CHECK_ARGV(_fn, _args, _idx, _type, _nullok,          \
                             _msg) do {                                 \
        if ((_args)[(_idx)]->Is##_type())                               \
            break;                                                      \
        if ((_nullok) && (_args)[(_idx)]->IsNull())                     \
            break;                                                      \
                                                                        \
        if (!*(_msg))                                                   \
            V8IOT_THROW(TypeError, #_fn": expects "#_type               \
                        " as "#_idx" argument");                        \
        else                                                            \
            V8IOT_THROW(TypeError, #_fn": "_msg);                       \
                                                                        \
        V8IOT_RETURN_DEFAULT();                                         \
    } while (0)
#else
#    define V8IOT_RETTYPE void
#    define V8IOT_ARGTYPE const FunctionCallbackInfo<Value>
#    define V8IOT_HANDLE_SCOPE() HandleScope scope(Isolate::GetCurrent())
#    define V8IOT_RETURN_DEFAULT() return

#    define V8IOT_THROW(_type, _msg) do {                                 \
        v8::Isolate* iso = Isolate::GetCurrent();                         \
        v8::Local<v8::Value> e =                                          \
            Exception::_type(String::NewFromUtf8(iso, _msg,               \
                                                 String::kNormalString)); \
        iso->ThrowException(e);                                           \
    } while (0)

#    define V8IOT_CHECK_ARGC(_fn, _args, _expected, _msg) do {          \
        if ((_args).Length() == (_expected))                            \
            break;                                                      \
                                                                        \
        if (!*(_msg)) {                                                 \
            V8IOT_THROW(TypeError, #_fn": expects "#_expected           \
                        " arguments");                                  \
        }                                                               \
        else {                                                          \
            V8IOT_THROW(TypeError, #_fn": "_msg);                       \
        }                                                               \
                                                                        \
        V8IOT_RETURN_DEFAULT();                                         \
    } while (0)

#    define V8IOT_CHECK_ARGV(_fn, _args, _idx, _type, _nullok,          \
                             _msg) do {                                 \
        if ((_args)[(_idx)]->Is##_type())                               \
            break;                                                      \
        if ((_nullok) && (_args)[(_idx)]->IsNull())                     \
            break;                                                      \
                                                                        \
        if (!*(_msg))                                                   \
            V8IOT_THROW(TypeError, #_fn": expects "#_type               \
                        " as "#_idx" argument");                        \
        else                                                            \
            V8IOT_THROW(TypeError, #_fn": "_msg);                       \
                                                                        \
        V8IOT_RETURN_DEFAULT();                                         \
    } while (0)

#endif


static V8IOT_RETTYPE JS_SetDebug(V8IOT_ARGTYPE &args);
static V8IOT_RETTYPE JS_BridgeSystemSignals(V8IOT_ARGTYPE &args);
static V8IOT_RETTYPE JS_SubscribeEvents(V8IOT_ARGTYPE &args);
static V8IOT_RETTYPE JS_SendEvent(V8IOT_ARGTYPE &args);

static void dispatch_event(iot_app_t *iot, const char *event, iot_json_t *data);

/**
 * Our top-level IoT Application Framework NodeJS class.
 *
 * This is a wrapper class for the IoT app framework application context.
 * The singleton instance of this class encapsulates all the state necessary
 * to expose the IoT app framework functionality to NodeJS.
 */
class NodeIoTApp {
public:
    // create, initialize and get singleton instance
    static NodeIoTApp *Get(Handle<Object> &exports);
    // get existing singleton instance
    static NodeIoTApp *Get();
    // destructor
    inline ~NodeIoTApp() {
#if NODE_MODULE_VERSION <= 11
        js_.Dispose();
        js_.Clear();
#else
        js_.Reset();
#endif
    }
    // IoT app framework context getter
    inline iot_app_t *IoTApp() { return iot_; }
    // JS (extension) object getter
    inline Persistent<Object> &JsObj() {
        assert(!js_.IsEmpty());
        return js_;
    }
    // JS (extension) object setter
    void SetJsObj(Local<Object> &obj) {
        if (!js_.IsEmpty())
            return;
#if NODE_MODULE_VERSION <= 11
        js_ = Persistent<Object>::New(obj);
#else
        js_.Reset(Isolate::GetCurrent(), obj);
#endif
    }

    // JS event dispatcher
    void DispatchEvent(const char *event, Local<Value> &data);

    // JS to iot_app_id_t extractor
    bool ObjectToAppId(Local<Object> &js_o, iot_app_id_t *id, char *buf,
                       size_t size);

    // JSON to JS converters
    Local<Value> JsonToArray(iot_json_t *a);
    Local<Value> JsonToObject(iot_json_t *o);

    // JS to JSON converters
    iot_json_t *ArrayToJson(Local<Array> &js_a);
    iot_json_t *ObjectToJson(Local<Object> &js_o);

    // Wrappers for creating JS objects
    Local<Value> JS_String(const char *s);
    Local<Value> JS_Integer(int i);
    Local<Value> JS_Boolean(bool b);
    Local<Value> JS_Double(double d);
    Local<Array> JS_Array();
    Local<Object> JS_Object();

#if NODE_MODULE_VERSION <= 11
#    define JS_FunctionTemplate(_fn)                    \
    FunctionTemplate::New(_fn)
#else
#    define JS_FunctionTemplate(_fn)                    \
    FunctionTemplate::New(Isolate::GetCurrent(), _fn)
#endif

    // Wrapper for getting the value of a JS string
    char *JS_StringValue(Local<Value> js_s, char *buf, size_t size);

    // Wrappers for setting object members and array items
    bool JS_SetMember(Local<Array> js_a, uint32_t i, const char *s);
    bool JS_SetMember(Local<Array> js_a, uint32_t i, int n);
    bool JS_SetMember(Local<Array> js_a, uint32_t i, bool b);
    bool JS_SetMember(Local<Array> js_a, uint32_t i, double d);
    bool JS_SetMember(Local<Array> js_a, uint32_t i, Local<Value> o);

    bool JS_SetMember(Local<Object> js_o, const char *key, const char *s);
    bool JS_SetMember(Local<Object> js_o, const char *key, int n);
    bool JS_SetMember(Local<Object> js_o, const char *key, bool b);
    bool JS_SetMember(Local<Object> js_o, const char *key, double d);
    bool JS_SetMember(Local<Object> js_o, const char *key, Local<Value> o);

    // Wrappers for getting object members
    bool JS_GetMember(Local<Object> &js_o, const char *key, char *buf,
                      size_t size);
    bool JS_GetMember(Local<Object> &js_o, const char *key, int &i);
    bool JS_GetMember(Local<Object> &js_o, const char *key, double &d);
    bool JS_GetMember(Local<Object> &js_o, const char *key, bool &b);

private:
    static NodeIoTApp *app_;             // singleton instance
    iot_app_t *iot_;                     // underlying C-lib context
    Persistent<Object>js_;               // JS extension object

    // private constructor
    NodeIoTApp(Handle<Object> &exports);
    // export a function with the given name to JS
    void Export(Handle<Object> &e, const char *s, Local<Function> f);
};


/////////////////////////
// NodeIoTApp
//

NodeIoTApp *NodeIoTApp::app_ = NULL;


NodeIoTApp::NodeIoTApp(Handle<Object> &exports)
{
    iot_mainloop_t *ml;

    ml = iot_mainloop_uv_get(uv_default_loop());
    assert(ml != NULL);

    iot_ = iot_app_create(ml, this);
    assert(iot_ != NULL);

    iot_app_event_set_handler(iot_, dispatch_event);

    Export(exports, "SetDebug",
           JS_FunctionTemplate(JS_SetDebug)->GetFunction());
    Export(exports, "BridgeSystemSignals",
           JS_FunctionTemplate(JS_BridgeSystemSignals)->GetFunction());
    Export(exports, "SubscribeEvents",
           JS_FunctionTemplate(JS_SubscribeEvents)->GetFunction());
    Export(exports, "SendEvent",
           JS_FunctionTemplate(JS_SendEvent)->GetFunction());
}


void NodeIoTApp::DispatchEvent(const char *event, Local<Value> &data)
{
    V8IOT_HANDLE_SCOPE();

    Handle<Value> js_argv[] = {
        Handle<Value>(JS_String(event)),
        data
    };
    int js_argc = IOT_ARRAY_SIZE(js_argv);
#if NODE_MODULE_VERSION <= 11
    Local<Object> obj = *js_;
#else
    Local<Object> obj = Local<Object>::New(Isolate::GetCurrent(), js_);
#endif
    Local<Value> js_fn = obj->Get(Handle<Value>(JS_String("onIOTEvent")));

    if (!js_fn->IsFunction())
        return;

    Local<Function>::Cast(js_fn)->Call(obj, js_argc, js_argv);
}


NodeIoTApp *NodeIoTApp::Get(Handle<Object> &exports)
{
    assert(app_ == NULL);

    app_ = new NodeIoTApp(exports);

    return app_;
}


NodeIoTApp *NodeIoTApp::Get()
{
    assert(app_ != NULL);

    return app_;
}


void NodeIoTApp::Export(Handle<Object> &e, const char *s, Local<Function> f)
{
#if NODE_MODULE_VERSION <= 11
    e->Set(String::NewSymbol(s), f);
#else
    e->Set(String::NewFromUtf8(Isolate::GetCurrent(),
                               s, String::kInternalizedString), f);
#endif
}


/////////////////////////
// JS convenience functions
//

Local<Value> NodeIoTApp::JS_String(const char *s)
{
#if NODE_MODULE_VERSION <= 11
    return String::New(s);
#else
    return String::NewFromUtf8(Isolate::GetCurrent(), s,
                               String::kNormalString);
#endif
}


Local<Value> NodeIoTApp::JS_Integer(int i)
{
#if NODE_MODULE_VERSION <= 11
    return Integer::New(i);
#else
    return Integer::New(Isolate::GetCurrent(), i);
#endif
}


Local<Value> NodeIoTApp::JS_Boolean(bool b)
{
#if NODE_MODULE_VERSION <= 11
    return BooleanObject::New(b);
#else
    return Boolean::New(Isolate::GetCurrent(), b);
#endif
}


Local<Value> NodeIoTApp::JS_Double(double d)
{
#if NODE_MODULE_VERSION <= 11
    return Number::New(d);
#else
    return Number::New(Isolate::GetCurrent(), d);
#endif
}


Local<Array> NodeIoTApp::JS_Array()
{
#if NODE_MODULE_VERSION <= 11
    return Array::New();
#else
    return Array::New(Isolate::GetCurrent());
#endif
}


Local<Object> NodeIoTApp::JS_Object()
{
#if NODE_MODULE_VERSION <= 11
    return Object::New();
#else
    return Object::New(Isolate::GetCurrent());
#endif
}




char *NodeIoTApp::JS_StringValue(Local<Value> js_s, char *buf, size_t size)
{
    static char scratch[8192], *p = scratch;
    int n;

    // if no buffer was given, use the scratch space
    if (buf == NULL) {
    retry:
        buf = p;
        size = sizeof(scratch) - (p - scratch);
    }

    n = snprintf(buf, size, "%s",
                 (char *)*String::Utf8Value(Local<String>::Cast(js_s)));

    if (n < (int)size) {
        if (buf == p)
            p += n + 1;
        return buf;
    }

    // if we were asked to use the scratch buffer, and
    //   1) there is plenty of stuff there (more than half full)
    //   2) js_s would fit in after a wraparound still leaving room to spare
    // wrap around the scratch buffer. IOW, we assume the old items have
    // been already used up by the user and we invalidate them.
    if (buf == p &&
        p > scratch + sizeof(scratch) / 2 &&
        n < (int)sizeof(scratch) / 8) {
        p = scratch;
        goto retry;
    }

    return NULL;
}


bool NodeIoTApp::JS_SetMember(Local<Array> js_a, uint32_t i, const char *s)
{
    js_a->Set(i, JS_String(s));

    return true;
}


bool NodeIoTApp::JS_SetMember(Local<Array> js_a, uint32_t i, int n)
{
    js_a->Set(i, JS_Integer(n));

    return true;
}

bool NodeIoTApp::JS_SetMember(Local<Array> js_a, uint32_t i, bool b)
{
    js_a->Set(i, JS_Boolean(b));

    return true;
}


bool NodeIoTApp::JS_SetMember(Local<Array> js_a, uint32_t i, double d)
{
    js_a->Set(i, JS_Double(d));

    return true;
}


bool NodeIoTApp::JS_SetMember(Local<Array> js_a, uint32_t i, Local<Value> o)
{
    js_a->Set(i, o);

    return true;
}


bool NodeIoTApp::JS_SetMember(Local<Object> js_o, const char *key,
                              const char *s)
{
    js_o->Set(JS_String(key), JS_String(s));

    return true;
}

bool NodeIoTApp::JS_SetMember(Local<Object> js_o, const char *key, int n)
{
    js_o->Set(JS_String(key), JS_Integer(n));

    return true;
}

bool NodeIoTApp::JS_SetMember(Local<Object> js_o, const char *key, bool b)
{
    js_o->Set(JS_String(key), JS_Boolean(b));

    return true;
}

bool NodeIoTApp::JS_SetMember(Local<Object> js_o, const char *key, double d)
{
    js_o->Set(JS_String(key), JS_Double(d));

    return true;
}


bool NodeIoTApp::JS_SetMember(Local<Object> js_o, const char *key,
                              Local<Value> o)
{
    js_o->Set(JS_String(key), o);

    return true;
}


bool NodeIoTApp::JS_GetMember(Local<Object> &js_o, const char *key, char *buf,
                             size_t size)
{
    Local<Value> v = js_o->Get(JS_String(key));

    if (v->IsUndefined())
        *buf = '\0';
    else if (v->IsString()) {
        if (JS_StringValue(v, buf, size))
            return true;
    }

    return false;
}


bool NodeIoTApp::JS_GetMember(Local<Object> &js_o, const char *key, int &i)
{
    Local<Value> v = js_o->Get(JS_String(key));

    if (v->IsNumber()) {
        i = v->ToInteger()->Value();
        return true;
    }

    return false;
}


bool NodeIoTApp::JS_GetMember(Local<Object> &js_o, const char *key, double &d)
{
    Local<Value> v = js_o->Get(JS_String(key));

    if (v->IsNumber()) {
        d = v->ToNumber()->Value();
        return true;
    }

    return false;
}


bool NodeIoTApp::JS_GetMember(Local<Object> &js_o, const char *key, bool &b)
{
    Local<Value> v = js_o->Get(JS_String(key));

    if (v->IsBoolean()) {
        b = v->ToBoolean()->BooleanValue();
        return true;
    }

    return false;
}


/////////////////////////
// JSON to JS conversion
//

Local<Value> NodeIoTApp::JsonToArray(iot_json_t *a)
{
    Local<Array> js_a = JS_Array();
    uint32_t n = (uint32_t)iot_json_array_length(a);

    for (uint32_t i = 0; i < n; i++) {
        iot_json_t *e = iot_json_array_get(a, i);

        switch (iot_json_get_type(e)) {
        case IOT_JSON_STRING:
            JS_SetMember(js_a, i, iot_json_string_value(e));
            break;
        case IOT_JSON_INTEGER:
            JS_SetMember(js_a, i, iot_json_integer_value(e));
            break;
        case IOT_JSON_BOOLEAN:
            JS_SetMember(js_a, i, iot_json_boolean_value(e) ? true : false);
            break;
        case IOT_JSON_DOUBLE:
            JS_SetMember(js_a, i, iot_json_double_value(e));
            break;
        case IOT_JSON_ARRAY:
            JS_SetMember(js_a, i, JsonToArray(e));
            break;
        case IOT_JSON_OBJECT:
            JS_SetMember(js_a, i, JsonToObject(e));
            break;

        default:
            break;
        }
    }

    return js_a;
}


Local<Value> NodeIoTApp::JsonToObject(iot_json_t *o)
{
    if (o == NULL)
        return JS_Object();

    switch (iot_json_get_type(o)) {
    case IOT_JSON_STRING:
        return JS_String(iot_json_string_value(o));
    case IOT_JSON_INTEGER:
        return JS_Integer(iot_json_integer_value(o));
    case IOT_JSON_BOOLEAN:
        return JS_Boolean(iot_json_boolean_value(o) ? true : false);
    case IOT_JSON_DOUBLE:
        return JS_Double(iot_json_double_value(o));
    case IOT_JSON_ARRAY:
        return JsonToArray(o);
    case IOT_JSON_OBJECT: {
        const char *key;
        iot_json_t *val;
        iot_json_iter_t it;
        Local<Object> js_o = JS_Object();

        iot_json_foreach_member(o, key, val, it) {
            switch (iot_json_get_type(val)) {
            case IOT_JSON_STRING:
                JS_SetMember(js_o, key, iot_json_string_value(val));
                break;
            case IOT_JSON_INTEGER:
                JS_SetMember(js_o, key, iot_json_integer_value(val));
                break;
            case IOT_JSON_BOOLEAN:
                JS_SetMember(js_o, key, iot_json_boolean_value(val)?true:false);
                break;
            case IOT_JSON_DOUBLE:
                JS_SetMember(js_o, key, iot_json_double_value(val));
                break;
            case IOT_JSON_ARRAY:
                JS_SetMember(js_o, key, JsonToArray(val));
                break;
            case IOT_JSON_OBJECT:
                JS_SetMember(js_o, key, JsonToObject(val));
                break;

            default:
                break;
            }
        }

        return js_o;
    }

    default:
        return JS_Object();
    }
}


/////////////////////////
// JS to JSON conversion
//

iot_json_t *NodeIoTApp::ArrayToJson(Local<Array> &js_a)
{
    iot_json_t *a = iot_json_create(IOT_JSON_ARRAY);

    if (a == NULL)
        return NULL;

    uint32_t n = js_a->Length();

    for (uint32_t i = 0; i < n; i++) {
        Local<Value> v;
        iot_json_t *e;

        v = js_a->Get(i);

        if (v->IsString()) {
            e = iot_json_string(JS_StringValue(v, NULL, 0));
        }
        else if (v->IsNumber()) {
            if (v->NumberValue() == v->IntegerValue())
                e = iot_json_integer(v->IntegerValue());
            else
                e = iot_json_double(v->NumberValue());
        }
        else if (v->IsBoolean()) {
            e = iot_json_boolean(v->BooleanValue() ? 1 : 0);
        }
        else if (v->IsArray()) {
            Local<Array>js_arr = Local<Array>::Cast(v);
            e = ArrayToJson(js_arr);
        }
        else if (v->IsObject()) {
            Local<Object> js_obj = Local<Object>::Cast(v);
            e = ObjectToJson(js_obj);
        }
        else
            continue;

        iot_json_array_append(a, e);
    }

    return a;
}


iot_json_t *NodeIoTApp::ObjectToJson(Local<Object> &js_o)
{
    if (js_o->IsString())
        return iot_json_string(JS_StringValue(js_o, NULL, 0));

    if (js_o->IsNumber()) {
        if (js_o->NumberValue() == js_o->IntegerValue())
            return iot_json_integer(js_o->IntegerValue());
        else
            return iot_json_double(js_o->NumberValue());
    }

    if (js_o->IsBoolean())
        return iot_json_boolean(js_o->BooleanValue());

    if (js_o->IsObject() && js_o->IsArray()) {
        Local<Array>js_a = Local<Array>::Cast(js_o);
        return ArrayToJson(js_a);
    }

    if (!js_o->IsObject())
        return iot_json_create(IOT_JSON_OBJECT);

    Local<Array> members = js_o->GetOwnPropertyNames();
    iot_json_t *o = iot_json_create(IOT_JSON_OBJECT);

    if (o == NULL)
        return NULL;

    uint32_t n = members->Length();

    for (uint32_t i = 0; i < n; i++) {
        Local<Value> k, v;
        char key[256];

        k = members->Get(i);

        if (!k->IsString())
            continue;

        JS_StringValue(k, key, sizeof(key));

        v = js_o->Get(Handle<Value>(JS_String(key)));

        if (v->IsString())
            iot_json_add_string(o, key, JS_StringValue(v, NULL, 0));
        else if (v->IsNumber()) {
            if (v->NumberValue() == v->IntegerValue())
                iot_json_add_integer(o, key, v->IntegerValue());
            else
                iot_json_add_double(o, key, v->NumberValue());
        }
        else if (v->IsBoolean())
            iot_json_add_boolean(o, key, v->BooleanValue());
        else if (v->IsObject() && !v->IsArray()) {
            Local<Object> js_obj = Local<Object>::Cast(v);
            iot_json_add(o, key, ObjectToJson(js_obj));
        }
        else if (v->IsArray()) {
            Local<Array> js_arr = Local<Array>::Cast(v);
            iot_json_add(o, key, ArrayToJson(js_arr));
        }
    }

    return o;
}


bool NodeIoTApp::ObjectToAppId(Local<Object> &js_o, iot_app_id_t *id, char *buf,
                               size_t size)
{
    char user[256], *p;
    int n;

    p = buf;
    if (JS_GetMember(js_o, "label", p, size)) {
        id->label = p;
        n = strlen(p) + 1;
        size -= (size_t)n;
        p += n;
    }
    else
        id->label = NULL;

    if (JS_GetMember(js_o, "appid", p, size)) {
        id->appid = p;
        n = strlen(p) + 1;
        size -= (size_t)n;
        p += n;
    }
    else
        id->appid = NULL;

    if (JS_GetMember(js_o, "binary", p, size)) {
        id->binary = p;
        n = strlen(p) + 1;
        size -= (size_t)n;
        p += n;
    }
    else
        id->binary = NULL;

    if (!JS_GetMember(js_o, "process", id->process))
        id->process = 0;

    if (JS_GetMember(js_o, "user", user, sizeof(user))) {
        struct passwd *pw = getpwnam(user);

        if (pw == NULL)
            return false;
        else
            id->user = pw->pw_uid;
    }
    else {
        id->user = (uid_t)-1;
    }

    return true;
}


/////////////////////////
// Javascript handlers
//

static V8IOT_RETTYPE JS_SetDebug(V8IOT_ARGTYPE &args)
{
    NodeIoTApp *app = NodeIoTApp::Get();

    V8IOT_HANDLE_SCOPE();

    V8IOT_CHECK_ARGC(SetDebug, args, 1, "");
    V8IOT_CHECK_ARGV(SetDebug, args, 0, Array, NONULL, "");

    Local<Array> sites = Local<Array>::Cast(args[0]);
    int nsite = sites->Length();
    char *site;

    if (nsite <= 0)
        V8IOT_RETURN_DEFAULT();

    iot_log_enable(IOT_LOG_MASK_DEBUG);
    iot_debug_enable(true);

    for (int i = 0; i < nsite; i++) {
        if ((site = app->JS_StringValue(sites->Get(i), NULL, 0)))
            iot_debug_set_config(site);
    }

    V8IOT_RETURN_DEFAULT();
}


static V8IOT_RETTYPE JS_BridgeSystemSignals(V8IOT_ARGTYPE &args)
{
    NodeIoTApp *app = NodeIoTApp::Get();

    V8IOT_HANDLE_SCOPE();

    V8IOT_CHECK_ARGC(BridgeSystemSignals, args, 0, "");

    iot_debug("setting up bridging of system signals (SIGHUP, SIGTERM)");
    iot_app_bridge_signals(app->IoTApp());

    Local<Object> js = args.This();
    app->SetJsObj(js);

    V8IOT_RETURN_DEFAULT();
}


static V8IOT_RETTYPE JS_SubscribeEvents(V8IOT_ARGTYPE &args)
{
    NodeIoTApp *app = NodeIoTApp::Get();

    V8IOT_HANDLE_SCOPE();

    V8IOT_CHECK_ARGC(SubscribeEvents, args, 1, "");
    V8IOT_CHECK_ARGV(SubscribeEvents, args, 0, Array, NONULL, "");

    //
    // v8 does not offer a way to get your hands on the pointer to the
    // actual char * data of the elements in a Local<Array> of strings,
    // (or if it does, I could not figure out how). All the provided ways
    // involve copy constructors (directly or indirectly via casting
    // operators) so one cannot use them in a loop and trust that all the
    // data stays valid throughout the full loop. This really sucks since
    // it forces us to mindlessly copy data around even if we only need
    // the data in the same scope where the Local<Array> is known to be
    // alive.
    //
    // Yuck... If there is a more efficient way of accomplishing this which
    // I failed to realise, I'm all ears.
    //

    Local<Array> names = Local<Array>::Cast(args[0]);
    int nname = names->Length();
    char *events[nname + 1];
    int i;

    for (i = 0; i < nname; i++) {
        events[i] = app->JS_StringValue(names->Get(i), NULL, 0);

        if (events[i] == NULL)
            V8IOT_THROW(RangeError, "Out of event name buffer space.");

        iot_debug("subscribing for event <%s>...", events[i]);
    }
    events[i] = NULL;

    if (iot_app_event_subscribe(app->IoTApp(), (char **)events, NULL, NULL) < 0)
        V8IOT_THROW(Error, "Failed to subscribe for events.");

    Local<Object> js = args.This();
    app->SetJsObj(js);

    V8IOT_RETURN_DEFAULT();
}


static V8IOT_RETTYPE JS_SendEvent(V8IOT_ARGTYPE &args)
{
    NodeIoTApp *app = NodeIoTApp::Get();
    int argc = args.Length();

    V8IOT_HANDLE_SCOPE();

    if (argc != 2 && argc != 3)
        V8IOT_THROW(TypeError, "SendEvent expects 2 or 3 arguments.");

    V8IOT_CHECK_ARGV(SendEvent, args, 0, String, NONULL, "");
    V8IOT_CHECK_ARGV(SendEvent, args, 1, Object, NULLOK, "");
    if (argc > 2)
        V8IOT_CHECK_ARGV(SendEvent, args, 2, Object, NULLOK, "");

    char *event, buf[2048];
    iot_app_id_t dst = { NULL, NULL, NULL, (uid_t)-1, (pid_t)0 };

    event = app->JS_StringValue(Local<String>::Cast(args[0]), NULL, 0);

    if (event == NULL)
        V8IOT_THROW(TypeError, "Invalid event name for SendEvent.");

    if (args[1]->IsObject()) {
        Local<Object> js_dst = Local<Object>::Cast(args[1]);
        if (app->ObjectToAppId(js_dst, &dst, buf, sizeof(buf)) < 0)
            V8IOT_THROW(TypeError, "Invalid destination app id for SendEvent.");
    }

    iot_debug("sending event <%s> to { %s, %s, %s, %d, %d }", event,
              dst.label ? dst.label : "*", dst.appid ? dst.appid : "*",
              dst.binary ? dst.binary : "*", dst.user, dst.process);

    Local<Object>js_data = Local<Object>::Cast(args[2]);
    iot_json_t *data = app->ObjectToJson(js_data);

    iot_app_event_send(app->IoTApp(), event, data, &dst, NULL, NULL);

    V8IOT_RETURN_DEFAULT();
}


/////////////////////////
// event handler
static void dispatch_event(iot_app_t *iot, const char *event, iot_json_t *data)
{
    NodeIoTApp *app = NodeIoTApp::Get();
    Local<Value> js_data = app->JsonToObject(data);

    IOT_UNUSED(iot);

    iot_debug("received event <%s> with data %s", event,
              iot_json_object_to_string(data));

    app->DispatchEvent(event, js_data);
}


// set up our extension and register with NodeJS
void setup(Handle<Object> exports, Handle<Object> module)
{
    IOT_UNUSED(module);

    NodeIoTApp::Get(exports);
}

NODE_MODULE(iot_appfw, setup)
