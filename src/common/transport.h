/*
 * Copyright (c) 2012, Intel Corporation
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *  * Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 *  * Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *  * Neither the name of Intel Corporation nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef __IOT_TRANSPORT_H__
#define __IOT_TRANSPORT_H__

#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/un.h>

#include <iot/common/macros.h>
#include <iot/common/list.h>
#include <iot/common/mainloop.h>

/*
 * json-c and JSON-Glib have a symbol clash on json_object_get_type.
 * Unfortunately we'd really need to include our own json.h here to
 * get iot_json_t defined. That however pulls in json-c's headers as
 * our implementation uses json-c and the type itself is just a
 * typedef'd alias to json_object.
 *
 * Now if some unfortunate sould ends up directly or indirectly
 * including both our transport.h, and consequently json.h, and
 * JSON-Glib, we'll trigger the symbol clash.
 *
 * As a workaround if we detect that JSON-Glib has already been
 * included we'll compile with alternative signatures (void *,
 * instead of iot_json_t *) and omit including json.h. Also we
 * let people give us a warning by defining __JSON_GLIB_DANGER__
 * that they will or might include JSON-Glib, in which case
 * we also compile with the alternative signatures. Oh boy...
 */

#if !defined(__JSON_TYPES_H__) && !defined(__JSON_GLIB_DANGER__)
#  include <iot/common/json.h>
#else
#  define iot_json_t void
#endif

IOT_CDECL_BEGIN

typedef struct iot_transport_s iot_transport_t;



/*
 * transport socket address
 */

#define IOT_SOCKADDR_SIZE 256

typedef union {
    struct sockaddr     any;
    struct sockaddr_in  ipv4;
    struct sockaddr_in6 ipv6;
    struct sockaddr_un  unx;
    char                data[IOT_SOCKADDR_SIZE];
} iot_sockaddr_t;


static inline iot_sockaddr_t *iot_sockaddr_cpy(iot_sockaddr_t *d,
                                               iot_sockaddr_t *s, socklen_t n)
{
    memcpy(d, s, n);
    return d;
}


/*
 * various transport flags
 */

typedef enum {
    IOT_TRANSPORT_MODE_RAW    = 0x00,    /* uses bitpipe mode */
    IOT_TRANSPORT_MODE_JSON   = 0x01,    /* uses JSON messages */
} iot_transport_mode_t;

typedef enum {
    IOT_TRANSPORT_MODE_MASK   = 0x0f,    /* mask of mode bits */
    IOT_TRANSPORT_INHERIT     = 0x0f,    /* mask of all inherited flags */

    IOT_TRANSPORT_REUSEADDR = 0x010,
    IOT_TRANSPORT_NONBLOCK  = 0x020,
    IOT_TRANSPORT_CLOEXEC   = 0x040,
    IOT_TRANSPORT_CONNECTED = 0x080,
    IOT_TRANSPORT_LISTENED  = 0x001,
} iot_transport_flag_t;

#define IOT_TRANSPORT_MODE(t) ((t)->flags & IOT_TRANSPORT_MODE_MASK)


#define IOT_TRANSPORT_OPT_TYPEMAP "type-map"

/*
 * transport requests
 *
 * Transport requests correspond to top-down event propagation in the
 * communication stack. These requests are made by the core tansport
 * abstraction layer to the underlying actual transport implementation
 * to carry out the implementation-specific details of some transport
 * operation.
 */

typedef struct {
    /** Open a new transport. */
    int  (*open)(iot_transport_t *t);
    /** Create a new transport from an existing backend object. */
    int  (*createfrom)(iot_transport_t *t, void *obj);
    /** Bind a transport to a given transport-specific address. */
    int  (*bind)(iot_transport_t *t, iot_sockaddr_t *addr, socklen_t addrlen);
    /** Listen on a transport for incoming connections. */
    int  (*listen)(iot_transport_t *t, int backlog);
    /** Accept a new transport connection over an existing transport. */
    int  (*accept)(iot_transport_t *t, iot_transport_t *lt);
    /** Connect a transport to an endpoint. */
    int  (*connect)(iot_transport_t *t, iot_sockaddr_t *addr,
                    socklen_t addrlen);
    /** Disconnect a transport, if it is connection-oriented. */
    int  (*disconnect)(iot_transport_t *t);
    /** Close a transport, free all resources from open/accept/connect. */
    void (*close)(iot_transport_t *t);
    /** Set a (possibly type specific) transport option. */
    int  (*setopt)(iot_transport_t *t, const char *opt, const void *value);
    /** Send raw data over a (connected) transport. */
    int (*sendraw)(iot_transport_t *t, void *buf, size_t size);
    /** Send a JSON message over a (connected) transport. */
    int (*sendjson)(iot_transport_t *t, iot_json_t *msg);
    /** Send raw data over a(n unconnected) transport. */
    int (*sendrawto)(iot_transport_t *t, void *buf, size_t size,
                     iot_sockaddr_t *addr, socklen_t addrlen);
    /** Send a JSON messgae over a(n unconnected) transport. */
    int (*sendjsonto)(iot_transport_t *t, iot_json_t *msg, iot_sockaddr_t *addr,
                      socklen_t addrlen);
} iot_transport_req_t;


/*
 * transport events
 *
 * Transport events correspond to bottom-up event propagation in the
 * communication stack. These callbacks are made by the actual transport
 * implementation to the generic transport abstraction to inform it
 * about relevant transport events, such as the reception of data, or
 * transport disconnection by the peer.
 */

typedef struct {
    /** Message received on a connected transport. */
    union {
        /** Raw data callback for connected transports. */
        void (*recvraw)(iot_transport_t *t, void *data, size_t size,
                        void *user_data);
        /** JSON type callback for connected transports. */
        void (*recvjson)(iot_transport_t *t, iot_json_t *msg, void *user_data);
    };

    /** Message received on an unconnected transport. */
    union {
        /** Raw data callback for unconnected transports. */
        void (*recvrawfrom)(iot_transport_t *t, void *data, size_t size,
                            iot_sockaddr_t *addr, socklen_t addrlen,
                            void *user_data);
        /** JSON type callback for unconnected transports. */
        void (*recvjsonfrom)(iot_transport_t *t, iot_json_t *msg,
                             iot_sockaddr_t *addr, socklen_t addrlen,
                             void *user_data);
    };
    /** Connection closed by peer. */
    void (*closed)(iot_transport_t *t, int error, void *user_data);
    /** Connection attempt on a socket being listened on. */
    void (*connection)(iot_transport_t *t, void *user_data);
} iot_transport_evt_t;


/*
 * transport descriptor
 */

typedef struct {
    const char          *type;           /* transport type name */
    size_t               size;           /* full transport struct size */
    iot_transport_req_t  req;            /* transport requests */
    socklen_t          (*resolve)(const char *str, iot_sockaddr_t *addr,
                                  socklen_t addrlen, const char **typep);
    iot_list_hook_t      hook;           /* to list of registered transports */
} iot_transport_descr_t;


/*
 * transport
 */

#define IOT_TRANSPORT_PUBLIC_FIELDS                                       \
    iot_mainloop_t          *ml;                                          \
    iot_transport_descr_t   *descr;                                       \
    iot_transport_evt_t      evt;                                         \
    int                    (*check_destroy)(iot_transport_t *t);          \
    int                    (*recv_data)(iot_transport_t *t, void *data,   \
                                        size_t size,                      \
                                        iot_sockaddr_t *addr,             \
                                        socklen_t addrlen);               \
    void                    *user_data;                                   \
    int                      flags;                                       \
    int                      mode;                                        \
    int                      busy;                                        \
    int                      connected : 1;                               \
    int                      listened : 1;                                \
    int                      destroyed : 1                                \


struct iot_transport_s {
    IOT_TRANSPORT_PUBLIC_FIELDS;
};


/*
 * Notes:
 *
 *    Transports can get destructed in two slightly different ways.
 *
 *    1)
 *      Someone calls iot_transport_destroy while the transport is
 *      idle, ie. with no callbacks or operations being active. This
 *      is simple and straightforward:
 *         - iot_transport_destroy calls req.disconnect
 *         - iot_transport_destroy calls req.close
 *         - iot_transport_destroy check and sees the transport is idle
 *           so it frees the transport
 *
 *    2)
 *      Someone calls iot_tansport_destroy while the transport is
 *      busy, ie. it has an unfinished callback or operation running.
 *      This typically happens when an operation or callback function,
 *      or a user function called from either of those calls
 *      iot_transport_destroy as a result of a received message, or a
 *      (communication) error. In this case destroying the transport
 *      is less straightforward and needs to get delayed to avoid
 *      shooting out the transport underneath the active operation or
 *      callback.
 *
 *    To handle the latter case, the generic (ie. top-level) transport
 *    layer has a member function check_destroy. This function checks
 *    for pending destroy requests and destroys the transport if it
 *    is not busy. All transport backends MUST CALL this function and
 *    CHECK ITS RETURN VALUE, whenever a user callback or a transport
 *    callback (ie. bottom-up event propagation) function invoked by
 *    the backend returns.
 *
 *    If the transport has been left intact, check_destroy returns
 *    FALSE and processing can continue normally, taking into account
 *    that any transport state stored locally in the stack frame of the
 *    backend function might have changed during the callback. However,
 *    if check_destroy returns TRUE, it has nuked the transport and the
 *    backend MUST NOT touch or try to dereference the transport any more
 *    as its resources have already been released.
 */


/*
 * convenience macros
 */

/**
 * Macro to mark a transport busy while running a block of code.
 *
 * The backend needs to make sure the transport is not freed while a
 * transport request or event callback function is active. Similarly,
 * the backend needs to check if the transport has been marked for
 * destruction whenever an event callback returns and trigger the
 * destruction if it is necessary and possible (ie. the above criterium
 * of not being active is fullfilled).
 *
 * These are the easiest to accomplish using the provided IOT_TRANSPORT_BUSY
 * macro and the check_destroy callback member provided by iot_transport_t.
 *
 *     1) Use the provided IOT_TRANSPORT_BUSY macro to enclose al blocks of
 *        code that invoke event callbacks. Do not do a return directly
 *        from within the enclosed call blocks, rather just set a flag
 *        within the block, check it after the block and do the return
 *        from there if necessary.
 *
 *     2) Call iot_transport_t->check_destroy after any call to an event
 *        callback. check_destroy will check for any pending destroy
 *        request and perform the actual destruction if it is necessary
 *        and possible. If the transport has been left intact, check_destroy
 *        returns FALSE. However, if the transport has been destroyed and
 *        freed it returns TRUE, in which case the caller must not attempt
 *        to use or dereference the transport data structures any more.
 */


#ifndef __IOT_TRANSPORT_DISABLE_CODE_CHECK__
#  define W iot_log_error
#  define __TRANSPORT_CHK_BLOCK(...) do {                                  \
        static int __checked = FALSE, __warned = FALSE;                    \
                                                                           \
        if (IOT_UNLIKELY(!__checked)) {                                    \
            __checked = TRUE;                                              \
            if (IOT_UNLIKELY(!__warned &&                                  \
                             strstr(#__VA_ARGS__, "return") != NULL)) {    \
                W("*********************** WARNING ********************"); \
                W("* You seem to directly do a return from a block of *"); \
                W("* code protected by IOT_TRANSPORT_BUSY. Are you    *"); \
                W("* absolutely sure you know what you are doing and  *"); \
                W("* that you are also doing it correctly ?           *"); \
                W("****************************************************"); \
                W("The suspicious code block is located at: ");            \
                W("  %s@%s:%d", __FUNCTION__, __FILE__, __LINE__);         \
                W("and it looks like this:");                              \
                W("---------------------------------------------");        \
                W("%s", #__VA_ARGS__);                                     \
                W("---------------------------------------------");        \
                W("If you understand what IOT_TRANSPORT_BUSY does and");   \
                W("how, and you are sure about the corretness of your");   \
                W("code you can disable this error message by");           \
                W("#defining __IOT_TRANSPORT_DISABLE_CODE_CHECK__");       \
                W("when compiling %s.", __FILE__);                         \
                __warned = TRUE;                                           \
            }                                                              \
        }                                                                  \
    } while (0)
#else
#  define __TRANSPORT_CHK_BLOCK(...) do { } while (0)
#endif

#define IOT_TRANSPORT_BUSY(t, ...) do {                \
        __TRANSPORT_CHK_BLOCK(__VA_ARGS__);        \
        (t)->busy++;                                \
        __VA_ARGS__                                \
        (t)->busy--;                                \
    } while (0)



/** Automatically register a transport on startup. */
#define IOT_REGISTER_TRANSPORT(_prfx, _typename, _structtype, _resolve,   \
                               _open, _createfrom, _close, _setopt,       \
                               _bind, _listen, _accept,                   \
                               _connect, _disconnect,                     \
                               _sendraw, _sendrawto,                      \
                               _sendjson, _sendjsonto)                    \
    static void _prfx##_register_transport(void)                          \
         __attribute__((constructor));                                    \
                                                                          \
    static void _prfx##_register_transport(void) {                        \
        static iot_transport_descr_t descriptor = {                       \
            .type    = _typename,                                         \
            .size    = sizeof(_structtype),                               \
            .resolve = _resolve,                                          \
            .req     = {                                                  \
                .open         = _open,                                    \
                .createfrom   = _createfrom,                              \
                .bind         = _bind,                                    \
                .listen       = _listen,                                  \
                .accept       = _accept,                                  \
                .close        = _close,                                   \
                .setopt       = _setopt,                                  \
                .connect      = _connect,                                 \
                .disconnect   = _disconnect,                              \
                .sendraw      = _sendraw,                                 \
                .sendrawto    = _sendrawto,                               \
                .sendjson     = _sendjson,                                \
                .sendjsonto   = _sendjsonto,                              \
            },                                                            \
        };                                                                \
                                                                          \
        if (!iot_transport_register(&descriptor))                         \
            iot_log_error("Failed to register transport '%s'.",           \
                          _typename);                                     \
        else                                                              \
            iot_log_info("Registered transport '%s'.", _typename);        \
    }                                                                     \
    struct iot_allow_trailing_semicolon



/** Register a new transport type. */
int iot_transport_register(iot_transport_descr_t *d);

/** Unregister a transport. */
void iot_transport_unregister(iot_transport_descr_t *d);

/** Create a new transport. */
iot_transport_t *iot_transport_create(iot_mainloop_t *ml, const char *type,
                                      iot_transport_evt_t *evt,
                                      void *user_data, int flags);

/** Create a new transport from a backend object. */
iot_transport_t *iot_transport_create_from(iot_mainloop_t *ml, const char *type,
                                           void *conn, iot_transport_evt_t *evt,
                                           void *user_data, int flags,
                                           int state);

/** Set a (possibly type-specific) transport option. */
int iot_transport_setopt(iot_transport_t *t, const char *opt, const void *val);

/** Resolve an address string to a transport-specific address. */
socklen_t iot_transport_resolve(iot_transport_t *t, const char *str,
                                iot_sockaddr_t *addr, socklen_t addrlen,
                                const char **type);

/** Bind a given transport to a transport-specific address. */
int iot_transport_bind(iot_transport_t *t, iot_sockaddr_t *addr,
                       socklen_t addrlen);

/** Listen for incoming connection on the given transport. */
int  iot_transport_listen(iot_transport_t *t, int backlog);

/** Accept and create a new transport connection. */
iot_transport_t *iot_transport_accept(iot_transport_t *t,
                                      void *user_data, int flags);

/** Destroy a transport. */
void iot_transport_destroy(iot_transport_t *t);

/** Connect a transport to the given address. */
int iot_transport_connect(iot_transport_t *t, iot_sockaddr_t  *addr,
                          socklen_t addrlen);

/** Disconnect a transport. */
int iot_transport_disconnect(iot_transport_t *t);

/** Send raw data through the given (connected) transport. */
int iot_transport_sendraw(iot_transport_t *t, void *data, size_t size);

/** Send raw data through the given transport to the remote address. */
int iot_transport_sendrawto(iot_transport_t *t, void *data, size_t size,
                            iot_sockaddr_t *addr, socklen_t addrlen);

/** Send a JSON message through the given (connected) transport. */
int iot_transport_sendjson(iot_transport_t *t, iot_json_t *msg);

/** Send a JSON message through the given transport to the remote address. */
int iot_transport_sendjsonto(iot_transport_t *t, iot_json_t *msg,
                             iot_sockaddr_t *addr, socklen_t addrlen);
IOT_CDECL_END

#endif /* __IOT_TRANSPORT_H__ */
