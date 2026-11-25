/*
 * PROJECT:     LiberNT kernel-mode tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     The IPC4 transport over the DSP model in the kernel: the loader's register order, a budget per message class, status and crash replies, notifications, the interrupt mode and its storm guard, two callers, large configurations in blocks and the poll policy, over the shared case rows; and the driver's request lock against a second thread
 * COPYRIGHT:   Copyright 2026 LiberNT contributors
 */

#include <kmt_test.h>
#include <fwload.h>
#include <sofipc.h>
#include <fake_dsp.h>
#include <ipclock.h>

#define NDEBUG
#include <debug.h>

#include "../fwload/fwload_cases.h"
#include "ipc4_transport_cases.h"

#define SOFIPC_TEST_TAG         'cIpS'
#define SOFIPC_TEST_BDL_ENTRIES 256
#define SOFIPC_TEST_ACCESS      2048
#define SOFIPC_TEST_SHAPE       64

typedef struct _SOFIPC_HARNESS
{
    PFAKE_DSP Dsp;
    PFWLOAD_CONTEXT Ctx;
    SOFIPC_CONTEXT Ipc;
    /* the model's operations with this harness as their context: a delay raises the line as the case asked */
    FWLOAD_OPS Ops;
    PVOID Virtual;
    FWLOAD_DMA_SEGMENT Segment;
    FWLOAD_BDL_ENTRY Bdl[SOFIPC_TEST_BDL_ENTRIES];
    FAKE_DSP_ACCESS Access[SOFIPC_TEST_ACCESS];
    BOOLEAN LockHeld;
    UINT32 LockAcquires;
    UINT32 LockWaits;
    UINT32 LockUnlocks;
    UINT32 LastLockTimeout;
    UINT32 InterruptsPerDelay;
    BOOLEAN IpcArmed;
} SOFIPC_HARNESS;

static const SOFIPC_OPS SofIpcModelLockOps;

static
BOOLEAN
SofIpcHarnessInterrupt(
    SOFIPC_HARNESS* H)
{
    BOOLEAN Claimed;

    FakeDspOps()->Lock(H->Dsp);
    Claimed = H->IpcArmed ? SofIpcInterruptService(&H->Ipc) : FwloadInterruptService(H->Ctx);
    FakeDspOps()->Unlock(H->Dsp);
    return Claimed;
}

static
UINT32
HRead32(
    PVOID Context,
    UINT32 Bar,
    UINT32 Offset)
{
    return FakeDspOps()->Read32(((SOFIPC_HARNESS*)Context)->Dsp, Bar, Offset);
}

static
VOID
HWrite32(
    PVOID Context,
    UINT32 Bar,
    UINT32 Offset,
    UINT32 Value)
{
    FakeDspOps()->Write32(((SOFIPC_HARNESS*)Context)->Dsp, Bar, Offset, Value);
}

static
BOOLEAN
HReadMemory(
    PVOID Context,
    UINT32 Offset,
    UINT8* Buffer,
    SIZE_T Bytes)
{
    return FakeDspOps()->ReadMemory(((SOFIPC_HARNESS*)Context)->Dsp, Offset, Buffer, Bytes);
}

static
UINT64
HNowUs(
    PVOID Context)
{
    return FakeDspOps()->NowUs(((SOFIPC_HARNESS*)Context)->Dsp);
}

static
VOID
HDelayUs(
    PVOID Context,
    UINT32 Microseconds)
{
    SOFIPC_HARNESS* H = (SOFIPC_HARNESS*)Context;
    UINT32 Index;

    FakeDspOps()->DelayUs(H->Dsp, Microseconds);
    for (Index = 0; Index < H->InterruptsPerDelay && FakeDspMessagePending(H->Dsp); Index++)
        SofIpcHarnessInterrupt(H);
}

static
FWLOAD_STATUS
HStreamSubmit(
    PVOID Context,
    PFWLOAD_CL_DMA Dma)
{
    return FakeDspOps()->StreamSubmit(((SOFIPC_HARNESS*)Context)->Dsp, Dma);
}

static
FWLOAD_STATUS
HStreamTrigger(
    PVOID Context,
    BOOLEAN Start)
{
    return FakeDspOps()->StreamTrigger(((SOFIPC_HARNESS*)Context)->Dsp, Start);
}

static
VOID
HStreamRelease(
    PVOID Context)
{
    FakeDspOps()->StreamRelease(((SOFIPC_HARNESS*)Context)->Dsp);
}

static
VOID
HLock(
    PVOID Context)
{
    FakeDspOps()->Lock(((SOFIPC_HARNESS*)Context)->Dsp);
}

static
VOID
HUnlock(
    PVOID Context)
{
    FakeDspOps()->Unlock(((SOFIPC_HARNESS*)Context)->Dsp);
}

static
BOOLEAN
HIpcWriteMemory(
    PVOID Context,
    UINT32 Offset,
    const UINT8* Buffer,
    SIZE_T Bytes)
{
    return FakeDspWriteMemory(((SOFIPC_HARNESS*)Context)->Dsp, Offset, Buffer, Bytes);
}

static
BOOLEAN
HIpcModelLock(
    PVOID Context,
    UINT32 TimeoutUs)
{
    SOFIPC_HARNESS* H = (SOFIPC_HARNESS*)Context;

    if (!H->LockHeld)
    {
        H->LockHeld = TRUE;
        H->LockAcquires++;
        return TRUE;
    }
    H->LockWaits++;
    H->LastLockTimeout = TimeoutUs;
    FakeDspOps()->DelayUs(H->Dsp, TimeoutUs);
    return FALSE;
}

static
VOID
HIpcModelUnlock(
    PVOID Context)
{
    SOFIPC_HARNESS* H = (SOFIPC_HARNESS*)Context;

    H->LockHeld = FALSE;
    H->LockUnlocks++;
}

static const SOFIPC_OPS SofIpcModelLockOps =
{
    HIpcWriteMemory,
    HIpcModelLock,
    HIpcModelUnlock
};

/* The driver's request lock as the transport's operations: what intcdsp.sys hands the transport */
static
BOOLEAN
HIpcKernelLock(
    PVOID Context,
    UINT32 TimeoutUs)
{
    return IntcDspRequestLockAcquire((PINTCDSP_REQUEST_LOCK)Context, TimeoutUs);
}

static
VOID
HIpcKernelUnlock(
    PVOID Context)
{
    IntcDspRequestLockRelease((PINTCDSP_REQUEST_LOCK)Context);
}

static const SOFIPC_OPS SofIpcKernelLockOps =
{
    HIpcWriteMemory,
    HIpcKernelLock,
    HIpcKernelUnlock
};

static
VOID
SofIpcHarnessBoot(
    SOFIPC_HARNESS* H,
    const SOF_RI_IMAGE* Image,
    const FAKE_DSP_FAULTS* Faults)
{
    FWLOAD_CL_DMA Dma;

    FakeDspInit(H->Dsp, FwloadPlatformArlS(), Faults);
    FakeDspAccessBegin(H->Dsp, H->Access, SOFIPC_TEST_ACCESS);
    RtlZeroMemory(&Dma, sizeof(Dma));
    Dma.Virtual = H->Virtual;
    Dma.Capacity = (SIZE_T)H->Segment.Bytes;
    Dma.Segment = &H->Segment;
    Dma.SegmentCount = 1;
    Dma.Bdl = H->Bdl;
    Dma.BdlCapacity = SOFIPC_TEST_BDL_ENTRIES;
    Dma.BdlBytes = sizeof(H->Bdl);
    Dma.StreamTag = FWLOAD_CL_STREAM_TAG;
    Dma.StreamFormat = FWLOAD_CL_STREAM_FORMAT;
    FwloadInitContext(H->Ctx, &H->Ops, H, FwloadPlatformArlS(), Image, &Dma, NULL);
    H->IpcArmed = FALSE;
    H->InterruptsPerDelay = 0;
    H->LockHeld = FALSE;
    H->LockAcquires = 0;
    H->LockWaits = 0;
    H->LockUnlocks = 0;
    RtlZeroMemory(&H->Ipc, sizeof(H->Ipc));
    ok_eq_int(FwloadBoot(H->Ctx), FwloadOk);
    ok_eq_uint(H->Ctx->State, FwloadStateReady);
    ok_eq_uint(FakeDspIpcErrors(H->Dsp), 0);
}

static
VOID
SofIpcHarnessAttach(
    SOFIPC_HARNESS* H,
    const UINT32* TimeoutUs,
    BOOLEAN UseInterrupts)
{
    ok_eq_int(SofIpcInit(&H->Ipc, &SofIpcModelLockOps, H, H->Ctx, TimeoutUs, UseInterrupts), SofIpcOk);
    ok_eq_uint(H->Ipc.Link, SofIpcLinkReady);
    H->IpcArmed = UseInterrupts;
}

static
VOID
SofIpcHarnessRequest(
    SOFIPC_REQUEST* Request,
    SOFIPC_CLASS Class,
    UINT32 Primary,
    UINT32 Extension)
{
    RtlZeroMemory(Request, sizeof(*Request));
    Request->Class = Class;
    Request->Msg.Primary = Primary;
    Request->Msg.Extension = Extension;
}

/* An exchange reduced to its steps: a poll cycle that repeats the one before it is not a step of its own; a poll's value is its busy or done bit */
static
BOOLEAN
SameAccess(
    const FAKE_DSP_ACCESS* A,
    const FAKE_DSP_ACCESS* B)
{
    if (A->Op != B->Op || A->Bar != B->Bar || A->Offset != B->Offset)
        return FALSE;
    if (A->Op == FAKE_DSP_ACCESS_READ32 && A->Bar == FWLOAD_BAR_DSP &&
        (A->Offset == FwloadPlatformArlS()->IpcReply || A->Offset == FwloadPlatformArlS()->IpcTarget))
        return (A->Value & FWLOAD_IPC_DONE) == (B->Value & FWLOAD_IPC_DONE);
    return A->Value == B->Value;
}

static
UINT32
Shape(
    const FAKE_DSP_ACCESS* In,
    UINT32 Count,
    FAKE_DSP_ACCESS* Out)
{
    UINT32 OutCount = 0, Index = 0;

    while (Index < Count)
    {
        if (OutCount >= 2 && Index + 1 < Count && SameAccess(&Out[OutCount - 2], &In[Index]) &&
            SameAccess(&Out[OutCount - 1], &In[Index + 1]))
        {
            Index += 2;
            continue;
        }
        Out[OutCount++] = In[Index++];
    }
    return OutCount;
}

static
UINT32
ShapeOfExchange(
    SOFIPC_HARNESS* H,
    FAKE_DSP_ACCESS* Out)
{
    FAKE_DSP_ACCESS Slice[SOFIPC_TEST_SHAPE];
    UINT32 Count;

    ok(H->Dsp->ExchangeStart <= H->Dsp->ExchangeEnd && H->Dsp->ExchangeEnd < H->Dsp->AccessCount, "no exchange in the access log\n");
    Count = H->Dsp->ExchangeEnd - H->Dsp->ExchangeStart + 1;
    if (Count > SOFIPC_TEST_SHAPE)
        Count = SOFIPC_TEST_SHAPE;
    RtlCopyMemory(Slice, H->Access + H->Dsp->ExchangeStart, Count * sizeof(Slice[0]));
    return Shape(Slice, Count, Out);
}

/* (a) the register-order pin */
static
VOID
CheckRegisterOrderPin(
    SOFIPC_HARNESS* H,
    const SOF_RI_IMAGE* Image)
{
    FAKE_DSP_ACCESS Loader[SOFIPC_TEST_SHAPE], Transport[SOFIPC_TEST_SHAPE];
    UINT32 LoaderCount, TransportCount, Index;
    SOFIPC_REQUEST Request;
    SOFIPC4_MSG Msg;
    static UINT8 Reply[FWLOAD_MAILBOX_BYTES];

    SofIpcHarnessBoot(H, Image, NULL);
    LoaderCount = ShapeOfExchange(H, Loader);
    ok_eq_uint(LoaderCount, 8);
    FakeDspAccessBegin(H->Dsp, H->Access, SOFIPC_TEST_ACCESS);
    SofIpcHarnessAttach(H, NULL, FALSE);
    SofIpc4ModuleConfig(&Msg, FWLOAD_BASEFW_MODULE_ID, FWLOAD_BASEFW_INSTANCE_ID, FWLOAD_PARAM_ID_FW_CONFIG << FWLOAD_PARAM_ID_SHIFT, TRUE);
    SofIpcHarnessRequest(&Request, SofIpcClassModule, Msg.Primary, Msg.Extension);
    Request.ReplyBuffer = Reply;
    Request.ReplyBytes = sizeof(Reply);
    ok_eq_int(SofIpcRequest(&H->Ipc, &Request), SofIpcOk);
    TransportCount = ShapeOfExchange(H, Transport);
    ok_eq_uint(TransportCount, LoaderCount);
    for (Index = 0; Index < LoaderCount && Index < TransportCount; Index++)
        ok(SameAccess(&Loader[Index], &Transport[Index]), "step %lu differs: loader %u/%lx/%lx, transport %u/%lx/%lx\n", Index,
           Loader[Index].Op, Loader[Index].Offset, Loader[Index].Value, Transport[Index].Op, Transport[Index].Offset, Transport[Index].Value);
    ok_eq_uint(Transport[1].Value, 0xC1000000);
    ok_eq_uint(Transport[6].Op, FAKE_DSP_ACCESS_READMEMORY);
    ok_eq_uint(Transport[6].Value, FWLOAD_MAILBOX_BYTES);
    ok_eq_uint(Transport[7].Op, FAKE_DSP_ACCESS_WRITE32);
    ok_eq_uint(Transport[7].Value, FWLOAD_IPC_DONE);
    ok_eq_uint(H->Ipc.Last.ReplyPrimary, 0x61000000);
    ok(RtlCompareMemory(Reply, H->Ctx->Mailbox, FWLOAD_MAILBOX_BYTES) == FWLOAD_MAILBOX_BYTES, "the reply's payload differs from the loader's\n");
    ok_eq_uint(H->Ipc.Requests, 1);
    ok_eq_uint(H->Ipc.Replies, 1);
    ok_eq_uint(FakeDspIpcErrors(H->Dsp), 0);
}

/* (b) a budget per message class */
static
VOID
CheckTimeouts(
    SOFIPC_HARNESS* H,
    const SOF_RI_IMAGE* Image)
{
    UINT32 Timeouts[SofIpcClassCount];
    SOFIPC_REQUEST Request;
    UINT32 Index;

    for (Index = 0; Index < IPC4_TRANSPORT_TIMEOUT_CASES; Index++)
    {
        const IPC4_TRANSPORT_TIMEOUT_CASE* Case = &Ipc4TransportTimeouts[Index];

        SofIpcHarnessBoot(H, Image, NULL);
        SofIpcDefaultTimeouts(Timeouts, SOFIPC_DEFAULT_TIMEOUT_MS);
        Timeouts[Case->Class] = Case->TimeoutUs;
        SofIpcHarnessAttach(H, Timeouts, FALSE);
        H->Dsp->Faults.NeverReplyForType = Case->Type;
        ok_eq_int(SofIpcClassOf(Case->Primary), Case->Class);
        SofIpcHarnessRequest(&Request, Case->Class, Case->Primary, Case->Extension);
        ok_eq_int(SofIpcRequest(&H->Ipc, &Request), SofIpcTimeout);
        ok(H->Ipc.Last.ElapsedUs >= Case->TimeoutUs && H->Ipc.Last.ElapsedUs < Case->TimeoutUs + SOFIPC_SLOW_POLL_US,
           "class %lu ended after %I64u us for a budget of %lu\n", Case->Class, H->Ipc.Last.ElapsedUs, Case->TimeoutUs);
        ok_eq_uint(H->Ipc.Link, SofIpcLinkFaulted);
        ok_eq_int(H->Ipc.Failure, SofIpcTimeout);
        ok_eq_uint(H->Ipc.Requests, 1);
        ok_eq_uint(H->Ipc.Replies, 0);
        ok_eq_uint(H->Dsp->TransportRequests, 1);
        SofIpcHarnessRequest(&Request, Case->Class, Case->Primary, Case->Extension);
        ok_eq_int(SofIpcRequest(&H->Ipc, &Request), SofIpcBadState);
        ok_eq_uint(H->Ipc.Requests, 1);
    }
}

/* (c) a non-success status, and a reply to another message */
static
VOID
CheckStatus(
    SOFIPC_HARNESS* H,
    const SOF_RI_IMAGE* Image)
{
    SOFIPC_REQUEST Request;

    SofIpcHarnessBoot(H, Image, NULL);
    SofIpcHarnessAttach(H, NULL, FALSE);
    H->Dsp->Faults.IpcStatusForType = SofIpc4GlbSetPipelineState;
    H->Dsp->Faults.IpcStatusValue = SofIpc4BadState;
    SofIpcHarnessRequest(&Request, SofIpcClassPipeline, Ipc4TransportSelfTest[0].Primary, 0);
    ok_eq_int(SofIpcRequest(&H->Ipc, &Request), SofIpcOk);
    SofIpcHarnessRequest(&Request, SofIpcClassPipeline, Ipc4TransportSelfTest[1].Primary, 0);
    ok_eq_int(SofIpcRequest(&H->Ipc, &Request), SofIpcFwStatus);
    ok_eq_uint(H->Ipc.Last.Status, SofIpc4BadState);
    ok_eq_str(SofIpc4StatusName(H->Ipc.Last.Status), "bad state");
    ok_eq_str(SofIpcTypeName(H->Ipc.Last.RequestPrimary), "set pipeline state");
    ok_eq_uint(H->Ipc.Link, SofIpcLinkFaulted);
    ok_eq_uint(H->Ipc.Replies, 2);
    ok_eq_uint(FakeDspIpcErrors(H->Dsp), 0);
    SofIpcHarnessRequest(&Request, SofIpcClassPipeline, Ipc4TransportSelfTest[2].Primary, 0);
    ok_eq_int(SofIpcRequest(&H->Ipc, &Request), SofIpcBadState);

    SofIpcHarnessBoot(H, Image, NULL);
    SofIpcHarnessAttach(H, NULL, FALSE);
    H->Dsp->Faults.ReplyTypeShift = 1;
    SofIpcHarnessRequest(&Request, SofIpcClassPipeline, Ipc4TransportSelfTest[0].Primary, 0);
    ok_eq_int(SofIpcRequest(&H->Ipc, &Request), SofIpcBadReply);
    ok_eq_uint(H->Ipc.Link, SofIpcLinkFaulted);
}

/* (d) a crash notification during a request */
static
VOID
CheckCrash(
    SOFIPC_HARNESS* H,
    const SOF_RI_IMAGE* Image)
{
    SOFIPC_REQUEST Request;
    SOFIPC_NOTIFICATION Notification;

    SofIpcHarnessBoot(H, Image, NULL);
    SofIpcHarnessAttach(H, NULL, FALSE);
    H->Dsp->Faults.CrashDuringRequest = TRUE;
    SofIpcHarnessRequest(&Request, SofIpcClassPipeline, Ipc4TransportSelfTest[0].Primary, 0);
    ok_eq_int(SofIpcRequest(&H->Ipc, &Request), SofIpcCrash);
    ok_eq_uint(H->Ipc.Link, SofIpcLinkFaulted);
    ok_eq_uint(H->Ipc.Requests, 1);
    ok_eq_uint(H->Ipc.Replies, 0);
    ok_eq_uint(H->Ipc.Notifications, 1);
    ok(SofIpcTakeNotification(&H->Ipc, &Notification), "no notification kept\n");
    ok_eq_uint(Notification.Type, SOFIPC4_NOTIFY_EXCEPTION_CAUGHT);
    ok(!SofIpcTakeNotification(&H->Ipc, &Notification), "a second notification kept\n");
    ok_eq_uint(FakeDspIpcErrors(H->Dsp), 0);
}

/* (e) notifications queued while a request is outstanding */
static
VOID
CheckNotifications(
    SOFIPC_HARNESS* H,
    const SOF_RI_IMAGE* Image)
{
    SOFIPC_REQUEST Request;
    SOFIPC_NOTIFICATION Notification;
    UINT32 Index;

    SofIpcHarnessBoot(H, Image, NULL);
    SofIpcHarnessAttach(H, NULL, FALSE);
    H->Dsp->Faults.NotifyDuringRequest = TRUE;
    for (Index = 0; Index < IPC4_TRANSPORT_SELF_TEST_EXCHANGES; Index++)
    {
        SofIpcHarnessRequest(&Request, SofIpcClassPipeline, Ipc4TransportSelfTest[Index].Primary, 0);
        ok_eq_int(SofIpcRequest(&H->Ipc, &Request), SofIpcOk);
        ok_eq_uint(H->Ipc.Last.Status, SofIpc4Success);
    }
    ok_eq_uint(H->Ipc.Notifications, IPC4_TRANSPORT_SELF_TEST_EXCHANGES);
    ok_eq_uint(H->Ipc.RingCount, IPC4_TRANSPORT_SELF_TEST_EXCHANGES);
    ok_eq_uint(H->Ipc.Replies, IPC4_TRANSPORT_SELF_TEST_EXCHANGES);
    ok_eq_uint(H->Ipc.Link, SofIpcLinkReady);
    for (Index = 0; Index < IPC4_TRANSPORT_SELF_TEST_EXCHANGES; Index++)
    {
        ok(SofIpcTakeNotification(&H->Ipc, &Notification), "notification %lu missing\n", Index);
        ok_eq_uint(Notification.Type, SOFIPC4_NOTIFY_LOG_BUFFER_STATUS);
    }
    ok_eq_uint(FakeDspIpcErrors(H->Dsp), 0);
}

/* (f) the interrupt mode: set at the initialization, masked by the service routine on every interrupt, set again after each exchange; the storm guard */
static
VOID
CheckInterruptMode(
    SOFIPC_HARNESS* H,
    const SOF_RI_IMAGE* Image)
{
    SOFIPC_REQUEST Request;
    FAKE_DSP_FAULTS Faults;
    UINT32 Index, Writes;

    SofIpcHarnessBoot(H, Image, NULL);
    SofIpcHarnessAttach(H, NULL, TRUE);
    ok_eq_uint(H->Dsp->InterruptEnable, 1);
    ok_eq_uint(H->Dsp->HostIpcEnable, 1);
    ok_eq_uint(H->Dsp->IpcControl, 3);
    Writes = H->Dsp->InterruptWritesLocked;
    ok_eq_uint(Writes, 6);
    H->InterruptsPerDelay = 1;
    for (Index = 0; Index < IPC4_TRANSPORT_SELF_TEST_EXCHANGES; Index++)
    {
        SofIpcHarnessRequest(&Request, SofIpcClassPipeline, Ipc4TransportSelfTest[Index].Primary, 0);
        ok_eq_int(SofIpcRequest(&H->Ipc, &Request), SofIpcOk);
        ok_eq_uint(H->Dsp->InterruptEnable, 1);
        ok_eq_uint(H->Dsp->HostIpcEnable, 1);
        ok_eq_uint(H->Dsp->IpcControl, 3);
        ok_eq_uint(H->Ipc.InterruptCount, Index + 1);
    }
    ok_eq_uint(H->Dsp->InterruptWritesLocked, Writes + 6 * IPC4_TRANSPORT_SELF_TEST_EXCHANGES);
    ok_eq_uint(H->Dsp->InterruptWritesUnlocked, 0);
    ok_eq_uint(H->Dsp->InterruptLockRecursions, 0);
    ok_eq_uint(H->Dsp->InterruptLockDelays, 0);

    /* the guard: a message pending and no request outstanding */
    H->InterruptsPerDelay = 0;
    H->Dsp->IpcTarget = FWLOAD_IPC_BUSY | SOFIPC4_FW_READY_PRIMARY;
    for (Index = 1; Index < FWLOAD_INTERRUPT_STORM_LIMIT; Index++)
    {
        ok(SofIpcHarnessInterrupt(H), "interrupt %lu not claimed\n", Index);
        ok_eq_uint(H->Dsp->InterruptEnable, 0);
        ok_eq_uint(H->Dsp->HostIpcEnable, 0);
        ok_eq_uint(H->Dsp->IpcControl, 0);
        ok(!H->Ctx->InterruptStorm, "the guard tripped early\n");
    }
    ok(SofIpcHarnessInterrupt(H), "the last interrupt before the guard was not claimed\n");
    ok(H->Ctx->InterruptStorm, "the guard did not trip\n");
    ok(!SofIpcHarnessInterrupt(H), "a disarmed routine claimed the line\n");
    ok_eq_int(SofIpcInit(&H->Ipc, &SofIpcModelLockOps, H, H->Ctx, NULL, TRUE), SofIpcInterruptStorm);
    ok_eq_uint(H->Ipc.Link, SofIpcLinkFaulted);

    /* the guard during a wait under a notification stream */
    Faults = *FakeDspDefaultFaults();
    Faults.NotificationStorm = FAKE_DSP_STORM_AFTER_FW_READY;
    SofIpcHarnessBoot(H, Image, &Faults);
    SofIpcHarnessAttach(H, NULL, TRUE);
    H->InterruptsPerDelay = FWLOAD_INTERRUPT_STORM_LIMIT;
    H->Dsp->Faults.NeverReplyForType = SofIpc4GlbSetPipelineState;
    SofIpcHarnessRequest(&Request, SofIpcClassPipeline, Ipc4TransportSelfTest[1].Primary, 0);
    ok_eq_int(SofIpcRequest(&H->Ipc, &Request), SofIpcInterruptStorm);
    ok_eq_uint(H->Ipc.Failure, SofIpcInterruptStorm);
    ok_eq_uint(H->Ipc.Notifications, SOFIPC_NOTIFICATION_RING + 2);
    ok(H->Ipc.Last.ElapsedUs <= 2 * SOFIPC_FAST_POLL_US, "the wait went on for %I64u us after the guard tripped\n", H->Ipc.Last.ElapsedUs);
}

/* The driver's request lock with a second thread: the thread holds the lock for a while, this thread's short wait reaches its budget and its long one gets the lock once the thread lets go */
typedef struct _SOFIPC_LOCK_THREAD
{
    PINTCDSP_REQUEST_LOCK Lock;
    PKEVENT Held;
} SOFIPC_LOCK_THREAD;

static
VOID
NTAPI
LockHolder(
    PVOID Context)
{
    SOFIPC_LOCK_THREAD* Info = (SOFIPC_LOCK_THREAD*)Context;
    LARGE_INTEGER Delay;

    if (!IntcDspRequestLockAcquire(Info->Lock, 0))
    {
        KeSetEvent(Info->Held, IO_NO_INCREMENT, FALSE);
        PsTerminateSystemThread(STATUS_UNSUCCESSFUL);
    }
    KeSetEvent(Info->Held, IO_NO_INCREMENT, FALSE);
    Delay.QuadPart = -10 * 100000LL;
    KeDelayExecutionThread(KernelMode, FALSE, &Delay);
    IntcDspRequestLockRelease(Info->Lock);
    PsTerminateSystemThread(STATUS_SUCCESS);
}

static
VOID
CheckRequestLock(VOID)
{
    INTCDSP_REQUEST_LOCK Lock;
    SOFIPC_LOCK_THREAD Info;
    KEVENT Held;
    PKTHREAD Thread;
    LARGE_INTEGER Before, After, Frequency;
    BOOLEAN Taken;

    IntcDspRequestLockInit(&Lock);
    ok(IntcDspRequestLockAcquire(&Lock, 0), "a free lock was not taken\n");
    Before = KeQueryPerformanceCounter(&Frequency);
    ok(!IntcDspRequestLockAcquire(&Lock, 20000), "the lock recursed on the thread that holds it\n");
    After = KeQueryPerformanceCounter(NULL);
    ok(After.QuadPart - Before.QuadPart >= 20 * Frequency.QuadPart / 1000, "the second acquire returned after %I64d ticks\n", After.QuadPart - Before.QuadPart);
    IntcDspRequestLockRelease(&Lock);
    ok(IntcDspRequestLockAcquire(&Lock, 0), "a released lock was not taken\n");
    IntcDspRequestLockRelease(&Lock);

    KeInitializeEvent(&Held, NotificationEvent, FALSE);
    Info.Lock = &Lock;
    Info.Held = &Held;
    Thread = KmtStartThread(LockHolder, &Info);
    if (Thread == NULL)
        return;
    KeWaitForSingleObject(&Held, Executive, KernelMode, FALSE, NULL);
    ok(!IntcDspRequestLockAcquire(&Lock, 20000), "the lock was taken while the other thread held it\n");
    Before = KeQueryPerformanceCounter(NULL);
    Taken = IntcDspRequestLockAcquire(&Lock, 5000000);
    After = KeQueryPerformanceCounter(NULL);
    ok(Taken, "the lock was not got after the other thread released it\n");
    ok(After.QuadPart - Before.QuadPart < 4 * Frequency.QuadPart, "the long wait took %I64d ticks\n", After.QuadPart - Before.QuadPart);
    IntcDspRequestLockRelease(&Lock);
    KmtFinishThread(Thread, NULL);
}

/* (g) two callers: the second waits its budget, fails busy and sends nothing; the transport then runs the self-test over the driver's real lock */
static
VOID
CheckTwoCallers(
    SOFIPC_HARNESS* H,
    const SOF_RI_IMAGE* Image)
{
    static INTCDSP_REQUEST_LOCK Lock;
    SOFIPC_REQUEST Request;
    UINT32 Index;

    SofIpcHarnessBoot(H, Image, NULL);
    SofIpcHarnessAttach(H, NULL, FALSE);
    H->LockHeld = TRUE;
    SofIpcHarnessRequest(&Request, SofIpcClassPipeline, Ipc4TransportSelfTest[0].Primary, 0);
    ok_eq_int(SofIpcRequest(&H->Ipc, &Request), SofIpcBusy);
    ok_eq_uint(H->Ipc.Last.ElapsedUs, 500000);
    ok_eq_uint(H->LastLockTimeout, 500000);
    ok_eq_uint(H->LockWaits, 1);
    ok_eq_uint(H->Ipc.Requests, 0);
    ok_eq_uint(H->Ipc.Link, SofIpcLinkReady);
    ok_eq_str(SofIpcStatusName(SofIpcBusy), "busy");
    H->LockHeld = FALSE;

    /* the driver's lock under the transport: every exchange of the self-test takes it and releases it */
    IntcDspRequestLockInit(&Lock);
    SofIpcHarnessBoot(H, Image, NULL);
    ok_eq_int(SofIpcInit(&H->Ipc, &SofIpcKernelLockOps, &Lock, H->Ctx, NULL, FALSE), SofIpcOk);
    for (Index = 0; Index < IPC4_TRANSPORT_SELF_TEST_EXCHANGES; Index++)
    {
        SofIpcHarnessRequest(&Request, SofIpcClassPipeline, Ipc4TransportSelfTest[Index].Primary, 0);
        ok_eq_int(SofIpcRequest(&H->Ipc, &Request), SofIpcOk);
    }
    ok_eq_uint(H->Ipc.Requests, IPC4_TRANSPORT_SELF_TEST_EXCHANGES);
    ok(IntcDspRequestLockAcquire(&Lock, 0), "the lock was left taken\n");
    IntcDspRequestLockRelease(&Lock);
}

/* (h) large configurations in blocks, written and read */
static
VOID
CheckLargeConfig(
    SOFIPC_HARNESS* H,
    const SOF_RI_IMAGE* Image)
{
    static UINT32 Data[FAKE_DSP_LARGE_CONFIG_BYTES / 4];
    static UINT8 Buffer[FAKE_DSP_LARGE_CONFIG_BYTES];
    SIZE_T Bytes;
    UINT32 Index, Block, Word;

    for (Word = 0; Word < sizeof(Data) / sizeof(Data[0]); Word++)
        Data[Word] = Word;

    for (Index = 0; Index < IPC4_TRANSPORT_LARGE_CASES; Index++)
    {
        const IPC4_TRANSPORT_LARGE_CASE* Case = &Ipc4TransportLarge[Index];

        SofIpcHarnessBoot(H, Image, NULL);
        SofIpcHarnessAttach(H, NULL, FALSE);
        ok_eq_int(SofIpcLargeConfigSet(&H->Ipc, IPC4_CASE_LARGE_MODULE, IPC4_CASE_LARGE_INSTANCE, IPC4_CASE_LARGE_PARAM, Data, Case->TotalBytes), SofIpcOk);
        ok_eq_uint(H->Dsp->LargeConfigBlocks, Case->Blocks);
        ok_eq_uint(H->Dsp->LargeConfigErrors, 0);
        ok_eq_uint(H->Dsp->LargeConfigBytes, Case->TotalBytes);
        ok_eq_uint(H->Ipc.Requests, Case->Blocks);
        ok(RtlCompareMemory(H->Dsp->LargeConfig, Data, Case->TotalBytes) == Case->TotalBytes, "the blocks did not carry the configuration\n");
        for (Block = 0; Block < Case->Blocks; Block++)
        {
            ok_eq_uint(H->Dsp->RequestPrimary[Block], IPC4_CASE_LARGE_PRIMARY | FWLOAD_IPC_BUSY);
            ok_eq_uint(H->Dsp->RequestExtension[Block], (IPC4_CASE_LARGE_PARAM << 20) | Case->Block[Block].SizeField | Case->Block[Block].Flags);
        }
    }

    for (Index = 0; Index < IPC4_TRANSPORT_READ_CASES; Index++)
    {
        const IPC4_TRANSPORT_READ_CASE* Case = &Ipc4TransportReads[Index];

        SofIpcHarnessBoot(H, Image, NULL);
        SofIpcHarnessAttach(H, NULL, FALSE);
        RtlZeroMemory(Buffer, sizeof(Buffer));
        H->Dsp->LargeConfigTotal = Case->TotalBytes;
        ok_eq_int(SofIpcLargeConfigGet(&H->Ipc, IPC4_CASE_LARGE_MODULE, IPC4_CASE_LARGE_INSTANCE, IPC4_CASE_LARGE_PARAM, Buffer, Case->Capacity, &Bytes), Case->Status);
        ok_eq_size(Bytes, Case->Bytes);
        ok_eq_uint(H->Ipc.Requests, Case->Blocks);
        ok_eq_uint(H->Ipc.Link, SofIpcLinkReady);
        if (Case->Status == SofIpcOk)
        {
            for (Word = 0; Word < Case->Bytes / 4; Word++)
            {
                if (((UINT32*)Buffer)[Word] != Word)
                    break;
            }
            ok_eq_uint(Word, Case->Bytes / 4);
        }
    }
}

/* (i) the poll policy */
static
VOID
CheckPollPolicy(
    SOFIPC_HARNESS* H,
    const SOF_RI_IMAGE* Image)
{
    SOFIPC_REQUEST Request;

    SofIpcHarnessBoot(H, Image, NULL);
    H->Dsp->Faults.IpcReplyUs = 100;
    SofIpcHarnessAttach(H, NULL, FALSE);
    SofIpcHarnessRequest(&Request, SofIpcClassPipeline, Ipc4TransportSelfTest[0].Primary, 0);
    ok_eq_int(SofIpcRequest(&H->Ipc, &Request), SofIpcOk);
    ok_eq_uint(H->Ipc.FastPolls, 5);
    ok_eq_uint(H->Ipc.SlowPolls, 0);
    ok_eq_ulonglong(H->Ipc.Last.ElapsedUs, 100);

    SofIpcHarnessBoot(H, Image, NULL);
    H->Dsp->Faults.IpcReplyUs = 3000;
    SofIpcHarnessAttach(H, NULL, FALSE);
    SofIpcHarnessRequest(&Request, SofIpcClassPipeline, Ipc4TransportSelfTest[0].Primary, 0);
    ok_eq_int(SofIpcRequest(&H->Ipc, &Request), SofIpcOk);
    ok_eq_uint(H->Ipc.FastPolls, SOFIPC_FAST_PHASE_US / SOFIPC_FAST_POLL_US);
    ok_eq_uint(H->Ipc.SlowPolls, 4);
    ok_eq_ulonglong(H->Ipc.Last.ElapsedUs, 3000);
}

/* The self-test's words as the codec builds them, and the names */
static
VOID
CheckWords(VOID)
{
    SOFIPC4_MSG Msg;

    SofIpc4PipelineCreate(&Msg, IPC4_CASE_PIPELINE_INSTANCE, 0, IPC4_CASE_PIPELINE_PAGES, 0, 0, 0);
    ok_eq_hex(Msg.Primary, Ipc4TransportSelfTest[0].Primary);
    ok_eq_hex(Msg.Extension, Ipc4TransportSelfTest[0].Extension);
    SofIpc4PipelineSetState(&Msg, IPC4_CASE_PIPELINE_INSTANCE, SOFIPC4_PIPELINE_STATE_PAUSED, 0);
    ok_eq_hex(Msg.Primary, Ipc4TransportSelfTest[1].Primary);
    SofIpc4PipelineSetState(&Msg, IPC4_CASE_PIPELINE_INSTANCE, SOFIPC4_PIPELINE_STATE_RUNNING, 0);
    ok_eq_hex(Msg.Primary, Ipc4TransportSelfTest[2].Primary);
    SofIpc4PipelineSetState(&Msg, IPC4_CASE_PIPELINE_INSTANCE, SOFIPC4_PIPELINE_STATE_RESET, 0);
    ok_eq_hex(Msg.Primary, Ipc4TransportSelfTest[4].Primary);
    SofIpc4PipelineDelete(&Msg, IPC4_CASE_PIPELINE_INSTANCE);
    ok_eq_hex(Msg.Primary, Ipc4TransportSelfTest[5].Primary);
    ok_eq_str(SofIpcTypeName(Ipc4TransportSelfTest[0].Primary), "create pipeline");
    ok_eq_str(SofIpcTypeName(Ipc4TransportSelfTest[5].Primary), "delete pipeline");
    ok_eq_str(SofIpcTypeName(0x44020003), "large config set");
    ok_eq_str(SofIpcStatusName(SofIpcTimeout), "reply timeout");
    ok_eq_str(SofIpcStatusName(SofIpcCrash), "firmware crash");
    ok_eq_str(SofIpcLinkName(SofIpcLinkFaulted), "faulted");
}

START_TEST(SofIpc)
{
    static UINT8 Image[FWLOAD_CASE_IMAGE_BYTES];
    SOFIPC_HARNESS* H;
    SOF_RI_MODULE Modules[4];
    SOF_RI_IMAGE Parsed;
    ULONG Index;

    H = (SOFIPC_HARNESS*)ExAllocatePoolWithTag(NonPagedPool, sizeof(*H), SOFIPC_TEST_TAG);
    if (H == NULL)
    {
        ok(FALSE, "allocation failed\n");
        return;
    }
    RtlZeroMemory(H, sizeof(*H));
    H->Dsp = (PFAKE_DSP)ExAllocatePoolWithTag(NonPagedPool, sizeof(FAKE_DSP), SOFIPC_TEST_TAG);
    H->Ctx = (PFWLOAD_CONTEXT)ExAllocatePoolWithTag(NonPagedPool, sizeof(FWLOAD_CONTEXT), SOFIPC_TEST_TAG);
    H->Virtual = ExAllocatePoolWithTag(NonPagedPool, FWLOAD_CASE_IMAGE_BYTES - FWLOAD_CASE_PAYLOAD_OFFSET, SOFIPC_TEST_TAG);
    if (H->Dsp == NULL || H->Ctx == NULL || H->Virtual == NULL)
    {
        ok(FALSE, "allocation failed\n");
        goto Cleanup;
    }
    H->Ops = *FakeDspOps();
    H->Ops.Read32 = HRead32;
    H->Ops.Write32 = HWrite32;
    H->Ops.ReadMemory = HReadMemory;
    H->Ops.NowUs = HNowUs;
    H->Ops.DelayUs = HDelayUs;
    H->Ops.StreamSubmit = HStreamSubmit;
    H->Ops.StreamTrigger = HStreamTrigger;
    H->Ops.StreamRelease = HStreamRelease;
    H->Ops.Lock = HLock;
    H->Ops.Unlock = HUnlock;
    H->Ops.Log = NULL;
    H->Segment.Address = 0x10000000ULL;
    H->Segment.Bytes = FWLOAD_CASE_IMAGE_BYTES - FWLOAD_CASE_PAYLOAD_OFFSET;
    FwloadCaseBuildImage(Image);
    ok_eq_int(SofRiParse(Image, FWLOAD_CASE_IMAGE_BYTES, Modules, 4, &Parsed), FwloadOk);
    /* the model boots a well-formed image only: every malformed variant of the shared cases is refused before any register is touched */
    for (Index = 0; Index < FwloadCaseFaultCount; Index++)
    {
        FwloadCaseBuildImage(Image);
        FwloadCaseApplyFault(Image, FwloadCaseFaults[Index].Fault);
        ok_eq_int(SofRiParse(Image, FWLOAD_CASE_IMAGE_BYTES, Modules, 4, &Parsed), FwloadCaseFaults[Index].Status);
    }
    FwloadCaseBuildImage(Image);
    ok_eq_int(SofRiParse(Image, FWLOAD_CASE_IMAGE_BYTES, Modules, 4, &Parsed), FwloadOk);

    CheckWords();
    CheckRequestLock();
    CheckRegisterOrderPin(H, &Parsed);
    CheckTimeouts(H, &Parsed);
    CheckStatus(H, &Parsed);
    CheckCrash(H, &Parsed);
    CheckNotifications(H, &Parsed);
    CheckInterruptMode(H, &Parsed);
    CheckTwoCallers(H, &Parsed);
    CheckLargeConfig(H, &Parsed);
    CheckPollPolicy(H, &Parsed);

Cleanup:
    if (H->Virtual) ExFreePoolWithTag(H->Virtual, SOFIPC_TEST_TAG);
    if (H->Ctx) ExFreePoolWithTag(H->Ctx, SOFIPC_TEST_TAG);
    if (H->Dsp) ExFreePoolWithTag(H->Dsp, SOFIPC_TEST_TAG);
    ExFreePoolWithTag(H, SOFIPC_TEST_TAG);
}
