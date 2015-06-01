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

#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <netdb.h>
#include <fcntl.h>
#define _GNU_SOURCE
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <netinet/in.h>
#include <sys/un.h>
#include <sys/uio.h>

#include <iot/common/macros.h>
#include <iot/common/mm.h>
#include <iot/common/log.h>
#include <iot/common/fragbuf.h>
#include <iot/common/socket-utils.h>
#include <iot/common/transport.h>

#ifndef UNIX_PATH_MAX
#    define UNIX_PATH_MAX sizeof(((struct sockaddr_un *)NULL)->sun_path)
#endif

#define TCP4  "tcp4"
#define TCP4L 4
#define TCP6  "tcp6"
#define TCP6L 4
#define UNXS  "unxs"
#define UNXSL 4

#define DEFAULT_SIZE 128                 /* default input buffer size */

typedef struct {
    IOT_TRANSPORT_PUBLIC_FIELDS;         /* common transport fields */
    int             sock;                /* TCP socket */
    iot_io_watch_t *iow;                 /* socket I/O watch */
    iot_fragbuf_t  *buf;                 /* fragment buffer */
} strm_t;


static void strm_recv_cb(iot_io_watch_t *w, int fd, iot_io_event_t events,
                         void *user_data);
static int strm_disconnect(iot_transport_t *mt);
static int open_socket(strm_t *t, int family);



static int parse_address(const char *str, int *familyp, char *nodep,
                         size_t nsize, char **servicep, const char **typep)
{
    char       *node, *service;
    const char *type;
    int         family;
    size_t      l, nl;

    node = (char *)str;

    if (!strncmp(node, TCP4":", l=TCP4L+1)) {
        family = AF_INET;
        type   = TCP4;
        node  += l;
    }
    else if (!strncmp(node, TCP6":", l=TCP6L+1)) {
        family = AF_INET6;
        type   = TCP6;
        node  += l;
    }
    else if (!strncmp(node, UNXS":", l=UNXSL+1)) {
        family = AF_UNIX;
        type   = UNXS;
        node  += l;
    }
    else {
        if      (node[0] == '[') family = AF_INET6;
        else if (node[0] == '/') family = AF_UNIX;
        else if (node[0] == '@') family = AF_UNIX;
        else                     family = AF_UNSPEC;

        type = NULL;
    }

    switch (family) {
    case AF_INET:
        service = strrchr(node, ':');
        if (service == NULL) {
            errno = EINVAL;
            return -1;
        }

        nl = service - node;
        service++;

    case AF_INET6:
        service = strrchr(node, ':');

        if (service == NULL || service == node) {
            errno = EINVAL;
            return -1;
        }

        if (node[0] == '[') {
            node++;

            if (service[-1] != ']') {
                errno = EINVAL;
                return -1;
            }

            nl = service - node - 1;
        }
        else
            nl = service - node;

        service++;
        break;

    case AF_UNSPEC:
        if (!strncmp(node, "tcp:", l=4))
            node += l;
        service = strrchr(node, ':');

        if (service == NULL || service == node) {
            errno = EINVAL;
            return -1;
        }

        if (node[0] == '[') {
            node++;
            family = AF_INET6;

            if (service[-1] != ']') {
                errno = EINVAL;
                return -1;
            }

            nl = service - node - 1;
        }
        else {
            family = AF_INET;
            nl = service - node;
        }
        service++;
        break;

    case AF_UNIX:
        service = NULL;
        nl      = strlen(node);
    }

    if (nl >= nsize) {
        errno = ENOMEM;
        return -1;
    }

    strncpy(nodep, node, nl);
    nodep[nl] = '\0';
    *servicep = service;
    *familyp  = family;
    if (typep != NULL)
        *typep = type;

    return 0;
}


static socklen_t strm_resolve(const char *str, iot_sockaddr_t *addr,
                              socklen_t size, const char **typep)
{
    struct addrinfo    *ai, hints;
    struct sockaddr_un *un;
    char                node[UNIX_PATH_MAX], *port;
    socklen_t           len;

    iot_clear(&hints);

    if (parse_address(str, &hints.ai_family, node, sizeof(node),
                      &port, typep) < 0)
        return 0;

    switch (hints.ai_family) {
    case AF_UNIX:
        un  = &addr->unx;
        len = IOT_OFFSET(typeof(*un), sun_path) + strlen(node) + 1;

        if (size < len)
            errno = ENOMEM;
        else {
            un->sun_family = AF_UNIX;
            strncpy(un->sun_path, node, UNIX_PATH_MAX-1);
            if (un->sun_path[0] == '@')
                un->sun_path[0] = '\0';
        }

        /* When binding the socket, we don't need the null at the end */
        len--;

        break;

    case AF_INET:
    case AF_INET6:
    default:
        if (getaddrinfo(node, port, &hints, &ai) == 0) {
            if (ai->ai_addrlen <= size) {
                memcpy(addr, ai->ai_addr, ai->ai_addrlen);
                len = ai->ai_addrlen;
            }
            else
                len = 0;

            freeaddrinfo(ai);
        }
        else
            len = 0;
    }

    return len;
}


static int strm_open(iot_transport_t *mt)
{
    strm_t *t = (strm_t *)mt;

    t->sock = -1;

    return TRUE;
}


static int set_nonblocking(int sock, int nonblocking)
{
    long nb = (nonblocking ? 1 : 0);

    return fcntl(sock, F_SETFL, O_NONBLOCK, nb);
}


static int set_cloexec(int fd, int cloexec)
{
    int on = cloexec ? 1 : 0;

    return fcntl(fd, F_SETFL, O_CLOEXEC, on);
}


static int set_reuseaddr(int sock, int reuseaddr)
{
    int on;

    if (reuseaddr) {
        on = 1;
        return setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));
    }
    else
        return 0;
}


static int strm_createfrom(iot_transport_t *mt, void *conn)
{
    strm_t           *t = (strm_t *)mt;
    iot_io_event_t   events;

    t->sock = *(int *)conn;

    if (t->sock >= 0) {
        if (mt->flags & IOT_TRANSPORT_REUSEADDR)
            if (set_reuseaddr(t->sock, true) < 0)
                return FALSE;

        if (mt->flags & IOT_TRANSPORT_NONBLOCK || t->listened)
            if (set_nonblocking(t->sock, true) < 0)
                return FALSE;

        if (t->connected || t->listened) {
            if (!t->connected ||
                (t->buf = iot_fragbuf_create(TRUE, 0)) != NULL) {
                events = IOT_IO_EVENT_IN | IOT_IO_EVENT_HUP;
                t->iow = iot_add_io_watch(t->ml, t->sock, events,
                                          strm_recv_cb, t);

                if (t->iow != NULL)
                    return TRUE;

                iot_fragbuf_destroy(t->buf);
                t->buf = NULL;
            }
        }
    }

    return FALSE;
}


static void strm_close(iot_transport_t *mt)
{
    strm_t *t = (strm_t *)mt;

    iot_debug("closing transport %p", mt);

    iot_del_io_watch(t->iow);
    t->iow = NULL;

    iot_fragbuf_destroy(t->buf);
    t->buf = NULL;

    if (t->sock >= 0){
        close(t->sock);
        t->sock = -1;
    }
}


static int strm_bind(iot_transport_t *mt, iot_sockaddr_t *addr,
                     socklen_t addrlen)
{
    strm_t *t = (strm_t *)mt;

    if (t->sock != -1 || open_socket(t, addr->any.sa_family)) {
        if (bind(t->sock, &addr->any, addrlen) == 0) {
            iot_debug("transport %p bound", mt);
            return TRUE;
        }
    }

    iot_debug("failed to bind transport %p", mt);
    return FALSE;
}


static int strm_listen(iot_transport_t *mt, int backlog)
{
    strm_t *t = (strm_t *)mt;

    if (t->sock != -1 && t->iow != NULL && t->evt.connection != NULL) {
        if (set_nonblocking(t->sock, true) < 0)
            return FALSE;

        if (listen(t->sock, backlog) == 0) {
            iot_debug("transport %p listening", mt);
            t->listened = TRUE;
            return TRUE;
        }
    }

    iot_debug("transport %p failed to listen", mt);
    return FALSE;
}


static int strm_accept(iot_transport_t *mt, iot_transport_t *mlt)
{
    strm_t         *t, *lt;
    iot_sockaddr_t  addr;
    socklen_t       addrlen;
    iot_io_event_t  events;

    t  = (strm_t *)mt;
    lt = (strm_t *)mlt;

    if (lt->sock < 0) {
        errno = EBADF;

        return FALSE;
    }

    addrlen = sizeof(addr);
    t->sock = accept(lt->sock, &addr.any, &addrlen);

    if (t->sock >= 0) {
        if (mt->flags & IOT_TRANSPORT_REUSEADDR)
            if (set_reuseaddr(t->sock, true) < 0)
                goto reject;

        if (mt->flags & IOT_TRANSPORT_NONBLOCK)
            if (set_nonblocking(t->sock, true) < 0)
                goto reject;

        if (mt->flags & IOT_TRANSPORT_CLOEXEC)
            if (set_cloexec(t->sock, true) < 0)
                goto reject;

        t->buf = iot_fragbuf_create(TRUE, 0);
        events = IOT_IO_EVENT_IN | IOT_IO_EVENT_HUP;
        t->iow = iot_add_io_watch(t->ml, t->sock, events, strm_recv_cb, t);

        if (t->iow != NULL && t->buf != NULL) {
            iot_debug("accepted connection on transport %p/%p", mlt, mt);
            return TRUE;
        }
        else {
            iot_fragbuf_destroy(t->buf);
            t->buf = NULL;
            close(t->sock);
            t->sock = -1;
        }
    }
    else {
    reject:
        if (iot_reject_connection(lt->sock, NULL, 0) < 0) {
            iot_log_error("%s(): accept failed, closing transport %p (%d: %s).",
                          __FUNCTION__, mlt, errno, strerror(errno));
            strm_close(mlt);

            /* Notes:
             *     Unfortunately we cannot safely emit a closed event here.
             *     The closed event is semantically attached to an accepted
             *     tranport being closed and there is no equivalent for a
             *     listening transport (we should have had a generic error
             *     event). There for the transport owner expects and treats
             *     (IOW casts) the associated user_data accordingly. That
             *     would end up in a disaster... Once we cleanup/rework the
             *     transport infra, this needs to be done better.
             */
        }
        else
            iot_log_error("%s(): rejected connection for transport %p (%d: %s).",
                          __FUNCTION__, mlt, errno, strerror(errno));
    }

    return FALSE;
}


static void strm_recv_cb(iot_io_watch_t *w, int fd, iot_io_event_t events,
                         void *user_data)
{
    strm_t          *t  = (strm_t *)user_data;
    iot_transport_t *mt = (iot_transport_t *)t;
    void            *data, *buf;
    uint32_t         pending;
    size_t           size;
    ssize_t          n;
    int              error;

    IOT_UNUSED(w);

    iot_debug("event 0x%x for transport %p", events, t);

    if (events & IOT_IO_EVENT_IN) {
        if (IOT_UNLIKELY(mt->listened != 0)) {
            IOT_TRANSPORT_BUSY(mt, {
                    iot_debug("connection event on transport %p", mt);
                    mt->evt.connection(mt, mt->user_data);
                });

            t->check_destroy(mt);
            return;
        }

        while (ioctl(fd, FIONREAD, &pending) == 0 && pending > 0) {
            buf = iot_fragbuf_alloc(t->buf, pending);

            if (buf == NULL) {
                error = ENOMEM;
            fatal_error:
                iot_debug("transport %p closed with error %d", mt, error);
            closed:
                strm_disconnect(mt);

                if (t->evt.closed != NULL)
                    IOT_TRANSPORT_BUSY(mt, {
                            mt->evt.closed(mt, error, mt->user_data);
                        });

                t->check_destroy(mt);
                return;
            }

            n = read(fd, buf, pending);

            if (n >= 0) {
                if (n < (ssize_t)pending)
                    iot_fragbuf_trim(t->buf, buf, pending, n);
            }

            if (n < 0 && errno != EAGAIN) {
                error = EIO;
                goto fatal_error;
            }
        }

        data = NULL;
        size = 0;
        while (iot_fragbuf_pull(t->buf, &data, &size)) {
            if (t->mode != IOT_TRANSPORT_MODE_JSON)
                error = t->recv_data(mt, data, size, NULL, 0);
            else {
                iot_json_t *msg = iot_json_string_to_object(data, size);

                if (msg != NULL) {
                    error = t->recv_data((iot_transport_t *)t, msg, 0, NULL, 0);
                    iot_json_unref(msg);
                }
                else
                    error = EILSEQ;
            }

            if (error)
                goto fatal_error;

            if (t->check_destroy(mt))
                return;
        }
    }

    if (events & IOT_IO_EVENT_HUP) {
        iot_debug("transport %p closed by peer", mt);
        error = 0;
        goto closed;
    }
}


static int open_socket(strm_t *t, int family)
{
    iot_io_event_t events;

    t->sock = socket(family, SOCK_STREAM, 0);

    if (t->sock != -1) {
        if (t->flags & IOT_TRANSPORT_REUSEADDR)
            if (set_reuseaddr(t->sock, true) < 0)
                goto fail;

        if (t->flags & IOT_TRANSPORT_NONBLOCK)
            if (set_nonblocking(t->sock, true) < 0)
                goto fail;

        if (t->flags & IOT_TRANSPORT_CLOEXEC)
            if (set_cloexec(t->sock, true) < 0)
                goto fail;

        events = IOT_IO_EVENT_IN | IOT_IO_EVENT_HUP;
        t->iow = iot_add_io_watch(t->ml, t->sock, events, strm_recv_cb, t);

        if (t->iow != NULL)
            return TRUE;
        else {
        fail:
            close(t->sock);
            t->sock = -1;
        }
    }

    return FALSE;
}


static int strm_connect(iot_transport_t *mt, iot_sockaddr_t *addr,
                        socklen_t addrlen)
{
    strm_t         *t    = (strm_t *)mt;
    iot_io_event_t  events;

    t->sock = socket(addr->any.sa_family, SOCK_STREAM, 0);

    if (t->sock < 0)
        goto fail;

    if (connect(t->sock, &addr->any, addrlen) == 0) {
        if (set_reuseaddr(t->sock, true)   < 0 ||
            set_nonblocking(t->sock, true) < 0)
            goto close_and_fail;

        t->buf = iot_fragbuf_create(TRUE, 0);

        if (t->buf != NULL) {
            events = IOT_IO_EVENT_IN | IOT_IO_EVENT_HUP;
            t->iow = iot_add_io_watch(t->ml, t->sock, events, strm_recv_cb, t);

            if (t->iow != NULL) {
                iot_debug("connected transport %p", mt);

                return TRUE;
            }

            iot_fragbuf_destroy(t->buf);
            t->buf = NULL;
        }
    }

    if (t->sock != -1) {
    close_and_fail:
        close(t->sock);
        t->sock = -1;
    }

 fail:
    iot_debug("failed to connect transport %p", mt);

    return FALSE;
}


static int strm_disconnect(iot_transport_t *mt)
{
    strm_t *t = (strm_t *)mt;

    if (t->connected/* || t->iow != NULL*/) {
        iot_del_io_watch(t->iow);
        t->iow = NULL;

        shutdown(t->sock, SHUT_RDWR);

        iot_fragbuf_destroy(t->buf);
        t->buf = NULL;

        iot_debug("disconnected transport %p", mt);

        return TRUE;
    }
    else
        return FALSE;
}


static int strm_getopt(iot_transport_t *mt, const char *opt, void *val,
                       socklen_t *len)
{
    strm_t *t = (strm_t *)mt;

    if (!t->connected) {
        errno = ENOTCONN;
        return FALSE;
    }

    if (!strcmp(opt, IOT_TRANSPORT_OPT_PEERCRED)) {
        if (getsockopt(t->sock, SOL_SOCKET, SO_PEERCRED, val, len) < 0)
            return FALSE;
        else
            return TRUE;
    }

    if (!strcmp(opt, IOT_TRANSPORT_OPT_PEERSEC)) {
        if (getsockopt(t->sock, SOL_SOCKET, SO_PEERSEC, val, len) < 0)
            return FALSE;
        else
            return TRUE;
    }

    errno = EOPNOTSUPP;
    return FALSE;
}


static int strm_sendraw(iot_transport_t *mt, void *data, size_t size)
{
    strm_t  *t = (strm_t *)mt;
    ssize_t  n;

    if (t->connected) {
        n = write(t->sock, data, size);

        if (n == (ssize_t)size)
            return TRUE;
        else {
            if (n == -1 && errno == EAGAIN) {
                iot_log_error("%s(): XXX TODO: this sucks, need to add "
                              "output queuing for strm-transport.",
                              __FUNCTION__);
            }
        }
    }

    return FALSE;
}


static int strm_sendjson(iot_transport_t *mt, iot_json_t *msg)
{
    strm_t       *t = (strm_t *)mt;
    struct iovec  iov[2];
    const char   *s;
    ssize_t       size, n;
    uint32_t      len;

    if (t->connected && (s = iot_json_object_to_string(msg)) != NULL) {
        size = strlen(s);
        len  = htobe32(size);
        iov[0].iov_base = &len;
        iov[0].iov_len  = sizeof(len);
        iov[1].iov_base = (void *)s;
        iov[1].iov_len  = size;

        n = writev(t->sock, iov, 2);

        if (n == (ssize_t)(size + sizeof(len)))
            return TRUE;
        else {
            if (n == -1 && errno == EAGAIN) {
                iot_log_error("%s(): XXX TODO: this sucks, need to add "
                              "output queuing for strm-transport.",
                              __FUNCTION__);
            }
        }
    }

    return FALSE;
}


IOT_REGISTER_TRANSPORT(tcp4, TCP4, strm_t, strm_resolve,
                       strm_open, strm_createfrom, strm_close,
                       NULL, NULL,
                       strm_bind, strm_listen, strm_accept,
                       strm_connect, strm_disconnect,
                       strm_sendraw, NULL,
                       strm_sendjson, NULL);

IOT_REGISTER_TRANSPORT(tcp6, TCP6, strm_t, strm_resolve,
                       strm_open, strm_createfrom, strm_close,
                       NULL, NULL,
                       strm_bind, strm_listen, strm_accept,
                       strm_connect, strm_disconnect,
                       strm_sendraw, NULL,
                       strm_sendjson, NULL);

IOT_REGISTER_TRANSPORT(unxstrm, UNXS, strm_t, strm_resolve,
                       strm_open, strm_createfrom, strm_close,
                       NULL, strm_getopt,
                       strm_bind, strm_listen, strm_accept,
                       strm_connect, strm_disconnect,
                       strm_sendraw, NULL,
                       strm_sendjson, NULL);
