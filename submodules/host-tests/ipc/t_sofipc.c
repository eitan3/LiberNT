/*
 * PROJECT:     LiberNT host-native tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     The IPC4 transport over a DSP model: the loader's register order for the configuration query, a budget per message class, a non-success status, a crash notification, notifications beside a reply, the storm guard and masking in interrupt mode, two callers, large configurations in blocks, the poll policy, and the link's states; under ASan and UBSan
 * COPYRIGHT:   Copyright 2026 LiberNT contributors
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "shim.h"

#include "fake_dsp.h"
#include "fwload_cases.h"
#include "sofipc.h"
#include "fake_bus.h"
#include <softplg.h>
#include <ipc4model.h>
#include "stream.h"
#include "ipc4_transport_cases.h"

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
        unsigned long long _a = (unsigned long long)(a), _b = (unsigned long long)(b); \
        ++Checks; \
        if (_a != _b) { \
            fprintf(stderr, "%s:%d: FAIL: %s == %s: %llx != %llx\n", __FILE__, __LINE__, #a, #b, _a, _b); \
            ++Failures; \
        } \
    } while (0)

#define CHECK_STR(a, b) \
    do { \
        ++Checks; \
        if (strcmp((a), (b)) != 0) { \
            fprintf(stderr, "%s:%d: FAIL: %s == %s: \"%s\" != \"%s\"\n", __FILE__, __LINE__, #a, #b, (a), (b)); \
            ++Failures; \
        } \
    } while (0)

#define HARNESS_ACCESS_ENTRIES  2048
#define HARNESS_BDL_ENTRIES     256
#define HARNESS_DELAY_LOG       128
#define HARNESS_SHAPE_ENTRIES   64

typedef struct _HARNESS
{
    FAKE_DSP* Dsp;
    FWLOAD_CONTEXT* Ctx;
    SOFIPC_CONTEXT Ipc;
    /* the model's operations with this harness as their context, so that a delay can raise the line and run the case's hook */
    FWLOAD_OPS Ops;
    UINT8* Virtual;
    FWLOAD_DMA_SEGMENT Segment;
    FWLOAD_BDL_ENTRY Bdl[HARNESS_BDL_ENTRIES];
    FAKE_DSP_ACCESS Access[HARNESS_ACCESS_ENTRIES];
    /* the request lock as a model of the driver's: taken by the request in progress; a second caller's wait advances the clock by its budget when LockWaitAdvances is set */
    BOOLEAN LockHeld;
    BOOLEAN LockWaitAdvances;
    UINT32 LastLockTimeout;
    UINT32 LockAcquires;
    UINT32 LockWaits;
    UINT32 LockUnlocks;
    /* the lock acquisitions so far, and the one that answers busy as a second caller would find the lock taken: 0 never */
    UINT32 LockAttempts;
    UINT32 LockBusyAtRequest;
    /* the line raises this many interrupts per delay while a message is pending; IpcArmed picks the transport's service routine over the loader's */
    UINT32 InterruptsPerDelay;
    BOOLEAN IpcArmed;
    BOOLEAN ClockStalled;
    UINT32 Delays;
    UINT32 DelayLog[HARNESS_DELAY_LOG];
    /* a request a case sends from inside another one's wait, at the delay NestedAtDelay */
    BOOLEAN NestedRun;
    UINT32 NestedAtDelay;
    SOFIPC_REQUEST Nested;
    SOFIPC_STATUS NestedStatus;
    UINT64 NestedElapsed;
} HARNESS;

static const SOFIPC_OPS HarnessIpcOps;

static
BOOLEAN
HarnessInterrupt(
    HARNESS* H);

static
UINT32
HRead32(
    PVOID Context,
    UINT32 Bar,
    UINT32 Offset)
{
    return FakeDspOps()->Read32(((HARNESS*)Context)->Dsp, Bar, Offset);
}

static
VOID
HWrite32(
    PVOID Context,
    UINT32 Bar,
    UINT32 Offset,
    UINT32 Value)
{
    FakeDspOps()->Write32(((HARNESS*)Context)->Dsp, Bar, Offset, Value);
}

static
BOOLEAN
HReadMemory(
    PVOID Context,
    UINT32 Offset,
    UINT8* Buffer,
    SIZE_T Bytes)
{
    return FakeDspOps()->ReadMemory(((HARNESS*)Context)->Dsp, Offset, Buffer, Bytes);
}

static
UINT64
HNowUs(
    PVOID Context)
{
    return ((HARNESS*)Context)->ClockStalled ? 0 : FakeDspOps()->NowUs(((HARNESS*)Context)->Dsp);
}

static
VOID
HDelayUs(
    PVOID Context,
    UINT32 Microseconds)
{
    HARNESS* H = (HARNESS*)Context;
    UINT32 Index;

    FakeDspOps()->DelayUs(H->Dsp, Microseconds);
    H->Delays++;
    if (H->Delays <= HARNESS_DELAY_LOG)
        H->DelayLog[H->Delays - 1] = Microseconds;
    for (Index = 0; Index < H->InterruptsPerDelay && FakeDspMessagePending(H->Dsp); Index++)
        HarnessInterrupt(H);
    if (H->NestedRun && H->Delays == H->NestedAtDelay)
    {
        UINT64 Start = FakeDspOps()->NowUs(H->Dsp);

        H->NestedRun = FALSE;
        H->NestedStatus = SofIpcRequest(&H->Ipc, &H->Nested);
        H->NestedElapsed = FakeDspOps()->NowUs(H->Dsp) - Start;
    }
}

static
FWLOAD_STATUS
HStreamSubmit(
    PVOID Context,
    PFWLOAD_CL_DMA Dma)
{
    return FakeDspOps()->StreamSubmit(((HARNESS*)Context)->Dsp, Dma);
}

static
FWLOAD_STATUS
HStreamTrigger(
    PVOID Context,
    BOOLEAN Start)
{
    return FakeDspOps()->StreamTrigger(((HARNESS*)Context)->Dsp, Start);
}

static
VOID
HStreamRelease(
    PVOID Context)
{
    FakeDspOps()->StreamRelease(((HARNESS*)Context)->Dsp);
}

static
VOID
HLock(
    PVOID Context)
{
    FakeDspOps()->Lock(((HARNESS*)Context)->Dsp);
}

static
VOID
HUnlock(
    PVOID Context)
{
    FakeDspOps()->Unlock(((HARNESS*)Context)->Dsp);
}

/* The bus driver's interrupt service routine: the DSP callback under the interrupt lock, as hda_interrupt calls it; the loader's routine during a boot, the transport's once the transport owns the line */
static
BOOLEAN
HarnessInterrupt(
    HARNESS* H)
{
    BOOLEAN Claimed;

    FakeDspOps()->Lock(H->Dsp);
    Claimed = H->IpcArmed ? SofIpcInterruptService(&H->Ipc) : FwloadInterruptService(H->Ctx);
    FakeDspOps()->Unlock(H->Dsp);
    return Claimed;
}

static
BOOLEAN
HIpcWriteMemory(
    PVOID Context,
    UINT32 Offset,
    const UINT8* Buffer,
    SIZE_T Bytes)
{
    return FakeDspWriteMemory(((HARNESS*)Context)->Dsp, Offset, Buffer, Bytes);
}

static
BOOLEAN
HIpcRequestLock(
    PVOID Context,
    UINT32 TimeoutUs)
{
    HARNESS* H = (HARNESS*)Context;

    H->LockAttempts++;
    if (H->LockBusyAtRequest != 0 && H->LockAttempts == H->LockBusyAtRequest)
    {
        H->LockWaits++;
        H->LastLockTimeout = TimeoutUs;
        return FALSE;
    }
    if (!H->LockHeld)
    {
        H->LockHeld = TRUE;
        H->LockAcquires++;
        return TRUE;
    }
    H->LockWaits++;
    H->LastLockTimeout = TimeoutUs;
    if (H->LockWaitAdvances)
        FakeDspOps()->DelayUs(H->Dsp, TimeoutUs);
    return FALSE;
}

static
VOID
HIpcRequestUnlock(
    PVOID Context)
{
    HARNESS* H = (HARNESS*)Context;

    H->LockHeld = FALSE;
    H->LockUnlocks++;
}

static const SOFIPC_OPS HarnessIpcOps =
{
    HIpcWriteMemory,
    HIpcRequestLock,
    HIpcRequestUnlock
};

/* A transport without a request lock: the caller serializes, and a re-entrant request must still be refused */
static const SOFIPC_OPS HarnessIpcOpsUnlocked =
{
    HIpcWriteMemory,
    NULL,
    NULL
};

static
void
HarnessInit(
    HARNESS* H)
{
    const FWLOAD_OPS* Model = FakeDspOps();

    memset(H, 0, sizeof(*H));
    H->Dsp = (FAKE_DSP*)calloc(1, sizeof(FAKE_DSP));
    H->Ctx = (FWLOAD_CONTEXT*)calloc(1, sizeof(FWLOAD_CONTEXT));
    H->Virtual = (UINT8*)calloc(1, FWLOAD_CASE_IMAGE_BYTES);
    if (H->Dsp == NULL || H->Ctx == NULL || H->Virtual == NULL)
        exit(2);
    H->Ops = *Model;
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
}

/* One boot of the model from the synthetic image, the loader's context re-initialized in place as the driver does */
static
void
HarnessBoot(
    HARNESS* H,
    const SOF_RI_IMAGE* Image,
    const FAKE_DSP_FAULTS* Faults)
{
    FWLOAD_CL_DMA Dma;

    FakeDspInit(H->Dsp, FwloadPlatformArlS(), Faults);
    FakeDspAccessBegin(H->Dsp, H->Access, HARNESS_ACCESS_ENTRIES);
    memset(&Dma, 0, sizeof(Dma));
    Dma.Virtual = H->Virtual;
    Dma.Capacity = (SIZE_T)H->Segment.Bytes;
    Dma.Segment = &H->Segment;
    Dma.SegmentCount = 1;
    Dma.Bdl = H->Bdl;
    Dma.BdlCapacity = HARNESS_BDL_ENTRIES;
    Dma.BdlBytes = sizeof(H->Bdl);
    Dma.StreamTag = FWLOAD_CL_STREAM_TAG;
    Dma.StreamFormat = FWLOAD_CL_STREAM_FORMAT;
    FwloadInitContext(H->Ctx, &H->Ops, H, FwloadPlatformArlS(), Image, &Dma, NULL);
    H->IpcArmed = FALSE;
    H->InterruptsPerDelay = 0;
    H->ClockStalled = FALSE;
    H->NestedRun = FALSE;
    H->NestedStatus = SofIpcOk;
    H->NestedElapsed = 0;
    H->LockWaitAdvances = FALSE;
    H->Delays = 0;
    H->LockHeld = FALSE;
    H->LockAcquires = 0;
    H->LockWaits = 0;
    H->LockUnlocks = 0;
    H->LockAttempts = 0;
    H->LockBusyAtRequest = 0;
    memset(&H->Ipc, 0, sizeof(H->Ipc));
}

/* The transport's own counts, once the transport is attached: the delays from here on are its polls */
static
void
HarnessTransportReset(
    HARNESS* H)
{
    H->Delays = 0;
    H->NestedRun = FALSE;
    H->NestedStatus = SofIpcOk;
    H->NestedElapsed = 0;
}

static
void
HarnessStart(
    HARNESS* H,
    const SOF_RI_IMAGE* Image,
    const FAKE_DSP_FAULTS* Faults)
{
    HarnessBoot(H, Image, Faults);
    CHECK_EQ(FwloadBoot(H->Ctx), FwloadOk);
    CHECK_EQ(H->Ctx->State, FwloadStateReady);
    CHECK_EQ(FakeDspIpcErrors(H->Dsp), 0);
}

static
void
HarnessAttach(
    HARNESS* H,
    const UINT32* TimeoutUs,
    BOOLEAN UseInterrupts)
{
    CHECK_EQ(SofIpcInit(&H->Ipc, &HarnessIpcOps, H, H->Ctx, TimeoutUs, UseInterrupts), SofIpcOk);
    CHECK_EQ(H->Ipc.Link, SofIpcLinkReady);
    H->IpcArmed = UseInterrupts;
    HarnessTransportReset(H);
}

static
void
HarnessRequest(
    SOFIPC_REQUEST* Request,
    SOFIPC_CLASS Class,
    UINT32 Primary,
    UINT32 Extension)
{
    memset(Request, 0, sizeof(*Request));
    Request->Class = Class;
    Request->Msg.Primary = Primary;
    Request->Msg.Extension = Extension;
}

/* Whether two accesses are the same step: a poll of the reply or the target register is compared by its busy or done bit only, as the header beside the bit is the register's history */
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

/* An exchange reduced to its steps: a poll cycle that repeats the one before it is not a step of its own */
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
void
ShapePrint(
    const char* Tag,
    const FAKE_DSP_ACCESS* Shape,
    UINT32 Count)
{
    static const char* const Ops[] = { "read", "write", "read-memory", "write-memory" };
    UINT32 Index;

    printf("%s:", Tag);
    for (Index = 0; Index < Count; Index++)
        printf(" %s[bar %u +0x%x]=0x%x", Ops[Shape[Index].Op], Shape[Index].Bar, Shape[Index].Offset, Shape[Index].Value);
    printf("\n");
}

static
UINT32
ShapeOfExchange(
    HARNESS* H,
    FAKE_DSP_ACCESS* Out)
{
    FAKE_DSP_ACCESS Slice[HARNESS_SHAPE_ENTRIES];
    UINT32 Count;

    CHECK(H->Dsp->ExchangeStart <= H->Dsp->ExchangeEnd && H->Dsp->ExchangeEnd < H->Dsp->AccessCount);
    Count = H->Dsp->ExchangeEnd - H->Dsp->ExchangeStart + 1;
    CHECK(Count <= HARNESS_SHAPE_ENTRIES);
    if (Count > HARNESS_SHAPE_ENTRIES)
        Count = HARNESS_SHAPE_ENTRIES;
    memcpy(Slice, H->Access + H->Dsp->ExchangeStart, Count * sizeof(Slice[0]));
    return Shape(Slice, Count, Out);
}

/* (a) The register-order pin: the transport's access log for the configuration query equals the loader's, step for step */
static
void
CheckRegisterOrderPin(
    HARNESS* H,
    const SOF_RI_IMAGE* Image)
{
    FAKE_DSP_ACCESS Loader[HARNESS_SHAPE_ENTRIES], Transport[HARNESS_SHAPE_ENTRIES];
    UINT32 LoaderCount, TransportCount, Index;
    SOFIPC_REQUEST Request;
    SOFIPC4_MSG Msg;
    static UINT8 Reply[FWLOAD_MAILBOX_BYTES];

    HarnessStart(H, Image, NULL);
    LoaderCount = ShapeOfExchange(H, Loader);
    CHECK_EQ(LoaderCount, 8);
    FakeDspAccessBegin(H->Dsp, H->Access, HARNESS_ACCESS_ENTRIES);
    HarnessAttach(H, NULL, FALSE);
    SofIpc4ModuleConfig(&Msg, FWLOAD_BASEFW_MODULE_ID, FWLOAD_BASEFW_INSTANCE_ID, FWLOAD_PARAM_ID_FW_CONFIG << FWLOAD_PARAM_ID_SHIFT, TRUE);
    HarnessRequest(&Request, SofIpcClassModule, Msg.Primary, Msg.Extension);
    Request.ReplyBuffer = Reply;
    Request.ReplyBytes = sizeof(Reply);
    CHECK_EQ(SofIpcRequest(&H->Ipc, &Request), SofIpcOk);
    TransportCount = ShapeOfExchange(H, Transport);
    CHECK_EQ(TransportCount, LoaderCount);
    for (Index = 0; Index < LoaderCount && Index < TransportCount; Index++)
    {
        CHECK(SameAccess(&Loader[Index], &Transport[Index]));
    }
    if (Failures != 0)
    {
        ShapePrint("loader", Loader, LoaderCount);
        ShapePrint("transport", Transport, TransportCount);
    }
    /* the steps by name: extension, primary with the busy bit, the two polls, the reply's header and extension, its payload, the acknowledgement */
    CHECK_EQ(Transport[0].Op, FAKE_DSP_ACCESS_WRITE32);
    CHECK_EQ(Transport[0].Offset, FwloadPlatformArlS()->IpcRequestData);
    CHECK_EQ(Transport[0].Value, FWLOAD_PARAM_ID_FW_CONFIG << FWLOAD_PARAM_ID_SHIFT);
    CHECK_EQ(Transport[1].Op, FAKE_DSP_ACCESS_WRITE32);
    CHECK_EQ(Transport[1].Offset, FwloadPlatformArlS()->IpcRequest);
    CHECK_EQ(Transport[1].Value, 0xC1000000);
    CHECK_EQ(Transport[2].Op, FAKE_DSP_ACCESS_READ32);
    CHECK_EQ(Transport[2].Offset, FwloadPlatformArlS()->IpcReply);
    CHECK_EQ(Transport[2].Value & FWLOAD_IPC_DONE, 0);
    CHECK_EQ(Transport[3].Offset, FwloadPlatformArlS()->IpcTarget);
    CHECK_EQ(Transport[4].Value, 0xE1000000);
    CHECK_EQ(Transport[5].Offset, FwloadPlatformArlS()->IpcTargetData);
    CHECK_EQ(Transport[6].Op, FAKE_DSP_ACCESS_READMEMORY);
    CHECK_EQ(Transport[6].Offset, FwloadPlatformArlS()->UplinkOffset);
    CHECK_EQ(Transport[6].Value, FWLOAD_MAILBOX_BYTES);
    CHECK_EQ(Transport[7].Op, FAKE_DSP_ACCESS_WRITE32);
    CHECK_EQ(Transport[7].Offset, FwloadPlatformArlS()->IpcReply);
    CHECK_EQ(Transport[7].Value, FWLOAD_IPC_DONE);
    /* the reply is the loader's reply: status 0 and the same tuples */
    CHECK_EQ(H->Ipc.Last.ReplyPrimary, 0x61000000);
    CHECK_EQ(H->Ipc.Last.Status, SofIpc4Success);
    CHECK_EQ(memcmp(Reply, H->Ctx->Mailbox, FWLOAD_MAILBOX_BYTES), 0);
    CHECK_EQ(H->Ipc.Requests, 1);
    CHECK_EQ(H->Ipc.Replies, 1);
    CHECK_EQ(H->Dsp->IpcRequestErrors, 0);
    CHECK_EQ(H->Dsp->UnexpectedAccesses, 0);
    CHECK_EQ(FakeDspIpcErrors(H->Dsp), 0);
}

/* (b) A budget per message class, on the injected clock; a stalled clock ends the wait by its poll bound */
static
void
CheckTimeouts(
    HARNESS* H,
    const SOF_RI_IMAGE* Image)
{
    UINT32 Timeouts[SofIpcClassCount];
    SOFIPC_REQUEST Request;
    UINT32 Index, Class;

    for (Index = 0; Index < IPC4_TRANSPORT_TIMEOUT_CASES; Index++)
    {
        const IPC4_TRANSPORT_TIMEOUT_CASE* Case = &Ipc4TransportTimeouts[Index];

        HarnessStart(H, Image, NULL);
        SofIpcDefaultTimeouts(Timeouts, SOFIPC_DEFAULT_TIMEOUT_MS);
        Timeouts[Case->Class] = Case->TimeoutUs;
        HarnessAttach(H, Timeouts, FALSE);
        H->Dsp->Faults.NeverReplyForType = Case->Type;
        CHECK_EQ(SofIpcClassOf(Case->Primary), Case->Class);
        HarnessRequest(&Request, Case->Class, Case->Primary, Case->Extension);
        CHECK_EQ(SofIpcRequest(&H->Ipc, &Request), SofIpcTimeout);
        CHECK(H->Ipc.Last.ElapsedUs >= Case->TimeoutUs && H->Ipc.Last.ElapsedUs < Case->TimeoutUs + SOFIPC_SLOW_POLL_US);
        CHECK_EQ(H->Ipc.Link, SofIpcLinkFaulted);
        CHECK_EQ(H->Ipc.Failure, SofIpcTimeout);
        CHECK_EQ(H->Ipc.Last.Outcome, SofIpcTimeout);
        CHECK_EQ(H->Ipc.Requests, 1);
        CHECK_EQ(H->Ipc.Replies, 0);
        CHECK_EQ(H->Ipc.Timeouts, 1);
        CHECK_EQ(H->Ipc.Faults, 1);
        CHECK_EQ(H->Ipc.Last.RequestPrimary, Case->Primary | FWLOAD_IPC_BUSY);
        CHECK_EQ(H->Ipc.Last.ReplyPrimary, 0);
        /* nothing is retried, and a faulted link sends nothing more */
        HarnessRequest(&Request, Case->Class, Case->Primary, Case->Extension);
        CHECK_EQ(SofIpcRequest(&H->Ipc, &Request), SofIpcBadState);
        CHECK_EQ(H->Ipc.Requests, 1);
        CHECK_EQ(H->Dsp->TransportRequests, 1);
        CHECK_EQ(H->Dsp->UnexpectedAccesses, 0);
    }
    SofIpcDefaultTimeouts(Timeouts, 250);
    for (Class = 0; Class < SofIpcClassCount; Class++)
        CHECK_EQ(Timeouts[Class], 250000);
    /* a zero in the caller's table leaves the plan's default for that class */
    HarnessStart(H, Image, NULL);
    SofIpcDefaultTimeouts(Timeouts, 0);
    Timeouts[SofIpcClassModule] = 70000;
    HarnessAttach(H, Timeouts, FALSE);
    CHECK_EQ(H->Ipc.TimeoutUs[SofIpcClassPipeline], 500000);
    CHECK_EQ(H->Ipc.TimeoutUs[SofIpcClassModule], 70000);
    CHECK_EQ(H->Ipc.TimeoutUs[SofIpcClassLargeConfig], 500000);

    /* a clock that does not advance: the poll bound ends the wait, at twice the fast polls the budget needs */
    HarnessStart(H, Image, NULL);
    SofIpcDefaultTimeouts(Timeouts, SOFIPC_DEFAULT_TIMEOUT_MS);
    Timeouts[SofIpcClassPipeline] = 200;
    HarnessAttach(H, Timeouts, FALSE);
    H->Dsp->Faults.NeverReplyForType = SofIpc4GlbSetPipelineState;
    H->ClockStalled = TRUE;
    HarnessRequest(&Request, SofIpcClassPipeline, Ipc4TransportSelfTest[1].Primary, 0);
    CHECK_EQ(SofIpcRequest(&H->Ipc, &Request), SofIpcClockStalled);
    CHECK_EQ(H->Ipc.FastPolls, 20);
    CHECK_EQ(H->Ipc.SlowPolls, 0);
    CHECK_EQ(H->Ipc.Link, SofIpcLinkFaulted);

    /* a budget shorter than one poll is one poll, then the deadline */
    HarnessStart(H, Image, NULL);
    SofIpcDefaultTimeouts(Timeouts, SOFIPC_DEFAULT_TIMEOUT_MS);
    Timeouts[SofIpcClassPipeline] = 1;
    HarnessAttach(H, Timeouts, FALSE);
    H->Dsp->Faults.NeverReplyForType = SofIpc4GlbSetPipelineState;
    HarnessRequest(&Request, SofIpcClassPipeline, Ipc4TransportSelfTest[1].Primary, 0);
    CHECK_EQ(SofIpcRequest(&H->Ipc, &Request), SofIpcTimeout);
    CHECK_EQ(H->Ipc.FastPolls, 1);
    /* a budget of zero is one look at the registers and no poll */
    HarnessStart(H, Image, NULL);
    SofIpcDefaultTimeouts(Timeouts, SOFIPC_DEFAULT_TIMEOUT_MS);
    Timeouts[SofIpcClassPipeline] = 1;
    HarnessAttach(H, Timeouts, FALSE);
    H->Ipc.TimeoutUs[SofIpcClassPipeline] = 0;
    H->Dsp->Faults.NeverReplyForType = SofIpc4GlbSetPipelineState;
    HarnessRequest(&Request, SofIpcClassPipeline, Ipc4TransportSelfTest[1].Primary, 0);
    CHECK_EQ(SofIpcRequest(&H->Ipc, &Request), SofIpcTimeout);
    CHECK_EQ(H->Ipc.FastPolls, 0);
    CHECK_EQ(H->Ipc.SlowPolls, 0);
}

/* (c) A reply whose status is not a success faults the link and is acknowledged; so does a reply to another message */
static
void
CheckStatus(
    HARNESS* H,
    const SOF_RI_IMAGE* Image)
{
    SOFIPC_REQUEST Request;
    SOFIPC4_MSG Msg;

    HarnessStart(H, Image, NULL);
    HarnessAttach(H, NULL, FALSE);
    H->Dsp->Faults.IpcStatusForType = SofIpc4GlbSetPipelineState;
    H->Dsp->Faults.IpcStatusValue = SofIpc4BadState;
    HarnessRequest(&Request, SofIpcClassPipeline, Ipc4TransportSelfTest[0].Primary, 0);
    CHECK_EQ(SofIpcRequest(&H->Ipc, &Request), SofIpcOk);
    HarnessRequest(&Request, SofIpcClassPipeline, Ipc4TransportSelfTest[1].Primary, 0);
    CHECK_EQ(SofIpcRequest(&H->Ipc, &Request), SofIpcFwStatus);
    CHECK_EQ(H->Ipc.Last.Status, SofIpc4BadState);
    CHECK_EQ(H->Ipc.Last.ReplyPrimary & (0xF0000000 | SOFIPC4_MSG_TGT_BIT), 0x30000000);
    CHECK_EQ(H->Ipc.Last.ReplyPrimary & SOFIPC4_PRIMARY_PAYLOAD_MASK, SofIpc4BadState);
    CHECK_EQ(H->Ipc.Link, SofIpcLinkFaulted);
    CHECK_EQ(H->Ipc.Failure, SofIpcFwStatus);
    CHECK_EQ(H->Ipc.Faults, 1);
    CHECK_EQ(H->Ipc.Requests, 2);
    CHECK_EQ(H->Ipc.Replies, 2);
    /* the refused reply was acknowledged: the model holds none pending */
    CHECK_EQ(FakeDspIpcErrors(H->Dsp), 0);
    HarnessRequest(&Request, SofIpcClassPipeline, Ipc4TransportSelfTest[2].Primary, 0);
    CHECK_EQ(SofIpcRequest(&H->Ipc, &Request), SofIpcBadState);
    CHECK_EQ(H->Ipc.Requests, 2);
    /* the words of the driver's line */
    CHECK_STR(SofIpcTypeName(H->Ipc.Last.RequestPrimary), "set pipeline state");
    CHECK_STR(SofIpc4StatusName(H->Ipc.Last.Status), "bad state");
    CHECK_STR(SofIpcStatusName(SofIpcFwStatus), "firmware status");

    /* a reply of another type than the request */
    HarnessStart(H, Image, NULL);
    HarnessAttach(H, NULL, FALSE);
    H->Dsp->Faults.ReplyTypeShift = 1;
    HarnessRequest(&Request, SofIpcClassPipeline, Ipc4TransportSelfTest[0].Primary, 0);
    CHECK_EQ(SofIpcRequest(&H->Ipc, &Request), SofIpcBadReply);
    CHECK_EQ(H->Ipc.Link, SofIpcLinkFaulted);
    /* the reply came, with the shifted type, the reply bit set and the target bit clear as the request's */
    CHECK_EQ((H->Ipc.Last.ReplyPrimary >> SOFIPC4_TYPE_SHIFT) & SOFIPC4_TYPE_MASK, SofIpc4GlbCreatePipeline + 1);
    CHECK_EQ(H->Ipc.Last.ReplyPrimary & (SOFIPC4_RSP_BIT | SOFIPC4_MSG_TGT_BIT), SOFIPC4_RSP_BIT);
    CHECK_EQ(H->Ipc.Replies, 1);
    CHECK_EQ(FakeDspIpcErrors(H->Dsp), 0);

    /* a message the firmware does not know comes back as its status, not as a transport failure of its own */
    HarnessStart(H, Image, NULL);
    HarnessAttach(H, NULL, FALSE);
    SofIpc4InitRequest(&Msg, 26, SOFIPC4_TARGET_FW_GEN_MSG);
    HarnessRequest(&Request, SofIpcClassModule, Msg.Primary, 0);
    CHECK_EQ(SofIpcRequest(&H->Ipc, &Request), SofIpcFwStatus);
    CHECK_EQ(H->Ipc.Last.Status, SofIpc4ErrorInvalidParam);
    CHECK_STR(SofIpc4StatusName(SofIpc4ErrorInvalidParam), "invalid parameter");
    CHECK_EQ(H->Dsp->IpcRequestErrors, 1);
    CHECK_STR(SofIpcTypeName(Msg.Primary), "internal message");
}

/* (d) A crash notification during a request ends the exchange at once and faults the link; found while nothing was asked for, it faults it too */
static
void
CheckCrash(
    HARNESS* H,
    const SOF_RI_IMAGE* Image)
{
    SOFIPC_REQUEST Request;
    SOFIPC_NOTIFICATION Notification;

    HarnessStart(H, Image, NULL);
    HarnessAttach(H, NULL, FALSE);
    H->Dsp->Faults.CrashDuringRequest = TRUE;
    HarnessRequest(&Request, SofIpcClassPipeline, Ipc4TransportSelfTest[0].Primary, 0);
    CHECK_EQ(SofIpcRequest(&H->Ipc, &Request), SofIpcCrash);
    CHECK_EQ(H->Ipc.Link, SofIpcLinkFaulted);
    CHECK_EQ(H->Ipc.Failure, SofIpcCrash);
    CHECK_EQ(H->Ipc.Requests, 1);
    CHECK_EQ(H->Ipc.Replies, 0);
    CHECK_EQ(H->Ipc.Notifications, 1);
    CHECK_EQ(H->Ipc.RingCount, 1);
    CHECK(H->Ipc.Last.ElapsedUs < 100);
    CHECK(SofIpcTakeNotification(&H->Ipc, &Notification));
    CHECK_EQ(Notification.Type, SOFIPC4_NOTIFY_EXCEPTION_CAUGHT);
    CHECK_EQ(Notification.Primary, (SofIpc4GlbNotification << SOFIPC4_TYPE_SHIFT) | (SOFIPC4_NOTIFY_EXCEPTION_CAUGHT << SOFIPC4_NOTIFICATION_TYPE_SHIFT));
    CHECK(!SofIpcTakeNotification(&H->Ipc, &Notification));
    /* the crash notification was acknowledged before the link was given up: no message is left to the firmware */
    CHECK_EQ(FakeDspIpcErrors(H->Dsp), 0);
    CHECK_EQ(H->Dsp->UnexpectedAccesses, 0);

    HarnessStart(H, Image, NULL);
    HarnessAttach(H, NULL, FALSE);
    H->Dsp->IpcTarget = FWLOAD_IPC_BUSY | (SofIpc4GlbNotification << SOFIPC4_TYPE_SHIFT) |
        (SOFIPC4_NOTIFY_WATCHDOG_TIMEOUT << SOFIPC4_NOTIFICATION_TYPE_SHIFT);
    H->Dsp->IpcTargetAck |= FWLOAD_IPC_BUSY;
    H->Dsp->NotificationPending = TRUE;
    CHECK(!SofIpcPollNotifications(&H->Ipc));
    CHECK_EQ(H->Ipc.Link, SofIpcLinkFaulted);
    CHECK_EQ(H->Ipc.Failure, SofIpcCrash);
    CHECK_EQ(FakeDspIpcErrors(H->Dsp), 0);
}

/* (e) Notifications queued while a request is outstanding are consumed and kept, the reply is still waited for; those posted beside the reply are taken by the next poll */
static
void
CheckNotifications(
    HARNESS* H,
    const SOF_RI_IMAGE* Image)
{
    SOFIPC_REQUEST Request;
    SOFIPC_NOTIFICATION Notification;
    UINT32 Index;

    HarnessStart(H, Image, NULL);
    HarnessAttach(H, NULL, FALSE);
    H->Dsp->Faults.NotifyDuringRequest = TRUE;
    for (Index = 0; Index < IPC4_TRANSPORT_SELF_TEST_EXCHANGES; Index++)
    {
        HarnessRequest(&Request, SofIpcClassPipeline, Ipc4TransportSelfTest[Index].Primary, 0);
        CHECK_EQ(SofIpcRequest(&H->Ipc, &Request), SofIpcOk);
        CHECK_EQ(H->Ipc.Last.Status, SofIpc4Success);
    }
    CHECK_EQ(H->Ipc.Notifications, IPC4_TRANSPORT_SELF_TEST_EXCHANGES);
    CHECK_EQ(H->Ipc.RingCount, IPC4_TRANSPORT_SELF_TEST_EXCHANGES);
    CHECK_EQ(H->Ipc.Replies, IPC4_TRANSPORT_SELF_TEST_EXCHANGES);
    for (Index = 0; Index < IPC4_TRANSPORT_SELF_TEST_EXCHANGES; Index++)
    {
        CHECK(SofIpcTakeNotification(&H->Ipc, &Notification));
        CHECK_EQ(Notification.Type, SOFIPC4_NOTIFY_LOG_BUFFER_STATUS);
    }
    CHECK(!SofIpcTakeNotification(&H->Ipc, &Notification));
    CHECK_EQ(H->Ipc.Link, SofIpcLinkReady);
    CHECK_EQ(FakeDspIpcErrors(H->Dsp), 0);

    /* beside the reply: the reply is taken first, the notification stays for the next poll */
    HarnessStart(H, Image, NULL);
    HarnessAttach(H, NULL, FALSE);
    H->Dsp->Faults.NotifyBesideReply = TRUE;
    HarnessRequest(&Request, SofIpcClassPipeline, Ipc4TransportSelfTest[0].Primary, 0);
    CHECK_EQ(SofIpcRequest(&H->Ipc, &Request), SofIpcOk);
    CHECK_EQ(H->Ipc.Notifications, 0);
    CHECK_EQ(H->Ipc.Link, SofIpcLinkReady);
    CHECK_EQ(FakeDspIpcErrors(H->Dsp), FAKE_DSP_IPC_NOTIFY_LEFT_BUSY);
    CHECK(SofIpcPollNotifications(&H->Ipc));
    CHECK_EQ(H->Ipc.Notifications, 1);
    CHECK_EQ(H->Ipc.RingCount, 1);
    CHECK_EQ(FakeDspIpcErrors(H->Dsp), 0);
    /* the next request takes one just the same before it sends anything */
    H->Dsp->IpcTarget = FWLOAD_IPC_BUSY | (SofIpc4GlbNotification << SOFIPC4_TYPE_SHIFT) |
        (SOFIPC4_NOTIFY_LOG_BUFFER_STATUS << SOFIPC4_NOTIFICATION_TYPE_SHIFT);
    H->Dsp->IpcTargetAck |= FWLOAD_IPC_BUSY;
    H->Dsp->NotificationPending = TRUE;
    HarnessRequest(&Request, SofIpcClassPipeline, Ipc4TransportSelfTest[1].Primary, 0);
    CHECK_EQ(SofIpcRequest(&H->Ipc, &Request), SofIpcOk);
    CHECK_EQ(H->Ipc.Notifications, 2);
    CHECK_EQ(H->Ipc.RingCount, 2);

    /* the ring keeps the newest sixteen */
    HarnessStart(H, Image, NULL);
    HarnessAttach(H, NULL, FALSE);
    for (Index = 0; Index < SOFIPC_NOTIFICATION_RING + 4; Index++)
    {
        H->Dsp->IpcTarget = FWLOAD_IPC_BUSY | (SofIpc4GlbNotification << SOFIPC4_TYPE_SHIFT) |
            (SOFIPC4_NOTIFY_LOG_BUFFER_STATUS << SOFIPC4_NOTIFICATION_TYPE_SHIFT);
        H->Dsp->IpcTargetAck |= FWLOAD_IPC_BUSY;
        H->Dsp->NotificationPending = TRUE;
        CHECK(SofIpcPollNotifications(&H->Ipc));
        CHECK_EQ(H->Ipc.RingCount, Index + 1 < SOFIPC_NOTIFICATION_RING ? Index + 1 : SOFIPC_NOTIFICATION_RING);
    }
    CHECK_EQ(H->Ipc.Notifications, SOFIPC_NOTIFICATION_RING + 4);
    for (Index = 0; Index < SOFIPC_NOTIFICATION_RING; Index++)
        CHECK(SofIpcTakeNotification(&H->Ipc, &Notification));
    CHECK(!SofIpcTakeNotification(&H->Ipc, &Notification));

    /* a reply nobody asked for is acknowledged and skipped, and counted */
    H->Dsp->IpcReply = FWLOAD_IPC_DONE | 0x61000000;
    H->Dsp->ReplyPending = TRUE;
    H->Dsp->ReplyHasPayload = FALSE;
    CHECK(SofIpcPollNotifications(&H->Ipc));
    CHECK_EQ(H->Ipc.StrayReplies, 1);
    CHECK_EQ((H->Dsp->IpcReply & FWLOAD_IPC_DONE), 0);
    CHECK_EQ(FakeDspIpcErrors(H->Dsp), 0);
}

/* (f) Interrupt mode: the enables are set, the service routine masks on every interrupt and the passive path sets them again after each exchange; the loader's storm guard trips and the next request is refused */
static
void
CheckInterruptMode(
    HARNESS* H,
    const SOF_RI_IMAGE* Image)
{
    SOFIPC_REQUEST Request;
    FAKE_DSP_FAULTS Faults;
    UINT32 Index, Writes, WriteCount;

    HarnessStart(H, Image, NULL);
    HarnessAttach(H, NULL, TRUE);
    CHECK_EQ(H->Dsp->InterruptEnable, 1);
    CHECK_EQ(H->Dsp->HostIpcEnable, 1);
    CHECK_EQ(H->Dsp->IpcControl, 3);
    CHECK(H->Ctx->InterruptsEnabled);
    /* the two masks of the boot and the set at the initialization, three writes each, then per exchange three to mask and three to set again */
    Writes = H->Dsp->InterruptWritesLocked;
    CHECK_EQ(Writes, 9);
    H->InterruptsPerDelay = 1;
    for (Index = 0; Index < IPC4_TRANSPORT_SELF_TEST_EXCHANGES; Index++)
    {
        HarnessRequest(&Request, SofIpcClassPipeline, Ipc4TransportSelfTest[Index].Primary, 0);
        CHECK_EQ(SofIpcRequest(&H->Ipc, &Request), SofIpcOk);
        /* masked by the service routine on the reply, set again by the passive path after the acknowledgement */
        CHECK_EQ(H->Dsp->InterruptEnable, 1);
        CHECK_EQ(H->Dsp->HostIpcEnable, 1);
        CHECK_EQ(H->Dsp->IpcControl, 3);
        CHECK_EQ(H->Ipc.InterruptCount, Index + 1);
        CHECK_EQ(H->Ctx->InterruptCount, Index + 1);
        CHECK_EQ(H->Ctx->StormCount, 0);
        CHECK(!H->Ctx->InterruptStorm);
    }
    /* three writes to set at the initialization, then per exchange three to mask and three to set again */
    CHECK_EQ(H->Dsp->InterruptWritesLocked, Writes + 6 * IPC4_TRANSPORT_SELF_TEST_EXCHANGES);
    CHECK_EQ(H->Dsp->InterruptWritesUnlocked, 0);
    CHECK_EQ(H->Dsp->InterruptLockRecursions, 0);
    CHECK_EQ(H->Dsp->InterruptLockDelays, 0);
    CHECK_EQ(H->Dsp->InterruptLockDepth, 0);
    CHECK_EQ(FakeDspIpcErrors(H->Dsp), 0);

    /* a message pending and no request outstanding: FWLOAD_INTERRUPT_STORM_LIMIT interrupts in a row, none separated by an acknowledged message, trip the guard; every claim masked the three enables */
    H->InterruptsPerDelay = 0;
    H->Dsp->IpcTarget = FWLOAD_IPC_BUSY | SOFIPC4_FW_READY_PRIMARY;
    for (Index = 1; Index < FWLOAD_INTERRUPT_STORM_LIMIT; Index++)
    {
        CHECK(HarnessInterrupt(H));
        CHECK_EQ(H->Dsp->InterruptEnable, 0);
        CHECK_EQ(H->Dsp->HostIpcEnable, 0);
        CHECK_EQ(H->Dsp->IpcControl, 0);
        CHECK(!H->Ctx->InterruptStorm);
    }
    CHECK(HarnessInterrupt(H));
    CHECK(H->Ctx->InterruptStorm);
    CHECK_EQ(H->Ipc.InterruptCount, IPC4_TRANSPORT_SELF_TEST_EXCHANGES + FWLOAD_INTERRUPT_STORM_LIMIT);
    WriteCount = H->Dsp->WriteCount;
    CHECK(!HarnessInterrupt(H));
    CHECK_EQ(H->Dsp->WriteCount, WriteCount);
    HarnessRequest(&Request, SofIpcClassPipeline, Ipc4TransportSelfTest[0].Primary, 0);
    CHECK_EQ(SofIpcRequest(&H->Ipc, &Request), SofIpcInterruptStorm);
    CHECK_EQ(H->Ipc.Link, SofIpcLinkFaulted);
    CHECK_EQ(H->Ipc.Failure, SofIpcInterruptStorm);
    CHECK_EQ(H->Ipc.Requests, IPC4_TRANSPORT_SELF_TEST_EXCHANGES);
    CHECK_EQ(H->Dsp->InterruptWritesLocked, Writes + 6 * IPC4_TRANSPORT_SELF_TEST_EXCHANGES + 3 * FWLOAD_INTERRUPT_STORM_LIMIT);

    /* a guard the boot tripped leaves nothing to enable */
    HarnessStart(H, Image, NULL);
    HarnessInterrupt(H);
    H->Dsp->IpcTarget = FWLOAD_IPC_BUSY | SOFIPC4_FW_READY_PRIMARY;
    for (Index = 0; Index < FWLOAD_INTERRUPT_STORM_LIMIT; Index++)
        HarnessInterrupt(H);
    CHECK(H->Ctx->InterruptStorm);
    /* a guard that tripped leaves nothing to attach: the initialization faults the link at once */
    CHECK_EQ(SofIpcInit(&H->Ipc, &HarnessIpcOps, H, H->Ctx, NULL, TRUE), SofIpcInterruptStorm);
    CHECK_EQ(H->Ipc.Link, SofIpcLinkFaulted);
    CHECK_EQ(H->Ipc.Failure, SofIpcInterruptStorm);

    /* a guard that trips during a wait: a firmware that posts a notification on every acknowledgement keeps the line pending, FWLOAD_INTERRUPT_STORM_LIMIT interrupts per poll trip the guard, and the wait ends at once as a storm instead of at its budget */
    Faults = *FakeDspDefaultFaults();
    Faults.NotificationStorm = FAKE_DSP_STORM_AFTER_FW_READY;
    HarnessStart(H, Image, &Faults);
    CHECK(H->Dsp->StormPosted > 0);
    HarnessAttach(H, NULL, TRUE);
    H->InterruptsPerDelay = FWLOAD_INTERRUPT_STORM_LIMIT;
    H->Dsp->Faults.NeverReplyForType = SofIpc4GlbSetPipelineState;
    HarnessRequest(&Request, SofIpcClassPipeline, Ipc4TransportSelfTest[1].Primary, 0);
    CHECK_EQ(SofIpcRequest(&H->Ipc, &Request), SofIpcInterruptStorm);
    CHECK_EQ(H->Ipc.Link, SofIpcLinkFaulted);
    CHECK_EQ(H->Ipc.Failure, SofIpcInterruptStorm);
    CHECK(H->Ctx->InterruptStorm);
    /* the drain before the request is bounded: a ring's worth, then the wait's polls */
    CHECK_EQ(H->Ipc.Notifications, SOFIPC_NOTIFICATION_RING + 2);
    CHECK(H->Ipc.Last.ElapsedUs <= 2 * SOFIPC_FAST_POLL_US);
    H->InterruptsPerDelay = 0;
}

/* (g) Two callers: the second waits for the first's request up to its own budget, then fails busy; nothing is sent and the link stays ready */
static
void
CheckTwoCallers(
    HARNESS* H,
    const SOF_RI_IMAGE* Image)
{
    SOFIPC_REQUEST Request;
    UINT32 Requests, Primary;
    static UINT8 Payload[FWLOAD_MAILBOX_BYTES];
    static UINT8 Reply[FWLOAD_MAILBOX_BYTES];

    HarnessStart(H, Image, NULL);
    HarnessAttach(H, NULL, FALSE);
    H->LockHeld = TRUE;
    H->LockWaitAdvances = TRUE;
    Requests = H->Dsp->TransportRequests;
    HarnessRequest(&Request, SofIpcClassPipeline, Ipc4TransportSelfTest[0].Primary, 0);
    CHECK_EQ(SofIpcRequest(&H->Ipc, &Request), SofIpcBusy);
    CHECK_EQ(H->Ipc.Last.Outcome, SofIpcBusy);
    CHECK_EQ(H->Ipc.Last.ElapsedUs, 500000);
    CHECK_EQ(H->Ipc.Last.RequestPrimary, Ipc4TransportSelfTest[0].Primary | FWLOAD_IPC_BUSY);
    CHECK_EQ(H->LastLockTimeout, 500000);
    CHECK_EQ(H->LockWaits, 1);
    CHECK_EQ(H->LockAcquires, 0);
    CHECK_EQ(H->Ipc.Requests, 0);
    CHECK_EQ(H->Dsp->TransportRequests, Requests);
    CHECK_EQ(H->Ipc.Link, SofIpcLinkReady);
    CHECK_EQ(H->Ipc.Failure, SofIpcOk);
    CHECK_STR(SofIpcStatusName(SofIpcBusy), "busy");
    H->LockHeld = FALSE;
    HarnessRequest(&Request, SofIpcClassPipeline, Ipc4TransportSelfTest[0].Primary, 0);
    CHECK_EQ(SofIpcRequest(&H->Ipc, &Request), SofIpcOk);
    CHECK_EQ(H->Ipc.Requests, 1);
    CHECK_EQ(H->LockAcquires, 1);
    CHECK_EQ(H->LockUnlocks, 1);

    /* the shape itself: a request in progress, and a second one from inside its wait */
    HarnessStart(H, Image, NULL);
    HarnessAttach(H, NULL, FALSE);
    H->LockWaitAdvances = TRUE;
    H->Dsp->Faults.IpcReplyUs = 2000;
    H->NestedRun = TRUE;
    H->NestedAtDelay = 3;
    HarnessRequest(&Request, SofIpcClassPipeline, Ipc4TransportSelfTest[0].Primary, 0);
    HarnessRequest(&H->Nested, SofIpcClassPipeline, Ipc4TransportSelfTest[1].Primary, 0);
    CHECK_EQ(SofIpcRequest(&H->Ipc, &Request), SofIpcOk);
    CHECK_EQ(H->NestedStatus, SofIpcBusy);
    CHECK_EQ(H->NestedElapsed, 500000);
    CHECK_EQ(H->LockAcquires, 1);
    CHECK_EQ(H->LockWaits, 1);
    CHECK_EQ(H->LockUnlocks, 1);
    CHECK_EQ(H->Ipc.Requests, 1);
    CHECK_EQ(H->Dsp->TransportRequests, 1);
    CHECK_EQ(H->Ipc.Link, SofIpcLinkReady);
    CHECK_EQ(FakeDspIpcErrors(H->Dsp), 0);
    /* the first caller's exchange went on to its reply after the second gave up */
    CHECK(H->Ipc.Last.ElapsedUs >= 500000);
    CHECK_EQ(H->Ipc.Last.Outcome, SofIpcOk);

    /* without a lock: a re-entrant request is refused at once and nothing is sent behind the first */
    HarnessStart(H, Image, NULL);
    CHECK_EQ(SofIpcInit(&H->Ipc, &HarnessIpcOpsUnlocked, H, H->Ctx, NULL, FALSE), SofIpcOk);
    HarnessTransportReset(H);
    H->Dsp->Faults.IpcReplyUs = 2000;
    H->NestedRun = TRUE;
    H->NestedAtDelay = 3;
    HarnessRequest(&Request, SofIpcClassPipeline, Ipc4TransportSelfTest[0].Primary, 0);
    HarnessRequest(&H->Nested, SofIpcClassPipeline, Ipc4TransportSelfTest[1].Primary, 0);
    CHECK_EQ(SofIpcRequest(&H->Ipc, &Request), SofIpcOk);
    /* the hook ran: the nested request was refused at once, nothing was sent behind the first */
    CHECK(!H->NestedRun);
    CHECK_EQ(H->NestedStatus, SofIpcBusy);
    CHECK_EQ(H->NestedElapsed, 0);
    CHECK_EQ(H->Ipc.Requests, 1);
    CHECK_EQ(H->Dsp->TransportRequests, 1);

    /* a payload is refused before anything is sent when it cannot travel through a block, and a reply buffer past the uplink mailbox too */
    HarnessStart(H, Image, NULL);
    HarnessAttach(H, NULL, FALSE);
    /* 1024 dwords fill the downlink mailbox: the header and the payload agree, and the model's table takes the instance */
    SofIpc4ModuleInitInstance(&Request.Msg, 1, 0, 1, FWLOAD_MAILBOX_BYTES / 4, 0, 0);
    Request.Class = SofIpcClassModule;
    Request.Payload = Payload;
    Request.PayloadBytes = FWLOAD_MAILBOX_BYTES + 4;
    memset(Payload, 0x5A, sizeof(Payload));
    CHECK_EQ(SofIpcRequest(&H->Ipc, &Request), SofIpcBadLength);
    CHECK_EQ(H->Ipc.Last.Outcome, SofIpcBadLength);
    CHECK_EQ(H->Ipc.Requests, 0);
    CHECK_EQ(H->Ipc.Link, SofIpcLinkReady);
    Request.PayloadBytes = 3;
    CHECK_EQ(SofIpcRequest(&H->Ipc, &Request), SofIpcBadLength);
    /* a full block is written into the downlink mailbox before the header */
    Request.PayloadBytes = FWLOAD_MAILBOX_BYTES;
    Request.ReplyBuffer = Reply;
    Request.ReplyBytes = FWLOAD_MAILBOX_BYTES + 4;
    CHECK_EQ(SofIpcRequest(&H->Ipc, &Request), SofIpcBadLength);
    Request.ReplyBytes = 0;
    Primary = Request.Msg.Primary;
    CHECK_EQ(SofIpcRequest(&H->Ipc, &Request), SofIpcOk);
    CHECK_EQ(H->Dsp->LastDownlinkBytes, FWLOAD_MAILBOX_BYTES);
    CHECK(memcmp(H->Dsp->Downlink, Payload, sizeof(Payload)) == 0);
    /* the header the model received is the codec's words, the payload first; the reply's header came back in the request */
    CHECK_EQ(H->Dsp->RequestPrimary[0], Primary | FWLOAD_IPC_BUSY);
    CHECK_EQ(H->Dsp->RequestExtension[0], (FWLOAD_MAILBOX_BYTES / 4) | (1 << 16));
    CHECK_EQ(Request.Msg.Primary, (SofIpc4ModInitInstance << SOFIPC4_TYPE_SHIFT) | SOFIPC4_RSP_BIT | SOFIPC4_MSG_TGT_BIT | SofIpc4Success);
}

/* (h) A large configuration in blocks: the first carries the total with the first flag, the others their offset, the last the last flag; a read follows the firmware's total */
static
void
CheckLargeConfig(
    HARNESS* H,
    const SOF_RI_IMAGE* Image)
{
    static UINT8 Data[FAKE_DSP_LARGE_CONFIG_BYTES];
    static UINT8 Buffer[FAKE_DSP_LARGE_CONFIG_BYTES];
    SIZE_T Bytes;
    UINT32 Index, Block, Word;

    for (Word = 0; Word < sizeof(Data) / 4; Word++)
        ((UINT32*)Data)[Word] = Word;

    for (Index = 0; Index < IPC4_TRANSPORT_LARGE_CASES; Index++)
    {
        const IPC4_TRANSPORT_LARGE_CASE* Case = &Ipc4TransportLarge[Index];

        HarnessStart(H, Image, NULL);
        HarnessAttach(H, NULL, FALSE);
        CHECK_EQ(SofIpcLargeConfigSet(&H->Ipc, IPC4_CASE_LARGE_MODULE, IPC4_CASE_LARGE_INSTANCE, IPC4_CASE_LARGE_PARAM, Data, Case->TotalBytes), SofIpcOk);
        CHECK_EQ(H->Dsp->LargeConfigBlocks, Case->Blocks);
        CHECK_EQ(H->Dsp->LargeConfigErrors, 0);
        CHECK_EQ(H->Dsp->LargeConfigBytes, Case->TotalBytes);
        CHECK_EQ(H->Ipc.Requests, Case->Blocks);
        CHECK_EQ(H->Ipc.Replies, Case->Blocks);
        CHECK_EQ(H->Ipc.Link, SofIpcLinkReady);
        CHECK(memcmp(H->Dsp->LargeConfig, Data, Case->TotalBytes) == 0);
        CHECK_EQ(FakeDspIpcErrors(H->Dsp), 0);
        for (Block = 0; Block < Case->Blocks; Block++)
        {
            CHECK_EQ(H->Dsp->RequestPrimary[Block], IPC4_CASE_LARGE_PRIMARY | FWLOAD_IPC_BUSY);
            CHECK_EQ(H->Dsp->RequestExtension[Block],
                (IPC4_CASE_LARGE_PARAM << 20) | Case->Block[Block].SizeField | Case->Block[Block].Flags);
        }
    }
    CHECK_EQ(SofIpcLargeConfigSet(&H->Ipc, 3, 2, 2, Data, 0), SofIpcBadLength);
    CHECK_EQ(SofIpcLargeConfigSet(&H->Ipc, 3, 2, 2, Data, 3), SofIpcBadLength);
    CHECK_EQ(SofIpcLargeConfigSet(&H->Ipc, 3, 2, 2, Data, (SIZE_T)SOFIPC4_LARGE_CONFIG_DATA_MAX + 4), SofIpcBadLength);
    CHECK_EQ(H->Ipc.Requests, 1);
    CHECK_EQ(H->Ipc.Link, SofIpcLinkReady);

    for (Index = 0; Index < IPC4_TRANSPORT_READ_CASES; Index++)
    {
        const IPC4_TRANSPORT_READ_CASE* Case = &Ipc4TransportReads[Index];

        HarnessStart(H, Image, NULL);
        HarnessAttach(H, NULL, FALSE);
        memset(Buffer, 0, sizeof(Buffer));
        H->Dsp->LargeConfigTotal = Case->TotalBytes;
        CHECK_EQ(SofIpcLargeConfigGet(&H->Ipc, IPC4_CASE_LARGE_MODULE, IPC4_CASE_LARGE_INSTANCE, IPC4_CASE_LARGE_PARAM, Buffer, Case->Capacity, &Bytes), Case->Status);
        CHECK_EQ(Bytes, Case->Bytes);
        CHECK_EQ(H->Ipc.Requests, Case->Blocks);
        CHECK_EQ(H->Ipc.Link, SofIpcLinkReady);
        CHECK_EQ(H->Ipc.Replies, Case->Blocks);
        if (Case->Status == SofIpcOk)
        {
            /* every word read is the word the firmware held at that offset, and nothing past the total was touched */
            for (Word = 0; Word < Case->Bytes / 4; Word++)
            {
                if (((UINT32*)Buffer)[Word] != Word)
                    break;
            }
            CHECK_EQ(Word, Case->Bytes / 4);
            /* the reply's size field: the total on a single block, the offset of the last block otherwise */
            CHECK_EQ(H->Ipc.Last.ReplyExtension & SOFIPC4_LARGE_CONFIG_DATA_MAX,
                Case->Blocks == 1 ? Case->TotalBytes : (Case->Blocks - 1) * FWLOAD_MAILBOX_BYTES);
            CHECK(H->Ipc.Last.ReplyExtension & SOFIPC4_LARGE_CONFIG_FINAL_BLOCK);
        }
        CHECK_EQ(FakeDspIpcErrors(H->Dsp), 0);
    }
    /* the read's requests: the first names the capacity with the first flag, the next continue at their offset */
    HarnessStart(H, Image, NULL);
    HarnessAttach(H, NULL, FALSE);
    H->Dsp->LargeConfigTotal = 6000;
    CHECK_EQ(SofIpcLargeConfigGet(&H->Ipc, 3, 2, 2, Buffer, 8192, &Bytes), SofIpcOk);
    CHECK_EQ(H->Dsp->RequestPrimary[0], (IPC4_CASE_LARGE_PRIMARY - 0x01000000) | FWLOAD_IPC_BUSY);
    CHECK_EQ(H->Dsp->RequestExtension[0], (2 << 20) | SOFIPC4_LARGE_CONFIG_INIT_BLOCK | 8192);
    CHECK_EQ(H->Dsp->RequestExtension[1], (2 << 20) | 4096);
    CHECK_EQ(H->Dsp->DownlinkWrites, 0);
    CHECK_EQ(SofIpcLargeConfigGet(&H->Ipc, 3, 2, 2, Buffer, 0, &Bytes), SofIpcBadLength);
    CHECK_EQ(H->Ipc.Requests, 2);
}

/* (i) The poll policy: a short stall inside the first millisecond, sleeps after it, no poll at all for a reply already waiting */
static
void
CheckPollPolicy(
    HARNESS* H,
    const SOF_RI_IMAGE* Image)
{
    SOFIPC_REQUEST Request;
    UINT32 Index;

    HarnessStart(H, Image, NULL);
    H->Dsp->Faults.IpcReplyUs = 100;
    HarnessAttach(H, NULL, FALSE);
    HarnessRequest(&Request, SofIpcClassPipeline, Ipc4TransportSelfTest[0].Primary, 0);
    CHECK_EQ(SofIpcRequest(&H->Ipc, &Request), SofIpcOk);
    CHECK_EQ(H->Ipc.FastPolls, 5);
    CHECK_EQ(H->Ipc.SlowPolls, 0);
    CHECK_EQ(H->Ipc.Last.ElapsedUs, 100);
    for (Index = 0; Index < 5; Index++)
        CHECK_EQ(H->DelayLog[Index], SOFIPC_FAST_POLL_US);

    HarnessStart(H, Image, NULL);
    H->Dsp->Faults.IpcReplyUs = 3000;
    HarnessAttach(H, NULL, FALSE);
    HarnessRequest(&Request, SofIpcClassPipeline, Ipc4TransportSelfTest[0].Primary, 0);
    CHECK_EQ(SofIpcRequest(&H->Ipc, &Request), SofIpcOk);
    CHECK_EQ(H->Ipc.FastPolls, SOFIPC_FAST_PHASE_US / SOFIPC_FAST_POLL_US);
    CHECK_EQ(H->Ipc.SlowPolls, 4);
    CHECK_EQ(H->Ipc.Last.ElapsedUs, 3000);
    for (Index = 0; Index < SOFIPC_FAST_PHASE_US / SOFIPC_FAST_POLL_US; Index++)
        CHECK_EQ(H->DelayLog[Index], SOFIPC_FAST_POLL_US);
    for (; Index < SOFIPC_FAST_PHASE_US / SOFIPC_FAST_POLL_US + 4; Index++)
        CHECK_EQ(H->DelayLog[Index], SOFIPC_SLOW_POLL_US);

    HarnessStart(H, Image, NULL);
    H->Dsp->Faults.IpcReplyUs = 0;
    HarnessAttach(H, NULL, FALSE);
    HarnessRequest(&Request, SofIpcClassPipeline, Ipc4TransportSelfTest[0].Primary, 0);
    CHECK_EQ(SofIpcRequest(&H->Ipc, &Request), SofIpcOk);
    CHECK_EQ(H->Ipc.FastPolls, 0);
    CHECK_EQ(H->Ipc.SlowPolls, 0);
    CHECK_EQ(H->Ipc.Last.ElapsedUs, 0);
}

/* The link's states: offline before a boot and after a power-down, faulted until a new boot, ready again after one; the names */
static
void
CheckLink(
    HARNESS* H,
    const SOF_RI_IMAGE* Image)
{
    SOFIPC_REQUEST Request;
    SOFIPC_NOTIFICATION Notification;

    HarnessBoot(H, Image, NULL);
    CHECK_EQ(SofIpcInit(&H->Ipc, &HarnessIpcOps, H, H->Ctx, NULL, FALSE), SofIpcBadState);
    CHECK_EQ(H->Ipc.Link, SofIpcLinkOffline);
    HarnessRequest(&Request, SofIpcClassPipeline, Ipc4TransportSelfTest[0].Primary, 0);
    CHECK_EQ(SofIpcRequest(&H->Ipc, &Request), SofIpcBadState);
    CHECK(!SofIpcPollNotifications(&H->Ipc));
    CHECK_EQ(H->Dsp->TransportRequests, 0);
    CHECK_EQ(SofIpcInit(&H->Ipc, &HarnessIpcOps, H, NULL, NULL, FALSE), SofIpcBadState);
    CHECK_EQ(SofIpcRequest(NULL, &Request), SofIpcBadState);
    Request.Class = SofIpcClassCount;
    CHECK_EQ(SofIpcRequest(&H->Ipc, &Request), SofIpcBadState);
    Request.Class = SofIpcClassPipeline;
    CHECK_EQ(SofIpcRequest(&H->Ipc, NULL), SofIpcBadState);

    /* after a power-down the loader is off: the link goes offline at the next request */
    HarnessStart(H, Image, NULL);
    HarnessAttach(H, NULL, FALSE);
    CHECK_EQ(FwloadPowerDown(H->Ctx), FwloadOk);
    HarnessRequest(&Request, SofIpcClassPipeline, Ipc4TransportSelfTest[0].Primary, 0);
    CHECK_EQ(SofIpcRequest(&H->Ipc, &Request), SofIpcBadState);
    CHECK_EQ(H->Ipc.Link, SofIpcLinkOffline);

    /* a faulted link recovers through a new boot and a new initialization only: a mailbox write that fails faults it before anything is sent */
    HarnessStart(H, Image, NULL);
    HarnessAttach(H, NULL, FALSE);
    H->Dsp->Faults.MemoryWriteFails = TRUE;
    HarnessRequest(&Request, SofIpcClassPipeline, Ipc4TransportSelfTest[0].Primary, 0);
    Request.Payload = (const UINT8*)&Request;
    Request.PayloadBytes = 8;
    CHECK_EQ(SofIpcRequest(&H->Ipc, &Request), SofIpcHardwareFault);
    CHECK_EQ(H->Ipc.Link, SofIpcLinkFaulted);
    CHECK_EQ(H->Ipc.Requests, 0);
    Request.PayloadBytes = 0;
    HarnessRequest(&Request, SofIpcClassPipeline, Ipc4TransportSelfTest[0].Primary, 0);
    CHECK_EQ(SofIpcRequest(&H->Ipc, &Request), SofIpcBadState);
    CHECK(!SofIpcPollNotifications(&H->Ipc));
    CHECK(!SofIpcTakeNotification(&H->Ipc, &Notification));

    HarnessStart(H, Image, NULL);
    HarnessAttach(H, NULL, FALSE);
    H->Dsp->Faults.IpcStatusForType = SofIpc4GlbCreatePipeline;
    H->Dsp->Faults.IpcStatusValue = SofIpc4InvalidResourceState;
    HarnessRequest(&Request, SofIpcClassPipeline, Ipc4TransportSelfTest[0].Primary, 0);
    CHECK_EQ(SofIpcRequest(&H->Ipc, &Request), SofIpcFwStatus);
    CHECK_EQ(H->Ipc.Last.Status, SofIpc4InvalidResourceState);
    CHECK_STR(SofIpc4StatusName(SofIpc4InvalidResourceState), "invalid resource state");
    CHECK_EQ(H->Ipc.Link, SofIpcLinkFaulted);
    HarnessRequest(&Request, SofIpcClassPipeline, Ipc4TransportSelfTest[0].Primary, 0);
    CHECK_EQ(SofIpcRequest(&H->Ipc, &Request), SofIpcBadState);
    CHECK_EQ(H->Ipc.Requests, 1);
    CHECK_EQ(FwloadPowerDown(H->Ctx), FwloadOk);
    HarnessStart(H, Image, NULL);
    HarnessAttach(H, NULL, FALSE);
    CHECK_EQ(H->Ipc.Link, SofIpcLinkReady);
    CHECK_EQ(H->Ipc.Failure, SofIpcOk);
    HarnessRequest(&Request, SofIpcClassPipeline, Ipc4TransportSelfTest[0].Primary, 0);
    CHECK_EQ(SofIpcRequest(&H->Ipc, &Request), SofIpcOk);

    CHECK_STR(SofIpcLinkName(SofIpcLinkOffline), "offline");
    CHECK_STR(SofIpcLinkName(SofIpcLinkReady), "ready");
    CHECK_STR(SofIpcLinkName(SofIpcLinkBusy), "busy");
    CHECK_STR(SofIpcLinkName(SofIpcLinkFaulted), "faulted");
    CHECK_STR(SofIpcLinkName((SOFIPC_LINK)9), "unknown");
    CHECK_STR(SofIpcStatusName(SofIpcOk), "ok");
    CHECK_STR(SofIpcStatusName(SofIpcTimeout), "reply timeout");
    CHECK_STR(SofIpcStatusName(SofIpcCrash), "firmware crash");
    CHECK_STR(SofIpcStatusName(SofIpcInterruptStorm), "interrupt storm");
    CHECK_STR(SofIpcStatusName(SofIpcHardwareFault), "hardware fault");
    CHECK_STR(SofIpcStatusName((SOFIPC_STATUS)99), "unknown");
    CHECK_STR(SofIpcTypeName(Ipc4TransportSelfTest[0].Primary), "create pipeline");
    CHECK_STR(SofIpcTypeName(Ipc4TransportSelfTest[5].Primary), "delete pipeline");
    CHECK_STR(SofIpcTypeName(0x44020003), "large config set");
    CHECK_STR(SofIpcTypeName(0x43020003), "large config get");
    CHECK_STR(SofIpcTypeName(0x4B000000), "delete instance");
    CHECK_STR(SofIpcTypeName(0x4E000000), "message");
    CHECK_STR(SofIpcTypeName(0x1E000000), "message");
    CHECK_EQ(SofIpcClassOf(0x44020003), SofIpcClassLargeConfig);
    CHECK_EQ(SofIpcClassOf(0x43020003), SofIpcClassLargeConfig);
    CHECK_EQ(SofIpcClassOf(0x4B000000), SofIpcClassModule);
    CHECK_EQ(SofIpcClassOf(0x0E000000), SofIpcClassPipeline);
    CHECK_EQ(SofIpcClassOf(0x15010000), SofIpcClassPipeline);
    CHECK_EQ(SofIpcClassOf(0x0F010000), SofIpcClassModule);

    /* the codec builds the self-test's words */
    {
        SOFIPC4_MSG Msg;

        SofIpc4PipelineCreate(&Msg, IPC4_CASE_PIPELINE_INSTANCE, 0, IPC4_CASE_PIPELINE_PAGES, 0, 0, 0);
        CHECK_EQ(Msg.Primary, Ipc4TransportSelfTest[0].Primary);
        CHECK_EQ(Msg.Extension, Ipc4TransportSelfTest[0].Extension);
        SofIpc4PipelineSetState(&Msg, IPC4_CASE_PIPELINE_INSTANCE, SOFIPC4_PIPELINE_STATE_PAUSED, 0);
        CHECK_EQ(Msg.Primary, Ipc4TransportSelfTest[1].Primary);
        SofIpc4PipelineSetState(&Msg, IPC4_CASE_PIPELINE_INSTANCE, SOFIPC4_PIPELINE_STATE_RUNNING, 0);
        CHECK_EQ(Msg.Primary, Ipc4TransportSelfTest[2].Primary);
        SofIpc4PipelineSetState(&Msg, IPC4_CASE_PIPELINE_INSTANCE, SOFIPC4_PIPELINE_STATE_RESET, 0);
        CHECK_EQ(Msg.Primary, Ipc4TransportSelfTest[4].Primary);
        SofIpc4PipelineDelete(&Msg, IPC4_CASE_PIPELINE_INSTANCE);
        CHECK_EQ(Msg.Primary, Ipc4TransportSelfTest[5].Primary);
    }
}

/* ---- (i) The stream manager: the DSP driver's own transitions over the fake bus and the model, with the loopback topology ---- */

#define STREAM_LOG_BYTES        32768
#define STREAM_REAL_MODULES     64
#define STREAM_FRAMES           4096

typedef struct _STREAM_HARNESS
{
    HARNESS H;
    FAKE_BUS Bus;
    INTCDSP_STREAMS_OPS Ops;
    INTCDSP_SELFTEST_OPS SelfTestOps;
    INTCDSP_STREAMS Streams;
    UINT8* State;
    UINT8* ModelBuffer;
    PSOFTPLG_IPC4_MODEL Model;
    UINT8* File;
    SIZE_T FileBytes;
    UINT8* Firmware;
    SIZE_T FirmwareBytes;
    SOF_RI_MODULE Modules[STREAM_REAL_MODULES];
    SOF_RI_IMAGE Ri;
    CHAR Log[STREAM_LOG_BYTES];
    SIZE_T LogBytes;
    UINT32 Faults;
    /* the device's period event, as the driver's wait registers it on the capture stream */
    KEVENT Event;
    UINT32 Waits;
    /* the wire of the model's pipeline: whether it forwards, the latency it puts on the capture in frames, where the capture stream stands when it stops, in frames, and the capture frame whose second channel the wire gets wrong, by five */
    BOOLEAN Wire;
    UINT32 Latency;
    UINT32 StopFrames;
    UINT32 DamageFrame;
    /* the periods while the driver waits: how many the model still moves before they stall, the bytes one moves an entry by (the buffer's half when 0), the wakes so far */
    UINT32 PeriodsLeft;
    UINT32 PeriodBytes;
    UINT32 Wakes;
} STREAM_HARNESS;

static const INTCDSP_SELFTEST_OPS StreamSelfTestOps;

/* The model's answer to a wait on a period event, as the fake bus's wait hook takes it */
static
NTSTATUS
StreamWaitModel(
    PVOID Context,
    PKEVENT Event,
    PLARGE_INTEGER Timeout);

static
void
StreamLog(
    PVOID Context,
    const char* Format,
    va_list Arguments)
{
    STREAM_HARNESS* S = (STREAM_HARNESS*)Context;
    int Written = vsnprintf(S->Log + S->LogBytes, sizeof(S->Log) - S->LogBytes, Format, Arguments);

    if (Written > 0)
        S->LogBytes += (SIZE_T)Written < sizeof(S->Log) - S->LogBytes ? (SIZE_T)Written : sizeof(S->Log) - S->LogBytes - 1;
}

static
void
StreamFault(
    PVOID Context)
{
    ((STREAM_HARNESS*)Context)->Faults++;
}

static const INTCDSP_STREAMS_OPS StreamOpsTemplate =
{
    NULL,
    StreamLog,
    StreamFault,
    NULL
};

/* How often the log holds a line */
static
UINT32
StreamLogCount(
    const STREAM_HARNESS* S,
    const char* Text)
{
    UINT32 Count = 0;
    const char* At = S->Log;

    while ((At = strstr(At, Text)) != NULL)
    {
        Count++;
        At++;
    }
    return Count;
}

/* The module id of a UUID: its index in the real firmware image's module table, as the driver resolves it */
static
UINT32
StreamLookup(
    PVOID Context,
    const UINT8* Uuid)
{
    STREAM_HARNESS* S = (STREAM_HARNESS*)Context;
    UINT32 M;

    for (M = 0; M < S->Ri.ModuleCount; M++)
        if (memcmp(S->Ri.Modules[M].Uuid, Uuid, SOFTPLG_UUID_BYTES) == 0)
            return M;
    return SOFTPLG_MODULE_NONE;
}

/* A whole fixture file */
static
UINT8*
StreamFile(
    const char* Directory,
    const char* Name,
    SIZE_T* Bytes)
{
    FILE* File;
    UINT8* Data;
    long Size;
    char Path[512];

    snprintf(Path, sizeof(Path), "%s/%s", Directory, Name);
    File = fopen(Path, "rb");
    CHECK(File != NULL);
    if (File == NULL)
        exit(2);
    fseek(File, 0, SEEK_END);
    Size = ftell(File);
    fseek(File, 0, SEEK_SET);
    Data = (UINT8*)malloc((size_t)Size);
    if (Data == NULL)
        exit(2);
    CHECK_EQ(fread(Data, 1, (size_t)Size, File), (size_t)Size);
    fclose(File);
    *Bytes = (SIZE_T)Size;
    return Data;
}

/* The manager over a booted model: the loopback topology built into a model with the real firmware's module table, the fake bus, the stream members */
static
void
StreamStart(
    STREAM_HARNESS* S,
    const FAKE_DSP_FAULTS* Faults)
{
    SOFTPLG_MODEL_INPUT Input;
    SOFTPLG_IPC4_LIMITS Limits;
    SOFTPLG_IPC4_PROBLEM Problem;
    SOF_RI_MODULE Modules[4];
    SOF_RI_IMAGE Parsed;
    SIZE_T Bytes = 0;
    UINT8* Image;

    memset(S, 0, sizeof(*S));
    HarnessInit(&S->H);
    Image = (UINT8*)calloc(1, FWLOAD_CASE_IMAGE_BYTES);
    if (Image == NULL)
        exit(2);
    FwloadCaseBuildImage(Image);
    CHECK_EQ(SofRiParse(Image, FWLOAD_CASE_IMAGE_BYTES, Modules, 4, &Parsed), FwloadOk);
    HarnessStart(&S->H, &Parsed, Faults);
    free(Image);
    HarnessAttach(&S->H, NULL, FALSE);
    S->File = StreamFile(SOFTPLG_FIXTURE_DIR, "sof-ipc4-loopback.tplg", &S->FileBytes);
    S->Firmware = StreamFile(FWLOAD_FIXTURE_DIR, "sof-arl-s.ri", &S->FirmwareBytes);
    CHECK_EQ(SofRiParse(S->Firmware, S->FirmwareBytes, S->Modules, STREAM_REAL_MODULES, &S->Ri), FwloadOk);
    memset(&Input, 0, sizeof(Input));
    Input.Data = S->File;
    Input.Size = S->FileBytes;
    Input.Lookup = StreamLookup;
    Input.LookupContext = S;
    CHECK_EQ(SoftplgModelMeasure(&Input, &Bytes, &Problem), SoftplgModelOk);
    /* T1: the file's size and the model's are two numbers (the topology line prints the file's, 4286, beside the model's, 2288) */
    CHECK_EQ(Bytes, 2288);
    CHECK_EQ(S->FileBytes, 4286);
    S->ModelBuffer = (UINT8*)calloc(1, Bytes);
    CHECK(S->ModelBuffer != NULL);
    CHECK_EQ(SoftplgModelBuild(&Input, S->ModelBuffer, Bytes, &S->Model, &Problem), SoftplgModelOk);
    Limits.MaxPipelines = S->H.Ctx->FwConfig.MaxPipelines;
    Limits.MailboxBytes = S->H.Ctx->FwConfig.DownlinkMailboxBytes;
    CHECK_EQ(SoftplgModelValidate(S->Model, &Limits, &Problem), SoftplgModelOk);
    FakeBusInit(&S->Bus, 2, TRUE);
    S->State = (UINT8*)calloc(1, IntcDspStreamsBytes(S->Model));
    CHECK(S->State != NULL);
    S->Ops = StreamOpsTemplate;
    S->Ops.Context = S;
    S->SelfTestOps = StreamSelfTestOps;
    S->SelfTestOps.Context = S;
    IntcDspStreamsInit(&S->Streams, S->State, IntcDspStreamsBytes(S->Model), S->Model, &S->H.Ipc, &S->Bus.Interface, &S->Ops);
    CHECK(S->Streams.Ready);
    S->Wire = TRUE;
    S->PeriodsLeft = MAXULONG32;
    /* the driver's waits on a period event are answered by the model, with this harness */
    FakeBusWaitHook = StreamWaitModel;
    FakeBusWaitContext = S;
}

/* The PCM of the loopback, in a harness that is up */

static
void
StreamStop(
    STREAM_HARNESS* S)
{
    FakeBusWaitHook = NULL;
    FakeBusWaitContext = NULL;
    free(S->State);
    free(S->ModelBuffer);
    free(S->File);
    free(S->Firmware);
    free(S->H.Dsp);
    free(S->H.Ctx);
    free(S->H.Virtual);
}

/* The fake stream behind an MDL */
static
PFAKE_BUS_STREAM
StreamByMdl(
    STREAM_HARNESS* S,
    PMDL Mdl)
{
    UINT32 D, I;

    for (D = 0; D < 2; D++)
        for (I = 0; I < FAKE_BUS_STREAMS; I++)
            if (S->Bus.Stream[D][I].Used && S->Bus.Stream[D][I].Mdl == Mdl)
                return &S->Bus.Stream[D][I];
    return NULL;
}

static
PVOID
StreamMap(
    PVOID Context,
    PMDL Mdl,
    SIZE_T Bytes)
{
    STREAM_HARNESS* S = (STREAM_HARNESS*)Context;
    PFAKE_BUS_STREAM Stream = StreamByMdl(S, Mdl);

    CHECK(Stream != NULL && Stream->Bytes == Bytes);
    return Stream != NULL ? Stream->Memory : NULL;
}

/* The fake stream a period event is registered on, NULL for any other event */
static
PFAKE_BUS_STREAM
StreamByEvent(
    STREAM_HARNESS* S,
    PKEVENT Event)
{
    UINT32 D, I, E;

    for (D = 0; D < 2; D++)
        for (I = 0; I < FAKE_BUS_STREAMS; I++)
        {
            if (!S->Bus.Stream[D][I].Used)
                continue;
            for (E = 0; E < FAKE_BUS_EVENTS; E++)
                if (S->Bus.Stream[D][I].Events[E] == Event)
                    return &S->Bus.Stream[D][I];
        }
    return NULL;
}

/* The first running render stream, NULL when none runs */
static
PFAKE_BUS_STREAM
StreamRender(
    STREAM_HARNESS* S)
{
    UINT32 D, I;

    for (D = 0; D < 2; D++)
        for (I = 0; I < FAKE_BUS_STREAMS; I++)
            if (S->Bus.Stream[D][I].Used && !S->Bus.Stream[D][I].Capture && S->Bus.Stream[D][I].Running)
                return &S->Bus.Stream[D][I];
    return NULL;
}

/*
 * One period of the model, as the bus's interrupt sees it: both streams'
 * entries move Step bytes on around their buffers, and the wire has the
 * capture buffer take what the render buffer holds, Latency frames behind
 * around the cyclic buffer, when it forwards at all, with its one wrong
 * sample: channel 1 of DamageFrame, five over the render's.
 */
static
void
StreamPeriod(
    STREAM_HARNESS* S,
    PFAKE_BUS_STREAM Capture,
    UINT32 Step)
{
    PFAKE_BUS_STREAM Render = StreamRender(S);
    PUCHAR Damage, Source;
    UINT32 FrameBytes, Frames, I;

    FrameBytes = Capture->Format.NumberOfChannels * (Capture->Format.ContainerSize / 8);
    Frames = (UINT32)(Capture->Bytes / FrameBytes);
    Capture->Position = Capture->DmaPosition = (Capture->Position + Step) % (UINT32)Capture->Bytes;
    if (Render == NULL)
        return;
    Render->Position = Render->DmaPosition = (Render->Position + Step) % (UINT32)Render->Bytes;
    if (!S->Wire)
        return;
    for (I = 0; I < Frames; I++)
        memcpy((PUCHAR)Capture->Memory + (SIZE_T)I * FrameBytes,
               (PUCHAR)Render->Memory + (SIZE_T)((I + Frames - S->Latency) % Frames) * FrameBytes, FrameBytes);
    if (S->DamageFrame == 0 || S->DamageFrame >= Frames)
        return;
    /* the sample in its container's width: 16 bits, or 32 for 24 valid bits and more */
    Damage = (PUCHAR)Capture->Memory + (SIZE_T)S->DamageFrame * FrameBytes + FrameBytes / 2;
    Source = (PUCHAR)Render->Memory + (SIZE_T)((S->DamageFrame + Frames - S->Latency) % Frames) * FrameBytes + FrameBytes / 2;
    if (FrameBytes == 4)
        *(INT16*)Damage = (INT16)(*(INT16*)Source + 5);
    else
        *(INT32*)Damage = *(INT32*)Source + 5;
}

/*
 * The model's answer to the driver's wait on a period event, as the
 * kernel's wait: the wait's time passes on the model's clock, and either
 * the periods come before its end, each moving the entries and setting
 * the events as the bus's DPC would, or they stall, and the wait ends in
 * STATUS_TIMEOUT when the time is up. Timeout is relative, in 100 ns
 * units, as the driver's wait gives it. The context is the harness's.
 */
static
NTSTATUS
StreamWaitModel(
    PVOID Context,
    PKEVENT Event,
    PLARGE_INTEGER Timeout)
{
    STREAM_HARNESS* S = (STREAM_HARNESS*)Context;
    PFAKE_BUS_STREAM Capture;
    UINT64 LeftUs, PeriodUs;
    UINT32 Step, FrameBytes;

    CHECK(Timeout != NULL && Timeout->QuadPart < 0);
    if (Timeout == NULL)
        return STATUS_TIMEOUT;
    S->Wakes++;
    LeftUs = (UINT64)(-(LONGLONG)Timeout->QuadPart) / 10;
    Capture = StreamByEvent(S, Event);
    CHECK(Capture != NULL);
    if (Capture == NULL)
        return STATUS_TIMEOUT;
    Step = S->PeriodBytes != 0 ? S->PeriodBytes : (UINT32)(Capture->Bytes / 2);
    FrameBytes = Capture->Format.NumberOfChannels * (Capture->Format.ContainerSize / 8);
    PeriodUs = (UINT64)Step * 1000000 / (Capture->Format.SampleRate * FrameBytes);
    if (S->PeriodsLeft == 0 || PeriodUs > LeftUs)
    {
        /* no period before the wait's end: the time passes, the wait is over */
        FakeDspOps()->DelayUs(S->H.Dsp, (UINT32)LeftUs);
        return STATUS_TIMEOUT;
    }
    S->PeriodsLeft--;
    FakeDspOps()->DelayUs(S->H.Dsp, (UINT32)PeriodUs);
    StreamPeriod(S, Capture, Step);
    /* the event is set as the bus's DPC sets it, and taken by the wait, as a synchronization event's is */
    FakeBusSignal(&S->Bus, (HANDLE)Capture);
    Event->Signaled = FALSE;
    return STATUS_SUCCESS;
}

/*
 * The self-test's wait is the driver's own, IntcDspStreamWaitPeriods, on
 * the device's period event; the model above answers it through the fake
 * bus's wait hook. The capture stream runs on until the pause takes it:
 * StopFrames frames past the last period. The handle is the manager's.
 */
static
NTSTATUS
StreamSelfTestWait(
    PVOID Context,
    HANDLE Handle,
    UINT32 Count,
    UINT32 TimeoutMs,
    PULONG Periods)
{
    STREAM_HARNESS* S = (STREAM_HARNESS*)Context;
    PINTCDSP_STREAM Managed = IntcDspStreamOf(&S->Streams, Handle);
    PFAKE_BUS_STREAM Capture, Render;
    NTSTATUS Status;

    *Periods = 0;
    CHECK_EQ(Count, 2);
    CHECK(TimeoutMs != 0);
    S->Waits++;
    CHECK(Managed != NULL && Managed->Capture);
    if (Managed == NULL)
        return STATUS_INVALID_DEVICE_REQUEST;
    Capture = FakeBusOwned(&S->Bus, Managed->Handle);
    CHECK(Capture != NULL && Capture->Running);
    if (Capture == NULL || !Capture->Running)
        return STATUS_INVALID_DEVICE_REQUEST;
    Render = StreamRender(S);
    CHECK(Render != NULL);
    if (Render != NULL)
        CHECK_EQ(Render->Bytes, Capture->Bytes);
    Status = IntcDspStreamWaitPeriods(&S->Streams, Handle, Count, TimeoutMs, &S->Event, Periods);
    if (NT_SUCCESS(Status))
        Capture->Position = Capture->DmaPosition =
            (Capture->Position + S->StopFrames * Capture->Format.NumberOfChannels * (Capture->Format.ContainerSize / 8)) % (UINT32)Capture->Bytes;
    return Status;
}

static const INTCDSP_SELFTEST_OPS StreamSelfTestOps =
{
    NULL,
    StreamMap,
    StreamSelfTestWait
};

/* The model booted and the transport attached, as the driver's boot leaves them */
static
void
StreamBoot(
    STREAM_HARNESS* S,
    const FAKE_DSP_FAULTS* Faults)
{
    SOF_RI_MODULE Modules[4];
    SOF_RI_IMAGE Parsed;
    UINT8* Image;

    /* a boot after a fault: the model, the loader's context and its buffer of the boot before go first */
    free(S->H.Dsp);
    free(S->H.Ctx);
    free(S->H.Virtual);
    HarnessInit(&S->H);
    Image = (UINT8*)calloc(1, FWLOAD_CASE_IMAGE_BYTES);
    if (Image == NULL)
        exit(2);
    FwloadCaseBuildImage(Image);
    CHECK_EQ(SofRiParse(Image, FWLOAD_CASE_IMAGE_BYTES, Modules, 4, &Parsed), FwloadOk);
    HarnessStart(&S->H, &Parsed, Faults);
    free(Image);
    HarnessAttach(&S->H, NULL, FALSE);
}

/* The PCM of the loopback, in a harness that is up */
static
UINT32
StreamPcm(
    STREAM_HARNESS* S)
{
    UINT32 Pcm = IntcDspStreamPcm(&S->Streams, INTCDSP_SELFTEST_PCM);

    CHECK(Pcm != SOFTPLG_NO_PCM);
    CHECK_EQ(IntcDspStreamPcm(&S->Streams, "No such PCM"), SOFTPLG_NO_PCM);
    return Pcm;
}

/* The codec driver holds the first render stream and the first two capture streams, as it does on a machine with a codec: the DSP's streams then carry the model's guest tags 2 and 3 */
static
void
StreamTakeCodecStreams(
    STREAM_HARNESS* S)
{
    HDAUDIO_STREAM_FORMAT Format;
    HANDLE Taken;
    UINT8 Tag;

    memset(&Format, 0, sizeof(Format));
    CHECK_EQ(S->Bus.Interface.GetRenderStream(S->Bus.Interface.Context, Format, &Taken, &Tag), STATUS_SUCCESS);
    CHECK_EQ(Tag, 1);
    CHECK_EQ(S->Bus.Interface.GetCaptureStream(S->Bus.Interface.Context, Format, &Taken, &Tag), STATUS_SUCCESS);
    CHECK_EQ(Tag, 1);
    CHECK_EQ(S->Bus.Interface.GetCaptureStream(S->Bus.Interface.Context, Format, &Taken, &Tag), STATUS_SUCCESS);
    CHECK_EQ(Tag, 2);
}

/* Every stream object free, no path held, nothing built, the link ready and nothing faulted */
static
void
StreamQuiescent(
    STREAM_HARNESS* S,
    UINT32 Pcm)
{
    UINT32 Sx;

    for (Sx = 0; Sx < INTCDSP_STREAMS_MAX; Sx++)
        CHECK_EQ(S->Streams.Stream[Sx].State, IntcDspStreamStateFree);
    CHECK_EQ(S->Streams.Path[Pcm].Open, 0);
    CHECK_EQ(S->Streams.Path[Pcm].Held, 0);
    CHECK_EQ(S->Streams.Path[Pcm].Running, 0);
    CHECK(S->Streams.Path[Pcm].Built == FALSE);
    CHECK_EQ(S->Streams.PipelineInstance[0], 0);
    CHECK_EQ(S->Streams.Faulted, FALSE);
    CHECK_EQ(S->Faults, 0);
    CHECK_EQ(S->H.Ipc.Link, SofIpcLinkReady);
    CHECK_EQ(S->H.Dsp->Pipeline[1].Used, FALSE);
    CHECK(FakeDspModuleFind(S->H.Dsp, 5, 0) < 0 && FakeDspModuleFind(S->H.Dsp, 5, 1) < 0 && FakeDspModuleFind(S->H.Dsp, 7, 0) < 0);
}

/* (i1) The golden trace: the loopback pair's transitions in the plan's order, the words the model's table lists, and the stream tag in each copier's word 17 */
static
void
CheckStreamGolden(
    void)
{
    STREAM_HARNESS* S = (STREAM_HARNESS*)calloc(1, sizeof(*S));
    HANDLE Render = NULL, Capture = NULL;
    PULONG Register;
    const INTCDSP_TRACE* T;
    const IPC4_STREAM_CASE* C;
    INT32 Playback, CaptureCopier, Gain;
    UINT32 Pcm, I;
    SIZE_T Requests;

    CHECK(S != NULL);
    if (S == NULL)
        return;
    StreamStart(S, NULL);
    StreamTakeCodecStreams(S);
    Pcm = StreamPcm(S);
    Requests = S->H.Dsp->TransportRequests;
    CHECK_EQ(IntcDspStreamOpen(&S->Streams, Pcm, FALSE, 48000, 2, 32, 32, &Render), STATUS_SUCCESS);
    CHECK_EQ(IntcDspStreamOpen(&S->Streams, Pcm, TRUE, 48000, 2, 32, 32, &Capture), STATUS_SUCCESS);
    CHECK(Render != NULL && Capture != NULL);
    CHECK_EQ(IntcDspStreamOf(&S->Streams, Render)->Tag, IPC4_STREAM_CASE_RENDER_TAG);
    CHECK_EQ(IntcDspStreamOf(&S->Streams, Capture)->Tag, IPC4_STREAM_CASE_CAPTURE_TAG);
    /* the format index is the 32-bit one of the copier's host-side formats */
    CHECK_EQ(S->Model->Formats[S->Model->Widgets[IntcDspStreamOf(&S->Streams, Render)->Copier].FirstFormat +
                                IntcDspStreamOf(&S->Streams, Render)->Format].Words[SOFTPLG_FORMAT_WORD_DEPTH], 32);
    CHECK_EQ(S->Model->Formats[S->Model->Widgets[IntcDspStreamOf(&S->Streams, Capture)->Copier].FirstFormat +
                                S->Model->Widgets[IntcDspStreamOf(&S->Streams, Capture)->Copier].InputFormats +
                                IntcDspStreamOf(&S->Streams, Capture)->Format].Words[SOFTPLG_FORMAT_WORD_DEPTH], 32);
    /* the stream format the bus received: the valid bits in their container */
    CHECK_EQ(S->Bus.Stream[0][1].Format.SampleRate, 48000);
    CHECK_EQ(S->Bus.Stream[0][1].Format.ValidBitsPerSample, 32);
    CHECK_EQ(S->Bus.Stream[0][1].Format.ContainerSize, 32);
    CHECK_EQ(S->Bus.Stream[0][1].Format.NumberOfChannels, 2);
    /* the render stream's allocation builds the PCM once for both streams: the capture copier names the capture stream already */
    CHECK_EQ(IntcDspStreamAllocate(&S->Streams, Render, IPC4_STREAM_CASE_BUFFER_BYTES, 2), STATUS_SUCCESS);
    Playback = FakeDspModuleFind(S->H.Dsp, 5, 0);
    CaptureCopier = FakeDspModuleFind(S->H.Dsp, 5, 1);
    Gain = FakeDspModuleFind(S->H.Dsp, 7, 0);
    CHECK(Playback >= 0 && CaptureCopier >= 0 && Gain >= 0);
    if (Playback < 0 || CaptureCopier < 0 || Gain < 0)
        goto Out;
    /* the host-output copier: 21 words, the gateway node id = type << 8 | tag - 1, the input format the stream's, the gateway buffer two milliseconds of its input */
    CHECK(S->H.Dsp->Module[Playback].Copier);
    CHECK_EQ(S->H.Dsp->Module[Playback].Words, 21);
    CHECK_EQ(S->H.Dsp->Module[Playback].Config[SOFTPLG_COPIER_NODE_ID_WORD], IPC4_STREAM_CASE_PLAYBACK_NODE);
    CHECK_EQ(S->H.Dsp->Module[Playback].GwType, 0);
    CHECK_EQ(S->H.Dsp->Module[Playback].GwIndex, IPC4_STREAM_CASE_RENDER_TAG - 1);
    CHECK_EQ(S->H.Dsp->Module[Playback].Config[4], 48000);
    CHECK_EQ(S->H.Dsp->Module[Playback].Config[5], 32);
    CHECK_EQ(S->H.Dsp->Module[Playback].Config[9], (1u << 16) | (32u << 8) | 2);
    CHECK_EQ(S->H.Dsp->Module[Playback].Config[SOFTPLG_COPIER_DMA_BUFFER_WORD], 2 * S->H.Dsp->Module[Playback].Config[1]);
    CHECK_EQ(S->H.Dsp->Module[Playback].Config[SOFTPLG_COPIER_CONFIG_LENGTH_WORD], 1);
    CHECK_EQ(S->H.Dsp->Module[Playback].Config[SOFTPLG_COPIER_ATTRIBUTES_WORD], 0);
    /* the host-input copier: its output format is the capture stream's */
    CHECK(S->H.Dsp->Module[CaptureCopier].Copier);
    CHECK_EQ(S->H.Dsp->Module[CaptureCopier].Config[SOFTPLG_COPIER_NODE_ID_WORD], IPC4_STREAM_CASE_CAPTURE_NODE);
    CHECK_EQ(S->H.Dsp->Module[CaptureCopier].GwType, 1);
    CHECK_EQ(S->H.Dsp->Module[CaptureCopier].GwIndex, IPC4_STREAM_CASE_CAPTURE_TAG - 1);
    CHECK_EQ(S->H.Dsp->Module[CaptureCopier].Config[SOFTPLG_COPIER_OUT_FORMAT_WORD + 0], 48000);
    CHECK_EQ(S->H.Dsp->Module[CaptureCopier].Config[SOFTPLG_COPIER_OUT_FORMAT_WORD + 1], 32);
    CHECK_EQ(S->H.Dsp->Module[CaptureCopier].Config[SOFTPLG_COPIER_OUT_FORMAT_WORD + 5], (1u << 16) | (32u << 8) | 2);
    CHECK_EQ(S->H.Dsp->Module[CaptureCopier].Config[SOFTPLG_COPIER_DMA_BUFFER_WORD], 2 * S->H.Dsp->Module[CaptureCopier].Config[2]);
    /* the gain: a base configuration of 10 words, no gateway */
    CHECK(!S->H.Dsp->Module[Gain].Copier);
    CHECK_EQ(S->H.Dsp->Module[Gain].Words, 10);
    CHECK_EQ(S->H.Dsp->Module[Gain].Config[1], S->H.Dsp->Module[Gain].Config[2]);
    CHECK_EQ(S->H.Dsp->Module[Gain].Config[4], 48000);
    /* the binds: the gain behind the host-output copier, the host-input copier behind the gain */
    CHECK_EQ(S->H.Dsp->Module[Gain].Src, Playback);
    CHECK_EQ(S->H.Dsp->Module[CaptureCopier].Src, Gain);
    CHECK_EQ(S->H.Dsp->Module[Playback].Src, FAKE_DSP_NO_MODULE);
    CHECK_EQ(S->H.Dsp->ModuleInits, 3);
    CHECK_EQ(S->H.Dsp->ModuleBinds, 2);
    CHECK_EQ(S->H.Dsp->ModuleDeletes, 0);
    CHECK_EQ(S->H.Dsp->ModuleUpdates, 0);
    CHECK_EQ(S->Streams.PipelineInstance[0], 1);
    CHECK_EQ(S->H.Dsp->Pipeline[1].Used, TRUE);
    CHECK_EQ(S->H.Dsp->Pipeline[1].State, SOFIPC4_PIPELINE_STATE_UNINITIALIZED);
    /* the pipeline was created with the scheduler's values: priority 0, no pages, core 0, no low-power flag */
    CHECK_EQ(S->H.Dsp->RequestPrimary[0], 0x11010000 | FWLOAD_IPC_BUSY);
    CHECK_EQ(S->H.Dsp->RequestExtension[0], 0);
    CHECK_EQ(IntcDspStreamAllocate(&S->Streams, Capture, IPC4_STREAM_CASE_BUFFER_BYTES, 2), STATUS_SUCCESS);
    CHECK_EQ(S->H.Dsp->TransportRequests - Requests, 6);
    /* the software bound is at the buffer size, and the whole cycle is valid */
    CHECK(S->Bus.Stream[0][1].Spib && S->Bus.Stream[1][2].Spib);
    CHECK_EQ(S->Bus.Stream[0][1].SpibValue, IPC4_STREAM_CASE_BUFFER_BYTES);
    CHECK_EQ(S->Bus.Stream[1][2].SpibValue, IPC4_STREAM_CASE_BUFFER_BYTES);
    /* the capture stream runs first, the render stream after it; the pipelines were paused and running before either */
    CHECK_EQ(IntcDspStreamRun(&S->Streams, Capture), STATUS_SUCCESS);
    CHECK_EQ(S->H.Dsp->Pipeline[1].State, SOFIPC4_PIPELINE_STATE_RUNNING);
    CHECK(S->Bus.Stream[1][2].Running && !S->Bus.Stream[0][1].Running);
    CHECK_EQ(IntcDspStreamRun(&S->Streams, Render), STATUS_SUCCESS);
    CHECK(S->Bus.Stream[0][1].Running);
    CHECK_EQ(IntcDspStreamPositionRegister(&S->Streams, Render, &Register), STATUS_SUCCESS);
    CHECK(Register == &S->Bus.Stream[0][1].DmaPosition);
    CHECK_EQ(IntcDspStreamLinkPositionRegister(&S->Streams, Render, &Register), STATUS_SUCCESS);
    CHECK(Register == &S->Bus.Stream[0][1].Position);
    /* the capture stream pauses alone: the pipeline stays running for the render stream */
    CHECK_EQ(IntcDspStreamPause(&S->Streams, Capture), STATUS_SUCCESS);
    CHECK(!S->Bus.Stream[1][2].Running && S->Bus.Stream[0][1].Running);
    CHECK_EQ(S->H.Dsp->Pipeline[1].State, SOFIPC4_PIPELINE_STATE_RUNNING);
    CHECK_EQ(IntcDspStreamPause(&S->Streams, Render), STATUS_SUCCESS);
    CHECK_EQ(S->H.Dsp->Pipeline[1].State, SOFIPC4_PIPELINE_STATE_PAUSED);
    /* the capture stream frees first: its copier is told its stream is going, and nothing else is deleted */
    CHECK_EQ(IntcDspStreamFree(&S->Streams, Capture), STATUS_SUCCESS);
    CHECK_EQ(S->H.Dsp->ModuleUpdates, 1);
    CHECK_EQ(S->H.Dsp->Module[CaptureCopier].Config[SOFTPLG_COPIER_NODE_ID_WORD], IPC4_STREAM_CASE_PLACEHOLDER_NODE);
    CHECK_EQ(S->H.Dsp->Module[CaptureCopier].GwIndex, 0xFF);
    CHECK_EQ(S->H.Dsp->ModuleDeletes, 0);
    CHECK_EQ(S->Streams.Path[Pcm].Held, 1);
    /* the render stream frees last: the pipeline is reset, the instances and the pipeline deleted */
    CHECK_EQ(IntcDspStreamFree(&S->Streams, Render), STATUS_SUCCESS);
    CHECK_EQ(S->H.Dsp->ModuleDeletes, 3);
    CHECK(!S->H.Dsp->Pipeline[1].Used);
    CHECK_EQ(S->Streams.PipelineInstance[0], 0);
    CHECK_EQ(IntcDspStreamClose(&S->Streams, Capture), STATUS_SUCCESS);
    CHECK_EQ(IntcDspStreamClose(&S->Streams, Render), STATUS_SUCCESS);

Out:
    /* the trace is the golden one, line for line */
    CHECK_EQ(S->Streams.TraceCount, IPC4_STREAM_TRACE_ROWS);
    for (I = 0; I < IPC4_STREAM_TRACE_ROWS && I < S->Streams.TraceCount; I++)
    {
        T = &S->Streams.Trace[I];
        C = &Ipc4StreamTrace[I];
        if (T->Kind != C->Kind || T->Capture != C->Capture || T->Tag != C->Tag || T->Primary != C->Primary || T->Extension != C->Extension)
            fprintf(stderr, "  trace row %u (%s): kind %u/%u capture %u/%u tag %u/%u primary 0x%08x/0x%08x extension 0x%08x/0x%08x\n",
                    I, C->Step, T->Kind, C->Kind, T->Capture, C->Capture, T->Tag, C->Tag, T->Primary, C->Primary, T->Extension, C->Extension);
        CHECK_EQ(T->Kind, C->Kind);
        CHECK_EQ(T->Capture, C->Capture);
        CHECK_EQ(T->Tag, C->Tag);
        CHECK_EQ(T->Primary, C->Primary);
        CHECK_EQ(T->Extension, C->Extension);
        /* every request was a success; the buffer and its bound are the buffer's size */
        if (T->Kind == INTCDSP_TRACE_IPC)
            CHECK_EQ(T->Value, 0);
        if (T->Kind == INTCDSP_TRACE_BUFFER || T->Kind == INTCDSP_TRACE_SPIB)
            CHECK_EQ(T->Value, IPC4_STREAM_CASE_BUFFER_BYTES);
    }
    /* the transitions' starts are where the case header says */
    CHECK_STR(Ipc4StreamTrace[IPC4_STREAM_TRACE_ALLOCATE].Step, "allocate: the render buffer");
    CHECK_STR(Ipc4StreamTrace[IPC4_STREAM_TRACE_RUN].Step, "run: pipeline 1 paused");
    CHECK_STR(Ipc4StreamTrace[IPC4_STREAM_TRACE_PAUSE].Step, "pause: the capture stream");
    CHECK_STR(Ipc4StreamTrace[IPC4_STREAM_TRACE_FREE].Step, "free: the copier told its stream is going");
    CHECK_STR(Ipc4StreamTrace[IPC4_STREAM_TRACE_CLOSE].Step, "close: the capture tag");
    /* the bus gave back everything the pair took */
    CHECK_EQ(FakeBusCount(&S->Bus, FAKE_BUS_EVENT_OPEN), 5);
    CHECK_EQ(FakeBusCount(&S->Bus, FAKE_BUS_EVENT_FREE), 2);
    CHECK_EQ(FakeBusCount(&S->Bus, FAKE_BUS_EVENT_BUFFER), 2);
    CHECK_EQ(FakeBusCount(&S->Bus, FAKE_BUS_EVENT_BUFFER_FREE), 2);
    CHECK_EQ(FakeBusCount(&S->Bus, FAKE_BUS_EVENT_RUN), 2);
    CHECK_EQ(FakeBusCount(&S->Bus, FAKE_BUS_EVENT_STOP), 2);
    CHECK_EQ(FakeBusCount(&S->Bus, FAKE_BUS_EVENT_SPIB), 2);
    CHECK_EQ(FakeBusCount(&S->Bus, FAKE_BUS_EVENT_SPIB_OFF), 2);
    CHECK_EQ(FakeBusCount(&S->Bus, FAKE_BUS_EVENT_REGISTER), 0);
    CHECK_EQ(S->Bus.Stream[0][1].Runs, 1);
    CHECK_EQ(S->Bus.Stream[0][1].Stops, 1);
    CHECK(!S->Bus.Stream[0][1].Spib && !S->Bus.Stream[0][1].Buffer && !S->Bus.Stream[0][1].Used);
    StreamQuiescent(S, Pcm);
    StreamStop(S);
    free(S);
}

/* (i2) A format the host copier does not offer is refused before anything is taken; the depths the copier offers open with their format index */
static
void
CheckStreamFormats(
    void)
{
    STREAM_HARNESS* S = (STREAM_HARNESS*)calloc(1, sizeof(*S));
    HANDLE Render = NULL, Capture = NULL, Other;
    const SOFTPLG_IPC4_WIDGET* Copier;
    const SOFTPLG_IPC4_FORMAT* F;
    PINTCDSP_STREAM Stream;
    UINT32 Pcm, Index, Depth;
    static const ULONG Depths[] = { 16, 24, 32 };

    CHECK(S != NULL);
    if (S == NULL)
        return;
    StreamStart(S, NULL);
    Pcm = StreamPcm(S);
    CHECK_EQ(IntcDspStreamOpen(&S->Streams, 99, FALSE, 48000, 2, 32, 32, &Render), STATUS_INVALID_PARAMETER);
    /* 44.1 kHz, a packed 24-bit stream, one channel, six channels, 20 valid bits: none is among the copier's formats */
    CHECK_EQ(IntcDspStreamOpen(&S->Streams, Pcm, FALSE, 44100, 2, 16, 16, &Render), STATUS_NOT_SUPPORTED);
    CHECK_EQ(IntcDspStreamOpen(&S->Streams, Pcm, FALSE, 48000, 2, 24, 24, &Render), STATUS_NOT_SUPPORTED);
    CHECK_EQ(IntcDspStreamOpen(&S->Streams, Pcm, FALSE, 48000, 1, 16, 16, &Render), STATUS_NOT_SUPPORTED);
    CHECK_EQ(IntcDspStreamOpen(&S->Streams, Pcm, FALSE, 48000, 6, 32, 32, &Render), STATUS_NOT_SUPPORTED);
    CHECK_EQ(IntcDspStreamOpen(&S->Streams, Pcm, TRUE, 48000, 2, 20, 32, &Render), STATUS_NOT_SUPPORTED);
    CHECK_EQ(IntcDspStreamOpen(&S->Streams, Pcm, FALSE, 48000, 2, 8, 8, &Render), STATUS_NOT_SUPPORTED);
    CHECK(Render == NULL);
    CHECK_EQ(S->Streams.TraceCount, 0);
    CHECK_EQ(S->H.Dsp->TransportRequests, 0);
    CHECK_EQ(S->Streams.Path[Pcm].Open, 0);
    for (Index = 0; Index < 2; Index++)
        for (Depth = 0; Depth < FAKE_BUS_STREAMS; Depth++)
            CHECK(!S->Bus.Stream[Index][Depth].Used);
    /* every depth the copier offers opens, render and capture, with the index of its format */
    for (Index = 0; Index < 3; Index++)
    {
        CHECK_EQ(IntcDspStreamOpen(&S->Streams, Pcm, FALSE, 48000, 2, Depths[Index], Depths[Index] <= 16 ? 16 : 32, &Render), STATUS_SUCCESS);
        CHECK_EQ(IntcDspStreamOpen(&S->Streams, Pcm, TRUE, 48000, 2, Depths[Index], Depths[Index] <= 16 ? 16 : 32, &Capture), STATUS_SUCCESS);
        Stream = IntcDspStreamOf(&S->Streams, Render);
        Copier = &S->Model->Widgets[Stream->Copier];
        F = &S->Model->Formats[Copier->FirstFormat + Stream->Format];
        CHECK_EQ(SOFTPLG_FORMAT_VALID_OF(F->Words[SOFTPLG_FORMAT_WORD_CFG]), Depths[Index]);
        CHECK_EQ(F->Words[SOFTPLG_FORMAT_WORD_DEPTH], Depths[Index] <= 16 ? 16 : 32);
        /* the bus got the valid bits in the container the sample rule gives them */
        CHECK_EQ(S->Bus.Stream[0][0].Format.ValidBitsPerSample, Depths[Index]);
        CHECK_EQ(S->Bus.Stream[0][0].Format.ContainerSize, Depths[Index] <= 16 ? 16 : 32);
        CHECK_EQ(IntcDspStreamClose(&S->Streams, Render), STATUS_SUCCESS);
        CHECK_EQ(IntcDspStreamClose(&S->Streams, Capture), STATUS_SUCCESS);
    }
    CHECK_EQ(S->H.Dsp->TransportRequests, 0);
    CHECK_EQ(S->Streams.Path[Pcm].Open, 0);
    /* a stream object is one at a time: a second open of the same handle is refused, and a bad handle too */
    CHECK_EQ(IntcDspStreamOpen(&S->Streams, Pcm, FALSE, 48000, 2, 32, 32, &Render), STATUS_SUCCESS);
    CHECK_EQ(IntcDspStreamRun(&S->Streams, Render), STATUS_INVALID_DEVICE_REQUEST);
    CHECK_EQ(IntcDspStreamAllocate(&S->Streams, (HANDLE)&S->Streams, IPC4_STREAM_CASE_BUFFER_BYTES, 2), STATUS_INVALID_HANDLE);
    CHECK_EQ(IntcDspStreamAllocate(&S->Streams, Render, IPC4_STREAM_CASE_BUFFER_BYTES, 3), STATUS_INVALID_PARAMETER);
    CHECK_EQ(IntcDspStreamAllocate(&S->Streams, Render, 1001, 2), STATUS_INVALID_PARAMETER);
    CHECK_EQ(IntcDspStreamAllocate(&S->Streams, Render, IPC4_STREAM_CASE_BUFFER_BYTES, 1), STATUS_SUCCESS);
    CHECK_EQ(IntcDspStreamFree(&S->Streams, Render), STATUS_SUCCESS);
    Other = (HANDLE)Render;
    CHECK_EQ(IntcDspStreamClose(&S->Streams, Render), STATUS_SUCCESS);
    CHECK_EQ(IntcDspStreamClose(&S->Streams, Other), STATUS_INVALID_HANDLE);
    StreamQuiescent(S, Pcm);
    StreamStop(S);
    free(S);
}

/* (i3) An exhausted stream pool, a bus that fails, and a buffer the bus does not give: nothing is held, nothing is sent */
static
void
CheckStreamPool(
    void)
{
    STREAM_HARNESS* S = (STREAM_HARNESS*)calloc(1, sizeof(*S));
    HANDLE Render = NULL, Capture = NULL, Taken;
    HDAUDIO_STREAM_FORMAT Format;
    UINT8 Tag;
    UINT32 Pcm;

    CHECK(S != NULL);
    if (S == NULL)
        return;
    StreamStart(S, NULL);
    Pcm = StreamPcm(S);
    memset(&Format, 0, sizeof(Format));
    /* one render stream in the pool, and the codec driver holds it */
    S->Bus.RenderStreams = 1;
    CHECK_EQ(S->Bus.Interface.GetRenderStream(S->Bus.Interface.Context, Format, &Taken, &Tag), STATUS_SUCCESS);
    CHECK_EQ(IntcDspStreamOpen(&S->Streams, Pcm, FALSE, 48000, 2, 32, 32, &Render), STATUS_INSUFFICIENT_RESOURCES);
    CHECK(Render == NULL);
    CHECK_EQ(S->Streams.Path[Pcm].Open, 0);
    CHECK_EQ(S->Streams.TraceCount, 0);
    /* a bus that answers an error */
    S->Bus.CaptureStatus = STATUS_NO_SUCH_DEVICE;
    CHECK_EQ(IntcDspStreamOpen(&S->Streams, Pcm, TRUE, 48000, 2, 32, 32, &Capture), STATUS_NO_SUCH_DEVICE);
    /* a stream opens and its buffer is refused: the stream stays open with its tag, nothing was told to the firmware */
    S->Bus.RenderStreams = FAKE_BUS_STREAMS;
    S->Bus.BufferStatus = STATUS_INSUFFICIENT_RESOURCES;
    CHECK_EQ(IntcDspStreamOpen(&S->Streams, Pcm, FALSE, 48000, 2, 32, 32, &Render), STATUS_SUCCESS);
    CHECK_EQ(IntcDspStreamAllocate(&S->Streams, Render, IPC4_STREAM_CASE_BUFFER_BYTES, 2), STATUS_INSUFFICIENT_RESOURCES);
    CHECK_EQ(IntcDspStreamOf(&S->Streams, Render)->State, IntcDspStreamStateOpen);
    CHECK_EQ(S->Streams.Path[Pcm].Held, 0);
    CHECK_EQ(S->H.Dsp->TransportRequests, 0);
    CHECK_EQ(FakeBusCount(&S->Bus, FAKE_BUS_EVENT_BUFFER), 0);
    /* the buffer comes on the next try, and the stream runs its course */
    S->Bus.BufferStatus = STATUS_SUCCESS;
    CHECK_EQ(IntcDspStreamAllocate(&S->Streams, Render, IPC4_STREAM_CASE_BUFFER_BYTES, 2), STATUS_SUCCESS);
    CHECK_EQ(IntcDspStreamRun(&S->Streams, Render), STATUS_SUCCESS);
    CHECK_EQ(IntcDspStreamPause(&S->Streams, Render), STATUS_SUCCESS);
    CHECK_EQ(IntcDspStreamFree(&S->Streams, Render), STATUS_SUCCESS);
    CHECK_EQ(IntcDspStreamClose(&S->Streams, Render), STATUS_SUCCESS);
    CHECK_EQ(S->H.Dsp->ModuleInits, 3);
    CHECK_EQ(S->H.Dsp->ModuleDeletes, 3);
    CHECK_EQ(FakeBusCount(&S->Bus, FAKE_BUS_EVENT_FREE), 1);
    CHECK_EQ(FakeBusCount(&S->Bus, FAKE_BUS_EVENT_BUFFER_FREE), 1);
    StreamQuiescent(S, Pcm);
    /* a bus without the stream members does not start the manager, and the transitions say so */
    FakeBusTruncate(&S->Bus, RTL_SIZEOF_THROUGH_FIELD(ADSP_BUS_INTERFACE, GetTopologySources));
    CHECK(!IntcDspBusStreams(&S->Bus.Interface));
    IntcDspStreamsInit(&S->Streams, S->State, IntcDspStreamsBytes(S->Model), S->Model, &S->H.Ipc, &S->Bus.Interface, &S->Ops);
    CHECK(!S->Streams.Ready);
    CHECK(StreamLogCount(S, "streams: not started (the bus carries no stream members)\n") == 1);
    CHECK_EQ(IntcDspStreamOpen(&S->Streams, Pcm, FALSE, 48000, 2, 32, 32, &Render), STATUS_DEVICE_NOT_READY);
    CHECK_EQ(IntcDspStreamClockRegister(&S->Streams, (PULONG*)&Taken), STATUS_NOT_SUPPORTED);
    StreamStop(S);
    free(S);
}

/* (i4) A timeout at each request of the allocate and the run transitions: the link faults, nothing more is sent, every stream's buffer and tag go back to the bus, the driver is told, and the next boot starts over */
static
void
CheckStreamTimeouts(
    void)
{
    STREAM_HARNESS* S;
    FAKE_DSP_FAULTS Faults;
    HANDLE Render = NULL, Capture = NULL, Fresh;
    NTSTATUS Status;
    UINT32 K, Sx, Pcm;

    for (K = 1; K <= IPC4_STREAM_TRACE_ALLOCATE_REQUESTS + IPC4_STREAM_TRACE_RUN_REQUESTS; K++)
    {
        S = (STREAM_HARNESS*)calloc(1, sizeof(*S));
        CHECK(S != NULL);
        if (S == NULL)
            return;
        Faults = *FakeDspDefaultFaults();
        Faults.NeverReplyAtRequest = K;
        StreamStart(S, &Faults);
        StreamTakeCodecStreams(S);
        Pcm = StreamPcm(S);
        CHECK_EQ(IntcDspStreamOpen(&S->Streams, Pcm, FALSE, 48000, 2, 32, 32, &Render), STATUS_SUCCESS);
        CHECK_EQ(IntcDspStreamOpen(&S->Streams, Pcm, TRUE, 48000, 2, 32, 32, &Capture), STATUS_SUCCESS);
        Status = IntcDspStreamAllocate(&S->Streams, Render, IPC4_STREAM_CASE_BUFFER_BYTES, 2);
        if (K > IPC4_STREAM_TRACE_ALLOCATE_REQUESTS)
        {
            /* the capture stream's run: its paused request is the seventh, its running request the eighth */
            CHECK_EQ(Status, STATUS_SUCCESS);
            CHECK_EQ(IntcDspStreamAllocate(&S->Streams, Capture, IPC4_STREAM_CASE_BUFFER_BYTES, 2), STATUS_SUCCESS);
            Status = IntcDspStreamRun(&S->Streams, Capture);
        }
        /* the request that was dropped is the one the firmware never answers: the transport's timeout, the manager's status */
        CHECK_EQ(Status, STATUS_IO_TIMEOUT);
        CHECK_EQ(S->H.Dsp->TransportRequests, K);
        CHECK_EQ(S->H.Ipc.Timeouts, 1);
        CHECK(S->H.Ipc.Last.ElapsedUs >= 500000);
        CHECK_EQ(S->H.Ipc.Link, SofIpcLinkFaulted);
        CHECK_EQ(S->Streams.Faulted, TRUE);
        CHECK_EQ(S->Faults, 1);
        /* every stream's buffer and tag went back to the bus; none had run when the fault came */
        CHECK_EQ(FakeBusCount(&S->Bus, FAKE_BUS_EVENT_FREE), 2);
        CHECK_EQ(FakeBusCount(&S->Bus, FAKE_BUS_EVENT_BUFFER_FREE), K >= IPC4_STREAM_TRACE_ALLOCATE_REQUESTS + 1 ? 2 : 1);
        CHECK_EQ(FakeBusCount(&S->Bus, FAKE_BUS_EVENT_STOP), 0);
        CHECK_EQ(FakeBusCount(&S->Bus, FAKE_BUS_EVENT_SPIB_OFF), K >= IPC4_STREAM_TRACE_ALLOCATE_REQUESTS + 1 ? 2 : 1);
        for (Sx = 0; Sx < INTCDSP_STREAMS_MAX; Sx++)
            CHECK(S->Streams.Stream[Sx].State == IntcDspStreamStateFree || S->Streams.Stream[Sx].State == IntcDspStreamStateReleased);
        CHECK(S->Streams.Path[Pcm].Held == 0 && S->Streams.Path[Pcm].Built == FALSE);
        CHECK_EQ(S->Streams.PipelineInstance[0], 0);
        CHECK(StreamLogCount(S, "the link is faulted\n") == 1);
        CHECK(StreamLogCount(S, "streams: released after the fault; nothing is sent until the DSP is booted again\n") == 1);
        /* nothing more is sent: the transitions refuse, and the fault is reported once */
        CHECK_EQ(IntcDspStreamOpen(&S->Streams, Pcm, FALSE, 48000, 2, 32, 32, &Fresh), STATUS_DEVICE_NOT_READY);
        CHECK(Fresh == NULL);
        CHECK_EQ(IntcDspStreamRun(&S->Streams, Capture), STATUS_DEVICE_NOT_READY);
        CHECK_EQ(IntcDspStreamClose(&S->Streams, Render), STATUS_SUCCESS);
        CHECK_EQ(IntcDspStreamClose(&S->Streams, Capture), STATUS_SUCCESS);
        for (Sx = 0; Sx < INTCDSP_STREAMS_MAX; Sx++)
            CHECK_EQ(S->Streams.Stream[Sx].State, IntcDspStreamStateFree);
        CHECK_EQ(S->H.Dsp->TransportRequests, K);
        CHECK_EQ(S->Faults, 1);
        /* the next boot initializes the manager again, and the streams work */
        StreamBoot(S, NULL);
        IntcDspStreamsInit(&S->Streams, S->State, IntcDspStreamsBytes(S->Model), S->Model, &S->H.Ipc, &S->Bus.Interface, &S->Ops);
        CHECK(S->Streams.Ready && !S->Streams.Faulted);
        CHECK_EQ(IntcDspStreamOpen(&S->Streams, Pcm, FALSE, 48000, 2, 32, 32, &Render), STATUS_SUCCESS);
        CHECK_EQ(IntcDspStreamAllocate(&S->Streams, Render, IPC4_STREAM_CASE_BUFFER_BYTES, 2), STATUS_SUCCESS);
        /* the instance ids of the boot before are not reused: the pipeline is the boot's first one again, its own count starting over */
        CHECK_EQ(S->Streams.PipelineInstance[0], 1);
        CHECK_EQ(IntcDspStreamFree(&S->Streams, Render), STATUS_SUCCESS);
        CHECK_EQ(IntcDspStreamClose(&S->Streams, Render), STATUS_SUCCESS);
        StreamStop(S);
        free(S);
    }
}

/* (i5) A busy transport at each request of the build (a second caller holds the link): the link stays usable, what was built is deleted again in order, the buffer is released and the tag kept, and the next allocation builds afresh */
static
void
CheckStreamBusy(
    void)
{
    STREAM_HARNESS* S;
    HANDLE Render = NULL, Capture = NULL;
    UINT32 K, Pcm, Sent, Teardown, Sx;

    for (K = 1; K <= IPC4_STREAM_TRACE_ALLOCATE_REQUESTS; K++)
    {
        S = (STREAM_HARNESS*)calloc(1, sizeof(*S));
        CHECK(S != NULL);
        if (S == NULL)
            return;
        StreamStart(S, NULL);
        StreamTakeCodecStreams(S);
        Pcm = StreamPcm(S);
        S->H.LockBusyAtRequest = K;
        CHECK_EQ(IntcDspStreamOpen(&S->Streams, Pcm, FALSE, 48000, 2, 32, 32, &Render), STATUS_SUCCESS);
        CHECK_EQ(IntcDspStreamOpen(&S->Streams, Pcm, TRUE, 48000, 2, 32, 32, &Capture), STATUS_SUCCESS);
        CHECK_EQ(IntcDspStreamAllocate(&S->Streams, Render, IPC4_STREAM_CASE_BUFFER_BYTES, 2), STATUS_RETRY);
        /* the link is ready and nothing faulted */
        CHECK_EQ(S->H.Ipc.Link, SofIpcLinkReady);
        CHECK_EQ(S->Streams.Faulted, FALSE);
        CHECK_EQ(S->Faults, 0);
        /* the stream is as an open left it: its buffer released, its tag kept */
        CHECK_EQ(IntcDspStreamOf(&S->Streams, Render)->State, IntcDspStreamStateOpen);
        CHECK(IntcDspStreamOf(&S->Streams, Render)->Mdl == NULL);
        CHECK_EQ(FakeBusCount(&S->Bus, FAKE_BUS_EVENT_BUFFER_FREE), 1);
        CHECK_EQ(FakeBusCount(&S->Bus, FAKE_BUS_EVENT_SPIB_OFF), 1);
        CHECK_EQ(FakeBusCount(&S->Bus, FAKE_BUS_EVENT_FREE), 0);
        CHECK_EQ(S->Streams.Path[Pcm].Held, 0);
        /* what the build had told the firmware is deleted again: the pipeline reset, its instances, the pipeline */
        CHECK_EQ(S->H.Dsp->ModuleInits, S->H.Dsp->ModuleDeletes);
        CHECK(!S->H.Dsp->Pipeline[1].Used);
        CHECK_EQ(S->Streams.PipelineInstance[0], 0);
        for (Sx = 0; Sx < S->Model->WidgetCount; Sx++)
            CHECK_EQ(S->Streams.WidgetFlags[Sx], 0);
        /* the requests the firmware saw: the build's before the busy one, then the teardown's (reset, the instances initialized, delete pipeline) */
        Teardown = K == 1 ? 0 : (K == 2 ? 2 : 1 + (K < 6 ? K - 2 : 3) + 1);
        Sent = (K - 1) + Teardown;
        CHECK_EQ(S->H.Dsp->TransportRequests, Sent);
        /* the teardown's own order: the last request is the pipeline's deletion, the one before the first instance's */
        if (Sent != 0)
        {
            CHECK_EQ((S->H.Dsp->RequestPrimary[(Sent - 1) % FAKE_DSP_REQUEST_LOG] >> SOFIPC4_TYPE_SHIFT) & SOFIPC4_TYPE_MASK, SofIpc4GlbDeletePipeline);
            CHECK_EQ(S->H.Dsp->RequestPrimary[(Sent - 1) % FAKE_DSP_REQUEST_LOG] & 0x00FF0000, 1 << 16);
        }
        /* the next allocation builds afresh, and the pipeline ids of the boot are not reused */
        S->H.LockBusyAtRequest = 0;
        CHECK_EQ(IntcDspStreamAllocate(&S->Streams, Render, IPC4_STREAM_CASE_BUFFER_BYTES, 2), STATUS_SUCCESS);
        CHECK_EQ(S->Streams.PipelineInstance[0], K == 1 ? 1 : 2);
        CHECK_EQ(IntcDspStreamAllocate(&S->Streams, Capture, IPC4_STREAM_CASE_BUFFER_BYTES, 2), STATUS_SUCCESS);
        CHECK_EQ(IntcDspStreamFree(&S->Streams, Capture), STATUS_SUCCESS);
        CHECK_EQ(IntcDspStreamFree(&S->Streams, Render), STATUS_SUCCESS);
        CHECK_EQ(IntcDspStreamClose(&S->Streams, Capture), STATUS_SUCCESS);
        CHECK_EQ(IntcDspStreamClose(&S->Streams, Render), STATUS_SUCCESS);
        StreamQuiescent(S, Pcm);
        StreamStop(S);
        free(S);
    }
}

/* (i6) A stream that opens after the PCM was built: its copier waited with the placeholder, and one large-configuration set names the stream; a stream that frees while another holds the PCM has its copier told so */
static
void
CheckStreamLate(
    void)
{
    STREAM_HARNESS* S = (STREAM_HARNESS*)calloc(1, sizeof(*S));
    HANDLE Render = NULL, Capture = NULL;
    INT32 CaptureCopier;
    UINT32 Pcm;

    CHECK(S != NULL);
    if (S == NULL)
        return;
    StreamStart(S, NULL);
    Pcm = StreamPcm(S);
    /* the render stream alone builds the PCM: the capture copier is initialized with the placeholder and names no stream */
    CHECK_EQ(IntcDspStreamOpen(&S->Streams, Pcm, FALSE, 48000, 2, 32, 32, &Render), STATUS_SUCCESS);
    CHECK_EQ(IntcDspStreamAllocate(&S->Streams, Render, IPC4_STREAM_CASE_BUFFER_BYTES, 2), STATUS_SUCCESS);
    CaptureCopier = FakeDspModuleFind(S->H.Dsp, 5, 1);
    CHECK(CaptureCopier >= 0);
    if (CaptureCopier < 0)
        goto Out;
    CHECK_EQ(S->H.Dsp->Module[CaptureCopier].Config[SOFTPLG_COPIER_NODE_ID_WORD], IPC4_STREAM_CASE_PLACEHOLDER_NODE);
    CHECK_EQ(S->H.Dsp->ModuleUpdates, 0);
    /* the capture stream opens and allocates: one large-configuration set names it, nothing else is sent */
    CHECK_EQ(IntcDspStreamOpen(&S->Streams, Pcm, TRUE, 48000, 2, 32, 32, &Capture), STATUS_SUCCESS);
    CHECK_EQ(IntcDspStreamOf(&S->Streams, Capture)->Tag, 1);
    CHECK_EQ(IntcDspStreamAllocate(&S->Streams, Capture, IPC4_STREAM_CASE_BUFFER_BYTES, 2), STATUS_SUCCESS);
    CHECK_EQ(S->H.Dsp->ModuleUpdates, 1);
    CHECK_EQ(S->H.Dsp->Module[CaptureCopier].Updates, 1);
    CHECK_EQ(S->H.Dsp->Module[CaptureCopier].Config[SOFTPLG_COPIER_NODE_ID_WORD], (1u << 8) | 0);
    CHECK_EQ(S->H.Dsp->Module[CaptureCopier].Words, 21);
    CHECK_EQ(S->H.Dsp->ModuleInits, 3);
    /* the trace: the buffer, its bound, the set (both flags, 84 bytes) */
    CHECK_EQ(S->Streams.Trace[S->Streams.TraceCount - 1].Kind, INTCDSP_TRACE_IPC);
    CHECK_EQ(S->Streams.Trace[S->Streams.TraceCount - 1].Primary, 0x44010005);
    CHECK_EQ(S->Streams.Trace[S->Streams.TraceCount - 1].Extension, 0x30000054);
    /* the render stream frees first: its copier is told its stream is going; the capture stream frees last and tears the PCM down */
    CHECK_EQ(IntcDspStreamFree(&S->Streams, Render), STATUS_SUCCESS);
    CHECK_EQ(S->H.Dsp->ModuleUpdates, 2);
    CHECK_EQ(FakeDspModuleFind(S->H.Dsp, 5, 0) >= 0, TRUE);
    CHECK_EQ(S->H.Dsp->Module[FakeDspModuleFind(S->H.Dsp, 5, 0)].Config[SOFTPLG_COPIER_NODE_ID_WORD], (0u << 8) | 0xFF);
    CHECK_EQ(S->H.Dsp->ModuleDeletes, 0);
    CHECK_EQ(IntcDspStreamFree(&S->Streams, Capture), STATUS_SUCCESS);
    CHECK_EQ(S->H.Dsp->ModuleDeletes, 3);
    CHECK_EQ(IntcDspStreamClose(&S->Streams, Render), STATUS_SUCCESS);
    CHECK_EQ(IntcDspStreamClose(&S->Streams, Capture), STATUS_SUCCESS);
    StreamQuiescent(S, Pcm);
Out:
    StreamStop(S);
    free(S);
}

/* (i7) The comparison: a capture that is the render matches at offset 0, one that is the render a number of frames behind matches at that number, a capture stream that stopped part-way into its buffer is read from there around the cyclic buffer, a differing sample is named wherever it lies, and the rms is the exact one of the sine (the mean square divided once: O1) */
static
void
CheckStreamCompare(
    void)
{
    UINT8* Render = (UINT8*)calloc(1, 4096 * 8);
    UINT8* Capture = (UINT8*)calloc(1, 4096 * 8);
    INTCDSP_COMPARE Compare;
    UINT32 I;
    static const INT32 Peaks[] = { 0, 2147483647, 0 };
    const INT32* Sine = IntcDspSelfTestSine();

    CHECK(Render != NULL && Capture != NULL);
    if (Render == NULL || Capture == NULL)
        return;
    IntcDspSelfTestFill(Render, 4096 * 8, 2, 32, 32);
    /* no latency: offset 0, the whole buffer compared */
    memcpy(Capture, Render, 4096 * 8);
    IntcDspStreamCompare(Render, Capture, 4096 * 8, 2, 32, 0, &Compare);
    CHECK(Compare.Matched);
    CHECK_EQ(Compare.Offset, 0);
    CHECK_EQ(Compare.Samples, 4096 * 2);
    CHECK_EQ(Compare.Differing, 0);
    CHECK_EQ(Compare.MaxError, 0);
    CHECK_EQ(Compare.RenderRms, 379742706482ULL);
    CHECK_EQ(Compare.CaptureRms, 379742706482ULL);
    /* the capture is the render ten frames behind, around the cyclic buffer: it matches at offset 10 */
    for (I = 0; I < 4096; I++)
        memcpy(Capture + I * 8, Render + ((I + 4096 - 10) % 4096) * 8, 8);
    IntcDspStreamCompare(Render, Capture, 4096 * 8, 2, 32, 0, &Compare);
    CHECK(Compare.Matched);
    CHECK_EQ(Compare.Offset, 10);
    CHECK_EQ(Compare.Samples, 4096 * 2);
    /* the capture stream stopped eight frames into the buffer: the compare starts there, and the render's first frame is two frames in */
    IntcDspStreamCompare(Render, Capture, 4096 * 8, 2, 32, 8 * 8, &Compare);
    CHECK(Compare.Matched);
    CHECK_EQ(Compare.Offset, 2);
    /* one differing sample: named with its frame, channel and both values, and the error is its size; an offset that would skip it does not */
    memcpy(Capture, Render, 4096 * 8);
    *(INT32*)(Capture + 100 * 8 + 4) += 5;
    IntcDspStreamCompare(Render, Capture, 4096 * 8, 2, 32, 0, &Compare);
    CHECK(!Compare.Matched);
    CHECK_EQ(Compare.Offset, 0);
    CHECK_EQ(Compare.Differing, 1);
    CHECK_EQ(Compare.DifferingFrame, 100);
    CHECK_EQ(Compare.DifferingChannel, 1);
    CHECK_EQ(Compare.RenderSample, *(INT32*)(Render + 100 * 8 + 4));
    CHECK_EQ(Compare.CaptureSample, *(INT32*)(Render + 100 * 8 + 4) + 5);
    CHECK_EQ(Compare.MaxError, 5);
    /* nothing written: no offset matches, and the report names the first sample at the first offset */
    memset(Capture, 0xCD, 4096 * 8);
    IntcDspStreamCompare(Render, Capture, 4096 * 8, 2, 32, 0, &Compare);
    CHECK(!Compare.Matched);
    CHECK_EQ(Compare.Offset, 0);
    CHECK_EQ(Compare.Differing, 4096 * 2);
    CHECK_EQ(Compare.DifferingFrame, 0);
    CHECK_EQ(Compare.RenderSample, 0);
    CHECK_EQ(Compare.CaptureSample, (INT32)0xCDCDCDCDu);
    CHECK_EQ(Compare.MaxError, 1379021362);
    /* a 16-bit container: the same, with 16-bit samples; the rms is the sine's exact one over the 4096 frames, 5794.253 in thousandths (the truncating sum printed 5793.911, 0.006 % low: O1) */
    IntcDspSelfTestFill(Render, 4096 * 4, 2, 16, 16);
    memcpy(Capture, Render, 4096 * 4);
    IntcDspStreamCompare(Render, Capture, 4096 * 4, 2, 16, 0, &Compare);
    CHECK(Compare.Matched);
    CHECK_EQ(Compare.Offset, 0);
    CHECK_EQ(Compare.RenderRms, 5794253ULL);
    CHECK_EQ(Compare.CaptureRms, 5794253ULL);
    /* a constant buffer's rms is its value exactly, where a square divided before the sum lost a thousandth (O1): 16 bits, 24 valid bits in the 32-bit word, 32 bits */
    for (I = 0; I < 4096 * 2; I++)
        ((INT16*)Render)[I] = 8191;
    memcpy(Capture, Render, 4096 * 4);
    IntcDspStreamCompare(Render, Capture, 4096 * 4, 2, 16, 0, &Compare);
    CHECK(Compare.Matched);
    CHECK_EQ(Compare.RenderRms, 8191000ULL);
    CHECK_EQ(Compare.CaptureRms, 8191000ULL);
    for (I = 0; I < 4096 * 2; I++)
        ((INT32*)Render)[I] = 536870911;
    memcpy(Capture, Render, 4096 * 8);
    IntcDspStreamCompare(Render, Capture, 4096 * 8, 2, 32, 0, &Compare);
    CHECK(Compare.Matched);
    CHECK_EQ(Compare.RenderRms, 536870911000ULL);
    CHECK_EQ(Compare.CaptureRms, 536870911000ULL);
    /* the largest sample: its square is 62 bits, eight thousand of them do not fit 64, and the mean is still exact */
    for (I = 0; I < 4096 * 2; I++)
        ((INT32*)Render)[I] = (INT32)2147483647u;
    memcpy(Capture, Render, 4096 * 8);
    IntcDspStreamCompare(Render, Capture, 4096 * 8, 2, 32, 0, &Compare);
    CHECK(Compare.Matched);
    CHECK_EQ(Compare.RenderRms, 2147483647000ULL);
    CHECK_EQ(Compare.CaptureRms, 2147483647000ULL);
    /* the sine: a period of 48 frames from zero through the positive peak and back */
    for (I = 0; I < 3; I++)
        CHECK_EQ(Sine[I * 12], Peaks[I]);
    CHECK_EQ(Sine[36], -2147483647);
    free(Render);
    free(Capture);
}

/* (i8) The loopback self-test over the streams: three depths matched with the plan's lines, a latency found at its offset, a wire that forwards nothing and periods that stall stop at the first run with the line that says why, a fault releases everything, and nothing is held on any way out */
static
void
CheckStreamSelfTest(
    void)
{
    STREAM_HARNESS* S = (STREAM_HARNESS*)calloc(1, sizeof(*S));
    NTSTATUS Status;
    UINT32 Pcm, Requests;

    CHECK(S != NULL);
    if (S == NULL)
        return;
    StreamStart(S, NULL);
    Pcm = StreamPcm(S);
    /* the wire of the model: three runs, three matches, the lines of the plan's VM-acceptance row; the driver's wait takes two periods a run, 85 ms of the model's clock for 4096 frames, and the event is registered for each and released after it */
    Status = IntcDspLoopbackSelfTest(&S->Streams, &S->SelfTestOps, STREAM_FRAMES);
    if (Status != STATUS_SUCCESS)
        fprintf(stderr, "%s", S->Log);
    CHECK_EQ(Status, STATUS_SUCCESS);
    CHECK(StreamLogCount(S, "loopback self-test: endpoint 'Loopback', 48000 Hz 2 ch 16-bit, render stream 1, capture stream 1, 4096 frames\n") == 1);
    CHECK(StreamLogCount(S, "loopback self-test: endpoint 'Loopback', 48000 Hz 2 ch 24-bit, render stream 1, capture stream 1, 4096 frames\n") == 1);
    CHECK(StreamLogCount(S, "loopback self-test: endpoint 'Loopback', 48000 Hz 2 ch 32-bit, render stream 1, capture stream 1, 4096 frames\n") == 1);
    CHECK(StreamLogCount(S, "loopback self-test: 4096 frames captured after 85 ms, offset 0 frames, max abs error 0, render rms 5794.253, capture rms 5794.253, ok\n") == 1);
    CHECK(StreamLogCount(S, "loopback self-test: 4096 frames captured after 85 ms, offset 0 frames, max abs error 0, render rms 379742706.482, capture rms 379742706.482, ok\n") == 2);
    CHECK(StreamLogCount(S, "stopped at render position 0 (LPIB 0, buffer 0), capture position 0 (LPIB 0, buffer 0), 2 capture periods, released: 1 pipelines and 3 instances deleted, 2 streams freed, ok\n") == 3);
    CHECK(StreamLogCount(S, "loopback self-test: 3 runs, 3 matched, 0 failed, 45 requests, 0 notifications, 0 timeouts, 0 DSP interrupts, ok\n") == 1);
    CHECK_EQ(S->Waits, 3);
    CHECK_EQ(S->Wakes, 6);
    CHECK_EQ(FakeBusCount(&S->Bus, FAKE_BUS_EVENT_REGISTER), 3);
    CHECK_EQ(FakeBusCount(&S->Bus, FAKE_BUS_EVENT_UNREGISTER), 3);
    CHECK_EQ(S->H.Dsp->ModuleInits, 9);
    CHECK_EQ(S->H.Dsp->ModuleDeletes, 9);
    CHECK_EQ(S->H.Dsp->ModuleUpdates, 3);
    CHECK_EQ(FakeBusCount(&S->Bus, FAKE_BUS_EVENT_FREE), 6);
    CHECK_EQ(FakeBusCount(&S->Bus, FAKE_BUS_EVENT_BUFFER_FREE), 6);
    StreamQuiescent(S, Pcm);
    /* the wire puts ten frames of latency on the capture, and the capture stream stops eight frames into the buffer: the offset is what is left */
    S->LogBytes = 0;
    S->Latency = 10;
    S->StopFrames = 8;
    Status = IntcDspLoopbackSelfTest(&S->Streams, &S->SelfTestOps, STREAM_FRAMES);
    if (Status != STATUS_SUCCESS)
        fprintf(stderr, "%s", S->Log);
    CHECK_EQ(Status, STATUS_SUCCESS);
    CHECK(StreamLogCount(S, "offset 2 frames, max abs error 0, render rms 5794.253, capture rms 5794.253, ok\n") == 1);
    /* the stop position is in bytes: eight frames of 16 bits are 32, of 24 and 32 bits 64 */
    CHECK(StreamLogCount(S, "stopped at render position 0 (LPIB 0, buffer 0), capture position 32 (LPIB 32, buffer 32), 2 capture periods, released: 1 pipelines and 3 instances deleted, 2 streams freed, ok\n") == 1);
    CHECK(StreamLogCount(S, "stopped at render position 0 (LPIB 0, buffer 0), capture position 64 (LPIB 64, buffer 64), 2 capture periods, released: 1 pipelines and 3 instances deleted, 2 streams freed, ok\n") == 2);
    CHECK_EQ(S->Wakes, 12);
    S->Latency = 0;
    S->StopFrames = 0;
    StreamQuiescent(S, Pcm);
    /* the periods stall: the driver's wait ends at its own timeout, 670 ms, with the periods that came; the wait's status is the run's, the line says how many came and when, one run is all */
    S->LogBytes = 0;
    S->PeriodsLeft = 0;
    Status = IntcDspLoopbackSelfTest(&S->Streams, &S->SelfTestOps, STREAM_FRAMES);
    CHECK_EQ(Status, STATUS_IO_TIMEOUT);
    CHECK(StreamLogCount(S, "loopback self-test: 4096 frames not captured: 0 of 2 periods after 670 ms (0xc00000b5), failed\n") == 1);
    CHECK(StreamLogCount(S, "loopback self-test: 1 runs, 0 matched, 1 failed, 15 requests, 0 notifications, 0 timeouts, 0 DSP interrupts, failed\n") == 1);
    CHECK_EQ(S->Wakes, 13);
    CHECK_EQ(FakeBusCount(&S->Bus, FAKE_BUS_EVENT_FREE), 14);
    StreamQuiescent(S, Pcm);
    /* no operations: not run */
    CHECK_EQ(IntcDspLoopbackSelfTest(&S->Streams, NULL, STREAM_FRAMES), STATUS_NOT_FOUND);
    /* the wire forwards nothing but the periods come: the capture keeps its poison, the stop rule's line names the first sample */
    S->LogBytes = 0;
    S->PeriodsLeft = MAXULONG32;
    S->Wire = FALSE;
    Status = IntcDspLoopbackSelfTest(&S->Streams, &S->SelfTestOps, STREAM_FRAMES);
    CHECK_EQ(Status, STATUS_DEVICE_PROTOCOL_ERROR);
    if (StreamLogCount(S, "offset 0 frames, max abs error 21042, render rms 5794.253, capture rms 12851.000, mismatch at frame 0 channel 0 (render frame 0): render 0, capture -12851, failed\n") == 0)
        fprintf(stderr, "%s", S->Log);
    CHECK(StreamLogCount(S, "loopback self-test: 4096 frames captured after 85 ms, offset 0 frames, max abs error 21042, render rms 5794.253, capture rms 12851.000, mismatch at frame 0 channel 0 (render frame 0): render 0, capture -12851, failed\n") == 1);
    CHECK(StreamLogCount(S, "loopback self-test: 1 runs, 0 matched, 1 failed, 15 requests, 0 notifications, 0 timeouts, 0 DSP interrupts, failed\n") == 1);
    StreamQuiescent(S, Pcm);
    /* one damaged sample in the first run: the run stops the test at that sample */
    S->LogBytes = 0;
    S->Wire = TRUE;
    S->DamageFrame = 100;
    Status = IntcDspLoopbackSelfTest(&S->Streams, &S->SelfTestOps, STREAM_FRAMES);
    CHECK_EQ(Status, STATUS_DEVICE_PROTOCOL_ERROR);
    CHECK(StreamLogCount(S, "offset 0 frames, max abs error 5, render rms 5794.253, capture rms 5794.253, mismatch at frame 100 channel 1 (render frame 100): render 4095, capture 4100, failed\n") == 1);
    CHECK_EQ(FakeBusCount(&S->Bus, FAKE_BUS_EVENT_FREE), 18);
    StreamQuiescent(S, Pcm);
    /* a fault during the self-test: the fault's release, the fault's status, one run */
    S->LogBytes = 0;
    S->DamageFrame = 0;
    Requests = S->H.Dsp->TransportRequests;
    S->H.Dsp->Faults.NeverReplyAtRequest = Requests + 3;
    Status = IntcDspLoopbackSelfTest(&S->Streams, &S->SelfTestOps, STREAM_FRAMES);
    CHECK_EQ(Status, STATUS_IO_TIMEOUT);
    CHECK_EQ(S->Faults, 1);
    CHECK_EQ(S->H.Dsp->TransportRequests, Requests + 3);
    CHECK(StreamLogCount(S, "loopback self-test: 1 runs, 0 matched, 1 failed, 3 requests, 0 notifications, 1 timeouts, 0 DSP interrupts, failed\n") == 1);
    CHECK_EQ(FakeBusCount(&S->Bus, FAKE_BUS_EVENT_FREE), 20);
    /* after a fault nothing runs until the next boot */
    CHECK_EQ(IntcDspLoopbackSelfTest(&S->Streams, &S->SelfTestOps, STREAM_FRAMES), STATUS_DEVICE_NOT_READY);
    StreamStop(S);
    free(S);
}

/* (i9) The driver's wait for the capture's periods, as the kernel calls it: the entry advances by a period at every wake, and two periods end the wait with Periods 2, the clock two periods on and nothing further; an entry that wraps on its first step counts the same; an entry that moves by half a period needs four wakes, and two of four asked periods at every wake need two; an entry that stalls ends the wait at its timeout with the periods that came before, and a period due after the timeout does not count; nothing is waited for on a count of 0 or a handle that is not a stream's; the event is registered for each wait and released after it; at 16 and 32 bits, the self-test's two buffer sizes */
static
void
CheckStreamWait(
    void)
{
    STREAM_HARNESS* S = (STREAM_HARNESS*)calloc(1, sizeof(*S));
    static const ULONG Depths[] = { 16, 32 };
    HANDLE Capture = NULL;
    PINTCDSP_STREAM Managed;
    PFAKE_BUS_STREAM Stream;
    NTSTATUS Status;
    ULONG Periods;
    UINT32 D, Pcm, Bytes, Half, FrameBytes, Wake0, PeriodUs;
    UINT64 Clock;

    CHECK(S != NULL);
    if (S == NULL)
        return;
    StreamStart(S, NULL);
    Pcm = StreamPcm(S);
    for (D = 0; D < 2; D++)
    {
        FrameBytes = Depths[D] / 4;
        CHECK_EQ(IntcDspStreamOpen(&S->Streams, Pcm, TRUE, 48000, 2, Depths[D], Depths[D], &Capture), STATUS_SUCCESS);
        CHECK_EQ(IntcDspStreamAllocate(&S->Streams, Capture, (SIZE_T)STREAM_FRAMES * FrameBytes, 2), STATUS_SUCCESS);
        CHECK_EQ(IntcDspStreamRun(&S->Streams, Capture), STATUS_SUCCESS);
        Managed = IntcDspStreamOf(&S->Streams, Capture);
        Stream = FakeBusOwned(&S->Bus, Managed->Handle);
        /* the self-test's sizes: 16384 bytes at 16 bits, 32768 at 32, two periods each */
        Bytes = (UINT32)Managed->Bytes;
        Half = Bytes / 2;
        PeriodUs = (UINT32)((UINT64)Half * 1000000 / (48000 * FrameBytes));

        /* two periods at two wakes: success, Periods 2, the clock two periods on, the entry back where it began */
        Wake0 = S->Wakes;
        Clock = FakeDspOps()->NowUs(S->H.Dsp);
        Periods = 99;
        Status = IntcDspStreamWaitPeriods(&S->Streams, Capture, 2, 670, &S->Event, &Periods);
        CHECK_EQ(Status, STATUS_SUCCESS);
        CHECK_EQ(Periods, 2);
        CHECK_EQ(S->Wakes - Wake0, 2);
        CHECK_EQ(FakeDspOps()->NowUs(S->H.Dsp) - Clock, 2 * (UINT64)PeriodUs);
        CHECK_EQ(Stream->Position, 0);
        CHECK_EQ(Stream->DmaPosition, 0);

        /* the entry wraps on its first step: the same two periods, at the same two wakes */
        Stream->Position = Stream->DmaPosition = Bytes - Half / 2;
        Wake0 = S->Wakes;
        Clock = FakeDspOps()->NowUs(S->H.Dsp);
        Periods = 99;
        Status = IntcDspStreamWaitPeriods(&S->Streams, Capture, 2, 670, &S->Event, &Periods);
        CHECK_EQ(Status, STATUS_SUCCESS);
        CHECK_EQ(Periods, 2);
        CHECK_EQ(S->Wakes - Wake0, 2);
        CHECK_EQ(FakeDspOps()->NowUs(S->H.Dsp) - Clock, 2 * (UINT64)PeriodUs);
        CHECK_EQ(Stream->Position, Bytes - Half / 2);
        Stream->Position = Stream->DmaPosition = 0;

        /* the entry moves by half a period at every wake: four wakes, both periods counted */
        S->PeriodBytes = Half / 2;
        Wake0 = S->Wakes;
        Clock = FakeDspOps()->NowUs(S->H.Dsp);
        Periods = 99;
        Status = IntcDspStreamWaitPeriods(&S->Streams, Capture, 2, 670, &S->Event, &Periods);
        CHECK_EQ(Status, STATUS_SUCCESS);
        CHECK_EQ(Periods, 2);
        CHECK_EQ(S->Wakes - Wake0, 4);
        CHECK_EQ(FakeDspOps()->NowUs(S->H.Dsp) - Clock, 2 * (UINT64)PeriodUs);
        S->PeriodBytes = 0;
        Stream->Position = Stream->DmaPosition = 0;

        /* four periods asked, two of them at every wake: two wakes */
        Wake0 = S->Wakes;
        Periods = 99;
        Status = IntcDspStreamWaitPeriods(&S->Streams, Capture, 4, 670, &S->Event, &Periods);
        CHECK_EQ(Status, STATUS_SUCCESS);
        CHECK_EQ(Periods, 4);
        CHECK_EQ(S->Wakes - Wake0, 2);

        /* one period, then a stall: the wait ends at its timeout with the one period; the driver's remaining time is in whole milliseconds, so the wait runs the fraction of the period's millisecond past the timeout */
        S->PeriodsLeft = 1;
        Wake0 = S->Wakes;
        Clock = FakeDspOps()->NowUs(S->H.Dsp);
        Periods = 99;
        Status = IntcDspStreamWaitPeriods(&S->Streams, Capture, 2, 670, &S->Event, &Periods);
        CHECK_EQ(Status, STATUS_IO_TIMEOUT);
        CHECK_EQ(Periods, 1);
        CHECK_EQ(S->Wakes - Wake0, 2);
        CHECK_EQ(FakeDspOps()->NowUs(S->H.Dsp) - Clock, PeriodUs + (670 - PeriodUs / 1000) * 1000);

        /* a stall from the first wake: no period, the whole timeout */
        S->PeriodsLeft = 0;
        Wake0 = S->Wakes;
        Clock = FakeDspOps()->NowUs(S->H.Dsp);
        Periods = 99;
        Status = IntcDspStreamWaitPeriods(&S->Streams, Capture, 2, 670, &S->Event, &Periods);
        CHECK_EQ(Status, STATUS_IO_TIMEOUT);
        CHECK_EQ(Periods, 0);
        CHECK_EQ(S->Wakes - Wake0, 1);
        CHECK_EQ(FakeDspOps()->NowUs(S->H.Dsp) - Clock, 670000);
        S->PeriodsLeft = MAXULONG32;

        /* a period due after the wait's end: the wait ends at its end, nothing counted */
        Wake0 = S->Wakes;
        Clock = FakeDspOps()->NowUs(S->H.Dsp);
        Periods = 99;
        Status = IntcDspStreamWaitPeriods(&S->Streams, Capture, 2, 1, &S->Event, &Periods);
        CHECK_EQ(Status, STATUS_IO_TIMEOUT);
        CHECK_EQ(Periods, 0);
        CHECK_EQ(S->Wakes - Wake0, 1);
        CHECK_EQ(FakeDspOps()->NowUs(S->H.Dsp) - Clock, 1000);

        /* one period asked: the driver's period is the whole buffer then, two of the model's halves at two wakes */
        Wake0 = S->Wakes;
        Periods = 99;
        Status = IntcDspStreamWaitPeriods(&S->Streams, Capture, 1, 670, &S->Event, &Periods);
        CHECK_EQ(Status, STATUS_SUCCESS);
        CHECK_EQ(Periods, 1);
        CHECK_EQ(S->Wakes - Wake0, 2);

        /* nothing to wait for: the entry is left alone and no wake comes */
        Wake0 = S->Wakes;
        Periods = 99;
        CHECK_EQ(IntcDspStreamWaitPeriods(&S->Streams, Capture, 0, 670, &S->Event, &Periods), STATUS_INVALID_PARAMETER);
        CHECK_EQ(Periods, 0);
        Periods = 99;
        CHECK_EQ(IntcDspStreamWaitPeriods(&S->Streams, (HANDLE)&S->Streams, 2, 670, &S->Event, &Periods), STATUS_INVALID_PARAMETER);
        CHECK_EQ(Periods, 0);
        CHECK_EQ(S->Wakes, Wake0);
        /* the event was registered for each of the eight waits and released after it */
        CHECK_EQ(Managed->Events, 0);
        CHECK_EQ(FakeBusCount(&S->Bus, FAKE_BUS_EVENT_REGISTER), (D + 1) * 8);
        CHECK_EQ(FakeBusCount(&S->Bus, FAKE_BUS_EVENT_UNREGISTER), (D + 1) * 8);

        CHECK_EQ(IntcDspStreamPause(&S->Streams, Capture), STATUS_SUCCESS);
        CHECK_EQ(IntcDspStreamFree(&S->Streams, Capture), STATUS_SUCCESS);
        CHECK_EQ(IntcDspStreamClose(&S->Streams, Capture), STATUS_SUCCESS);
        StreamQuiescent(S, Pcm);
    }
    StreamStop(S);
    free(S);
}

int
main(
    void)
{
    HARNESS* H;
    UINT8* Image;
    SOF_RI_MODULE Modules[4];
    SOF_RI_IMAGE Parsed;

    H = (HARNESS*)calloc(1, sizeof(*H));
    Image = (UINT8*)calloc(1, FWLOAD_CASE_IMAGE_BYTES);
    if (H == NULL || Image == NULL)
        return 2;
    HarnessInit(H);
    FwloadCaseBuildImage(Image);
    CHECK_EQ(SofRiParse(Image, FWLOAD_CASE_IMAGE_BYTES, Modules, 4, &Parsed), FwloadOk);

    CheckRegisterOrderPin(H, &Parsed);
    CheckTimeouts(H, &Parsed);
    CheckStatus(H, &Parsed);
    CheckCrash(H, &Parsed);
    CheckNotifications(H, &Parsed);
    CheckInterruptMode(H, &Parsed);
    CheckTwoCallers(H, &Parsed);
    CheckLargeConfig(H, &Parsed);
    CheckPollPolicy(H, &Parsed);
    CheckLink(H, &Parsed);

    CheckStreamGolden();
    CheckStreamFormats();
    CheckStreamPool();
    CheckStreamTimeouts();
    CheckStreamBusy();
    CheckStreamLate();
    CheckStreamCompare();
    CheckStreamSelfTest();
    CheckStreamWait();

    printf("sofipc: %d checks, %d failures\n", Checks, Failures);
    free(H->Dsp);
    free(H->Ctx);
    free(H->Virtual);
    free(H);
    free(Image);
    return Failures != 0;
}
