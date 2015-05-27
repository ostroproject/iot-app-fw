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

#include <string.h>
#include <errno.h>

#include <iot/common/mm.h>
#include <iot/common/list.h>
#include <iot/common/log.h>
#include <iot/common/transport.h>

static int check_destroy(iot_transport_t *t);
static int recv_data(iot_transport_t *t, void *data, size_t size,
                     iot_sockaddr_t *addr, socklen_t addrlen);
static inline int purge_destroyed(iot_transport_t *t);


static IOT_LIST_HOOK(transports);
static iot_sighandler_t *pipe_handler;


static int check_request_callbacks(iot_transport_req_t *req)
{
    /* XXX TODO: hmm... this probably needs more thought/work */

    if (!req->open || !req->close)
        return FALSE;

    if (req->accept) {
        if (!req->sendraw || !req->sendjson)
            return FALSE;
    }
    else {
        if (!req->sendrawto || !req->sendjsonto)
            return FALSE;
    }

    if (( req->connect && !req->disconnect) ||
        (!req->connect &&  req->disconnect))
        return FALSE;

    return TRUE;
}


int iot_transport_register(iot_transport_descr_t *d)
{
    if (!check_request_callbacks(&d->req))
        return FALSE;

    if (d->size >= sizeof(iot_transport_t)) {
        iot_list_init(&d->hook);
        iot_list_append(&transports, &d->hook);

        return TRUE;
    }
    else
        return FALSE;
}


void iot_transport_unregister(iot_transport_descr_t *d)
{
    iot_list_delete(&d->hook);
}


static iot_transport_descr_t *find_transport(const char *type)
{
    iot_transport_descr_t *d;
    iot_list_hook_t       *p, *n;

    iot_list_foreach(&transports, p, n) {
        d = iot_list_entry(p, typeof(*d), hook);
        if (!strcmp(d->type, type))
            return d;
    }

    return NULL;
}


static int check_event_callbacks(iot_transport_evt_t *evt)
{
    /*
     * For connection-oriented transports we require a recv* callback
     * and a closed callback.
     *
     * For connectionless transports we only require a recvfrom* callback.
     * A recv* callback is optional, however the transport cannot be put
     * to connected mode (usually for doing sender-based filtering) if
     * recv* is omitted.
     */

    if (evt->connection != NULL) {
        if (evt->recvraw == NULL || evt->closed == NULL)
            return FALSE;
    }
    else {
        if (evt->recvrawfrom == NULL)
            return FALSE;
    }

    return TRUE;
}


static void sigpipe_handler(iot_sighandler_t *h, int sig, void *user_data)
{
    IOT_UNUSED(h);
    IOT_UNUSED(user_data);

    iot_debug("caught signal %d (%s)...", sig, strsignal(sig));
}


iot_transport_t *iot_transport_create(iot_mainloop_t *ml, const char *type,
                                      iot_transport_evt_t *evt, void *user_data,
                                      int flags)
{
    iot_transport_descr_t *d;
    iot_transport_t       *t;

    if (!pipe_handler)
        pipe_handler = iot_add_sighandler(ml, SIGPIPE, sigpipe_handler, NULL);

    if (!check_event_callbacks(evt)) {
        errno = EINVAL;
        return NULL;
    }

    if ((d = find_transport(type)) != NULL) {
        if ((t = iot_allocz(d->size)) != NULL) {
            t->descr     = d;
            t->ml        = ml;
            t->evt       = *evt;
            t->user_data = user_data;

            t->check_destroy = check_destroy;
            t->recv_data     = recv_data;
            t->flags         = flags & ~IOT_TRANSPORT_MODE_MASK;
            t->mode          = flags &  IOT_TRANSPORT_MODE_MASK;

            if (!t->descr->req.open(t)) {
                iot_free(t);
                t = NULL;
            }
        }
    }
    else
        t = NULL;

    return t;
}


iot_transport_t *iot_transport_create_from(iot_mainloop_t *ml, const char *type,
                                           void *conn, iot_transport_evt_t *evt,
                                           void *user_data, int flags,
                                           int state)
{
    iot_transport_descr_t *d;
    iot_transport_t       *t;

    if (!pipe_handler)
        pipe_handler = iot_add_sighandler(ml, SIGPIPE, sigpipe_handler, NULL);

    if (!check_event_callbacks(evt)) {
        errno = EINVAL;
        return NULL;
    }

    if ((d = find_transport(type)) != NULL) {
        if ((t = iot_allocz(d->size)) != NULL) {
            t->descr     = d;
            t->ml        = ml;
            t->evt       = *evt;
            t->user_data = user_data;

            t->check_destroy = check_destroy;
            t->recv_data     = recv_data;
            t->flags         = flags & ~IOT_TRANSPORT_MODE_MASK;
            t->mode          = flags &  IOT_TRANSPORT_MODE_MASK;

            t->connected = !!(state & IOT_TRANSPORT_CONNECTED);
            t->listened  = !!(state & IOT_TRANSPORT_LISTENED);

            if (t->connected && t->listened) {
                iot_free(t);
                return NULL;
            }

            if (!t->descr->req.createfrom(t, conn)) {
                iot_free(t);
                t = NULL;
            }
        }
    }
    else
        t = NULL;

    return t;
}


int iot_transport_setopt(iot_transport_t *t, const char *opt, const void *val)
{
    if (t != NULL) {
        if (t->descr->req.setopt != NULL)
            return t->descr->req.setopt(t, opt, val);
    }

    return FALSE;
}


static inline int type_matches(const char *type, const char *addr)
{
    while (*type == *addr)
        type++, addr++;

    return (*type == '\0' && *addr == ':');
}


socklen_t iot_transport_resolve(iot_transport_t *t, const char *str,
                                iot_sockaddr_t *addr, socklen_t size,
                                const char **typep)
{
    iot_transport_descr_t *d;
    iot_list_hook_t       *p, *n;
    socklen_t              l;

    if (t != NULL)
        return t->descr->resolve(str, addr, size, typep);
    else {
        iot_list_foreach(&transports, p, n) {
            d = iot_list_entry(p, typeof(*d), hook);
            l = d->resolve(str, addr, size, typep);

            if (l > 0)
                return l;
        }
    }

    return 0;
}


int iot_transport_bind(iot_transport_t *t, iot_sockaddr_t *addr,
                       socklen_t addrlen)
{
    if (t != NULL) {
        if (t->descr->req.bind != NULL)
            return t->descr->req.bind(t, addr, addrlen);
        else
            return TRUE;                  /* assume no binding is needed */
    }
    else
        return FALSE;
}


int iot_transport_listen(iot_transport_t *t, int backlog)
{
    int result;

    if (t != NULL) {
        if (t->descr->req.listen != NULL) {
            IOT_TRANSPORT_BUSY(t, {
                    result = t->descr->req.listen(t, backlog);
                });

            purge_destroyed(t);

            return result;
        }
    }

    return FALSE;
}


iot_transport_t *iot_transport_accept(iot_transport_t *lt,
                                      void *user_data, int flags)
{
    iot_transport_t *t;

    if ((t = iot_allocz(lt->descr->size)) != NULL) {
        bool failed  = FALSE;
        t->descr     = lt->descr;
        t->ml        = lt->ml;
        t->evt       = lt->evt;
        t->user_data = user_data;

        t->check_destroy = check_destroy;
        t->recv_data     = recv_data;
        t->flags         = (lt->flags & IOT_TRANSPORT_INHERIT) | flags;
        t->flags         = t->flags & ~IOT_TRANSPORT_MODE_MASK;
        t->mode          = lt->mode;

        IOT_TRANSPORT_BUSY(t, {
                if (!t->descr->req.accept(t, lt)) {
                    failed = TRUE;
                }
                else {
                    t->connected = TRUE;
                }
            });

        if (failed) {
            iot_free(t);
            t = NULL;
        }
    }

    return t;
}


static inline int purge_destroyed(iot_transport_t *t)
{
    if (t->destroyed && !t->busy) {
        iot_debug("destroying transport %p...", t);
        iot_free(t);
        return TRUE;
    }
    else
        return FALSE;
}


void iot_transport_destroy(iot_transport_t *t)
{
    if (t != NULL) {
        t->destroyed = TRUE;

        IOT_TRANSPORT_BUSY(t, {
                t->descr->req.disconnect(t);
                t->descr->req.close(t);
            });

        purge_destroyed(t);
    }
}


static int check_destroy(iot_transport_t *t)
{
    return purge_destroyed(t);
}


int iot_transport_connect(iot_transport_t *t, iot_sockaddr_t *addr,
                          socklen_t addrlen)
{
    int result;

    if (!t->connected) {

        /* make sure we can deliver reception noifications */
        if (t->evt.recvraw == NULL) {
            errno = EINVAL;
            return FALSE;
        }

        IOT_TRANSPORT_BUSY(t, {
                if (t->descr->req.connect(t, addr, addrlen))  {
                    t->connected = TRUE;
                    result       = TRUE;
                }
                else
                    result = FALSE;
            });

        purge_destroyed(t);
    }
    else {
        errno  = EISCONN;
        result = FALSE;
    }

    return result;
}


int iot_transport_disconnect(iot_transport_t *t)
{
    int result;

    if (t != NULL && t->connected) {
        IOT_TRANSPORT_BUSY(t, {
                if (t->descr->req.disconnect(t)) {
                    t->connected = FALSE;
                    result       = TRUE;
                }
                else
                    result = TRUE;
            });

        purge_destroyed(t);
    }
    else
        result = FALSE;

    return result;
}


int iot_transport_sendraw(iot_transport_t *t, void *data, size_t size)
{
    int result;

    if (t->connected &&
        t->mode == IOT_TRANSPORT_MODE_RAW && t->descr->req.sendraw) {
        IOT_TRANSPORT_BUSY(t, {
                result = t->descr->req.sendraw(t, data, size);
            });

        purge_destroyed(t);
    }
    else
        result = FALSE;

    return result;
}


int iot_transport_sendrawto(iot_transport_t *t, void *data, size_t size,
                            iot_sockaddr_t *addr, socklen_t addrlen)
{
    int result;

    if (t->mode == IOT_TRANSPORT_MODE_RAW && t->descr->req.sendrawto) {
        IOT_TRANSPORT_BUSY(t, {
                result = t->descr->req.sendrawto(t, data, size, addr, addrlen);
            });

        purge_destroyed(t);
    }
    else
        result = FALSE;

    return result;
}


int iot_transport_sendjson(iot_transport_t *t, iot_json_t *msg)
{
    int result;

    if (t->mode == IOT_TRANSPORT_MODE_JSON && t->descr->req.sendjson) {
        IOT_TRANSPORT_BUSY(t, {
                result = t->descr->req.sendjson(t, msg);
            });

        purge_destroyed(t);
    }
    else
        result = FALSE;

    return result;
}


int iot_transport_sendjsonto(iot_transport_t *t, iot_json_t *msg,
                             iot_sockaddr_t *addr, socklen_t addrlen)
{
    int result;

    if (t->mode == IOT_TRANSPORT_MODE_JSON && t->descr->req.sendjsonto) {
        IOT_TRANSPORT_BUSY(t, {
                result = t->descr->req.sendjsonto(t, msg, addr, addrlen);
            });

        purge_destroyed(t);
    }
    else
        result = FALSE;

    return result;
}


static int recv_data(iot_transport_t *t, void *data, size_t size,
                     iot_sockaddr_t *addr, socklen_t addrlen)
{
    switch (t->mode) {
    case IOT_TRANSPORT_MODE_RAW:
        if (t->connected) {
            IOT_TRANSPORT_BUSY(t, {
                    t->evt.recvraw(t, data, size, t->user_data);
                });
        }
        else {
            IOT_TRANSPORT_BUSY(t, {
                    t->evt.recvrawfrom(t, data, size, addr, addrlen,
                                       t->user_data);
                });
        }
        return 0;

    case IOT_TRANSPORT_MODE_JSON:
        if (t->connected) {
            if (t->evt.recvjson) {
                IOT_TRANSPORT_BUSY(t, {
                        t->evt.recvjson(t, data, t->user_data);
                    });
            }
        }
        else {
            if (t->evt.recvjsonfrom) {
                IOT_TRANSPORT_BUSY(t, {
                        t->evt.recvjsonfrom(t, data, addr, addrlen,
                                            t->user_data);
                    });
            }
        }
        return 0;

    default:
        return -EPROTOTYPE;
    }
}

