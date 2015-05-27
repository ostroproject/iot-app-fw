/*
 * Copyright (c) 2012-2014, Intel Corporation
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
#include <errno.h>
#include <time.h>
#include <signal.h>
#include <limits.h>
#include <stdarg.h>
#include <sys/epoll.h>
#include <sys/signalfd.h>
#include <sys/socket.h>

#include <iot/common/macros.h>
#include <iot/common/mm.h>
#include <iot/common/log.h>
#include <iot/common/list.h>
#include <iot/common/hashtbl.h>
#include <iot/common/json.h>
#include <iot/common/mainloop.h>

#define USECS_PER_SEC  (1000 * 1000)
#define USECS_PER_MSEC (1000)
#define NSECS_PER_USEC (1000)

/*
 * I/O watches
 */

struct iot_io_watch_s {
    iot_list_hook_t    hook;                     /* to list of watches */
    iot_list_hook_t    deleted;                  /* to list of pending delete */
    int              (*free)(void *ptr);         /* cb to free memory */
    iot_mainloop_t    *ml;                       /* mainloop */
    int                fd;                       /* file descriptor to watch */
    iot_io_event_t     events;                   /* events of interest */
    iot_io_watch_cb_t  cb;                       /* user callback */
    void              *user_data;                /* opaque user data */
    struct pollfd     *pollfd;                   /* associated pollfd */
    iot_list_hook_t    slave;                    /* watches with the same fd */
    int                wrhup;                    /* EPOLLHUPs delivered */
};

#define is_master(w) !iot_list_empty(&(w)->hook)
#define is_slave(w)  !iot_list_empty(&(w)->slave)


/*
 * timers
 */

struct iot_timer_s {
    iot_list_hook_t  hook;                       /* to list of timers */
    iot_list_hook_t  deleted;                    /* to list of pending delete */
    int            (*free)(void *ptr);           /* cb to free memory */
    iot_mainloop_t  *ml;                         /* mainloop */
    unsigned int     msecs;                      /* timer interval */
    uint64_t         expire;                     /* next expiration time */
    iot_timer_cb_t   cb;                         /* user callback */
    void            *user_data;                  /* opaque user data */
};


/*
 * deferred callbacks
 */

struct iot_deferred_s {
    iot_list_hook_t    hook;                     /* to list of cbs */
    iot_list_hook_t    deleted;                  /* to list of pending delete */
    int              (*free)(void *ptr);         /* cb to free memory */
    iot_mainloop_t    *ml;                       /* mainloop */
    iot_deferred_cb_t  cb;                       /* user callback */
    void              *user_data;                /* opaque user data */
    int                inactive : 1;
};


/*
 * signal handlers
 */

struct iot_sighandler_s {
    iot_list_hook_t      hook;                   /* to list of handlers */
    iot_list_hook_t      deleted;                /* to list of pending delete */
    int                (*free)(void *ptr);       /* cb to free memory */
    iot_mainloop_t      *ml;                     /* mainloop */
    int                  signum;                 /* signal number */
    iot_sighandler_cb_t  cb;                     /* user callback */
    void                *user_data;              /* opaque user data */
};


/*
 * wakeup notifications
 */

struct iot_wakeup_s {
    iot_list_hook_t      hook;                   /* to list of wakeup cbs */
    iot_list_hook_t      deleted;                /* to list of pending delete */
    int                (*free)(void *ptr);       /* cb to free memory */
    iot_mainloop_t      *ml;                     /* mainloop */
    iot_wakeup_event_t   events;                 /* wakeup event mask */
    uint64_t             lpf;                    /* wakeup at most this often */
    uint64_t             next;                   /* next wakeup time */
    iot_timer_t         *timer;                  /* forced interval timer */
    iot_wakeup_cb_t      cb;                     /* user callback */
    void                *user_data;              /* opaque user data */
};

#define mark_deleted(o) do {                                    \
        (o)->cb = NULL;                                         \
        iot_list_append(&(o)->ml->deleted, &(o)->deleted);      \
    } while (0)

#define is_deleted(o) ((o)->cb == NULL)


/*
 * any of the above data structures linked to the list of deleted items
 *
 * When deleted, the above data structures are first unlinked from their
 * native list and linked to the special list of deleted entries. At an
 * appropriate point upon every iteration of the main loop this list is
 * checked and all entries are freed. This structure is used to get a
 * pointer to the real structure that we need free. For this to work link
 * hooks in all of the above structures need to be kept at the same offset
 * as it is in deleted_t.
 */

typedef struct {
    iot_list_hook_t  hook;                       /* unfreed deleted items */
    iot_list_hook_t  deleted;                    /* to list of pending delete */
    int            (*free)(void *ptr);           /* cb to free memory */
} deleted_t;


/*
 * file descriptor table
 *
 * We do not want to associate direct pointers to related data structures
 * with epoll. We might get delivered pending events for deleted fds (at
 * least for unix domain sockets this seems to be the case) and with direct
 * pointers we'd get delivered a dangling pointer together with the event.
 * Instead we keep these structures in an fd table and use the fd to look
 * up the associated data structure for events. We ignore events for which
 * no data structure is found. In the fd table we keep a fixed size direct
 * table for a small amount of fds (we expect to be using at most in the
 * vast majority of cases) and we hash in the rest.
 */

#define FDTBL_SIZE 64

typedef struct {
    void       *t[FDTBL_SIZE];
    iot_htbl_t *h;
} fdtbl_t;


/*
 * event busses
 */

struct iot_event_bus_s {
    char            *name;                       /* bus name */
    iot_list_hook_t  hook;                       /* to list of busses */
    iot_mainloop_t  *ml;                         /* associated mainloop */
    iot_list_hook_t  watches;                    /* event watches on this bus */
    int              busy;                       /* whether pumping events */
    int              dead;
};


/*
 * event watches
 */

struct iot_event_watch_s {
    iot_list_hook_t       hook;                  /* to list of event watches */
    iot_event_bus_t      *bus;                   /* associated event bus */
    iot_event_mask_t      mask;                  /* mask of watched events */
    iot_event_watch_cb_t  cb;                    /* notification callback */
    void                 *user_data;             /* opaque user data */
    int                   dead : 1;              /* marked for deletion */
};


/*
 * pending events
 */

typedef struct {
    iot_list_hook_t  hook;                       /* to event queue */
    iot_event_bus_t *bus;                        /* bus for this event */
    uint32_t         id;                         /* event id */
    int              format;                     /* attached data format */
    void            *data;                       /* attached data */
} pending_event_t;


/*
 * main loop
 */

struct iot_mainloop_s {
    int                  epollfd;                /* our epoll descriptor */
    struct epoll_event  *events;                 /* epoll event buffer */
    int                  nevent;                 /* epoll event buffer size */
    fdtbl_t             *fdtbl;                  /* file descriptor table */

    iot_list_hook_t      iowatches;              /* list of I/O watches */
    int                  niowatch;               /* number of I/O watches */
    iot_io_event_t       iomode;                 /* default event trigger mode */

    iot_list_hook_t      timers;                 /* list of timers */
    iot_timer_t         *next_timer;             /* next expiring timer */

    iot_list_hook_t      deferred;               /* list of deferred cbs */
    iot_list_hook_t      inactive_deferred;      /* inactive defferred cbs */

    iot_list_hook_t      wakeups;                /* list of wakeup cbs */

    int                  poll_timeout;           /* next poll timeout */
    int                  poll_result;            /* return value from poll */

    int                  sigfd;                  /* signal polling fd */
    sigset_t             sigmask;                /* signal mask */
    iot_io_watch_t      *sigwatch;               /* sigfd I/O watch */
    iot_list_hook_t      sighandlers;            /* signal handlers */

    iot_list_hook_t      deleted;                /* unfreed deleted items */
    int                  quit;                   /* TRUE if _quit called */
    int                  exit_code;              /* returned from _run */

    iot_superloop_ops_t *super_ops;              /* superloop options */
    void                *super_data;             /* superloop glue data */
    void                *iow;                    /* superloop epollfd watch */
    void                *timer;                  /* superloop timer */
    void                *work;                   /* superloop deferred work */

    iot_list_hook_t      busses;                 /* known event busses */
    iot_list_hook_t      eventq;                 /* pending events */
    iot_deferred_t      *eventd;                 /* deferred event pump cb */
};


static iot_event_def_t *events;                  /* registered events */
static int              nevent;                  /* number of events */
static IOT_LIST_HOOK   (ewatches);               /* global, synchronous 'bus' */


static void adjust_superloop_timer(iot_mainloop_t *ml);
static size_t poll_events(void *id, iot_mainloop_t *ml, void **bufp);
static void pump_events(iot_deferred_t *d, void *user_data);

/*
 * fd table manipulation
 */

static int fd_cmp(const void *key1, const void *key2)
{
    return key2 - key1;
}


static uint32_t fd_hash(const void *key)
{
    uint32_t h;

    h = (uint32_t)(ptrdiff_t)key;

    return h;
}



static fdtbl_t *fdtbl_create(void)
{
    fdtbl_t           *ft;
    iot_htbl_config_t  hcfg;

    if ((ft = iot_allocz(sizeof(*ft))) != NULL) {
        iot_clear(&hcfg);

        hcfg.comp    = fd_cmp;
        hcfg.hash    = fd_hash;
        hcfg.free    = NULL;
        hcfg.nbucket = 16;

        ft->h = iot_htbl_create(&hcfg);

        if (ft->h != NULL)
            return ft;
        else
            iot_free(ft);
    }

    return NULL;
}


static void fdtbl_destroy(fdtbl_t *ft)
{
    if (ft != NULL) {
        iot_htbl_destroy(ft->h, FALSE);
        iot_free(ft);
    }
}


static void *fdtbl_lookup(fdtbl_t *ft, int fd)
{
    if (fd >= 0 && ft != NULL) {
        if (fd < FDTBL_SIZE)
            return ft->t[fd];
        else
            return iot_htbl_lookup(ft->h, (void *)(ptrdiff_t)fd);
    }

    return NULL;
}


static int fdtbl_insert(fdtbl_t *ft, int fd, void *ptr)
{
    if (fd >= 0 && ft != NULL) {
        if (fd < FDTBL_SIZE) {
            if (ft->t[fd] == NULL) {
                ft->t[fd] = ptr;
                return 0;
            }
            else
                errno = EEXIST;
        }
        else {
            if (iot_htbl_insert(ft->h, (void *)(ptrdiff_t)fd, ptr))
                return 0;
            else
                errno = EEXIST;
        }
    }
    else
        errno = EINVAL;

    return -1;
}


static void fdtbl_remove(fdtbl_t *ft, int fd)
{
    if (fd >= 0 && ft != NULL) {
        if (fd < FDTBL_SIZE)
            ft->t[fd] = NULL;
        else
            iot_htbl_remove(ft->h, (void *)(ptrdiff_t)fd, FALSE);
    }
}


/*
 * I/O watches
 */

static uint32_t epoll_event_mask(iot_io_watch_t *master, iot_io_watch_t *ignore)
{
    iot_io_watch_t  *w;
    iot_list_hook_t *p, *n;
    uint32_t         mask;

    mask = (master != ignore ?
            master->events : master->events & IOT_IO_TRIGGER_EDGE);

    iot_list_foreach(&master->slave, p, n) {
        w = iot_list_entry(p, typeof(*w), slave);

        if (w != ignore)
            mask |= w->events;
    }

    iot_debug("epoll event mask for I/O watch %p: %d", master, mask);

    return mask;
}


static int epoll_add_slave(iot_io_watch_t *master, iot_io_watch_t *slave)
{
    iot_mainloop_t     *ml = master->ml;
    struct epoll_event  evt;

    evt.events   = epoll_event_mask(master, NULL) | slave->events;
    evt.data.u64 = 0;
    evt.data.fd  = master->fd;

    if (epoll_ctl(ml->epollfd, EPOLL_CTL_MOD, master->fd, &evt) == 0) {
        iot_list_append(&master->slave, &slave->slave);

        return 0;
    }

    return -1;
}


static int epoll_add(iot_io_watch_t *w)
{
    iot_mainloop_t     *ml = w->ml;
    iot_io_watch_t     *master;
    struct epoll_event  evt;

    if (fdtbl_insert(ml->fdtbl, w->fd, w) == 0) {
        evt.events   = w->events;
        evt.data.u64 = 0;                /* init full union for valgrind... */
        evt.data.fd  = w->fd;

        if (epoll_ctl(ml->epollfd, EPOLL_CTL_ADD, w->fd, &evt) == 0) {
            iot_list_append(&ml->iowatches, &w->hook);
            ml->niowatch++;

            return 0;
        }
        else
            fdtbl_remove(ml->fdtbl, w->fd);
    }
    else {
        if (errno == EEXIST) {
            master = fdtbl_lookup(ml->fdtbl, w->fd);

            if (master != NULL)
                return epoll_add_slave(master, w);
        }
    }

    return -1;
}


static int epoll_del(iot_io_watch_t *w)
{
    iot_mainloop_t     *ml = w->ml;
    iot_io_watch_t     *master;
    struct epoll_event  evt;
    int                 status;

    if (is_master(w))
        master = w;
    else
        master = fdtbl_lookup(ml->fdtbl, w->fd);

    if (master != NULL) {
        evt.events   = epoll_event_mask(master, w);
        evt.data.u64 = 0;                /* init full union for valgrind... */
        evt.data.fd  = w->fd;

        if ((evt.events & IOT_IO_EVENT_ALL) == 0) {
            fdtbl_remove(ml->fdtbl, w->fd);
            status = epoll_ctl(ml->epollfd, EPOLL_CTL_DEL, w->fd, &evt);

            if (status == 0 || (errno == EBADF || errno == ENOENT))
                ml->niowatch--;
        }
        else
            status = epoll_ctl(ml->epollfd, EPOLL_CTL_MOD, w->fd, &evt);

        if (status == 0 || (errno == EBADF || errno == ENOENT))
            return 0;
        else
            iot_log_error("Failed to update epoll for deleted I/O watch %p "
                          "(fd %d, %d: %s).", w, w->fd, errno, strerror(errno));
    }
    else {
        iot_log_error("Failed to find master for deleted I/O watch %p "
                      "(fd %d).", w, w->fd);
        errno = EINVAL;
    }

    return -1;
}


static int free_io_watch(void *ptr)
{
    iot_io_watch_t *w  = (iot_io_watch_t *)ptr;
    iot_mainloop_t *ml = w->ml;
    iot_io_watch_t *master;

    master = fdtbl_lookup(ml->fdtbl, w->fd);

    if (master == w) {
        fdtbl_remove(ml->fdtbl, w->fd);

        if (!iot_list_empty(&w->slave)) {
            /* relink first slave as new master to mainloop */
            master = iot_list_entry(w->slave.next, typeof(*master), slave);
            iot_list_append(&ml->iowatches, &master->hook);

            fdtbl_insert(ml->fdtbl, master->fd, master);
        }
    }

    iot_list_delete(&w->slave);
    iot_free(w);

    return TRUE;
}


iot_io_watch_t *iot_add_io_watch(iot_mainloop_t *ml, int fd,
                                 iot_io_event_t events,
                                 iot_io_watch_cb_t cb, void *user_data)
{
    iot_io_watch_t *w;

    if (fd < 0 || cb == NULL)
        return NULL;

    if ((w = iot_allocz(sizeof(*w))) != NULL) {
        iot_list_init(&w->hook);
        iot_list_init(&w->deleted);
        iot_list_init(&w->slave);
        w->ml        = ml;
        w->fd        = fd;
        w->events    = events & IOT_IO_EVENT_ALL;

        switch (events & IOT_IO_TRIGGER_MASK) {
        case 0:
            if (ml->iomode == IOT_IO_TRIGGER_EDGE)
                w->events |= IOT_IO_TRIGGER_EDGE;
            break;
        case IOT_IO_TRIGGER_EDGE:
            w->events |= IOT_IO_TRIGGER_EDGE;
            break;
        case IOT_IO_TRIGGER_LEVEL:
            break;
        default:
            iot_log_warning("Invalid I/O event trigger mode 0x%x.",
                            events & IOT_IO_TRIGGER_MASK);
            break;
        }

        w->cb        = cb;
        w->user_data = user_data;
        w->free      = free_io_watch;

        if (epoll_add(w) != 0) {
            iot_free(w);
            w = NULL;
        }
        else
            iot_debug("added I/O watch %p (fd %d, events 0x%x)", w, w->fd, w->events);
    }

    return w;
}


void iot_del_io_watch(iot_io_watch_t *w)
{
    /*
     * Notes: It is not safe to free the watch here as there might be
     *        a delivered but unprocessed epoll event with a pointer
     *        to the watch. We just mark it deleted and take care of
     *        the actual deletion in the dispatching loop.
     */

    if (w != NULL && !is_deleted(w)) {
        iot_debug("marking I/O watch %p (fd %d) deleted", w, w->fd);

        mark_deleted(w);
        w->events = 0;

        epoll_del(w);
    }
}


iot_mainloop_t *iot_get_io_watch_mainloop(iot_io_watch_t *w)
{
    return w ? w->ml : NULL;
}


int iot_set_io_event_mode(iot_mainloop_t *ml, iot_io_event_t mode)
{
    if (mode == IOT_IO_TRIGGER_LEVEL || mode == IOT_IO_TRIGGER_EDGE) {
        ml->iomode = mode;
        return TRUE;
    }
    else {
        iot_log_error("Invalid I/O event mode 0x%x.", mode);
        return FALSE;
    }
}


iot_io_event_t iot_get_io_event_mode(iot_mainloop_t *ml)
{
    return ml->iomode ? ml->iomode : IOT_IO_TRIGGER_LEVEL;
}


/*
 * timers
 */

static uint64_t time_now(void)
{
    struct timespec ts;
    uint64_t        now;

    clock_gettime(CLOCK_MONOTONIC, &ts);
    now  = ts.tv_sec  * USECS_PER_SEC;
    now += ts.tv_nsec / NSECS_PER_USEC;

    return now;
}


static inline int usecs_to_msecs(uint64_t usecs)
{
    int msecs;

    msecs = (usecs + USECS_PER_MSEC - 1) / USECS_PER_MSEC;

    return msecs;
}


static void insert_timer(iot_timer_t *t)
{
    iot_mainloop_t  *ml = t->ml;
    iot_list_hook_t *p, *n;
    iot_timer_t     *t1, *next;
    int              inserted;

    /*
     * Notes:
     *     If there is ever a need to run a large number of
     *     simultaneous timers, we need to change this to a
     *     self-balancing data structure, eg. an red-black tree.
     */

    inserted = FALSE;
    next     = NULL;
    iot_list_foreach(&ml->timers, p, n) {
        t1 = iot_list_entry(p, iot_timer_t, hook);

        if (!is_deleted(t1)) {
            if (t->expire <= t1->expire) {
                iot_list_prepend(p->prev, &t->hook);
                inserted = TRUE;
                break;
            }
            if (next == NULL)
                next = t1;
        }
    }

    if (!inserted)
        iot_list_append(&ml->timers, &t->hook);

    if (next)
        ml->next_timer = next;
    else {
        ml->next_timer = t;
        adjust_superloop_timer(ml);
    }
}


static inline void rearm_timer(iot_timer_t *t)
{
    iot_list_delete(&t->hook);
    t->expire = time_now() + t->msecs * USECS_PER_MSEC;
    insert_timer(t);
}


static iot_timer_t *find_next_timer(iot_mainloop_t *ml)
{
    iot_list_hook_t *p, *n;
    iot_timer_t     *t = NULL;

    iot_list_foreach(&ml->timers, p, n) {
        t = iot_list_entry(p, typeof(*t), hook);

        if (!is_deleted(t))
            break;
        else
            t = NULL;
    }

    ml->next_timer = t;
    return t;
}


static int free_timer(void *ptr)
{
    iot_timer_t *t = (iot_timer_t *)ptr;

    iot_free(t);

    return TRUE;
}



iot_timer_t *iot_add_timer(iot_mainloop_t *ml, unsigned int msecs,
                           iot_timer_cb_t cb, void *user_data)
{
    iot_timer_t *t;

    if (cb == NULL)
        return NULL;

    if ((t = iot_allocz(sizeof(*t))) != NULL) {
        iot_list_init(&t->hook);
        iot_list_init(&t->deleted);
        t->ml        = ml;
        t->expire    = time_now() + msecs * USECS_PER_MSEC;
        t->msecs     = msecs;
        t->cb        = cb;
        t->user_data = user_data;
        t->free      = free_timer;

        insert_timer(t);
    }

    return t;
}


void iot_mod_timer(iot_timer_t *t, unsigned int msecs)
{
    if (t != NULL && !is_deleted(t)) {
        if (msecs != IOT_TIMER_RESTART)
            t->msecs = msecs;

        rearm_timer(t);
    }
}


void iot_del_timer(iot_timer_t *t)
{
    /*
     * Notes: It is not safe to simply free this entry here as we might
     *        be dispatching with this entry being the next to process.
     *        We check for this and if it is not the case we relink this
     *        to the list of deleted items which will be then processed
     *        at end of the mainloop iteration. Otherwise we only mark the
     *        this entry for deletion and the rest will be taken care of in
     *        dispatch_timers().
     */

    if (t != NULL && !is_deleted(t)) {
        iot_debug("marking timer %p deleted", t);

        mark_deleted(t);

        if (t->ml->next_timer == t) {
            find_next_timer(t->ml);
            adjust_superloop_timer(t->ml);
        }
    }
}


iot_mainloop_t *iot_get_timer_mainloop(iot_timer_t *t)
{
    return t ? t->ml : NULL;
}


/*
 * deferred/idle callbacks
 */

iot_deferred_t *iot_add_deferred(iot_mainloop_t *ml, iot_deferred_cb_t cb,
                                 void *user_data)
{
    iot_deferred_t *d;

    if (cb == NULL)
        return NULL;

    if ((d = iot_allocz(sizeof(*d))) != NULL) {
        iot_list_init(&d->hook);
        iot_list_init(&d->deleted);
        d->ml        = ml;
        d->cb        = cb;
        d->user_data = user_data;

        iot_list_append(&ml->deferred, &d->hook);
        adjust_superloop_timer(ml);
    }

    return d;
}


void iot_del_deferred(iot_deferred_t *d)
{
    /*
     * Notes: It is not safe to simply free this entry here as we might
     *        be dispatching with this entry being the next to process.
     *        We just mark this here deleted and take care of the rest
     *        in the dispatching loop.
     */

    if (d != NULL && !is_deleted(d)) {
        iot_debug("marking deferred %p deleted", d);
        mark_deleted(d);
    }
}


void iot_disable_deferred(iot_deferred_t *d)
{
    if (d != NULL)
        d->inactive = TRUE;
}


static inline void disable_deferred(iot_deferred_t *d)
{
    if (IOT_LIKELY(d->inactive)) {
        iot_list_delete(&d->hook);
        iot_list_append(&d->ml->inactive_deferred, &d->hook);
    }

}


void iot_enable_deferred(iot_deferred_t *d)
{
    if (d != NULL) {
        if (!is_deleted(d)) {
            d->inactive = FALSE;
            iot_list_delete(&d->hook);
            iot_list_append(&d->ml->deferred, &d->hook);
        }
    }
}


iot_mainloop_t *iot_get_deferred_mainloop(iot_deferred_t *d)
{
    return d ? d->ml : NULL;
}


/*
 * signal notifications
 */

static void dispatch_signals(iot_io_watch_t *w, int fd,
                             iot_io_event_t events, void *user_data)
{
    iot_mainloop_t          *ml = iot_get_io_watch_mainloop(w);
    struct signalfd_siginfo  sig;
    iot_list_hook_t         *p, *n;
    iot_sighandler_t        *h;
    int                      signum;

    IOT_UNUSED(events);
    IOT_UNUSED(user_data);

    while (read(fd, &sig, sizeof(sig)) > 0) {
        signum = sig.ssi_signo;

        iot_list_foreach(&ml->sighandlers, p, n) {
            h = iot_list_entry(p, typeof(*h), hook);

            if (!is_deleted(h)) {
                if (h->signum == signum)
                    h->cb(h, signum, h->user_data);
            }
        }
    }
}


static int setup_sighandlers(iot_mainloop_t *ml)
{
    if (ml->sigfd == -1) {
        sigemptyset(&ml->sigmask);

        ml->sigfd = signalfd(-1, &ml->sigmask, SFD_NONBLOCK | SFD_CLOEXEC);

        if (ml->sigfd == -1)
            return FALSE;

        ml->sigwatch = iot_add_io_watch(ml, ml->sigfd, IOT_IO_EVENT_IN,
                                        dispatch_signals, NULL);

        if (ml->sigwatch == NULL) {
            close(ml->sigfd);
            return FALSE;
        }
    }

    return TRUE;
}


iot_sighandler_t *iot_add_sighandler(iot_mainloop_t *ml, int signum,
                                     iot_sighandler_cb_t cb, void *user_data)
{
    iot_sighandler_t *s;

    if (cb == NULL || ml->sigfd == -1)
        return NULL;

    if ((s = iot_allocz(sizeof(*s))) != NULL) {
        iot_list_init(&s->hook);
        iot_list_init(&s->deleted);
        s->ml        = ml;
        s->signum    = signum;
        s->cb        = cb;
        s->user_data = user_data;

        iot_list_append(&ml->sighandlers, &s->hook);
        sigaddset(&ml->sigmask, s->signum);
        signalfd(ml->sigfd, &ml->sigmask, SFD_NONBLOCK|SFD_CLOEXEC);
        sigprocmask(SIG_BLOCK, &ml->sigmask, NULL);
    }

    return s;
}


static void recalc_sigmask(iot_mainloop_t *ml)
{
    iot_list_hook_t  *p, *n;
    iot_sighandler_t *h;

    sigprocmask(SIG_UNBLOCK, &ml->sigmask, NULL);
    sigemptyset(&ml->sigmask);

    iot_list_foreach(&ml->sighandlers, p, n) {
        h = iot_list_entry(p, typeof(*h), hook);
        if (!is_deleted(h))
            sigaddset(&ml->sigmask, h->signum);
    }

    sigprocmask(SIG_BLOCK, &ml->sigmask, NULL);
}


void iot_del_sighandler(iot_sighandler_t *h)
{
    if (h != NULL && !is_deleted(h)) {
        iot_debug("marking sighandler %p deleted", h);

        mark_deleted(h);
        recalc_sigmask(h->ml);
    }
}


iot_mainloop_t *iot_get_sighandler_mainloop(iot_sighandler_t *h)
{
    return h ? h->ml : NULL;
}


/*
 * wakeup notifications
 */

static void wakeup_cb(iot_wakeup_t *w, iot_wakeup_event_t event, uint64_t now)
{
    if (w->next > now) {
        iot_debug("skipping wakeup %p because of low-pass filter", w);
        return;
    }

    w->cb(w, event, w->user_data);

    if (w->lpf != IOT_WAKEUP_NOLIMIT)
        w->next = now + w->lpf;

    if (w->timer != NULL)
        iot_mod_timer(w->timer, IOT_TIMER_RESTART);
}


static void forced_wakeup_cb(iot_timer_t *t, void *user_data)
{
    iot_wakeup_t *w = (iot_wakeup_t *)user_data;

    IOT_UNUSED(t);

    if (is_deleted(w))
        return;

    iot_debug("dispatching forced wakeup cb %p", w);

    wakeup_cb(w, IOT_WAKEUP_EVENT_LIMIT, time_now());
}


iot_wakeup_t *iot_add_wakeup(iot_mainloop_t *ml, iot_wakeup_event_t events,
                             unsigned int lpf_msecs, unsigned int force_msecs,
                             iot_wakeup_cb_t cb, void *user_data)
{
    iot_wakeup_t *w;

    if (cb == NULL)
        return NULL;

    if (lpf_msecs > force_msecs && force_msecs != IOT_WAKEUP_NOLIMIT)
        return NULL;

    if ((w = iot_allocz(sizeof(*w))) != NULL) {
        iot_list_init(&w->hook);
        iot_list_init(&w->deleted);
        w->ml        = ml;
        w->events    = events;
        w->cb        = cb;
        w->user_data = user_data;

        w->lpf = lpf_msecs * USECS_PER_MSEC;

        if (lpf_msecs != IOT_WAKEUP_NOLIMIT)
            w->next = time_now() + w->lpf;

        if (force_msecs != IOT_WAKEUP_NOLIMIT) {
            w->timer = iot_add_timer(ml, force_msecs, forced_wakeup_cb, w);

            if (w->timer == NULL) {
                iot_free(w);
                return NULL;
            }
        }

        iot_list_append(&ml->wakeups, &w->hook);
    }

    return w;
}


void iot_del_wakeup(iot_wakeup_t *w)
{
    /*
     * Notes: It is not safe to simply free this entry here as we might
     *        be dispatching with this entry being the next to process.
     *        We just mark this here deleted and take care of the rest
     *        in the dispatching loop.
     */

    if (w != NULL && !is_deleted(w)) {
        iot_debug("marking wakeup %p deleted", w);
        mark_deleted(w);
    }
}


iot_mainloop_t *iot_get_wakeup_mainloop(iot_wakeup_t *w)
{
    return w ? w->ml : NULL;
}


/*
 * external mainloop that pumps us
 */


static void super_io_cb(void *super_data, void *id, int fd,
                        iot_io_event_t events, void *user_data)
{
    iot_mainloop_t      *ml  = (iot_mainloop_t *)user_data;
    iot_superloop_ops_t *ops = ml->super_ops;

    IOT_UNUSED(super_data);
    IOT_UNUSED(id);
    IOT_UNUSED(fd);
    IOT_UNUSED(events);

    ops->mod_defer(ml->super_data, ml->work, TRUE);
}


static void super_timer_cb(void *super_data, void *id, void *user_data)
{
    iot_mainloop_t      *ml  = (iot_mainloop_t *)user_data;
    iot_superloop_ops_t *ops = ml->super_ops;

    IOT_UNUSED(super_data);
    IOT_UNUSED(id);

    ops->mod_defer(ml->super_data, ml->work, TRUE);
}


static void super_work_cb(void *super_data, void *id, void *user_data)
{
    iot_mainloop_t      *ml  = (iot_mainloop_t *)user_data;
    iot_superloop_ops_t *ops = ml->super_ops;
    unsigned int         timeout;

    IOT_UNUSED(super_data);
    IOT_UNUSED(id);

    iot_mainloop_poll(ml, FALSE);
    iot_mainloop_dispatch(ml);

    if (!ml->quit) {
        iot_mainloop_prepare(ml);

        /*
         * Notes:
         *
         *     Some mainloop abstractions (eg. the one in PulseAudio)
         *     have deferred callbacks that starve all other event
         *     processing until no more deferred callbacks are pending.
         *     For this reason, we cannot map our deferred callbacks
         *     directly to superloop deferred callbacks (in some cases
         *     this could starve the superloop indefinitely). Hence, if
         *     we have enabled deferred callbacks, we arm our timer with
         *     0 timeout to let the superloop do one round of its event
         *     processing.
         */

        timeout = iot_list_empty(&ml->deferred) ? ml->poll_timeout : 0;
        ops->mod_timer(ml->super_data, ml->timer, timeout);
        ops->mod_defer(ml->super_data, ml->work, FALSE);
    }
    else {
        ops->del_io(ml->super_data, ml->iow);
        ops->del_timer(ml->super_data, ml->timer);
        ops->del_defer(ml->super_data, ml->work);

        ml->iow   = NULL;
        ml->timer = NULL;
        ml->work  = NULL;
    }
}


static void adjust_superloop_timer(iot_mainloop_t *ml)
{
    iot_superloop_ops_t *ops = ml->super_ops;
    unsigned int         timeout;

    if (ops == NULL)
        return;

    iot_mainloop_prepare(ml);
    timeout = iot_list_empty(&ml->deferred) ? ml->poll_timeout : 0;
    ops->mod_timer(ml->super_data, ml->timer, timeout);
}


int iot_set_superloop(iot_mainloop_t *ml, iot_superloop_ops_t *ops,
                      void *loop_data)
{
    iot_io_event_t events;
    int            timeout;

    if (ml->super_ops == NULL) {
        if (ops->poll_io != NULL)
            ops->poll_events = poll_events;

        ml->super_ops  = ops;
        ml->super_data = loop_data;

        iot_mainloop_prepare(ml);

        events    = IOT_IO_EVENT_IN | IOT_IO_EVENT_OUT | IOT_IO_EVENT_HUP;
        ml->iow   = ops->add_io(ml->super_data, ml->epollfd, events,
                                super_io_cb, ml);
        ml->work  = ops->add_defer(ml->super_data, super_work_cb, ml);

        /*
         * Notes:
         *
         *     Some mainloop abstractions (eg. the one in PulseAudio)
         *     have deferred callbacks that starve all other event
         *     processing until no more deferred callbacks are pending.
         *     For this reason, we cannot map our deferred callbacks
         *     directly to superloop deferred callbacks (in some cases
         *     this could starve the superloop indefinitely). Hence, if
         *     we have enabled deferred callbacks, we arm our timer with
         *     0 timeout to let the superloop do one round of its event
         *     processing.
         */

        timeout   = iot_list_empty(&ml->deferred) ? ml->poll_timeout : 0;
        ml->timer = ops->add_timer(ml->super_data, timeout, super_timer_cb, ml);

        if (ml->iow != NULL && ml->timer != NULL && ml->work != NULL)
            return TRUE;
        else
            iot_clear_superloop(ml);
    }

    return FALSE;
}


int iot_clear_superloop(iot_mainloop_t *ml)
{
    iot_superloop_ops_t *ops  = ml->super_ops;
    void                *data = ml->super_data;

    if (ops != NULL) {
        if (ml->iow != NULL) {
            ops->del_io(data, ml->iow);
            ml->iow = NULL;
        }

        if (ml->work != NULL) {
            ops->del_defer(data, ml->work);
            ml->work = NULL;
        }

        if (ml->timer != NULL) {
            ops->del_timer(data, ml->timer);
            ml->timer = NULL;
        }

        ml->super_ops  = NULL;
        ml->super_data = NULL;

        ops->unregister(data);

        return TRUE;
    }
    else
        return FALSE;
}


int iot_mainloop_unregister(iot_mainloop_t *ml)
{
    return iot_clear_superloop(ml);
}


/*
 * mainloop
 */

static void purge_io_watches(iot_mainloop_t *ml)
{
    iot_list_hook_t *p, *n, *sp, *sn;
    iot_io_watch_t  *w, *s;

    iot_list_foreach(&ml->iowatches, p, n) {
        w = iot_list_entry(p, typeof(*w), hook);
        iot_list_delete(&w->hook);
        iot_list_delete(&w->deleted);

        iot_list_foreach(&w->slave, sp, sn) {
            s = iot_list_entry(sp, typeof(*s), slave);
            iot_list_delete(&s->slave);
            iot_free(s);
        }

        iot_free(w);
    }
}


static void purge_timers(iot_mainloop_t *ml)
{
    iot_list_hook_t *p, *n;
    iot_timer_t     *t;

    iot_list_foreach(&ml->timers, p, n) {
        t = iot_list_entry(p, typeof(*t), hook);
        iot_list_delete(&t->hook);
        iot_list_delete(&t->deleted);
        iot_free(t);
    }
}


static void purge_deferred(iot_mainloop_t *ml)
{
    iot_list_hook_t *p, *n;
    iot_deferred_t  *d;

    iot_list_foreach(&ml->deferred, p, n) {
        d = iot_list_entry(p, typeof(*d), hook);
        iot_list_delete(&d->hook);
        iot_list_delete(&d->deleted);
        iot_free(d);
    }

    iot_list_foreach(&ml->inactive_deferred, p, n) {
        d = iot_list_entry(p, typeof(*d), hook);
        iot_list_delete(&d->hook);
        iot_list_delete(&d->deleted);
        iot_free(d);
    }
}


static void purge_sighandlers(iot_mainloop_t *ml)
{
    iot_list_hook_t  *p, *n;
    iot_sighandler_t *s;

    iot_list_foreach(&ml->sighandlers, p, n) {
        s = iot_list_entry(p, typeof(*s), hook);
        iot_list_delete(&s->hook);
        iot_list_delete(&s->deleted);
        iot_free(s);
    }
}


static void purge_wakeups(iot_mainloop_t *ml)
{
    iot_list_hook_t *p, *n;
    iot_wakeup_t    *w;

    iot_list_foreach(&ml->wakeups, p, n) {
        w = iot_list_entry(p, typeof(*w), hook);
        iot_list_delete(&w->hook);
        iot_list_delete(&w->deleted);
        iot_free(w);
    }
}


static void purge_deleted(iot_mainloop_t *ml)
{
    iot_list_hook_t *p, *n;
    deleted_t       *d;

    iot_list_foreach(&ml->deleted, p, n) {
        d = iot_list_entry(p, typeof(*d), deleted);
        iot_list_delete(&d->deleted);
        iot_list_delete(&d->hook);
        if (d->free == NULL) {
            iot_debug("purging deleted object %p", d);
            iot_free(d);
        }
        else {
            iot_debug("purging deleted object %p (free cb: %p)", d, d->free);
            if (!d->free(d)) {
                iot_log_error("Failed to free purged item %p.", d);
                iot_list_prepend(p, &d->deleted);
            }
        }
    }
}


iot_mainloop_t *iot_mainloop_create(void)
{
    iot_mainloop_t *ml;

    if ((ml = iot_allocz(sizeof(*ml))) != NULL) {
        ml->epollfd = epoll_create1(EPOLL_CLOEXEC);
        ml->sigfd   = -1;
        ml->fdtbl   = fdtbl_create();

        if (ml->epollfd >= 0 && ml->fdtbl != NULL) {
            iot_list_init(&ml->iowatches);
            iot_list_init(&ml->timers);
            iot_list_init(&ml->deferred);
            iot_list_init(&ml->inactive_deferred);
            iot_list_init(&ml->sighandlers);
            iot_list_init(&ml->wakeups);
            iot_list_init(&ml->deleted);
            iot_list_init(&ml->busses);
            iot_list_init(&ml->eventq);

            ml->eventd = iot_add_deferred(ml, pump_events, ml);
            if (ml->eventd == NULL)
                goto fail;
            iot_disable_deferred(ml->eventd);

            if (!setup_sighandlers(ml))
                goto fail;
        }
        else {
        fail:
            close(ml->epollfd);
            fdtbl_destroy(ml->fdtbl);
            iot_free(ml);
            ml = NULL;
        }
    }



    return ml;
}


void iot_mainloop_destroy(iot_mainloop_t *ml)
{
    if (ml != NULL) {
        iot_clear_superloop(ml);
        purge_io_watches(ml);
        purge_timers(ml);
        purge_deferred(ml);
        purge_sighandlers(ml);
        purge_wakeups(ml);
        purge_deleted(ml);

        close(ml->sigfd);
        close(ml->epollfd);
        fdtbl_destroy(ml->fdtbl);

        iot_free(ml->events);
        iot_free(ml);
    }
}


#if 0
static inline void dump_timers(iot_mainloop_t *ml)
{
    iot_timer_t     *t;
    iot_list_hook_t *p, *n;
    int              i;
    iot_timer_t     *next = NULL;

    iot_debug("timer dump:");
    i = 0;
    iot_list_foreach(&ml->timers, p, n) {
        t = iot_list_entry(p, typeof(*t), hook);

        iot_debug("  #%d: %p, @%u, next %llu (%s)", i, t, t->msecs, t->expire,
                  is_deleted(t) ? "DEAD" : "alive");

        if (!is_deleted(t) && next == NULL)
            next = t;

        i++;
    }

    iot_debug("next timer: %p", ml->next_timer);
    iot_debug("poll timer: %d", ml->poll_timeout);

    if (next != NULL && ml->next_timer != NULL &&
        !is_deleted(ml->next_timer) && next != ml->next_timer) {
        iot_debug("*** BUG ml->next_timer is not the nearest !!! ***");
        if (getenv("__MURPHY_TIMER_CHECK_ABORT") != NULL)
            abort();
    }
}
#endif


int iot_mainloop_prepare(iot_mainloop_t *ml)
{
    iot_timer_t *next_timer;
    int          timeout;
    uint64_t     now;

    if (!iot_list_empty(&ml->deferred)) {
        timeout = 0;
    }
    else {
        next_timer = ml->next_timer;

        if (next_timer == NULL)
            timeout = -1;
        else {
            now = time_now();
            if (IOT_UNLIKELY(next_timer->expire <= now))
                timeout = 0;
            else
                timeout = usecs_to_msecs(next_timer->expire - now);
        }
    }

    ml->poll_timeout = timeout;

    if (ml->nevent < ml->niowatch) {
        ml->nevent = ml->niowatch;
        ml->events = iot_realloc(ml->events, ml->nevent * sizeof(*ml->events));

        IOT_ASSERT(ml->events != NULL, "can't allocate epoll event buffer");
    }

    iot_debug("mainloop %p prepared: %d I/O watches, timeout %d", ml,
              ml->niowatch, ml->poll_timeout);

    return TRUE;
}


static size_t poll_events(void *id, iot_mainloop_t *ml, void **bufp)
{
    void *buf;
    int   n;

    if (IOT_UNLIKELY(id != ml->iow)) {
        iot_log_error("superloop polling with invalid I/O watch (%p != %p)",
                      id, ml->iow);
        *bufp = NULL;
        return 0;
    }

    buf = iot_allocz(ml->nevent * sizeof(ml->events[0]));

    if (buf != NULL) {
        n = epoll_wait(ml->epollfd, buf, ml->nevent, 0);

        if (n < 0)
            n = 0;
    }
    else
        n = 0;

    *bufp = buf;
    return n * sizeof(ml->events[0]);
}


int iot_mainloop_poll(iot_mainloop_t *ml, int may_block)
{
    int n, timeout;

    timeout = may_block && iot_list_empty(&ml->deferred) ? ml->poll_timeout : 0;

    if (ml->nevent > 0) {
        if (ml->super_ops == NULL || ml->super_ops->poll_io == NULL) {
            iot_debug("polling %d descriptors with timeout %d",
                      ml->nevent, timeout);

            n = epoll_wait(ml->epollfd, ml->events, ml->nevent, timeout);

            if (n < 0 && errno == EINTR)
                n = 0;
        }
        else {
            iot_superloop_ops_t *super_ops  = ml->super_ops;
            void                *super_data = ml->super_data;
            void                *id         = ml->iow;
            void                *buf        = ml->events;
            size_t               size       = ml->nevent * sizeof(ml->events[0]);

            size = super_ops->poll_io(super_data, id, buf, size);
            n    = size / sizeof(ml->events[0]);

            IOT_ASSERT(n * sizeof(ml->events[0]) == size,
                       "superloop passed us a partial epoll_event");
        }

        iot_debug("mainloop %p has %d/%d I/O events waiting", ml, n,
                  ml->nevent);

        ml->poll_result = n;
    }
    else {
        /*
         * Notes: Practically we should never branch here because
         *     we always have at least ml->sigfd registered for epoll.
         */
        if (timeout > 0)
            usleep(timeout * USECS_PER_MSEC);

        ml->poll_result = 0;
    }

    return TRUE;
}


static void dispatch_wakeup(iot_mainloop_t *ml)
{
    iot_list_hook_t    *p, *n;
    iot_wakeup_t       *w;
    iot_wakeup_event_t  event;
    uint64_t            now;

    if (ml->poll_timeout == 0) {
        iot_debug("skipping wakeup callbacks (poll timeout was 0)");
        return;
    }

    if (ml->poll_result == 0) {
        iot_debug("woken up by timeout");
        event = IOT_WAKEUP_EVENT_TIMER;
    }
    else {
        iot_debug("woken up by I/O (or signal)");
        event = IOT_WAKEUP_EVENT_IO;
    }

    now = time_now();

    iot_list_foreach(&ml->wakeups, p, n) {
        w = iot_list_entry(p, typeof(*w), hook);

        if (!(w->events & event))
            continue;

        if (!is_deleted(w)) {
            iot_debug("dispatching wakeup cb %p", w);
            wakeup_cb(w, event, now);
        }
        else
            iot_debug("skipping deleted wakeup cb %p", w);

        if (ml->quit)
            break;
    }
}


static void dispatch_deferred(iot_mainloop_t *ml)
{
    iot_list_hook_t *p, *n;
    iot_deferred_t  *d;

    iot_list_foreach(&ml->deferred, p, n) {
        d = iot_list_entry(p, typeof(*d), hook);

        if (!is_deleted(d) && !d->inactive) {
            iot_debug("dispatching active deferred cb %p", d);
            d->cb(d, d->user_data);
        }
        else
            iot_debug("skipping %s deferred cb %p",
                      is_deleted(d) ? "deleted" : "inactive", d);

        if (!is_deleted(d) && d->inactive)
            disable_deferred(d);

        if (ml->quit)
            break;
    }
}


static void dispatch_timers(iot_mainloop_t *ml)
{
    iot_list_hook_t *p, *n;
    iot_timer_t     *t;
    uint64_t         now;

    now = time_now();

    iot_list_foreach(&ml->timers, p, n) {
        t = iot_list_entry(p, typeof(*t), hook);

        if (!is_deleted(t)) {
            if (t->expire <= now) {
                iot_debug("dispatching expired timer %p", t);

                t->cb(t, t->user_data);

                if (!is_deleted(t))
                    rearm_timer(t);
            }
            else
                break;
        }
        else
            iot_debug("skipping deleted timer %p", t);

        if (ml->quit)
            break;
    }
}


static void dispatch_slaves(iot_io_watch_t *w, struct epoll_event *e)
{
    iot_io_watch_t  *s;
    iot_list_hook_t *p, *n;
    iot_io_event_t   events;

    events = e->events & ~(IOT_IO_EVENT_INOUT & w->events);

    iot_list_foreach(&w->slave, p, n) {
        if (events == IOT_IO_EVENT_NONE)
            break;

        s = iot_list_entry(p, typeof(*s), slave);

        if (!is_deleted(s)) {
            iot_debug("dispatching slave I/O watch %p (fd %d)", s, s->fd);
            s->cb(s, s->fd, events, s->user_data);
        }
        else
            iot_debug("skipping slave I/O watch %p (fd %d)", s, s->fd);

        events &= ~(IOT_IO_EVENT_INOUT & s->events);
    }
}


static void dispatch_poll_events(iot_mainloop_t *ml)
{
    struct epoll_event *e;
    iot_io_watch_t     *w, *tblw;
    int                 i, fd;

    for (i = 0, e = ml->events; i < ml->poll_result; i++, e++) {
        fd = e->data.fd;
        w  = fdtbl_lookup(ml->fdtbl, fd);

        if (w == NULL) {
            iot_debug("ignoring event for deleted fd %d", fd);
            continue;
        }

        if (!is_deleted(w)) {
            iot_debug("dispatching I/O watch %p (fd %d)", w, fd);
            w->cb(w, w->fd, e->events, w->user_data);
        }
        else
            iot_debug("skipping deleted I/O watch %p (fd %d)", w, fd);

        if (!iot_list_empty(&w->slave))
            dispatch_slaves(w, e);

        if (e->events & EPOLLRDHUP) {
            tblw = fdtbl_lookup(ml->fdtbl, w->fd);

            if (tblw == w) {
                iot_debug("forcibly stop polling fd %d for watch %p", w->fd, w);
                epoll_del(w);
            }
            else if (tblw != NULL)
                iot_debug("don't stop polling reused fd %d of watch %p",
                          w->fd, w);
        }
        else {
            if ((e->events & EPOLLHUP) && !is_deleted(w)) {
                /*
                 * Notes:
                 *
                 *    If the user does not react to EPOLLHUPs delivered
                 *    we stop monitoring the fd to avoid sitting in an
                 *    infinite busy loop just delivering more EPOLLHUP
                 *    notifications...
                 */

                if (w->wrhup++ > 5) {
                    tblw = fdtbl_lookup(ml->fdtbl, w->fd);

                    if (tblw == w) {
                        iot_debug("forcibly stop polling fd %d for watch %p",
                                  w->fd, w);
                        epoll_del(w);
                    }
                    else if (tblw != NULL)
                        iot_debug("don't stop polling reused fd %d of watch %p",
                                  w->fd, w);
                }
            }
        }

        if (ml->quit)
            break;
    }

    if (ml->quit)
        return;

    iot_debug("done dispatching poll events");
}


int iot_mainloop_dispatch(iot_mainloop_t *ml)
{
    dispatch_wakeup(ml);

    if (ml->quit)
        goto quit;

    dispatch_deferred(ml);

    if (ml->quit)
        goto quit;

    dispatch_timers(ml);

    if (ml->quit)
        goto quit;

    dispatch_poll_events(ml);

 quit:
    purge_deleted(ml);

    return !ml->quit;
}


int iot_mainloop_iterate(iot_mainloop_t *ml)
{
    return
        iot_mainloop_prepare(ml) &&
        iot_mainloop_poll(ml, TRUE) &&
        iot_mainloop_dispatch(ml) &&
        !ml->quit;
}


int iot_mainloop_run(iot_mainloop_t *ml)
{
    while (iot_mainloop_iterate(ml))
        ;

    return ml->exit_code;
}


void iot_mainloop_quit(iot_mainloop_t *ml, int exit_code)
{
    ml->exit_code = exit_code;
    ml->quit      = TRUE;
}


/*
 * event bus and events
 */

static inline void *ref_event_data(void *data, int format)
{
    switch (format & IOT_EVENT_FORMAT_MASK) {
    case IOT_EVENT_FORMAT_JSON:
        return iot_json_ref((iot_json_t *)data);
    default:
        return data;
    }
}


static inline void unref_event_data(void *data, int format)
{
    switch (format & IOT_EVENT_FORMAT_MASK) {
    case IOT_EVENT_FORMAT_JSON:
        iot_json_unref((iot_json_t *)data);
        break;
    default:
        break;
    }
}


iot_event_bus_t *iot_event_bus_get(iot_mainloop_t *ml, const char *name)
{
    iot_list_hook_t *p, *n;
    iot_event_bus_t *bus;

    if (name == NULL || !strcmp(name, IOT_GLOBAL_BUS_NAME))
        return IOT_GLOBAL_BUS;

    iot_list_foreach(&ml->busses, p, n) {
        bus = iot_list_entry(p, typeof(*bus), hook);

        if (!strcmp(bus->name, name))
            return bus;
    }

    bus = iot_allocz(sizeof(*bus));

    if (bus == NULL)
        return NULL;

    bus->name = iot_strdup(name);

    if (bus->name == NULL) {
        iot_free(bus);
        return NULL;
    }

    iot_list_init(&bus->hook);
    iot_list_init(&bus->watches);
    bus->ml = ml;

    iot_list_append(&ml->busses, &bus->hook);

    return bus;
}


uint32_t iot_event_id(const char *name)
{
    iot_event_def_t *e;
    int              i;

    if (events != NULL)
        for (i = 0, e = events; i < nevent; i++, e++)
            if (!strcmp(e->name, name))
                return e->id;

    if (!iot_reallocz(events, nevent, nevent + 1))
        return 0;

    e = events + nevent;

    e->id   = nevent;
    e->name = iot_strdup(name);

    if (e->name == NULL) {
        iot_reallocz(events, nevent + 1, nevent);
        return 0;
    }

    nevent++;

    return e->id;
}


const char *iot_event_name(uint32_t id)
{
    if ((int)id < nevent)
        return events[id].name;
    else
        return IOT_EVENT_UNKNOWN_NAME;
}


char *iot_event_dump_mask(iot_event_mask_t *mask, char *buf, size_t size)
{
    char *p, *t;
    int   l, n, id;

    p = buf;
    l = (int)size;
    t = "";

    IOT_MASK_FOREACH_SET(mask, id, 1) {
        n = snprintf(p, l, "%s%s", t, iot_event_name(id));
        t = "|";

        if (n >= l)
            return "<insufficient mask dump buffer>";

        p += n;
        l -= n;
    }

    return buf;
}


iot_event_watch_t *iot_event_add_watch(iot_event_bus_t *bus, uint32_t id,
                                       iot_event_watch_cb_t cb, void *user_data)
{
    iot_list_hook_t   *watches = bus ? &bus->watches : &ewatches;
    iot_event_watch_t *w;

    w = iot_allocz(sizeof(*w));

    if (w == NULL)
        return NULL;

    iot_list_init(&w->hook);
    iot_mask_init(&w->mask);
    w->bus       = bus;
    w->cb        = cb;
    w->user_data = user_data;

    if (!iot_mask_set(&w->mask, id)) {
        iot_free(w);
        return NULL;
    }

    iot_list_append(watches, &w->hook);

    iot_debug("added event watch %p for event %d (%s) on bus %s", w, id,
              iot_event_name(id), bus ? bus->name : IOT_GLOBAL_BUS_NAME);

    return w;
}


iot_event_watch_t *iot_event_add_watch_mask(iot_event_bus_t *bus,
                                            iot_event_mask_t *mask,
                                            iot_event_watch_cb_t cb,
                                            void *user_data)
{
    iot_list_hook_t   *watches = bus ? &bus->watches : &ewatches;
    iot_event_watch_t *w;
    char               events[512];

    w = iot_allocz(sizeof(*w));

    if (w == NULL)
        return NULL;

    iot_list_init(&w->hook);
    iot_mask_init(&w->mask);
    w->bus       = bus;
    w->cb        = cb;
    w->user_data = user_data;

    if (!iot_mask_copy(&w->mask, mask)) {
        iot_free(w);
        return NULL;
    }

    iot_list_append(watches, &w->hook);

    iot_debug("added event watch %p for events <%s> on bus %s", w,
              iot_event_dump_mask(&w->mask, events, sizeof(events)),
              bus ? bus->name : IOT_GLOBAL_BUS_NAME);

    return w;
}


void iot_event_del_watch(iot_event_watch_t *w)
{
    if (w == NULL)
        return;

    if (w->bus != NULL && w->bus->busy) {
        w->dead = TRUE;
        w->bus->dead++;
        return;
    }

    iot_list_delete(&w->hook);
    iot_mask_reset(&w->mask);
    iot_free(w);
}


void bus_purge_dead(iot_event_bus_t *bus)
{
    iot_event_watch_t *w;
    iot_list_hook_t   *p, *n;

    if (!bus->dead)
        return;

    iot_list_foreach(&bus->watches, p, n) {
        w = iot_list_entry(p, typeof(*w), hook);

        if (!w->dead)
            continue;

        iot_list_delete(&w->hook);
        iot_mask_reset(&w->mask);
        iot_free(w);
    }

    bus->dead = 0;
}


static int queue_event(iot_event_bus_t *bus, uint32_t id, void *data,
                       iot_event_flag_t flags)
{
    pending_event_t *e;

    e = iot_allocz(sizeof(*e));

    if (e == NULL)
        return -1;

    iot_list_init(&e->hook);
    e->bus    = bus;
    e->id     = id;
    e->format = flags & IOT_EVENT_FORMAT_MASK;
    e->data   = ref_event_data(data, e->format);
    iot_list_append(&bus->ml->eventq, &e->hook);

    iot_enable_deferred(bus->ml->eventd);

    return 0;
}


static int emit_event(iot_event_bus_t *bus, uint32_t id, void *data,
                      iot_event_flag_t flags)
{
    iot_list_hook_t   *watches;
    iot_event_watch_t *w;
    iot_list_hook_t   *p, *n;

    if (bus)
        watches = &bus->watches;
    else {
        if (!(flags & IOT_EVENT_SYNCHRONOUS)) {
            errno = EINVAL;
            return -1;
        }
        watches = &ewatches;
    }

    if (bus)
        bus->busy++;

    iot_debug("emitting event 0x%x (%s) on bus <%s>", id, iot_event_name(id),
              bus ? bus->name : IOT_GLOBAL_BUS_NAME);

    iot_list_foreach(watches, p, n) {
        w = iot_list_entry(p, typeof(*w), hook);

        if (w->dead)
            continue;

        if (iot_mask_test(&w->mask, id))
            w->cb(w, id, flags & IOT_EVENT_FORMAT_MASK, data, w->user_data);
    }

    if (bus) {
        bus->busy--;

        if (!bus->busy)
            bus_purge_dead(bus);
    }

    return 0;
}


static void pump_events(iot_deferred_t *d, void *user_data)
{
    iot_mainloop_t  *ml = (iot_mainloop_t *)user_data;
    iot_list_hook_t *p, *n;
    pending_event_t *e;

 pump:
    iot_list_foreach(&ml->eventq, p, n) {
        e = iot_list_entry(p, typeof(*e), hook);

        emit_event(e->bus, e->id, e->data, e->format);

        iot_list_delete(&e->hook);
        unref_event_data(e->data, e->format);

        iot_free(e);
    }

    if (!iot_list_empty(&ml->eventq))
        goto pump;

    iot_disable_deferred(d);
}


int iot_emit_event(iot_event_bus_t *bus, uint32_t id, iot_event_flag_t flags,
                   void *data)
{
    int status;

    if (flags & IOT_EVENT_SYNCHRONOUS) {
        ref_event_data(data, flags);
        status = emit_event(bus, id, data, flags);
        unref_event_data(data, flags);

        return status;
    }
    else {
        if (bus != NULL)
            return queue_event(bus, id, data, flags);

        errno = EOPNOTSUPP;
        return -1;
    }
}


IOT_INIT static void init_events(void)
{
    IOT_ASSERT(iot_event_id(IOT_EVENT_UNKNOWN_NAME) == IOT_EVENT_UNKNOWN,
               "reserved id 0x%x for builtin event <%s> already taken",
               IOT_EVENT_UNKNOWN, IOT_EVENT_UNKNOWN_NAME);
}
