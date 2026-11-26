/*
 * PROJECT:     LiberNT audio driver libraries
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     The IPC4 transport: one request at a time through a loader context, a budget per message class, a poll for every reply, payloads in mailbox-sized blocks, notifications kept while a request is outstanding, and a link that fails closed; through injected operations, free of I/O
 * COPYRIGHT:   Copyright 2026 LiberNT contributors
 */

#include <ntddk.h>
#include "sofipc.h"

static
BOOLEAN
SofIpcIsCrash(
    const SOFIPC4_MSG* Message)
{
    if (!SofIpc4IsNotification(Message))
        return FALSE;
    return SofIpc4NotificationType(Message) == SOFIPC4_NOTIFY_EXCEPTION_CAUGHT ||
        SofIpc4NotificationType(Message) == SOFIPC4_NOTIFY_WATCHDOG_TIMEOUT;
}

static
UINT64
SofIpcNow(
    const SOFIPC_CONTEXT* Ipc)
{
    return Ipc->Fw->Ops->NowUs(Ipc->Fw->OpsContext);
}

/* A notification's header joins the ring; the ring keeps the newest SOFIPC_NOTIFICATION_RING */
static
VOID
SofIpcKeep(
    SOFIPC_CONTEXT* Ipc,
    const SOFIPC4_MSG* Message)
{
    SOFIPC_NOTIFICATION* Slot = &Ipc->Ring[Ipc->RingHead];

    Slot->Type = SofIpc4NotificationType(Message);
    Slot->Primary = Message->Primary;
    Slot->Extension = Message->Extension;
    Ipc->RingHead = (Ipc->RingHead + 1) % SOFIPC_NOTIFICATION_RING;
    if (Ipc->RingCount < SOFIPC_NOTIFICATION_RING)
        Ipc->RingCount++;
    Ipc->Notifications++;
}

/* Everything the firmware posted, at most a ring's worth per call: a notification is kept and acknowledged, a reply nobody asked for is acknowledged and skipped, a crash notification is the end. A firmware that posts a new notification on every acknowledgement cannot keep the caller here: what is left waits for the next call or the next poll */
static
SOFIPC_STATUS
SofIpcDrain(
    SOFIPC_CONTEXT* Ipc)
{
    SOFIPC4_MSG Message;
    BOOLEAN FromReply;
    UINT32 Count;

    if (Ipc->Fw->InterruptStorm)
        return SofIpcInterruptStorm;
    for (Count = 0; Count < SOFIPC_NOTIFICATION_RING && FwloadIpcReceive(Ipc->Fw, &Message, &FromReply); Count++)
    {
        if (FromReply)
        {
            FwloadIpcDone(Ipc->Fw, TRUE);
            Ipc->StrayReplies++;
            continue;
        }
        SofIpcKeep(Ipc, &Message);
        FwloadIpcDone(Ipc->Fw, FALSE);
        if (SofIpcIsCrash(&Message))
            return SofIpcCrash;
        if (Ipc->Fw->InterruptStorm)
            return SofIpcInterruptStorm;
    }
    return SofIpcOk;
}

/* Why a wait that has not met its condition stops: a tripped interrupt guard, the deadline and the poll bound; SofIpcOk asks for one poll more. The poll bound never comes before the first poll: a budget shorter than one poll is one poll, a stalled clock is a clock that did not move over the polls the budget needs */
static
SOFIPC_STATUS
SofIpcWaitStop(
    const SOFIPC_CONTEXT* Ipc,
    UINT64 Start,
    UINT64 Polls,
    UINT32 TimeoutUs)
{
    UINT64 StallPolls = SOFIPC_STALLED_POLL_FACTOR * (UINT64)TimeoutUs / SOFIPC_FAST_POLL_US;

    if (Ipc->Fw->InterruptStorm)
        return SofIpcInterruptStorm;
    if (SofIpcNow(Ipc) - Start >= TimeoutUs)
        return SofIpcTimeout;
    if (StallPolls != 0 && Polls >= StallPolls)
        return SofIpcClockStalled;
    return SofIpcOk;
}

/* The poll policy: a short stall inside the first millisecond, a sleep after it */
static
VOID
SofIpcPoll(
    SOFIPC_CONTEXT* Ipc,
    UINT64 Start)
{
    if (SofIpcNow(Ipc) - Start < SOFIPC_FAST_PHASE_US)
    {
        Ipc->Fw->Ops->DelayUs(Ipc->Fw->OpsContext, SOFIPC_FAST_POLL_US);
        Ipc->FastPolls++;
        return;
    }
    Ipc->Fw->Ops->DelayUs(Ipc->Fw->OpsContext, SOFIPC_SLOW_POLL_US);
    Ipc->SlowPolls++;
}

/* The exchange's record, as the caller's log line prints it */
static
VOID
SofIpcRecord(
    SOFIPC_CONTEXT* Ipc,
    const SOFIPC_REQUEST* Request,
    UINT64 Start,
    SOFIPC_STATUS Outcome)
{
    Ipc->Last.ElapsedUs = SofIpcNow(Ipc) - Start;
    Ipc->Last.Outcome = Outcome;
    if (Outcome == SofIpcBusy || Outcome == SofIpcBadLength)
    {
        Ipc->Last.Type = SofIpc4MsgType(&Request->Msg);
        Ipc->Last.RequestPrimary = Request->Msg.Primary | FWLOAD_IPC_BUSY;
        Ipc->Last.RequestExtension = Request->Msg.Extension;
        Ipc->Last.ReplyPrimary = 0;
        Ipc->Last.ReplyExtension = 0;
        Ipc->Last.Status = 0;
    }
}

/* One exchange from the lock taken to the reply acknowledged: the loader's register order, its receive and acknowledge routines, and a reply consumed only once its payload is read */
static
SOFIPC_STATUS
SofIpcExchange(
    SOFIPC_CONTEXT* Ipc,
    SOFIPC_REQUEST* Request,
    UINT64 Start)
{
    FWLOAD_CONTEXT* Fw = Ipc->Fw;
    const FWLOAD_PLATFORM* Platform = Fw->Platform;
    SOFIPC_EXCHANGE* Last = &Ipc->Last;
    SOFIPC4_MSG Reply;
    SOFIPC_STATUS Result;
    BOOLEAN FromReply, Parsed;
    UINT32 Type, Target, Status;
    UINT64 Polls = 0;

    Type = SofIpc4MsgType(&Request->Msg);
    Target = SofIpc4MsgTarget(&Request->Msg);
    Last->Type = Type;
    Last->RequestPrimary = Request->Msg.Primary | FWLOAD_IPC_BUSY;
    Last->RequestExtension = Request->Msg.Extension;
    Last->ReplyPrimary = 0;
    Last->ReplyExtension = 0;
    Last->Status = 0;
    /* what the firmware posted while nobody asked is consumed first, so that the reply is not waited for behind older messages */
    Result = SofIpcDrain(Ipc);
    if (Result != SofIpcOk)
        return Result;
    if (Request->PayloadBytes != 0 &&
        (Ipc->Ops->WriteMemory == NULL ||
         !Ipc->Ops->WriteMemory(Ipc->OpsContext, Platform->DownlinkOffset, Request->Payload, Request->PayloadBytes)))
        return SofIpcHardwareFault;
    Fw->Ops->Write32(Fw->OpsContext, FWLOAD_BAR_DSP, Platform->IpcRequestData, Request->Msg.Extension);
    Fw->Ops->Write32(Fw->OpsContext, FWLOAD_BAR_DSP, Platform->IpcRequest, Last->RequestPrimary);
    Ipc->Requests++;
    for (;;)
    {
        if (FwloadIpcReceive(Fw, &Reply, &FromReply))
        {
            if (FromReply)
                break;
            /* a notification beside the reply: kept and acknowledged, and the wait goes on */
            SofIpcKeep(Ipc, &Reply);
            FwloadIpcDone(Fw, FALSE);
            if (SofIpcIsCrash(&Reply))
                return SofIpcCrash;
        }
        Result = SofIpcWaitStop(Ipc, Start, Polls, Ipc->TimeoutUs[Request->Class]);
        if (Result != SofIpcOk)
            return Result;
        SofIpcPoll(Ipc, Start);
        Polls++;
    }
    /* the reply is read before the done bit is written back, whatever the reply says */
    if (Request->ReplyBytes != 0 &&
        !Fw->Ops->ReadMemory(Fw->OpsContext, Platform->UplinkOffset, Request->ReplyBuffer, Request->ReplyBytes))
    {
        FwloadIpcDone(Fw, TRUE);
        return SofIpcHardwareFault;
    }
    Parsed = SofIpc4ParseReply(&Reply, Type, Target, &Status);
    FwloadIpcDone(Fw, TRUE);
    Ipc->Replies++;
    /* the reply's header is the caller's to read: a large-configuration reply names its size in its extension */
    Request->Msg = Reply;
    Last->ReplyPrimary = Reply.Primary;
    Last->ReplyExtension = Reply.Extension;
    if (!Parsed)
        return SofIpcBadReply;
    Last->Status = Status;
    if (Status != SofIpc4Success)
        return SofIpcFwStatus;
    return SofIpcOk;
}

SOFIPC_STATUS
SofIpcDefaultTimeouts(
    UINT32* TimeoutUs,
    UINT32 Milliseconds)
{
    UINT64 Microseconds = (UINT64)Milliseconds * 1000;
    SOFIPC_CLASS Class;

    /* a budget the table cannot hold is refused, and the table is left as it was */
    if (Microseconds > 0xFFFFFFFFULL)
        return SofIpcBadLength;
    for (Class = 0; Class < SofIpcClassCount; Class++)
        TimeoutUs[Class] = (UINT32)Microseconds;
    return SofIpcOk;
}

SOFIPC_STATUS
SofIpcInit(
    SOFIPC_CONTEXT* Ipc,
    const SOFIPC_OPS* Ops,
    PVOID OpsContext,
    FWLOAD_CONTEXT* Fw,
    const UINT32* TimeoutUs,
    BOOLEAN UseInterrupts)
{
    static const SOFIPC_CONTEXT Empty;
    FWLOAD_STATUS Status;
    SOFIPC_CLASS Class;

    *Ipc = Empty;
    Ipc->Ops = Ops;
    Ipc->OpsContext = OpsContext;
    Ipc->Fw = Fw;
    SofIpcDefaultTimeouts(Ipc->TimeoutUs, SOFIPC_DEFAULT_TIMEOUT_MS);
    if (TimeoutUs != NULL)
    {
        for (Class = 0; Class < SofIpcClassCount; Class++)
            if (TimeoutUs[Class] != 0)
                Ipc->TimeoutUs[Class] = TimeoutUs[Class];
    }
    Ipc->UseInterrupts = UseInterrupts;
    if (Fw == NULL || Fw->State != FwloadStateReady)
    {
        Ipc->Link = SofIpcLinkOffline;
        return SofIpcBadState;
    }
    Ipc->BlockBytes = (Fw->FwConfig.DownlinkMailboxBytes < Fw->FwConfig.UplinkMailboxBytes ?
        Fw->FwConfig.DownlinkMailboxBytes : Fw->FwConfig.UplinkMailboxBytes) & ~(UINT32)3;
    Ipc->Link = SofIpcLinkReady;
    if (Fw->InterruptStorm)
    {
        Ipc->Link = SofIpcLinkFaulted;
        Ipc->Failure = SofIpcInterruptStorm;
        return SofIpcInterruptStorm;
    }
    if (UseInterrupts)
    {
        Status = FwloadInterruptsEnable(Fw, TRUE);
        if (Status != FwloadOk)
        {
            Ipc->Link = SofIpcLinkFaulted;
            Ipc->Failure = SofIpcHardwareFault;
            return SofIpcHardwareFault;
        }
    }
    return SofIpcOk;
}

SOFIPC_STATUS
SofIpcRequest(
    SOFIPC_CONTEXT* Ipc,
    SOFIPC_REQUEST* Request)
{
    UINT64 Start;
    SOFIPC_STATUS Result;

    if (Ipc == NULL || Ipc->Fw == NULL || Ipc->Ops == NULL || Request == NULL ||
        (UINT32)Request->Class >= SofIpcClassCount)
        return SofIpcBadState;
    if (Ipc->Fw->State != FwloadStateReady)
    {
        Ipc->Link = SofIpcLinkOffline;
        return SofIpcBadState;
    }
    if (Ipc->Link == SofIpcLinkOffline || Ipc->Link == SofIpcLinkFaulted)
        return SofIpcBadState;
    Start = SofIpcNow(Ipc);
    if ((Request->PayloadBytes & 3) != 0 || Request->PayloadBytes > Ipc->BlockBytes ||
        (Request->ReplyBytes & 3) != 0 || Request->ReplyBytes > Ipc->Fw->FwConfig.UplinkMailboxBytes)
    {
        SofIpcRecord(Ipc, Request, Start, SofIpcBadLength);
        return SofIpcBadLength;
    }
    /* the second caller waits for the request in progress, up to its own budget */
    if (Ipc->Ops->RequestLock != NULL && !Ipc->Ops->RequestLock(Ipc->OpsContext, Ipc->TimeoutUs[Request->Class]))
    {
        SofIpcRecord(Ipc, Request, Start, SofIpcBusy);
        return SofIpcBusy;
    }
    /* a lock that does not lock, or none: nothing is sent behind the request in progress */
    if (Ipc->Outstanding)
    {
        if (Ipc->Ops->RequestUnlock != NULL)
            Ipc->Ops->RequestUnlock(Ipc->OpsContext);
        SofIpcRecord(Ipc, Request, Start, SofIpcBusy);
        return SofIpcBusy;
    }
    Ipc->Outstanding = TRUE;
    Ipc->Link = SofIpcLinkBusy;
    Result = SofIpcExchange(Ipc, Request, Start);
    /* the service routine masked the enables on the reply; in interrupt mode they are set again for the next message */
    if (Result == SofIpcOk && Ipc->UseInterrupts && FwloadInterruptsEnable(Ipc->Fw, TRUE) != FwloadOk)
        Result = SofIpcHardwareFault;
    Ipc->Outstanding = FALSE;
    if (Ipc->Ops->RequestUnlock != NULL)
        Ipc->Ops->RequestUnlock(Ipc->OpsContext);
    Ipc->Link = Result == SofIpcOk ? SofIpcLinkReady : SofIpcLinkFaulted;
    if (Result == SofIpcTimeout)
        Ipc->Timeouts++;
    if (Result != SofIpcOk)
    {
        Ipc->Failure = Result;
        Ipc->Faults++;
    }
    SofIpcRecord(Ipc, Request, Start, Result);
    return Result;
}

SOFIPC_STATUS
SofIpcLargeConfigSet(
    SOFIPC_CONTEXT* Ipc,
    UINT32 ModuleId,
    UINT32 InstanceId,
    UINT32 ParamId,
    const VOID* Data,
    SIZE_T Bytes)
{
    SOFIPC_REQUEST Request;
    SOFIPC_STATUS Result;
    SIZE_T Offset = 0, Chunk;
    UINT32 Flags, SizeField;

    if (Bytes == 0 || (Bytes & 3) != 0 || Bytes > SOFIPC4_LARGE_CONFIG_DATA_MAX || Ipc->BlockBytes == 0)
        return SofIpcBadLength;
    do
    {
        Chunk = Bytes - Offset;
        if (Chunk > Ipc->BlockBytes)
            Chunk = Ipc->BlockBytes;
        /* the first block carries the total size with the first flag, every later one its offset; the last block sets the last flag */
        Flags = Offset == 0 ? SOFIPC4_LARGE_CONFIG_INIT_BLOCK : 0;
        if (Offset + Chunk == Bytes)
            Flags |= SOFIPC4_LARGE_CONFIG_FINAL_BLOCK;
        SizeField = Offset == 0 ? (UINT32)Bytes : (UINT32)Offset;
        RtlZeroMemory(&Request, sizeof(Request));
        Request.Class = SofIpcClassLargeConfig;
        SofIpc4ModuleLargeConfig(&Request.Msg, ModuleId, InstanceId, ParamId, SizeField, Flags, FALSE);
        Request.Payload = (const UINT8*)Data + Offset;
        Request.PayloadBytes = Chunk;
        Result = SofIpcRequest(Ipc, &Request);
        if (Result != SofIpcOk)
            return Result;
        Offset += Chunk;
    } while (Offset < Bytes);
    return SofIpcOk;
}

SOFIPC_STATUS
SofIpcLargeConfigGet(
    SOFIPC_CONTEXT* Ipc,
    UINT32 ModuleId,
    UINT32 InstanceId,
    UINT32 ParamId,
    VOID* Buffer,
    SIZE_T Capacity,
    SIZE_T* Bytes)
{
    SOFIPC_REQUEST Request;
    SOFIPC_STATUS Result;
    SIZE_T Offset = 0, Remaining, Chunk, Limit;
    UINT32 Total;

    *Bytes = 0;
    Capacity &= ~(SIZE_T)3;
    if (Capacity == 0 || Ipc->BlockBytes == 0)
        return SofIpcBadLength;
    Limit = Capacity < Ipc->BlockBytes ? Capacity : Ipc->BlockBytes;
    Remaining = Capacity;
    do
    {
        Chunk = Remaining < Limit ? Remaining : Limit;
        /* the first request names what the caller can take; every later one the offset it continues from */
        RtlZeroMemory(&Request, sizeof(Request));
        Request.Class = SofIpcClassLargeConfig;
        SofIpc4ModuleLargeConfig(&Request.Msg, ModuleId, InstanceId, ParamId,
            Offset == 0 ? (UINT32)Capacity : (UINT32)Offset, Offset == 0 ? SOFIPC4_LARGE_CONFIG_INIT_BLOCK : 0, TRUE);
        Request.ReplyBuffer = (UINT8*)Buffer + Offset;
        Request.ReplyBytes = Chunk;
        Result = SofIpcRequest(Ipc, &Request);
        if (Result != SofIpcOk)
            return Result;
        if (Offset == 0)
        {
            /* the first reply reports the firmware's total; a larger one is not taken, a smaller one ends the read early */
            Total = Request.Msg.Extension & SOFIPC4_LARGE_CONFIG_DATA_MAX;
            if (Total > Capacity)
                return SofIpcBadLength;
            if (Total < Chunk)
                Chunk = Total;
            Remaining = Total - Chunk;
        }
        else
            Remaining -= Chunk;
        Offset += Chunk;
    } while (Remaining != 0);
    *Bytes = Offset;
    return SofIpcOk;
}

BOOLEAN
SofIpcPollNotifications(
    SOFIPC_CONTEXT* Ipc)
{
    SOFIPC_STATUS Result;
    BOOLEAN Held = FALSE;

    if (Ipc->Fw == NULL)
        return FALSE;
    if (Ipc->Fw->State != FwloadStateReady)
    {
        Ipc->Link = SofIpcLinkOffline;
        return FALSE;
    }
    /* the request lock when there is one, taken without a wait: a request holds it from before it sends anything until after its reply, and the link says busy only in between, so the lock and not the link is what a poll must stand behind */
    if (Ipc->Ops != NULL && Ipc->Ops->RequestLock != NULL)
    {
        if (!Ipc->Ops->RequestLock(Ipc->OpsContext, 0))
            return FALSE;
        Held = TRUE;
    }
    /* nothing is touched behind a request in progress or on a dead link */
    if (Ipc->Link != SofIpcLinkReady)
    {
        if (Held && Ipc->Ops->RequestUnlock != NULL)
            Ipc->Ops->RequestUnlock(Ipc->OpsContext);
        return FALSE;
    }
    Result = SofIpcDrain(Ipc);
    if (Held && Ipc->Ops->RequestUnlock != NULL)
        Ipc->Ops->RequestUnlock(Ipc->OpsContext);
    if (Result == SofIpcOk)
        return TRUE;
    Ipc->Link = SofIpcLinkFaulted;
    Ipc->Failure = Result;
    Ipc->Faults++;
    return FALSE;
}

BOOLEAN
SofIpcInterruptService(
    SOFIPC_CONTEXT* Ipc)
{
    if (Ipc->Fw == NULL || !FwloadInterruptService(Ipc->Fw))
        return FALSE;
    Ipc->InterruptCount++;
    return TRUE;
}

BOOLEAN
SofIpcTakeNotification(
    SOFIPC_CONTEXT* Ipc,
    SOFIPC_NOTIFICATION* Notification)
{
    UINT32 Oldest;

    if (Ipc->RingCount == 0)
        return FALSE;
    Oldest = (Ipc->RingHead + SOFIPC_NOTIFICATION_RING - Ipc->RingCount) % SOFIPC_NOTIFICATION_RING;
    *Notification = Ipc->Ring[Oldest];
    Ipc->RingCount--;
    return TRUE;
}

SOFIPC_CLASS
SofIpcClassOf(
    UINT32 Primary)
{
    UINT32 Type = (Primary >> SOFIPC4_TYPE_SHIFT) & SOFIPC4_TYPE_MASK;

    if (!(Primary & SOFIPC4_MSG_TGT_BIT))
    {
        if (Type == SofIpc4GlbChainDma || (Type >= SofIpc4GlbCreatePipeline && Type <= SofIpc4GlbRestorePipeline))
            return SofIpcClassPipeline;
        return SofIpcClassModule;
    }
    if (Type == SofIpc4ModLargeConfigGet || Type == SofIpc4ModLargeConfigSet)
        return SofIpcClassLargeConfig;
    return SofIpcClassModule;
}

const char*
SofIpcStatusName(
    SOFIPC_STATUS Status)
{
    static const char* const Names[] =
    {
        "ok", "bad state", "busy", "bad length", "reply timeout", "clock stalled", "bad reply",
        "firmware status", "firmware crash", "interrupt storm", "hardware fault"
    };

    if ((UINT32)Status >= sizeof(Names) / sizeof(Names[0]))
        return "unknown";
    return Names[Status];
}

const char*
SofIpcLinkName(
    SOFIPC_LINK Link)
{
    static const char* const Names[] =
    {
        "offline", "ready", "busy", "faulted"
    };

    if ((UINT32)Link >= sizeof(Names) / sizeof(Names[0]))
        return "unknown";
    return Names[Link];
}

const char*
SofIpcTypeName(
    UINT32 Primary)
{
    UINT32 Type = (Primary >> SOFIPC4_TYPE_SHIFT) & SOFIPC4_TYPE_MASK;

    if (Primary & SOFIPC4_MSG_TGT_BIT)
    {
        static const char* const Module[] =
        {
            "init instance", "config get", "config set", "large config get", "large config set", "bind", "unbind",
            "set DX", "set D0ix", "enter module restore", "exit module restore", "delete instance"
        };

        if (Type >= sizeof(Module) / sizeof(Module[0]))
            return "message";
        return Module[Type];
    }
    static const char* const Global[] =
    {
        "boot config", "ROM control", "IPC gateway command", NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
        NULL, "performance measurements", "chain DMA", "load multiple modules", "unload multiple modules",
        "create pipeline", "delete pipeline", "set pipeline state", "get pipeline state", "get pipeline context size",
        "save pipeline", "restore pipeline", "load library", "load library prepare", "internal message", "notification",
        NULL, NULL, NULL, "enter GDB"
    };

    if (Type >= sizeof(Global) / sizeof(Global[0]) || Global[Type] == NULL)
        return "message";
    return Global[Type];
}
