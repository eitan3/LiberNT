/*
 * PROJECT:     LiberNT host-native tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     The device enumerator's notification clients and the diff of a device set (dll/win32/mmdevapi/devenum_notif.h, the code mmdevapi.dll compiles): the three device callbacks per verdict on every registered client and none for an unchanged set, a client unregistering itself or another under a callback, a walk under a callback, the dead wrappers freed when the last walk ends; the control interface of a part that stands for a KS node (dll/win32/mmdevapi/devicetopology_control.h): the request the driver sees through a fake KS channel and which parts stand for a node; under ASan and UBSan
 * COPYRIGHT:   Copyright 2026 LiberNT contributors
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stddef.h>
#include <wine/list.h>

/* What devenum.c's headers give the unit: the types and the constants it names */
typedef struct _GUID
{
    unsigned int Data1;
    unsigned short Data2, Data3;
    unsigned char Data4[8];
} GUID;
typedef unsigned int DWORD;
typedef unsigned int UINT;
typedef unsigned int UINT32;
typedef unsigned char BOOLEAN;
#define TRUE   1
#define FALSE  0
#define DEVICE_STATE_ACTIVE      0x1
#define DEVICE_STATE_UNPLUGGED    0x2
#define DEVICE_STATE_NOTPRESENT   0x4

static
int
IsEqualGUID(
    const GUID* a,
    const GUID* b)
{
    return memcmp(a, b, sizeof(*a)) == 0;
}

#include "devenum_notif.h"

static int Checks;
static int Failures;

#define CHECK(cond) \
    do { \
        ++Checks; \
        if (!(cond)) { \
            fprintf(stderr, "%s:%d: FAIL: %s\n", __FILE__, __LINE__, #cond); \
            ++Failures; \
        } \
    } while (0)

#define CHECK_EQ(a, b) \
    do { \
        long long _a = (long long)(a), _b = (long long)(b); \
        ++Checks; \
        if (_a != _b) { \
            fprintf(stderr, "%s:%d: FAIL: %s == %s: %lld != %lld\n", __FILE__, __LINE__, #a, #b, _a, _b); \
            ++Failures; \
        } \
    } while (0)

#define DEVICES    8
#define CALLS      64

/* A device of the set: its id, its state, whether the answer names it; its index is its name */
typedef struct _DEVICE
{
    GUID id;
    DWORD state;
    BOOLEAN named;
} DEVICE;

typedef struct _SET
{
    DEVICE devices[DEVICES];
    UINT count;
    UINT at;
} SET;

/* One change told to a client: the kind, the device's index, the state it carries (devenum.c's notif_change_args) */
typedef struct _TELL
{
    enum notif_change change;
    int device;
    DWORD state;
} TELL;

/* What a client does in a callback */
enum action
{
    ACTION_NONE,
    ACTION_UNREGISTER_SELF,   /* in its first callback */
    ACTION_UNREGISTER_OTHER,  /* in its first callback */
    ACTION_NESTED_WALK,       /* in its first callback: another walk of the same clients */
    ACTION_NESTED_UNREGISTER, /* under the nested walk: unregister the other */
    ACTION_REGISTER_NEW       /* in its first callback */
};

typedef struct _FAKE_CLIENT
{
    const char* name;
    TELL calls[CALLS];
    UINT count;
    /* how deep the walks were at each call: the outermost walk is 1 */
    UINT32 depths[CALLS];
    enum action action;
    struct notif_clients* clients;
    struct _FAKE_CLIENT* other;
    void* new_client;
    /* what the nested walk a callback starts tells */
    TELL nested;
} FAKE_CLIENT;

/* The diff's run over the device set: the context both of its callbacks take */
typedef struct _DIFF_CTX
{
    struct notif_clients* clients;
    SET* set;
} DIFF_CTX;

/* The diff's Next over the device set: the list's order is the array's */
static
BOOLEAN
set_next(
    void* context,
    struct notif_after* after)
{
    DIFF_CTX* ctx = context;
    SET* set = ctx->set;

    if (set->at >= set->count)
        return FALSE;
    after->id = &set->devices[set->at].id;
    after->state = set->devices[set->at].state;
    after->named = set->devices[set->at].named;
    after->device = &set->devices[set->at];
    set->at++;
    return TRUE;
}

static
void
fake_call(
    void* client,
    void* context);

/* The diff's Tell as devenum.c's refresh_tell is: the verdict goes to every client, and a removed device is not present from now on */
static
void
set_tell(
    enum notif_change change,
    struct notif_after* after,
    void* context)
{
    DIFF_CTX* ctx = context;
    DEVICE* device = after->device;
    TELL tell;

    if (change == NOTIF_REMOVED)
        device->state = DEVICE_STATE_NOTPRESENT;
    tell.change = change;
    tell.device = (int)(device - ctx->set->devices);
    tell.state = device->state;
    notif_clients_walk(ctx->clients, fake_call, &tell);
}

static
void
fake_call(
    void* client,
    void* context)
{
    FAKE_CLIENT* self = client;
    TELL* tell = context;

    CHECK(self->count < CALLS);
    if (self->count >= CALLS)
        return;
    self->calls[self->count] = *tell;
    self->depths[self->count] = self->clients->walks;
    self->count++;
    switch (self->action)
    {
        case ACTION_UNREGISTER_SELF:
            if (self->count == 1)
                CHECK(notif_clients_unregister(self->clients, self));
            break;
        case ACTION_UNREGISTER_OTHER:
            if (self->count == 1)
                CHECK(notif_clients_unregister(self->clients, self->other));
            break;
        case ACTION_NESTED_WALK:
            if (self->count == 1)
            {
                self->action = ACTION_NESTED_UNREGISTER;
                notif_clients_walk(self->clients, fake_call, &self->nested);
            }
            break;
        case ACTION_NESTED_UNREGISTER:
            /* under the nested walk: the other goes */
            CHECK(notif_clients_unregister(self->clients, self->other));
            self->action = ACTION_NONE;
            break;
        case ACTION_REGISTER_NEW:
            if (self->count == 1)
                CHECK(notif_clients_register(self->clients, self->new_client) != NULL);
            break;
        case ACTION_NONE:
            break;
    }
}

static
void
reset(
    FAKE_CLIENT* client,
    const char* name)
{
    memset(client, 0, sizeof(*client));
    client->name = name;
}

/* The name of a kind of call, for the lines */
static
const char*
kind_name(
    enum notif_change change)
{
    switch (change)
    {
        case NOTIF_DEFAULT: return "default";
        case NOTIF_UNCHANGED: return "unchanged";
        case NOTIF_REMOVED: return "removed";
        case NOTIF_ADDED: return "added";
        case NOTIF_STATE: return "state";
    }
    return "?";
}

static
void
check_call(
    const FAKE_CLIENT* client,
    UINT index,
    enum notif_change change,
    int device)
{
    if (index >= client->count)
    {
        fprintf(stderr, "%s: call %u missing (%s of device %d), %u calls\n", client->name, index, kind_name(change), device, client->count);
        CHECK(index < client->count);
        return;
    }
    if (client->calls[index].change != change || client->calls[index].device != device)
        fprintf(stderr, "%s: call %u: %s of device %d, not %s of device %d\n", client->name, index,
                kind_name(client->calls[index].change), client->calls[index].device, kind_name(change), device);
    CHECK_EQ(client->calls[index].change, change);
    CHECK_EQ(client->calls[index].device, device);
}

/* The diff's verdict on every combination of what a device was and what the answer says of it */
static
void
check_diff_table(
    void)
{
    static const DWORD olds[] = { NOTIF_ABSENT, DEVICE_STATE_ACTIVE, DEVICE_STATE_UNPLUGGED, DEVICE_STATE_NOTPRESENT };
    static const DWORD states[] = { DEVICE_STATE_ACTIVE, DEVICE_STATE_UNPLUGGED, DEVICE_STATE_NOTPRESENT };
    UINT o, s, n;
    enum notif_change expect;

    for (o = 0; o < 4; o++)
        for (s = 0; s < 3; s++)
            for (n = 0; n < 2; n++)
            {
                if (!n)
                    expect = o == 0 || o == 3 ? NOTIF_UNCHANGED : NOTIF_REMOVED;
                else if (s == 2)
                    expect = NOTIF_UNCHANGED;
                else if (o == 0 || o == 3)
                    expect = NOTIF_ADDED;
                else
                    expect = olds[o] != states[s] ? NOTIF_STATE : NOTIF_UNCHANGED;
                if (notif_diff(olds[o], states[s], (BOOLEAN)n) != expect)
                    fprintf(stderr, "diff(old %u, state %u, named %u)\n", olds[o], states[s], n);
                CHECK_EQ(notif_diff(olds[o], states[s], (BOOLEAN)n), expect);
            }
}

/* (m1) The three device callbacks on every registered client, each device once, in the list's order, and none for an unchanged set; the lookup of the snapshot */
static
void
check_device_diff(
    void)
{
    struct notif_clients clients;
    FAKE_CLIENT first, second;
    SET set;
    DIFF_CTX ctx;
    struct notif_before before[DEVICES];
    UINT count = 0, i;

    notif_clients_init(&clients);
    reset(&first, "first");
    reset(&second, "second");
    first.clients = second.clients = &clients;
    CHECK(notif_clients_register(&clients, &first) != NULL);
    CHECK(notif_clients_register(&clients, &second) != NULL);
    /* the devices' ids: one apart in the last byte */
    memset(&set, 0, sizeof(set));
    ctx.clients = &clients;
    ctx.set = &set;
    set.count = 6;
    for (i = 0; i < set.count; i++)
        set.devices[i].id.Data4[7] = (unsigned char)(i + 1);
    /* the snapshot: A active, B active, C not present, E active; D and F not there */
    before[count].id = set.devices[0].id; before[count++].state = DEVICE_STATE_ACTIVE;
    before[count].id = set.devices[1].id; before[count++].state = DEVICE_STATE_ACTIVE;
    before[count].id = set.devices[2].id; before[count++].state = DEVICE_STATE_NOTPRESENT;
    before[count].id = set.devices[4].id; before[count++].state = DEVICE_STATE_ACTIVE;
    CHECK_EQ(count, 4);
    /* the answer: A named active (unchanged), B not named (removed), C named active (added back), D named active (new), E named unplugged (changed), F not named not present (nothing: it was not there) */
    set.devices[0].named = TRUE; set.devices[0].state = DEVICE_STATE_ACTIVE;
    set.devices[1].named = FALSE; set.devices[1].state = DEVICE_STATE_ACTIVE;
    set.devices[2].named = TRUE; set.devices[2].state = DEVICE_STATE_ACTIVE;
    set.devices[3].named = TRUE; set.devices[3].state = DEVICE_STATE_ACTIVE;
    set.devices[4].named = TRUE; set.devices[4].state = DEVICE_STATE_UNPLUGGED;
    set.devices[5].named = FALSE; set.devices[5].state = DEVICE_STATE_NOTPRESENT;
    /* the snapshot's lookup */
    CHECK_EQ(notif_before_state(before, count, &set.devices[1].id), DEVICE_STATE_ACTIVE);
    CHECK_EQ(notif_before_state(before, count, &set.devices[2].id), DEVICE_STATE_NOTPRESENT);
    CHECK_EQ(notif_before_state(before, count, &set.devices[3].id), NOTIF_ABSENT);
    CHECK_EQ(notif_before_state(before, 0, &set.devices[0].id), NOTIF_ABSENT);
    /* the diff run with devenum.c's Tell: the walks of the clients, and a removed device marked not present */
    set.at = 0;
    notif_diff_run(before, count, set_next, set_tell, &ctx);
    /* each client: removed B, added C, added D, state E: exactly four calls, in the list's order */
    CHECK_EQ(first.count, 4);
    CHECK_EQ(second.count, 4);
    check_call(&first, 0, NOTIF_REMOVED, 1);
    check_call(&first, 1, NOTIF_ADDED, 2);
    check_call(&first, 2, NOTIF_ADDED, 3);
    check_call(&first, 3, NOTIF_STATE, 4);
    check_call(&second, 0, NOTIF_REMOVED, 1);
    check_call(&second, 1, NOTIF_ADDED, 2);
    check_call(&second, 2, NOTIF_ADDED, 3);
    check_call(&second, 3, NOTIF_STATE, 4);
    /* the state a state call carries is the new one, and a removed device is not present from now on; the others keep theirs */
    CHECK_EQ(first.calls[3].state, DEVICE_STATE_UNPLUGGED);
    CHECK_EQ(first.calls[0].state, DEVICE_STATE_NOTPRESENT);
    CHECK_EQ(set.devices[1].state, DEVICE_STATE_NOTPRESENT);
    CHECK_EQ(set.devices[0].state, DEVICE_STATE_ACTIVE);
    CHECK_EQ(set.devices[3].state, DEVICE_STATE_ACTIVE);
    CHECK_EQ(set.devices[5].state, DEVICE_STATE_NOTPRESENT);
    /* an unchanged answer tells nothing: the snapshot taken again of the set as it is now, the answer the same */
    count = 0;
    for (i = 0; i < set.count; i++)
    {
        if (!set.devices[i].named && set.devices[i].state != DEVICE_STATE_NOTPRESENT)
            continue;
        before[count].id = set.devices[i].id;
        before[count++].state = set.devices[i].state;
    }
    first.count = second.count = 0;
    set.at = 0;
    notif_diff_run(before, count, set_next, set_tell, &ctx);
    CHECK_EQ(first.count, 0);
    CHECK_EQ(second.count, 0);
    /* a device the answer names as not present tells nothing, added or removed alike */
    set.devices[5].named = TRUE;
    first.count = second.count = 0;
    set.at = 0;
    notif_diff_run(before, count, set_next, set_tell, &ctx);
    CHECK_EQ(first.count, 0);
    CHECK_EQ(second.count, 0);
    /* the clients let go: nothing stays in the list */
    CHECK(notif_clients_unregister(&clients, &first));
    CHECK(notif_clients_unregister(&clients, &second));
    CHECK_EQ(list_count(&clients.clients), 0);
    CHECK(notif_clients_unregister(&clients, &first) == FALSE);
    CHECK_EQ(clients.walks, 0);
    check_diff_table();
}

/* (m2) A client that unregisters itself under a callback: it is not called again, by the walk that called it or by one after, and its wrapper is freed when the walk ends; a second unregistration finds nothing */
static
void
check_unregister_self(
    void)
{
    struct notif_clients clients;
    FAKE_CLIENT self, next, tell;

    notif_clients_init(&clients);
    reset(&self, "self");
    reset(&next, "next");
    reset(&tell, "tell");
    self.clients = next.clients = &clients;
    self.action = ACTION_UNREGISTER_SELF;
    CHECK(notif_clients_register(&clients, &self) != NULL);
    CHECK(notif_clients_register(&clients, &next) != NULL);
    notif_clients_walk(&clients, fake_call, &tell);
    /* the walk called it once, then the next client; the wrapper is gone with the walk, and the client is not registered any more */
    CHECK_EQ(self.count, 1);
    CHECK_EQ(next.count, 1);
    CHECK_EQ(self.depths[0], 1);
    CHECK_EQ(list_count(&clients.clients), 1);
    CHECK(notif_clients_unregister(&clients, &self) == FALSE);
    /* the next walk tells the next client only */
    notif_clients_walk(&clients, fake_call, &tell);
    CHECK_EQ(self.count, 1);
    CHECK_EQ(next.count, 2);
    CHECK(notif_clients_unregister(&clients, &next));
    CHECK_EQ(list_count(&clients.clients), 0);
}

/* (m3) A client that unregisters another under its callback: the other is not called afterwards, in that walk or in one after, and its wrapper is freed when the walk ends */
static
void
check_unregister_other(
    void)
{
    struct notif_clients clients;
    FAKE_CLIENT first, second, third, tell;

    notif_clients_init(&clients);
    reset(&first, "first");
    reset(&second, "second");
    reset(&third, "third");
    reset(&tell, "tell");
    first.clients = second.clients = third.clients = &clients;
    first.action = ACTION_UNREGISTER_OTHER;
    first.other = &second;
    CHECK(notif_clients_register(&clients, &first) != NULL);
    CHECK(notif_clients_register(&clients, &second) != NULL);
    CHECK(notif_clients_register(&clients, &third) != NULL);
    notif_clients_walk(&clients, fake_call, &tell);
    /* first called (and unregistered second), second skipped, third called */
    CHECK_EQ(first.count, 1);
    CHECK_EQ(second.count, 0);
    CHECK_EQ(third.count, 1);
    CHECK_EQ(list_count(&clients.clients), 2);
    CHECK(notif_clients_unregister(&clients, &second) == FALSE);
    notif_clients_walk(&clients, fake_call, &tell);
    CHECK_EQ(first.count, 2);
    CHECK_EQ(second.count, 0);
    CHECK_EQ(third.count, 2);
    CHECK(notif_clients_unregister(&clients, &first));
    CHECK(notif_clients_unregister(&clients, &third));
    CHECK_EQ(list_count(&clients.clients), 0);
}

/* (m4) A walk under a callback: the clients are told again, the walk in it counted as a walk, and a client it unregisters is not called by the outer walk after, the outer walk holding that very client as its next when the nested walk unregisters it; the dead wrapper is freed when the outer walk ends, not before */
static
void
check_nested_walk(
    void)
{
    struct notif_clients clients;
    FAKE_CLIENT first, second, third, tell;

    notif_clients_init(&clients);
    reset(&first, "first");
    reset(&second, "second");
    reset(&third, "third");
    reset(&tell, "tell");
    first.clients = second.clients = third.clients = &clients;
    /* the second's first callback starts the nested walk; in it, called again, it unregisters the third, which the outer walk has as its next */
    second.action = ACTION_NESTED_WALK;
    second.other = &third;
    second.nested.change = NOTIF_STATE;
    second.nested.device = 7;
    CHECK(notif_clients_register(&clients, &first) != NULL);
    CHECK(notif_clients_register(&clients, &second) != NULL);
    CHECK(notif_clients_register(&clients, &third) != NULL);
    notif_clients_walk(&clients, fake_call, &tell);
    /* the outer walk: first, then second, whose callback starts the nested walk: first again, second again (and the third is unregistered there), the third skipped; then the outer walk goes on to the third, dead, and skips it */
    CHECK_EQ(first.count, 2);
    CHECK_EQ(second.count, 2);
    CHECK_EQ(third.count, 0);
    CHECK_EQ(first.depths[0], 1);
    CHECK_EQ(first.depths[1], 2);
    CHECK_EQ(second.depths[0], 1);
    CHECK_EQ(second.depths[1], 2);
    CHECK_EQ(second.calls[1].change, NOTIF_STATE);
    CHECK_EQ(second.calls[1].device, 7);
    /* the third is gone with the outer walk's end; the first and the second remain */
    CHECK_EQ(list_count(&clients.clients), 2);
    CHECK(notif_clients_unregister(&clients, &third) == FALSE);
    CHECK_EQ(clients.walks, 0);
    notif_clients_walk(&clients, fake_call, &tell);
    CHECK_EQ(first.count, 3);
    CHECK_EQ(second.count, 3);
    CHECK_EQ(third.count, 0);
    CHECK(notif_clients_unregister(&clients, &first));
    CHECK(notif_clients_unregister(&clients, &second));
    CHECK_EQ(list_count(&clients.clients), 0);
}

/* (m5) A client registered under a walk is at the end of the list, where the walk reaches it; a client let go outside any walk is freed at once; the static initializer is an empty, unwalked list */
static
void
check_register_under_walk(
    void)
{
    struct notif_clients clients;
    static struct notif_clients seeded = NOTIF_CLIENTS_INIT(seeded);
    FAKE_CLIENT first, second, fresh, tell;

    notif_clients_init(&clients);
    reset(&first, "first");
    reset(&second, "second");
    reset(&fresh, "fresh");
    reset(&tell, "tell");
    first.clients = second.clients = fresh.clients = &clients;
    first.action = ACTION_REGISTER_NEW;
    first.new_client = &fresh;
    CHECK(notif_clients_register(&clients, &first) != NULL);
    CHECK(notif_clients_register(&clients, &second) != NULL);
    notif_clients_walk(&clients, fake_call, &tell);
    CHECK_EQ(first.count, 1);
    CHECK_EQ(second.count, 1);
    CHECK_EQ(fresh.count, 1);
    CHECK_EQ(fresh.depths[0], 1);
    CHECK_EQ(list_count(&clients.clients), 3);
    /* outside a walk the unregistration frees at once */
    CHECK(notif_clients_unregister(&clients, &fresh));
    CHECK_EQ(list_count(&clients.clients), 2);
    CHECK(notif_clients_unregister(&clients, &first));
    CHECK(notif_clients_unregister(&clients, &second));
    CHECK_EQ(list_count(&clients.clients), 0);
    CHECK_EQ(list_count(&seeded.clients), 0);
    CHECK_EQ(seeded.walks, 0);
}

/* ---- The control interface of a part (devicetopology_control.h) ---- */

/* What ks.h and the Windows headers give the unit: HRESULT, BOOL, the KS request headers as ks.h lays them out
 * on amd64 (ULONG 32 bits), the IOCTLs of the four KS requests, S_OK and E_INVALIDARG */
typedef long HRESULT;
typedef int BOOL;
typedef unsigned int ULONG;
typedef void *HANDLE;
#define ZeroMemory(p, n) memset((p), 0, (n))
typedef struct _KSPROPERTY
{
    GUID Set;
    ULONG Id;
    ULONG Flags;
} KSPROPERTY;
typedef struct _KSMETHOD
{
    GUID Set;
    ULONG Id;
    ULONG Flags;
} KSMETHOD;
typedef struct _KSEVENT
{
    GUID Set;
    ULONG Id;
    ULONG Flags;
} KSEVENT;
typedef struct _KSP_NODE
{
    KSPROPERTY Property;
    ULONG NodeId;
    ULONG Reserved;
} KSP_NODE;
typedef struct _KSM_NODE
{
    KSMETHOD Method;
    ULONG NodeID;
    ULONG Reserved;
} KSM_NODE;
typedef struct _KSE_NODE
{
    KSEVENT Event;
    ULONG NodeId;
    ULONG Reserved;
} KSE_NODE;
#define FILE_DEVICE_KS 0x47
#define METHOD_NEITHER 3
#define FILE_ANY_ACCESS 0
#define CTL_CODE(d, f, m, a) (((d) << 16) | ((a) << 14) | ((f) << 2) | (m))
#define IOCTL_KS_PROPERTY CTL_CODE(FILE_DEVICE_KS, 0x000, METHOD_NEITHER, FILE_ANY_ACCESS)
#define IOCTL_KS_ENABLE_EVENT CTL_CODE(FILE_DEVICE_KS, 0x001, METHOD_NEITHER, FILE_ANY_ACCESS)
#define IOCTL_KS_DISABLE_EVENT CTL_CODE(FILE_DEVICE_KS, 0x002, METHOD_NEITHER, FILE_ANY_ACCESS)
#define IOCTL_KS_METHOD CTL_CODE(FILE_DEVICE_KS, 0x003, METHOD_NEITHER, FILE_ANY_ACCESS)
#define S_OK 0
#define E_INVALIDARG ((HRESULT)0x80070057)
/* the node's flag of each request, as ks.h defines it */
#define KSPROPERTY_TYPE_TOPOLOGY 0x10000000
#define KSMETHOD_TYPE_TOPOLOGY   0x10000000
#define KSEVENT_TYPE_TOPOLOGY    0x10000000

#include "devicetopology_control.h"

/* The property set of the audio nodes, a value of the test's own (KSPROPSETID_Audio) */
static const GUID AudioPropertySet = {0x45b0c0a3, 0x8ede, 0x11d0, {0xa0, 0x8c, 0x00, 0xa0, 0xc9, 0x22, 0x31, 0x96}};
#define NODE 3
#define GET 0x1

/*
 * A fake KS channel: the driver's side of the IOCTL. The handle is the fake itself; every request is
 * recorded as it arrives (the code, the request's bytes, the data buffer) and answered as told.
 */
#define REQUEST_BYTES 64
typedef struct _FAKE_CHANNEL
{
    struct ks_channel channel;
    int calls;
    DWORD code;
    unsigned char request[REQUEST_BYTES];
    ULONG request_size;
    void *data;
    ULONG data_size;
    HRESULT result;
    ULONG returned;
} FAKE_CHANNEL;

static
void
fake_channel_init(
    FAKE_CHANNEL *fake);

static
HRESULT
fake_request(
    HANDLE filter,
    DWORD code,
    const void *in,
    ULONG in_size,
    void *out,
    ULONG out_size,
    ULONG *returned)
{
    FAKE_CHANNEL *fake = (FAKE_CHANNEL *)filter;

    ++fake->calls;
    fake->code = code;
    fake->request_size = in_size;
    memset(fake->request, 0, sizeof(fake->request));
    if (in_size)
        memcpy(fake->request, in, in_size < sizeof(fake->request) ? in_size : sizeof(fake->request));
    fake->data = out;
    fake->data_size = out_size;
    *returned = fake->returned;
    return fake->result;
}

static
void
fake_channel_init(
    FAKE_CHANNEL *fake)
{
    memset(fake, 0, sizeof(*fake));
    fake->channel.filter = fake;
    fake->channel.request = fake_request;
}

/* What the driver sees of a request: its code, its bytes, the data buffer and length */
#define CHECK_REQUEST(fake, code_, size_, data_, data_size_) \
    do { \
        CHECK_EQ((fake)->calls, 1); \
        CHECK_EQ((fake)->code, (DWORD)(code_)); \
        CHECK_EQ((fake)->request_size, (size_)); \
        CHECK((fake)->data == (data_)); \
        CHECK_EQ((fake)->data_size, (data_size_)); \
    } while (0)

/* A property of the node: a KSP_NODE of the part's node, the caller's header with the node's flag and the data as the driver takes them */
static
void
check_node_property(
    void)
{
    FAKE_CHANNEL fake;
    KSPROPERTY property;
    KSP_NODE callers, seen;
    ULONG returned, level = 0;
    HRESULT hr;

    fake_channel_init(&fake);
    fake.returned = sizeof(level);
    memset(&property, 0, sizeof(property));
    property.Set = AudioPropertySet;
    property.Id = 7;
    property.Flags = GET;
    hr = ks_node_property(&fake.channel, NODE, &property, sizeof(property), &level, sizeof(level), &returned);
    CHECK_EQ(hr, S_OK);
    CHECK_REQUEST(&fake, IOCTL_KS_PROPERTY, sizeof(KSP_NODE), &level, sizeof(level));
    memcpy(&seen, fake.request, sizeof(seen));
    CHECK(IsEqualGUID(&seen.Property.Set, &AudioPropertySet));
    CHECK_EQ(seen.Property.Id, 7);
    CHECK_EQ(seen.Property.Flags, GET | KSPROPERTY_TYPE_TOPOLOGY);
    CHECK_EQ(seen.NodeId, NODE);
    CHECK_EQ(seen.Reserved, 0);
    CHECK_EQ(returned, sizeof(level));

    /* a caller who set the node's flag is answered the same one flag */
    fake_channel_init(&fake);
    property.Flags = GET | KSPROPERTY_TYPE_TOPOLOGY;
    hr = ks_node_property(&fake.channel, NODE, &property, sizeof(property), &level, sizeof(level), &returned);
    CHECK_EQ(hr, S_OK);
    memcpy(&seen, fake.request, sizeof(seen));
    CHECK_EQ(seen.Property.Flags, GET | KSPROPERTY_TYPE_TOPOLOGY);
    property.Flags = GET;

    /* the caller's buffer is a KSP_NODE naming another node and a reserved word: the driver sees the part's node and no reserved word */
    fake_channel_init(&fake);
    memset(&callers, 0, sizeof(callers));
    callers.Property = property;
    callers.NodeId = 0x00020000u | NODE;
    callers.Reserved = 0xdeadbeef;
    hr = ks_node_property(&fake.channel, NODE, &callers.Property, sizeof(callers), &level, sizeof(level), &returned);
    CHECK_EQ(hr, S_OK);
    CHECK_REQUEST(&fake, IOCTL_KS_PROPERTY, sizeof(KSP_NODE), &level, sizeof(level));
    memcpy(&seen, fake.request, sizeof(seen));
    CHECK_EQ(seen.NodeId, NODE);
    CHECK_EQ(seen.Reserved, 0);

    /* the driver's failure and the bytes it reports are the caller's */
    fake_channel_init(&fake);
    fake.result = (HRESULT)0x80070490;
    fake.returned = 4;
    returned = 0;
    hr = ks_node_property(&fake.channel, NODE, &property, sizeof(property), NULL, 0, &returned);
    CHECK_EQ(hr, (HRESULT)0x80070490);
    CHECK_EQ(returned, 4);
    CHECK_REQUEST(&fake, IOCTL_KS_PROPERTY, sizeof(KSP_NODE), NULL, 0);

    /* a header too short to be a property, or none, is not the driver's to judge */
    fake_channel_init(&fake);
    CHECK_EQ(ks_node_property(&fake.channel, NODE, &property, sizeof(property) - 1, &level, sizeof(level), &returned), E_INVALIDARG);
    CHECK_EQ(ks_node_property(&fake.channel, NODE, NULL, sizeof(property), &level, sizeof(level), &returned), E_INVALIDARG);
    CHECK_EQ(fake.calls, 0);
}

/* A method of the node: a KSM_NODE through IOCTL_KS_METHOD */
static
void
check_node_method(
    void)
{
    FAKE_CHANNEL fake;
    KSMETHOD method;
    KSM_NODE seen;
    ULONG returned = 0, value = 0;
    HRESULT hr;

    fake_channel_init(&fake);
    fake.returned = sizeof(value);
    memset(&method, 0, sizeof(method));
    method.Set = AudioPropertySet;
    method.Id = 9;
    method.Flags = 0x3;
    hr = ks_node_method(&fake.channel, NODE, &method, sizeof(method), &value, sizeof(value), &returned);
    CHECK_EQ(hr, S_OK);
    CHECK_REQUEST(&fake, IOCTL_KS_METHOD, sizeof(KSM_NODE), &value, sizeof(value));
    memcpy(&seen, fake.request, sizeof(seen));
    CHECK(IsEqualGUID(&seen.Method.Set, &AudioPropertySet));
    CHECK_EQ(seen.Method.Id, 9);
    CHECK_EQ(seen.Method.Flags, 0x3 | KSMETHOD_TYPE_TOPOLOGY);
    CHECK_EQ(seen.NodeID, NODE);
    CHECK_EQ(seen.Reserved, 0);
    CHECK_EQ(returned, sizeof(value));

    fake_channel_init(&fake);
    CHECK_EQ(ks_node_method(&fake.channel, NODE, &method, sizeof(method) - 1, &value, sizeof(value), &returned), E_INVALIDARG);
    CHECK_EQ(fake.calls, 0);
}

/* An event of the node: enabled as a KSE_NODE, disabled by the data alone */
static
void
check_node_event(
    void)
{
    FAKE_CHANNEL fake;
    KSEVENT event;
    KSE_NODE seen;
    unsigned char data[8] = {1, 2, 3, 4, 5, 6, 7, 8};
    ULONG returned = 0;
    HRESULT hr;

    fake_channel_init(&fake);
    memset(&event, 0, sizeof(event));
    event.Set = AudioPropertySet;
    event.Id = 5;
    event.Flags = 0x1;
    hr = ks_node_event(&fake.channel, NODE, &event, sizeof(event), data, sizeof(data), &returned);
    CHECK_EQ(hr, S_OK);
    CHECK_REQUEST(&fake, IOCTL_KS_ENABLE_EVENT, sizeof(KSE_NODE), data, sizeof(data));
    memcpy(&seen, fake.request, sizeof(seen));
    CHECK(IsEqualGUID(&seen.Event.Set, &AudioPropertySet));
    CHECK_EQ(seen.Event.Id, 5);
    CHECK_EQ(seen.Event.Flags, 0x1 | KSEVENT_TYPE_TOPOLOGY);
    CHECK_EQ(seen.NodeId, NODE);
    CHECK_EQ(seen.Reserved, 0);

    /* no event: the data is the request of the disabling, nothing is answered */
    fake_channel_init(&fake);
    hr = ks_node_event(&fake.channel, NODE, NULL, 0, data, sizeof(data), &returned);
    CHECK_EQ(hr, S_OK);
    CHECK_EQ(fake.calls, 1);
    CHECK_EQ(fake.code, (DWORD)IOCTL_KS_DISABLE_EVENT);
    CHECK_EQ(fake.request_size, sizeof(data));
    CHECK(memcmp(fake.request, data, sizeof(data)) == 0);
    CHECK(fake.data == NULL);
    CHECK_EQ(fake.data_size, 0);

    /* an event too short is refused before the driver */
    fake_channel_init(&fake);
    CHECK_EQ(ks_node_event(&fake.channel, NODE, &event, sizeof(event) - 1, data, sizeof(data), &returned), E_INVALIDARG);
    CHECK_EQ(fake.calls, 0);
}

/* Which parts stand for a node: a subunit of a filter's topology, by its index; a connector, a part of an
 * endpoint's own topology (no path) and an id of no kind stand for none */
static
void
check_node_parts(
    void)
{
    ULONG node = ~0u;
    static const char path[] = "filter interface path";

    CHECK(part_node_id(PART_ID(PART_KIND_SUBUNIT, NODE), path, &node));
    CHECK_EQ(node, NODE);
    CHECK(part_node_id(PART_ID(PART_KIND_SUBUNIT, 0), path, &node));
    CHECK_EQ(node, 0);
    node = ~0u;
    CHECK(!part_node_id(PART_ID(PART_KIND_CONNECTOR, NODE), path, &node));
    CHECK(!part_node_id(PART_ID(PART_KIND_SUBUNIT, NODE), NULL, &node));
    CHECK(!part_node_id(0, path, &node));
    CHECK_EQ(node, ~0u);
}

int
main(
    void)
{
    check_device_diff();
    check_unregister_self();
    check_unregister_other();
    check_nested_walk();
    check_register_under_walk();
    check_node_property();
    check_node_method();
    check_node_event();
    check_node_parts();

    printf("mmdevapi: %d checks, %d failures\n", Checks, Failures);
    return Failures != 0;
}
