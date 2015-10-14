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

#include <iot/common/macros.h>
#include <iot/common/list.h>
#include <iot/common/mm.h>
#include <iot/common/debug.h>
#include <iot/common/log.h>
#include <iot/common/mask.h>
#include <iot/common/hash-table.h>

/*
 * If __INLINED_MASKS__ is defined we use inlined chunk allocation bitmasks.
 * This might save memory by putting the chunk allocation mask into the
 * leftover area after the last entry in the chunk.
 */
#define __INLINED_MASKS__

#define MIN_BUCKETS   16                 /* use at least this many buckets */
#define MAX_BUCKETS  512                 /* use at most this many buckets */
#define CHUNKSIZE   4096                 /* allocation chunk size */

typedef struct {
    uint32_t table_maxmem;               /* max memory for a single table */
    uint32_t total_maxmem;               /* max memory for all tables */
} hash_limits_t;

typedef struct {
    iot_list_hook_t  hook;               /* to bucket entries */
    const void      *key;                /* key for this entry */
    const void      *object;             /* object for this entry */
    uint32_t         cookie;             /* cookie for fast access */
} hash_entry_t;

typedef struct {
    iot_list_hook_t hook;                /* to empty/used/full bucket list */
    iot_list_hook_t entries;             /* entries for this bucket */
} hash_bucket_t;

typedef struct {
#ifndef __INLINED_MASKS__
    iot_mask_t       used;               /* entry usage mask */
#endif
    iot_list_hook_t  hook;               /* to free list */
    uint32_t         idx;                /* hash chunk index */
    hash_entry_t     entries[0];         /* actual entries */
} hash_chunk_t;

typedef struct {
    iot_list_hook_t *b;                  /* hook of current bucket */
    iot_list_hook_t *e;                  /* hook of current entry */
    uint32_t         g;                  /* iterator generation */
    int              d;                  /* iterating direction */
} hash_iter_t;

struct iot_hashtbl_s {
    uint32_t          nentry;            /* used table entries */
    uint32_t          nlimit;            /* maximum allowed entries */
    uint32_t          nalloc;            /* allocated table entries */
    iot_hash_fn_t     hash;              /* key hash function */
    iot_comp_fn_t     comp;              /* key comparison function */
    iot_free_fn_t     free;              /* object freeing function */
    hash_bucket_t    *buckets;           /* hash buckets */
    uint32_t          nbucket;           /* number of buckets */
    iot_list_hook_t   used;              /* used buckets (for iteration) */
    hash_chunk_t    **chunks;            /* entry chunks */
    uint32_t          nchunk;            /* number of chunks */
    uint32_t          nperchunk;         /* entries in a single chunk */
    uint32_t          nlast;             /* entries in last chunk */
    iot_list_hook_t   space;             /* chunks with free entries */
    hash_iter_t       it;                /* current/last seen iterator */
};

static hash_limits_t limits = { 0, 0 };

static int calculate_sizes(iot_hashtbl_t *t)
{
#ifndef __INLINED_MASKS__

    t->nperchunk = (CHUNKSIZE - sizeof(hash_chunk_t)) / sizeof(hash_entry_t);

#else

    int           usable, mask;
    hash_chunk_t *c;
    char         *e;

    /*
     * When inline masks are enabled we keep the chunk allocation bitmask
     * including the bitmask bits in the chunk itself after the last hash
     * entry. We need then to calculate nperchunk (the number of entries
     * that fit into a chunk) so that there is enough room spared at the
     * end for a bitmask of nperchunk bits. We do this with a brute-force
     * opportunistic algorithm:
     *   1) calculate nperchunk disregarding the bitmask bits
     *   2) see if the last entry in the mask fits in CHUNKSIZE
     *   3) if not, reduce the usable size by the bitmask necessary for
     *      the unadjusted nperchunk entries and recalculate nperchunk.
     *   4) Make a final verification that now we fit into CHUNKSIZE.
     */

    t->nperchunk = (CHUNKSIZE - sizeof(hash_chunk_t)) / sizeof(hash_entry_t);
    mask = iot_mask_inlined_size(t->nperchunk);

    c = (hash_chunk_t *)0x0;
    e = (char *)&c->entries[t->nperchunk] + mask;

    if (e >= ((char *)c) + CHUNKSIZE) {
        iot_debug("adjusting nperchunk (%d, %d bytes) to fit into %d bytes",
                  t->nperchunk, (int)(ptrdiff_t)e, CHUNKSIZE);

        usable = CHUNKSIZE - sizeof(hash_chunk_t) - mask;
        t->nperchunk = usable / sizeof(hash_entry_t);

        mask = iot_mask_inlined_size(t->nperchunk);

        c = (hash_chunk_t *)0x0;
        e = (char *)&c->entries[t->nperchunk] + mask;

        iot_debug("adjusted nperchunk to %d, %d bytes", t->nperchunk,
                  (int)(ptrdiff_t)e);

        IOT_ASSERT(e < ((char *)c) + CHUNKSIZE,
                   "hash_chunk inlined allocation bitmask overflow");
    }
#endif

    IOT_ASSERT(IOT_OFFSET(hash_chunk_t, entries[t->nperchunk]) < CHUNKSIZE,
               "hash_chunk_t overflow, nperchunk too large ?");

    if (!t->nbucket) {
        if (t->nlimit)
            t->nbucket = t->nlimit / 16;
        else if (t->nalloc)
            t->nbucket = t->nalloc / 4;
    }

    if (t->nbucket < MIN_BUCKETS)
        t->nbucket = MIN_BUCKETS;

    if (t->nbucket > MAX_BUCKETS)
        t->nbucket = MAX_BUCKETS;

    iot_debug("%u entries per chunk, %u buckets", t->nperchunk, t->nbucket);

    return 0;
}


static inline iot_mask_t *chunk_mask(hash_chunk_t *c, int nentry)
{
    iot_mask_t *m;

#ifdef __INLINED_MASKS__
    m = (void *)&c->entries[nentry];
#else
    IOT_UNUSED(nentry);

    m = &c->used;
#endif

    return m;
}


static int allocate_chunks(iot_hashtbl_t *t, uint32_t nentry)
{
    hash_chunk_t *c;
    iot_mask_t   *m;
    uint32_t      nchunk, n, total, full, last, size, i;

    IOT_ASSERT(t->nlast == 0,
               "%s(): hash-table internal error, can't allocate more chunks, "
               "last chunk not full-sized", __FUNCTION__);

    full = nentry / t->nperchunk;
    last = nentry % t->nperchunk;

    /*
     * Notes:
     *    If we're not reaching our preset limit yet, we need to make sure
     *    the last chunk gets allocated full-sized. This way we don't need
     *    to realloc (and potentially move) the current last chunk and we
     *    can avoid having to play games with list-relocation/adjustment.
     */
    if (last != 0) {
        total = t->nalloc + (full + 1) * t->nperchunk;

        if (total <= t->nlimit) {
            full += 1;
            last  = 0;
        }
        else {
            total  = t->nlimit;
            total -= t->nalloc;
            full   = total / t->nperchunk;
            last   = total % t->nperchunk;
        }
    }

    n = full + (last ? 1 : 0);
    nchunk = t->nchunk + n;

    iot_debug("resizing by %u entries: %u -> %u (%u + %u)", nentry,
              t->nchunk, nchunk, full, last);

    if (!iot_reallocz(t->chunks, t->nchunk, nchunk))
        return -1;

    for (i = 0; i < n; i++) {
        if (i < full)
            size = CHUNKSIZE;
        else {
            size = IOT_OFFSET(hash_chunk_t, entries[last]);
            size += iot_mask_inlined_size(last);
        }

        if (iot_memalignz((void **)&c, CHUNKSIZE, size) < 0)
            return -1;

        iot_list_init(&c->hook);

        c->idx = t->nchunk;
        t->chunks[c->idx] = c;

        iot_list_append(&t->space, &c->hook);

        t->nchunk++;
        t->nalloc += i < full ? t->nperchunk : last;

        m = chunk_mask(c, i < full ? t->nperchunk : last);

#ifdef __INLINED_MASKS__
        if (!iot_mask_init_inlined(m, i < full ? t->nperchunk : last, c, size))
            return -1;
#else
        iot_mask_init(m);

        if (!iot_mask_ensure(&c->used, t->nperchunk))
            return -1;
#endif
    }

    t->nlast = last;

    iot_debug("resized by %u full chunks + %u last entries", full, last);

    return 0;
}


static inline hash_bucket_t *hash_bucket(iot_hashtbl_t *t, uint32_t h)
{
    uint32_t idx;
    uint32_t i;

    idx = h % t->nbucket;

    if (t->buckets == NULL) {
        t->buckets = iot_allocz(t->nbucket * sizeof(t->buckets[0]));

        if (t->buckets == NULL)
            return NULL;

        for (i = 0; i < t->nbucket; i++) {
            iot_list_init(&t->buckets[i].hook);
            iot_list_init(&t->buckets[i].entries);
        }
    }

    return t->buckets + idx;
}


iot_hashtbl_t *iot_hashtbl_create(iot_hashtbl_config_t *config)
{
    iot_hashtbl_t *t;

    if (config->hash == NULL || config->comp == NULL) {
        errno = EINVAL;
        return NULL;
    }

    if (config->nalloc && config->nlimit && config->nlimit < config->nalloc) {
        errno = EINVAL;
        return NULL;
    }

    t = iot_allocz(sizeof(*t));

    if (t == NULL)
        return NULL;

    iot_list_init(&t->used);
    iot_list_init(&t->space);

    t->hash = config->hash;
    t->comp = config->comp;
    t->free = config->free;

    t->nlimit  = config->nlimit;
    t->nbucket = config->nbucket;

    if (calculate_sizes(t) < 0)
        goto fail;

    if (allocate_chunks(t, t->nalloc) < 0)
        goto fail;

    return t;

 fail:
    iot_hashtbl_destroy(t, false);
    return NULL;
}


static inline hash_chunk_t *entry_chunk(hash_entry_t *e)
{
    return (hash_chunk_t *)((ptrdiff_t)e & ~(CHUNKSIZE - 1));
}


static inline uint32_t entry_cookie(iot_hashtbl_t *t, hash_entry_t *e)
{
    hash_chunk_t *c;
    uint32_t      cidx, eidx, cookie;

    if (e == NULL)
        return IOT_HASH_COOKIE_NONE;

    c = entry_chunk(e);
    cidx = c->idx;
    eidx = e - c->entries;

    cookie = cidx * t->nperchunk + eidx + 1;

    iot_debug("entry %p => cookie 0x%x <%u.%u>", e, cookie, cidx, eidx);

    return cookie;
}


static inline hash_entry_t *cookie_entry(iot_hashtbl_t *t, uint32_t cookie)
{
    hash_entry_t *e;
    uint32_t      cidx, eidx, n;

    cidx = (cookie - 1) / t->nperchunk;
    eidx = (cookie - 1) % t->nperchunk;

    if (cidx >= t->nchunk) {
        if (cookie - 1 >= t->nlimit) {
        erange:
            errno = ERANGE;
            return NULL;
        }

        n = cookie - t->nalloc;

        if (allocate_chunks(t, n) < 0)
            return NULL;

        IOT_ASSERT(cidx < t->nchunk, "hash-table chunk allocation error");
    }

    if (eidx >= t->nperchunk)
        goto erange;

    if (t->nlast && cidx == t->nchunk - 1 && eidx >= t->nlast)
        goto erange;

    e = t->chunks[cidx]->entries + eidx;

    iot_debug("cookie 0x%x <%u.%u> => entry %p", cookie, cidx, eidx, e);

    return e;
}


static inline hash_entry_t *hash_entry(iot_hashtbl_t *t, const void *key,
                                       uint32_t cookie, hash_bucket_t **bptr)
{
    hash_bucket_t   *b;
    hash_entry_t    *e;
    iot_list_hook_t *p, *n;
    uint32_t         h;
    int              diff;

    h = t->hash(key);
    if (bptr != NULL && *bptr != NULL)
        b = *bptr;
    else {
        b = hash_bucket(t, h);
        if (bptr != NULL)
            *bptr = b;
    }

    if (b == NULL)
        goto not_found;

    if (bptr != NULL)
        *bptr = b;

    iot_list_foreach(&b->entries, p, n) {
        e = iot_list_entry(p, typeof(*e), hook);

        diff = t->comp(key, e->key);

        if (diff < 0)
            continue;

        if (diff == 0) {
            if (cookie == IOT_HASH_COOKIE_NONE || e->cookie == cookie)
                return e;
            else
                continue;
        }
    }

 not_found:
    errno = ENOENT;
    return NULL;
}


static inline hash_entry_t *alloc_entry(iot_hashtbl_t *t)
{
    hash_chunk_t    *c;
    iot_mask_t      *m;
    iot_list_hook_t *p, *n;
    int              i;

    iot_list_foreach(&t->space, p, n) {
        c = iot_list_entry(p, typeof(*c), hook);
        m = chunk_mask(c, c->idx == t->nchunk - 1 && t->nlast ?
                       t->nlast : t->nperchunk);
        i = iot_mask_alloc(m);

        if ((i < 0 || i >= (int)t->nperchunk) ||
            (c->idx == t->nchunk - 1 && t->nlast && i >= (int)t->nlast)) {
            iot_debug("chunk #%d full, unlinking from space list", c->idx);
            iot_list_delete(&c->hook);
        }
        else
            return c->entries + i;
    }

    errno = ENOSPC;
    return NULL;
}


static inline void free_entry(iot_hashtbl_t *t, hash_entry_t *e, bool release)
{
    hash_chunk_t *c;
    iot_mask_t   *m;
    uint32_t      i;

    if (release && t->free)
        t->free((void *)e->key, (void *)e->object);

    e->cookie = IOT_HASH_COOKIE_NONE;
    e->key = e->object = NULL;

    iot_list_delete(&e->hook);

    c = entry_chunk(e);
    m = chunk_mask(c, c->idx == t->nchunk - 1 && t->nlast ?
                   t->nlast : t->nperchunk);
    i = e - c->entries;

    iot_mask_clear(m, i);

    if (iot_list_empty(&c->hook))
        iot_list_append(&t->space, &c->hook);
}


void iot_hashtbl_reset(iot_hashtbl_t *t, bool release)
{
    hash_bucket_t   *b;
    iot_list_hook_t *bp, *bn;
    hash_entry_t    *e;
    iot_list_hook_t *ep, *en;

    if (t == NULL)
        return;

    iot_list_foreach(&t->used, bp, bn) {
        b = iot_list_entry(bp, typeof(*b), hook);

        iot_list_foreach(&b->entries, ep, en) {
            e = iot_list_entry(ep, typeof(*e), hook);
            free_entry(t, e, release);
        }
    }

    t->nentry = 0;
}


void iot_hashtbl_destroy(iot_hashtbl_t *t, bool release)
{
    uint32_t i;

    if (t == NULL)
        return;

    iot_hashtbl_reset(t, release);

    for (i = 0; i < t->nchunk; i++)
        iot_free(t->chunks[i]);

    iot_free(t->chunks);
    iot_free(t);
}


int iot_hashtbl_add(iot_hashtbl_t *t, const void *key, void *obj,
                    uint32_t *cookiep)
{
    hash_entry_t  *e;
    hash_bucket_t *b;
    uint32_t       cookie, n;

    if (t == NULL) {
        errno = EINVAL;
        return -1;
    }

    if (t->nlimit && t->nentry >= t->nlimit) {
        errno = ENOSPC;
        return -1;
    }

    cookie = cookiep ? *cookiep : IOT_HASH_COOKIE_NONE;

    if (cookie != IOT_HASH_COOKIE_NONE) {
        e = cookie_entry(t, cookie);

        if (e == NULL)
            return -1;
    }
    else {
        if (t->nalloc <= t->nentry) {
            if (t->nalloc + t->nperchunk < t->nlimit)
                n = t->nperchunk;
            else {
                if (t->nlimit)
                    n = t->nlimit - t->nalloc;
                else
                    n = t->nperchunk;
            }

            allocate_chunks(t, n);
        }

        e = alloc_entry(t);

        if (e == NULL)
            return -1;

        cookie = entry_cookie(t, e);
    }

    e->cookie = cookie;
    e->key    = key;
    e->object = obj;

    if ((b = hash_bucket(t, t->hash(key))) == NULL)
        return -1;

    iot_list_append(&b->entries, &e->hook);

    if (iot_list_empty(&b->hook))
        iot_list_append(&t->used, &b->hook);

    t->nentry++;

    if (cookiep != NULL)
        *cookiep = cookie;

    return 0;
}


void *iot_hashtbl_del(iot_hashtbl_t *t, const void *key, uint32_t cookie,
                      bool release)
{
    hash_bucket_t *b;
    hash_entry_t  *e;
    void          *obj;
    int            dir;

    if (t == NULL) {
        errno = EINVAL;
        return NULL;
    }

    b = hash_bucket(t, t->hash(key));

    if (b == NULL)
        return NULL;

    if (cookie != IOT_HASH_COOKIE_NONE){
        e = cookie_entry(t, cookie);

        if (e == NULL || e->cookie != cookie)
            goto find_by_key;
    }
    else {
    find_by_key:
        e = hash_entry(t, key, cookie, &b);
    }

    if (e == NULL) {
        errno = ENOENT;
        return NULL;
    }

    if (t->it.e && t->it.e == &e->hook) {
        if (!_iot_hashtbl_iter(t, (iot_hashtbl_iter_t *)&t->it, dir = -t->it.d,
                               NULL, NULL, NULL)) {
            t->it.b = NULL;
            t->it.e = NULL;
            t->it.d = -dir;
        }
    }

    obj = (void *)e->object;
    free_entry(t, e, release);

    if (iot_list_empty(&b->entries))
        iot_list_delete(&b->hook);

    t->nentry--;

    return obj;
}


void *iot_hashtbl_lookup(iot_hashtbl_t *t, const void *key, uint32_t cookie)
{
    hash_bucket_t *b;
    hash_entry_t  *e;

    if (t == NULL) {
        errno = EINVAL;
        return  NULL;
    }

    b = hash_bucket(t, t->hash(key));

    if (b == NULL)
        return NULL;

    if (cookie != IOT_HASH_COOKIE_NONE) {
        e = cookie_entry(t, cookie);

        if (e == NULL || e->cookie != cookie || t->comp(key, e->key))
            goto find_by_key;
    }
    else {
    find_by_key:
        e = hash_entry(t, key, cookie, &b);
    }

    if (e == NULL)
        return NULL;

    if (cookie != IOT_HASH_COOKIE_NONE && e->cookie != cookie)
        return NULL;

    return (void *)e->object;
}


void *iot_hashtbl_replace(iot_hashtbl_t *t, void *key, uint32_t cookie,
                          void *obj, bool release)
{
    hash_bucket_t *b;
    hash_entry_t  *e;
    void          *old;
    int            dir;

    if (t == NULL) {
        errno = EINVAL;
        return  NULL;
    }

    b = hash_bucket(t, t->hash(key));

    if (b == NULL) {
    add:
        iot_hashtbl_add(t, key, obj, &cookie);
        return NULL;
    }

    if (cookie != IOT_HASH_COOKIE_NONE) {
        e = cookie_entry(t, cookie);

        if (e == NULL || e->cookie != cookie)
            goto find_by_key;
    }
    else {
    find_by_key:
        e = hash_entry(t, key, cookie, &b);
    }

    if (e == NULL)
        goto add;

    if (t->it.e && t->it.e == &e->hook) {
        if (!_iot_hashtbl_iter(t, (iot_hashtbl_iter_t *)&t->it, dir = -t->it.d,
                               NULL, NULL, NULL)) {
            t->it.b = NULL;
            t->it.e = NULL;
            t->it.d = -dir;
        }
    }

    old = (void *)e->object;

    if (t->free && release)
        t->free((void *)e->key, (void *)e->object);

    e->key    = key;
    e->object = obj;

    return old;
}


void _iot_hashtbl_begin(iot_hashtbl_t *t, iot_hashtbl_iter_t *it, int dir)
{
    it->b = t->it.b = NULL;
    it->e = t->it.e = NULL;
    it->d = t->it.d = dir;
    it->g = ++(t->it.g);
}


void *_iot_hashtbl_iter(iot_hashtbl_t *t, iot_hashtbl_iter_t *it, int dir,
                        const void **key, uint32_t *cookie, const void **obj)
{
    iot_list_hook_t *bp, *bn, *ep, *en;
    hash_bucket_t   *b;
    hash_entry_t    *e;

    if (it->g != t->it.g) {
        errno = EBUSY;
        goto end;
    }

    if (it->g == t->it.g) {
        it->b = t->it.b;
        it->e = t->it.e;
        it->d = t->it.d;
    }

    if (it->b == NULL) {
        if (iot_list_empty(&t->used))
            goto end;

        it->b = &t->used;
        goto next_bucket;
    }
    else {
        bp = it->b;
        bn = (dir < 0 ? bp->prev : bp->next);
        b  = iot_list_entry(bp, typeof(*b), hook);
        ep = it->e;

        if (ep == NULL) /* hmm... can this happen ? */
            ep = it->e = t->it.e = &b->entries;

        goto next_entry;
    }

 next_bucket:
    bp = it->b;
    bn = (dir < 0 ? bp->prev : bp->next);

    if (bn == &t->used) {
    end:
        if (key)
            *key = NULL;
        if (cookie)
            *cookie = 0;
        if (obj)
            *obj = NULL;
        it->b = it->e = NULL;
        t->it.b = t->it.e = NULL;
        t->it.d = 0;

        return NULL;
    }

    bp = bn;
    b  = iot_list_entry(bp, typeof(*b), hook);
    ep = &b->entries;

 next_entry:
    en = (dir < 0 ? ep->prev : ep->next);

    if (en == &b->entries)
        goto next_bucket;

    ep = en;
    e  = iot_list_entry(ep, typeof(*e), hook);

    if (key)
        *key = e->key;
    if (cookie)
        *cookie = e->cookie;
    if (obj)
        *obj = e->object;

    iot_debug("%s(%d): now at cookie 0x%x", __FUNCTION__, dir, e->cookie);

    it->b = t->it.b = bp;
    it->e = t->it.e = ep;

    return it;
}


uint32_t iot_hash_string(const void *key)
{
    uint32_t    h;
    const char *p;

    for (h = 0, p = key; *p; p++) {
        h <<= 1;
        h  ^= *p;
    }

    return h;
}


int iot_comp_string(const void *key1, const void *key2)
{
    return strcmp((const char *)key1, (const char *)key2);
}


uint32_t iot_hash_direct(const void *key)
{
    return (uint32_t)(ptrdiff_t)key;
}


int iot_comp_direct(const void *key1, const void *key2)
{
    return (ptrdiff_t)key1 - (ptrdiff_t)key2;
}


int iot_hashtbl_set_limits(iot_hashtbl_limits_t *l)
{
    limits.table_maxmem = l->table_maxmem;
    limits.total_maxmem = l->total_maxmem;

    return 0;
}


int iot_hashtbl_add_limits(iot_hashtbl_limits_t *l)
{
    if (l->table_maxmem)
        limits.table_maxmem = l->table_maxmem;
    if (l->total_maxmem)
        limits.total_maxmem = l->total_maxmem;

    return 0;
}
