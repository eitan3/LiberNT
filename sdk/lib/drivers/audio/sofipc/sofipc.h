/*
 * PROJECT:     LiberNT audio driver libraries
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Carries IPC4 requests to a booted ACE-class audio DSP and their replies back: one request at a time, a budget per message class, a poll for every reply, every payload through the mailbox in blocks, and a link that fails closed on a timeout, a bad reply, a firmware status or a crash notification; free of I/O
 * COPYRIGHT:   Copyright 2026 LiberNT contributors
 */

/*
 * The register protocol is the loader's (fwboot.c): the request extension, then the primary with its busy bit;
 * a reply is a done bit in the reply register and a notification a busy bit in the target register, each consumed
 * before it is acknowledged. The transport does not re-express it: it calls the loader's receive, acknowledge and
 * interrupt-enable routines, so the boot and the transport cannot disagree about their order.
 * The large-configuration block rules (the first block carries the total size with the first flag, later blocks
 * their offset, the last one the last flag; a read's first reply reports the total) are the behaviour of Linux v6.12
 * sound/soc/sof/ipc4.c (sof_ipc4_set_get_data), and the extension layout of the large-configuration message and
 * its reply is SOF src/include/ipc4/module.h (struct ipc4_module_large_config, struct ipc4_module_large_config_reply)
 * at fcebbbf22d7c08159fbb8486d5f3687da4f167c3. Both were studied under LICENSING.md rule 7: the code, names and
 * comments are ours, no text was copied.
 */

#ifndef _AUDIO_SOFIPC_H_
#define _AUDIO_SOFIPC_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <fwload.h>

/* A driver choice, not a hardware constant: the first millisecond of a wait is polled every 20 microseconds, so that a firmware answering within that time (the model answers in 50 to 200 microseconds) costs neither a sleep nor a dependence on the timer resolution; every later poll sleeps */
#define SOFIPC_FAST_POLL_US             20
#define SOFIPC_FAST_PHASE_US            1000
#define SOFIPC_SLOW_POLL_US             500
/* A wait whose clock does not advance would never reach its budget: after this many times the fast polls the budget needs, it is reported as a stalled clock */
#define SOFIPC_STALLED_POLL_FACTOR      2
/* The budget of every message class, IpcTimeoutMs of the plan's settings table; it is the boot's reply budget (FWLOAD_TIMEOUT_IPC_REPLY_US) */
#define SOFIPC_DEFAULT_TIMEOUT_MS       500
/* Notifications whose headers are kept for the log */
#define SOFIPC_NOTIFICATION_RING        16
/* No message type matches the faults that name one */
#define SOFIPC_NO_TYPE                  0xFFFFFFFFUL

/* The message classes and their budgets: the pipeline messages, the module messages and one block of a large configuration */
typedef enum _SOFIPC_CLASS
{
    SofIpcClassPipeline = 0,
    SofIpcClassModule,
    SofIpcClassLargeConfig,
    SofIpcClassCount
} SOFIPC_CLASS;

/* The link of the plan's failure policy: offline until a boot, ready between requests, busy inside one, faulted until the DSP is powered down */
typedef enum _SOFIPC_LINK
{
    SofIpcLinkOffline = 0,
    SofIpcLinkReady,
    SofIpcLinkBusy,
    SofIpcLinkFaulted
} SOFIPC_LINK;

typedef enum _SOFIPC_STATUS
{
    SofIpcOk = 0,
    SofIpcBadState,
    SofIpcBusy,
    SofIpcBadLength,
    SofIpcTimeout,
    SofIpcClockStalled,
    SofIpcBadReply,
    SofIpcFwStatus,
    SofIpcCrash,
    SofIpcInterruptStorm,
    SofIpcHardwareFault
} SOFIPC_STATUS;

/* One exchange as it ended, for the caller's log line: the request as sent, the reply as it came, the firmware's status and the time the whole exchange took */
typedef struct _SOFIPC_EXCHANGE
{
    UINT32 Type;
    UINT32 RequestPrimary;
    UINT32 RequestExtension;
    UINT32 ReplyPrimary;
    UINT32 ReplyExtension;
    UINT32 Status;
    UINT64 ElapsedUs;
    SOFIPC_STATUS Outcome;
} SOFIPC_EXCHANGE, *PSOFIPC_EXCHANGE;

typedef struct _SOFIPC_NOTIFICATION
{
    UINT32 Type;
    UINT32 Primary;
    UINT32 Extension;
} SOFIPC_NOTIFICATION, *PSOFIPC_NOTIFICATION;

/*
 * What the loader's operations table lacks: the downlink mailbox, which the boot never writes, and the request
 * lock. Every register access, the uplink mailbox, the clock, the delays and the interrupt lock are the loader's
 * own operations, reached through the loader context the transport hangs off. RequestLock and RequestUnlock are
 * optional: without them the caller serializes the requests itself, and a request that re-enters a context whose
 * exchange is still running fails busy at once. With them, RequestLock waits up to its timeout for the lock of
 * the only request the mailbox can carry and says whether it got it.
 */
typedef struct _SOFIPC_OPS
{
    BOOLEAN (*WriteMemory)(_Inout_ PVOID Context, UINT32 Offset, _In_reads_bytes_(Bytes) const UINT8* Buffer, SIZE_T Bytes);
    BOOLEAN (*RequestLock)(_Inout_ PVOID Context, UINT32 TimeoutUs);
    VOID (*RequestUnlock)(_Inout_ PVOID Context);
} SOFIPC_OPS, *PSOFIPC_OPS;

/* One request: the class that bounds it, the header as the codec built it (the busy bit is the transport's to set), the payload written into the downlink mailbox before the header, and the bytes of the uplink mailbox read into ReplyBuffer before the reply is acknowledged */
typedef struct _SOFIPC_REQUEST
{
    SOFIPC_CLASS Class;
    SOFIPC4_MSG Msg;
    const UINT8* Payload;
    SIZE_T PayloadBytes;
    UINT8* ReplyBuffer;
    SIZE_T ReplyBytes;
} SOFIPC_REQUEST, *PSOFIPC_REQUEST;

typedef struct _SOFIPC_CONTEXT
{
    const SOFIPC_OPS* Ops;
    PVOID OpsContext;
    FWLOAD_CONTEXT* Fw;
    SOFIPC_LINK Link;
    /* the outcome that faulted the link */
    SOFIPC_STATUS Failure;
    UINT32 TimeoutUs[SofIpcClassCount];
    BOOLEAN UseInterrupts;
    /* the bytes one block of a payload may carry: the smaller of the two mailboxes the firmware reported, less its remainder below four */
    UINT32 BlockBytes;
    UINT32 Requests;
    UINT32 Replies;
    /* replies nobody asked for, acknowledged and skipped */
    UINT32 StrayReplies;
    UINT32 Timeouts;
    UINT32 Notifications;
    UINT32 Faults;
    UINT32 InterruptCount;
    BOOLEAN Outstanding;
    SOFIPC_EXCHANGE Last;
    SOFIPC_NOTIFICATION Ring[SOFIPC_NOTIFICATION_RING];
    UINT32 RingCount;
    UINT32 RingHead;
    /* the poll policy, counted: the delays of the fast phase and of the sleeping phase since the initialization */
    UINT32 FastPolls;
    UINT32 SlowPolls;
} SOFIPC_CONTEXT, *PSOFIPC_CONTEXT;

/* The budget of every class in microseconds: Milliseconds, with a class the table leaves at zero given the plan's default */
VOID
SofIpcDefaultTimeouts(
    _Out_writes_(SofIpcClassCount) UINT32* TimeoutUs,
    UINT32 Milliseconds);

/*
 * Attaches the transport to a loader context: ready when the loader is, offline otherwise. The mailboxes are the
 * ones the boot's configuration reply named. In interrupt mode the three IPC interrupt enables are set here, and
 * again by the passive path after each exchange the service routine masked; a storm the boot's guard already
 * tripped, or an enable that does not read back, faults the link at once.
 */
SOFIPC_STATUS
SofIpcInit(
    _Out_ SOFIPC_CONTEXT* Ipc,
    _In_opt_ const SOFIPC_OPS* Ops,
    _In_opt_ PVOID OpsContext,
    _In_ FWLOAD_CONTEXT* Fw,
    _In_reads_opt_(SofIpcClassCount) const UINT32* TimeoutUs,
    BOOLEAN UseInterrupts);

/*
 * One request and its reply. The payload, if any, is written into the downlink mailbox first; the reply's type,
 * direction and target must echo the request's and its status must be success; the reply buffer, if any, is read
 * before the reply is acknowledged. A notification that arrives while the reply is waited for is kept and
 * acknowledged, and the wait goes on. Nothing is retried: every failure except a refused payload or a busy
 * caller faults the link, and a faulted link answers every later request with SofIpcBadState until the next
 * SofIpcInit.
 */
SOFIPC_STATUS
SofIpcRequest(
    _Inout_ SOFIPC_CONTEXT* Ipc,
    _Inout_ SOFIPC_REQUEST* Request);

/* A configuration larger than a block, written in blocks of BlockBytes: each block is one request of the large-configuration class */
SOFIPC_STATUS
SofIpcLargeConfigSet(
    _Inout_ SOFIPC_CONTEXT* Ipc,
    UINT32 ModuleId,
    UINT32 InstanceId,
    UINT32 ParamId,
    _In_reads_bytes_(Bytes) const VOID* Data,
    SIZE_T Bytes);

/* A configuration read in blocks: Capacity is what the caller can take, Bytes what the firmware had; more than the caller can take is SofIpcBadLength after the first reply, and leaves the link ready */
SOFIPC_STATUS
SofIpcLargeConfigGet(
    _Inout_ SOFIPC_CONTEXT* Ipc,
    UINT32 ModuleId,
    UINT32 InstanceId,
    UINT32 ParamId,
    _Out_writes_bytes_(Capacity) VOID* Buffer,
    SIZE_T Capacity,
    _Out_ SIZE_T* Bytes);

/* Consumes what the firmware posted while no request was outstanding: notifications are kept and acknowledged, a reply nobody asked for is acknowledged, and a crash notification or a tripped guard faults the link; FALSE once the link is not usable */
BOOLEAN
SofIpcPollNotifications(
    _Inout_ SOFIPC_CONTEXT* Ipc);

/* The service routine of the DSP's IPC line while the transport owns it: the loader's routine (count, mask, storm guard) plus the transport's own count; runs under the caller's interrupt lock, at the caller's interrupt level, and touches no payload */
BOOLEAN
SofIpcInterruptService(
    _Inout_ SOFIPC_CONTEXT* Ipc);

/* The oldest notification not yet taken; FALSE when the ring is empty */
BOOLEAN
SofIpcTakeNotification(
    _Inout_ SOFIPC_CONTEXT* Ipc,
    _Out_ PSOFIPC_NOTIFICATION Notification);

/* The class a request belongs to by the plan's table, read from its primary word: the pipeline messages, the large-configuration blocks, and the module messages, which every type outside the table joins */
SOFIPC_CLASS
SofIpcClassOf(
    UINT32 Primary);

const char* SofIpcStatusName(SOFIPC_STATUS Status);
const char* SofIpcLinkName(SOFIPC_LINK Link);
/* A request in the words of the log, from its primary word: "create pipeline", "set pipeline state", "large config set", ... */
const char* SofIpcTypeName(UINT32 Primary);

#ifdef __cplusplus
}
#endif

#endif
