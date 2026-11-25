/*
 * PROJECT:     LiberNT host-native tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     The firmware loader boots a DSP model from the real sof-arl-s.ri and from a synthetic image, times out or reports every fault the model injects, powers down from every state, never trusts a malformed image or descriptor list, and reaches the model only through the harness it was given
 * COPYRIGHT:   Copyright 2026 LiberNT contributors
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "shim.h"

#include "fake_dsp.h"
#include "fwload_cases.h"

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

/* The fixture: intel-signed/sof-arl-s.ri of sof-firmware 2026.09.2-1, LICENCE.Intel beside it */
#define REAL_IMAGE_BYTES    1158164
#define REAL_PAYLOAD_BYTES  0x11A000
#define REAL_IMAGE_FNV1A64  0xFCD4604A4627D3F5ULL
/* the descriptor list hdaudbus gives a stream: 4096 bytes */
#define STREAM_BDL_ENTRIES  256
#define HARNESS_BDL_ENTRIES 1024

#define HARNESS_SIGNATURE   0x4841524E45535321ULL

/* Bits of the operations the harness table carries, in the order of FWLOAD_OPS */
#define HOP_READ32          0
#define HOP_WRITE32         1
#define HOP_READMEMORY      2
#define HOP_NOWUS           3
#define HOP_DELAYUS         4
#define HOP_STREAMSUBMIT    5
#define HOP_STREAMTRIGGER   6
#define HOP_STREAMRELEASE   7
#define HOP_LOG             8
#define HOP_COUNT           9
#define HARNESS_OPS_BOOTED  ((1u << HOP_COUNT) - 1)

typedef struct _HARNESS
{
    UINT64 Signature;
    FAKE_DSP* Dsp;
    FWLOAD_CONTEXT* Ctx;
    UINT8* Virtual;
    SIZE_T Bytes;
    FWLOAD_DMA_SEGMENT Segment[8];
    SIZE_T SegmentCount;
    FWLOAD_BDL_ENTRY Bdl[HARNESS_BDL_ENTRIES];
    SIZE_T BdlCapacity;
    FWLOAD_TIMEOUTS Timeouts;
    UINT32 EntryBoundary;
    UINT32 CancelAfterDelays;
    UINT32 Delays;
    UINT32 StreamTag;
    UINT32 OpsSeen;
    /* a clock that reports the same instant whatever the delays: the model still runs, the loader's budgets cannot expire */
    BOOLEAN ClockStalled;
    CHAR Logs[8][48];
    UINT32 LogCount;
} HARNESS;

/* Calls made without the harness as their context: the detector case expects them, anywhere else they abort the run */
static UINT32 ContextFaults;
static UINT32 ContextFaultsExpected;

static
HARNESS*
HarnessEnter(
    PVOID Context,
    UINT32 Operation,
    const char* Name)
{
    HARNESS* H = (HARNESS*)Context;

    if (Context != NULL && H->Signature == HARNESS_SIGNATURE)
    {
        H->OpsSeen |= 1u << Operation;
        return H;
    }
    ContextFaults++;
    if (ContextFaultsExpected)
        return NULL;
    fprintf(stderr, "t_fwload.c: FAIL: the loader called %s with context %p, not the harness\n", Name, Context);
    abort();
}

static
UINT32
HRead32(
    PVOID Context,
    UINT32 Bar,
    UINT32 Offset)
{
    HARNESS* H = HarnessEnter(Context, HOP_READ32, "Read32");

    return H != NULL ? FakeDspOps()->Read32(H->Dsp, Bar, Offset) : 0xFFFFFFFF;
}

static
VOID
HWrite32(
    PVOID Context,
    UINT32 Bar,
    UINT32 Offset,
    UINT32 Value)
{
    HARNESS* H = HarnessEnter(Context, HOP_WRITE32, "Write32");

    if (H != NULL)
        FakeDspOps()->Write32(H->Dsp, Bar, Offset, Value);
}

static
BOOLEAN
HReadMemory(
    PVOID Context,
    UINT32 Offset,
    UINT8* Buffer,
    SIZE_T Bytes)
{
    HARNESS* H = HarnessEnter(Context, HOP_READMEMORY, "ReadMemory");

    return H != NULL ? FakeDspOps()->ReadMemory(H->Dsp, Offset, Buffer, Bytes) : FALSE;
}

static
UINT64
HNowUs(
    PVOID Context)
{
    HARNESS* H = HarnessEnter(Context, HOP_NOWUS, "NowUs");

    if (H == NULL)
        return 0;
    return H->ClockStalled ? 0 : FakeDspOps()->NowUs(H->Dsp);
}

/* the one injected operation: the clock advance, after which a cancellation can be requested */
static
VOID
HDelayUs(
    PVOID Context,
    UINT32 Microseconds)
{
    HARNESS* H = HarnessEnter(Context, HOP_DELAYUS, "DelayUs");

    if (H == NULL)
        return;
    FakeDspOps()->DelayUs(H->Dsp, Microseconds);
    H->Delays++;
    if (H->CancelAfterDelays != 0 && H->Delays >= H->CancelAfterDelays)
        H->Ctx->Cancel = TRUE;
}

static
FWLOAD_STATUS
HStreamSubmit(
    PVOID Context,
    PFWLOAD_CL_DMA Dma)
{
    HARNESS* H = HarnessEnter(Context, HOP_STREAMSUBMIT, "StreamSubmit");

    return H != NULL ? FakeDspOps()->StreamSubmit(H->Dsp, Dma) : FwloadBadState;
}

static
FWLOAD_STATUS
HStreamTrigger(
    PVOID Context,
    BOOLEAN Start)
{
    HARNESS* H = HarnessEnter(Context, HOP_STREAMTRIGGER, "StreamTrigger");

    return H != NULL ? FakeDspOps()->StreamTrigger(H->Dsp, Start) : FwloadBadState;
}

static
VOID
HStreamRelease(
    PVOID Context)
{
    HARNESS* H = HarnessEnter(Context, HOP_STREAMRELEASE, "StreamRelease");

    if (H != NULL)
        FakeDspOps()->StreamRelease(H->Dsp);
}

static
VOID
HLog(
    PVOID Context,
    const char* Text)
{
    HARNESS* H = HarnessEnter(Context, HOP_LOG, "Log");

    if (H == NULL)
        return;
    if (H->LogCount < 8)
    {
        strncpy(H->Logs[H->LogCount], Text, sizeof(H->Logs[0]) - 1);
        H->Logs[H->LogCount][sizeof(H->Logs[0]) - 1] = 0;
        H->LogCount++;
    }
}

/* The driver's shape: one const table of functions, and the harness reaches them as the context of every call */
static const FWLOAD_OPS HarnessOps =
{
    HRead32,
    HWrite32,
    HReadMemory,
    HNowUs,
    HDelayUs,
    HStreamSubmit,
    HStreamTrigger,
    HStreamRelease,
    HLog
};

static
void
HarnessInit(
    HARNESS* H)
{
    memset(H, 0, sizeof(*H));
    H->Signature = HARNESS_SIGNATURE;
    H->Dsp = (FAKE_DSP*)calloc(1, sizeof(FAKE_DSP));
    H->Ctx = (FWLOAD_CONTEXT*)calloc(1, sizeof(FWLOAD_CONTEXT));
    H->Virtual = (UINT8*)calloc(1, REAL_PAYLOAD_BYTES + 0x10000);
    if (H->Dsp == NULL || H->Ctx == NULL || H->Virtual == NULL)
        exit(2);
    H->Bytes = REAL_PAYLOAD_BYTES;
    H->Segment[0].Address = 0x10000000ULL;
    H->Segment[0].Bytes = H->Bytes;
    H->SegmentCount = 1;
    H->BdlCapacity = STREAM_BDL_ENTRIES;
    FwloadDefaultTimeouts(&H->Timeouts);
}

static
void
HarnessBoot(
    HARNESS* H,
    const SOF_RI_IMAGE* Image,
    const FAKE_DSP_FAULTS* Faults)
{
    FWLOAD_CL_DMA Dma;

    FakeDspInit(H->Dsp, FwloadPlatformArlS(), Faults);
    memset(&Dma, 0, sizeof(Dma));
    Dma.Virtual = H->Virtual;
    Dma.Capacity = H->Bytes;
    Dma.Segment = H->Segment;
    Dma.SegmentCount = H->SegmentCount;
    Dma.Bdl = H->Bdl;
    Dma.BdlCapacity = H->BdlCapacity;
    Dma.BdlBytes = H->BdlCapacity * sizeof(FWLOAD_BDL_ENTRY);
    Dma.EntryBoundary = H->EntryBoundary;
    Dma.StreamTag = (UINT8)H->StreamTag;
    FwloadInitContext(H->Ctx, &HarnessOps, H, FwloadPlatformArlS(), Image, &Dma, &H->Timeouts);
    H->Delays = 0;
    H->LogCount = 0;
    H->OpsSeen = 0;
    H->ClockStalled = FALSE;
}

static
void
HarnessFrees(
    HARNESS* H)
{
    free(H->Dsp);
    free(H->Ctx);
    free(H->Virtual);
}

/* The documented write sequence of a cold boot, as the model recorded it */
static
void
CheckBootWrites(
    const HARNESS* H,
    UINT32 InterruptWrites)
{
    const FAKE_DSP* Dsp = H->Dsp;
    const FWLOAD_PLATFORM* Platform = FwloadPlatformArlS();
    const FAKE_DSP_WRITE* Request;
    const FAKE_DSP_WRITE* CoreSpa;
    const FAKE_DSP_WRITE* Done;
    const FAKE_DSP_WRITE* Ipc;
    UINT32 Index;

    /* GPROCEN on before the subsystem request; the purge request before the core request */
    CHECK(Dsp->WriteCount >= 8);
    CHECK(Dsp->WriteLog[0].Bar == FWLOAD_BAR_PP && Dsp->WriteLog[0].Offset == FWLOAD_PP_OFFSET_PPCTL &&
        (Dsp->WriteLog[0].Value & FWLOAD_PPCTL_GPROCEN));
    CHECK(Dsp->WriteLog[1].Bar == FWLOAD_BAR_DSP && Dsp->WriteLog[1].Offset == Platform->SubsystemControl &&
        (Dsp->WriteLog[1].Value & 0x00010000));
    Request = FakeDspWriteFind(Dsp, FWLOAD_BAR_DSP, Platform->IpcRequest, 0x80000000UL);
    /* the configuration query is the request with the module-message bit */
    Ipc = FakeDspWriteFind(Dsp, FWLOAD_BAR_DSP, Platform->IpcRequest, 0x40000000UL);
    CoreSpa = NULL;
    Done = NULL;
    for (Index = 0; Index < Dsp->WriteCount; Index++)
    {
        const FAKE_DSP_WRITE* Write = &Dsp->WriteLog[Index];

        if (Write->Offset == Platform->CoreControl && (Write->Value & 1) && CoreSpa == NULL)
            CoreSpa = Write;
        if (Write->Offset == Platform->IpcReply && Done == NULL)
            Done = Write;
    }
    CHECK(Request != NULL && CoreSpa != NULL && Request < CoreSpa);
    CHECK(Request != NULL && Request->Value == 0x81004000UL);
    CHECK(CoreSpa != NULL && CoreSpa->Value == 0x02000001UL);
    /* the owner bits go in before the power request, as their own write */
    CHECK(CoreSpa != NULL && Dsp->WriteLog[CoreSpa - Dsp->WriteLog - 1].Offset == Platform->CoreControl &&
        Dsp->WriteLog[CoreSpa - Dsp->WriteLog - 1].Value == 0x02000000UL);
    /* the ROM's acknowledgement is cleared after it was seen */
    CHECK(Done != NULL && Done->Value == 0x80000000UL);
    CHECK_EQ(FakeDspWriteCount(Dsp, FWLOAD_BAR_DSP, FAKE_DSP_INTERRUPT_POINTER), InterruptWrites);
    CHECK_EQ(FakeDspWriteCount(Dsp, FWLOAD_BAR_DSP, Platform->HostIpcInterruptEnable), InterruptWrites);
    CHECK_EQ(FakeDspWriteCount(Dsp, FWLOAD_BAR_DSP, Platform->IpcControl), InterruptWrites);
    /* the firmware configuration query: MOD_CONFIG_GET, module message, module 0 instance 0, parameter FW_CONFIG, busy */
    CHECK(Ipc != NULL && Ipc->Value == 0xC1000000UL);
    CHECK(Dsp->IpcRequestData == 0x00700000UL);
    CHECK(Dsp->IpcRequests == 1 && Dsp->IpcRequestErrors == 0);
}

static
void
CheckDspIsOff(
    const HARNESS* H)
{
    const FAKE_DSP* Dsp = H->Dsp;

    CHECK(FakeDspPowerEventSeen(Dsp, FAKE_POWER_SUBSYSTEM_OFF));
    CHECK(FakeDspPowerEventSeen(Dsp, FAKE_POWER_GPROCEN_OFF));
    CHECK((Dsp->SubsystemControl & 0x01000000) == 0);
    CHECK((Dsp->PpControl & FWLOAD_PPCTL_GPROCEN) == 0);
    CHECK_EQ(Dsp->UnexpectedAccesses, 0);
    CHECK_EQ(Dsp->StreamSubmits, Dsp->StreamReleases);
    CHECK_EQ(Dsp->StreamSubmits, Dsp->StreamStops);
    CHECK(Dsp->StreamStarts <= Dsp->StreamStops);
    /* every message the DSP posted was consumed and acknowledged before the boot gave up */
    CHECK_EQ(FakeDspIpcErrors(Dsp), 0);
    /* the failed context can still be powered down to off */
    CHECK_EQ(FwloadPowerDown(H->Ctx), FwloadOk);
    CHECK_EQ(H->Ctx->State, FwloadStateOff);
    CHECK_EQ(H->Ctx->Status, FwloadOk);
    CHECK_EQ(Dsp->UnexpectedAccesses, 0);
}

static
void
CheckFailedBootIsOff(
    const HARNESS* H,
    FWLOAD_STATUS Status,
    FWLOAD_STATE FailedState)
{
    CHECK_EQ(H->Ctx->Status, Status);
    CHECK_EQ(H->Ctx->State, FwloadStateFailed);
    CHECK_EQ(H->Ctx->FailedState, FailedState);
    CHECK_EQ(H->Ctx->CleanupStatus, FwloadOk);
    CheckDspIsOff(H);
    if (FailedState != FwloadStatePoweringUp)
        CHECK(FakeDspPowerEventSeen(H->Dsp, FAKE_POWER_INTERRUPTS_OFF));
}

static
void
CheckHappyPath(
    HARNESS* H,
    const SOF_RI_IMAGE* Image)
{
    FWLOAD_STATUS Status;
    const FAKE_DSP_WRITE* Request;

    HarnessBoot(H, Image, NULL);
    Status = FwloadBoot(H->Ctx);
    CHECK_EQ(Status, FwloadOk);
    CHECK_EQ(H->Ctx->State, FwloadStateReady);
    CHECK_EQ(H->Ctx->Status, FwloadOk);
    CHECK_EQ(H->Ctx->FailedState, FwloadStateOff);
    CHECK_EQ(H->Ctx->Attempt, 1);
    CHECK_EQ(H->Ctx->Dma.FragmentCount, 1);
    CHECK_EQ(H->Ctx->Dma.Bytes, Image->PayloadBytes);
    CHECK_EQ(H->Ctx->Dma.LastValidIndex, 0);
    CHECK_EQ(H->Ctx->Dma.StreamTag, 1);
    CHECK_EQ(H->Ctx->Dma.StreamFormat, 0x40);
    CHECK_EQ(H->Ctx->Dma.Spib, Image->PayloadBytes);
    CHECK(H->Ctx->Dma.Bdl[0].Bytes == Image->PayloadBytes);
    CHECK(H->Ctx->Dma.Bdl[0].AddressLow == 0x10000000U && H->Ctx->Dma.Bdl[0].AddressHigh == 0);
    CHECK(memcmp(H->Ctx->Dma.Virtual, Image->Data + Image->PayloadOffset, 0x1000) == 0);
    CHECK(memcmp(H->Ctx->Dma.Virtual + Image->PayloadBytes - 0x1000, Image->Data + Image->Size - 0x1000, 0x1000) == 0);
    CHECK_EQ(H->Dsp->DmaErrors, 0);
    CHECK_EQ(H->Dsp->UnexpectedAccesses, 0);
    CHECK_EQ(H->Dsp->RomRequests, 1);
    CHECK_EQ(H->Dsp->CorePowerUps, 1);
    CHECK_EQ(H->Dsp->StreamSubmits, 1);
    CHECK_EQ(H->Dsp->StreamStarts, 1);
    CHECK_EQ(H->Dsp->StreamStops, 1);
    CHECK_EQ(H->Dsp->StreamReleases, 1);
    CHECK_EQ(H->Dsp->Halted, 0);
    CHECK_EQ(H->Dsp->LoadedBytes, Image->PayloadBytes);
    /* the firmware answered with the image's version and the model's configuration */
    CHECK(H->Ctx->FwConfig.HasVersion);
    CHECK_EQ(H->Ctx->FwConfig.Major, Image->Major);
    CHECK_EQ(H->Ctx->FwConfig.Minor, Image->Minor);
    CHECK_EQ(H->Ctx->FwConfig.Hotfix, Image->Hotfix);
    CHECK_EQ(H->Ctx->FwConfig.Build, Image->Build);
    CHECK(H->Ctx->FwConfig.HasDownlinkMailbox && H->Ctx->FwConfig.HasUplinkMailbox && H->Ctx->FwConfig.HasTraceLog);
    CHECK(H->Ctx->FwConfig.HasMaxPipelines && H->Ctx->FwConfig.HasMaxLibraries && H->Ctx->FwConfig.HasContextSave);
    CHECK_EQ(H->Ctx->FwConfig.DownlinkMailboxBytes, 0x1000);
    CHECK_EQ(H->Ctx->FwConfig.UplinkMailboxBytes, 0x1000);
    CHECK_EQ(H->Ctx->FwConfig.TraceLogBytes, 0x2000);
    CHECK_EQ(H->Ctx->FwConfig.MaxPipelines, 32);
    CHECK_EQ(H->Ctx->FwConfig.MaxLibraries, 16);
    CHECK_EQ(H->Ctx->FwConfig.ContextSave, 0);
    CHECK_EQ(H->Ctx->FwConfig.TupleCount, 8);
    CHECK_EQ(H->Ctx->FwConfig.Registers.AbiVersion, 1);
    CHECK_EQ(H->Ctx->FwConfig.Registers.State, 5);
    CHECK_EQ(H->Ctx->FwConfig.Registers.Module, 2);
    CHECK_EQ(H->Ctx->FwConfig.Registers.Running, 0);
    CHECK_EQ(H->Ctx->FwConfig.Registers.RomFuseValues, 0x5A);
    CHECK_EQ(H->Ctx->FwConfig.Registers.RomLoadMethod, 1);
    CHECK_EQ(H->Ctx->FwConfig.Registers.RomImplementationVersionMajor, 1);
    CHECK_EQ(H->Ctx->FwConfig.Registers.RomImplementationVersionMinor, 2);
    CHECK_EQ(H->Ctx->FwConfig.Registers.RomImplementationRevisionMinor, 3);
    CHECK_EQ(H->Ctx->LastIpcStatus, SofIpc4Success);
    /* the virtual clock: two settles, four polls to FW_ENTERED, one to FW_READY, one to the reply */
    CHECK_EQ(H->Ctx->BootUs, 5000);
    CHECK_EQ(H->Delays, 8);
    CHECK_EQ(H->Dsp->UnstableCpaReads, 0);
    CHECK_EQ(H->Dsp->FirstCpaPollAt - H->Dsp->SubsystemSpaAt, 1000);
    CHECK_EQ(H->Dsp->FirstCoreCpaPollAt - H->Dsp->CoreSpaAt, 1000);
    CHECK_EQ(H->Dsp->EventLogCount, 7);
    CHECK_EQ(H->Dsp->EventLog[0], FAKE_EVENT_SUBSYSTEM_ON);
    CHECK_EQ(H->Dsp->EventLog[1], FAKE_EVENT_CORE_ON);
    CHECK_EQ(H->Dsp->EventLog[2], FAKE_EVENT_ROM_ACK);
    CHECK_EQ(H->Dsp->EventLog[3], FAKE_EVENT_ROM_READY);
    CHECK_EQ(H->Dsp->EventLog[4], FAKE_EVENT_FW_ENTERED);
    CHECK_EQ(H->Dsp->EventLog[5], FAKE_EVENT_FW_READY);
    CHECK_EQ(H->Dsp->EventLog[6], FAKE_EVENT_IPC_REPLY);
    CHECK_EQ(FakeDspEventTime(H->Dsp, FAKE_EVENT_ROM_ACK), 1300);
    CHECK_EQ(FakeDspEventTime(H->Dsp, FAKE_EVENT_ROM_READY), 1700);
    CHECK_EQ(FakeDspEventTime(H->Dsp, FAKE_EVENT_FW_ENTERED), 4000);
    CHECK_EQ(FakeDspEventTime(H->Dsp, FAKE_EVENT_FW_READY), 4200);
    CHECK_EQ(FakeDspEventTime(H->Dsp, FAKE_EVENT_IPC_REPLY), 4550);
    CHECK_EQ(H->LogCount, 4);
    CHECK_STR(H->Logs[0], "subsystem powered");
    CHECK_STR(H->Logs[1], "ROM ready for the firmware");
    CHECK_STR(H->Logs[2], "firmware entered");
    CHECK_STR(H->Logs[3], "FW_READY received");
    CHECK_EQ(H->Dsp->PowerOrderCount, 4);
    CHECK_EQ(H->Dsp->PowerOrder[0], FAKE_POWER_GPROCEN_ON);
    CHECK_EQ(H->Dsp->PowerOrder[1], FAKE_POWER_SUBSYSTEM_ON);
    CHECK_EQ(H->Dsp->PowerOrder[2], FAKE_POWER_CORE_ON);
    CHECK_EQ(H->Dsp->PowerOrder[3], FAKE_POWER_STREAM_RELEASE);
    CHECK(!FakeDspPowerEventSeen(H->Dsp, FAKE_POWER_INTERRUPTS_OFF));
    CHECK(!FakeDspPowerEventSeen(H->Dsp, FAKE_POWER_CORE_OFF));
    CHECK_EQ(H->Dsp->InterruptEnable, 1);
    CHECK_EQ(H->Dsp->HostIpcEnable, 1);
    CHECK_EQ(H->Dsp->IpcControl, 3);
    CheckBootWrites(H, 1);

    /* power down from ready: interrupts, core, subsystem, GPROCEN, and nothing else */
    Status = FwloadPowerDown(H->Ctx);
    CHECK_EQ(Status, FwloadOk);
    CHECK_EQ(H->Ctx->State, FwloadStateOff);
    CHECK_EQ(H->Ctx->Status, FwloadOk);
    CHECK_EQ(H->Ctx->CleanupStatus, FwloadOk);
    CHECK_EQ(H->Dsp->PowerOrderCount, 8);
    CHECK_EQ(H->Dsp->PowerOrder[4], FAKE_POWER_INTERRUPTS_OFF);
    CHECK_EQ(H->Dsp->PowerOrder[5], FAKE_POWER_CORE_OFF);
    CHECK_EQ(H->Dsp->PowerOrder[6], FAKE_POWER_SUBSYSTEM_OFF);
    CHECK_EQ(H->Dsp->PowerOrder[7], FAKE_POWER_GPROCEN_OFF);
    CHECK((H->Dsp->CoreControl & 0x100) == 0);
    CHECK((H->Dsp->SubsystemControl & 0x01000000) == 0);
    CHECK((H->Dsp->PpControl & FWLOAD_PPCTL_GPROCEN) == 0);
    CHECK_EQ(H->Dsp->InterruptEnable, 0);
    CHECK_EQ(H->Dsp->HostIpcEnable, 0);
    CHECK_EQ(H->Dsp->IpcControl, 0);
    CHECK_EQ(H->Dsp->UnexpectedAccesses, 0);
    CHECK_EQ(H->Dsp->StreamReleases, 1);
    CheckBootWrites(H, 2);
    /* powering down twice is harmless; booting again after that boots again */
    CHECK_EQ(FwloadPowerDown(H->Ctx), FwloadOk);
    HarnessBoot(H, Image, NULL);
    CHECK_EQ(FwloadBoot(H->Ctx), FwloadOk);
    CHECK_EQ(FwloadPowerDown(H->Ctx), FwloadOk);
    CHECK_EQ(H->Dsp->UnexpectedAccesses, 0);

    /* the stream tag the caller was given goes into the purge request: tag 2 encodes as 1 << 9 */
    H->StreamTag = 2;
    HarnessBoot(H, Image, NULL);
    CHECK_EQ(FwloadBoot(H->Ctx), FwloadOk);
    CHECK_EQ(H->Ctx->Dma.StreamTag, 2);
    Request = FakeDspWriteFind(H->Dsp, FWLOAD_BAR_DSP, FwloadPlatformArlS()->IpcRequest, 0x80000000UL);
    CHECK(Request != NULL && Request->Value == 0x81004200UL);
    CHECK_EQ(H->Dsp->DmaErrors, 0);
    CHECK_EQ(FwloadPowerDown(H->Ctx), FwloadOk);
    H->StreamTag = 0;
    HarnessBoot(H, Image, NULL);
    CHECK_EQ(FwloadBoot(H->Ctx), FwloadOk);
    CHECK_EQ(H->Ctx->Dma.StreamTag, FWLOAD_CL_STREAM_TAG);
    CHECK_EQ(FwloadPowerDown(H->Ctx), FwloadOk);
}

static
void
CheckTimeouts(
    HARNESS* H,
    const SOF_RI_IMAGE* Image)
{
    FAKE_DSP_FAULTS Faults;
    FWLOAD_STATUS Status;

    /* the subsystem never reports power active */
    Faults = *FakeDspDefaultFaults();
    Faults.SubsystemNeverPowers = TRUE;
    HarnessBoot(H, Image, &Faults);
    Status = FwloadBoot(H->Ctx);
    CHECK_EQ(Status, FwloadTimeoutPowerUp);
    CheckFailedBootIsOff(H, FwloadTimeoutPowerUp, FwloadStatePoweringUp);
    CHECK(H->Ctx->BootUs >= 51000 && H->Ctx->BootUs <= 52000);
    CHECK_EQ(H->Dsp->CorePowerUps, 0);
    CHECK_EQ(H->Dsp->RomRequests, 0);

    /* the core never reports power active: like every ROM init failure it is retried three times */
    Faults = *FakeDspDefaultFaults();
    Faults.CoreNeverPowers = TRUE;
    HarnessBoot(H, Image, &Faults);
    Status = FwloadBoot(H->Ctx);
    CHECK_EQ(Status, FwloadTimeoutPowerUp);
    CheckFailedBootIsOff(H, FwloadTimeoutPowerUp, FwloadStateRomInit);
    CHECK(H->Ctx->BootUs >= 1000 + 3 * (1000 + H->Timeouts.PowerUpUs) && H->Ctx->BootUs < 1000 + 3 * (1000 + H->Timeouts.PowerUpUs) + 12000);
    CHECK_EQ(H->Dsp->RomRequests, 3);
    CHECK_EQ(H->Ctx->Attempt, 3);
    CHECK_EQ(H->Dsp->CorePowerUps, 0);

    /* the ROM never acknowledges: three attempts, the core power-cycled between them */
    Faults = *FakeDspDefaultFaults();
    Faults.NeverAckRom = TRUE;
    HarnessBoot(H, Image, &Faults);
    Status = FwloadBoot(H->Ctx);
    CHECK_EQ(Status, FwloadTimeoutRomInit);
    CheckFailedBootIsOff(H, FwloadTimeoutRomInit, FwloadStateRomInit);
    CHECK_EQ(H->Dsp->RomRequests, 3);
    CHECK_EQ(H->Dsp->CorePowerUps, 3);
    CHECK_EQ(H->Ctx->Attempt, 3);
    CHECK(H->Ctx->BootUs >= 3 * H->Timeouts.RomInitUs && H->Ctx->BootUs < 3 * H->Timeouts.RomInitUs + 20000);
    CHECK_EQ(H->LogCount, 3);
    CHECK_STR(H->Logs[1], "ROM init attempt failed");
    CHECK_STR(H->Logs[2], "ROM init attempt failed");
    CHECK_EQ(H->Dsp->StreamStarts, 0);
    CHECK_EQ(H->Dsp->StreamReleases, 1);

    /* the ROM never reaches INIT_DONE */
    Faults = *FakeDspDefaultFaults();
    Faults.NeverRomReady = TRUE;
    HarnessBoot(H, Image, &Faults);
    Status = FwloadBoot(H->Ctx);
    CHECK_EQ(Status, FwloadTimeoutRomStatus);
    CheckFailedBootIsOff(H, FwloadTimeoutRomStatus, FwloadStateRomInit);
    CHECK_EQ(H->Dsp->RomRequests, 3);
    CHECK(H->Ctx->BootUs >= 3 * H->Timeouts.RomStatusUs && H->Ctx->BootUs < 3 * H->Timeouts.RomStatusUs + 20000);
    CHECK_EQ(H->Ctx->RomStatus & FWLOAD_FSR_STATE_MASK, FWLOAD_FSR_ROM_PURGE_BOOT);

    /* the firmware never reports entered */
    Faults = *FakeDspDefaultFaults();
    Faults.NeverFwEntered = TRUE;
    HarnessBoot(H, Image, &Faults);
    Status = FwloadBoot(H->Ctx);
    CHECK_EQ(Status, FwloadTimeoutBaseFw);
    CheckFailedBootIsOff(H, FwloadTimeoutBaseFw, FwloadStateLoading);
    CHECK(H->Ctx->BootUs >= H->Timeouts.BaseFwUs + 2000 && H->Ctx->BootUs < H->Timeouts.BaseFwUs + 12000);
    CHECK_EQ(H->Dsp->StreamStops, 1);
    CHECK_EQ(H->Dsp->StreamReleases, 1);
    CHECK_EQ(H->Ctx->RomStatus & FWLOAD_FSR_STATE_MASK, FWLOAD_FSR_ROM_FW_CODE_LOADED);

    /* the firmware never announces itself */
    Faults = *FakeDspDefaultFaults();
    Faults.NeverFwReady = TRUE;
    HarnessBoot(H, Image, &Faults);
    Status = FwloadBoot(H->Ctx);
    CHECK_EQ(Status, FwloadTimeoutFwReady);
    CheckFailedBootIsOff(H, FwloadTimeoutFwReady, FwloadStateFwReady);
    CHECK(H->Ctx->BootUs >= H->Timeouts.FwReadyUs + 4000 && H->Ctx->BootUs < H->Timeouts.FwReadyUs + 14000);

    /* the firmware never answers the configuration query */
    Faults = *FakeDspDefaultFaults();
    Faults.NeverIpcReply = TRUE;
    HarnessBoot(H, Image, &Faults);
    Status = FwloadBoot(H->Ctx);
    CHECK_EQ(Status, FwloadTimeoutIpcReply);
    CheckFailedBootIsOff(H, FwloadTimeoutIpcReply, FwloadStateConfiguring);
    CHECK(H->Ctx->BootUs >= H->Timeouts.IpcReplyUs + 4500 && H->Ctx->BootUs < H->Timeouts.IpcReplyUs + 14500);
    CHECK_EQ(H->Dsp->IpcRequests, 1);

    /* a slow firmware is given more time by the caller, and then boots */
    Faults = *FakeDspDefaultFaults();
    Faults.FwLoadUs = 5000000;
    HarnessBoot(H, Image, &Faults);
    CHECK_EQ(FwloadBoot(H->Ctx), FwloadTimeoutBaseFw);
    H->Timeouts.BaseFwUs = 6000000;
    HarnessBoot(H, Image, &Faults);
    CHECK_EQ(FwloadBoot(H->Ctx), FwloadOk);
    CHECK(H->Ctx->BootUs >= 5003000 && H->Ctx->BootUs <= 5003500);
    FwloadDefaultTimeouts(&H->Timeouts);

    /* one attempt only: the first ROM fault is fatal at once */
    Faults = *FakeDspDefaultFaults();
    Faults.NeverAckRom = TRUE;
    HarnessBoot(H, Image, &Faults);
    H->Ctx->Timeouts.BootAttempts = 1;
    CHECK_EQ(FwloadBoot(H->Ctx), FwloadTimeoutRomInit);
    CHECK_EQ(H->Dsp->RomRequests, 1);
    CHECK_EQ(H->Ctx->Attempt, 1);
}

/*
 * The first reply of a booted firmware (the p2b2 image's D1): the done register carries the header, the target data
 * register the extension, no notification is pending; the reply reaches the parser and the tuple walk, the payload is
 * read before the done bit is written back, and the model sees no protocol violation
 */
static
void
CheckFwConfigReply(
    HARNESS* H,
    const SOF_RI_IMAGE* Image)
{
    const FWLOAD_PLATFORM* Platform = FwloadPlatformArlS();
    FAKE_DSP_FAULTS Faults;
    UINT32 Index, Query = 0xFFFFFFFF, Acks = 0, LastAck = 0;

    HarnessBoot(H, Image, NULL);
    CHECK_EQ(FwloadBoot(H->Ctx), FwloadOk);
    /* the model posted FW_READY into the target register and the reply into the done register, and nothing else */
    CHECK_EQ(H->Dsp->Notifications, 1);
    CHECK_EQ(H->Dsp->Replies, 1);
    CHECK_EQ(H->Dsp->LastIpcPrimary, 0xC1000000UL);
    /* the receive path took the header from the done register without its bit and the extension from the target data register */
    CHECK_EQ(H->Ctx->LastIpcPrimary, 0x61000000UL);
    CHECK_EQ(H->Ctx->LastIpcExtension, 0);
    /* the reply reached SofIpc4ParseReply: its status came out of the header */
    CHECK_EQ(H->Ctx->LastIpcStatus, SofIpc4Success);
    /* and the tuple walk ran over the uplink mailbox, which was read exactly once */
    CHECK_EQ(H->Dsp->UplinkReads, 1);
    CHECK_EQ(H->Ctx->FwConfig.TupleCount, 8);
    CHECK(H->Ctx->FwConfig.HasVersion && H->Ctx->FwConfig.Major == Image->Major && H->Ctx->FwConfig.Build == Image->Build);
    CHECK_EQ(H->Dsp->WindowReads, 1);
    /* the done bit went back once for the reply after once for the ROM's acknowledgement, and after the query; the busy bit and its acknowledgement once each */
    for (Index = 0; Index < H->Dsp->WriteCount; Index++)
    {
        const FAKE_DSP_WRITE* Write = &H->Dsp->WriteLog[Index];

        if (Write->Offset == Platform->IpcRequest && (Write->Value & 0x40000000UL))
            Query = Index;
        if (Write->Offset == Platform->IpcReply && (Write->Value & FWLOAD_IPC_DONE))
        {
            Acks++;
            LastAck = Index;
        }
    }
    CHECK_EQ(Acks, 2);
    CHECK(Query != 0xFFFFFFFF && LastAck > Query);
    CHECK_EQ(FakeDspWriteCount(H->Dsp, FWLOAD_BAR_DSP, Platform->IpcTarget), 1);
    CHECK_EQ(FakeDspWriteCount(H->Dsp, FWLOAD_BAR_DSP, Platform->IpcTargetAck), 1);
    /* nothing was acknowledged before its payload was read, and nothing was left unacknowledged */
    CHECK_EQ(FakeDspIpcErrors(H->Dsp), 0);
    CHECK((H->Dsp->IpcReply & FWLOAD_IPC_DONE) == 0 && (H->Dsp->IpcTarget & FWLOAD_IPC_BUSY) == 0);
    CHECK((H->Dsp->IpcTargetAck & FWLOAD_IPC_BUSY) == 0);
    CHECK_EQ(FwloadPowerDown(H->Ctx), FwloadOk);
    CHECK_EQ(FakeDspIpcErrors(H->Dsp), 0);

    /* the acknowledgement follows the reply whatever the boot decides about it: a refused query, a bad header, malformed tuples, a shifted version */
    Faults = *FakeDspDefaultFaults();
    Faults.IpcStatus = SofIpc4Busy;
    HarnessBoot(H, Image, &Faults);
    CHECK_EQ(FwloadBoot(H->Ctx), FwloadIpcError);
    CheckFailedBootIsOff(H, FwloadIpcError, FwloadStateConfiguring);
    CHECK_EQ(H->Ctx->LastIpcPrimary, 0x61000004UL);
    Faults = *FakeDspDefaultFaults();
    Faults.TupleFault = FAKE_DSP_TUPLES_NO_VERSION;
    HarnessBoot(H, Image, &Faults);
    CHECK_EQ(FwloadBoot(H->Ctx), FwloadIpcBadReply);
    CheckFailedBootIsOff(H, FwloadIpcBadReply, FwloadStateConfiguring);
    Faults = *FakeDspDefaultFaults();
    Faults.ReportedVersionShift = 1;
    HarnessBoot(H, Image, &Faults);
    CHECK_EQ(FwloadBoot(H->Ctx), FwloadVersionMismatch);
    CheckFailedBootIsOff(H, FwloadVersionMismatch, FwloadStateConfiguring);
    CHECK_EQ(H->Ctx->LastIpcPrimary, 0x61000000UL);
    /* a crash notification while the reply is waited for is acknowledged too */
    Faults = *FakeDspDefaultFaults();
    Faults.CrashNotification = SOFIPC4_NOTIFY_EXCEPTION_CAUGHT;
    HarnessBoot(H, Image, &Faults);
    CHECK_EQ(FwloadBoot(H->Ctx), FwloadFwCrash);
    CheckFailedBootIsOff(H, FwloadFwCrash, FwloadStateFwReady);
    CHECK_EQ(H->Ctx->LastIpcPrimary, (SofIpc4GlbNotification << SOFIPC4_TYPE_SHIFT) |
        (SOFIPC4_NOTIFY_EXCEPTION_CAUGHT << SOFIPC4_NOTIFICATION_TYPE_SHIFT));
}

/* The polls after which a budget that is still running is reported as a stalled clock: twice what the budget needs at the poll interval; the power-down of a failed boot adds its two settles */
#define STALLED_POLLS(TimeoutUs) (2 * (UINT64)(TimeoutUs) / FWLOAD_POLL_INTERVAL_US)

/*
 * A clock that does not advance (the p2b2 image's D2): the model keeps running, so the DSP's silence is what the
 * budget must bound; every budget ends as a stalled clock after the polls it needs, never as a timeout, never as a hang
 */
static
void
CheckStalledClock(
    HARNESS* H,
    const SOF_RI_IMAGE* Image)
{
    FAKE_DSP_FAULTS Faults;
    FWLOAD_STATUS Status;

    /* the subsystem never reports power active: the 50 ms power-up budget */
    Faults = *FakeDspDefaultFaults();
    Faults.SubsystemNeverPowers = TRUE;
    HarnessBoot(H, Image, &Faults);
    H->ClockStalled = TRUE;
    Status = FwloadBoot(H->Ctx);
    CHECK_EQ(Status, FwloadClockStalled);
    CheckFailedBootIsOff(H, FwloadClockStalled, FwloadStatePoweringUp);
    /* the two settles of the subsystem and 100 polls for 50 ms at 500 us */
    CHECK_EQ(H->Delays, STALLED_POLLS(H->Timeouts.PowerUpUs) + 2);
    CHECK_EQ(H->Ctx->BootUs, 0);
    CHECK_EQ(H->Dsp->CorePowerUps, 0);
    CHECK_EQ(H->Dsp->RomRequests, 0);
    CHECK_STR(H->Logs[0], "subsystem powered");

    /* the ROM never acknowledges: three attempts, each stalled after the 500 ms ROM init budget, the core power-cycled between them */
    Faults = *FakeDspDefaultFaults();
    Faults.NeverAckRom = TRUE;
    HarnessBoot(H, Image, &Faults);
    H->ClockStalled = TRUE;
    Status = FwloadBoot(H->Ctx);
    CHECK_EQ(Status, FwloadClockStalled);
    CheckFailedBootIsOff(H, FwloadClockStalled, FwloadStateRomInit);
    /* the subsystem settle, then per attempt a core settle and 1000 polls, between attempts a core settle, at the end two settles */
    CHECK_EQ(H->Delays, 3 * STALLED_POLLS(H->Timeouts.RomInitUs) + 8);
    CHECK_EQ(H->Ctx->Attempt, 3);
    CHECK_EQ(H->Dsp->RomRequests, 3);
    CHECK_EQ(H->Dsp->CorePowerUps, 3);
    CHECK_EQ(H->LogCount, 3);
    CHECK_STR(H->Logs[1], "ROM init attempt failed");

    /* the ROM never reaches INIT_DONE: the 300 ms ROM status budget, three attempts */
    Faults = *FakeDspDefaultFaults();
    Faults.NeverRomReady = TRUE;
    HarnessBoot(H, Image, &Faults);
    H->ClockStalled = TRUE;
    Status = FwloadBoot(H->Ctx);
    CHECK_EQ(Status, FwloadClockStalled);
    CheckFailedBootIsOff(H, FwloadClockStalled, FwloadStateRomInit);
    CHECK_EQ(H->Delays, 3 * STALLED_POLLS(H->Timeouts.RomStatusUs) + 8);
    CHECK_EQ(H->Ctx->RomStatus & FWLOAD_FSR_STATE_MASK, FWLOAD_FSR_ROM_PURGE_BOOT);

    /* the firmware never reports entered: the 3 s base firmware budget */
    Faults = *FakeDspDefaultFaults();
    Faults.NeverFwEntered = TRUE;
    HarnessBoot(H, Image, &Faults);
    H->ClockStalled = TRUE;
    Status = FwloadBoot(H->Ctx);
    CHECK_EQ(Status, FwloadClockStalled);
    CheckFailedBootIsOff(H, FwloadClockStalled, FwloadStateLoading);
    /* two settles, 6000 polls, two settles */
    CHECK_EQ(H->Delays, STALLED_POLLS(H->Timeouts.BaseFwUs) + 4);
    CHECK_EQ(H->Dsp->StreamStops, 1);

    /* the firmware never announces itself: the 1 s FW_READY budget */
    Faults = *FakeDspDefaultFaults();
    Faults.NeverFwReady = TRUE;
    HarnessBoot(H, Image, &Faults);
    H->ClockStalled = TRUE;
    Status = FwloadBoot(H->Ctx);
    CHECK_EQ(Status, FwloadClockStalled);
    CheckFailedBootIsOff(H, FwloadClockStalled, FwloadStateFwReady);
    /* two settles, four polls to FW_ENTERED, 2000 polls, two settles */
    CHECK_EQ(H->Delays, STALLED_POLLS(H->Timeouts.FwReadyUs) + 8);

    /* the firmware never answers the configuration query: the 500 ms IPC reply budget */
    Faults = *FakeDspDefaultFaults();
    Faults.NeverIpcReply = TRUE;
    HarnessBoot(H, Image, &Faults);
    H->ClockStalled = TRUE;
    Status = FwloadBoot(H->Ctx);
    CHECK_EQ(Status, FwloadClockStalled);
    CheckFailedBootIsOff(H, FwloadClockStalled, FwloadStateConfiguring);
    /* as FW_READY plus one poll to FW_READY */
    CHECK_EQ(H->Delays, STALLED_POLLS(H->Timeouts.IpcReplyUs) + 9);
    CHECK_EQ(H->Dsp->IpcRequests, 1);
    CHECK_EQ(H->Ctx->LastIpcPrimary, SOFIPC4_FW_READY_PRIMARY);

    /* with a clock that advances the same silence is a timeout after half the polls */
    Faults = *FakeDspDefaultFaults();
    Faults.NeverIpcReply = TRUE;
    HarnessBoot(H, Image, &Faults);
    CHECK_EQ(FwloadBoot(H->Ctx), FwloadTimeoutIpcReply);
    CheckFailedBootIsOff(H, FwloadTimeoutIpcReply, FwloadStateConfiguring);
    CHECK_EQ(H->Delays, H->Timeouts.IpcReplyUs / FWLOAD_POLL_INTERVAL_US + 9);
}

static
void
CheckRomErrors(
    HARNESS* H,
    const SOF_RI_IMAGE* Image)
{
    FAKE_DSP_FAULTS Faults;
    UINT32 Index;
    FWLOAD_STATUS Status;

    for (Index = 0; Index < sizeof(FwloadCaseRomErrors) / sizeof(FwloadCaseRomErrors[0]); Index++)
    {
        UINT32 Code = FwloadCaseRomErrors[Index].Code;

        /* the ROM halts instead of reaching INIT_DONE: three attempts, each halted */
        Faults = *FakeDspDefaultFaults();
        Faults.HaltStage = FAKE_DSP_HALT_AFTER_PURGE;
        Faults.HaltCode = Code;
        HarnessBoot(H, Image, &Faults);
        Status = FwloadBoot(H->Ctx);
        CHECK_EQ(Status, FwloadRomError);
        CheckFailedBootIsOff(H, FwloadRomError, FwloadStateRomInit);
        CHECK_EQ(H->Ctx->RomError, Code);
        CHECK_EQ(H->Ctx->RomStatus, Code < 0x10000 ? (Code | FWLOAD_FSR_HALTED) : Code);
        CHECK_STR(FwloadRomErrorName(H->Ctx->RomError), FwloadCaseRomErrors[Index].Name);
        CHECK_EQ(H->Dsp->Halted, 3);
        CHECK_EQ(H->Dsp->StreamStarts, 0);
        CHECK(H->Ctx->BootUs < 20000);

        /* the firmware dies while it is being loaded: one attempt, the stream stopped */
        Faults = *FakeDspDefaultFaults();
        Faults.HaltStage = FAKE_DSP_HALT_DURING_LOAD;
        Faults.HaltCode = Code;
        HarnessBoot(H, Image, &Faults);
        Status = FwloadBoot(H->Ctx);
        CHECK_EQ(Status, FwloadRomError);
        CheckFailedBootIsOff(H, FwloadRomError, FwloadStateLoading);
        CHECK_EQ(H->Ctx->RomError, Code);
        CHECK_EQ(H->Dsp->Halted, 1);
        CHECK_EQ(H->Dsp->StreamStops, 1);
        CHECK(H->Ctx->BootUs < 6000);
    }
    CHECK_STR(FwloadRomErrorName(0), "unknown ROM error");
    CHECK_STR(FwloadRomErrorName(99), "unknown ROM error");
    CHECK_STR(FwloadFsrStateName(FWLOAD_FSR_FW_ENTERED), "firmware entered");
    CHECK_STR(FwloadFsrStateName(FWLOAD_FSR_ROM_FW_CODE_LOADED), "ROM firmware code loaded");
    CHECK_STR(FwloadFsrStateName(0x7F), "unknown state");
}

static
void
CheckFirmwareFaults(
    HARNESS* H,
    const SOF_RI_IMAGE* Image)
{
    FAKE_DSP_FAULTS Faults;

    /* the firmware's register layout is not the one the host knows */
    Faults = *FakeDspDefaultFaults();
    Faults.AbiVersion = 2;
    HarnessBoot(H, Image, &Faults);
    CHECK_EQ(FwloadBoot(H->Ctx), FwloadAbiMismatch);
    CheckFailedBootIsOff(H, FwloadAbiMismatch, FwloadStateFwReady);
    CHECK_EQ(H->Ctx->FwConfig.Registers.AbiVersion, 2);
    CHECK_EQ(H->Dsp->IpcRequests, 0);

    /* the firmware that answered is not the image that was loaded */
    Faults = *FakeDspDefaultFaults();
    Faults.ReportedVersionShift = 1;
    HarnessBoot(H, Image, &Faults);
    CHECK_EQ(FwloadBoot(H->Ctx), FwloadVersionMismatch);
    CheckFailedBootIsOff(H, FwloadVersionMismatch, FwloadStateConfiguring);
    CHECK_EQ(H->Ctx->FwConfig.Major, Image->Major + 1);

    /* the configuration reply is malformed */
    Faults = *FakeDspDefaultFaults();
    Faults.TupleFault = FAKE_DSP_TUPLES_LENGTH_OVERRUN;
    HarnessBoot(H, Image, &Faults);
    CHECK_EQ(FwloadBoot(H->Ctx), FwloadTruncated);
    CheckFailedBootIsOff(H, FwloadTruncated, FwloadStateConfiguring);
    Faults.TupleFault = FAKE_DSP_TUPLES_NO_VERSION;
    HarnessBoot(H, Image, &Faults);
    CHECK_EQ(FwloadBoot(H->Ctx), FwloadIpcBadReply);
    CheckFailedBootIsOff(H, FwloadIpcBadReply, FwloadStateConfiguring);
    CHECK_EQ(H->Ctx->FwConfig.TupleCount, 7);
    Faults.TupleFault = FAKE_DSP_TUPLES_NO_TERMINATOR;
    HarnessBoot(H, Image, &Faults);
    CHECK_EQ(FwloadBoot(H->Ctx), FwloadTruncated);
    CheckFailedBootIsOff(H, FwloadTruncated, FwloadStateConfiguring);

    /* the configuration query is refused */
    Faults = *FakeDspDefaultFaults();
    Faults.IpcStatus = SofIpc4Busy;
    HarnessBoot(H, Image, &Faults);
    CHECK_EQ(FwloadBoot(H->Ctx), FwloadIpcError);
    CheckFailedBootIsOff(H, FwloadIpcError, FwloadStateConfiguring);
    CHECK_EQ(H->Ctx->LastIpcStatus, SofIpc4Busy);

    /* the firmware crashes: an exception or watchdog notification instead of FW_READY */
    Faults = *FakeDspDefaultFaults();
    Faults.CrashNotification = SOFIPC4_NOTIFY_EXCEPTION_CAUGHT;
    HarnessBoot(H, Image, &Faults);
    CHECK_EQ(FwloadBoot(H->Ctx), FwloadFwCrash);
    CheckFailedBootIsOff(H, FwloadFwCrash, FwloadStateFwReady);
    CHECK_EQ(H->Ctx->RomError, SOFIPC4_NOTIFY_EXCEPTION_CAUGHT);
    Faults.CrashNotification = SOFIPC4_NOTIFY_WATCHDOG_TIMEOUT;
    HarnessBoot(H, Image, &Faults);
    CHECK_EQ(FwloadBoot(H->Ctx), FwloadFwCrash);
    CHECK_EQ(H->Ctx->RomError, SOFIPC4_NOTIFY_WATCHDOG_TIMEOUT);

    /* the register window cannot be read */
    Faults = *FakeDspDefaultFaults();
    Faults.MemoryReadFails = TRUE;
    HarnessBoot(H, Image, &Faults);
    CHECK_EQ(FwloadBoot(H->Ctx), FwloadHardwareFault);
    CheckFailedBootIsOff(H, FwloadHardwareFault, FwloadStateFwReady);
}

/*
 * R1 of the branch review: the interrupt pointer is a register of the DSP's power domain. Whatever it reads before the subsystem is powered, nothing is written through it, and a pointer that names no 32-bit
 * register names none for the whole boot
 */
static
void
CheckInterruptPointer(
    HARNESS* H,
    const SOF_RI_IMAGE* Image)
{
    const FWLOAD_PLATFORM* Platform = FwloadPlatformArlS();
    /* zero, an odd address, the last address of the pointer's field, and the bus's all ones */
    static const UINT32 NoRegister[] = { 0, 1, 0x1D01, 0x1FFFFF, 0xFFFFFFFF };
    FAKE_DSP_FAULTS Faults;
    UINT32 Index, EnableWrites;

    /* all ones until the subsystem is powered: the pointer is read where it is used, so every write to the three enables arrives powered and the boot is the model's */
    Faults = *FakeDspDefaultFaults();
    Faults.PointerNeedsPower = TRUE;
    HarnessBoot(H, Image, &Faults);
    CHECK_EQ(FwloadBoot(H->Ctx), FwloadOk);
    EnableWrites = 0;
    for (Index = 0; Index < H->Dsp->WriteCount; Index++)
    {
        const FAKE_DSP_WRITE* Write = &H->Dsp->WriteLog[Index];

        if (Write->Bar == FWLOAD_BAR_DSP && (Write->Offset == FAKE_DSP_INTERRUPT_POINTER ||
            Write->Offset == Platform->HostIpcInterruptEnable || Write->Offset == Platform->IpcControl))
        {
            CHECK(Write->Powered);
            EnableWrites++;
        }
    }
    CHECK_EQ(EnableWrites, 3);
    CHECK_EQ(H->Dsp->InterruptPointerReads, 1);
    CHECK_EQ(H->Dsp->InterruptPointerReadsUnpowered, 0);
    CHECK_EQ(H->Dsp->UnexpectedAccesses, 0);
    CHECK_EQ(FwloadPowerDown(H->Ctx), FwloadOk);
    CHECK_EQ(H->Dsp->InterruptPointerReads, 2);
    CHECK_EQ(H->Dsp->UnexpectedAccesses, 0);

    /* the subsystem never powers up: the cleanup meets all ones and writes nothing through the pointer */
    Faults = *FakeDspDefaultFaults();
    Faults.PointerNeedsPower = TRUE;
    Faults.SubsystemNeverPowers = TRUE;
    HarnessBoot(H, Image, &Faults);
    CHECK_EQ(FwloadBoot(H->Ctx), FwloadTimeoutPowerUp);
    CheckFailedBootIsOff(H, FwloadTimeoutPowerUp, FwloadStatePoweringUp);
    CHECK_EQ(FakeDspWriteCount(H->Dsp, FWLOAD_BAR_DSP, FAKE_DSP_INTERRUPT_POINTER), 0);
    CHECK_EQ(FakeDspWriteCount(H->Dsp, FWLOAD_BAR_DSP, 0x1FFFFF), 0);
    CHECK_EQ(H->Dsp->InterruptPointerReads, 2);
    CHECK_EQ(H->Dsp->InterruptPointerReadsUnpowered, 2);

    /* a pointer that names no 32-bit register once the DSP is powered: the interrupt is not enabled and nothing is written through it */
    for (Index = 0; Index < sizeof(NoRegister) / sizeof(NoRegister[0]); Index++)
    {
        Faults = *FakeDspDefaultFaults();
        Faults.InterruptPointerOverride = TRUE;
        Faults.InterruptPointer = NoRegister[Index];
        HarnessBoot(H, Image, &Faults);
        CHECK_EQ(FwloadBoot(H->Ctx), FwloadHardwareFault);
        CHECK_EQ(H->Ctx->State, FwloadStateFailed);
        CHECK_EQ(H->Ctx->FailedState, FwloadStateRomInit);
        CHECK_EQ(H->Ctx->CleanupStatus, FwloadOk);
        /* no register was written through the pointer, so the model never saw the interrupt go off */
        CHECK(!FakeDspPowerEventSeen(H->Dsp, FAKE_POWER_INTERRUPTS_OFF));
        CheckDspIsOff(H);
        CHECK_EQ(FakeDspWriteCount(H->Dsp, FWLOAD_BAR_DSP, FAKE_DSP_INTERRUPT_POINTER), 0);
        CHECK_EQ(FakeDspWriteCount(H->Dsp, FWLOAD_BAR_DSP, NoRegister[Index] & 0x1FFFFF), 0);
        CHECK_EQ(H->Dsp->InterruptEnable, 0);
        CHECK_EQ(H->Dsp->HostIpcEnable, 0);
        CHECK_EQ(H->Dsp->IpcControl, 0);
    }
}

static
void
CheckRetry(
    HARNESS* H,
    const SOF_RI_IMAGE* Image)
{
    FAKE_DSP_FAULTS Faults;

    /* the ROM misses the first two purge requests; the third boots */
    Faults = *FakeDspDefaultFaults();
    Faults.DroppedRomRequests = 2;
    HarnessBoot(H, Image, &Faults);
    CHECK_EQ(FwloadBoot(H->Ctx), FwloadOk);
    CHECK_EQ(H->Ctx->Attempt, 3);
    CHECK_EQ(H->Dsp->RomRequests, 3);
    CHECK_EQ(H->Dsp->CorePowerUps, 3);
    CHECK_EQ(H->Dsp->DroppedRequests, 2);
    CHECK_EQ(H->Ctx->Status, FwloadOk);
    CHECK(H->Ctx->BootUs >= 2 * H->Timeouts.RomInitUs);
    CheckBootWrites(H, 3);
    CHECK_EQ(H->Dsp->UnexpectedAccesses, 0);
}

static
void
CheckCancellation(
    HARNESS* H,
    const SOF_RI_IMAGE* Image)
{
    static const FWLOAD_STATE Phases[8] =
    {
        FwloadStatePoweringUp, FwloadStateRomInit, FwloadStateLoading, FwloadStateLoading,
        FwloadStateLoading, FwloadStateFwReady, FwloadStateConfiguring, FwloadStateConfiguring
    };
    FWLOAD_STATUS Status;
    UINT32 Delays;
    FAKE_DSP_FAULTS Faults;

    /* a cancellation after each clock advance of the boot: the boot unwinds and the DSP is off */
    for (Delays = 1; Delays <= 8; Delays++)
    {
        HarnessBoot(H, Image, NULL);
        H->CancelAfterDelays = Delays;
        Status = FwloadBoot(H->Ctx);
        H->CancelAfterDelays = 0;
        CHECK_EQ(Status, FwloadCancelled);
        CHECK_EQ(H->Ctx->State, FwloadStateFailed);
        CHECK_EQ(H->Ctx->Status, FwloadCancelled);
        CHECK_EQ(H->Ctx->FailedState, Phases[Delays - 1]);
        /* the cancellation is honoured at the next poll, not at the end of the timeout */
        CHECK(H->Ctx->BootUs < 20000);
        CHECK((H->Dsp->SubsystemControl & 0x01000000) == 0);
        CHECK((H->Dsp->PpControl & FWLOAD_PPCTL_GPROCEN) == 0);
        CHECK_EQ(H->Dsp->StreamSubmits, H->Dsp->StreamReleases);
        CHECK_EQ(H->Dsp->StreamSubmits, H->Dsp->StreamStops);
        CHECK(H->Dsp->StreamStarts <= H->Dsp->StreamStops);
        CHECK_EQ(H->Dsp->UnexpectedAccesses, 0);
        if (Delays >= 3)
        {
            CHECK_EQ(H->Dsp->StreamStops, 1);
            CHECK_EQ(H->Dsp->StreamReleases, 1);
        }
        /* a failed context cannot be booted again; powering it down returns it to off */
        CHECK_EQ(FwloadBoot(H->Ctx), FwloadBadState);
        CHECK_EQ(FwloadPowerDown(H->Ctx), FwloadOk);
        CHECK_EQ(H->Ctx->State, FwloadStateOff);
    }
    /* a context that was never initialised cannot boot either */
    memset(H->Ctx, 0, sizeof(*H->Ctx));
    CHECK_EQ(FwloadBoot(H->Ctx), FwloadBadState);
    CHECK_EQ(FwloadPowerDown(H->Ctx), FwloadOk);
    /* a cancellation during a power-status poll is honoured at the next poll, not after the timeout */
    Faults = *FakeDspDefaultFaults();
    Faults.CoreNeverPowers = TRUE;
    HarnessBoot(H, Image, &Faults);
    H->CancelAfterDelays = 3;
    Status = FwloadBoot(H->Ctx);
    H->CancelAfterDelays = 0;
    CHECK_EQ(Status, FwloadCancelled);
    CHECK_EQ(H->Ctx->FailedState, FwloadStateRomInit);
    CHECK(H->Ctx->BootUs >= 3500 && H->Ctx->BootUs < 20000);
    CHECK_EQ(H->Ctx->Attempt, 1);
    CHECK_EQ(H->Dsp->UnexpectedAccesses, 0);
}

/* The buffer changes after it was parsed: the ROM rejects what it is given */
static
void
CheckBadPayload(
    HARNESS* H,
    UINT8* Data,
    const SOF_RI_IMAGE* Parsed)
{
    static const struct
    {
        SIZE_T Offset;
        UINT8 Xor;
        UINT32 Code;
    } Cases[] =
    {
        { 0xC14, 0x01, FWLOAD_ROM_BASE_FW_NOT_FOUND },                  /* the directory magic */
        { 0xC14 + 0x5C + 16, 0x01, FWLOAD_ROM_CSE_VALIDATION_FAILED }, /* the signature header's vendor */
        { 0xC14 + 0x2000, 0x01, FWLOAD_ROM_BASE_FW_NOT_FOUND },         /* the firmware header magic */
        { 0x2D30 + 80 + 12 + 8, 0x40, FWLOAD_ROM_IMR_TO_SMALL },        /* the base firmware's last window, past the stream */
        { 0x2C14 + 28, 0x5A, FWLOAD_ROM_BASEFW_INCOMPAT }               /* a firmware of another major version: the model refuses it */
    };
    UINT32 Index;

    for (Index = 0; Index < sizeof(Cases) / sizeof(Cases[0]); Index++)
    {
        Data[Cases[Index].Offset] ^= Cases[Index].Xor;
        HarnessBoot(H, Parsed, NULL);
        CHECK_EQ(FwloadBoot(H->Ctx), FwloadRomError);
        CheckFailedBootIsOff(H, FwloadRomError, FwloadStateLoading);
        CHECK_EQ(H->Ctx->RomError, Cases[Index].Code);
        CHECK_EQ(H->Dsp->Halted, 1);
        CHECK_EQ(H->Dsp->StreamStops, 1);
        Data[Cases[Index].Offset] ^= Cases[Index].Xor;
    }
    HarnessBoot(H, Parsed, NULL);
    CHECK_EQ(FwloadBoot(H->Ctx), FwloadOk);
}

static
UINT64
Fnv1a64(
    const UINT8* Data,
    SIZE_T Bytes)
{
    UINT64 Hash = 0xCBF29CE484222325ULL;
    SIZE_T Index;

    for (Index = 0; Index < Bytes; Index++)
    {
        Hash ^= Data[Index];
        Hash *= 0x100000001B3ULL;
    }
    return Hash;
}

static
UINT8*
LoadRealImage(
    void)
{
    FILE* File;
    UINT8* Data;
    char Path[512];

    snprintf(Path, sizeof(Path), "%s/sof-arl-s.ri", FWLOAD_FIXTURE_DIR);
    File = fopen(Path, "rb");
    if (File == NULL)
    {
        fprintf(stderr, "cannot open %s\n", Path);
        exit(2);
    }
    Data = (UINT8*)malloc(REAL_IMAGE_BYTES);
    if (Data == NULL)
        exit(2);
    CHECK_EQ(fread(Data, 1, REAL_IMAGE_BYTES, File), REAL_IMAGE_BYTES);
    CHECK(fgetc(File) == EOF);
    fclose(File);
    CHECK_EQ(Fnv1a64(Data, REAL_IMAGE_BYTES), REAL_IMAGE_FNV1A64);
    return Data;
}

static
void
CheckRealImage(
    const UINT8* Data,
    SOF_RI_IMAGE* Image,
    PSOF_RI_MODULE Modules)
{
    SOF_RI_IMAGE Spare;
    UINT32 Index, Segment;
    UINT8* Copy;

    CHECK_EQ(SofRiParse(Data, REAL_IMAGE_BYTES, Modules, 64, Image), FwloadOk);
    CHECK_EQ(Image->ExtManVersionMajor, 1);
    CHECK_EQ(Image->ExtManVersionMinor, 0);
    CHECK_EQ(Image->ExtManModuleCount, 32);
    CHECK_EQ(Image->PayloadOffset, 0xC14);
    CHECK_EQ(Image->PayloadBytes, REAL_PAYLOAD_BYTES);
    CHECK_EQ(Image->UsedBytes, REAL_PAYLOAD_BYTES);
    CHECK(Image->HasPartitionDirectory);
    CHECK_STR(Image->PartitionDirectoryName, "ADSP");
    CHECK_EQ(Image->PartitionEntryCount, 3);
    CHECK_EQ(Image->PartitionCount, 3);
    CHECK_STR(Image->Partition[0].Name, "ADSP.man");
    CHECK_EQ(Image->Partition[0].Offset, 0x5C);
    CHECK_EQ(Image->Partition[0].Length, 0x4B8);
    CHECK_STR(Image->Partition[1].Name, "ADSP.met");
    CHECK_EQ(Image->Partition[1].Offset, 0x4C0);
    CHECK_EQ(Image->Partition[1].Length, 0x70);
    CHECK_STR(Image->Partition[2].Name, "ADSP");
    CHECK_EQ(Image->Partition[2].Offset, 0x540);
    CHECK_EQ(Image->Partition[2].Length, 0x119ACC);
    CHECK(Image->HasSignature);
    CHECK_EQ(Image->SignatureVendor, 0x8086);
    CHECK_EQ(Image->SignatureDate, 0x20260915);
    CHECK_EQ(Image->SignatureMajor, 2);
    CHECK_EQ(Image->SignatureMinor, 15);
    CHECK_EQ(Image->SignatureHotfix, 0);
    CHECK_EQ(Image->SignatureBuild, 1);
    CHECK_EQ(Image->SignatureSvn, 0);
    CHECK_EQ(Image->HeaderOffset, 0x2C14);
    CHECK_STR(Image->Name, "ADSPFW");
    CHECK_EQ(Image->Major, 2);
    CHECK_EQ(Image->Minor, 15);
    CHECK_EQ(Image->Hotfix, 0);
    CHECK_EQ(Image->Build, 1);
    CHECK_EQ(Image->PreloadPageCount, 282);
    CHECK_EQ(Image->ImageFlags, 2);
    CHECK_EQ(Image->FeatureMask, 2);
    CHECK_EQ(Image->HwBufferBase, 0x100000);
    CHECK_EQ(Image->HwBufferLength, 0);
    CHECK_EQ(Image->LoadOffset, 0x40000);
    CHECK_EQ(Image->ModuleCount, 32);
    CHECK_EQ(Image->ConfigOffset, 0x2C48 + 32 * 116);
    CHECK(Image->Data == Data);
    CHECK_EQ(Image->Size, REAL_IMAGE_BYTES);

    CHECK_STR(Modules[0].Name, "BRNGUP");
    CHECK_EQ(Modules[0].Type, 0x21);
    CHECK_EQ(Modules[0].LoadType, 1);
    CHECK_EQ(Modules[0].EntryPoint, 0xA1048000);
    CHECK_EQ(Modules[0].CfgOffset, 0);
    CHECK_EQ(Modules[0].CfgCount, 0);
    CHECK_EQ(Modules[0].AffinityMask, 3);
    CHECK_EQ(Modules[0].InstanceMaxCount, 1);
    CHECK_EQ(Modules[0].InstanceBssSize, 1);
    CHECK(Modules[0].Uuid[0] == 0xF3 && Modules[0].Uuid[1] == 0xE4 && Modules[0].Uuid[15] == 0xEB);
    CHECK_EQ(Modules[0].Segment[0].FileOffset, 0x8000);
    CHECK_EQ(Modules[0].Segment[0].Pages, 1);
    CHECK_EQ(Modules[0].Segment[0].Type, 0);
    CHECK(Modules[0].Segment[0].Load);
    CHECK_EQ(Modules[0].Segment[1].FileOffset, 0x9000);
    CHECK_EQ(Modules[0].Segment[1].Pages, 1);
    CHECK_EQ(Modules[0].Segment[1].Type, 1);
    CHECK_EQ(Modules[0].Segment[2].Pages, 0);
    CHECK(!Modules[0].HasConfig);
    CHECK_EQ(Modules[0].IsBytes, 0);
    CHECK_STR(Modules[1].Name, "COLD");
    CHECK_EQ(Modules[1].Segment[0].Pages, 11);
    CHECK_EQ(Modules[1].Segment[0].FileOffset, 0xA000);
    CHECK_EQ(Modules[1].Segment[1].Pages, 85);
    CHECK_STR(Modules[2].Name, "BASEFW");
    CHECK_EQ(Modules[2].EntryPoint, 0xA0031000);
    CHECK_EQ(Modules[2].Segment[0].Pages, 108);
    CHECK_EQ(Modules[2].Segment[1].Pages, 68);
    CHECK_EQ(Modules[2].Segment[1].FileOffset, 0xD6000);
    CHECK_EQ(Modules[2].Segment[2].Type, 2);
    CHECK_EQ(Modules[2].Segment[2].Pages, 4);
    CHECK(!Modules[2].Segment[2].Load);
    CHECK_STR(Modules[3].Name, "MIXIN");
    CHECK_EQ(Modules[3].LoadType, 0);
    CHECK_EQ(Modules[3].CfgOffset, 0);
    CHECK_EQ(Modules[3].CfgCount, 5);
    CHECK(Modules[3].HasConfig);
    CHECK_EQ(Modules[3].IsBytes, 0x128);
    CHECK_EQ(Modules[3].Cps, 0x4C3BA0);
    CHECK_EQ(Modules[3].Ibs, 0x180);
    CHECK_EQ(Modules[3].Obs, 0x180);
    CHECK_EQ(Modules[3].Cpc, 0x1384);
    CHECK_EQ(Modules[3].InstanceMaxCount, 30);
    CHECK_STR(Modules[5].Name, "COPIER");
    CHECK_EQ(Modules[5].CfgOffset, 10);
    CHECK_EQ(Modules[5].CfgCount, 42);
    CHECK(Modules[5].HasConfig);
    CHECK_EQ(Modules[5].IsBytes, 0x118);
    CHECK_EQ(Modules[5].Cps, 0x4B0AF0);
    CHECK_EQ(Modules[5].Ibs, 0x300);
    CHECK_EQ(Modules[5].Obs, 0x300);
    CHECK_EQ(Modules[5].Cpc, 0x1336);
    CHECK_STR(Modules[6].Name, "PEAKVOL");
    CHECK_EQ(Modules[6].CfgOffset, 52);
    CHECK_EQ(Modules[6].IsBytes, 0x1E0);
    CHECK_EQ(Modules[6].Cps, 0xB20638);
    CHECK_EQ(Modules[6].Cpc, 0x2D93);
    CHECK_STR(Modules[31].Name, "PHASEVOC");
    CHECK_EQ(Modules[31].CfgOffset, 184);
    CHECK(Modules[31].HasConfig);
    CHECK_EQ(Modules[31].IsBytes, 0x1000);
    CHECK_EQ(Modules[31].Cps, 0xF4240);
    CHECK_EQ(Modules[31].Ibs, 0x80);
    CHECK_EQ(Modules[31].Obs, 0x80);
    for (Index = 0; Index < 32; Index++)
    {
        CHECK(Modules[Index].Name[0] != 0);
        for (Segment = 0; Segment < 3; Segment++)
            if (Modules[Index].Segment[Segment].Pages && Modules[Index].Segment[Segment].Load)
                CHECK((SIZE_T)Modules[Index].Segment[Segment].FileOffset +
                    (SIZE_T)Modules[Index].Segment[Segment].Pages * 4096 <= REAL_PAYLOAD_BYTES);
    }

    /* too small a module array: the count is reported, nothing is written */
    memset(&Spare, 0, sizeof(Spare));
    CHECK_EQ(SofRiParse(Data, REAL_IMAGE_BYTES, Modules, 31, &Spare), FwloadTooManyModules);
    CHECK_EQ(Spare.ModuleCount, 32);
    CHECK(Spare.Modules == NULL);
    CHECK_EQ(SofRiParse(Data, REAL_IMAGE_BYTES, NULL, 0, &Spare), FwloadTooManyModules);
    CHECK_EQ(SofRiParse(NULL, REAL_IMAGE_BYTES, Modules, 64, &Spare), FwloadNoData);
    CHECK_EQ(SofRiParse(Data, 0, Modules, 64, &Spare), FwloadNoData);

    /* the directory is recognised by its magic: without it the image still parses, unsigned */
    Copy = (UINT8*)malloc(REAL_IMAGE_BYTES);
    CHECK(Copy != NULL);
    memcpy(Copy, Data, REAL_IMAGE_BYTES);
    Copy[0xC14] ^= 1;
    CHECK_EQ(SofRiParse(Copy, REAL_IMAGE_BYTES, Modules, 64, &Spare), FwloadOk);
    CHECK(!Spare.HasPartitionDirectory);
    CHECK(!Spare.HasSignature);
    CHECK_EQ(Spare.PartitionCount, 0);
    free(Copy);
}

static
void
CheckMalformedReal(
    const UINT8* Data)
{
    SOF_RI_IMAGE Image;
    SOF_RI_MODULE Modules[64];
    UINT8* Copy;
    SIZE_T Length;
    UINT32 Index, Accepted;
    FWLOAD_STATUS Status;

    Copy = (UINT8*)malloc(REAL_IMAGE_BYTES);
    if (Copy == NULL)
        return;
    /* every prefix up to the end of the module table: never accepted */
    for (Length = 0; Length <= 0x4100; Length++)
    {
        Status = SofRiParse(Data, Length, Modules, 64, &Image);
        CHECK(Status != FwloadOk);
        CHECK(Image.BadOffset <= REAL_IMAGE_BYTES);
    }
    /* the module array needs its whole room: 32 entries after the header */
    for (Length = 0x2C48; Length < 0x3AC8; Length++)
    {
        Status = SofRiParse(Data, Length, Modules, 64, &Image);
        CHECK_EQ(Status, FwloadBadLength);
        CHECK_EQ(Image.BadOffset, 0x2C38);
    }
    /* every page afterwards: the preloaded pages reach the end of the file, so a cut is truncated */
    for (Length = 0x3AC8; Length < REAL_IMAGE_BYTES; Length += 4096)
    {
        Status = SofRiParse(Data, Length, Modules, 64, &Image);
        CHECK_EQ(Status, FwloadTruncated);
        CHECK_EQ(Image.BadOffset, 0x2C24);
    }
    CHECK_EQ(SofRiParse(Data, REAL_IMAGE_BYTES - 1, Modules, 64, &Image), FwloadTruncated);
    CHECK_EQ(SofRiParse(Data, REAL_IMAGE_BYTES, Modules, 64, &Image), FwloadOk);

    /* single byte flips in the headers and the module table: accepted only when no fact the reader checks moved */
    Accepted = 0;
    for (Index = 0; Index < 0x4100; Index++)
    {
        memcpy(Copy, Data, REAL_IMAGE_BYTES);
        Copy[Index] ^= 0x5A;
        Status = SofRiParse(Copy, REAL_IMAGE_BYTES, Modules, 64, &Image);
        CHECK(Status == FwloadOk || Status == FwloadBadSignature || Status == FwloadBadLength ||
            Status == FwloadTruncated || Status == FwloadUnsupported || Status == FwloadTooManyModules);
        if (Status == FwloadOk)
        {
            Accepted++;
            CHECK_EQ(Image.ModuleCount, 32);
        }
        else
            CHECK(Image.BadOffset < REAL_IMAGE_BYTES);
    }
    CHECK(Accepted >= 0x4100 - 512);
    /* the bytes the reader relies on reject the flip: the two magics and lengths, the module counts, the preload pages, the module ids, a configuration range and a segment window */
    static const struct
    {
        SIZE_T From;
        SIZE_T To;
    } Rejected[] =
    {
        { 0x00, 0x0A }, { 0x0C, 0x0E }, { 0x10, 0x14 },
        { 0x2C14, 0x2C1C }, { 0x2C24, 0x2C28 }, { 0x2C38, 0x2C3C }, { 0x2C48, 0x2C4C },
        { 0x2CBC, 0x2CC0 }, { 0x2D30, 0x2D34 }, { 0x2D94, 0x2D95 },
        { 0x2DA4, 0x2DA8 }, { 0x2E18, 0x2E1C }
    };
    for (Index = 0; Index < sizeof(Rejected) / sizeof(Rejected[0]); Index++)
    {
        SIZE_T At;

        for (At = Rejected[Index].From; At < Rejected[Index].To; At++)
        {
            memcpy(Copy, Data, REAL_IMAGE_BYTES);
            Copy[At] ^= 0x5A;
            Status = SofRiParse(Copy, REAL_IMAGE_BYTES, Modules, 64, &Image);
            CHECK(Status != FwloadOk);
        }
    }
    /* flips anywhere: the reader stays inside the buffer (ASan) and reports the module count only for a whole image */
    for (Index = 0; Index < 4000; Index++)
    {
        SIZE_T At = (SIZE_T)((Index * 2654435761UL) % REAL_IMAGE_BYTES);

        memcpy(Copy, Data, REAL_IMAGE_BYTES);
        Copy[At] ^= (UINT8)(1 + (Index % 255));
        Status = SofRiParse(Copy, REAL_IMAGE_BYTES, Modules, 64, &Image);
        CHECK(Status != FwloadOk || Image.ModuleCount == 32);
    }
    free(Copy);
}

static
void
CheckSyntheticImage(
    HARNESS* H)
{
    UINT8* Image = (UINT8*)malloc(FWLOAD_CASE_IMAGE_BYTES);
    SOF_RI_IMAGE Parsed;
    SOF_RI_MODULE Modules[4];
    UINT32 Index;
    SIZE_T Length;
    FWLOAD_STATUS Status;

    if (Image == NULL)
        return;
    FwloadCaseBuildImage(Image);
    CHECK_EQ(SofRiParse(Image, FWLOAD_CASE_IMAGE_BYTES, Modules, 4, &Parsed), FwloadOk);
    CHECK_EQ(Parsed.ExtManModuleCount, 1);
    CHECK_EQ(Parsed.PayloadOffset, 0x30);
    CHECK_EQ(Parsed.PayloadBytes, 0xA000);
    CHECK_EQ(Parsed.UsedBytes, 0xA000);
    CHECK_EQ(Parsed.ModuleCount, 1);
    CHECK_EQ(Parsed.Major, 2);
    CHECK_EQ(Parsed.Minor, 15);
    CHECK_EQ(Parsed.PreloadPageCount, 2);
    CHECK_STR(Modules[0].Name, "BRNGUP");
    CHECK(Modules[0].HasConfig);
    CHECK_EQ(Modules[0].CfgOffset, 0);
    CHECK_EQ(Modules[0].CfgCount, 1);
    CHECK_EQ(Modules[0].IsBytes, 0x1000);
    CHECK_EQ(Modules[0].Cpc, 0x100);
    CHECK_EQ(Parsed.PartitionCount, 2);
    CHECK(Parsed.HasSignature);
    CHECK_EQ(Parsed.SignatureDate, 0x20260101);

    /* every prefix of the synthetic image is rejected */
    for (Length = 0; Length < FWLOAD_CASE_IMAGE_BYTES; Length++)
    {
        Status = SofRiParse(Image, Length, Modules, 4, &Parsed);
        CHECK(Status != FwloadOk);
    }

    /* every malformed variant reports the status the table expects */
    for (Index = 0; Index < FwloadCaseFaultCount; Index++)
    {
        FwloadCaseBuildImage(Image);
        FwloadCaseApplyFault(Image, FwloadCaseFaults[Index].Fault);
        Status = SofRiParse(Image, FWLOAD_CASE_IMAGE_BYTES, Modules, 4, &Parsed);
        CHECK_EQ(Status, FwloadCaseFaults[Index].Status);
        if (FwloadCaseFaults[Index].Fault == FwloadCaseFaultCssVendor)
        {
            CHECK(Parsed.HasSignature);
            CHECK_EQ(Parsed.SignatureVendor, 0x8087);
        }
        if (FwloadCaseFaults[Index].Fault == FwloadCaseFaultCpdEntriesLarge)
        {
            CHECK_EQ(Parsed.PartitionEntryCount, 0xFFFFFFFF);
            CHECK_EQ(Parsed.PartitionCount, 4);
        }
    }

    /* the synthetic image boots the model like the real one */
    FwloadCaseBuildImage(Image);
    CHECK_EQ(SofRiParse(Image, FWLOAD_CASE_IMAGE_BYTES, Modules, 4, &Parsed), FwloadOk);
    HarnessBoot(H, &Parsed, NULL);
    CHECK_EQ(FwloadBoot(H->Ctx), FwloadOk);
    CHECK_EQ(H->Dsp->LoadedBytes, 0xA000);
    CHECK_EQ(H->Dsp->DmaErrors, 0);
    CHECK_EQ(H->Dsp->UnexpectedAccesses, 0);
    /* the reader does not judge the signature header: the ROM does, and halts */
    FwloadCaseApplyFault(Image, FwloadCaseFaultCssVendor);
    CHECK_EQ(SofRiParse(Image, FWLOAD_CASE_IMAGE_BYTES, Modules, 4, &Parsed), FwloadOk);
    HarnessBoot(H, &Parsed, NULL);
    CHECK_EQ(FwloadBoot(H->Ctx), FwloadRomError);
    CheckFailedBootIsOff(H, FwloadRomError, FwloadStateLoading);
    CHECK_EQ(H->Ctx->RomError, FWLOAD_ROM_CSE_VALIDATION_FAILED);
    /* a buffer that is cut after parsing streams what is left; the ROM finds the module window missing */
    FwloadCaseBuildImage(Image);
    CHECK_EQ(SofRiParse(Image, FWLOAD_CASE_IMAGE_BYTES, Modules, 4, &Parsed), FwloadOk);
    HarnessBoot(H, &Parsed, NULL);
    H->Bytes = 0x9000;
    H->Segment[0].Bytes = 0x9000;
    CHECK_EQ(FwloadBoot(H->Ctx), FwloadDmaTooSmall);
    CheckFailedBootIsOff(H, FwloadDmaTooSmall, FwloadStatePoweringUp);
    H->Bytes = REAL_PAYLOAD_BYTES;
    H->Segment[0].Bytes = H->Bytes;
    free(Image);
}

static
void
CheckDma(
    HARNESS* H,
    const UINT8* Data,
    const SOF_RI_IMAGE* Image)
{
    FWLOAD_CL_DMA Dma;
    SIZE_T Index, Expected, At;
    FWLOAD_STATUS Status;

    memset(&Dma, 0, sizeof(Dma));
    Dma.Virtual = H->Virtual;
    Dma.Capacity = H->Bytes;
    Dma.Segment = H->Segment;
    Dma.SegmentCount = 1;
    Dma.Bdl = H->Bdl;
    Dma.BdlCapacity = STREAM_BDL_ENTRIES;
    Dma.BdlBytes = STREAM_BDL_ENTRIES * sizeof(FWLOAD_BDL_ENTRY);

    /* one contiguous segment: one descriptor for the whole payload */
    Status = FwloadClDmaPrepare(&Dma, Data + Image->PayloadOffset, Image->PayloadBytes);
    CHECK_EQ(Status, FwloadOk);
    CHECK_EQ(Dma.FragmentCount, 1);
    CHECK_EQ(Dma.Bdl[0].Bytes, Image->PayloadBytes);
    CHECK_EQ(Dma.Bdl[0].AddressLow, 0x10000000U);
    CHECK_EQ(Dma.Bdl[0].AddressHigh, 0);
    CHECK_EQ(Dma.Bdl[0].InterruptOnCompletion, 0);
    CHECK_EQ(Dma.LastValidIndex, 0);
    CHECK_EQ(Dma.Spib, Image->PayloadBytes);
    CHECK_EQ(Dma.StreamTag, 1);
    CHECK_EQ(Dma.StreamFormat, 0x40);
    CHECK(memcmp(Dma.Virtual, Data + Image->PayloadOffset, Image->PayloadBytes) == 0);

    /* the 4 KiB rule as a caller option: one descriptor per page, more than the stream has */
    CHECK_EQ(FwloadClDmaEntryCount(Dma.Segment, 1, Image->PayloadBytes, 0x1000), 282);
    Dma.EntryBoundary = 0x1000;
    CHECK_EQ(FwloadClDmaPrepare(&Dma, Data + Image->PayloadOffset, Image->PayloadBytes), FwloadDmaTooManyFragments);
    CHECK_EQ(Dma.FragmentCount, 0);
    Dma.BdlCapacity = HARNESS_BDL_ENTRIES;
    Dma.BdlBytes = HARNESS_BDL_ENTRIES * sizeof(FWLOAD_BDL_ENTRY);
    CHECK_EQ(FwloadClDmaPrepare(&Dma, Data + Image->PayloadOffset, Image->PayloadBytes), FwloadOk);
    CHECK_EQ(Dma.FragmentCount, 282);
    CHECK_EQ(Dma.LastValidIndex, 281);
    for (Index = 0; Index < 282; Index++)
    {
        CHECK_EQ(Dma.Bdl[Index].Bytes, Index < 281 ? 0x1000 : Image->PayloadBytes - 281 * 0x1000);
        CHECK(((SIZE_T)Dma.Bdl[Index].AddressLow & 0xFFF) == 0);
        CHECK_EQ(Dma.Bdl[Index].AddressLow, 0x10000000U + (UINT32)Index * 0x1000);
    }
    /* a descriptor list whose byte room is too small is refused before anything is written */
    Dma.BdlBytes = 281 * 16;
    CHECK_EQ(FwloadClDmaPrepare(&Dma, Data + Image->PayloadOffset, Image->PayloadBytes), FwloadDmaTooManyFragments);
    Dma.BdlBytes = HARNESS_BDL_ENTRIES * sizeof(FWLOAD_BDL_ENTRY);

    /* three segments with an unaligned boundary: one descriptor per segment */
    H->Segment[0].Address = 0x10000000ULL;
    H->Segment[0].Bytes = 0x100000;
    H->Segment[1].Address = 0x20000500ULL;
    H->Segment[1].Bytes = 0x10000;
    H->Segment[2].Address = 0x30000000ULL;
    H->Segment[2].Bytes = 0x20000;
    H->SegmentCount = 3;
    Dma.SegmentCount = 3;
    Dma.EntryBoundary = 0;
    CHECK_EQ(FwloadClDmaEntryCount(H->Segment, 3, Image->PayloadBytes, 0), 3);
    Status = FwloadClDmaPrepare(&Dma, Data + Image->PayloadOffset, Image->PayloadBytes);
    CHECK_EQ(Status, FwloadOk);
    CHECK_EQ(Dma.FragmentCount, 3);
    CHECK_EQ(Dma.Bdl[0].Bytes, 0x100000);
    CHECK_EQ(Dma.Bdl[1].AddressLow, 0x20000500U);
    CHECK_EQ(Dma.Bdl[1].Bytes, 0x10000);
    CHECK_EQ(Dma.Bdl[2].AddressLow, 0x30000000U);
    CHECK_EQ(Dma.Bdl[2].Bytes, 0x11A000 - 0x110000);
    CHECK_EQ(Dma.LastValidIndex, 2);
    /* with the 4 KiB rule: an independent count over the three segments */
    Expected = 0;
    At = 0;
    while (At < Image->PayloadBytes)
    {
        UINT64 Address = At < 0x100000 ? 0x10000000ULL + At : At < 0x180000 ? 0x20000500ULL + At - 0x100000 : 0x30000000ULL + At - 0x180000;
        SIZE_T Chunk = 0x1000 - (SIZE_T)(Address & 0xFFF);

        if (Image->PayloadBytes - At < Chunk)
            Chunk = Image->PayloadBytes - At;
        At += Chunk;
        Expected++;
    }
    CHECK(Expected == 256 + 17 + 10);
    CHECK_EQ(FwloadClDmaEntryCount(H->Segment, 3, Image->PayloadBytes, 0x1000), Expected);
    Dma.EntryBoundary = 0x1000;
    Status = FwloadClDmaPrepare(&Dma, Data + Image->PayloadOffset, Image->PayloadBytes);
    CHECK_EQ(Status, FwloadOk);
    CHECK_EQ(Dma.FragmentCount, Expected);
    for (Index = 0; Index < Dma.FragmentCount; Index++)
        CHECK(((SIZE_T)Dma.Bdl[Index].AddressLow % 0x1000) + Dma.Bdl[Index].Bytes <= 0x1000);
    /* the model accepts the three-segment stream and boots from it, either way */
    Dma.EntryBoundary = 0;
    H->BdlCapacity = HARNESS_BDL_ENTRIES;
    HarnessBoot(H, Image, NULL);
    CHECK_EQ(FwloadBoot(H->Ctx), FwloadOk);
    CHECK_EQ(H->Dsp->DmaErrors, 0);
    H->EntryBoundary = 0x1000;
    HarnessBoot(H, Image, NULL);
    CHECK_EQ(FwloadBoot(H->Ctx), FwloadOk);
    CHECK_EQ(H->Dsp->DmaErrors, 0);
    H->BdlCapacity = STREAM_BDL_ENTRIES;
    HarnessBoot(H, Image, NULL);
    CHECK_EQ(FwloadBoot(H->Ctx), FwloadDmaTooManyFragments);
    CheckFailedBootIsOff(H, FwloadDmaTooManyFragments, FwloadStatePoweringUp);
    H->EntryBoundary = 0;
    H->BdlCapacity = HARNESS_BDL_ENTRIES;

    /* segments that do not cover the payload, or a buffer that is too small */
    H->Segment[2].Bytes = 0x8000;
    Dma.SegmentCount = 3;
    Dma.BdlCapacity = STREAM_BDL_ENTRIES;
    Dma.BdlBytes = STREAM_BDL_ENTRIES * sizeof(FWLOAD_BDL_ENTRY);
    CHECK_EQ(FwloadClDmaEntryCount(H->Segment, 3, Image->PayloadBytes, 0), 0);
    CHECK_EQ(FwloadClDmaPrepare(&Dma, Data + Image->PayloadOffset, Image->PayloadBytes), FwloadDmaTooSmall);
    H->Segment[2].Bytes = 0x20000;
    Dma.Capacity = 0x1000;
    CHECK_EQ(FwloadClDmaPrepare(&Dma, Data + Image->PayloadOffset, Image->PayloadBytes), FwloadDmaTooSmall);
    Dma.Capacity = Image->PayloadBytes - 1;
    CHECK_EQ(FwloadClDmaPrepare(&Dma, Data + Image->PayloadOffset, Image->PayloadBytes), FwloadDmaTooSmall);
    Dma.Capacity = H->Bytes;
    Dma.EntryBoundary = 0x1800;
    CHECK_EQ(FwloadClDmaPrepare(&Dma, Data + Image->PayloadOffset, Image->PayloadBytes), FwloadBadLength);
    Dma.EntryBoundary = 0;
    CHECK_EQ(FwloadClDmaPrepare(&Dma, NULL, 16), FwloadBadLength);
    CHECK_EQ(FwloadClDmaPrepare(&Dma, Data, 0), FwloadBadLength);
    CHECK_EQ(FwloadClDmaPrepare(NULL, Data, 16), FwloadBadLength);
    /* a descriptor list that breaks the stream contract is refused by the model */
    H->Segment[0].Address = 0x10000000ULL;
    H->Segment[0].Bytes = H->Bytes;
    H->SegmentCount = 1;
    Dma.SegmentCount = 1;
    Status = FwloadClDmaPrepare(&Dma, Data + Image->PayloadOffset, Image->PayloadBytes);
    CHECK_EQ(Status, FwloadOk);
    CHECK_EQ(FakeDspOps()->StreamSubmit(H->Dsp, &Dma), FwloadOk);
    Dma.Bdl[0].InterruptOnCompletion = 1;
    CHECK_EQ(FakeDspOps()->StreamSubmit(H->Dsp, &Dma), FwloadHardwareFault);
    CHECK_EQ(H->Dsp->DmaErrors & FAKE_DSP_DMA_IOC, FAKE_DSP_DMA_IOC);
    Dma.Bdl[0].InterruptOnCompletion = 0;
    Dma.StreamTag = 0;
    CHECK_EQ(FakeDspOps()->StreamSubmit(H->Dsp, &Dma), FwloadHardwareFault);
    CHECK_EQ(H->Dsp->DmaErrors & FAKE_DSP_DMA_TAG, FAKE_DSP_DMA_TAG);
    Dma.StreamTag = 1;
    Dma.StreamFormat = 0x20;
    CHECK_EQ(FakeDspOps()->StreamSubmit(H->Dsp, &Dma), FwloadHardwareFault);
    CHECK_EQ(H->Dsp->DmaErrors & FAKE_DSP_DMA_FORMAT, FAKE_DSP_DMA_FORMAT);
    Dma.StreamFormat = 0x40;
    Dma.Spib = 0;
    CHECK_EQ(FakeDspOps()->StreamSubmit(H->Dsp, &Dma), FwloadHardwareFault);
    CHECK_EQ(H->Dsp->DmaErrors & FAKE_DSP_DMA_SPIB, FAKE_DSP_DMA_SPIB);
    Dma.Spib = Dma.Bytes;
    Dma.LastValidIndex = 1;
    CHECK_EQ(FakeDspOps()->StreamSubmit(H->Dsp, &Dma), FwloadHardwareFault);
    CHECK_EQ(H->Dsp->DmaErrors & FAKE_DSP_DMA_LVI, FAKE_DSP_DMA_LVI);
    Dma.LastValidIndex = 0;
    Dma.Bdl[0].Bytes -= 16;
    CHECK_EQ(FakeDspOps()->StreamSubmit(H->Dsp, &Dma), FwloadHardwareFault);
    CHECK_EQ(H->Dsp->DmaErrors & FAKE_DSP_DMA_BYTES, FAKE_DSP_DMA_BYTES);
    Dma.Bdl[0].Bytes += 16;
    Dma.Bdl[0].AddressLow += 0x100;
    CHECK_EQ(FakeDspOps()->StreamSubmit(H->Dsp, &Dma), FwloadHardwareFault);
    CHECK(H->Dsp->DmaErrors & (FAKE_DSP_DMA_COVERAGE | FAKE_DSP_DMA_BYTES));
}

static
void
CheckNames(
    void)
{
    UINT32 Index, Other;

    for (Index = 0; Index <= (UINT32)FwloadBadState; Index++)
    {
        CHECK(FwloadStatusName((FWLOAD_STATUS)Index)[0] != 0);
        for (Other = Index + 1; Other <= (UINT32)FwloadBadState; Other++)
            CHECK(strcmp(FwloadStatusName((FWLOAD_STATUS)Index), FwloadStatusName((FWLOAD_STATUS)Other)) != 0);
    }
    for (Index = 0; Index <= (UINT32)FwloadStateFailed; Index++)
        CHECK(FwloadStateName((FWLOAD_STATE)Index)[0] != 0);
    CHECK_STR(FwloadStatusName(FwloadOk), "ok");
    CHECK_STR(FwloadStatusName(FwloadRomError), "ROM error");
    CHECK_STR(FwloadStatusName(FwloadFwCrash), "firmware crash");
    CHECK_STR(FwloadStatusName(FwloadVersionMismatch), "version mismatch");
    CHECK_STR(FwloadStatusName((FWLOAD_STATUS)999), "unknown");
    CHECK_STR(FwloadStateName(FwloadStateReady), "ready");
    CHECK_STR(FwloadStateName(FwloadStateFailed), "failed");
    CHECK_STR(FwloadStateName((FWLOAD_STATE)999), "unknown");
    CHECK(FwloadPlatformArlS() == FwloadPlatformArlS());
    CHECK_EQ(FwloadPlatformArlS()->RomStatus, 0x163200);
    CHECK_EQ(FwloadPlatformArlS()->IpcRequest, 0x73210);
    CHECK_EQ(FwloadPlatformArlS()->IpcReply, 0x73214);
    CHECK_EQ(FwloadPlatformArlS()->Window0Offset, 0x180000);
    CHECK_EQ(FwloadPlatformArlS()->UplinkOffset, 0x181000);
}

static
void
CheckOpsContext(
    HARNESS* H,
    const SOF_RI_IMAGE* Image)
{
    FWLOAD_CL_DMA Dma;
    UINT8 Buffer[16];
    PVOID Bad[2];
    UINT32 Before, Index;

    /* a boot hands the harness to every operation it makes */
    HarnessBoot(H, Image, NULL);
    CHECK(H->Ctx->Ops == &HarnessOps && H->Ctx->OpsContext == H);
    CHECK_EQ(FwloadBoot(H->Ctx), FwloadOk);
    CHECK_EQ(H->OpsSeen, HARNESS_OPS_BOOTED);
    /* a boot without the harness stops before its first operation */
    HarnessBoot(H, Image, NULL);
    H->Ctx->OpsContext = NULL;
    CHECK_EQ(FwloadBoot(H->Ctx), FwloadBadState);
    CHECK_EQ(H->Ctx->State, FwloadStateOff);
    CHECK_EQ(FwloadPowerDown(H->Ctx), FwloadOk);
    CHECK_EQ(ContextFaults, 0);
    CHECK(H->OpsSeen == 0 && H->Dsp->Clock == 0 && H->Dsp->WriteCount == 0 && H->Dsp->StreamSubmits == 0);
    /* the detector itself: with NULL and with the model as the context, every operation is refused */
    memset(&Dma, 0, sizeof(Dma));
    memset(Buffer, 0, sizeof(Buffer));
    Bad[0] = NULL;
    Bad[1] = H->Dsp;
    ContextFaultsExpected = 1;
    Before = ContextFaults;
    for (Index = 0; Index < 2; Index++)
    {
        CHECK_EQ(HarnessOps.Read32(Bad[Index], FWLOAD_BAR_DSP, 0), 0xFFFFFFFF);
        HarnessOps.Write32(Bad[Index], FWLOAD_BAR_DSP, 0, 0);
        CHECK(!HarnessOps.ReadMemory(Bad[Index], 0, Buffer, sizeof(Buffer)));
        CHECK_EQ(HarnessOps.NowUs(Bad[Index]), 0);
        HarnessOps.DelayUs(Bad[Index], 1000);
        CHECK_EQ(HarnessOps.StreamSubmit(Bad[Index], &Dma), FwloadBadState);
        CHECK_EQ(HarnessOps.StreamTrigger(Bad[Index], TRUE), FwloadBadState);
        HarnessOps.StreamRelease(Bad[Index]);
        HarnessOps.Log(Bad[Index], "refused");
    }
    ContextFaultsExpected = 0;
    CHECK_EQ(ContextFaults - Before, 2 * HOP_COUNT);
    CHECK(H->OpsSeen == 0 && H->Dsp->Clock == 0 && H->Dsp->WriteCount == 0 && H->Delays == 0 && H->LogCount == 0);
}

int
main(
    int argc,
    char** argv)
{
    HARNESS H;
    UINT8* Data;
    SOF_RI_IMAGE* Image = (SOF_RI_IMAGE*)calloc(1, sizeof(SOF_RI_IMAGE));
    PSOF_RI_MODULE Modules = (PSOF_RI_MODULE)calloc(64, sizeof(SOF_RI_MODULE));

    (void)argc;
    (void)argv;
    if (Image == NULL || Modules == NULL)
        return 2;
    HarnessInit(&H);
    Data = LoadRealImage();
    CheckRealImage(Data, Image, Modules);
    CheckHappyPath(&H, Image);
    CheckOpsContext(&H, Image);
    CheckTimeouts(&H, Image);
    CheckFwConfigReply(&H, Image);
    CheckStalledClock(&H, Image);
    CheckRomErrors(&H, Image);
    CheckFirmwareFaults(&H, Image);
    CheckInterruptPointer(&H, Image);
    CheckRetry(&H, Image);
    CheckCancellation(&H, Image);
    CheckBadPayload(&H, Data, Image);
    CheckDma(&H, Data, Image);
    CheckSyntheticImage(&H);
    CheckMalformedReal(Data);
    CheckNames();
    free(Data);
    free(Image);
    free(Modules);
    HarnessFrees(&H);
    /* the detector case's calls are the only ones ever made without the harness */
    CHECK_EQ(ContextFaults, 2 * HOP_COUNT);
    printf("fwload: %d checks, %d failures\n", Checks, Failures);
    return Failures ? 1 : 0;
}
