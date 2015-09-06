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

#ifndef __IOT_LIST_H__
#define __IOT_LIST_H__

#include <iot/common/macros.h>


IOT_CDECL_BEGIN


/** \file
 * A simple doubly-linked circular list implementation, obviously inspired
 * by the linux kernel.
 */


/** A list hook. Used both a list head and to hook up objects to the list. */
typedef struct iot_list_hook_s iot_list_hook_t;
struct iot_list_hook_s {
    iot_list_hook_t *prev;
    iot_list_hook_t *next;
};

/** Macro to initialize a list to be empty. */
#define IOT_LIST_INIT(list) { .prev = &(list), .next = &(list) }

/** Macro to define a list and initialize it to be empty. */
#define IOT_LIST_HOOK(list) iot_list_hook_t list = IOT_LIST_INIT(list)

/** Initialize a list to be empty. */
static inline void iot_list_init(iot_list_hook_t *list)
{
    list->prev = list->next = list;
}

/** Check if a list is empty. */
static inline int iot_list_empty(iot_list_hook_t *list)
{
    if (list->next == list->prev) {
        if (list->next == list)
            return TRUE;

#ifdef __IOT_LIST_ALLOW_NULL
        if (!list->next)
            return TRUE;
#endif
    }

    return FALSE;
}

/** Append a new item to a list (add it after the last item). */
static inline void iot_list_append(iot_list_hook_t *list, iot_list_hook_t *item)
{
    if (iot_list_empty(list)) {
        list->next = list->prev = item;
        item->next = item->prev = list;
    }
    else {
        iot_list_hook_t *prev = list->prev;

        prev->next = item;
        item->prev = prev;
        item->next = list;
        list->prev = item;
    }
}

/** Prepend a new item to a list (add it before the first item). */
static inline void iot_list_prepend(iot_list_hook_t *list,
                                    iot_list_hook_t *item)
{
    if (iot_list_empty(list)) {
        list->next = list->prev = item;
        item->next = item->prev = list;
    }
    else {
        iot_list_hook_t *next = list->next;

        list->next = item;
        item->prev = list;
        item->next = next;
        next->prev = item;
    }
}

/** Insert a new item to the list before a given item. */
static inline void iot_list_insert_before(iot_list_hook_t *next,
                                          iot_list_hook_t *item)
{
    iot_list_append(next, item);
}

/** Insert a new item to the list after a given item. */
static inline void iot_list_insert_after(iot_list_hook_t *prev,
                                         iot_list_hook_t *item)
{
    iot_list_prepend(prev, item);
}

/** Delete the given item from the list. */
static inline void iot_list_delete(iot_list_hook_t *item)
{
    iot_list_hook_t *prev, *next;

    if (!iot_list_empty(item)) {
        prev = item->prev;
        next = item->next;

        prev->next = next;
        next->prev = prev;

        item->prev = item->next = item;
    }
}

/** Reattach a list to a new hook. Initialize old hook to be empty. */
static inline void iot_list_move(iot_list_hook_t *new_hook,
                                 iot_list_hook_t *old_hook)
{
    *new_hook = *old_hook;

    new_hook->next->prev = new_hook;
    new_hook->prev->next = new_hook;

    iot_list_init(old_hook);
}

/** Append the tail to head, reinitialize tail to be empty. */
static inline void iot_list_join(iot_list_hook_t *head,
                                 iot_list_hook_t *tail)
{
    if (iot_list_empty(tail))
        return;

    if (iot_list_empty(head)) {
        iot_list_move(head, tail);
        return;
    }

    head->prev->next = tail->next;
    tail->next->prev = head->prev;

    tail->prev->next = head;
    head->prev = tail->prev;

    iot_list_init(tail);
}

/** Update a list when the address of a hook has changed (eg. by realloc). */
static inline void iot_list_update_address(iot_list_hook_t *new_addr,
                                           iot_list_hook_t *old_addr)
{
    iot_list_hook_t *prev, *next;
    ptrdiff_t        diff;

    diff = new_addr - old_addr;
    prev = new_addr->prev;
    next = new_addr->next;

    prev->next += diff;
    next->prev += diff;
}


/** Macro to iterate through a list (current item safe to remove). */
#define iot_list_foreach(list, p, n)                                      \
    if ((list)->next != NULL)                                             \
        for (p = (list)->next, n = p->next; p != (list); p = n, n = n->next)

/** Macro to iterate through a list backwards (current item safe to remove). */
#define iot_list_foreach_back(list, p, n)                                 \
    if ((list)->prev != NULL)                                             \
        for (p = (list)->prev, n = p->prev; p != (list); p = n, n = n->prev)

/** Macro to get a pointer to a embedding structure from a list pointer. */
#ifndef __cplusplus
#    define PTR_ARITH_TYPE void
#else
#    define PTR_ARITH_TYPE char
#endif

#define iot_list_entry(ptr, type, member)                                 \
    (type *)(((PTR_ARITH_TYPE *)(ptr)) - IOT_OFFSET(type, member))


IOT_CDECL_END


#endif /* __IOT_LIST_H__ */

