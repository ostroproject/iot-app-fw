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


#include <pulse/mainloop-api.h>
#include <pulse/timeval.h>

#include <iot/common/mm.h>
#include <iot/common/mainloop.h>

#include <iot/common/pulse-glue.h>


typedef struct {
    pa_mainloop_api *pa;
} pulse_glue_t;


typedef struct {
    pa_io_event  *pa_io;
    void        (*cb)(void *glue_data,
                      void *id, int fd, iot_io_event_t events,
                      void *user_data);
    void         *user_data;
    void         *glue_data;
} io_t;


typedef struct {
    pa_time_event  *pa_t;
    void          (*cb)(void *glue_data, void *id, void *user_data);
    void           *user_data;
    void           *glue_data;
} tmr_t;


typedef struct {
    pa_defer_event  *pa_d;
    void           (*cb)(void *glue_data, void *id, void *user_data);
    void            *user_data;
    void            *glue_data;
} dfr_t;


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


static void io_cb(pa_mainloop_api *pa, pa_io_event *e, int fd,
                  pa_io_event_flags_t mask, void *user_data)
{
    io_t           *io     = (io_t *)user_data;
    iot_io_event_t  events = IOT_IO_EVENT_NONE;

    IOT_UNUSED(pa);
    IOT_UNUSED(e);

    if (mask & PA_IO_EVENT_INPUT)  events |= IOT_IO_EVENT_IN;
    if (mask & PA_IO_EVENT_OUTPUT) events |= IOT_IO_EVENT_OUT;
    if (mask & PA_IO_EVENT_HANGUP) events |= IOT_IO_EVENT_HUP;
    if (mask & PA_IO_EVENT_ERROR)  events |= IOT_IO_EVENT_ERR;

    io->cb(io->glue_data, io, fd, events, io->user_data);
}


static void *add_io(void *glue_data, int fd, iot_io_event_t events,
                    void (*cb)(void *glue_data, void *id, int fd,
                               iot_io_event_t events, void *user_data),
                    void *user_data)
{
    pulse_glue_t        *glue = (pulse_glue_t *)glue_data;
    pa_mainloop_api     *pa   = glue->pa;
    pa_io_event_flags_t  mask = PA_IO_EVENT_NULL;
    io_t                *io;

    io = iot_allocz(sizeof(*io));

    if (io != NULL) {
        if (events & IOT_IO_EVENT_IN)  mask |= PA_IO_EVENT_INPUT;
        if (events & IOT_IO_EVENT_OUT) mask |= PA_IO_EVENT_OUTPUT;
        if (events & IOT_IO_EVENT_HUP) mask |= PA_IO_EVENT_HANGUP;
        if (events & IOT_IO_EVENT_ERR) mask |= PA_IO_EVENT_ERROR;

        io->pa_io = pa->io_new(pa, fd, mask, io_cb, io);

        if (io->pa_io != NULL) {
            io->cb        = cb;
            io->user_data = user_data;
            io->glue_data = glue_data;

            return io;
        }
        else
            iot_free(io);
    }

    return NULL;
}


static void del_io(void *glue_data, void *id)
{
    pulse_glue_t    *glue = (pulse_glue_t *)glue_data;
    pa_mainloop_api *pa   = glue->pa;
    io_t            *io   = (io_t *)id;

    pa->io_free(io->pa_io);
    iot_free(io);
}


static void timer_cb(pa_mainloop_api *pa, pa_time_event *e,
                     const struct timeval *tv, void *user_data)
{
    tmr_t *t = (tmr_t *)user_data;

    IOT_UNUSED(pa);
    IOT_UNUSED(e);
    IOT_UNUSED(tv);

    t->cb(t->glue_data, t, t->user_data);
}


static void *add_timer(void *glue_data, unsigned int msecs,
                       void (*cb)(void *glue_data, void *id, void *user_data),
                       void *user_data)
{
    pulse_glue_t    *glue = (pulse_glue_t *)glue_data;
    pa_mainloop_api *pa   = glue->pa;
    struct timeval   tv;
    tmr_t           *t;

    t = iot_allocz(sizeof(*t));

    if (t != NULL) {
        pa_gettimeofday(&tv);

        tv.tv_sec  += msecs / 1000;
        tv.tv_usec += 1000 * (msecs % 1000);

        t->pa_t = pa->time_new(pa, &tv, timer_cb, t);

        if (t->pa_t != NULL) {
            t->cb        = cb;
            t->user_data = user_data;
            t->glue_data = glue_data;

            return t;
        }
        else
            iot_free(t);
    }

    return NULL;
}


static void del_timer(void *glue_data, void *id)
{
    pulse_glue_t    *glue = (pulse_glue_t *)glue_data;
    pa_mainloop_api *pa   = glue->pa;
    tmr_t           *t    = (tmr_t *)id;

    pa->time_free(t->pa_t);
    iot_free(t);
}


static void mod_timer(void *glue_data, void *id, unsigned int msecs)
{
    pulse_glue_t    *glue = (pulse_glue_t *)glue_data;
    pa_mainloop_api *pa   = glue->pa;
    tmr_t           *t    = (tmr_t *)id;
    struct timeval   tv;

    if (t != NULL) {
        pa_gettimeofday(&tv);

        tv.tv_sec  += msecs / 1000;
        tv.tv_usec += 1000 * (msecs % 1000);

        pa->time_restart(t->pa_t, &tv);
    }
}


static void defer_cb(pa_mainloop_api *pa, pa_defer_event *e, void *user_data)
{
    dfr_t *d = (dfr_t *)user_data;

    IOT_UNUSED(pa);
    IOT_UNUSED(e);

    d->cb(d->glue_data, d, d->user_data);
}


static void *add_defer(void *glue_data,
                       void (*cb)(void *glue_data, void *id, void *user_data),
                       void *user_data)
{
    pulse_glue_t    *glue = (pulse_glue_t *)glue_data;
    pa_mainloop_api *pa   = glue->pa;
    dfr_t           *d;

    d = iot_allocz(sizeof(*d));

    if (d != NULL) {
        d->pa_d = pa->defer_new(pa, defer_cb, d);

        if (d->pa_d != NULL) {
            d->cb        = cb;
            d->user_data = user_data;
            d->glue_data = glue_data;

            return d;
        }
        else
            iot_free(d);
    }

    return NULL;
}


static void del_defer(void *glue_data, void *id)
{
    pulse_glue_t    *glue = (pulse_glue_t *)glue_data;
    pa_mainloop_api *pa   = glue->pa;
    dfr_t           *d    = (dfr_t *)id;

    pa->defer_free(d->pa_d);
    iot_free(d);
}


static void mod_defer(void *glue_data, void *id, int enabled)
{
    pulse_glue_t    *glue = (pulse_glue_t *)glue_data;
    pa_mainloop_api *pa   = glue->pa;
    dfr_t           *d    = (dfr_t *)id;

    pa->defer_enable(d->pa_d, !!enabled);
}


static void unregister(void *data)
{
    pulse_glue_t *glue = (pulse_glue_t *)data;

    iot_free(glue);
}


static iot_superloop_ops_t pa_ops = {
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


int iot_mainloop_register_with_pulse(iot_mainloop_t *ml, pa_mainloop_api *pa)
{
    pulse_glue_t *glue;

    glue = iot_allocz(sizeof(*glue));

    if (glue != NULL) {
        glue->pa = pa;

        if (iot_set_superloop(ml, &pa_ops, glue))
            return TRUE;
        else
            iot_free(glue);
    }

    return FALSE;
}


int iot_mainloop_unregister_from_pulse(iot_mainloop_t *ml)
{
    return iot_mainloop_unregister(ml);
}



static iot_mainloop_t *pulse_ml;

iot_mainloop_t *iot_mainloop_pulse_get(pa_mainloop_api *pa)
{
    if (pulse_ml == NULL) {
        pulse_ml = iot_mainloop_create();

        if (pulse_ml != NULL) {
            if (!iot_mainloop_register_with_pulse(pulse_ml, pa)) {
                iot_mainloop_destroy(pulse_ml);
                pulse_ml = NULL;
            }
        }
    }

    return pulse_ml;
}
