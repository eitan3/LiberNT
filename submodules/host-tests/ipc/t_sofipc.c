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
    SofIpc4ModuleInitInstance(&Request.Msg, 1, 0, 1, 1025, 0, 0);
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
    CHECK_EQ(H->Dsp->RequestExtension[0], 1025 | (1 << 16));
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

    printf("sofipc: %d checks, %d failures\n", Checks, Failures);
    free(H->Dsp);
    free(H->Ctx);
    free(H->Virtual);
    free(H);
    free(Image);
    return Failures != 0;
}
