/*
 * Copyright (c) 2015, Intel Corporation
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

#include <stdio.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>

#include <iot/common/mm.h>
#include <iot/common/mainloop.h>
#include <iot/common/uv-glue.h>


typedef struct {
    uv_loop_t *uv;
} uv_glue_t;


typedef struct {
    uv_poll_t        uv_poll;
    int              fd;
    void           (*cb)(void *glue_data,
                         void *id, int fd, iot_io_event_t events,
                         void *user_data);
    iot_io_event_t   mask;
    void            *user_data;
    void            *glue_data;
} io_t;


typedef struct {
    uv_timer_t   uv_tmr;
    void       (*cb)(void *glue_data, void *id, void *user_data);
    void        *user_data;
    void        *glue_data;
} tmr_t;


typedef struct {
    uv_timer_t   uv_tmr;
    void       (*cb)(void *glue_data, void *id, void *user_data);
    void        *user_data;
    void        *glue_data;
    int          enabled;
} dfr_t;


#define D(fmt, args...) do {                                     \
        printf("* [%s]: "fmt"\n", __FUNCTION__ , ## args);       \
    } while (0)


static void *add_io(void *glue_data, int fd, iot_io_event_t events,
                    void (*cb)(void *glue_data, void *id, int fd,
                               iot_io_event_t events, void *user_data),
                    void *user_data);
static void  del_io(void *glue_data, void *id);

static void *add_timer(void *glue_data, unsigned int msecs,
                       void (*cb)(void *glue_data, void *id, void *user_data),
                       void *user_data);
static void  del_timer(void *glue_data, void *id);
static void  mod_timer(void *glue_data, void *id, unsigned int msecs);

static void *add_defer(void *glue_data,
                       void (*cb)(void *glue_data, void *id, void *user_data),
                       void *user_data);
static void  del_defer(void *glue_data, void *id);
static void  mod_defer(void *glue_data, void *id, int enabled);


static int check_hup(int fd)
{
  char buf[1];
  int  saved_errno, n;

  saved_errno = errno;
  n = recv(fd, buf, 1, MSG_PEEK);
  errno = saved_errno;

  return (n == 0);
}


static void io_cb(uv_poll_t *handle, int status, int mask)
{
    io_t           *io     = (io_t *)handle->data;
    iot_io_event_t  events = IOT_IO_EVENT_NONE;
    int             fd     = io->fd;

    if (status < 0)
        events |= IOT_IO_EVENT_ERR;
    if (mask & UV_READABLE)
        events |= (check_hup(fd) ? IOT_IO_EVENT_HUP : IOT_IO_EVENT_IN);
    if (mask & UV_WRITABLE)
        events |= IOT_IO_EVENT_OUT;

    io->cb(io->glue_data, io, fd, events, io->user_data);
}


static void *add_io(void *glue_data, int fd, iot_io_event_t events,
                    void (*cb)(void *glue_data, void *id, int fd,
                               iot_io_event_t events, void *user_data),
                    void *user_data)
{
    uv_glue_t *uv   = (uv_glue_t *)glue_data;
    int        mask = 0;
    io_t      *io;

    io = iot_allocz(sizeof(*io));

    if (io == NULL)
        return NULL;

    if (events & IOT_IO_EVENT_IN)
        mask |= UV_READABLE;
    if (events & IOT_IO_EVENT_OUT)
        mask |= UV_WRITABLE;

    uv_poll_init_socket(uv->uv, &io->uv_poll, fd);
    io->uv_poll.data = io;
    uv_poll_start(&io->uv_poll, mask, io_cb);

    io->mask      = events;
    io->cb        = cb;
    io->user_data = user_data;
    io->glue_data = glue_data;

    return io;
}


static void del_io(void *glue_data, void *id)
{
    io_t *io = (io_t *)id;

    IOT_UNUSED(glue_data);

    uv_poll_stop(&io->uv_poll);
    iot_free(io);
}

static void timer_cb(uv_timer_t *handle
#if UV_VERSION_MAJOR < 1
                     , int status
#endif
                     )
{
    tmr_t *t = (tmr_t *)handle->data;

#if UV_VERSION_MAJOR < 1
    IOT_UNUSED(status);
#endif

    t->cb(t->glue_data, t, t->user_data);
}


static void *add_timer(void *glue_data, unsigned int msecs,
                       void (*cb)(void *glue_data, void *id, void *user_data),
                       void *user_data)
{
    uv_glue_t *uv = (uv_glue_t *)glue_data;
    tmr_t     *t;

    t = iot_allocz(sizeof(*t));

    if (t == NULL)
        return NULL;

    uv_timer_init(uv->uv, &t->uv_tmr);
    t->uv_tmr.data = t;
    uv_timer_start(&t->uv_tmr, timer_cb, msecs, msecs);

    t->cb        = cb;
    t->user_data = user_data;
    t->glue_data = glue_data;

    return t;
}


static void del_timer(void *glue_data, void *id)
{
    tmr_t *t = (tmr_t *)id;

    IOT_UNUSED(glue_data);

    uv_timer_stop(&t->uv_tmr);
    iot_free(t);
}


static void mod_timer(void *glue_data, void *id, unsigned int msecs)
{
    uv_glue_t *uv = (uv_glue_t *)glue_data;
    tmr_t     *t = (tmr_t *)id;

    uv_timer_stop(&t->uv_tmr);
    uv_timer_init(uv->uv, &t->uv_tmr);
    t->uv_tmr.data = t;
    uv_timer_start(&t->uv_tmr, timer_cb, msecs, msecs);
}


static void defer_cb(uv_timer_t *handle
#if UV_VERSION_MAJOR < 1
                     , int status
#endif
                     )
{
    dfr_t *d = (dfr_t *)handle->data;

#if UV_VERSION_MAJOR < 1
    IOT_UNUSED(status);
#endif

    d->cb(d->glue_data, d, d->user_data);

    if (d->enabled)
        uv_timer_again(&d->uv_tmr);
}


static void *add_defer(void *glue_data,
                       void (*cb)(void *glue_data, void *id, void *user_data),
                       void *user_data)
{
    uv_glue_t *uv = (uv_glue_t *)glue_data;
    dfr_t     *d;

    d = iot_allocz(sizeof(*d));

    if (d == NULL)
        return NULL;

    uv_timer_init(uv->uv, &d->uv_tmr);
    d->uv_tmr.data = d;
    uv_timer_start(&d->uv_tmr, defer_cb, 0, 0);

    d->cb        = cb;
    d->user_data = user_data;
    d->glue_data = glue_data;
    d->enabled   = TRUE;

    return d;
}


static void del_defer(void *glue_data, void *id)
{
    dfr_t *d = (dfr_t *)id;

    IOT_UNUSED(glue_data);

    uv_timer_stop(&d->uv_tmr);
    iot_free(d);
}


static void mod_defer(void *glue_data, void *id, int enabled)
{
    uv_glue_t *uv = (uv_glue_t *)glue_data;
    dfr_t     *d  = (dfr_t *)id;

    if (enabled && !d->enabled) {
        uv_timer_init(uv->uv, &d->uv_tmr);
        d->uv_tmr.data = d;
        uv_timer_start(&d->uv_tmr, defer_cb, 0, 0);
        d->enabled = TRUE;
    }
    else if (!enabled && d->enabled) {
        uv_timer_stop(&d->uv_tmr);
        d->enabled = FALSE;
    }
}


static void unregister(void *data)
{
    uv_glue_t *glue = (uv_glue_t *)data;

    iot_free(glue);
}


static iot_superloop_ops_t uv_ops = {
    .add_io     = add_io,
    .del_io     = del_io,
    .add_timer  = add_timer,
    .del_timer  = del_timer,
    .mod_timer  = mod_timer,
    .add_defer  = add_defer,
    .del_defer  = del_defer,
    .mod_defer  = mod_defer,
    .unregister = unregister,
};


int iot_mainloop_register_with_uv(iot_mainloop_t *ml, uv_loop_t *uv)
{
    uv_glue_t *glue;

    glue = iot_allocz(sizeof(*glue));

    if (glue == NULL)
        return FALSE;

    glue->uv = uv;

    if (iot_set_superloop(ml, &uv_ops, glue))
        return TRUE;
    else
        iot_free(glue);

    return FALSE;
}


int iot_mainloop_unregister_from_uv(iot_mainloop_t *ml)
{
    return iot_mainloop_unregister(ml);
}


iot_mainloop_t *iot_mainloop_uv_get(uv_loop_t *uv)
{
    iot_mainloop_t *ml;

    if (uv == NULL)
        uv = uv_default_loop();

    if (uv == NULL)
        return NULL;

    ml = iot_mainloop_create();

    if (ml == NULL)
        return NULL;

    if (iot_mainloop_register_with_uv(ml, uv))
        return ml;
    else
        iot_mainloop_destroy(ml);

    return NULL;
}
