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

/*
 * The transport's request, wrapped (N7-02): the transport faults the link on
 * every failed exchange, a firmware status included, so no fake DSP makes a
 * deletion fail with the link ready, the case R8-12 asks for. With
 * FailDeleteRequest set, a delete-pipeline request is answered as a timeout
 * that left the link ready, and sent nowhere; sofipc.c's own entry is
 * compiled under the name SofIpcRequestReal beside it.
 */
SOFIPC_STATUS SofIpcRequestReal(SOFIPC_CONTEXT* Ipc, SOFIPC_REQUEST* Request);
static BOOLEAN FailDeleteRequest;

SOFIPC_STATUS SofIpcRequest(SOFIPC_CONTEXT* Ipc, SOFIPC_REQUEST* Request)
{
    if (FailDeleteRequest && Request != NULL && SofIpc4MsgType(&Request->Msg) == SofIpc4GlbDeletePipeline)
    {
        Ipc->Failure = SofIpcTimeout;
        Ipc->Faults++;
        Ipc->Timeouts++;
        return SofIpcTimeout;
    }
    return SofIpcRequestReal(Ipc, Request);
}

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
    /* a second attempt that answers busy as well (R1-04's case: the build's and its undo's) */
    UINT32 LockBusyAlsoAtRequest;
    /* from this lock attempt on every attempt answers busy (FA-03's case: busy after all the tries): 0 never */
    UINT32 LockBusyFromRequest;
    /* the stream manager's lock depth (fake_bus.h FakeBusLockDepth) at each request lock: the least and the most seen, reset by the stream cases */
    UINT32 LockDepthMin;
    UINT32 LockDepthMax;
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
    if ((UINT32)FakeBusLockDepth < H->LockDepthMin)
        H->LockDepthMin = (UINT32)FakeBusLockDepth;
    if ((UINT32)FakeBusLockDepth > H->LockDepthMax)
        H->LockDepthMax = (UINT32)FakeBusLockDepth;
    if ((H->LockBusyAtRequest != 0 && H->LockAttempts == H->LockBusyAtRequest) ||
        (H->LockBusyAlsoAtRequest != 0 && H->LockAttempts == H->LockBusyAlsoAtRequest) ||
        (H->LockBusyFromRequest != 0 && H->LockAttempts >= H->LockBusyFromRequest))
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

    FakeDspInit(H->Dsp, FwloadPlatformForDevice(FWLOAD_DEV_ARL_S), Faults);
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
    FwloadInitContext(H->Ctx, &H->Ops, H, FwloadPlatformForDevice(FWLOAD_DEV_ARL_S), Image, &Dma, NULL);
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
    H->LockBusyAlsoAtRequest = 0;
    H->LockBusyFromRequest = 0;
    H->LockDepthMin = MAXULONG32;
    H->LockDepthMax = 0;
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
        (A->Offset == FwloadPlatformForDevice(FWLOAD_DEV_ARL_S)->IpcReply || A->Offset == FwloadPlatformForDevice(FWLOAD_DEV_ARL_S)->IpcTarget))
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
    CHECK_EQ(Transport[0].Offset, FwloadPlatformForDevice(FWLOAD_DEV_ARL_S)->IpcRequestData);
    CHECK_EQ(Transport[0].Value, FWLOAD_PARAM_ID_FW_CONFIG << FWLOAD_PARAM_ID_SHIFT);
    CHECK_EQ(Transport[1].Op, FAKE_DSP_ACCESS_WRITE32);
    CHECK_EQ(Transport[1].Offset, FwloadPlatformForDevice(FWLOAD_DEV_ARL_S)->IpcRequest);
    CHECK_EQ(Transport[1].Value, 0xC1000000);
    CHECK_EQ(Transport[2].Op, FAKE_DSP_ACCESS_READ32);
    CHECK_EQ(Transport[2].Offset, FwloadPlatformForDevice(FWLOAD_DEV_ARL_S)->IpcReply);
    CHECK_EQ(Transport[2].Value & FWLOAD_IPC_DONE, 0);
    CHECK_EQ(Transport[3].Offset, FwloadPlatformForDevice(FWLOAD_DEV_ARL_S)->IpcTarget);
    CHECK_EQ(Transport[4].Value, 0xE1000000);
    CHECK_EQ(Transport[5].Offset, FwloadPlatformForDevice(FWLOAD_DEV_ARL_S)->IpcTargetData);
    CHECK_EQ(Transport[6].Op, FAKE_DSP_ACCESS_READMEMORY);
    CHECK_EQ(Transport[6].Offset, FwloadPlatformForDevice(FWLOAD_DEV_ARL_S)->UplinkOffset);
    CHECK_EQ(Transport[6].Value, FWLOAD_MAILBOX_BYTES);
    CHECK_EQ(Transport[7].Op, FAKE_DSP_ACCESS_WRITE32);
    CHECK_EQ(Transport[7].Offset, FwloadPlatformForDevice(FWLOAD_DEV_ARL_S)->IpcReply);
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
    /* the conversion is made in 64 bits: the largest budget the table holds is 4,294,967 ms, one millisecond more does not wrap into a short budget but is refused, and the table is left as it was (R2-09) */
    Timeouts[0] = 500000;
    Timeouts[1] = 500001;
    Timeouts[2] = 500002;
    CHECK_EQ(SofIpcDefaultTimeouts(Timeouts, 4294968), SofIpcBadLength);
    CHECK_EQ(Timeouts[0], 500000);
    CHECK_EQ(Timeouts[1], 500001);
    CHECK_EQ(Timeouts[2], 500002);
    CHECK_EQ(SofIpcDefaultTimeouts(Timeouts, 0xFFFFFFFF), SofIpcBadLength);
    CHECK_EQ(Timeouts[0], 500000);
    CHECK_EQ(Timeouts[1], 500001);
    CHECK_EQ(Timeouts[2], 500002);
    CHECK_EQ(SofIpcDefaultTimeouts(Timeouts, 4294967), SofIpcOk);
    for (Class = 0; Class < SofIpcClassCount; Class++)
        CHECK_EQ(Timeouts[Class], 4294967000);
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

/* (e2) The poll takes the request lock when there is one, and does not wait for it: a request that holds the lock, before it has made the link busy, leaves the registers untouched (R2-08) */
static
void
CheckPollTakesLock(
    HARNESS* H,
    const SOF_RI_IMAGE* Image)
{
    SOFIPC_NOTIFICATION Notification;
    SOFIPC_REQUEST Request;

    /* the lock free: taken, the notification kept and acknowledged, released */
    HarnessStart(H, Image, NULL);
    HarnessAttach(H, NULL, FALSE);
    H->Dsp->IpcTarget = FWLOAD_IPC_BUSY | (SofIpc4GlbNotification << SOFIPC4_TYPE_SHIFT) |
        (SOFIPC4_NOTIFY_LOG_BUFFER_STATUS << SOFIPC4_NOTIFICATION_TYPE_SHIFT);
    H->Dsp->IpcTargetAck |= FWLOAD_IPC_BUSY;
    H->Dsp->NotificationPending = TRUE;
    CHECK(SofIpcPollNotifications(&H->Ipc));
    CHECK_EQ(H->LockAttempts, 1);
    CHECK_EQ(H->LockAcquires, 1);
    CHECK_EQ(H->LockUnlocks, 1);
    CHECK(!H->LockHeld);
    CHECK_EQ(H->Ipc.Notifications, 1);
    CHECK_EQ(H->Ipc.RingCount, 1);
    CHECK_EQ(FakeDspIpcErrors(H->Dsp), 0);

    /* the lock held by a request in progress, the link still ready: the poll tries once, does not wait, and touches nothing */
    H->Dsp->IpcTarget = FWLOAD_IPC_BUSY | (SofIpc4GlbNotification << SOFIPC4_TYPE_SHIFT) |
        (SOFIPC4_NOTIFY_LOG_BUFFER_STATUS << SOFIPC4_NOTIFICATION_TYPE_SHIFT);
    H->Dsp->IpcTargetAck |= FWLOAD_IPC_BUSY;
    H->Dsp->NotificationPending = TRUE;
    H->LockHeld = TRUE;
    CHECK(!SofIpcPollNotifications(&H->Ipc));
    CHECK_EQ(H->LockAttempts, 2);
    CHECK_EQ(H->LockAcquires, 1);
    CHECK_EQ(H->LockUnlocks, 1);
    CHECK_EQ(H->LastLockTimeout, 0);
    CHECK_EQ(H->Ipc.Notifications, 1);
    CHECK_EQ(H->Ipc.RingCount, 1);
    CHECK_EQ(H->Ipc.Link, SofIpcLinkReady);
    CHECK_EQ(H->Ipc.Faults, 0);
    CHECK(H->Dsp->NotificationPending);
    CHECK(H->Dsp->IpcTarget & FWLOAD_IPC_BUSY);
    CHECK(H->Dsp->IpcTargetAck & FWLOAD_IPC_BUSY);
    /* the model's own verdict: the notification still waits for its acknowledgement */
    CHECK_EQ(FakeDspIpcErrors(H->Dsp), FAKE_DSP_IPC_NOTIFY_LEFT_BUSY);
    /* the request ends: the next poll takes the lock and the notification */
    H->LockHeld = FALSE;
    CHECK(SofIpcPollNotifications(&H->Ipc));
    CHECK_EQ(H->LockAttempts, 3);
    CHECK_EQ(H->LockAcquires, 2);
    CHECK_EQ(H->LockUnlocks, 2);
    CHECK_EQ(H->Ipc.Notifications, 2);
    CHECK(SofIpcTakeNotification(&H->Ipc, &Notification));
    CHECK(SofIpcTakeNotification(&H->Ipc, &Notification));
    CHECK_EQ(FakeDspIpcErrors(H->Dsp), 0);

    /* a fault found by the poll is recorded with the lock released */
    H->Dsp->IpcTarget = FWLOAD_IPC_BUSY | (SofIpc4GlbNotification << SOFIPC4_TYPE_SHIFT) |
        (SOFIPC4_NOTIFY_EXCEPTION_CAUGHT << SOFIPC4_NOTIFICATION_TYPE_SHIFT);
    H->Dsp->IpcTargetAck |= FWLOAD_IPC_BUSY;
    H->Dsp->NotificationPending = TRUE;
    CHECK(!SofIpcPollNotifications(&H->Ipc));
    CHECK_EQ(H->LockAcquires, 3);
    CHECK_EQ(H->LockUnlocks, 3);
    CHECK(!H->LockHeld);
    CHECK_EQ(H->Ipc.Link, SofIpcLinkFaulted);
    CHECK_EQ(H->Ipc.Failure, SofIpcCrash);
    CHECK_EQ(FakeDspIpcErrors(H->Dsp), 0);

    /* without a request lock the poll is what it was: the caller serializes */
    HarnessStart(H, Image, NULL);
    CHECK_EQ(SofIpcInit(&H->Ipc, &HarnessIpcOpsUnlocked, H, H->Ctx, NULL, FALSE), SofIpcOk);
    H->Dsp->IpcTarget = FWLOAD_IPC_BUSY | (SofIpc4GlbNotification << SOFIPC4_TYPE_SHIFT) |
        (SOFIPC4_NOTIFY_LOG_BUFFER_STATUS << SOFIPC4_NOTIFICATION_TYPE_SHIFT);
    H->Dsp->IpcTargetAck |= FWLOAD_IPC_BUSY;
    H->Dsp->NotificationPending = TRUE;
    CHECK(SofIpcPollNotifications(&H->Ipc));
    CHECK_EQ(H->LockAttempts, 0);
    CHECK_EQ(H->Ipc.Notifications, 1);
    CHECK_EQ(H->Ipc.RingCount, 1);
    CHECK_EQ(FakeDspIpcErrors(H->Dsp), 0);

    /* the requests themselves are as before: one lock taken and released for each */
    HarnessStart(H, Image, NULL);
    HarnessAttach(H, NULL, FALSE);
    HarnessRequest(&Request, SofIpcClassPipeline, Ipc4TransportSelfTest[0].Primary, 0);
    CHECK_EQ(SofIpcRequest(&H->Ipc, &Request), SofIpcOk);
    CHECK_EQ(H->LockAttempts, 1);
    CHECK_EQ(H->LockAcquires, 1);
    CHECK_EQ(H->LockUnlocks, 1);
    CHECK_EQ(H->LockWaits, 0);
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

/* A wave pin holding the MDL of its stream's buffer, as PortCls keeps one across the manager's transitions (R5-01); room for the pins of five boots, the manager's table of held buffers being four boots of sixteen (INTCDSP_HELD_MAX) */
typedef struct _STREAM_PIN
{
    HANDLE Handle;
    PMDL Mdl;
    BOOLEAN Open;
} STREAM_PIN;

#define STREAM_PINS    (INTCDSP_HELD_MAX + 8)

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
    /* the manager's lock depth (fake_bus.h) when the fault callback came, and how often a buffer went back through the unmap operation */
    UINT32 FaultDepth;
    UINT32 Unmaps;
    /* the boot at an open (E1): how often the Boot operation came and at what depth of the manager's lock; whether a boot stops before the manager is started, as a DSP that does not come up */
    UINT32 Boots;
    UINT32 BootDepth;
    BOOLEAN BootStops;
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
    /* the pins that hold their stream's buffer (R5-01): each a wave pin with the MDL its allocation gave, until its close */
    STREAM_PIN Pins[STREAM_PINS];
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
    STREAM_HARNESS* S = (STREAM_HARNESS*)Context;

    S->Faults++;
    S->FaultDepth = (UINT32)FakeBusLockDepth;
}

/* A buffer goes back to the bus: the driver's mappings of it go first; the harness counts them */
static
void
StreamUnmap(
    PVOID Context,
    PMDL Mdl)
{
    STREAM_HARNESS* S = (STREAM_HARNESS*)Context;

    (VOID)Mdl;
    S->Unmaps++;
}

static const INTCDSP_STREAMS_OPS StreamOpsTemplate =
{
    NULL,
    StreamLog,
    StreamFault,
    NULL,
    StreamUnmap
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

/* The log just past a line that begins at At and is Text's: NULL where the line at At is another's */
static
const char*
StreamLineIs(
    const char* At,
    const char* Text)
{
    SIZE_T Bytes = strlen(Text);

    if (strncmp(At, Text, Bytes) != 0)
    {
        fprintf(stderr, "the line is not \"%s\": \"%.100s\"\n", Text, At);
        ++Failures;
        return NULL;
    }
    return At + Bytes;
}

/*
 * The lines of a release as the design orders them (E1, §4), read from At
 * on, each right after the one before: for every stream let go, in the
 * table's order, the fault's line, its kind and its time in ms (a number
 * the case does not guess: the digits are skipped), then its pin's; the
 * release's count ends them. The log just past the count is returned.
 */
static
const char*
StreamReleaseLines(
    const char* At,
    UINT32 Count,
    const UINT32* Tags,
    const char* Kind,
    UINT32 Held,
    UINT32 TagsBack)
{
    char Line[160];
    const char* P = At;
    UINT32 I;

    for (I = 0; I < Count && P != NULL; I++)
    {
        snprintf(Line, sizeof(Line), "fault during stream %u: %s after ", Tags[I], Kind);
        if ((P = StreamLineIs(P, Line)) != NULL)
        {
            while (*P >= '0' && *P <= '9')
                P++;
            P = StreamLineIs(P, " ms\n");
        }
        if (P != NULL)
        {
            snprintf(Line, sizeof(Line), "stream %u: the pin is told: device removed\n", Tags[I]);
            P = StreamLineIs(P, Line);
        }
    }
    if (P != NULL)
    {
        snprintf(Line, sizeof(Line), "release: %u streams let go, %u buffers held for their pins, %u tags back\n", Count, Held, TagsBack);
        P = StreamLineIs(P, Line);
    }
    return P;
}

/* The index of the Skip-th event of a kind in the fake bus's log, FAKE_BUS_EVENTS_LOG when there is none: the order of the bus calls is read from it */
static
UINT32
StreamBusEventIndex(
    const STREAM_HARNESS* S,
    UINT32 Kind,
    UINT32 Skip)
{
    UINT32 I;

    for (I = 0; I < S->Bus.LogCount; I++)
        if (S->Bus.Log[I].Kind == Kind && Skip-- == 0)
            return I;
    return FAKE_BUS_EVENTS_LOG;
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
    /* T1: the file's size and the model's are two numbers (the topology line prints the file's, 4286, beside the model's: 2288 before the graph's adjacency and the walk's queue joined the model (R2-04), 2384 since) */
    CHECK_EQ(Bytes, 2384);
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

/* The pin of a handle, NULL for none */
static
STREAM_PIN*
StreamPinOf(
    STREAM_HARNESS* S,
    HANDLE Handle)
{
    UINT32 I;

    for (I = 0; I < STREAM_PINS; I++)
        if (S->Pins[I].Open && S->Pins[I].Handle == Handle)
            return &S->Pins[I];
    return NULL;
}

/*
 * A pin takes the buffer its stream's allocation gave, after the allocate
 * succeeded (R5-01): the fake counts the owner, so a free of the buffer while
 * the pin holds it is refused and logged where the bus would free the pages
 * under the pin's mapping.
 */
static
void
StreamPinOpen(
    STREAM_HARNESS* S,
    HANDLE Handle)
{
    PINTCDSP_STREAM Stream = IntcDspStreamOf(&S->Streams, Handle);
    STREAM_PIN* Pin = NULL;
    UINT32 I;

    for (I = 0; I < STREAM_PINS; I++)
        if (S->Pins[I].Mdl == NULL)
        {
            Pin = &S->Pins[I];
            break;
        }
    CHECK(Pin != NULL && Stream != NULL && Stream->Mdl != NULL);
    if (Pin == NULL || Stream == NULL || Stream->Mdl == NULL)
        return;
    Pin->Handle = Handle;
    Pin->Mdl = Stream->Mdl;
    Pin->Open = TRUE;
    FakeBusPinOpen(&S->Bus, Stream->Mdl);
}

/*
 * The pin's close: a probe through the MDL, whose pages must be the pin's
 * while it holds it, then its free of the buffer, which must be answered
 * STATUS_SUCCESS and return the pages once. The pin is gone afterwards; a
 * second close of its handle finds nothing (StreamPinCloseAgain).
 */
static
void
StreamPinClose(
    STREAM_HARNESS* S,
    HANDLE Handle)
{
    STREAM_PIN* Pin = StreamPinOf(S, Handle);
    PVOID Memory;
    NTSTATUS Status;

    CHECK(Pin != NULL);
    if (Pin == NULL)
        return;
    Memory = FakeBusMdlMemory(&S->Bus, Pin->Mdl);
    CHECK(Memory != NULL);
    if (Memory != NULL)
        ((UINT8*)Memory)[0] ^= 0x5A;
    /* the wave pin unmaps before its free (pin_wavert.cpp): the pin lets the pages go, and the fake will not refuse the owner's own free of a live stream */
    FakeBusPinClose(&S->Bus, Pin->Mdl);
    Status = IntcDspStreamFree(&S->Streams, Handle);
    CHECK_EQ(Status, STATUS_SUCCESS);
    Pin->Open = FALSE;
    /* the pages are gone: the fake saw the held buffer's return, and the MDL names nothing */
    CHECK(FakeBusMdlMemory(&S->Bus, Pin->Mdl) == NULL);
    Pin->Mdl = NULL;
}

/* A second close of a pin's handle whose stream its own free released (the drain of its pending teardown): nothing is held under the word, so nothing is freed and the free is refused */
static
void
StreamPinCloseAgain(
    STREAM_HARNESS* S,
    HANDLE Handle)
{
    CHECK(StreamPinOf(S, Handle) == NULL);
    CHECK_EQ(IntcDspStreamFree(&S->Streams, Handle), STATUS_INVALID_HANDLE);
}

/* A second close of a pin's handle on a stream the device let go (E1, D4): the fault's or the power-down's release made it Lost, or a later boot made its word stale; nothing is held under the word, and the pin is told the device is gone */
static
void
StreamPinCloseAgainRemoved(
    STREAM_HARNESS* S,
    HANDLE Handle)
{
    CHECK(StreamPinOf(S, Handle) == NULL);
    CHECK_EQ(IntcDspStreamFree(&S->Streams, Handle), STATUS_DEVICE_REMOVED);
}

/* The pin forgets its MDL, as the wave pin does when its free is answered, with a failure or with the teardown pending (pin_wavert.cpp): the pin unmapped before its free, so the fake's count of the pin goes with the mapping; no probe, no free from the pin again */
static
void
StreamPinForget(
    STREAM_HARNESS* S,
    HANDLE Handle)
{
    STREAM_PIN* Pin = StreamPinOf(S, Handle);

    CHECK(Pin != NULL);
    if (Pin == NULL)
        return;
    FakeBusPinClose(&S->Bus, Pin->Mdl);
    Pin->Open = FALSE;
    Pin->Mdl = NULL;
}

/* The held-buffer contract of a release under a live pin (R5-01): while the pin lives nothing is freed, and the fake never saw a free under it */
static
void
StreamPinHeld(
    STREAM_HARNESS* S,
    UINT32 Buffers,
    UINT32 GivenBack)
{
    CHECK_EQ(FakeBusCount(&S->Bus, FAKE_BUS_EVENT_BUFFER_FREE_UNDER_PIN), 0);
    CHECK_EQ(FakeBusCount(&S->Bus, FAKE_BUS_EVENT_BUFFER_DETACH), Buffers + GivenBack);
    CHECK_EQ(FakeBusCount(&S->Bus, FAKE_BUS_EVENT_BUFFER_FREE), 0);
    CHECK_EQ(FakeBusCount(&S->Bus, FAKE_BUS_EVENT_BUFFER_HELD_FREE), GivenBack);
    CHECK_EQ(IntcDspStreamsHeld(&S->Streams), Buffers);
    CHECK_EQ(FakeBusLockDepth, 0);
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

/*
 * The boot at an open (E1), the driver's boot sequence as the manager's
 * Boot operation sees it: the model booted again on a fresh fake DSP, as
 * the D0 entry's boot leaves it, and the manager started on it, inside the
 * open's lock (the mutex is recursive, so the start's own initialization
 * runs under the open). The fake's clock is carried over, as the loader's
 * is on a machine, so the open's line says the boot's time. BootStops: the
 * boot stops before the manager is started, a DSP that does not come up,
 * and answers STATUS_UNSUCCESSFUL; the manager stays as it was.
 */
static
NTSTATUS
StreamBootAtOpen(
    PVOID Context)
{
    STREAM_HARNESS* S = (STREAM_HARNESS*)Context;
    UINT64 Clock;

    S->Boots++;
    S->BootDepth = (UINT32)FakeBusLockDepth;
    if (S->BootStops)
        return STATUS_UNSUCCESSFUL;
    Clock = S->H.Dsp->Clock;
    StreamBoot(S, NULL);
    S->H.Dsp->Clock += Clock;
    IntcDspStreamsInit(&S->Streams, S->State, IntcDspStreamsBytes(S->Model), S->Model, &S->H.Ipc, &S->Bus.Interface, &S->Ops);
    return S->Streams.Ready && !S->Streams.Faulted ? STATUS_SUCCESS : STATUS_UNSUCCESSFUL;
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
            /* the render pin holds its buffer; the capture stream's run: its paused request is the seventh, its running request the eighth */
            CHECK_EQ(Status, STATUS_SUCCESS);
            StreamPinOpen(S, Render);
            CHECK_EQ(IntcDspStreamAllocate(&S->Streams, Capture, IPC4_STREAM_CASE_BUFFER_BYTES, 2), STATUS_SUCCESS);
            StreamPinOpen(S, Capture);
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
        /* every tag went back to the bus (R5-01: no tag is kept across the release); the buffer of the allocate that failed is the manager's to return, the pins' buffers are held for them and none had run when the fault came */
        CHECK_EQ(FakeBusCount(&S->Bus, FAKE_BUS_EVENT_FREE), 2);
        StreamPinHeld(S, K >= IPC4_STREAM_TRACE_ALLOCATE_REQUESTS + 1 ? 2 : 0, K >= IPC4_STREAM_TRACE_ALLOCATE_REQUESTS + 1 ? 0 : 1);
        CHECK_EQ(FakeBusCount(&S->Bus, FAKE_BUS_EVENT_STOP), 0);
        CHECK_EQ(FakeBusCount(&S->Bus, FAKE_BUS_EVENT_SPIB_OFF), K >= IPC4_STREAM_TRACE_ALLOCATE_REQUESTS + 1 ? 2 : 1);
        for (Sx = 0; Sx < INTCDSP_STREAMS_MAX; Sx++)
            CHECK(S->Streams.Stream[Sx].State == IntcDspStreamStateFree || S->Streams.Stream[Sx].State == IntcDspStreamStateReleased);
        CHECK(S->Streams.Path[Pcm].Held == 0 && S->Streams.Path[Pcm].Built == FALSE);
        CHECK_EQ(S->Streams.PipelineInstance[0], 0);
        CHECK(StreamLogCount(S, "the link is faulted\n") == 1);
        CHECK(StreamLogCount(S, "streams: released after the fault; nothing is sent until the next stream open or D0 entry\n") == 1);
        /* nothing more is sent: the transitions refuse, the capture's run with the device removed (E1, D4: the fault let it go), and the fault is reported once */
        CHECK_EQ(IntcDspStreamOpen(&S->Streams, Pcm, FALSE, 48000, 2, 32, 32, &Fresh), STATUS_DEVICE_NOT_READY);
        CHECK(Fresh == NULL);
        CHECK_EQ(IntcDspStreamRun(&S->Streams, Capture), STATUS_DEVICE_REMOVED);
        /* the pins' frees return their held buffers after the fault, whatever the manager's state; a second free of a word is refused */
        if (K >= IPC4_STREAM_TRACE_ALLOCATE_REQUESTS + 1)
        {
            StreamPinClose(S, Render);
            StreamPinCloseAgainRemoved(S, Render);
            StreamPinClose(S, Capture);
            StreamPinCloseAgainRemoved(S, Capture);
        }
        StreamPinHeld(S, 0, K >= IPC4_STREAM_TRACE_ALLOCATE_REQUESTS + 1 ? 2 : 1);
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

/* (i10) The generation (R1-01): the manager is initialized again at every D0 entry while a pin holds its handle; a new pin takes the object the first one had, and the stale handle names nothing at any entry: it acts neither on the new stream nor on a free object */
static
void
CheckStreamGeneration(
    void)
{
    STREAM_HARNESS* S = (STREAM_HARNESS*)calloc(1, sizeof(*S));
    HANDLE Stale = NULL, Fresh = NULL;
    PINTCDSP_STREAM Stream;
    PULONG Register;
    UINT32 Pcm, Requests;

    CHECK(S != NULL);
    if (S == NULL)
        return;
    StreamStart(S, NULL);
    Pcm = StreamPcm(S);
    /* a handle is the manager's own word, never a pointer into its table */
    CHECK_EQ(IntcDspStreamOpen(&S->Streams, Pcm, FALSE, 48000, 2, 32, 32, &Stale), STATUS_SUCCESS);
    CHECK(Stale != NULL);
    CHECK((UINT_PTR)Stale != (UINT_PTR)&S->Streams.Stream[0]);
    Stream = IntcDspStreamOf(&S->Streams, Stale);
    CHECK(Stream == &S->Streams.Stream[0]);
    /* the D0 entry boots the DSP again and initializes the manager while the pin holds its handle, and a new pin opens the object the first one had */
    StreamBoot(S, NULL);
    IntcDspStreamsInit(&S->Streams, S->State, IntcDspStreamsBytes(S->Model), S->Model, &S->H.Ipc, &S->Bus.Interface, &S->Ops);
    CHECK(S->Streams.Ready && !S->Streams.Faulted);
    CHECK_EQ(IntcDspStreamOpen(&S->Streams, Pcm, FALSE, 48000, 2, 32, 32, &Fresh), STATUS_SUCCESS);
    CHECK(Fresh != NULL);
    CHECK(IntcDspStreamOf(&S->Streams, Fresh) == &S->Streams.Stream[0]);
    Requests = S->H.Dsp->TransportRequests;
    /* the stale handle names nothing: the pin that holds it is told so (E1, D4), STATUS_DEVICE_REMOVED at every entry of the design's table before it reads, its close answered, nothing is sent, and the new stream is as its open left it; the event entries refuse the word as before */
    CHECK(IntcDspStreamOf(&S->Streams, Stale) == NULL);
    CHECK_EQ(IntcDspStreamRun(&S->Streams, Stale), STATUS_DEVICE_REMOVED);
    CHECK_EQ(IntcDspStreamPause(&S->Streams, Stale), STATUS_DEVICE_REMOVED);
    CHECK_EQ(IntcDspStreamFree(&S->Streams, Stale), STATUS_DEVICE_REMOVED);
    CHECK_EQ(IntcDspStreamAllocate(&S->Streams, Stale, IPC4_STREAM_CASE_BUFFER_BYTES, 2), STATUS_DEVICE_REMOVED);
    CHECK_EQ(IntcDspStreamPositionRegister(&S->Streams, Stale, &Register), STATUS_DEVICE_REMOVED);
    CHECK(Register == NULL);
    CHECK_EQ(IntcDspStreamLinkPositionRegister(&S->Streams, Stale, &Register), STATUS_DEVICE_REMOVED);
    CHECK_EQ(IntcDspStreamPosition(&S->Streams, Stale), 0);
    CHECK_EQ(IntcDspStreamRegisterEvent(&S->Streams, Stale, &S->Event), STATUS_INVALID_HANDLE);
    CHECK_EQ(IntcDspStreamUnregisterEvent(&S->Streams, Stale, &S->Event), STATUS_INVALID_HANDLE);
    CHECK_EQ(IntcDspStreamClose(&S->Streams, Stale), STATUS_SUCCESS);
    CHECK_EQ(S->H.Dsp->TransportRequests, Requests);
    CHECK_EQ(FakeBusCount(&S->Bus, FAKE_BUS_EVENT_FREE), 0);
    Stream = IntcDspStreamOf(&S->Streams, Fresh);
    CHECK(Stream != NULL);
    if (Stream != NULL)
    {
        CHECK_EQ(Stream->State, IntcDspStreamStateOpen);
        CHECK(Stream->Handle != NULL);
        CHECK(Stream->Mdl == NULL);
    }
    /* the new pin's handle works, and the state, not the handle, refuses what the stream is not ready for */
    CHECK_EQ(IntcDspStreamRun(&S->Streams, Fresh), STATUS_INVALID_DEVICE_REQUEST);
    CHECK_EQ(IntcDspStreamAllocate(&S->Streams, Fresh, IPC4_STREAM_CASE_BUFFER_BYTES, 2), STATUS_SUCCESS);
    CHECK_EQ(IntcDspStreamRun(&S->Streams, Fresh), STATUS_SUCCESS);
    CHECK_EQ(IntcDspStreamPause(&S->Streams, Fresh), STATUS_SUCCESS);
    CHECK_EQ(IntcDspStreamFree(&S->Streams, Fresh), STATUS_SUCCESS);
    CHECK_EQ(IntcDspStreamClose(&S->Streams, Fresh), STATUS_SUCCESS);
    /* a closed handle is stale too: its object is free */
    CHECK_EQ(IntcDspStreamClose(&S->Streams, Fresh), STATUS_INVALID_HANDLE);
    /* values that are no handle at all: none, and pointers into the manager */
    CHECK(IntcDspStreamOf(&S->Streams, NULL) == NULL);
    CHECK(IntcDspStreamOf(&S->Streams, (HANDLE)&S->Streams) == NULL);
    CHECK(IntcDspStreamOf(&S->Streams, (HANDLE)S->State) == NULL);
    StreamStop(S);
    free(S);
}

/* (i11) The power-down's release (R1-01): every stream gives the bus what it gave it, as after a fault but with nothing sent and the driver not told, the paths forget what they built, the manager is not ready until its next initialization, and every handle is refused; the stale handle of a pin that held a stream across the transition names nothing once the next boot gave its object to a new pin. The handle's word is the index and one in the low bits, the generation above them */
static
void
CheckStreamPowerDown(
    void)
{
    STREAM_HARNESS* S = (STREAM_HARNESS*)calloc(1, sizeof(*S));
    HANDLE Render = NULL, Capture = NULL, Fresh = NULL;
    PINTCDSP_STREAM Stream;
    PULONG Register;
    UINT32 Pcm, Requests, D, I;

    CHECK(S != NULL);
    if (S == NULL)
        return;
    StreamStart(S, NULL);
    Pcm = StreamPcm(S);
    CHECK_EQ(S->Streams.Generation, 1);
    /* a pin of each direction with a buffer, both running: what two WaveRT pins hold at a suspend */
    CHECK_EQ(IntcDspStreamOpen(&S->Streams, Pcm, FALSE, 48000, 2, 32, 32, &Render), STATUS_SUCCESS);
    CHECK_EQ(IntcDspStreamOpen(&S->Streams, Pcm, TRUE, 48000, 2, 32, 32, &Capture), STATUS_SUCCESS);
    CHECK_EQ((UINT_PTR)Render & INTCDSP_STREAM_INDEX_MASK, 1);
    CHECK_EQ((UINT_PTR)Capture & INTCDSP_STREAM_INDEX_MASK, 2);
    CHECK_EQ((UINT_PTR)Render >> INTCDSP_STREAM_INDEX_BITS, 1);
    CHECK_EQ((UINT_PTR)Capture >> INTCDSP_STREAM_INDEX_BITS, 1);
    CHECK_EQ(IntcDspStreamAllocate(&S->Streams, Render, IPC4_STREAM_CASE_BUFFER_BYTES, 2), STATUS_SUCCESS);
    StreamPinOpen(S, Render);
    CHECK_EQ(IntcDspStreamAllocate(&S->Streams, Capture, IPC4_STREAM_CASE_BUFFER_BYTES, 2), STATUS_SUCCESS);
    StreamPinOpen(S, Capture);
    CHECK_EQ(IntcDspStreamRun(&S->Streams, Capture), STATUS_SUCCESS);
    CHECK_EQ(IntcDspStreamRun(&S->Streams, Render), STATUS_SUCCESS);
    Requests = S->H.Dsp->TransportRequests;
    /* the power-down (intcdsp.c IntcDspPowerDown, from D0 exit): everything goes back to the bus but the pins' buffers, which are held for them (R5-01), nothing is sent, the driver is not told */
    IntcDspStreamsRelease(&S->Streams);
    CHECK(!S->Streams.Ready);
    CHECK(!S->Streams.Faulted);
    CHECK_EQ(S->Faults, 0);
    CHECK_EQ(S->H.Dsp->TransportRequests, Requests);
    CHECK_EQ(FakeBusCount(&S->Bus, FAKE_BUS_EVENT_STOP), 2);
    StreamPinHeld(S, 2, 0);
    CHECK_EQ(FakeBusCount(&S->Bus, FAKE_BUS_EVENT_SPIB_OFF), 2);
    /* every tag went back at the release, the pins' buffers notwithstanding: a tag is never kept across a suspend */
    CHECK_EQ(FakeBusCount(&S->Bus, FAKE_BUS_EVENT_FREE), 2);
    CHECK_EQ(S->Unmaps, 2);
    for (D = 0; D < 2; D++)
        for (I = 0; I < FAKE_BUS_STREAMS; I++)
            CHECK(!S->Bus.Stream[D][I].Used);
    CHECK_EQ(S->Streams.Path[Pcm].Held, 0);
    CHECK_EQ(S->Streams.Path[Pcm].Running, 0);
    CHECK(S->Streams.Path[Pcm].Built == FALSE);
    CHECK_EQ(S->Streams.PipelineInstance[0], 0);
    for (I = 0; I < INTCDSP_STREAMS_MAX; I++)
        CHECK(S->Streams.Stream[I].State == IntcDspStreamStateFree || S->Streams.Stream[I].State == IntcDspStreamStateReleased);
    CHECK(StreamLogCount(S, "streams: released for the power-down; nothing is sent until the next stream open or D0 entry\n") == 1);
    CHECK_EQ(S->Streams.Generation, 2);
    /* between the power-down and the next D0 entry the handles are answered STATUS_DEVICE_REMOVED (E1, D4: the device let their streams go), their closes are answered, and an open fails fast: no request pays the budget; the capture pin's free is the exception, the held buffer returned under its stale word (R5-01) */
    CHECK_EQ(IntcDspStreamRun(&S->Streams, Render), STATUS_DEVICE_REMOVED);
    StreamPinClose(S, Capture);
    StreamPinCloseAgainRemoved(S, Capture);
    StreamPinHeld(S, 1, 1);
    CHECK_EQ(IntcDspStreamClose(&S->Streams, Render), STATUS_SUCCESS);
    CHECK_EQ(IntcDspStreamPosition(&S->Streams, Render), 0);
    CHECK_EQ(IntcDspStreamOpen(&S->Streams, Pcm, FALSE, 48000, 2, 32, 32, &Fresh), STATUS_DEVICE_NOT_READY);
    CHECK(Fresh == NULL);
    CHECK_EQ(S->H.Dsp->TransportRequests, Requests);
    /* a manager that holds nothing is released without a line, and its generation moves on */
    IntcDspStreamsRelease(&S->Streams);
    CHECK_EQ(S->Streams.Generation, 3);
    CHECK(StreamLogCount(S, "streams: released for the power-down; nothing is sent until the next stream open or D0 entry\n") == 1);
    /* the next D0 entry boots the DSP and initializes the manager: a new pin takes the object the render pin had */
    StreamBoot(S, NULL);
    IntcDspStreamsInit(&S->Streams, S->State, IntcDspStreamsBytes(S->Model), S->Model, &S->H.Ipc, &S->Bus.Interface, &S->Ops);
    CHECK(S->Streams.Ready && !S->Streams.Faulted);
    CHECK_EQ(S->Streams.Generation, 4);
    for (I = 0; I < INTCDSP_STREAMS_MAX; I++)
        CHECK_EQ(S->Streams.Stream[I].State, IntcDspStreamStateFree);
    CHECK_EQ(IntcDspStreamOpen(&S->Streams, Pcm, FALSE, 48000, 2, 32, 32, &Fresh), STATUS_SUCCESS);
    CHECK(IntcDspStreamOf(&S->Streams, Fresh) == &S->Streams.Stream[0]);
    CHECK_EQ((UINT_PTR)Fresh & INTCDSP_STREAM_INDEX_MASK, 1);
    CHECK_EQ((UINT_PTR)Fresh >> INTCDSP_STREAM_INDEX_BITS, 4);
    /* the stale render handle: STATUS_DEVICE_REMOVED at every entry but its buffer's free and its close (E1, D4), the new stream untouched and nothing sent; the render pin frees its held buffer after the D0 entry */
    CHECK(IntcDspStreamOf(&S->Streams, Render) == NULL);
    CHECK_EQ(IntcDspStreamClose(&S->Streams, Render), STATUS_SUCCESS);
    CHECK_EQ(IntcDspStreamRun(&S->Streams, Render), STATUS_DEVICE_REMOVED);
    StreamPinClose(S, Render);
    StreamPinCloseAgainRemoved(S, Render);
    StreamPinHeld(S, 0, 2);
    CHECK_EQ(IntcDspStreamAllocate(&S->Streams, Render, IPC4_STREAM_CASE_BUFFER_BYTES, 2), STATUS_DEVICE_REMOVED);
    CHECK_EQ(IntcDspStreamPositionRegister(&S->Streams, Render, &Register), STATUS_DEVICE_REMOVED);
    CHECK_EQ(IntcDspStreamRegisterEvent(&S->Streams, Render, &S->Event), STATUS_INVALID_HANDLE);
    Stream = IntcDspStreamOf(&S->Streams, Fresh);
    CHECK(Stream != NULL);
    if (Stream != NULL)
    {
        CHECK_EQ(Stream->State, IntcDspStreamStateOpen);
        CHECK(Stream->Handle != NULL);
        CHECK(Stream->Events == 0);
    }
    CHECK_EQ(S->H.Dsp->TransportRequests, 0);
    CHECK_EQ(FakeBusCount(&S->Bus, FAKE_BUS_EVENT_FREE), 2);
    /* the new pin's stream runs its course */
    CHECK_EQ(IntcDspStreamAllocate(&S->Streams, Fresh, IPC4_STREAM_CASE_BUFFER_BYTES, 2), STATUS_SUCCESS);
    CHECK_EQ(IntcDspStreamRun(&S->Streams, Fresh), STATUS_SUCCESS);
    CHECK_EQ(IntcDspStreamPause(&S->Streams, Fresh), STATUS_SUCCESS);
    CHECK_EQ(IntcDspStreamFree(&S->Streams, Fresh), STATUS_SUCCESS);
    CHECK_EQ(IntcDspStreamClose(&S->Streams, Fresh), STATUS_SUCCESS);
    CHECK(IntcDspStreamOf(&S->Streams, Fresh) == NULL);
    CHECK_EQ(FakeBusCount(&S->Bus, FAKE_BUS_EVENT_FREE), 3);
    StreamQuiescent(S, Pcm);
    StreamStop(S);
    free(S);

    /* a free answered while its teardown is pending (N7-01): the pin forgot the buffer, so the power-down release returns it with every tag, and the D0 entry finds no held record of it */
    S = (STREAM_HARNESS*)calloc(1, sizeof(*S));
    CHECK(S != NULL);
    if (S == NULL)
        return;
    StreamStart(S, NULL);
    Pcm = StreamPcm(S);
    CHECK_EQ(IntcDspStreamOpen(&S->Streams, Pcm, FALSE, 48000, 2, 32, 32, &Render), STATUS_SUCCESS);
    CHECK_EQ(IntcDspStreamOpen(&S->Streams, Pcm, TRUE, 48000, 2, 32, 32, &Capture), STATUS_SUCCESS);
    CHECK_EQ(IntcDspStreamAllocate(&S->Streams, Render, IPC4_STREAM_CASE_BUFFER_BYTES, 2), STATUS_SUCCESS);
    CHECK_EQ(IntcDspStreamAllocate(&S->Streams, Capture, IPC4_STREAM_CASE_BUFFER_BYTES, 2), STATUS_SUCCESS);
    StreamPinOpen(S, Render);
    StreamPinOpen(S, Capture);
    /* the capture's free: its copier's renaming busy to every try, answered with the teardown pending; the wave pin forgets its MDL as the portcls pin does whatever the answer */
    S->H.LockBusyFromRequest = S->H.LockAttempts + 1;
    CHECK_EQ(IntcDspStreamFree(&S->Streams, Capture), STATUS_SUCCESS);
    StreamPinForget(S, Capture);
    CHECK_EQ(IntcDspStreamsHeld(&S->Streams), 1);
    S->H.LockBusyFromRequest = 0;
    /* the power-down: every tag back, the pending buffer returned since its pin forgot it, the render pin's buffer held for it */
    IntcDspStreamsRelease(&S->Streams);
    CHECK(!S->Streams.Ready);
    CHECK_EQ(S->Faults, 0);
    CHECK_EQ(S->Streams.Freed, 2);
    CHECK_EQ(FakeBusCount(&S->Bus, FAKE_BUS_EVENT_FREE), 2);
    CHECK_EQ(IntcDspStreamsHeld(&S->Streams), 1);
    CHECK_EQ(FakeBusMdlCount(&S->Bus), 1);
    StreamPinHeld(S, 1, 1);
    CHECK(StreamLogCount(S, "streams: 1 teardowns were pending at the release; their buffers are returned\n") == 1);
    /* every bus stream is back in the pool before the entry, the pending one with the others */
    for (Requests = 0; Requests < 2; Requests++)
        for (I = 0; I < FAKE_BUS_STREAMS; I++)
            CHECK(!S->Bus.Stream[Requests][I].Used);
    /* the D0 entry: no held record of the pending buffer, its pages gone; the render pin's held buffer is the only one, returned by its free */
    StreamBoot(S, NULL);
    IntcDspStreamsInit(&S->Streams, S->State, IntcDspStreamsBytes(S->Model), S->Model, &S->H.Ipc, &S->Bus.Interface, &S->Ops);
    CHECK(S->Streams.Ready && !S->Streams.Faulted);
    CHECK_EQ(IntcDspStreamsHeld(&S->Streams), 1);
    CHECK_EQ(FakeBusMdlCount(&S->Bus), 1);
    StreamPinClose(S, Render);
    StreamPinHeld(S, 0, 2);
    CHECK_EQ(FakeBusMdlCount(&S->Bus), 0);
    StreamStop(S);
    free(S);
}

/* (i12) The lock of the transitions (R1-02): every public entry takes it at PASSIVE_LEVEL before anything of a stream is read and gives it back on every way out, a call above PASSIVE_LEVEL is refused before it takes anything, and every request the transitions send is sent under it, the transport's request lock taken inside it; the register reads take none */
static
void
CheckStreamLock(
    void)
{
    STREAM_HARNESS* S;
    FAKE_DSP_FAULTS Faults;
    HANDLE Render = NULL, Capture = NULL;
    PULONG Register;
    NTSTATUS Status;
    UINT32 Pcm, K;

    S = (STREAM_HARNESS*)calloc(1, sizeof(*S));
    CHECK(S != NULL);
    if (S == NULL)
        return;
    StreamStart(S, NULL);
    Pcm = StreamPcm(S);
    CHECK_EQ(FakeBusLockDepth, 0);
    /* above PASSIVE_LEVEL every entry refuses, and nothing is taken */
    FakeBusIrql = DISPATCH_LEVEL;
    CHECK_EQ(IntcDspStreamOpen(&S->Streams, Pcm, FALSE, 48000, 2, 32, 32, &Render), STATUS_INVALID_DEVICE_STATE);
    CHECK(Render == NULL);
    CHECK_EQ(IntcDspStreamAllocate(&S->Streams, NULL, IPC4_STREAM_CASE_BUFFER_BYTES, 2), STATUS_INVALID_DEVICE_STATE);
    CHECK_EQ(IntcDspStreamRun(&S->Streams, NULL), STATUS_INVALID_DEVICE_STATE);
    CHECK_EQ(IntcDspStreamPause(&S->Streams, NULL), STATUS_INVALID_DEVICE_STATE);
    CHECK_EQ(IntcDspStreamFree(&S->Streams, NULL), STATUS_INVALID_DEVICE_STATE);
    CHECK_EQ(IntcDspStreamClose(&S->Streams, NULL), STATUS_INVALID_DEVICE_STATE);
    CHECK_EQ(IntcDspStreamRegisterEvent(&S->Streams, NULL, &S->Event), STATUS_INVALID_DEVICE_STATE);
    CHECK_EQ(IntcDspStreamUnregisterEvent(&S->Streams, NULL, &S->Event), STATUS_INVALID_DEVICE_STATE);
    FakeBusIrql = PASSIVE_LEVEL;
    CHECK_EQ(FakeBusLockDepth, 0);
    StreamStop(S);
    free(S);

    /* the transitions: each request under the manager's lock, the lock free again after each */
    S = (STREAM_HARNESS*)calloc(1, sizeof(*S));
    CHECK(S != NULL);
    if (S == NULL)
        return;
    StreamStart(S, NULL);
    Pcm = StreamPcm(S);
    S->H.LockDepthMin = MAXULONG32;
    S->H.LockDepthMax = 0;
    CHECK_EQ(IntcDspStreamOpen(&S->Streams, Pcm, FALSE, 48000, 2, 32, 32, &Render), STATUS_SUCCESS);
    CHECK_EQ(IntcDspStreamOpen(&S->Streams, Pcm, TRUE, 48000, 2, 32, 32, &Capture), STATUS_SUCCESS);
    CHECK_EQ(IntcDspStreamAllocate(&S->Streams, Render, IPC4_STREAM_CASE_BUFFER_BYTES, 2), STATUS_SUCCESS);
    CHECK_EQ(IntcDspStreamAllocate(&S->Streams, Capture, IPC4_STREAM_CASE_BUFFER_BYTES, 2), STATUS_SUCCESS);
    CHECK_EQ(IntcDspStreamRun(&S->Streams, Capture), STATUS_SUCCESS);
    CHECK_EQ(IntcDspStreamRun(&S->Streams, Render), STATUS_SUCCESS);
    CHECK_EQ(IntcDspStreamPause(&S->Streams, Capture), STATUS_SUCCESS);
    CHECK_EQ(IntcDspStreamPause(&S->Streams, Render), STATUS_SUCCESS);
    CHECK_EQ(IntcDspStreamFree(&S->Streams, Capture), STATUS_SUCCESS);
    CHECK_EQ(IntcDspStreamFree(&S->Streams, Render), STATUS_SUCCESS);
    CHECK(S->H.Dsp->TransportRequests != 0);
    CHECK_EQ(S->H.LockDepthMin, 1);
    CHECK_EQ(S->H.LockDepthMax, 1);
    CHECK_EQ(FakeBusLockDepth, 0);
    /* the event registrations are serialized with the close: under the lock, none above PASSIVE_LEVEL */
    CHECK_EQ(IntcDspStreamRegisterEvent(&S->Streams, Render, &S->Event), STATUS_SUCCESS);
    CHECK_EQ(FakeBusCount(&S->Bus, FAKE_BUS_EVENT_REGISTER), 1);
    FakeBusIrql = DISPATCH_LEVEL;
    CHECK_EQ(IntcDspStreamUnregisterEvent(&S->Streams, Render, &S->Event), STATUS_INVALID_DEVICE_STATE);
    FakeBusIrql = PASSIVE_LEVEL;
    CHECK_EQ(FakeBusCount(&S->Bus, FAKE_BUS_EVENT_UNREGISTER), 0);
    CHECK_EQ(IntcDspStreamUnregisterEvent(&S->Streams, Render, &S->Event), STATUS_SUCCESS);
    CHECK_EQ(IntcDspStreamOf(&S->Streams, Render)->Events, 0);
    /* the register reads take no lock */
    CHECK_EQ(IntcDspStreamPositionRegister(&S->Streams, Render, &Register), STATUS_SUCCESS);
    CHECK_EQ(IntcDspStreamLinkPositionRegister(&S->Streams, Render, &Register), STATUS_SUCCESS);
    CHECK_EQ(IntcDspStreamPosition(&S->Streams, Render), 0);
    CHECK_EQ(FakeBusLockDepth, 0);
    /* the power-down's release and a re-initialization leave the lock as they found it */
    IntcDspStreamsRelease(&S->Streams);
    CHECK_EQ(FakeBusLockDepth, 0);
    IntcDspStreamsInit(&S->Streams, S->State, IntcDspStreamsBytes(S->Model), S->Model, &S->H.Ipc, &S->Bus.Interface, &S->Ops);
    CHECK_EQ(FakeBusLockDepth, 0);
    StreamStop(S);
    free(S);

    /* a fault's way out: the driver is told under the lock, and the lock is given back after it, at every request the link may drop */
    for (K = 1; K <= IPC4_STREAM_TRACE_ALLOCATE_REQUESTS; K++)
    {
        S = (STREAM_HARNESS*)calloc(1, sizeof(*S));
        CHECK(S != NULL);
        if (S == NULL)
            return;
        Faults = *FakeDspDefaultFaults();
        Faults.NeverReplyAtRequest = K;
        StreamStart(S, &Faults);
        Pcm = StreamPcm(S);
        CHECK_EQ(IntcDspStreamOpen(&S->Streams, Pcm, FALSE, 48000, 2, 32, 32, &Render), STATUS_SUCCESS);
        CHECK_EQ(IntcDspStreamOpen(&S->Streams, Pcm, TRUE, 48000, 2, 32, 32, &Capture), STATUS_SUCCESS);
        S->H.LockDepthMin = MAXULONG32;
        S->H.LockDepthMax = 0;
        Status = IntcDspStreamAllocate(&S->Streams, Render, IPC4_STREAM_CASE_BUFFER_BYTES, 2);
        CHECK_EQ(Status, STATUS_IO_TIMEOUT);
        CHECK_EQ(S->Faults, 1);
        CHECK_EQ(S->FaultDepth, 1);
        CHECK_EQ(S->H.LockDepthMin, 1);
        CHECK_EQ(S->H.LockDepthMax, 1);
        CHECK_EQ(FakeBusLockDepth, 0);
        CHECK_EQ(IntcDspStreamClose(&S->Streams, Render), STATUS_SUCCESS);
        CHECK_EQ(IntcDspStreamClose(&S->Streams, Capture), STATUS_SUCCESS);
        CHECK_EQ(FakeBusLockDepth, 0);
        StreamStop(S);
        free(S);
    }
}

/*
 * (i13a) A buffer held for its pin across each release the manager makes
 * without the pin asking (R5-01, R8-12). First the undo that fails and is not
 * busy (R1-04): the build's third request is busy and the undo's pipeline
 * deletions are answered with a firmware error, which FA-03 left without a
 * case. The run settled INFO 10: the transport faults the link on every
 * failed exchange, a firmware status included (sofipc.c SofIpcRequest), so
 * the forced release is reached through the lost link and no outcome of the
 * fake DSP leaves the link ready beside a failed undo; the case asserts the
 * release as it comes, and, with the transport's entry wrapped so that a
 * deletion fails with the link ready (N7-02), once more as R8-12 asked it.
 * The buffer the allocate did not hand to its pin is
 * the manager's to return. Then the same forced release from the pin's own
 * free, with another pin's buffer live: held across a D0 entry, returned by
 * that pin's free once. The contract in each: nothing is freed while a pin
 * lives, the tag goes back at once, the pin's free returns the pages once, a
 * second free of the word is refused, the lock is given back.
 */
static
void
CheckStreamHeldBuffer(
    void)
{
    STREAM_HARNESS* S;
    HANDLE Render = NULL, Capture = NULL, Taken;
    PINTCDSP_STREAM Stream;
    NTSTATUS Status;
    UINT32 Pcm, Requests, Sx, I, D;

    /* the build's undo, stuck: the forced release, the link still ready */
    S = (STREAM_HARNESS*)calloc(1, sizeof(*S));
    CHECK(S != NULL);
    if (S == NULL)
        return;
    StreamStart(S, NULL);
    Pcm = StreamPcm(S);
    CHECK_EQ(IntcDspStreamOpen(&S->Streams, Pcm, FALSE, 48000, 2, 32, 32, &Render), STATUS_SUCCESS);
    S->H.LockBusyAtRequest = 3;
    S->H.Dsp->Faults.IpcStatusForType = SofIpc4GlbDeletePipeline;
    S->H.Dsp->Faults.IpcStatusValue = SofIpc4InvalidResourceState;
    Requests = S->H.Dsp->TransportRequests;
    Status = IntcDspStreamAllocate(&S->Streams, Render, IPC4_STREAM_CASE_BUFFER_BYTES, 2);
    /* the undo's status, the one that lost the link (N7-03): the firmware's error on the deletion, not the build's busy status */
    CHECK_EQ(Status, STATUS_DEVICE_PROTOCOL_ERROR);
    CHECK_EQ(S->H.Ipc.Link, SofIpcLinkFaulted);
    CHECK_EQ(S->H.Ipc.Faults, 1);
    CHECK_EQ(S->Streams.Faulted, TRUE);
    CHECK_EQ(S->Streams.Faults, 1);
    CHECK_EQ(S->Faults, 1);
    CHECK_EQ(S->FaultDepth, 1);
    /* the bus has its tag back and the buffer detached; the pin never had it, so the manager returned it at once */
    CHECK_EQ(FakeBusCount(&S->Bus, FAKE_BUS_EVENT_FREE), 1);
    StreamPinHeld(S, 0, 1);
    CHECK_EQ(S->Unmaps, 1);
    CHECK(IntcDspStreamOf(&S->Streams, Render) != NULL && IntcDspStreamOf(&S->Streams, Render)->State == IntcDspStreamStateReleased);
    CHECK(StreamLogCount(S, "released after the fault; nothing is sent until the next stream open or D0 entry\n") == 1);
    CHECK(S->H.Dsp->TransportRequests > Requests);
    /* the pin's free of a word nothing is held under: refused, the device removed (E1, D4: the fault let the stream go), nothing freed */
    CHECK_EQ(IntcDspStreamFree(&S->Streams, Render), STATUS_DEVICE_REMOVED);
    CHECK_EQ(FakeBusCount(&S->Bus, FAKE_BUS_EVENT_BUFFER_HELD_FREE), 1);
    StreamStop(S);
    free(S);

    /* R8-12's case as it was asked (N7-02): the build busy at its third request, and the undo's deletion of the pipeline failing while the link stays ready, which only the wrapped transport gives. The forcing branch of IntcDspStuck releases everything as after a lost link, the driver is told, and the status is the undo's */
    S = (STREAM_HARNESS*)calloc(1, sizeof(*S));
    CHECK(S != NULL);
    if (S == NULL)
        return;
    StreamStart(S, NULL);
    Pcm = StreamPcm(S);
    CHECK_EQ(IntcDspStreamOpen(&S->Streams, Pcm, FALSE, 48000, 2, 32, 32, &Render), STATUS_SUCCESS);
    S->H.LockBusyAtRequest = 3;
    FailDeleteRequest = TRUE;
    Status = IntcDspStreamAllocate(&S->Streams, Render, IPC4_STREAM_CASE_BUFFER_BYTES, 2);
    FailDeleteRequest = FALSE;
    /* the undo's status, and the link never faulted by the transport: the manager faulted itself */
    CHECK_EQ(Status, STATUS_IO_TIMEOUT);
    CHECK_EQ(S->H.Ipc.Link, SofIpcLinkReady);
    CHECK_EQ(S->H.Ipc.Faults, 1);
    CHECK_EQ(S->H.Ipc.Timeouts, 1);
    CHECK_EQ(S->Streams.Faulted, TRUE);
    CHECK_EQ(S->Streams.Faults, 1);
    CHECK_EQ(S->Faults, 1);
    CHECK_EQ(S->FaultDepth, 1);
    /* the release: the bus has its tag back and the buffer detached, the pin never had it, so the manager returned it at once */
    CHECK_EQ(FakeBusCount(&S->Bus, FAKE_BUS_EVENT_FREE), 1);
    StreamPinHeld(S, 0, 1);
    CHECK_EQ(S->Unmaps, 1);
    CHECK(IntcDspStreamOf(&S->Streams, Render) != NULL && IntcDspStreamOf(&S->Streams, Render)->State == IntcDspStreamStateReleased);
    CHECK(StreamLogCount(S, "could not be deleted again (0xc00000b5); released as after a fault\n") == 1);
    CHECK(StreamLogCount(S, "released after the fault; nothing is sent until the next stream open or D0 entry\n") == 1);
    /* the deletion's own line says the link ready, as the build's busy request's does: the failures were the exchanges' alone, never the link's */
    CHECK(StreamLogCount(S, "the link is ready\n") == 2);
    StreamStop(S);
    free(S);

    /* the pin's own free that forces the release: the other pin's buffer is held across the D0 entry and returned by its free */
    S = (STREAM_HARNESS*)calloc(1, sizeof(*S));
    CHECK(S != NULL);
    if (S == NULL)
        return;
    StreamStart(S, NULL);
    Pcm = StreamPcm(S);
    CHECK_EQ(IntcDspStreamOpen(&S->Streams, Pcm, FALSE, 48000, 2, 32, 32, &Render), STATUS_SUCCESS);
    CHECK_EQ(IntcDspStreamOpen(&S->Streams, Pcm, TRUE, 48000, 2, 32, 32, &Capture), STATUS_SUCCESS);
    CHECK_EQ(IntcDspStreamAllocate(&S->Streams, Render, IPC4_STREAM_CASE_BUFFER_BYTES, 2), STATUS_SUCCESS);
    CHECK_EQ(IntcDspStreamAllocate(&S->Streams, Capture, IPC4_STREAM_CASE_BUFFER_BYTES, 2), STATUS_SUCCESS);
    StreamPinOpen(S, Render);
    StreamPinOpen(S, Capture);
    /* the render's free while the capture holds the PCM: its copier is told the stream is going, and the large-configuration set is answered with a firmware error, which faults the link; the forced release follows */
    S->H.Dsp->Faults.IpcStatusForType = SofIpc4ModLargeConfigSet;
    S->H.Dsp->Faults.IpcStatusValue = SofIpc4InvalidResourceState;
    Status = IntcDspStreamFree(&S->Streams, Render);
    CHECK_EQ(Status, STATUS_DEVICE_PROTOCOL_ERROR);
    CHECK_EQ(S->H.Ipc.Link, SofIpcLinkFaulted);
    CHECK_EQ(S->Streams.Faulted, TRUE);
    CHECK_EQ(S->Faults, 1);
    CHECK_EQ(S->FaultDepth, 1);
    /* both tags went back; the render pin's buffer, which its own free asked for, is returned on the way out; the capture pin's is held */
    CHECK_EQ(FakeBusCount(&S->Bus, FAKE_BUS_EVENT_FREE), 2);
    StreamPinHeld(S, 1, 1);
    CHECK_EQ(S->Unmaps, 2);
    CHECK(StreamLogCount(S, "released as after a fault\n") == 1);
    CHECK(StreamLogCount(S, "released after the fault; nothing is sent until the next stream open or D0 entry\n") == 1);
    /* the D0 entry boots the DSP and initializes the manager: the held buffer outlives the generation */
    StreamBoot(S, NULL);
    IntcDspStreamsInit(&S->Streams, S->State, IntcDspStreamsBytes(S->Model), S->Model, &S->H.Ipc, &S->Bus.Interface, &S->Ops);
    CHECK(S->Streams.Ready && !S->Streams.Faulted);
    StreamPinHeld(S, 1, 1);
    /* the capture pin's close: the probe through its MDL, the free that returns the pages once, a second free refused */
    StreamPinClose(S, Capture);
    StreamPinCloseAgainRemoved(S, Capture);
    StreamPinHeld(S, 0, 2);
    /* the render pin's word: its buffer went back on the way out of its free and the pin forgot the MDL, so nothing is held; a free of the word again is refused */
    StreamPinForget(S, Render);
    StreamPinCloseAgainRemoved(S, Render);
    CHECK_EQ(FakeBusCount(&S->Bus, FAKE_BUS_EVENT_BUFFER_HELD_FREE), 2);
    for (Sx = 0; Sx < INTCDSP_STREAMS_MAX; Sx++)
        CHECK_EQ(S->Streams.Stream[Sx].State, IntcDspStreamStateFree);
    CHECK_EQ(FakeBusMdlCount(&S->Bus), 0);
    CHECK_EQ(FakeBusLockDepth, 0);
    StreamStop(S);
    free(S);

    /* the manager's table of held buffers full (R5-01's "held for nobody"): four boots of sixteen pinned streams fill it, and the fifth boot's one stream finds it full at its release. That buffer was detached from the bus's stream, so the slot is the pool's again with the tag, but no record keeps it: it is left allocated, never freed, under a pin or not, and its pin's later free of the stale word is refused. A leak is the safe side, a free under a mapping is not */
    S = (STREAM_HARNESS*)calloc(1, sizeof(*S));
    CHECK(S != NULL);
    if (S == NULL)
        return;
    StreamStart(S, NULL);
    Pcm = StreamPcm(S);
    for (I = 0; I <= INTCDSP_HELD_MAX / INTCDSP_STREAMS_MAX; I++)
    {
        if (I != 0)
        {
            StreamBoot(S, NULL);
            IntcDspStreamsInit(&S->Streams, S->State, IntcDspStreamsBytes(S->Model), S->Model, &S->H.Ipc, &S->Bus.Interface, &S->Ops);
            CHECK(S->Streams.Ready && !S->Streams.Faulted);
        }
        Requests = I < INTCDSP_HELD_MAX / INTCDSP_STREAMS_MAX ? INTCDSP_STREAMS_MAX : 1;
        for (D = 0; D < Requests; D++)
        {
            CHECK_EQ(IntcDspStreamOpen(&S->Streams, Pcm, (D & 1) != 0, 48000, 2, 32, 32, &Taken), STATUS_SUCCESS);
            CHECK_EQ(IntcDspStreamAllocate(&S->Streams, Taken, IPC4_STREAM_CASE_BUFFER_BYTES, 2), STATUS_SUCCESS);
            StreamPinOpen(S, Taken);
        }
        IntcDspStreamsRelease(&S->Streams);
        CHECK(!S->Streams.Ready);
        /* every tag back at every release (R5-01: no tag is kept across a suspend), every buffer detached, none freed */
        CHECK_EQ(S->Streams.Freed, Requests);
        for (D = 0; D < 2; D++)
            for (Sx = 0; Sx < FAKE_BUS_STREAMS; Sx++)
                CHECK(!S->Bus.Stream[D][Sx].Used);
    }
    CHECK_EQ(FakeBusCount(&S->Bus, FAKE_BUS_EVENT_FREE), INTCDSP_HELD_MAX + 1);
    CHECK_EQ(IntcDspStreamsHeld(&S->Streams), INTCDSP_HELD_MAX);
    CHECK_EQ(FakeBusCount(&S->Bus, FAKE_BUS_EVENT_BUFFER_DETACH), INTCDSP_HELD_MAX + 1);
    CHECK_EQ(FakeBusCount(&S->Bus, FAKE_BUS_EVENT_BUFFER_FREE), 0);
    CHECK_EQ(FakeBusCount(&S->Bus, FAKE_BUS_EVENT_BUFFER_FREE_UNDER_PIN), 0);
    CHECK_EQ(FakeBusCount(&S->Bus, FAKE_BUS_EVENT_BUFFER_HELD_FREE), 0);
    CHECK_EQ(FakeBusMdlCount(&S->Bus), INTCDSP_HELD_MAX + 1);
    CHECK(StreamLogCount(S, "held for nobody, the manager's table of held buffers being full; left allocated\n") == 1);
    /* the pins' frees after the fifth boot: the held ones return their pages; the one held for nobody is refused its stale word, the device removed, and its pages stay allocated */
    for (I = 0; I < INTCDSP_HELD_MAX; I++)
        StreamPinClose(S, S->Pins[I].Handle);
    CHECK_EQ(FakeBusCount(&S->Bus, FAKE_BUS_EVENT_BUFFER_HELD_FREE), INTCDSP_HELD_MAX);
    CHECK_EQ(IntcDspStreamsHeld(&S->Streams), 0);
    Stream = IntcDspStreamOf(&S->Streams, S->Pins[INTCDSP_HELD_MAX].Handle);
    CHECK(Stream == NULL);
    FakeBusPinClose(&S->Bus, S->Pins[INTCDSP_HELD_MAX].Mdl);
    CHECK_EQ(IntcDspStreamFree(&S->Streams, S->Pins[INTCDSP_HELD_MAX].Handle), STATUS_DEVICE_REMOVED);
    CHECK(FakeBusMdlMemory(&S->Bus, S->Pins[INTCDSP_HELD_MAX].Mdl) != NULL);
    CHECK_EQ(FakeBusMdlCount(&S->Bus), 1);
    CHECK_EQ(FakeBusLockDepth, 0);
    /* the leak the manager made, freed here so that the sanitizer does not count it against the harness */
    for (I = 0; I < FAKE_BUS_MDLS; I++)
        if (S->Bus.Mdls[I].Mdl != NULL)
        {
            free(S->Bus.Mdls[I].Memory);
            free(S->Bus.Mdls[I].Mdl);
            S->Bus.Mdls[I].Mdl = NULL;
        }
    StreamStop(S);
    free(S);
}

/* (i13) An undo that fails while the link still carries requests (R1-04, FA-03): the build's teardown, the free's copier and the free's teardown. A busy transport (STATUS_RETRY) is not a fault: the undo is sent again a bounded number of times, the driver is not told, no tag goes back and the DSP is not powered down. Still busy after the tries, the allocate fails with STATUS_RETRY, the state records what the firmware holds and the next transition of the PCM finishes the undo; the free answers STATUS_SUCCESS with its teardown pending (N7-01, (i16)). A failure that is not busy still forces the uniform release, as after a lost link */
static
void
CheckStreamStuckUndo(
    void)
{
    STREAM_HARNESS* S;
    HANDLE Render = NULL, Capture = NULL;
    PINTCDSP_STREAM Stream;
    NTSTATUS Status;
    UINT32 Pcm, Requests, I, Used;

    /* the build's third request is busy and the undo's second, an instance's deletion, is busy once: the try after deletes it */
    S = (STREAM_HARNESS*)calloc(1, sizeof(*S));
    CHECK(S != NULL);
    if (S == NULL)
        return;
    StreamStart(S, NULL);
    Pcm = StreamPcm(S);
    CHECK_EQ(IntcDspStreamOpen(&S->Streams, Pcm, FALSE, 48000, 2, 32, 32, &Render), STATUS_SUCCESS);
    CHECK_EQ(IntcDspStreamOpen(&S->Streams, Pcm, TRUE, 48000, 2, 32, 32, &Capture), STATUS_SUCCESS);
    S->H.LockBusyAtRequest = 3;
    S->H.LockBusyAlsoAtRequest = 5;
    Status = IntcDspStreamAllocate(&S->Streams, Render, IPC4_STREAM_CASE_BUFFER_BYTES, 2);
    CHECK_EQ(Status, STATUS_RETRY);
    /* the link is not lost and the DSP is not powered down: no fault, the driver not told */
    CHECK_EQ(S->H.Ipc.Link, SofIpcLinkReady);
    CHECK_EQ(S->H.Ipc.Faults, 0);
    CHECK_EQ(S->Streams.Faulted, FALSE);
    CHECK_EQ(S->Faults, 0);
    CHECK_EQ(S->FaultDepth, 0);
    /* the stream table is as after a build undone: the buffer back, the tag kept for the close, the path and the pipelines as after a boot */
    CHECK_EQ(FakeBusCount(&S->Bus, FAKE_BUS_EVENT_BUFFER_FREE), 1);
    CHECK_EQ(FakeBusCount(&S->Bus, FAKE_BUS_EVENT_SPIB_OFF), 1);
    CHECK_EQ(FakeBusCount(&S->Bus, FAKE_BUS_EVENT_FREE), 0);
    CHECK_EQ(S->Unmaps, 1);
    CHECK_EQ(S->Streams.Path[Pcm].Held, 0);
    CHECK(S->Streams.Path[Pcm].Built == FALSE);
    CHECK(S->Streams.Path[Pcm].Undo == FALSE);
    CHECK_EQ(S->Streams.PipelineInstance[0], 0);
    for (I = Used = 0; I < FAKE_DSP_PIPELINES; I++)
        Used += S->H.Dsp->Pipeline[I].Used ? 1 : 0;
    CHECK_EQ(Used, 0);
    CHECK_EQ(IntcDspStreamOf(&S->Streams, Render)->State, IntcDspStreamStateOpen);
    CHECK(IntcDspStreamOf(&S->Streams, Render)->Mdl == NULL);
    CHECK(IntcDspStreamOf(&S->Streams, Render)->Handle != NULL);
    for (Pcm = 0; Pcm < S->Model->WidgetCount; Pcm++)
        CHECK_EQ(S->Streams.WidgetFlags[Pcm], 0);
    Pcm = StreamPcm(S);
    /* the firmware saw the build's two requests, the undo's reset and, after the busy answer, the instance's deletion and the pipeline's */
    CHECK_EQ(S->H.Dsp->TransportRequests, 5);
    /* the try after the busy answer deleted the rest: the build's undo is one that succeeded, on its second try */
    CHECK(StreamLogCount(S, "could not be built (0xc0000227); what was built is deleted again\n") == 1);
    CHECK(StreamLogCount(S, "the transport answered busy") == 0);
    CHECK(StreamLogCount(S, "released as after a fault\n") == 0);
    CHECK(StreamLogCount(S, "released after the fault") == 0);
    /* the DSP up: the next allocation builds afresh, and the stream frees */
    CHECK_EQ(IntcDspStreamAllocate(&S->Streams, Render, IPC4_STREAM_CASE_BUFFER_BYTES, 2), STATUS_SUCCESS);
    CHECK_EQ(IntcDspStreamFree(&S->Streams, Render), STATUS_SUCCESS);
    CHECK_EQ(S->Faults, 0);
    CHECK_EQ(IntcDspStreamClose(&S->Streams, Render), STATUS_SUCCESS);
    CHECK_EQ(IntcDspStreamClose(&S->Streams, Capture), STATUS_SUCCESS);
    CHECK_EQ(FakeBusLockDepth, 0);
    StreamStop(S);
    free(S);

    /* the free's teardown: the last holder's pipelines, an instance's deletion busy once; the try after deletes everything, and the stream is as a free leaves it */
    S = (STREAM_HARNESS*)calloc(1, sizeof(*S));
    CHECK(S != NULL);
    if (S == NULL)
        return;
    StreamStart(S, NULL);
    Pcm = StreamPcm(S);
    CHECK_EQ(IntcDspStreamOpen(&S->Streams, Pcm, FALSE, 48000, 2, 32, 32, &Render), STATUS_SUCCESS);
    CHECK_EQ(IntcDspStreamAllocate(&S->Streams, Render, IPC4_STREAM_CASE_BUFFER_BYTES, 2), STATUS_SUCCESS);
    S->H.LockBusyAtRequest = S->H.LockAttempts + 2;
    Status = IntcDspStreamFree(&S->Streams, Render);
    CHECK_EQ(Status, STATUS_SUCCESS);
    CHECK_EQ(S->H.Ipc.Link, SofIpcLinkReady);
    CHECK_EQ(S->Streams.Faulted, FALSE);
    CHECK_EQ(S->Faults, 0);
    CHECK_EQ(FakeBusCount(&S->Bus, FAKE_BUS_EVENT_BUFFER_FREE), 1);
    CHECK_EQ(FakeBusCount(&S->Bus, FAKE_BUS_EVENT_FREE), 0);
    CHECK_EQ(S->Streams.Path[Pcm].Held, 0);
    CHECK(S->Streams.Path[Pcm].Built == FALSE);
    CHECK(S->Streams.Path[Pcm].Undo == FALSE);
    CHECK_EQ(S->Streams.PipelineInstance[0], 0);
    CHECK_EQ(IntcDspStreamOf(&S->Streams, Render)->State, IntcDspStreamStateOpen);
    CHECK(StreamLogCount(S, "released as after a fault\n") == 0);
    CHECK_EQ(IntcDspStreamClose(&S->Streams, Render), STATUS_SUCCESS);
    CHECK_EQ(FakeBusLockDepth, 0);
    StreamStop(S);
    free(S);

    /* the free's copier: a stream that goes while another holds the PCM, and its copier is told busy once; the try after tells it, the other stream's path is built still */
    S = (STREAM_HARNESS*)calloc(1, sizeof(*S));
    CHECK(S != NULL);
    if (S == NULL)
        return;
    StreamStart(S, NULL);
    Pcm = StreamPcm(S);
    CHECK_EQ(IntcDspStreamOpen(&S->Streams, Pcm, FALSE, 48000, 2, 32, 32, &Render), STATUS_SUCCESS);
    CHECK_EQ(IntcDspStreamOpen(&S->Streams, Pcm, TRUE, 48000, 2, 32, 32, &Capture), STATUS_SUCCESS);
    CHECK_EQ(IntcDspStreamAllocate(&S->Streams, Render, IPC4_STREAM_CASE_BUFFER_BYTES, 2), STATUS_SUCCESS);
    CHECK_EQ(IntcDspStreamAllocate(&S->Streams, Capture, IPC4_STREAM_CASE_BUFFER_BYTES, 2), STATUS_SUCCESS);
    S->H.LockBusyAtRequest = S->H.LockAttempts + 1;
    Requests = S->H.LockAttempts;
    Status = IntcDspStreamFree(&S->Streams, Capture);
    CHECK_EQ(Status, STATUS_SUCCESS);
    CHECK_EQ(S->H.Ipc.Link, SofIpcLinkReady);
    CHECK_EQ(S->Streams.Faulted, FALSE);
    CHECK_EQ(S->Faults, 0);
    /* one busy answer, one taken: the copier is told twice in all */
    CHECK_EQ(S->H.LockAttempts, Requests + 2);
    CHECK_EQ(FakeBusCount(&S->Bus, FAKE_BUS_EVENT_BUFFER_FREE), 1);
    CHECK_EQ(FakeBusCount(&S->Bus, FAKE_BUS_EVENT_FREE), 0);
    CHECK_EQ(S->Streams.Path[Pcm].Held, 1);
    CHECK(S->Streams.Path[Pcm].Built == TRUE);
    CHECK(S->Streams.Path[Pcm].Undo == FALSE);
    CHECK_EQ(S->Streams.WidgetFlags[IntcDspStreamOf(&S->Streams, Capture)->Copier] & INTCDSP_WIDGET_NAMED, 0);
    CHECK_EQ(IntcDspStreamOf(&S->Streams, Capture)->State, IntcDspStreamStateOpen);
    CHECK(IntcDspStreamOf(&S->Streams, Capture)->Mdl == NULL);
    CHECK_EQ(IntcDspStreamOf(&S->Streams, Render)->State, IntcDspStreamStatePrepared);
    CHECK(IntcDspStreamOf(&S->Streams, Render)->Mdl != NULL);
    CHECK(StreamLogCount(S, "released as after a fault\n") == 0);
    CHECK_EQ(IntcDspStreamFree(&S->Streams, Render), STATUS_SUCCESS);
    CHECK_EQ(S->Faults, 0);
    CHECK_EQ(IntcDspStreamClose(&S->Streams, Render), STATUS_SUCCESS);
    CHECK_EQ(IntcDspStreamClose(&S->Streams, Capture), STATUS_SUCCESS);
    CHECK_EQ(FakeBusLockDepth, 0);
    StreamStop(S);
    free(S);

    /* still busy after the tries, at the free's copier: a stream that goes while another holds the PCM, and its copier is busy to every try. The free answers STATUS_SUCCESS with the buffer held for the manager and the teardown pending (N7-01): the copier still names the stream, the PCM's hold is as it was and the tag is kept; the next transition with the transport free, the other stream's free, drains it first: the copier told, the held buffer returned and the tag back, the stream released for its close */
    S = (STREAM_HARNESS*)calloc(1, sizeof(*S));
    CHECK(S != NULL);
    if (S == NULL)
        return;
    StreamStart(S, NULL);
    Pcm = StreamPcm(S);
    CHECK_EQ(IntcDspStreamOpen(&S->Streams, Pcm, FALSE, 48000, 2, 32, 32, &Render), STATUS_SUCCESS);
    CHECK_EQ(IntcDspStreamOpen(&S->Streams, Pcm, TRUE, 48000, 2, 32, 32, &Capture), STATUS_SUCCESS);
    CHECK_EQ(IntcDspStreamAllocate(&S->Streams, Render, IPC4_STREAM_CASE_BUFFER_BYTES, 2), STATUS_SUCCESS);
    CHECK_EQ(IntcDspStreamAllocate(&S->Streams, Capture, IPC4_STREAM_CASE_BUFFER_BYTES, 2), STATUS_SUCCESS);
    S->H.LockBusyFromRequest = S->H.LockAttempts + 1;
    Requests = S->H.LockAttempts;
    Status = IntcDspStreamFree(&S->Streams, Capture);
    CHECK_EQ(Status, STATUS_SUCCESS);
    CHECK_EQ(S->H.Ipc.Link, SofIpcLinkReady);
    CHECK_EQ(S->Streams.Faulted, FALSE);
    CHECK_EQ(S->Faults, 0);
    /* the copier is told in each of the three tries, busy every time */
    CHECK_EQ(S->H.LockAttempts, Requests + 3);
    CHECK_EQ(S->H.LockWaits, 3);
    CHECK_EQ(FakeBusCount(&S->Bus, FAKE_BUS_EVENT_BUFFER_FREE), 0);
    CHECK_EQ(FakeBusCount(&S->Bus, FAKE_BUS_EVENT_BUFFER_DETACH), 1);
    CHECK_EQ(FakeBusCount(&S->Bus, FAKE_BUS_EVENT_FREE), 0);
    CHECK_EQ(IntcDspStreamsHeld(&S->Streams), 1);
    Stream = IntcDspStreamOf(&S->Streams, Capture);
    CHECK(Stream != NULL && Stream->State == IntcDspStreamStateTeardown && Stream->Mdl == NULL && Stream->Handle != NULL);
    CHECK_EQ(S->Streams.Path[Pcm].Held, 2);
    CHECK(S->Streams.Path[Pcm].Built == TRUE);
    CHECK(S->Streams.Path[Pcm].Undo == FALSE);
    CHECK((S->Streams.WidgetFlags[Stream->Copier] & INTCDSP_WIDGET_NAMED) == INTCDSP_WIDGET_NAMED);
    CHECK_EQ(S->Streams.Freed, 0);
    CHECK(StreamLogCount(S, "its free met a busy transport at the copier's renaming; the buffer is held for the manager and the teardown is pending\n") == 1);
    CHECK(StreamLogCount(S, "released as after a fault\n") == 0);
    /* the transport free: the render's free drains the capture in its prologue, the copier told once, then frees the render and the path; the capture is released, waiting for its close */
    S->H.LockBusyFromRequest = 0;
    CHECK_EQ(IntcDspStreamFree(&S->Streams, Render), STATUS_SUCCESS);
    CHECK_EQ(S->H.LockAttempts, Requests + 9);
    CHECK_EQ(S->Faults, 0);
    CHECK_EQ(FakeBusCount(&S->Bus, FAKE_BUS_EVENT_BUFFER_FREE), 1);
    CHECK_EQ(FakeBusCount(&S->Bus, FAKE_BUS_EVENT_BUFFER_HELD_FREE), 1);
    CHECK_EQ(FakeBusCount(&S->Bus, FAKE_BUS_EVENT_FREE), 1);
    CHECK_EQ(FakeBusMdlCount(&S->Bus), 0);
    CHECK_EQ(IntcDspStreamsHeld(&S->Streams), 0);
    CHECK_EQ(S->Streams.Path[Pcm].Held, 0);
    CHECK(S->Streams.Path[Pcm].Built == FALSE);
    CHECK((S->Streams.WidgetFlags[Stream->Copier] & INTCDSP_WIDGET_NAMED) == 0);
    CHECK_EQ(Stream->State, IntcDspStreamStateReleased);
    CHECK_EQ(S->Streams.Freed, 1);
    CHECK_EQ(IntcDspStreamOf(&S->Streams, Render)->State, IntcDspStreamStateOpen);
    CHECK(StreamLogCount(S, "its pending teardown is done; the buffer is returned and the tag is back, the stream left for its pin's close\n") == 1);
    /* the pins' closes: the released stream is freed, the open one as a free left it */
    CHECK_EQ(IntcDspStreamClose(&S->Streams, Render), STATUS_SUCCESS);
    CHECK_EQ(IntcDspStreamClose(&S->Streams, Capture), STATUS_SUCCESS);
    CHECK_EQ(FakeBusCount(&S->Bus, FAKE_BUS_EVENT_FREE), 2);
    CHECK_EQ(S->Streams.Freed, 2);
    StreamQuiescent(S, Pcm);
    CHECK_EQ(FakeBusLockDepth, 0);
    StreamStop(S);
    free(S);

    /* still busy after the tries, at the build's undo: busy from its instance's deletion on. The allocation fails with STATUS_RETRY, nothing is faulted, and the state records what the firmware holds, the pipeline created and reset and the instance initialized; the next allocation finishes the undo and builds afresh */
    S = (STREAM_HARNESS*)calloc(1, sizeof(*S));
    CHECK(S != NULL);
    if (S == NULL)
        return;
    StreamStart(S, NULL);
    Pcm = StreamPcm(S);
    CHECK_EQ(IntcDspStreamOpen(&S->Streams, Pcm, FALSE, 48000, 2, 32, 32, &Render), STATUS_SUCCESS);
    S->H.LockBusyAtRequest = 3;
    S->H.LockBusyFromRequest = 5;
    Status = IntcDspStreamAllocate(&S->Streams, Render, IPC4_STREAM_CASE_BUFFER_BYTES, 2);
    CHECK_EQ(Status, STATUS_RETRY);
    CHECK_EQ(S->H.Ipc.Link, SofIpcLinkReady);
    CHECK_EQ(S->Streams.Faulted, FALSE);
    CHECK_EQ(S->Faults, 0);
    /* the buffer went back and the tag is kept; the path is the firmware's: built, its undo owed */
    CHECK_EQ(FakeBusCount(&S->Bus, FAKE_BUS_EVENT_BUFFER_FREE), 1);
    CHECK_EQ(FakeBusCount(&S->Bus, FAKE_BUS_EVENT_FREE), 0);
    CHECK_EQ(S->Streams.Path[Pcm].Held, 0);
    CHECK(S->Streams.Path[Pcm].Built == TRUE);
    CHECK(S->Streams.Path[Pcm].Undo == TRUE);
    CHECK_EQ(S->Streams.PipelineInstance[0], 1);
    CHECK_EQ(S->Streams.PipelineState[0], SOFIPC4_PIPELINE_STATE_RESET);
    for (I = Used = 0; I < FAKE_DSP_PIPELINES; I++)
        Used += S->H.Dsp->Pipeline[I].Used ? 1 : 0;
    CHECK_EQ(Used, 1);
    CHECK_EQ(IntcDspStreamOf(&S->Streams, Render)->State, IntcDspStreamStateOpen);
    CHECK(IntcDspStreamOf(&S->Streams, Render)->Mdl == NULL);
    /* the build's two requests and the undo's reset were sent; then busy: the build's third, the undo's instance's deletion in each of the build's three tries, and in each of the three the buffer's undo sent after */
    CHECK_EQ(S->H.Dsp->TransportRequests, 3);
    CHECK_EQ(S->H.LockAttempts, 10);
    CHECK_EQ(S->H.LockWaits, 7);
    CHECK(StreamLogCount(S, "the transport answered busy to the undo of what was built") == 1);
    CHECK(StreamLogCount(S, "could not be deleted after the failed build (0xc0000227): the transport answered busy") == 1);
    CHECK(StreamLogCount(S, "released as after a fault\n") == 0);
    /* the transport free: the allocation finishes the undo first, then builds afresh, and the stream runs and frees */
    S->H.LockBusyFromRequest = 0;
    CHECK_EQ(IntcDspStreamAllocate(&S->Streams, Render, IPC4_STREAM_CASE_BUFFER_BYTES, 2), STATUS_SUCCESS);
    CHECK_EQ(S->Faults, 0);
    CHECK(S->Streams.Path[Pcm].Built == TRUE);
    CHECK(S->Streams.Path[Pcm].Undo == FALSE);
    CHECK_EQ(IntcDspStreamRun(&S->Streams, Render), STATUS_SUCCESS);
    CHECK_EQ(IntcDspStreamPause(&S->Streams, Render), STATUS_SUCCESS);
    CHECK_EQ(IntcDspStreamFree(&S->Streams, Render), STATUS_SUCCESS);
    CHECK_EQ(S->Streams.PipelineInstance[0], 0);
    CHECK_EQ(IntcDspStreamClose(&S->Streams, Render), STATUS_SUCCESS);
    CHECK_EQ(FakeBusLockDepth, 0);
    StreamStop(S);
    free(S);

    /* still busy after the tries, at the free: the last holder's teardown meets busy from its instance's deletion on. The free answers STATUS_SUCCESS with the buffer held for the manager and the teardown pending (N7-01); the path is the firmware's, its undo owed; the stream does not start or take a buffer while pending (STATUS_RETRY, the drain meeting busy again), is answered stopped, and its close is accepted and orphans it; the next transition with the transport free drains it and the object is freed */
    S = (STREAM_HARNESS*)calloc(1, sizeof(*S));
    CHECK(S != NULL);
    if (S == NULL)
        return;
    StreamStart(S, NULL);
    Pcm = StreamPcm(S);
    CHECK_EQ(IntcDspStreamOpen(&S->Streams, Pcm, FALSE, 48000, 2, 32, 32, &Render), STATUS_SUCCESS);
    CHECK_EQ(IntcDspStreamAllocate(&S->Streams, Render, IPC4_STREAM_CASE_BUFFER_BYTES, 2), STATUS_SUCCESS);
    S->H.LockBusyFromRequest = S->H.LockAttempts + 2;
    Requests = S->H.LockAttempts;
    Status = IntcDspStreamFree(&S->Streams, Render);
    CHECK_EQ(Status, STATUS_SUCCESS);
    CHECK_EQ(S->H.Ipc.Link, SofIpcLinkReady);
    CHECK_EQ(S->Streams.Faulted, FALSE);
    CHECK_EQ(S->Faults, 0);
    /* the reset was sent, the instance's deletion was busy in each of the three tries */
    CHECK_EQ(S->H.LockAttempts, Requests + 4);
    CHECK_EQ(S->H.LockWaits, 3);
    CHECK_EQ(FakeBusCount(&S->Bus, FAKE_BUS_EVENT_BUFFER_FREE), 0);
    CHECK_EQ(FakeBusCount(&S->Bus, FAKE_BUS_EVENT_BUFFER_DETACH), 1);
    CHECK_EQ(FakeBusCount(&S->Bus, FAKE_BUS_EVENT_FREE), 0);
    CHECK_EQ(IntcDspStreamsHeld(&S->Streams), 1);
    Stream = IntcDspStreamOf(&S->Streams, Render);
    CHECK(Stream != NULL && Stream->State == IntcDspStreamStateTeardown && Stream->Mdl == NULL && Stream->Handle != NULL);
    CHECK_EQ(S->Streams.Path[Pcm].Held, 1);
    CHECK(S->Streams.Path[Pcm].Built == TRUE);
    CHECK(S->Streams.Path[Pcm].Undo == TRUE);
    CHECK_EQ(S->Streams.PipelineInstance[0], 1);
    CHECK_EQ(S->Streams.PipelineState[0], SOFIPC4_PIPELINE_STATE_RESET);
    CHECK(StreamLogCount(S, "its free met a busy transport at the pipelines' teardown; the buffer is held for the manager and the teardown is pending\n") == 1);
    CHECK(StreamLogCount(S, "released as after a fault\n") == 0);
    /* while pending the stream does not start or take a buffer, the drain meeting the busy transport again at each; stopped, it is answered; nothing else of it changes */
    CHECK_EQ(IntcDspStreamRun(&S->Streams, Render), STATUS_RETRY);
    CHECK_EQ(IntcDspStreamAllocate(&S->Streams, Render, IPC4_STREAM_CASE_BUFFER_BYTES, 2), STATUS_RETRY);
    CHECK_EQ(IntcDspStreamPause(&S->Streams, Render), STATUS_SUCCESS);
    CHECK_EQ(S->H.LockAttempts, Requests + 13);
    CHECK_EQ(S->Faults, 0);
    CHECK_EQ(IntcDspStreamsHeld(&S->Streams), 1);
    CHECK_EQ(S->Streams.Path[Pcm].Held, 1);
    /* the pin's close while the transport is still busy: accepted, the stream orphaned, its teardown still owed */
    CHECK_EQ(IntcDspStreamClose(&S->Streams, Render), STATUS_SUCCESS);
    CHECK_EQ(S->H.LockAttempts, Requests + 16);
    Stream = IntcDspStreamOf(&S->Streams, Render);
    CHECK(Stream != NULL && Stream->State == IntcDspStreamStateTeardown && Stream->Orphaned);
    CHECK(StreamLogCount(S, "its pending teardown is done") == 0);
    /* the transport free: the next transition drains it, and the object is freed: the instances and the pipeline deleted, the buffer returned, the tag back; the transition's own work then finds the handle naming nothing */
    S->H.LockBusyFromRequest = 0;
    CHECK_EQ(IntcDspStreamFree(&S->Streams, Render), STATUS_INVALID_HANDLE);
    CHECK_EQ(S->H.LockAttempts, Requests + 20);
    CHECK_EQ(S->Faults, 0);
    CHECK_EQ(FakeBusCount(&S->Bus, FAKE_BUS_EVENT_BUFFER_HELD_FREE), 1);
    CHECK_EQ(FakeBusCount(&S->Bus, FAKE_BUS_EVENT_FREE), 1);
    CHECK_EQ(FakeBusMdlCount(&S->Bus), 0);
    CHECK_EQ(S->Streams.Path[Pcm].Held, 0);
    CHECK(S->Streams.Path[Pcm].Built == FALSE);
    CHECK(S->Streams.Path[Pcm].Undo == FALSE);
    CHECK_EQ(S->Streams.PipelineInstance[0], 0);
    CHECK(IntcDspStreamOf(&S->Streams, Render) == NULL);
    CHECK(StreamLogCount(S, "its pending teardown is done; the buffer is returned and the tag is back, its pin having closed it\n") == 1);
    /* the object is the pool's again: the next open takes it */
    CHECK_EQ(IntcDspStreamOpen(&S->Streams, Pcm, FALSE, 48000, 2, 32, 32, &Capture), STATUS_SUCCESS);
    CHECK(IntcDspStreamOf(&S->Streams, Capture) == &S->Streams.Stream[0]);
    CHECK_EQ(IntcDspStreamClose(&S->Streams, Capture), STATUS_SUCCESS);
    StreamQuiescent(S, Pcm);
    CHECK_EQ(FakeBusLockDepth, 0);
    StreamStop(S);
    free(S);
}

/* (i16) A free that meets a busy transport (N7-01): the pin is answered STATUS_SUCCESS and forgets its buffer, so the manager takes it. The stream's bus stream is stopped and its buffer detached and held for the manager, the tag and the pipeline hold kept, the teardown pending; the pin's Close is accepted meanwhile, freeing the stream when the drain runs and orphaning it when the drain is still busy, the next transition finishing it; the drain sends the release again, returns the held buffer, gives the tag back and closes the stream. A fault inside the free returns the buffer on the way out, as the other forced releases of the free do. The bus's stream is stopped before its buffer is detached */
static
void
CheckStreamPendingTeardown(
    void)
{
    STREAM_HARNESS* S;
    HANDLE Render = NULL, Capture = NULL;
    PINTCDSP_STREAM Stream;
    NTSTATUS Status;
    UINT32 Pcm, Requests;

    /* the pin's close while the transport is still busy: accepted, the stream orphaned; a second free of the word returns the held buffer; the other stream's free drains the orphan and frees the object */
    S = (STREAM_HARNESS*)calloc(1, sizeof(*S));
    CHECK(S != NULL);
    if (S == NULL)
        return;
    StreamStart(S, NULL);
    Pcm = StreamPcm(S);
    CHECK_EQ(IntcDspStreamOpen(&S->Streams, Pcm, FALSE, 48000, 2, 32, 32, &Render), STATUS_SUCCESS);
    CHECK_EQ(IntcDspStreamOpen(&S->Streams, Pcm, TRUE, 48000, 2, 32, 32, &Capture), STATUS_SUCCESS);
    CHECK_EQ(IntcDspStreamAllocate(&S->Streams, Render, IPC4_STREAM_CASE_BUFFER_BYTES, 2), STATUS_SUCCESS);
    CHECK_EQ(IntcDspStreamAllocate(&S->Streams, Capture, IPC4_STREAM_CASE_BUFFER_BYTES, 2), STATUS_SUCCESS);
    StreamPinOpen(S, Render);
    StreamPinOpen(S, Capture);
    S->H.LockBusyFromRequest = S->H.LockAttempts + 1;
    Requests = S->H.LockAttempts;
    CHECK_EQ(IntcDspStreamFree(&S->Streams, Capture), STATUS_SUCCESS);
    CHECK_EQ(S->H.LockAttempts, Requests + 3);
    /* the wave pin forgets its MDL whatever the answer (pin_wavert.cpp) */
    StreamPinForget(S, Capture);
    StreamPinHeld(S, 1, 0);
    CHECK_EQ(FakeBusMdlCount(&S->Bus), 2);
    Stream = IntcDspStreamOf(&S->Streams, Capture);
    CHECK(Stream != NULL && Stream->State == IntcDspStreamStateTeardown && !Stream->Orphaned);
    /* a second free of the word: answered, and it returns the held buffer as the free of a pin's held buffer does (R5-01); the teardown is still pending */
    CHECK_EQ(IntcDspStreamFree(&S->Streams, Capture), STATUS_SUCCESS);
    CHECK_EQ(S->H.LockAttempts, Requests + 6);
    CHECK_EQ(FakeBusCount(&S->Bus, FAKE_BUS_EVENT_BUFFER_HELD_FREE), 1);
    CHECK_EQ(IntcDspStreamsHeld(&S->Streams), 0);
    CHECK_EQ(FakeBusMdlCount(&S->Bus), 1);
    Stream = IntcDspStreamOf(&S->Streams, Capture);
    CHECK(Stream != NULL && Stream->State == IntcDspStreamStateTeardown);
    /* the pin's close while the transport is still busy: accepted, the stream orphaned, its teardown still owed */
    CHECK_EQ(IntcDspStreamClose(&S->Streams, Capture), STATUS_SUCCESS);
    CHECK_EQ(S->H.LockAttempts, Requests + 9);
    Stream = IntcDspStreamOf(&S->Streams, Capture);
    CHECK(Stream != NULL && Stream->State == IntcDspStreamStateTeardown && Stream->Orphaned);
    CHECK(StreamLogCount(S, "its pending teardown is done") == 0);
    /* the transport free: the render pin's close frees its stream, whose prologue drains the orphan: the copier told, the tag back, the object freed; the render's own free follows */
    S->H.LockBusyFromRequest = 0;
    StreamPinClose(S, Render);
    CHECK_EQ(S->H.LockAttempts, Requests + 15);
    CHECK_EQ(S->Faults, 0);
    CHECK(IntcDspStreamOf(&S->Streams, Capture) == NULL);
    CHECK_EQ(FakeBusCount(&S->Bus, FAKE_BUS_EVENT_FREE), 1);
    CHECK_EQ(FakeBusCount(&S->Bus, FAKE_BUS_EVENT_BUFFER_HELD_FREE), 1);
    CHECK_EQ(FakeBusCount(&S->Bus, FAKE_BUS_EVENT_BUFFER_FREE), 1);
    CHECK_EQ(FakeBusMdlCount(&S->Bus), 0);
    CHECK_EQ(S->Streams.Path[Pcm].Held, 0);
    CHECK(StreamLogCount(S, "its pending teardown is done; the buffer is returned and the tag is back, its pin having closed it\n") == 1);
    /* the freed word names nothing; the render's close frees its tag */
    StreamPinCloseAgain(S, Capture);
    CHECK_EQ(IntcDspStreamClose(&S->Streams, Render), STATUS_SUCCESS);
    CHECK_EQ(FakeBusCount(&S->Bus, FAKE_BUS_EVENT_FREE), 2);
    CHECK_EQ(S->Streams.Freed, 2);
    StreamQuiescent(S, Pcm);
    CHECK_EQ(FakeBusLockDepth, 0);
    StreamStop(S);
    free(S);

    /* the pin's close with the transport free: the prologue drains the pending teardown and the close frees the stream at once */
    S = (STREAM_HARNESS*)calloc(1, sizeof(*S));
    CHECK(S != NULL);
    if (S == NULL)
        return;
    StreamStart(S, NULL);
    Pcm = StreamPcm(S);
    CHECK_EQ(IntcDspStreamOpen(&S->Streams, Pcm, FALSE, 48000, 2, 32, 32, &Render), STATUS_SUCCESS);
    CHECK_EQ(IntcDspStreamOpen(&S->Streams, Pcm, TRUE, 48000, 2, 32, 32, &Capture), STATUS_SUCCESS);
    CHECK_EQ(IntcDspStreamAllocate(&S->Streams, Render, IPC4_STREAM_CASE_BUFFER_BYTES, 2), STATUS_SUCCESS);
    CHECK_EQ(IntcDspStreamAllocate(&S->Streams, Capture, IPC4_STREAM_CASE_BUFFER_BYTES, 2), STATUS_SUCCESS);
    StreamPinOpen(S, Render);
    StreamPinOpen(S, Capture);
    S->H.LockBusyFromRequest = S->H.LockAttempts + 1;
    Requests = S->H.LockAttempts;
    CHECK_EQ(IntcDspStreamFree(&S->Streams, Capture), STATUS_SUCCESS);
    StreamPinForget(S, Capture);
    S->H.LockBusyFromRequest = 0;
    CHECK_EQ(IntcDspStreamClose(&S->Streams, Capture), STATUS_SUCCESS);
    /* the copier told once, the tag back, the object freed by the close */
    CHECK_EQ(S->H.LockAttempts, Requests + 4);
    CHECK_EQ(S->Faults, 0);
    CHECK(IntcDspStreamOf(&S->Streams, Capture) == NULL);
    CHECK_EQ(FakeBusCount(&S->Bus, FAKE_BUS_EVENT_FREE), 1);
    CHECK_EQ(FakeBusCount(&S->Bus, FAKE_BUS_EVENT_BUFFER_HELD_FREE), 1);
    CHECK_EQ(IntcDspStreamsHeld(&S->Streams), 0);
    CHECK_EQ(S->Streams.Path[Pcm].Held, 1);
    CHECK(StreamLogCount(S, "its pending teardown is done; the buffer is returned and the tag is back, the stream left for its pin's close\n") == 1);
    StreamPinClose(S, Render);
    StreamPinCloseAgain(S, Capture);
    CHECK_EQ(IntcDspStreamClose(&S->Streams, Render), STATUS_SUCCESS);
    CHECK_EQ(FakeBusCount(&S->Bus, FAKE_BUS_EVENT_FREE), 2);
    CHECK_EQ(FakeBusMdlCount(&S->Bus), 0);
    StreamQuiescent(S, Pcm);
    CHECK_EQ(FakeBusLockDepth, 0);
    StreamStop(S);
    free(S);

    /* the drain's renaming answered with a firmware error: the link faults, the forced release lets both streams go and returns the pending buffer on the way out, the render pin's buffer held for it */
    S = (STREAM_HARNESS*)calloc(1, sizeof(*S));
    CHECK(S != NULL);
    if (S == NULL)
        return;
    StreamStart(S, NULL);
    Pcm = StreamPcm(S);
    CHECK_EQ(IntcDspStreamOpen(&S->Streams, Pcm, FALSE, 48000, 2, 32, 32, &Render), STATUS_SUCCESS);
    CHECK_EQ(IntcDspStreamOpen(&S->Streams, Pcm, TRUE, 48000, 2, 32, 32, &Capture), STATUS_SUCCESS);
    CHECK_EQ(IntcDspStreamAllocate(&S->Streams, Render, IPC4_STREAM_CASE_BUFFER_BYTES, 2), STATUS_SUCCESS);
    CHECK_EQ(IntcDspStreamAllocate(&S->Streams, Capture, IPC4_STREAM_CASE_BUFFER_BYTES, 2), STATUS_SUCCESS);
    StreamPinOpen(S, Render);
    StreamPinOpen(S, Capture);
    S->H.LockBusyFromRequest = S->H.LockAttempts + 1;
    CHECK_EQ(IntcDspStreamFree(&S->Streams, Capture), STATUS_SUCCESS);
    StreamPinForget(S, Capture);
    S->H.LockBusyFromRequest = 0;
    S->H.Dsp->Faults.IpcStatusForType = SofIpc4ModLargeConfigSet;
    S->H.Dsp->Faults.IpcStatusValue = SofIpc4InvalidResourceState;
    /* the render's run drains the capture first, and the drain faults the link: the transition finds the manager faulted, which let the render stream go too (E1, D4) */
    Status = IntcDspStreamRun(&S->Streams, Render);
    CHECK_EQ(Status, STATUS_DEVICE_REMOVED);
    CHECK_EQ(S->H.Ipc.Link, SofIpcLinkFaulted);
    CHECK_EQ(S->Streams.Faulted, TRUE);
    CHECK_EQ(S->Faults, 1);
    CHECK_EQ(S->FaultDepth, 1);
    /* both tags went back, the pending buffer whose pin forgot it is returned, the render pin's is held */
    CHECK_EQ(S->Streams.Freed, 2);
    CHECK_EQ(FakeBusCount(&S->Bus, FAKE_BUS_EVENT_FREE), 2);
    CHECK_EQ(FakeBusCount(&S->Bus, FAKE_BUS_EVENT_BUFFER_HELD_FREE), 1);
    CHECK_EQ(IntcDspStreamsHeld(&S->Streams), 1);
    StreamPinHeld(S, 1, 1);
    CHECK(StreamLogCount(S, "released as after a fault\n") == 1);
    CHECK(StreamLogCount(S, "streams: 1 teardowns were pending at the release; their buffers are returned\n") == 1);
    CHECK(StreamLogCount(S, "its pending teardown is done") == 0);
    /* the pins after the fault: the render pin's free returns its held buffer, the capture's word holds nothing; both close */
    StreamPinClose(S, Render);
    StreamPinCloseAgainRemoved(S, Render);
    StreamPinCloseAgainRemoved(S, Capture);
    StreamPinHeld(S, 0, 2);
    CHECK_EQ(IntcDspStreamClose(&S->Streams, Render), STATUS_SUCCESS);
    CHECK_EQ(IntcDspStreamClose(&S->Streams, Capture), STATUS_SUCCESS);
    for (Requests = 0; Requests < INTCDSP_STREAMS_MAX; Requests++)
        CHECK_EQ(S->Streams.Stream[Requests].State, IntcDspStreamStateFree);
    CHECK_EQ(FakeBusMdlCount(&S->Bus), 0);
    StreamStop(S);
    free(S);

    /* a pause that faults inside the free (N7-01's fifth way): the fault's release holds the buffer for a pin that forgets it with the answer, so the free returns it on the way out and the status is the fault's */
    S = (STREAM_HARNESS*)calloc(1, sizeof(*S));
    CHECK(S != NULL);
    if (S == NULL)
        return;
    StreamStart(S, NULL);
    Pcm = StreamPcm(S);
    CHECK_EQ(IntcDspStreamOpen(&S->Streams, Pcm, FALSE, 48000, 2, 32, 32, &Render), STATUS_SUCCESS);
    CHECK_EQ(IntcDspStreamAllocate(&S->Streams, Render, IPC4_STREAM_CASE_BUFFER_BYTES, 2), STATUS_SUCCESS);
    StreamPinOpen(S, Render);
    CHECK_EQ(IntcDspStreamRun(&S->Streams, Render), STATUS_SUCCESS);
    S->H.Dsp->Faults.IpcStatusForType = SofIpc4GlbSetPipelineState;
    S->H.Dsp->Faults.IpcStatusValue = SofIpc4InvalidResourceState;
    Status = IntcDspStreamFree(&S->Streams, Render);
    CHECK_EQ(Status, STATUS_DEVICE_PROTOCOL_ERROR);
    CHECK_EQ(S->H.Ipc.Link, SofIpcLinkFaulted);
    CHECK_EQ(S->Streams.Faulted, TRUE);
    CHECK_EQ(S->Faults, 1);
    CHECK_EQ(FakeBusCount(&S->Bus, FAKE_BUS_EVENT_FREE), 1);
    CHECK_EQ(FakeBusCount(&S->Bus, FAKE_BUS_EVENT_BUFFER_FREE), 0);
    CHECK_EQ(FakeBusCount(&S->Bus, FAKE_BUS_EVENT_BUFFER_FREE_UNDER_PIN), 0);
    CHECK_EQ(FakeBusCount(&S->Bus, FAKE_BUS_EVENT_BUFFER_HELD_FREE), 1);
    CHECK_EQ(IntcDspStreamsHeld(&S->Streams), 0);
    CHECK_EQ(FakeBusMdlCount(&S->Bus), 0);
    Stream = IntcDspStreamOf(&S->Streams, Render);
    CHECK(Stream != NULL && Stream->State == IntcDspStreamStateReleased);
    CHECK(StreamLogCount(S, "teardowns were pending at the release") == 0);
    StreamPinForget(S, Render);
    CHECK_EQ(IntcDspStreamClose(&S->Streams, Render), STATUS_SUCCESS);
    CHECK_EQ(FakeBusLockDepth, 0);
    StreamStop(S);
    free(S);

    /* a pause that is busy inside the free, and the teardown after it busy to every try: the bus's stream is stopped before its buffer is detached, and the free is answered with the teardown pending; the close's prologue drains it with the transport free */
    S = (STREAM_HARNESS*)calloc(1, sizeof(*S));
    CHECK(S != NULL);
    if (S == NULL)
        return;
    StreamStart(S, NULL);
    Pcm = StreamPcm(S);
    CHECK_EQ(IntcDspStreamOpen(&S->Streams, Pcm, FALSE, 48000, 2, 32, 32, &Render), STATUS_SUCCESS);
    CHECK_EQ(IntcDspStreamAllocate(&S->Streams, Render, IPC4_STREAM_CASE_BUFFER_BYTES, 2), STATUS_SUCCESS);
    StreamPinOpen(S, Render);
    CHECK_EQ(IntcDspStreamRun(&S->Streams, Render), STATUS_SUCCESS);
    S->H.LockBusyFromRequest = S->H.LockAttempts + 1;
    Requests = S->H.LockAttempts;
    Status = IntcDspStreamFree(&S->Streams, Render);
    CHECK_EQ(Status, STATUS_SUCCESS);
    /* the pause's request, then the teardown's pause in each of the three tries */
    CHECK_EQ(S->H.LockAttempts, Requests + 4);
    CHECK_EQ(S->H.Ipc.Link, SofIpcLinkReady);
    CHECK_EQ(S->Streams.Faulted, FALSE);
    CHECK_EQ(S->Faults, 0);
    CHECK_EQ(FakeBusCount(&S->Bus, FAKE_BUS_EVENT_STOP), 1);
    CHECK_EQ(FakeBusCount(&S->Bus, FAKE_BUS_EVENT_BUFFER_DETACH), 1);
    /* the order: the bus's stream stopped before the buffer left it, a detach under a running stream being what the bus refuses */
    CHECK(StreamBusEventIndex(S, FAKE_BUS_EVENT_STOP, 0) < StreamBusEventIndex(S, FAKE_BUS_EVENT_BUFFER_DETACH, 0));
    Stream = IntcDspStreamOf(&S->Streams, Render);
    CHECK(Stream != NULL && Stream->State == IntcDspStreamStateTeardown && Stream->Mdl == NULL && Stream->Handle != NULL);
    CHECK_EQ(S->Streams.Path[Pcm].Held, 1);
    CHECK(S->Streams.Path[Pcm].Built == TRUE);
    CHECK(S->Streams.Path[Pcm].Undo == TRUE);
    CHECK_EQ(IntcDspStreamsHeld(&S->Streams), 1);
    CHECK(StreamLogCount(S, "its free met a busy transport at the pause of its pipelines and at the pipelines' teardown; the buffer is held for the manager and the teardown is pending\n") == 1);
    StreamPinForget(S, Render);
    S->H.LockBusyFromRequest = 0;
    CHECK_EQ(IntcDspStreamClose(&S->Streams, Render), STATUS_SUCCESS);
    CHECK_EQ(S->Faults, 0);
    /* the pipeline paused, reset, its instances and it deleted; the buffer returned, the tag back, the object freed by the close */
    CHECK_EQ(FakeBusCount(&S->Bus, FAKE_BUS_EVENT_BUFFER_HELD_FREE), 1);
    CHECK_EQ(FakeBusCount(&S->Bus, FAKE_BUS_EVENT_FREE), 1);
    CHECK_EQ(FakeBusMdlCount(&S->Bus), 0);
    CHECK_EQ(S->Streams.PipelineInstance[0], 0);
    CHECK_EQ(S->Streams.Path[Pcm].Held, 0);
    CHECK(IntcDspStreamOf(&S->Streams, Render) == NULL);
    StreamQuiescent(S, Pcm);
    CHECK_EQ(FakeBusLockDepth, 0);
    StreamStop(S);
    free(S);

    /* a pause that is busy inside the free and a release that goes through: the stream is as a free leaves it, its buffer freed by the bus, nothing pending */
    S = (STREAM_HARNESS*)calloc(1, sizeof(*S));
    CHECK(S != NULL);
    if (S == NULL)
        return;
    StreamStart(S, NULL);
    Pcm = StreamPcm(S);
    CHECK_EQ(IntcDspStreamOpen(&S->Streams, Pcm, FALSE, 48000, 2, 32, 32, &Render), STATUS_SUCCESS);
    CHECK_EQ(IntcDspStreamAllocate(&S->Streams, Render, IPC4_STREAM_CASE_BUFFER_BYTES, 2), STATUS_SUCCESS);
    StreamPinOpen(S, Render);
    CHECK_EQ(IntcDspStreamRun(&S->Streams, Render), STATUS_SUCCESS);
    /* the pin unmaps before its free (pin_wavert.cpp): the fake's count of the pin goes with the mapping, so the bus's free is not refused under it */
    FakeBusPinClose(&S->Bus, StreamPinOf(S, Render)->Mdl);
    S->H.LockBusyAtRequest = S->H.LockAttempts + 1;
    Requests = S->H.LockAttempts;
    Status = IntcDspStreamFree(&S->Streams, Render);
    CHECK_EQ(Status, STATUS_SUCCESS);
    /* the busy pause, then the teardown's pause, reset, three instances and the pipeline */
    CHECK_EQ(S->H.LockAttempts, Requests + 7);
    CHECK_EQ(S->Faults, 0);
    CHECK_EQ(FakeBusCount(&S->Bus, FAKE_BUS_EVENT_STOP), 1);
    CHECK_EQ(FakeBusCount(&S->Bus, FAKE_BUS_EVENT_BUFFER_DETACH), 0);
    CHECK_EQ(FakeBusCount(&S->Bus, FAKE_BUS_EVENT_BUFFER_FREE), 1);
    CHECK_EQ(IntcDspStreamsHeld(&S->Streams), 0);
    CHECK_EQ(IntcDspStreamOf(&S->Streams, Render)->State, IntcDspStreamStateOpen);
    CHECK(StreamLogCount(S, "teardown is pending") == 0);
    StreamPinForget(S, Render);
    CHECK_EQ(IntcDspStreamClose(&S->Streams, Render), STATUS_SUCCESS);
    StreamQuiescent(S, Pcm);
    CHECK_EQ(FakeBusLockDepth, 0);
    StreamStop(S);
    free(S);
}

/* Whether a widget is one the manager initializes, as stream.c IntcDspIsModule decides it: a module widget, neither nothing nor the scheduler */
static
BOOLEAN
StreamWidgetIsModule(
    const SOFTPLG_IPC4_WIDGET* Widget)
{
    return Widget->Kind != SoftplgKindNone && Widget->Kind != SoftplgKindScheduler;
}

/* (i14) A model whose module widget names a module id the manager cannot count (R1-05): the manager does not start on it, no stream opens and nothing is sent, and the line says why; the model within the range starts again */
static
void
CheckStreamModuleStart(
    void)
{
    STREAM_HARNESS* S = (STREAM_HARNESS*)calloc(1, sizeof(*S));
    HANDLE Render = NULL;
    UINT32 Pcm, Saved;

    CHECK(S != NULL);
    if (S == NULL)
        return;
    StreamStart(S, NULL);
    Pcm = StreamPcm(S);
    /* the first widget, the playback copier: a module widget, naming a module no table of the manager's size holds */
    CHECK(StreamWidgetIsModule(&S->Model->Widgets[0]));
    Saved = S->Model->Widgets[0].ModuleId;
    S->Model->Widgets[0].ModuleId = 0xFFFFFFFF;
    IntcDspStreamsInit(&S->Streams, S->State, IntcDspStreamsBytes(S->Model), S->Model, &S->H.Ipc, &S->Bus.Interface, &S->Ops);
    CHECK(!S->Streams.Ready);
    CHECK(StreamLogCount(S, "streams: not started (a widget names a module the manager cannot keep)\n") == 1);
    CHECK_EQ(IntcDspStreamOpen(&S->Streams, Pcm, FALSE, 48000, 2, 32, 32, &Render), STATUS_DEVICE_NOT_READY);
    CHECK(Render == NULL);
    CHECK_EQ(S->H.Dsp->TransportRequests, 0);
    CHECK_EQ(FakeBusCount(&S->Bus, FAKE_BUS_EVENT_OPEN), 0);
    /* the model as the topology gave it starts */
    S->Model->Widgets[0].ModuleId = Saved;
    IntcDspStreamsInit(&S->Streams, S->State, IntcDspStreamsBytes(S->Model), S->Model, &S->H.Ipc, &S->Bus.Interface, &S->Ops);
    CHECK(S->Streams.Ready);
    CHECK(StreamLogCount(S, "streams: not started (a widget names a module the manager cannot keep)\n") == 1);
    StreamStop(S);
    free(S);
}

/* (i15) The range the manager keeps (R1-05): every module widget of the loopback model within it, a module widget at INTCDSP_STREAM_MODULES_MAX named and the id below accepted; a widget without a module names any id */
static
void
CheckStreamModuleRange(
    void)
{
    STREAM_HARNESS* S = (STREAM_HARNESS*)calloc(1, sizeof(*S));
    const SOFTPLG_IPC4_WIDGET* Bare = NULL;
    UINT32 W, Saved;

    CHECK(S != NULL);
    if (S == NULL)
        return;
    StreamStart(S, NULL);
    CHECK_EQ(IntcDspStreamsModuleRange(S->Model), SOFTPLG_NO_WIDGET);
    for (W = 0; W < S->Model->WidgetCount; W++)
        if (!StreamWidgetIsModule(&S->Model->Widgets[W]))
            Bare = &S->Model->Widgets[W];
    Saved = S->Model->Widgets[0].ModuleId;
    S->Model->Widgets[0].ModuleId = INTCDSP_STREAM_MODULES_MAX;
    CHECK_EQ(IntcDspStreamsModuleRange(S->Model), 0);
    S->Model->Widgets[0].ModuleId = INTCDSP_STREAM_MODULES_MAX - 1;
    CHECK_EQ(IntcDspStreamsModuleRange(S->Model), SOFTPLG_NO_WIDGET);
    S->Model->Widgets[0].ModuleId = Saved;
    /* the last module widget is seen too */
    for (W = 0; W < S->Model->WidgetCount && StreamWidgetIsModule(&S->Model->Widgets[W]); W++)
        ;
    W--;
    CHECK(StreamWidgetIsModule(&S->Model->Widgets[W]));
    Saved = S->Model->Widgets[W].ModuleId;
    S->Model->Widgets[W].ModuleId = 0xFFFFFFFF;
    CHECK_EQ(IntcDspStreamsModuleRange(S->Model), W);
    S->Model->Widgets[W].ModuleId = Saved;
    /* the loopback's scheduler carries no module: its id is not the manager's to count */
    CHECK(Bare != NULL);
    if (Bare != NULL)
    {
        Saved = Bare->ModuleId;
        S->Model->Widgets[Bare - S->Model->Widgets].ModuleId = 0xFFFFFFFF;
        CHECK_EQ(IntcDspStreamsModuleRange(S->Model), SOFTPLG_NO_WIDGET);
        S->Model->Widgets[Bare - S->Model->Widgets].ModuleId = Saved;
    }
    StreamStop(S);
    free(S);
}

/*
 * (i17) The recovery's state machine (E1): a fault leaves the manager in
 * need of a boot, every stream of the boot Released and marked Lost, each
 * pin told once, the release counted; the power-down's release does the
 * same without a fault. An open with no Boot operation is refused as
 * before and takes nothing. A Boot operation that boots the model again on
 * a fresh DSP and starts the manager on it comes once, under the open's
 * lock, and the open goes through: the manager ready, nothing faulted, no
 * boot needed, the word of the new generation and the old ones stale, a
 * line saying the open recovered after the boot's time. A Boot that stops
 * before the manager is started leaves the open refused with
 * STATUS_DEVICE_NOT_READY, the pool untouched and the need kept, a line
 * saying so; the next open boots again.
 */
static
void
CheckStreamRecovery(
    void)
{
    STREAM_HARNESS* S = (STREAM_HARNESS*)calloc(1, sizeof(*S));
    HANDLE Render = NULL, Capture = NULL, Fresh = NULL;
    PINTCDSP_STREAM Stream;
    UINT32 Pcm, Sx, Requests;

    CHECK(S != NULL);
    if (S == NULL)
        return;
    StreamStart(S, NULL);
    Pcm = StreamPcm(S);
    CHECK(!S->Streams.NeedsBoot);
    CHECK_EQ(IntcDspStreamOpen(&S->Streams, Pcm, FALSE, 48000, 2, 32, 32, &Render), STATUS_SUCCESS);
    CHECK_EQ(IntcDspStreamOpen(&S->Streams, Pcm, TRUE, 48000, 2, 32, 32, &Capture), STATUS_SUCCESS);
    CHECK_EQ(IntcDspStreamAllocate(&S->Streams, Render, IPC4_STREAM_CASE_BUFFER_BYTES, 2), STATUS_SUCCESS);
    CHECK_EQ(IntcDspStreamAllocate(&S->Streams, Capture, IPC4_STREAM_CASE_BUFFER_BYTES, 2), STATUS_SUCCESS);
    StreamPinOpen(S, Render);
    StreamPinOpen(S, Capture);
    /* the fault: the render's run is answered with a firmware error on the pipelines' state, which faults the link */
    S->H.Dsp->Faults.IpcStatusForType = SofIpc4GlbSetPipelineState;
    S->H.Dsp->Faults.IpcStatusValue = SofIpc4InvalidResourceState;
    CHECK_EQ(IntcDspStreamRun(&S->Streams, Render), STATUS_DEVICE_PROTOCOL_ERROR);
    CHECK_EQ(S->Streams.Faulted, TRUE);
    CHECK_EQ(S->Streams.NeedsBoot, TRUE);
    CHECK_EQ(S->Faults, 1);
    /* every stream of the boot is Released and Lost: the fault's line and the pin's for each, then the count */
    for (Sx = 0; Sx < INTCDSP_STREAMS_MAX; Sx++)
    {
        CHECK(S->Streams.Stream[Sx].State == IntcDspStreamStateFree || S->Streams.Stream[Sx].State == IntcDspStreamStateReleased);
        CHECK_EQ(S->Streams.Stream[Sx].State == IntcDspStreamStateReleased, S->Streams.Stream[Sx].Lost);
    }
    StreamPinHeld(S, 2, 0);
    /* the pins' frees return their held buffers, a line each saying how many are left; their closes are answered */
    StreamPinClose(S, Render);
    StreamPinClose(S, Capture);
    CHECK(StreamLogCount(S, "its pin's free returned the buffer (") == 2);
    CHECK(StreamLogCount(S, "; 1 held left\n") == 1);
    CHECK(StreamLogCount(S, "; 0 held left\n") == 1);
    StreamPinHeld(S, 0, 2);
    StreamPinCloseAgainRemoved(S, Render);
    CHECK_EQ(IntcDspStreamClose(&S->Streams, Render), STATUS_SUCCESS);
    CHECK_EQ(IntcDspStreamClose(&S->Streams, Capture), STATUS_SUCCESS);
    for (Sx = 0; Sx < INTCDSP_STREAMS_MAX; Sx++)
        CHECK_EQ(S->Streams.Stream[Sx].State, IntcDspStreamStateFree);
    /* an open with no Boot operation: refused as before, nothing taken, the need kept */
    CHECK_EQ(IntcDspStreamOpen(&S->Streams, Pcm, FALSE, 48000, 2, 32, 32, &Fresh), STATUS_DEVICE_NOT_READY);
    CHECK(Fresh == NULL);
    CHECK(S->Streams.NeedsBoot);
    CHECK_EQ(S->Streams.Path[Pcm].Open, 0);
    /* the Boot operation: one boot on a fresh DSP, under the open's lock; the open goes through */
    S->Ops.Boot = StreamBootAtOpen;
    CHECK_EQ(IntcDspStreamOpen(&S->Streams, Pcm, FALSE, 48000, 2, 32, 32, &Fresh), STATUS_SUCCESS);
    CHECK(Fresh != NULL);
    CHECK_EQ(S->Boots, 1);
    CHECK_EQ(S->BootDepth, 1);
    CHECK(S->Streams.Ready && !S->Streams.Faulted && !S->Streams.NeedsBoot);
    CHECK_EQ(S->Streams.Generation, 2);
    CHECK_EQ(S->Streams.Faults, 0);
    CHECK_EQ(S->H.Ipc.Link, SofIpcLinkReady);
    Stream = IntcDspStreamOf(&S->Streams, Fresh);
    CHECK(Stream != NULL && Stream == &S->Streams.Stream[0] && !Stream->Lost);
    CHECK_EQ((UINT_PTR)Fresh >> INTCDSP_STREAM_INDEX_BITS, 2);
    /* the words of the boot before are stale, the new one is not, and a word without an object's index is no handle, stale or not */
    CHECK(IntcDspStreamStale(&S->Streams, Render) && IntcDspStreamStale(&S->Streams, Capture));
    CHECK(!IntcDspStreamStale(&S->Streams, Fresh));
    CHECK(!IntcDspStreamStale(&S->Streams, NULL));
    CHECK(StreamLogCount(S, "recovered: open ok after ") == 1);
    CHECK(StreamLogCount(S, "boot at stream open: ok") == 0);
    /* a notification the firmware posted beside a reply is said with its type (INFO_NEEDED 2) */
    S->H.Dsp->Faults.NotifyBesideReply = TRUE;
    CHECK_EQ(IntcDspStreamAllocate(&S->Streams, Fresh, IPC4_STREAM_CASE_BUFFER_BYTES, 2), STATUS_SUCCESS);
    S->H.Dsp->Faults.NotifyBesideReply = FALSE;
    CHECK(StreamLogCount(S, "ipc: a notification of type 0x") >= 1);
    /* the stream of the new boot runs its course; nothing is left of the boot before */
    CHECK_EQ(IntcDspStreamRun(&S->Streams, Fresh), STATUS_SUCCESS);
    CHECK_EQ(IntcDspStreamPause(&S->Streams, Fresh), STATUS_SUCCESS);
    CHECK_EQ(IntcDspStreamFree(&S->Streams, Fresh), STATUS_SUCCESS);
    CHECK_EQ(IntcDspStreamClose(&S->Streams, Fresh), STATUS_SUCCESS);
    for (Sx = 0; Sx < INTCDSP_STREAMS_MAX; Sx++)
        CHECK_EQ(S->Streams.Stream[Sx].State, IntcDspStreamStateFree);
    CHECK_EQ(S->Streams.Path[Pcm].Open, 0);
    CHECK_EQ(S->Streams.Path[Pcm].Held, 0);
    CHECK(S->Streams.Path[Pcm].Built == FALSE);
    /* the power-down's release: the need without a fault, the stream let go Lost and its pin told, no fault line */
    CHECK_EQ(IntcDspStreamOpen(&S->Streams, Pcm, FALSE, 48000, 2, 32, 32, &Render), STATUS_SUCCESS);
    CHECK_EQ(IntcDspStreamAllocate(&S->Streams, Render, IPC4_STREAM_CASE_BUFFER_BYTES, 2), STATUS_SUCCESS);
    StreamPinOpen(S, Render);
    Sx = (UINT32)((UINT_PTR)Render & INTCDSP_STREAM_INDEX_MASK) - 1;
    Requests = S->H.Dsp->TransportRequests;
    IntcDspStreamsRelease(&S->Streams);
    CHECK(!S->Streams.Ready && S->Streams.NeedsBoot && !S->Streams.Faulted);
    CHECK_EQ(S->Faults, 1);
    CHECK_EQ(S->H.Dsp->TransportRequests, Requests);
    CHECK(IntcDspStreamOf(&S->Streams, Render) == NULL);
    CHECK(S->Streams.Stream[Sx].State == IntcDspStreamStateReleased && S->Streams.Stream[Sx].Lost);
    CHECK(StreamLogCount(S, "fault during stream ") == 2);
    CHECK(StreamLogCount(S, ": the pin is told: device removed\n") == 3);
    CHECK(StreamLogCount(S, "release: 1 streams let go, 1 buffers held for their pins, 1 tags back\n") == 1);
    /* the fake's counts are the harness's whole life, three boots here: the held table and the frees under a pin are asked directly */
    CHECK_EQ(IntcDspStreamsHeld(&S->Streams), 1);
    CHECK_EQ(FakeBusCount(&S->Bus, FAKE_BUS_EVENT_BUFFER_FREE_UNDER_PIN), 0);
    /* a Boot that stops before the manager is started: refused, the pool untouched, the need kept */
    S->BootStops = TRUE;
    CHECK_EQ(IntcDspStreamOpen(&S->Streams, Pcm, FALSE, 48000, 2, 32, 32, &Fresh), STATUS_DEVICE_NOT_READY);
    CHECK(Fresh == NULL);
    CHECK_EQ(S->Boots, 2);
    CHECK(!S->Streams.Ready && S->Streams.NeedsBoot && !S->Streams.Faulted);
    CHECK_EQ(S->Streams.Path[Pcm].Open, 0);
    CHECK_EQ(S->Streams.Generation, 3);
    for (Sx = 0; Sx < INTCDSP_STREAMS_MAX; Sx++)
        CHECK(S->Streams.Stream[Sx].State == IntcDspStreamStateFree || S->Streams.Stream[Sx].State == IntcDspStreamStateReleased);
    CHECK(StreamLogCount(S, "boot at stream open failed: ") == 1);
    /* the next open boots again and goes through; the pin of the power-down frees its held buffer under its stale word */
    S->BootStops = FALSE;
    CHECK_EQ(IntcDspStreamOpen(&S->Streams, Pcm, FALSE, 48000, 2, 32, 32, &Fresh), STATUS_SUCCESS);
    CHECK_EQ(S->Boots, 3);
    CHECK(StreamLogCount(S, "recovered: open ok after ") == 2);
    CHECK(IntcDspStreamStale(&S->Streams, Render));
    StreamPinClose(S, Render);
    StreamPinCloseAgainRemoved(S, Render);
    CHECK_EQ(IntcDspStreamsHeld(&S->Streams), 0);
    CHECK_EQ(FakeBusCount(&S->Bus, FAKE_BUS_EVENT_BUFFER_FREE_UNDER_PIN), 0);
    CHECK_EQ(IntcDspStreamClose(&S->Streams, Render), STATUS_SUCCESS);
    CHECK_EQ(IntcDspStreamClose(&S->Streams, Fresh), STATUS_SUCCESS);
    for (Sx = 0; Sx < INTCDSP_STREAMS_MAX; Sx++)
        CHECK_EQ(S->Streams.Stream[Sx].State, IntcDspStreamStateFree);
    CHECK_EQ(S->Streams.HeldCount, 0);
    CHECK_EQ(FakeBusLockDepth, 0);
    StreamStop(S);
    free(S);
}

/*
 * (i18) Two opens around a boot (E1): after a fault the first open boots
 * and the second, in the wake of the first inside the same lock, finds the
 * manager ready: the operation came once for both, the two words are of
 * the boot's generation and both streams run their course. A Boot that
 * stops refuses the first open and the second alike, the operation coming
 * for each and nothing taken or sent either time; the boot after those two
 * lets the next open through, two stopped boots in a row being the round's
 * stop rule and not coded around.
 */
static
void
CheckStreamBootAtOpen(
    void)
{
    STREAM_HARNESS* S = (STREAM_HARNESS*)calloc(1, sizeof(*S));
    HANDLE Render = NULL, Again = NULL, First = NULL, Second = NULL;
    UINT32 Pcm, Sx, Requests;

    CHECK(S != NULL);
    if (S == NULL)
        return;
    StreamStart(S, NULL);
    Pcm = StreamPcm(S);
    S->Ops.Boot = StreamBootAtOpen;
    CHECK_EQ(IntcDspStreamOpen(&S->Streams, Pcm, FALSE, 48000, 2, 32, 32, &Render), STATUS_SUCCESS);
    CHECK_EQ(IntcDspStreamAllocate(&S->Streams, Render, IPC4_STREAM_CASE_BUFFER_BYTES, 2), STATUS_SUCCESS);
    StreamPinOpen(S, Render);
    /* the fault: the run's set of the pipelines' state is answered with a firmware error, which faults the link */
    S->H.Dsp->Faults.IpcStatusForType = SofIpc4GlbSetPipelineState;
    S->H.Dsp->Faults.IpcStatusValue = SofIpc4InvalidResourceState;
    CHECK_EQ(IntcDspStreamRun(&S->Streams, Render), STATUS_DEVICE_PROTOCOL_ERROR);
    CHECK(S->Streams.Faulted && S->Streams.NeedsBoot);
    /* the first open boots, the second finds the manager ready: one operation for both, nothing sent by either */
    CHECK_EQ(IntcDspStreamOpen(&S->Streams, Pcm, FALSE, 48000, 2, 32, 32, &First), STATUS_SUCCESS);
    CHECK_EQ(IntcDspStreamOpen(&S->Streams, Pcm, TRUE, 48000, 2, 32, 32, &Second), STATUS_SUCCESS);
    CHECK(First != NULL && Second != NULL && First != Second);
    CHECK_EQ(S->Boots, 1);
    CHECK_EQ(S->BootDepth, 1);
    CHECK(S->Streams.Ready && !S->Streams.Faulted && !S->Streams.NeedsBoot);
    CHECK_EQ((UINT_PTR)First >> INTCDSP_STREAM_INDEX_BITS, 2);
    CHECK_EQ((UINT_PTR)Second >> INTCDSP_STREAM_INDEX_BITS, 2);
    CHECK_EQ(S->H.Dsp->TransportRequests, 0);
    CHECK(StreamLogCount(S, "recovered: open ok after ") == 1);
    /* both run their course on the booted model */
    CHECK_EQ(IntcDspStreamAllocate(&S->Streams, First, IPC4_STREAM_CASE_BUFFER_BYTES, 2), STATUS_SUCCESS);
    CHECK_EQ(IntcDspStreamRun(&S->Streams, First), STATUS_SUCCESS);
    CHECK_EQ(IntcDspStreamAllocate(&S->Streams, Second, IPC4_STREAM_CASE_BUFFER_BYTES, 2), STATUS_SUCCESS);
    CHECK_EQ(IntcDspStreamRun(&S->Streams, Second), STATUS_SUCCESS);
    CHECK_EQ(IntcDspStreamPause(&S->Streams, First), STATUS_SUCCESS);
    CHECK_EQ(IntcDspStreamPause(&S->Streams, Second), STATUS_SUCCESS);
    CHECK_EQ(IntcDspStreamFree(&S->Streams, First), STATUS_SUCCESS);
    CHECK_EQ(IntcDspStreamFree(&S->Streams, Second), STATUS_SUCCESS);
    CHECK_EQ(IntcDspStreamClose(&S->Streams, First), STATUS_SUCCESS);
    CHECK_EQ(IntcDspStreamClose(&S->Streams, Second), STATUS_SUCCESS);
    for (Sx = 0; Sx < INTCDSP_STREAMS_MAX; Sx++)
        CHECK_EQ(S->Streams.Stream[Sx].State, IntcDspStreamStateFree);
    /* a fault again, then a Boot that stops: both opens are refused, the operation for each, nothing taken or sent */
    CHECK_EQ(IntcDspStreamOpen(&S->Streams, Pcm, FALSE, 48000, 2, 32, 32, &Again), STATUS_SUCCESS);
    CHECK_EQ(IntcDspStreamAllocate(&S->Streams, Again, IPC4_STREAM_CASE_BUFFER_BYTES, 2), STATUS_SUCCESS);
    StreamPinOpen(S, Again);
    S->H.Dsp->Faults.IpcStatusForType = SofIpc4GlbSetPipelineState;
    S->H.Dsp->Faults.IpcStatusValue = SofIpc4InvalidResourceState;
    CHECK_EQ(IntcDspStreamRun(&S->Streams, Again), STATUS_DEVICE_PROTOCOL_ERROR);
    Requests = S->H.Dsp->TransportRequests;
    S->BootStops = TRUE;
    First = NULL;
    Second = NULL;
    CHECK_EQ(IntcDspStreamOpen(&S->Streams, Pcm, FALSE, 48000, 2, 32, 32, &First), STATUS_DEVICE_NOT_READY);
    CHECK(First == NULL);
    CHECK_EQ(IntcDspStreamOpen(&S->Streams, Pcm, TRUE, 48000, 2, 32, 32, &Second), STATUS_DEVICE_NOT_READY);
    CHECK(Second == NULL);
    CHECK_EQ(S->Boots, 3);
    CHECK(S->Streams.Faulted && S->Streams.NeedsBoot);
    CHECK_EQ(S->Streams.Path[Pcm].Open, 0);
    CHECK_EQ(S->H.Dsp->TransportRequests, Requests);
    for (Sx = 0; Sx < INTCDSP_STREAMS_MAX; Sx++)
        CHECK(S->Streams.Stream[Sx].State == IntcDspStreamStateFree || S->Streams.Stream[Sx].State == IntcDspStreamStateReleased);
    CHECK(StreamLogCount(S, "boot at stream open failed: ") == 2);
    CHECK(StreamLogCount(S, "recovered: open ok after ") == 1);
    /* the boot after the two that stopped lets the next open through */
    S->BootStops = FALSE;
    CHECK_EQ(IntcDspStreamOpen(&S->Streams, Pcm, FALSE, 48000, 2, 32, 32, &First), STATUS_SUCCESS);
    CHECK_EQ(S->Boots, 4);
    CHECK(S->Streams.Ready && !S->Streams.Faulted && !S->Streams.NeedsBoot);
    CHECK(StreamLogCount(S, "recovered: open ok after ") == 2);
    /* the pins' closes free the held buffers under their stale words; the stale closes and the new one are answered */
    StreamPinClose(S, Render);
    StreamPinClose(S, Again);
    CHECK_EQ(IntcDspStreamClose(&S->Streams, Render), STATUS_SUCCESS);
    CHECK_EQ(IntcDspStreamClose(&S->Streams, Again), STATUS_SUCCESS);
    CHECK_EQ(IntcDspStreamClose(&S->Streams, First), STATUS_SUCCESS);
    CHECK_EQ(IntcDspStreamsHeld(&S->Streams), 0);
    for (Sx = 0; Sx < INTCDSP_STREAMS_MAX; Sx++)
        CHECK_EQ(S->Streams.Stream[Sx].State, IntcDspStreamStateFree);
    CHECK_EQ(FakeBusLockDepth, 0);
    StreamStop(S);
    free(S);
}

/*
 * (i19) The pin told (E1, D4): a stream the device let go, by a fault or
 * by the power-down, and a handle of an older generation are answered
 * STATUS_DEVICE_REMOVED at Run, Pause, Allocate and the two position
 * registers (the position property's path: dsppcm's GetPosition reads the
 * register getter), before anything of the manager is asked and with
 * nothing sent; the free returns the held buffer
 * once, a line saying so, and is answered the device removed after; the
 * close is answered STATUS_SUCCESS. A stream Released by its own free (a
 * teardown the drain finished while its pin lives) is not Lost and answers
 * as before: a misuse, not a removal. (i13a)'s counts: nothing freed while
 * a pin lives, the held buffer once at its free.
 */
static
void
CheckStreamPinTold(
    void)
{
    STREAM_HARNESS* S;
    HANDLE Render = NULL, Capture = NULL, Fresh = NULL, Told;
    PINTCDSP_STREAM Stream;
    PULONG Register;
    UINT32 Pcm, Requests, D;

    /* lost by the fault: the table on each stream, nothing sent */
    S = (STREAM_HARNESS*)calloc(1, sizeof(*S));
    CHECK(S != NULL);
    if (S == NULL)
        return;
    StreamStart(S, NULL);
    Pcm = StreamPcm(S);
    CHECK_EQ(IntcDspStreamOpen(&S->Streams, Pcm, FALSE, 48000, 2, 32, 32, &Render), STATUS_SUCCESS);
    CHECK_EQ(IntcDspStreamOpen(&S->Streams, Pcm, TRUE, 48000, 2, 32, 32, &Capture), STATUS_SUCCESS);
    CHECK_EQ(IntcDspStreamAllocate(&S->Streams, Render, IPC4_STREAM_CASE_BUFFER_BYTES, 2), STATUS_SUCCESS);
    CHECK_EQ(IntcDspStreamAllocate(&S->Streams, Capture, IPC4_STREAM_CASE_BUFFER_BYTES, 2), STATUS_SUCCESS);
    StreamPinOpen(S, Render);
    StreamPinOpen(S, Capture);
    CHECK_EQ(IntcDspStreamRun(&S->Streams, Capture), STATUS_SUCCESS);
    /* the fault: the capture's pause is answered with a firmware error on the pipelines' state, which faults the link */
    S->H.Dsp->Faults.IpcStatusForType = SofIpc4GlbSetPipelineState;
    S->H.Dsp->Faults.IpcStatusValue = SofIpc4InvalidResourceState;
    CHECK_EQ(IntcDspStreamPause(&S->Streams, Capture), STATUS_DEVICE_PROTOCOL_ERROR);
    Requests = S->H.Dsp->TransportRequests;
    for (D = 0; D < 2; D++)
    {
        Told = D == 0 ? Render : Capture;
        CHECK_EQ(IntcDspStreamRun(&S->Streams, Told), STATUS_DEVICE_REMOVED);
        CHECK_EQ(IntcDspStreamPause(&S->Streams, Told), STATUS_DEVICE_REMOVED);
        CHECK_EQ(IntcDspStreamAllocate(&S->Streams, Told, IPC4_STREAM_CASE_BUFFER_BYTES, 2), STATUS_DEVICE_REMOVED);
        Register = (PULONG)(UINT_PTR)1;
        CHECK_EQ(IntcDspStreamPositionRegister(&S->Streams, Told, &Register), STATUS_DEVICE_REMOVED);
        CHECK(Register == NULL);
        Register = (PULONG)(UINT_PTR)1;
        CHECK_EQ(IntcDspStreamLinkPositionRegister(&S->Streams, Told, &Register), STATUS_DEVICE_REMOVED);
        CHECK(Register == NULL);
        CHECK_EQ(IntcDspStreamPosition(&S->Streams, Told), 0);
    }
    CHECK_EQ(S->H.Dsp->TransportRequests, Requests);
    StreamPinHeld(S, 2, 0);
    /* the frees return the held buffers once, then the device removed; the closes are answered */
    StreamPinClose(S, Render);
    StreamPinCloseAgainRemoved(S, Render);
    StreamPinClose(S, Capture);
    StreamPinCloseAgainRemoved(S, Capture);
    CHECK(StreamLogCount(S, "its pin's free returned the buffer (") == 2);
    StreamPinHeld(S, 0, 2);
    CHECK_EQ(IntcDspStreamClose(&S->Streams, Render), STATUS_SUCCESS);
    CHECK_EQ(IntcDspStreamClose(&S->Streams, Capture), STATUS_SUCCESS);
    StreamStop(S);
    free(S);

    /* lost by the power-down: the same answers before the next boot */
    S = (STREAM_HARNESS*)calloc(1, sizeof(*S));
    CHECK(S != NULL);
    if (S == NULL)
        return;
    StreamStart(S, NULL);
    Pcm = StreamPcm(S);
    CHECK_EQ(IntcDspStreamOpen(&S->Streams, Pcm, FALSE, 48000, 2, 32, 32, &Render), STATUS_SUCCESS);
    CHECK_EQ(IntcDspStreamAllocate(&S->Streams, Render, IPC4_STREAM_CASE_BUFFER_BYTES, 2), STATUS_SUCCESS);
    StreamPinOpen(S, Render);
    CHECK_EQ(IntcDspStreamRun(&S->Streams, Render), STATUS_SUCCESS);
    IntcDspStreamsRelease(&S->Streams);
    CHECK_EQ(IntcDspStreamRun(&S->Streams, Render), STATUS_DEVICE_REMOVED);
    CHECK_EQ(IntcDspStreamPause(&S->Streams, Render), STATUS_DEVICE_REMOVED);
    CHECK_EQ(IntcDspStreamAllocate(&S->Streams, Render, IPC4_STREAM_CASE_BUFFER_BYTES, 2), STATUS_DEVICE_REMOVED);
    /* the position property's path: the register getter (dsppcm's GetPosition through GetPositionRegister) */
    Register = (PULONG)(UINT_PTR)1;
    CHECK_EQ(IntcDspStreamPositionRegister(&S->Streams, Render, &Register), STATUS_DEVICE_REMOVED);
    CHECK(Register == NULL);
    StreamPinClose(S, Render);
    StreamPinCloseAgainRemoved(S, Render);
    CHECK(StreamLogCount(S, "its pin's free returned the buffer (") == 1);
    CHECK(StreamLogCount(S, "; 0 held left\n") == 1);
    CHECK_EQ(IntcDspStreamClose(&S->Streams, Render), STATUS_SUCCESS);
    CHECK_EQ(FakeBusMdlCount(&S->Bus), 0);
    StreamStop(S);
    free(S);

    /* a handle of an older generation after the D0 entry's boot: the same answers, the new stream untouched */
    S = (STREAM_HARNESS*)calloc(1, sizeof(*S));
    CHECK(S != NULL);
    if (S == NULL)
        return;
    StreamStart(S, NULL);
    Pcm = StreamPcm(S);
    CHECK_EQ(IntcDspStreamOpen(&S->Streams, Pcm, FALSE, 48000, 2, 32, 32, &Render), STATUS_SUCCESS);
    StreamBoot(S, NULL);
    IntcDspStreamsInit(&S->Streams, S->State, IntcDspStreamsBytes(S->Model), S->Model, &S->H.Ipc, &S->Bus.Interface, &S->Ops);
    CHECK_EQ(IntcDspStreamOpen(&S->Streams, Pcm, FALSE, 48000, 2, 32, 32, &Fresh), STATUS_SUCCESS);
    CHECK_EQ(IntcDspStreamAllocate(&S->Streams, Fresh, IPC4_STREAM_CASE_BUFFER_BYTES, 2), STATUS_SUCCESS);
    Requests = S->H.Dsp->TransportRequests;
    CHECK(IntcDspStreamStale(&S->Streams, Render));
    CHECK_EQ(IntcDspStreamRun(&S->Streams, Render), STATUS_DEVICE_REMOVED);
    CHECK_EQ(IntcDspStreamPause(&S->Streams, Render), STATUS_DEVICE_REMOVED);
    CHECK_EQ(IntcDspStreamAllocate(&S->Streams, Render, IPC4_STREAM_CASE_BUFFER_BYTES, 2), STATUS_DEVICE_REMOVED);
    Register = (PULONG)(UINT_PTR)1;
    CHECK_EQ(IntcDspStreamPositionRegister(&S->Streams, Render, &Register), STATUS_DEVICE_REMOVED);
    CHECK(Register == NULL);
    Register = (PULONG)(UINT_PTR)1;
    CHECK_EQ(IntcDspStreamLinkPositionRegister(&S->Streams, Render, &Register), STATUS_DEVICE_REMOVED);
    CHECK(Register == NULL);
    CHECK_EQ(IntcDspStreamFree(&S->Streams, Render), STATUS_DEVICE_REMOVED);
    /* the event entries are not of the design's table: they refuse the word as before */
    CHECK_EQ(IntcDspStreamRegisterEvent(&S->Streams, Render, &S->Event), STATUS_INVALID_HANDLE);
    CHECK_EQ(IntcDspStreamClose(&S->Streams, Render), STATUS_SUCCESS);
    CHECK_EQ(S->H.Dsp->TransportRequests, Requests);
    Stream = IntcDspStreamOf(&S->Streams, Fresh);
    CHECK(Stream != NULL && Stream->State == IntcDspStreamStatePrepared && Stream->Mdl != NULL);
    CHECK(StreamLogCount(S, ": the pin is told: device removed\n") == 0);
    CHECK_EQ(IntcDspStreamFree(&S->Streams, Fresh), STATUS_SUCCESS);
    CHECK_EQ(IntcDspStreamClose(&S->Streams, Fresh), STATUS_SUCCESS);
    StreamStop(S);
    free(S);

    /* released by its own free: the drain finishes the pending teardown while its pin lives; a misuse is answered as before, not a removal */
    S = (STREAM_HARNESS*)calloc(1, sizeof(*S));
    CHECK(S != NULL);
    if (S == NULL)
        return;
    StreamStart(S, NULL);
    Pcm = StreamPcm(S);
    CHECK_EQ(IntcDspStreamOpen(&S->Streams, Pcm, FALSE, 48000, 2, 32, 32, &Render), STATUS_SUCCESS);
    CHECK_EQ(IntcDspStreamOpen(&S->Streams, Pcm, TRUE, 48000, 2, 32, 32, &Capture), STATUS_SUCCESS);
    CHECK_EQ(IntcDspStreamAllocate(&S->Streams, Render, IPC4_STREAM_CASE_BUFFER_BYTES, 2), STATUS_SUCCESS);
    CHECK_EQ(IntcDspStreamAllocate(&S->Streams, Capture, IPC4_STREAM_CASE_BUFFER_BYTES, 2), STATUS_SUCCESS);
    StreamPinOpen(S, Render);
    StreamPinOpen(S, Capture);
    S->H.LockBusyFromRequest = S->H.LockAttempts + 1;
    CHECK_EQ(IntcDspStreamFree(&S->Streams, Capture), STATUS_SUCCESS);
    StreamPinForget(S, Capture);
    S->H.LockBusyFromRequest = 0;
    /* the render's run drains the capture first: its teardown done, the stream left for its pin's close */
    CHECK_EQ(IntcDspStreamRun(&S->Streams, Render), STATUS_SUCCESS);
    Stream = IntcDspStreamOf(&S->Streams, Capture);
    CHECK(Stream != NULL && Stream->State == IntcDspStreamStateReleased && !Stream->Lost);
    CHECK_EQ(IntcDspStreamPosition(&S->Streams, Capture), 0);
    CHECK_EQ(IntcDspStreamRun(&S->Streams, Capture), STATUS_INVALID_DEVICE_REQUEST);
    CHECK_EQ(IntcDspStreamPause(&S->Streams, Capture), STATUS_INVALID_DEVICE_REQUEST);
    CHECK_EQ(IntcDspStreamAllocate(&S->Streams, Capture, IPC4_STREAM_CASE_BUFFER_BYTES, 2), STATUS_INVALID_DEVICE_REQUEST);
    CHECK_EQ(IntcDspStreamFree(&S->Streams, Capture), STATUS_INVALID_DEVICE_REQUEST);
    CHECK_EQ(IntcDspStreamClose(&S->Streams, Capture), STATUS_SUCCESS);
    CHECK(StreamLogCount(S, ": the pin is told: device removed\n") == 0);
    CHECK_EQ(S->Faults, 0);
    CHECK_EQ(IntcDspStreamPause(&S->Streams, Render), STATUS_SUCCESS);
    StreamPinClose(S, Render);
    CHECK_EQ(IntcDspStreamClose(&S->Streams, Render), STATUS_SUCCESS);
    StreamQuiescent(S, Pcm);
    StreamStop(S);
    free(S);
}

/*
 * (i20) A fault at each step of a stream's life (E1, §4): the run's set
 * of the pipelines' state, the free's set of the copier, and the drain of
 * a pending teardown (i16) by another stream's run (i13's forced release).
 * Each faults the link, and the release's lines come in the design's
 * order: the fault's line, its kind the firmware's status, and the pin's
 * for each stream let go, then the count, the streams let go, the buffers
 * held for their pins at the release (a pending teardown's is the
 * manager's already, returned at once, and not counted) and the tags back;
 * no stream is left but Released, each told once, nothing freed under a
 * pin.
 */
static
void
CheckStreamFaultAtEachStep(
    void)
{
    STREAM_HARNESS* S;
    HANDLE Render = NULL, Capture = NULL;
    PINTCDSP_STREAM Stream;
    NTSTATUS Status;
    UINT32 Tags[2], Pcm, Sx, Step;
    const char* At;

    for (Step = 0; Step < 3; Step++)
    {
        S = (STREAM_HARNESS*)calloc(1, sizeof(*S));
        CHECK(S != NULL);
        if (S == NULL)
            return;
        StreamStart(S, NULL);
        Pcm = StreamPcm(S);
        CHECK_EQ(IntcDspStreamOpen(&S->Streams, Pcm, FALSE, 48000, 2, 32, 32, &Render), STATUS_SUCCESS);
        CHECK_EQ(IntcDspStreamOpen(&S->Streams, Pcm, TRUE, 48000, 2, 32, 32, &Capture), STATUS_SUCCESS);
        CHECK_EQ(IntcDspStreamAllocate(&S->Streams, Render, IPC4_STREAM_CASE_BUFFER_BYTES, 2), STATUS_SUCCESS);
        CHECK_EQ(IntcDspStreamAllocate(&S->Streams, Capture, IPC4_STREAM_CASE_BUFFER_BYTES, 2), STATUS_SUCCESS);
        StreamPinOpen(S, Render);
        StreamPinOpen(S, Capture);
        Stream = IntcDspStreamOf(&S->Streams, Render);
        CHECK(Stream != NULL);
        if (Stream != NULL)
            Tags[0] = Stream->Tag;
        Stream = IntcDspStreamOf(&S->Streams, Capture);
        CHECK(Stream != NULL);
        if (Stream != NULL)
            Tags[1] = Stream->Tag;
        if (Step == 2)
        {
            /* the capture's free meets a busy transport: the teardown pending, its buffer the manager's, its pin having forgotten it */
            S->H.LockBusyFromRequest = S->H.LockAttempts + 1;
            CHECK_EQ(IntcDspStreamFree(&S->Streams, Capture), STATUS_SUCCESS);
            StreamPinForget(S, Capture);
            S->H.LockBusyFromRequest = 0;
        }
        S->H.Dsp->Faults.IpcStatusForType = Step == 0 ? SofIpc4GlbSetPipelineState : SofIpc4ModLargeConfigSet;
        S->H.Dsp->Faults.IpcStatusValue = SofIpc4InvalidResourceState;
        Status = Step == 1 ? IntcDspStreamFree(&S->Streams, Render) : IntcDspStreamRun(&S->Streams, Render);
        /* at the drain's fault the release lets the render go before its own run: the pin is told (E1, D4) */
        CHECK_EQ(Status, Step == 2 ? STATUS_DEVICE_REMOVED : STATUS_DEVICE_PROTOCOL_ERROR);
        CHECK(S->Streams.Faulted && S->Streams.NeedsBoot);
        CHECK_EQ(S->Faults, 1);
        /* the lines in the design's order, from the fault's first */
        At = strstr(S->Log, "fault during stream ");
        CHECK(At != NULL);
        /* two buffers allocated and detached, two tags back, nothing freed under a pin: the count says the buffers held for the pins at the release, both after the run's fault; at the free's the freed one is returned on the way out after it; at the drain's the pending teardown's is the manager's already, returned at once */
        if (At != NULL)
            CHECK(StreamReleaseLines(At, 2, Tags, SofIpc4StatusName(SofIpc4InvalidResourceState), Step == 2 ? 1 : 2, 2) != NULL);
        CHECK(StreamLogCount(S, "fault during stream ") == 2);
        CHECK(StreamLogCount(S, ": the pin is told: device removed\n") == 2);
        StreamPinHeld(S, Step == 0 ? 2 : 1, Step == 0 ? 0 : 1);
        CHECK_EQ(S->Streams.Freed, 2);
        CHECK_EQ(FakeBusCount(&S->Bus, FAKE_BUS_EVENT_FREE), 2);
        /* no stream is left but Released, each Lost */
        for (Sx = 0; Sx < INTCDSP_STREAMS_MAX; Sx++)
        {
            CHECK(S->Streams.Stream[Sx].State == IntcDspStreamStateFree || S->Streams.Stream[Sx].State == IntcDspStreamStateReleased);
            CHECK_EQ(S->Streams.Stream[Sx].State == IntcDspStreamStateReleased, S->Streams.Stream[Sx].Lost);
        }
        /* the pins: a buffer gone on the way out leaves its pin forgetting it, the others free theirs once; the closes are answered */
        if (Step == 1)
            StreamPinForget(S, Render);
        else
            StreamPinClose(S, Render);
        StreamPinCloseAgainRemoved(S, Render);
        if (Step == 2)
            StreamPinCloseAgainRemoved(S, Capture);
        else
            StreamPinClose(S, Capture);
        CHECK_EQ(IntcDspStreamClose(&S->Streams, Render), STATUS_SUCCESS);
        CHECK_EQ(IntcDspStreamClose(&S->Streams, Capture), STATUS_SUCCESS);
        for (Sx = 0; Sx < INTCDSP_STREAMS_MAX; Sx++)
            CHECK_EQ(S->Streams.Stream[Sx].State, IntcDspStreamStateFree);
        CHECK_EQ(IntcDspStreamsHeld(&S->Streams), 0);
        CHECK_EQ(FakeBusMdlCount(&S->Bus), 0);
        CHECK_EQ(FakeBusLockDepth, 0);
        StreamStop(S);
        free(S);
    }
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
    CheckPollTakesLock(H, &Parsed);
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
    CheckStreamGeneration();
    CheckStreamPowerDown();
    CheckStreamLock();
    CheckStreamStuckUndo();
    CheckStreamHeldBuffer();
    CheckStreamPendingTeardown();
    CheckStreamModuleStart();
    CheckStreamModuleRange();
    CheckStreamRecovery();
    CheckStreamBootAtOpen();
    CheckStreamPinTold();
    CheckStreamFaultAtEachStep();

    printf("sofipc: %d checks, %d failures\n", Checks, Failures);
    free(H->Dsp);
    free(H->Ctx);
    free(H->Virtual);
    free(H);
    free(Image);
    return Failures != 0;
}
