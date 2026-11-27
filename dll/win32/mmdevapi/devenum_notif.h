/*
 * PROJECT:     ReactOS Audio Sound Manager
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later): the licence of devenum.c, whose register and unregister bookkeeping and walks moved here
 * PURPOSE:     The notification clients of the device enumerator: the register and unregister bookkeeping and the walks that never call a client unregistered under them and never free a wrapper a walk still holds (N7-04). Free of the device list's own types and of COM, so that the host test runs the very code the DLL runs
 * COPYRIGHT:   Copyright 2026 LiberNT contributors
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA
 */

#ifndef _MMDEVAPI_DEVENUM_NOTIF_H_
#define _MMDEVAPI_DEVENUM_NOTIF_H_

#include <stddef.h>
#include <wine/list.h>

/* The includer provides UINT32, BOOLEAN, malloc and free */

/* A registered client's wrapper: dead from its unregistration under a walk until no walk holds it */
struct notif_client
{
    void *client;
    struct list entry;
    BOOLEAN dead;
};

/*
 * The registered clients of an enumerator, and how many walks are in progress
 * on the thread holding the lock. The lock is recursive, so a callback may
 * unregister a client and notify again under the walk that calls it; a walk
 * under a callback is another walk. Every function below is called with the
 * lock held.
 */
struct notif_clients
{
    struct list clients;
    UINT32 walks;
};

#define NOTIF_CLIENTS_INIT(name) { LIST_INIT(name.clients), 0 }

static inline void notif_clients_init(struct notif_clients *clients)
{
    list_init(&clients->clients);
    clients->walks = 0;
}

/* The client's wrapper, added at the end of the list; NULL when the memory is not had */
static inline struct notif_client *notif_clients_register(struct notif_clients *clients, void *client)
{
    struct notif_client *wrapper = malloc(sizeof(*wrapper));

    if (!wrapper)
        return NULL;
    wrapper->client = client;
    wrapper->dead = FALSE;
    list_add_tail(&clients->clients, &wrapper->entry);
    return wrapper;
}

/* A wrapper let go: under a walk it is only marked dead, since a walk may hold a pointer to it; else it is unlinked and freed at once */
static inline void notif_client_release(struct notif_clients *clients, struct notif_client *wrapper)
{
    if (clients->walks)
    {
        wrapper->dead = TRUE;
        return;
    }
    list_remove(&wrapper->entry);
    free(wrapper);
}

/* The client's wrapper found and let go; FALSE when the client is not registered, or is going already (dead under a walk) */
static inline BOOLEAN notif_clients_unregister(struct notif_clients *clients, void *client)
{
    struct notif_client *wrapper;

    LIST_FOR_EACH_ENTRY(wrapper, &clients->clients, struct notif_client, entry)
        if (wrapper->client == client && !wrapper->dead)
        {
            notif_client_release(clients, wrapper);
            return TRUE;
        }
    return FALSE;
}

/* The dead wrappers unlinked and freed, when no walk holds one */
static inline void notif_clients_sweep(struct notif_clients *clients)
{
    struct notif_client *wrapper, *next;

    if (clients->walks)
        return;
    LIST_FOR_EACH_ENTRY_SAFE(wrapper, next, &clients->clients, struct notif_client, entry)
        if (wrapper->dead)
        {
            list_remove(&wrapper->entry);
            free(wrapper);
        }
}

/*
 * Every client that lives, called once in the list's order with Context.
 * A client unregistered under the walk, by itself or by another's callback,
 * is marked dead and is not called again, by this walk or by one under it;
 * its wrapper is freed when the last walk on the thread ends. A client
 * registered under the walk is at the end of the list, where this walk
 * reaches it.
 */
static inline void notif_clients_walk(struct notif_clients *clients, void (*call)(void *client, void *context), void *context)
{
    struct notif_client *wrapper, *next;

    clients->walks++;
    LIST_FOR_EACH_ENTRY_SAFE(wrapper, next, &clients->clients, struct notif_client, entry)
        if (!wrapper->dead)
            call(wrapper->client, context);
    clients->walks--;
    notif_clients_sweep(clients);
}

#endif /* _MMDEVAPI_DEVENUM_NOTIF_H_ */
