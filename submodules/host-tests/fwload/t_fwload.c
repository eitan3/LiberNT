/*
 * PROJECT:     LiberNT host-native tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     The firmware loader boots a DSP model from the real sof-arl-s.ri and from a synthetic image, times out or reports every fault the model injects, powers down from every state, never trusts a malformed image or descriptor list, bounds its buffer by the DMA capability the bus reports, and reaches the model only through the harness it was given
 * COPYRIGHT:   Copyright 2026 LiberNT contributors
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "shim.h"

#include "fake_dsp.h"
#include "fake_bus.h"
#include <pcidiag.h>
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
/* The Meteor Lake image the driver ships beside it (drivers/wdm/audio/intcdsp/firmware, read from there: the test does not duplicate the blob), intel-signed/sof-mtl.ri of the same package, the same size */
#define MTL_IMAGE_BYTES     1158164
#define MTL_IMAGE_FNV1A64   0x3755AE5808363040ULL

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
#define HOP_LOCK            9
#define HOP_UNLOCK          10
#define HOP_DSPPOWERGATING  11
#define HOP_COUNT           12
#define HARNESS_OPS_BOOTED  ((1u << HOP_COUNT) - 1)

typedef struct _HARNESS
{
    UINT64 Signature;
    FAKE_DSP* Dsp;
    FWLOAD_CONTEXT* Ctx;
    /* the operations the context was given: the table, without the power gating when NoGating asks for the driver built before it */
    FWLOAD_OPS Ops;
    BOOLEAN NoGating;
    UINT8* Virtual;
    SIZE_T Bytes;
    FWLOAD_DMA_SEGMENT Segment[8];
    SIZE_T SegmentCount;
    FWLOAD_BDL_ENTRY Bdl[HARNESS_BDL_ENTRIES];
    SIZE_T BdlCapacity;
    FWLOAD_TIMEOUTS Timeouts;
    UINT64 EntryBoundary;
    UINT32 CancelAfterDelays;
    UINT32 Delays;
    /* the DSP interrupts the line raises per clock advance while a message is pending: the model has no line, the harness plays it */
    UINT32 InterruptsPerDelay;
    /* what the gating operation answers the loader: FwloadOk unless a case makes the bus refuse the gating */
    FWLOAD_STATUS GatingStatus;
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
BOOLEAN
HarnessInterrupt(
    HARNESS* H);

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

/* the one injected operation: the clock advance, after which a cancellation can be requested and the line raises the interrupts the case asked for */
static
VOID
HDelayUs(
    PVOID Context,
    UINT32 Microseconds)
{
    HARNESS* H = HarnessEnter(Context, HOP_DELAYUS, "DelayUs");
    UINT32 Index;

    if (H == NULL)
        return;
    FakeDspOps()->DelayUs(H->Dsp, Microseconds);
    H->Delays++;
    if (H->CancelAfterDelays != 0 && H->Delays >= H->CancelAfterDelays)
        H->Ctx->Cancel = TRUE;
    for (Index = 0; Index < H->InterruptsPerDelay && FakeDspMessagePending(H->Dsp); Index++)
        HarnessInterrupt(H);
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

static
VOID
HLock(
    PVOID Context)
{
    HARNESS* H = HarnessEnter(Context, HOP_LOCK, "Lock");

    if (H != NULL)
        FakeDspOps()->Lock(H->Dsp);
}

static
VOID
HUnlock(
    PVOID Context)
{
    HARNESS* H = HarnessEnter(Context, HOP_UNLOCK, "Unlock");

    if (H != NULL)
        FakeDspOps()->Unlock(H->Dsp);
}

/* The platform's power gating of the DSP: the model's power event in the order beside the registers', and the window decodes from the moment the gating is taken off; the answer is the case's, FwloadOk for a bus whose write took */
static
FWLOAD_STATUS
HDspPowerGating(
    PVOID Context,
    BOOLEAN AllowGating)
{
    HARNESS* H = HarnessEnter(Context, HOP_DSPPOWERGATING, "DspPowerGating");

    if (H == NULL)
        return FwloadOk;
    FakeDspGating(H->Dsp, AllowGating);
    return H->GatingStatus;
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
    HLog,
    HLock,
    HUnlock,
    HDspPowerGating
};

/* The bus driver's interrupt service routine: the DSP callback under the interrupt lock, as hda_interrupt calls it */
static
BOOLEAN
HarnessInterrupt(
    HARNESS* H)
{
    BOOLEAN Claimed;

    HarnessOps.Lock(H);
    Claimed = FwloadInterruptService(H->Ctx);
    HarnessOps.Unlock(H);
    return Claimed;
}

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
HarnessBootPlatform(
    HARNESS* H,
    const FWLOAD_PLATFORM* Platform,
    const SOF_RI_IMAGE* Image,
    const FAKE_DSP_FAULTS* Faults)
{
    FWLOAD_CL_DMA Dma;

    FakeDspInit(H->Dsp, Platform, Faults);
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
    H->Ops = HarnessOps;
    if (H->NoGating)
        H->Ops.DspPowerGating = NULL;
    FwloadInitContext(H->Ctx, &H->Ops, H, Platform, Image, &Dma, &H->Timeouts);
    /* the driver keeps one context for the device and re-initializes it in place for every boot (R4): nothing of the boot before survives it */
    CHECK_EQ(H->Ctx->State, FwloadStateOff);
    CHECK_EQ(H->Ctx->Status, FwloadOk);
    CHECK(!H->Ctx->Cancel && !H->Ctx->InterruptStorm && !H->Ctx->InterruptsEnabled && !H->Ctx->UseInterrupts &&
        !H->Ctx->StreamSubmitted && !H->Ctx->SubsystemOn && !H->Ctx->CoreOn && !H->Ctx->GatingDisabled);
    CHECK_EQ(H->Ctx->InterruptCount, 0);
    CHECK_EQ(H->Ctx->StormCount, 0);
    H->Delays = 0;
    H->LogCount = 0;
    H->OpsSeen = 0;
    H->ClockStalled = FALSE;
    H->InterruptsPerDelay = 0;
    H->GatingStatus = FwloadOk;
}

/* The boot the cases run: the Arrow Lake-S record with its image */
static
void
HarnessBoot(
    HARNESS* H,
    const SOF_RI_IMAGE* Image,
    const FAKE_DSP_FAULTS* Faults)
{
    HarnessBootPlatform(H, FwloadPlatformForDevice(FWLOAD_DEV_ARL_S), Image, Faults);
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

/* The image holder's operations with counters: the read hands out a private copy, so that Free releases real memory */
typedef struct _IMAGE_HARNESS
{
    const UINT8* Source;
    SIZE_T ReadBytes;
    FWLOAD_STATUS ReadStatus;
    BOOLEAN AllocateFails;
    UINT32 Reads;
    UINT32 Allocates;
    UINT32 Frees;
} IMAGE_HARNESS;

static
FWLOAD_STATUS
ImageRead(
    PVOID Context,
    const UINT8** Data,
    SIZE_T* Bytes)
{
    IMAGE_HARNESS* I = Context;
    UINT8* Copy;

    I->Reads++;
    *Data = NULL;
    *Bytes = 0;
    if (I->ReadStatus != FwloadOk)
        return I->ReadStatus;
    Copy = (UINT8*)malloc(I->ReadBytes);
    if (Copy == NULL)
        exit(2);
    memcpy(Copy, I->Source, I->ReadBytes);
    *Data = Copy;
    *Bytes = I->ReadBytes;
    return FwloadOk;
}

static
PVOID
ImageAllocate(
    PVOID Context,
    SIZE_T Bytes)
{
    IMAGE_HARNESS* I = Context;

    I->Allocates++;
    return I->AllocateFails ? NULL : malloc(Bytes);
}

static
VOID
ImageFree(
    PVOID Context,
    PVOID Memory)
{
    IMAGE_HARNESS* I = Context;

    I->Frees++;
    free(Memory);
}

static const FWLOAD_IMAGE_OPS ImageOps =
{
    ImageRead,
    ImageAllocate,
    ImageFree
};

/*
 * R6 of the branch review: the firmware image is read once and kept. The holder reads on the first call and not on the second, two boots from one holder read once, a failed read
 * or parse leaves nothing resident so that the next call is the re-read on demand, and the release gives back the bytes and the module table
 */
static
void
CheckImageResident(
    HARNESS* H,
    const UINT8* Data)
{
    IMAGE_HARNESS I;
    FWLOAD_IMAGE Image;
    FWLOAD_STATUS Status;

    memset(&I, 0, sizeof(I));
    I.Source = Data;
    I.ReadBytes = REAL_IMAGE_BYTES;
    memset(&Image, 0, sizeof(Image));
    Status = FwloadImageEnsure(&Image, &ImageOps, &I);
    CHECK_EQ(Status, FwloadOk);
    CHECK_EQ(I.Reads, 1);
    CHECK_EQ(I.Allocates, 1);
    CHECK(Image.Data != NULL && Image.Data != Data);
    CHECK_EQ(Image.Bytes, REAL_IMAGE_BYTES);
    CHECK_STR(Image.Ri.Name, "ADSPFW");
    CHECK_EQ(Image.Ri.ModuleCount, 32);
    CHECK(Image.Modules != NULL && Image.Ri.Modules == Image.Modules);
    /* the table is read behind the same guard: a failed CHECK records the failure and the test goes on */
    CHECK(Image.Modules != NULL && strcmp(Image.Modules[0].Name, "BRNGUP") == 0 && strcmp(Image.Modules[31].Name, "PHASEVOC") == 0);
    /* the second call finds it resident: no read, no allocation, the same bytes and table */
    Status = FwloadImageEnsure(&Image, &ImageOps, &I);
    CHECK_EQ(Status, FwloadOk);
    CHECK_EQ(I.Reads, 1);
    CHECK_EQ(I.Allocates, 1);
    CHECK_EQ(I.Frees, 0);
    /* a read that would fail changes nothing about a resident image */
    I.ReadStatus = FwloadNoData;
    CHECK_EQ(FwloadImageEnsure(&Image, &ImageOps, &I), FwloadOk);
    CHECK_EQ(I.Reads, 1);
    I.ReadStatus = FwloadOk;

    /* two boots from the one holder, as the driver does across a sleep and a wake: the context takes the holder's parsed container, and the second boot reads nothing */
    HarnessBoot(H, &Image.Ri, NULL);
    CHECK_EQ(FwloadBoot(H->Ctx), FwloadOk);
    CHECK_EQ(H->Ctx->Dma.Bytes, Image.Ri.PayloadBytes);
    CHECK(memcmp(H->Ctx->Dma.Virtual, Image.Data + Image.Ri.PayloadOffset, 0x1000) == 0);
    CHECK_EQ(FwloadPowerDown(H->Ctx), FwloadOk);
    HarnessBoot(H, &Image.Ri, NULL);
    CHECK_EQ(FwloadBoot(H->Ctx), FwloadOk);
    CHECK_EQ(FwloadPowerDown(H->Ctx), FwloadOk);
    CHECK_EQ(I.Reads, 1);
    CHECK_EQ(H->Dsp->UnexpectedAccesses, 0);

    /* released: the bytes and the table go back, the holder is empty, and the next call reads on demand */
    FwloadImageFree(&Image, &ImageOps, &I);
    CHECK_EQ(I.Frees, 2);
    CHECK(Image.Data == NULL && Image.Bytes == 0 && Image.Modules == NULL);
    CHECK(Image.Ri.Data == NULL && Image.Ri.Modules == NULL);
    CHECK_EQ(FwloadImageEnsure(&Image, &ImageOps, &I), FwloadOk);
    CHECK_EQ(I.Reads, 2);
    CHECK_EQ(I.Frees, 2);
    FwloadImageFree(&Image, &ImageOps, &I);
    CHECK_EQ(I.Frees, 4);

    /* a read that fails leaves nothing behind and is not kept: the next call reads again */
    memset(&Image, 0, sizeof(Image));
    I.ReadStatus = FwloadNoData;
    CHECK_EQ(FwloadImageEnsure(&Image, &ImageOps, &I), FwloadNoData);
    CHECK(Image.Data == NULL && Image.Bytes == 0 && Image.Modules == NULL);
    CHECK_EQ(I.Frees, 4);
    I.ReadStatus = FwloadOk;
    CHECK_EQ(FwloadImageEnsure(&Image, &ImageOps, &I), FwloadOk);
    CHECK_EQ(I.Reads, 4);
    FwloadImageFree(&Image, &ImageOps, &I);
    CHECK_EQ(I.Frees, 6);

    /* a cut image is refused and released; the parsed facts stay for the log line */
    I.ReadBytes = 0x3000;
    Status = FwloadImageEnsure(&Image, &ImageOps, &I);
    CHECK_EQ(Status, FwloadBadLength);
    CHECK(Image.Data == NULL && Image.Modules == NULL && Image.Ri.Data == NULL);
    CHECK_EQ(Image.Ri.BadOffset, 0x2C38);
    CHECK_EQ(I.Reads, 5);
    CHECK_EQ(I.Allocates, 3);
    CHECK_EQ(I.Frees, 7);

    /* a module table that cannot be allocated is out of memory, with the bytes released */
    I.ReadBytes = REAL_IMAGE_BYTES;
    I.AllocateFails = TRUE;
    CHECK_EQ(FwloadImageEnsure(&Image, &ImageOps, &I), FwloadOutOfMemory);
    CHECK(Image.Data == NULL && Image.Modules == NULL);
    CHECK_EQ(I.Frees, 8);
    CHECK_STR(FwloadStatusName(FwloadOutOfMemory), "out of memory");
    I.AllocateFails = FALSE;
    CHECK_EQ(FwloadImageEnsure(&Image, &ImageOps, &I), FwloadOk);
    CHECK_EQ(I.Reads, 7);
    FwloadImageFree(&Image, &ImageOps, &I);
    CHECK_EQ(I.Frees, 10);
}

/* The documented write sequence of a cold boot, as the model recorded it */
static
void
CheckBootWrites(
    const HARNESS* H,
    UINT32 InterruptWrites)
{
    const FAKE_DSP* Dsp = H->Dsp;
    const FWLOAD_PLATFORM* Platform = FwloadPlatformForDevice(FWLOAD_DEV_ARL_S);
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
    /* the boot polls: the three enables are masked right after the power-up and again at ROM ready, when the interrupt pointer can first name its register, and never set; the platform's power gating of the DSP comes off before the first register write and is allowed again as the boot's last step (N15) */
    CHECK_EQ(H->Dsp->PowerOrderCount, 8);
    CHECK_EQ(H->Dsp->PowerOrder[0], FAKE_POWER_GATING_OFF);
    CHECK_EQ(H->Dsp->PowerOrder[1], FAKE_POWER_GPROCEN_ON);
    CHECK_EQ(H->Dsp->PowerOrder[2], FAKE_POWER_SUBSYSTEM_ON);
    CHECK_EQ(H->Dsp->PowerOrder[3], FAKE_POWER_INTERRUPTS_OFF);
    CHECK_EQ(H->Dsp->PowerOrder[4], FAKE_POWER_CORE_ON);
    CHECK_EQ(H->Dsp->PowerOrder[5], FAKE_POWER_INTERRUPTS_OFF);
    CHECK_EQ(H->Dsp->PowerOrder[6], FAKE_POWER_STREAM_RELEASE);
    CHECK_EQ(H->Dsp->PowerOrder[7], FAKE_POWER_GATING_ON);
    CHECK(!H->Ctx->GatingDisabled);
    CHECK(!FakeDspPowerEventSeen(H->Dsp, FAKE_POWER_CORE_OFF));
    CHECK_EQ(H->Dsp->InterruptEnable, 0);
    CHECK_EQ(H->Dsp->HostIpcEnable, 0);
    CHECK_EQ(H->Dsp->IpcControl, 0);
    CHECK(!H->Ctx->InterruptsEnabled);
    CHECK_EQ(H->Ctx->InterruptCount, 0);
    CheckBootWrites(H, 2);

    /* power down from ready: the interrupts, core, subsystem, GPROCEN, and nothing else; the boot allowed the gating again, so the power-down does not ask twice */
    Status = FwloadPowerDown(H->Ctx);
    CHECK_EQ(Status, FwloadOk);
    CHECK_EQ(H->Ctx->State, FwloadStateOff);
    /* the record of the state the power-down came from, for the driver's line (D4): ready, not the off the context is in now */
    CHECK_EQ(H->Ctx->PowerDownFrom, FwloadStateReady);
    CHECK_EQ(H->Ctx->Status, FwloadOk);
    CHECK_EQ(H->Ctx->CleanupStatus, FwloadOk);
    CHECK_EQ(H->Dsp->PowerOrderCount, 12);
    CHECK_EQ(H->Dsp->PowerOrder[8], FAKE_POWER_INTERRUPTS_OFF);
    CHECK_EQ(H->Dsp->PowerOrder[9], FAKE_POWER_CORE_OFF);
    CHECK_EQ(H->Dsp->PowerOrder[10], FAKE_POWER_SUBSYSTEM_OFF);
    CHECK_EQ(H->Dsp->PowerOrder[11], FAKE_POWER_GPROCEN_OFF);
    CHECK((H->Dsp->CoreControl & 0x100) == 0);
    CHECK((H->Dsp->SubsystemControl & 0x01000000) == 0);
    CHECK((H->Dsp->PpControl & (FWLOAD_PPCTL_GPROCEN | FWLOAD_PPCTL_PIE)) == 0);
    CHECK_EQ(H->Dsp->InterruptEnable, 0);
    CHECK_EQ(H->Dsp->HostIpcEnable, 0);
    CHECK_EQ(H->Dsp->IpcControl, 0);
    CHECK_EQ(H->Dsp->UnexpectedAccesses, 0);
    CHECK_EQ(H->Dsp->StreamReleases, 1);
    /* the three masks ran under the interrupt lock, one write per enable each, and nothing waited under it */
    CHECK_EQ(H->Dsp->InterruptWritesLocked, 9);
    CHECK_EQ(H->Dsp->InterruptWritesUnlocked, 0);
    CHECK_EQ(H->Dsp->InterruptLockRecursions, 0);
    CHECK_EQ(H->Dsp->InterruptLockDelays, 0);
    CheckBootWrites(H, 3);
    /* powering down twice is harmless, and the second call from off leaves the record alone; booting again after that boots again */
    CHECK_EQ(FwloadPowerDown(H->Ctx), FwloadOk);
    CHECK_EQ(H->Ctx->PowerDownFrom, FwloadStateReady);
    HarnessBoot(H, Image, NULL);
    CHECK_EQ(FwloadBoot(H->Ctx), FwloadOk);
    CHECK_EQ(FwloadPowerDown(H->Ctx), FwloadOk);
    CHECK_EQ(H->Ctx->PowerDownFrom, FwloadStateReady);
    CHECK_EQ(H->Dsp->UnexpectedAccesses, 0);

    /* the stream tag the caller was given goes into the purge request: tag 2 encodes as 1 << 9 */
    H->StreamTag = 2;
    HarnessBoot(H, Image, NULL);
    CHECK_EQ(FwloadBoot(H->Ctx), FwloadOk);
    CHECK_EQ(H->Ctx->Dma.StreamTag, 2);
    Request = FakeDspWriteFind(H->Dsp, FWLOAD_BAR_DSP, FwloadPlatformForDevice(FWLOAD_DEV_ARL_S)->IpcRequest, 0x80000000UL);
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
    const FWLOAD_PLATFORM* Platform = FwloadPlatformForDevice(FWLOAD_DEV_ARL_S);
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

    /* D3 of the p2b3 simulator rerun: a tuple length that is not a multiple of four desynchronised the walk and a version-only reply ended in boot ok with 0-byte mailboxes */
    Faults.TupleFault = FAKE_DSP_TUPLES_LENGTH_THREE;
    HarnessBoot(H, Image, &Faults);
    CHECK_EQ(FwloadBoot(H->Ctx), FwloadIpcBadReply);
    CheckFailedBootIsOff(H, FwloadIpcBadReply, FwloadStateConfiguring);
    CHECK(H->Ctx->FwConfig.BadTuple);
    CHECK_EQ(H->Ctx->FwConfig.BadTupleType, 1);
    CHECK_EQ(H->Ctx->FwConfig.BadTupleOffset, 16);
    CHECK_EQ(H->Ctx->FwConfig.TupleCount, 1);
    /* a tuple without a value, and one of a type the firmware does not define */
    Faults.TupleFault = FAKE_DSP_TUPLES_LENGTH_ZERO;
    HarnessBoot(H, Image, &Faults);
    CHECK_EQ(FwloadBoot(H->Ctx), FwloadIpcBadReply);
    CheckFailedBootIsOff(H, FwloadIpcBadReply, FwloadStateConfiguring);
    CHECK(H->Ctx->FwConfig.BadTuple && H->Ctx->FwConfig.BadTupleType == 1 && H->Ctx->FwConfig.BadTupleOffset == 16);
    Faults.TupleFault = FAKE_DSP_TUPLES_UNKNOWN_TYPE;
    HarnessBoot(H, Image, &Faults);
    CHECK_EQ(FwloadBoot(H->Ctx), FwloadIpcBadReply);
    CheckFailedBootIsOff(H, FwloadIpcBadReply, FwloadStateConfiguring);
    CHECK(H->Ctx->FwConfig.BadTuple && H->Ctx->FwConfig.BadTupleType == 0x700 && H->Ctx->FwConfig.BadTupleOffset == 16);
    /* truncated-reply: the version alone ends the list before the mailboxes */
    Faults.TupleFault = FAKE_DSP_TUPLES_VERSION_ONLY;
    HarnessBoot(H, Image, &Faults);
    CHECK_EQ(FwloadBoot(H->Ctx), FwloadTruncated);
    CheckFailedBootIsOff(H, FwloadTruncated, FwloadStateConfiguring);
    CHECK(!H->Ctx->FwConfig.BadTuple);
    CHECK(H->Ctx->FwConfig.HasVersion && !H->Ctx->FwConfig.HasDownlinkMailbox);
    CHECK_EQ(H->Ctx->FwConfig.TupleCount, 1);
    /* mailboxes of zero bytes are not a configuration the host can use; a list that ends before the uplink size is cut short */
    Faults.TupleFault = FAKE_DSP_TUPLES_ZERO_MAILBOXES;
    HarnessBoot(H, Image, &Faults);
    CHECK_EQ(FwloadBoot(H->Ctx), FwloadIpcBadReply);
    CheckFailedBootIsOff(H, FwloadIpcBadReply, FwloadStateConfiguring);
    CHECK(!H->Ctx->FwConfig.BadTuple);
    CHECK(H->Ctx->FwConfig.HasDownlinkMailbox && H->Ctx->FwConfig.DownlinkMailboxBytes == 0);
    CHECK(H->Ctx->FwConfig.HasUplinkMailbox && H->Ctx->FwConfig.UplinkMailboxBytes == 0);
    CHECK_EQ(H->Ctx->FwConfig.TupleCount, 8);
    Faults.TupleFault = FAKE_DSP_TUPLES_NO_MAILBOXES;
    HarnessBoot(H, Image, &Faults);
    CHECK_EQ(FwloadBoot(H->Ctx), FwloadTruncated);
    CheckFailedBootIsOff(H, FwloadTruncated, FwloadStateConfiguring);
    CHECK(!H->Ctx->FwConfig.HasDownlinkMailbox && !H->Ctx->FwConfig.HasUplinkMailbox);
    CHECK_EQ(H->Ctx->FwConfig.TupleCount, 6);
    /* a mailbox larger than the one the host maps (FWLOAD_MAILBOX_BYTES) would let the transport's blocks run past the window: refused, either one */
    Faults.TupleFault = FAKE_DSP_TUPLES_OVERSIZED_DOWNLINK;
    HarnessBoot(H, Image, &Faults);
    CHECK_EQ(FwloadBoot(H->Ctx), FwloadIpcBadReply);
    CheckFailedBootIsOff(H, FwloadIpcBadReply, FwloadStateConfiguring);
    CHECK(!H->Ctx->FwConfig.BadTuple);
    CHECK(H->Ctx->FwConfig.HasDownlinkMailbox && H->Ctx->FwConfig.DownlinkMailboxBytes == FWLOAD_MAILBOX_BYTES + 4);
    CHECK(H->Ctx->FwConfig.HasUplinkMailbox && H->Ctx->FwConfig.UplinkMailboxBytes == FWLOAD_MAILBOX_BYTES);
    CHECK_EQ(H->Ctx->FwConfig.TupleCount, 8);
    Faults.TupleFault = FAKE_DSP_TUPLES_OVERSIZED_UPLINK;
    HarnessBoot(H, Image, &Faults);
    CHECK_EQ(FwloadBoot(H->Ctx), FwloadIpcBadReply);
    CheckFailedBootIsOff(H, FwloadIpcBadReply, FwloadStateConfiguring);
    CHECK(!H->Ctx->FwConfig.BadTuple);
    CHECK(H->Ctx->FwConfig.HasDownlinkMailbox && H->Ctx->FwConfig.DownlinkMailboxBytes == FWLOAD_MAILBOX_BYTES);
    CHECK(H->Ctx->FwConfig.HasUplinkMailbox && H->Ctx->FwConfig.UplinkMailboxBytes == 2 * FWLOAD_MAILBOX_BYTES);
    CHECK_EQ(H->Ctx->FwConfig.TupleCount, 8);
    /* the model's own reply, with the tuple the loader does not read, is still accepted */
    Faults.TupleFault = FAKE_DSP_TUPLES_GOOD;
    HarnessBoot(H, Image, &Faults);
    CHECK_EQ(FwloadBoot(H->Ctx), FwloadOk);
    CHECK(!H->Ctx->FwConfig.BadTuple);
    CHECK_EQ(FwloadPowerDown(H->Ctx), FwloadOk);

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
    const FWLOAD_PLATFORM* Platform = FwloadPlatformForDevice(FWLOAD_DEV_ARL_S);
    /* zero, an odd address, the last address of the pointer's field, and the bus's all ones */
    static const UINT32 NoRegister[] = { 0, 1, 0x1D01, 0x1FFFFF, 0xFFFFFFFF };
    FAKE_DSP_FAULTS Faults;
    const FAKE_DSP_WRITE* IntenWrite;
    const FAKE_DSP_WRITE* RomAck;
    static FAKE_DSP_ACCESS Access[4096];
    UINT32 Index, EnableWrites, RomAckIndex, IntenIndex, StatusPoll;

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
    /* the two masks of the boot, three writes each */
    CHECK_EQ(EnableWrites, 6);
    CHECK_EQ(H->Dsp->InterruptPointerReads, 2);
    CHECK_EQ(H->Dsp->InterruptPointerReadsUnpowered, 0);
    CHECK_EQ(H->Dsp->UnexpectedAccesses, 0);
    CHECK_EQ(FwloadPowerDown(H->Ctx), FwloadOk);
    CHECK_EQ(H->Dsp->InterruptPointerReads, 3);
    CHECK_EQ(H->Dsp->UnexpectedAccesses, 0);

    /* N5 of the simulator round: all ones until the core is powered, the first mask of the boot cannot reach the enable, and the enable is one a BIOS left set; the mask at ROM ready, the first the pointer answers, clears it: one write to it in the whole boot, powered, after the ROM's acknowledgement and before the firmware is loaded */
    Faults = *FakeDspDefaultFaults();
    Faults.PointerNeedsCore = TRUE;
    HarnessBoot(H, Image, &Faults);
    H->Dsp->InterruptEnable = 1;
    FakeDspAccessBegin(H->Dsp, Access, sizeof(Access) / sizeof(Access[0]));
    CHECK_EQ(FwloadBoot(H->Ctx), FwloadOk);
    CHECK_EQ(H->Dsp->InterruptEnable, 0);
    CHECK_EQ(H->Dsp->HostIpcEnable, 0);
    CHECK_EQ(H->Dsp->IpcControl, 0);
    CHECK_EQ(FakeDspWriteCount(H->Dsp, FWLOAD_BAR_DSP, FAKE_DSP_INTERRUPT_POINTER), 1);
    IntenWrite = FakeDspWriteFind(H->Dsp, FWLOAD_BAR_DSP, FAKE_DSP_INTERRUPT_POINTER, 0);
    RomAck = FakeDspWriteFind(H->Dsp, FWLOAD_BAR_DSP, Platform->IpcReply, FWLOAD_IPC_DONE);
    CHECK(IntenWrite != NULL && IntenWrite->Powered && (IntenWrite->Value & 1) == 0);
    CHECK(RomAck != NULL && IntenWrite > RomAck);
    /* in the access log: the write comes after the ROM's acknowledgement and after the ROM's own status was polled to ready, and before the first status poll of the firmware load */
    RomAckIndex = IntenIndex = StatusPoll = 0;
    for (Index = 0; Index < H->Dsp->AccessCount; Index++)
    {
        const FAKE_DSP_ACCESS* Step = &Access[Index];

        if (Step->Op == FAKE_DSP_ACCESS_WRITE32 && Step->Offset == Platform->IpcReply && (Step->Value & FWLOAD_IPC_DONE) && RomAckIndex == 0)
            RomAckIndex = Index + 1;
        else if (Step->Op == FAKE_DSP_ACCESS_WRITE32 && Step->Offset == FAKE_DSP_INTERRUPT_POINTER)
            IntenIndex = Index + 1;
        else if (Step->Op == FAKE_DSP_ACCESS_READ32 && Step->Offset == Platform->RomStatus && RomAckIndex != 0)
            StatusPoll = Index + 1;
    }
    CHECK(RomAckIndex != 0 && IntenIndex != 0 && StatusPoll != 0);
    /* the last status read before the write is the ROM's ready poll; the write precedes the load's polls, which end at the last one */
    CHECK(RomAckIndex < IntenIndex && IntenIndex < StatusPoll);
    /* the pointer was read once unpowered, by the mask of the power-up, and once by the mask at ROM ready */
    CHECK_EQ(H->Dsp->InterruptPointerReads, 2);
    CHECK_EQ(H->Dsp->InterruptPointerReadsUnpowered, 1);
    CHECK_EQ(FakeDspWriteCount(H->Dsp, FWLOAD_BAR_DSP, Platform->HostIpcInterruptEnable), 2);
    CHECK_EQ(FakeDspWriteCount(H->Dsp, FWLOAD_BAR_DSP, Platform->IpcControl), 2);
    CHECK_EQ(H->Dsp->UnexpectedAccesses, 0);
    CHECK_EQ(FwloadPowerDown(H->Ctx), FwloadOk);
    CHECK_EQ(FakeDspWriteCount(H->Dsp, FWLOAD_BAR_DSP, FAKE_DSP_INTERRUPT_POINTER), 2);
    CHECK_EQ(H->Dsp->InterruptPointerReads, 3);
    FakeDspAccessBegin(H->Dsp, NULL, 0);
    /* in that shape too the enables are asked for after the mask at ROM ready, so the BIOS's bit is cleared before the line is used */
    HarnessBoot(H, Image, &Faults);
    H->Dsp->InterruptEnable = 1;
    H->Ctx->UseInterrupts = TRUE;
    CHECK_EQ(FwloadBoot(H->Ctx), FwloadOk);
    CHECK_EQ(H->Dsp->InterruptEnable, 1);
    CHECK_EQ(FakeDspWriteCount(H->Dsp, FWLOAD_BAR_DSP, FAKE_DSP_INTERRUPT_POINTER), 2);
    CHECK_EQ(FwloadPowerDown(H->Ctx), FwloadOk);
    CHECK_EQ(H->Dsp->InterruptEnable, 0);

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

    /* a pointer that names no 32-bit register once the DSP is powered: the host IPC interrupt enable is left alone, the other two are masked, nothing is written through the pointer and the polled boot is the model's */
    for (Index = 0; Index < sizeof(NoRegister) / sizeof(NoRegister[0]); Index++)
    {
        Faults = *FakeDspDefaultFaults();
        Faults.InterruptPointerOverride = TRUE;
        Faults.InterruptPointer = NoRegister[Index];
        HarnessBoot(H, Image, &Faults);
        CHECK_EQ(FwloadBoot(H->Ctx), FwloadOk);
        CHECK_EQ(H->Ctx->InterruptCount, 0);
        CHECK_EQ(FakeDspWriteCount(H->Dsp, FWLOAD_BAR_DSP, FAKE_DSP_INTERRUPT_POINTER), 0);
        CHECK_EQ(FakeDspWriteCount(H->Dsp, FWLOAD_BAR_DSP, NoRegister[Index] & 0x1FFFFF), 0);
        /* no register was written through the pointer, so the model never saw the interrupt go off */
        CHECK(!FakeDspPowerEventSeen(H->Dsp, FAKE_POWER_INTERRUPTS_OFF));
        /* the two masks of the boot reach the two enables the pointer does not name */
        CHECK_EQ(FakeDspWriteCount(H->Dsp, FWLOAD_BAR_DSP, Platform->HostIpcInterruptEnable), 2);
        CHECK_EQ(FakeDspWriteCount(H->Dsp, FWLOAD_BAR_DSP, Platform->IpcControl), 2);
        CHECK_EQ(H->Dsp->HostIpcEnable, 0);
        CHECK_EQ(H->Dsp->IpcControl, 0);
        CHECK_EQ(H->Dsp->UnexpectedAccesses, 0);
        CHECK_EQ(FwloadPowerDown(H->Ctx), FwloadOk);
        CHECK_EQ(FakeDspWriteCount(H->Dsp, FWLOAD_BAR_DSP, FAKE_DSP_INTERRUPT_POINTER), 0);
        CHECK_EQ(H->Dsp->UnexpectedAccesses, 0);
    }

    /* an interrupt that is asked for with a pointer that names no register cannot be enabled: the guard has nothing to gate the line with */
    Faults = *FakeDspDefaultFaults();
    Faults.InterruptPointerOverride = TRUE;
    Faults.InterruptPointer = NoRegister[1];
    HarnessBoot(H, Image, &Faults);
    H->Ctx->UseInterrupts = TRUE;
    CHECK_EQ(FwloadBoot(H->Ctx), FwloadHardwareFault);
    CHECK_EQ(H->Ctx->FailedState, FwloadStateRomInit);
    CheckDspIsOff(H);
    CHECK_EQ(FakeDspWriteCount(H->Dsp, FWLOAD_BAR_DSP, FAKE_DSP_INTERRUPT_POINTER), 0);

    /* a pointer inside BAR4 that names no register the model has: the loader cannot tell and writes the mask there; all ones never read back as cleared, so the boot stops at once where an enable would have read back as set */
    Faults = *FakeDspDefaultFaults();
    Faults.InterruptPointerOverride = TRUE;
    Faults.InterruptPointer = 0x1234;
    HarnessBoot(H, Image, &Faults);
    CHECK_EQ(FwloadBoot(H->Ctx), FwloadInterruptTimeout);
    CHECK_EQ(H->Ctx->State, FwloadStateFailed);
    CHECK_EQ(H->Ctx->FailedState, FwloadStatePoweringUp);
    CHECK_EQ(H->Ctx->CleanupStatus, FwloadInterruptTimeout);
    CHECK(H->Ctx->BootUs >= 101000 && H->Ctx->BootUs < 103000);
    CHECK_EQ(FakeDspWriteCount(H->Dsp, FWLOAD_BAR_DSP, 0x1234), 2);
    CHECK_EQ(FakeDspWriteCount(H->Dsp, FWLOAD_BAR_DSP, FAKE_DSP_INTERRUPT_POINTER), 0);
    CHECK_EQ(FakeDspWriteCount(H->Dsp, FWLOAD_BAR_DSP, Platform->HostIpcInterruptEnable), 0);
    CHECK_EQ(FakeDspWriteCount(H->Dsp, FWLOAD_BAR_DSP, Platform->IpcControl), 0);
    CHECK((H->Dsp->SubsystemControl & 0x01000000) == 0);
    CHECK((H->Dsp->PpControl & FWLOAD_PPCTL_GPROCEN) == 0);
    CHECK_EQ(FwloadPowerDown(H->Ctx), FwloadInterruptTimeout);
    CHECK_EQ(H->Ctx->State, FwloadStateOff);
    CHECK_STR(FwloadStatusName(FwloadInterruptTimeout), "interrupt enable timeout");
}

/*
 * R2 of the branch review: the first boot polls. The IPC interrupts are masked right after the power-up and never enabled; the service routine masks the three enables on a DSP
 * interrupt and counts the ones no poll separates, and after FWLOAD_INTERRUPT_STORM_LIMIT of them it stops claiming the line and the boot's waits end with FwloadInterruptStorm.
 * Every passive-level write to the enables runs under the interrupt lock, and nothing waits under it
 */
static
void
CheckInterruptService(
    HARNESS* H,
    const SOF_RI_IMAGE* Image)
{
    const FWLOAD_PLATFORM* Platform = FwloadPlatformForDevice(FWLOAD_DEV_ARL_S);
    FWLOAD_INTERRUPT_STATE State;
    UINT32 Writes, Index;

    /* a message pending with no boot: nothing is claimed while nothing is pending, then each claim masks the three enables under the lock the bus holds, the first one reads the registers for the driver's log, and the guard trips after FWLOAD_INTERRUPT_STORM_LIMIT */
    HarnessBoot(H, Image, NULL);
    CHECK(!HarnessInterrupt(H));
    CHECK_EQ(H->Ctx->InterruptCount, 0);
    /* the line the driver prints for the first boot on silicon reads the same registers */
    FwloadReadInterruptState(H->Ctx, &State);
    CHECK_EQ(State.Pointer, FAKE_DSP_INTERRUPT_POINTER);
    CHECK_EQ(State.Enable, 0);
    CHECK_EQ(State.HostIpcEnable, 0);
    CHECK_EQ(State.IpcControl, 0);
    CHECK_EQ(State.SubsystemControl, 0);
    H->Dsp->IpcTarget = FWLOAD_IPC_BUSY | SOFIPC4_FW_READY_PRIMARY;
    CHECK(HarnessInterrupt(H));
    CHECK_EQ(H->Ctx->InterruptCount, 1);
    CHECK_EQ(H->Ctx->StormCount, 1);
    CHECK_EQ(H->Ctx->FirstInterrupt.Pointer, FAKE_DSP_INTERRUPT_POINTER);
    CHECK_EQ(H->Ctx->FirstInterrupt.Enable, 0);
    CHECK_EQ(H->Ctx->FirstInterrupt.HostIpcEnable, 0);
    CHECK_EQ(H->Ctx->FirstInterrupt.IpcControl, 0);
    CHECK_EQ(H->Ctx->FirstInterrupt.SubsystemControl, 0);
    for (Index = 2; Index < FWLOAD_INTERRUPT_STORM_LIMIT; Index++)
        CHECK(HarnessInterrupt(H));
    CHECK_EQ(H->Ctx->InterruptCount, FWLOAD_INTERRUPT_STORM_LIMIT - 1);
    CHECK(!H->Ctx->InterruptStorm);
    CHECK(HarnessInterrupt(H));
    CHECK(H->Ctx->InterruptStorm);
    CHECK_EQ(H->Ctx->InterruptCount, FWLOAD_INTERRUPT_STORM_LIMIT);
    /* disarmed: the line is not claimed again and no register is touched */
    Writes = H->Dsp->WriteCount;
    CHECK(!HarnessInterrupt(H));
    CHECK_EQ(H->Ctx->InterruptCount, FWLOAD_INTERRUPT_STORM_LIMIT);
    CHECK_EQ(H->Dsp->WriteCount, Writes);
    CHECK_EQ(FakeDspWriteCount(H->Dsp, FWLOAD_BAR_DSP, FAKE_DSP_INTERRUPT_POINTER), FWLOAD_INTERRUPT_STORM_LIMIT);
    CHECK_EQ(FakeDspWriteCount(H->Dsp, FWLOAD_BAR_DSP, Platform->HostIpcInterruptEnable), FWLOAD_INTERRUPT_STORM_LIMIT);
    CHECK_EQ(FakeDspWriteCount(H->Dsp, FWLOAD_BAR_DSP, Platform->IpcControl), FWLOAD_INTERRUPT_STORM_LIMIT);
    CHECK_EQ(H->Dsp->InterruptWritesLocked, 3 * FWLOAD_INTERRUPT_STORM_LIMIT);
    CHECK_EQ(H->Dsp->InterruptWritesUnlocked, 0);
    CHECK_EQ(H->Dsp->InterruptLockRecursions, 0);
    CHECK_EQ(H->Dsp->InterruptLockDelays, 0);
    CHECK_EQ(H->Dsp->InterruptLockUnderflows, 0);
    CHECK_EQ(H->Dsp->InterruptLockDepth, 0);
    /* the guard is a property of the context: a fresh one is armed again */
    HarnessBoot(H, Image, NULL);
    CHECK(!H->Ctx->InterruptStorm);
    CHECK(!HarnessInterrupt(H));

    /* one interrupt per message during a poll-only boot: the ROM's acknowledgement, FW_READY and the reply each raise the line once, the poll consumes the message and the count starts again: no storm, the boot is the model's */
    HarnessBoot(H, Image, NULL);
    H->InterruptsPerDelay = 1;
    CHECK_EQ(FwloadBoot(H->Ctx), FwloadOk);
    H->InterruptsPerDelay = 0;
    CHECK_EQ(H->Ctx->InterruptCount, 3);
    CHECK_EQ(H->Ctx->StormCount, 0);
    CHECK(!H->Ctx->InterruptStorm);
    CHECK_EQ(H->Dsp->InterruptEnable, 0);
    CHECK_EQ(H->Dsp->HostIpcEnable, 0);
    CHECK_EQ(H->Dsp->IpcControl, 0);
    /* powered and masked: what the driver's step lines show */
    FwloadReadInterruptState(H->Ctx, &State);
    CHECK_EQ(State.Pointer, FAKE_DSP_INTERRUPT_POINTER);
    CHECK_EQ(State.Enable, 0);
    CHECK_EQ(State.HostIpcEnable, 0);
    CHECK_EQ(State.IpcControl, 0);
    CHECK_EQ(State.SubsystemControl, 0x01010000);
    CHECK_EQ(H->Dsp->UnexpectedAccesses, 0);
    CHECK_EQ(FwloadPowerDown(H->Ctx), FwloadOk);

    /* FWLOAD_INTERRUPT_STORM_LIMIT interrupts short of the limit per message: as many as the limit less one, three times, never trip it */
    HarnessBoot(H, Image, NULL);
    H->InterruptsPerDelay = FWLOAD_INTERRUPT_STORM_LIMIT - 1;
    CHECK_EQ(FwloadBoot(H->Ctx), FwloadOk);
    H->InterruptsPerDelay = 0;
    CHECK_EQ(H->Ctx->InterruptCount, 3 * (FWLOAD_INTERRUPT_STORM_LIMIT - 1));
    CHECK(!H->Ctx->InterruptStorm);
    CHECK_EQ(FwloadPowerDown(H->Ctx), FwloadOk);

    /* FWLOAD_INTERRUPT_STORM_LIMIT in a row, here on the ROM's acknowledgement: the guard trips and the routine stops claiming the line; the waits that are satisfied at once still pass, the next one that is not ends the boot as an interrupt storm, and the DSP is off */
    HarnessBoot(H, Image, NULL);
    H->InterruptsPerDelay = FWLOAD_INTERRUPT_STORM_LIMIT;
    CHECK_EQ(FwloadBoot(H->Ctx), FwloadInterruptStorm);
    H->InterruptsPerDelay = 0;
    CHECK_EQ(H->Ctx->InterruptCount, FWLOAD_INTERRUPT_STORM_LIMIT);
    CheckFailedBootIsOff(H, FwloadInterruptStorm, FwloadStateLoading);

    /* with a clock that does not advance the storm still wins: it is read before the poll bound */
    HarnessBoot(H, Image, NULL);
    H->InterruptsPerDelay = FWLOAD_INTERRUPT_STORM_LIMIT;
    H->ClockStalled = TRUE;
    CHECK_EQ(FwloadBoot(H->Ctx), FwloadInterruptStorm);
    H->InterruptsPerDelay = 0;
    CheckFailedBootIsOff(H, FwloadInterruptStorm, FwloadStateLoading);

    /* FWLOAD_CONTEXT.UseInterrupts, the later phases' mode: the enables are set once the ROM is ready and stay set until the power-down; every write to them is under the lock */
    HarnessBoot(H, Image, NULL);
    H->Ctx->UseInterrupts = TRUE;
    CHECK_EQ(FwloadBoot(H->Ctx), FwloadOk);
    CHECK(H->Ctx->InterruptsEnabled);
    CHECK_EQ(H->Dsp->InterruptEnable, 1);
    CHECK_EQ(H->Dsp->HostIpcEnable, 1);
    CHECK_EQ(H->Dsp->IpcControl, 3);
    /* the gating off first and allowed again last, as in every boot */
    CHECK_EQ(H->Dsp->PowerOrderCount, 8);
    CHECK_EQ(H->Dsp->PowerOrder[0], FAKE_POWER_GATING_OFF);
    CHECK_EQ(H->Dsp->PowerOrder[1], FAKE_POWER_GPROCEN_ON);
    CHECK_EQ(H->Dsp->PowerOrder[3], FAKE_POWER_INTERRUPTS_OFF);
    CHECK_EQ(H->Dsp->PowerOrder[4], FAKE_POWER_CORE_ON);
    CHECK_EQ(H->Dsp->PowerOrder[5], FAKE_POWER_INTERRUPTS_OFF);
    CHECK_EQ(H->Dsp->PowerOrder[6], FAKE_POWER_STREAM_RELEASE);
    CHECK_EQ(H->Dsp->PowerOrder[7], FAKE_POWER_GATING_ON);
    /* the two masks and the enable: three writes each */
    CHECK_EQ(H->Dsp->InterruptWritesLocked, 9);
    CHECK_EQ(H->Dsp->InterruptWritesUnlocked, 0);
    CHECK_EQ(H->Dsp->InterruptLockRecursions, 0);
    CHECK_EQ(H->Dsp->InterruptLockDelays, 0);
    CheckBootWrites(H, 3);
    /* in that mode too the routine's mask and the poll's acknowledging are serialized: one interrupt per message still boots */
    H->Dsp->IpcTarget = FWLOAD_IPC_BUSY | SOFIPC4_FW_READY_PRIMARY;
    CHECK(HarnessInterrupt(H));
    CHECK_EQ(H->Ctx->InterruptCount, 1);
    CHECK_EQ(H->Dsp->InterruptEnable, 0);
    CHECK_EQ(H->Dsp->HostIpcEnable, 0);
    CHECK_EQ(H->Dsp->IpcControl, 0);
    CHECK_EQ(H->Dsp->InterruptWritesLocked, 12);
    CHECK_EQ(FwloadPowerDown(H->Ctx), FwloadOk);
    CHECK_EQ(H->Dsp->InterruptWritesLocked, 15);
    CHECK_EQ(H->Dsp->InterruptWritesUnlocked, 0);
    CHECK_EQ(H->Dsp->UnexpectedAccesses, 0);
}

/*
 * R5 of the branch review: a firmware that streams notifications faster than the polls drain them. Every wait ends at its bounds on every iteration, so the storm is a timeout with a running
 * clock and a stalled clock without one, with the notifications drained bounded by the polls; the model stops a storm after FAKE_DSP_STORM_MAX so that a loader without the bound still ends somewhere, with that many drained
 */
static
void
CheckNotificationStorm(
    HARNESS* H,
    const SOF_RI_IMAGE* Image)
{
    FAKE_DSP_FAULTS Faults;

    /* instead of FW_READY: the FW_READY budget ends the boot after its polls, each of which drained one notification */
    Faults = *FakeDspDefaultFaults();
    Faults.NotificationStorm = FAKE_DSP_STORM_INSTEAD_FW_READY;
    HarnessBoot(H, Image, &Faults);
    CHECK_EQ(FwloadBoot(H->Ctx), FwloadTimeoutFwReady);
    CHECK_EQ(H->Ctx->State, FwloadStateFailed);
    CHECK_EQ(H->Ctx->FailedState, FwloadStateFwReady);
    CHECK_EQ(H->Ctx->CleanupStatus, FwloadOk);
    CHECK(H->Ctx->BootUs >= H->Timeouts.FwReadyUs + 4000 && H->Ctx->BootUs < H->Timeouts.FwReadyUs + 14000);
    /* one notification posted at the moment FW_READY was due and one per poll of the budget: the model's cap is never reached */
    CHECK_EQ(H->Dsp->StormPosted, (UINT32)(H->Timeouts.FwReadyUs / FWLOAD_POLL_INTERVAL_US) + 1);
    CHECK_EQ(H->Dsp->StormPosted, H->Dsp->Notifications);
    CHECK_EQ(H->Dsp->Replies, 0);
    CHECK_EQ(H->Dsp->IpcRequests, 0);
    CHECK_EQ(H->Dsp->UnexpectedAccesses, 0);
    CHECK_EQ(H->Dsp->StreamSubmits, H->Dsp->StreamReleases);
    CHECK((H->Dsp->SubsystemControl & 0x01000000) == 0);
    CHECK((H->Dsp->PpControl & FWLOAD_PPCTL_GPROCEN) == 0);
    CHECK_EQ(FwloadPowerDown(H->Ctx), FwloadOk);

    /* the same with a clock that does not advance: the poll bound ends it, at twice the polls */
    HarnessBoot(H, Image, &Faults);
    H->ClockStalled = TRUE;
    CHECK_EQ(FwloadBoot(H->Ctx), FwloadClockStalled);
    CHECK_EQ(H->Ctx->FailedState, FwloadStateFwReady);
    CHECK_EQ(H->Dsp->StormPosted, 2 * (UINT32)(H->Timeouts.FwReadyUs / FWLOAD_POLL_INTERVAL_US) + 1);
    CHECK_EQ(H->Dsp->UnexpectedAccesses, 0);
    CHECK_EQ(FwloadPowerDown(H->Ctx), FwloadOk);

    /* after FW_READY, while the reply is waited for and never comes: the IPC reply budget, one notification posted when FW_READY was acknowledged, one per poll and one left waiting */
    Faults = *FakeDspDefaultFaults();
    Faults.NotificationStorm = FAKE_DSP_STORM_AFTER_FW_READY;
    Faults.NeverIpcReply = TRUE;
    HarnessBoot(H, Image, &Faults);
    CHECK_EQ(FwloadBoot(H->Ctx), FwloadTimeoutIpcReply);
    CHECK_EQ(H->Ctx->State, FwloadStateFailed);
    CHECK_EQ(H->Ctx->FailedState, FwloadStateConfiguring);
    CHECK_EQ(H->Ctx->CleanupStatus, FwloadOk);
    CHECK(H->Ctx->BootUs >= H->Timeouts.IpcReplyUs + 4500 && H->Ctx->BootUs < H->Timeouts.IpcReplyUs + 14500);
    CHECK_EQ(H->Delays, H->Timeouts.IpcReplyUs / FWLOAD_POLL_INTERVAL_US + 9);
    CHECK_EQ(H->Dsp->StormPosted, (UINT32)(H->Timeouts.IpcReplyUs / FWLOAD_POLL_INTERVAL_US) + 2);
    CHECK_EQ(H->Dsp->IpcRequests, 1);
    CHECK_EQ(H->Dsp->UnexpectedAccesses, 0);
    CHECK_EQ(FwloadPowerDown(H->Ctx), FwloadOk);

    /* a storm beside a reply that does come: the boot is the model's, unchanged; the reply wait's one poll drains the first storm notification and the second is left waiting */
    Faults = *FakeDspDefaultFaults();
    Faults.NotificationStorm = FAKE_DSP_STORM_AFTER_FW_READY;
    HarnessBoot(H, Image, &Faults);
    CHECK_EQ(FwloadBoot(H->Ctx), FwloadOk);
    CHECK_EQ(H->Ctx->BootUs, 5000);
    CHECK_EQ(H->Delays, 8);
    CHECK_EQ(H->Dsp->StormPosted, 2);
    CHECK_EQ(H->Ctx->InterruptCount, 0);
    CHECK_EQ(FwloadPowerDown(H->Ctx), FwloadOk);
    CHECK_EQ(H->Dsp->UnexpectedAccesses, 0);
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
    /* the mask of the power-up, one after each missed request, and the one at ROM ready */
    CheckBootWrites(H, 4);
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

/* A descriptor list over one buffer: the entries run from the buffer's address without a gap, none is empty or longer than a descriptor's length field, none crosses the boundary, and together they are the payload */
static
void
CheckDescriptorList(
    const FWLOAD_CL_DMA* Dma,
    UINT64 Address,
    UINT64 Boundary,
    SIZE_T Bytes)
{
    SIZE_T Index;
    UINT64 At = Address, Total = 0;

    CHECK_EQ(Dma->Bytes, Bytes);
    for (Index = 0; Index < Dma->FragmentCount; Index++)
    {
        const FWLOAD_BDL_ENTRY* Entry = &Dma->Bdl[Index];
        UINT64 EntryAddress = (UINT64)Entry->AddressLow | ((UINT64)Entry->AddressHigh << 32);

        CHECK_EQ(EntryAddress, At);
        CHECK(Entry->Bytes != 0);
        CHECK(Entry->Bytes <= 0xFFFFFFFFULL);
        if (Boundary != 0)
            CHECK((EntryAddress % Boundary) + Entry->Bytes <= Boundary);
        CHECK_EQ(Entry->InterruptOnCompletion, 0);
        At += Entry->Bytes;
        Total += Entry->Bytes;
    }
    CHECK_EQ(Total, Bytes);
    CHECK_EQ(At, Address + Bytes);
}

/* R11 of the branch review: the shared rows over one segment, counted for every row and built for those that fit a small buffer */
static
void
CheckBoundaryRows(
    const UINT8* Payload)
{
    static UINT8 Copy[0x9000];
    FWLOAD_DMA_SEGMENT Segment;
    FWLOAD_BDL_ENTRY Bdl[8];
    FWLOAD_CL_DMA Dma;
    SIZE_T Row;
    UINT64 Boundary;

    for (Row = 0; Row < sizeof(FwloadCaseBoundaries) / sizeof(FwloadCaseBoundaries[0]); Row++)
    {
        Boundary = FwloadCaseBoundaries[Row].Boundary;
        Segment.Address = FwloadCaseBoundaries[Row].Address;
        Segment.Bytes = (SIZE_T)FwloadCaseBoundaries[Row].SegmentBytes;
        CHECK_EQ(FwloadClDmaEntryCount(&Segment, 1, (SIZE_T)FwloadCaseBoundaries[Row].PayloadBytes, Boundary),
                 FwloadCaseBoundaries[Row].Entries);
        if (FwloadCaseBoundaries[Row].PayloadBytes > sizeof(Copy))
            continue;

        memset(&Dma, 0, sizeof(Dma));
        memset(Copy, 0xA5, sizeof(Copy));
        Dma.Virtual = Copy;
        Dma.Capacity = sizeof(Copy);
        Dma.Segment = &Segment;
        Dma.SegmentCount = 1;
        Dma.Bdl = Bdl;
        Dma.BdlCapacity = sizeof(Bdl) / sizeof(Bdl[0]);
        Dma.BdlBytes = sizeof(Bdl);
        Dma.EntryBoundary = Boundary;
        CHECK_EQ(FwloadClDmaPrepare(&Dma, Payload, (SIZE_T)FwloadCaseBoundaries[Row].PayloadBytes), FwloadOk);
        CHECK_EQ(Dma.FragmentCount, FwloadCaseBoundaries[Row].Entries);
        CheckDescriptorList(&Dma, Segment.Address, Boundary, (SIZE_T)FwloadCaseBoundaries[Row].PayloadBytes);
        CHECK(memcmp(Copy, Payload, (SIZE_T)FwloadCaseBoundaries[Row].PayloadBytes) == 0);
        /* the first row: the segment that straddles the boundary is cut exactly there */
        if (Row == 0)
        {
            CHECK_EQ(Bdl[0].AddressLow, 0xFFFF8000U);
            CHECK_EQ(Bdl[0].AddressHigh, 0);
            CHECK_EQ(Bdl[0].Bytes, 0x8000);
            CHECK_EQ(Bdl[1].AddressLow, 0);
            CHECK_EQ(Bdl[1].AddressHigh, 1);
            CHECK_EQ(Bdl[1].Bytes, 0x1000);
        }
    }
}

/* R8 of the branch review: the bus interface binds by size, in version 1 or 2, and the 64-bit DMA capability it reports bounds the code-loader buffer */
static
void
CheckBusBound(void)
{
    const SIZE_T V1Bytes = RTL_SIZEOF_THROUGH_FIELD(ADSP_BUS_INTERFACE, ReleaseInterruptLock);
    FAKE_BUS Bus;
    ADSP_BUS_INTERFACE Queried;

    /* version 2, a controller that addresses 64 bits: the whole range */
    FakeBusInit(&Bus, 2, TRUE);
    CHECK_EQ(Bus.Interface.Size, sizeof(ADSP_BUS_INTERFACE));
    CHECK_EQ(Bus.Interface.Version, 2);
    RtlZeroMemory(&Queried, sizeof(Queried));
    CHECK_EQ(FakeBusQueryInterface(&Bus, &Queried, sizeof(Queried), ADSP_BUS_INTERFACE_VERSION), STATUS_SUCCESS);
    CHECK(IntcDspBusComplete(&Queried));
    CHECK_EQ(IntcDspDmaHighBound(&Queried), MAXULONG64);
    CHECK_EQ(Bus.InfoCalls, 1);

    /* version 2, a controller without 64-bit DMA: below 4 GiB */
    FakeBusInit(&Bus, 2, FALSE);
    RtlZeroMemory(&Queried, sizeof(Queried));
    CHECK_EQ(FakeBusQueryInterface(&Bus, &Queried, sizeof(Queried), ADSP_BUS_INTERFACE_VERSION), STATUS_SUCCESS);
    CHECK_EQ(IntcDspDmaHighBound(&Queried), 0xFFFFFFFFULL);

    /* version 2, a capability query that fails: below 4 GiB */
    Bus.InfoStatus = STATUS_NO_SUCH_DEVICE;
    RtlZeroMemory(&Queried, sizeof(Queried));
    CHECK_EQ(FakeBusQueryInterface(&Bus, &Queried, sizeof(Queried), ADSP_BUS_INTERFACE_VERSION), STATUS_SUCCESS);
    CHECK_EQ(IntcDspDmaHighBound(&Queried), 0xFFFFFFFFULL);

    /* an older bus: version 1 answers the version 2 query with its shorter structure, reports no capability, and the buffer stays below 4 GiB */
    FakeBusInit(&Bus, 1, TRUE);
    CHECK_EQ(Bus.Interface.Size, V1Bytes);
    memset(&Queried, 0xA5, sizeof(Queried));
    CHECK_EQ(FakeBusQueryInterface(&Bus, &Queried, sizeof(Queried), ADSP_BUS_INTERFACE_VERSION), STATUS_SUCCESS);
    CHECK_EQ(Queried.Size, V1Bytes);
    CHECK_EQ(Queried.Version, 1);
    CHECK(IntcDspBusComplete(&Queried));
    /* what the caller's buffer holds beyond the bus's structure is not the bus's, and is never followed */
    CHECK(Queried.GetPlatformInfo != NULL);
    CHECK_EQ(IntcDspDmaHighBound(&Queried), 0xFFFFFFFFULL);
    CHECK_EQ(Bus.InfoCalls, 0);

    /* the query's rule: the bus may not exceed the caller's buffer or version, and a refused query writes nothing */
    FakeBusInit(&Bus, 2, TRUE);
    memset(&Queried, 0x5A, sizeof(Queried));
    CHECK_EQ(FakeBusQueryInterface(&Bus, &Queried, (ULONG)V1Bytes, ADSP_BUS_INTERFACE_VERSION), STATUS_INVALID_BUFFER_SIZE);
    CHECK_EQ(FakeBusQueryInterface(&Bus, &Queried, sizeof(Queried), 1), STATUS_INVALID_BUFFER_SIZE);
    CHECK(((const UINT8*)&Queried)[0] == 0x5A && ((const UINT8*)&Queried)[sizeof(Queried) - 1] == 0x5A);

    /* a bus shorter than version 1, or without one of its members, does not bind */
    RtlZeroMemory(&Queried, sizeof(Queried));
    CHECK(!IntcDspBusComplete(&Queried));
    FakeBusInit(&Bus, 2, TRUE);
    CHECK_EQ(FakeBusQueryInterface(&Bus, &Queried, sizeof(Queried), ADSP_BUS_INTERFACE_VERSION), STATUS_SUCCESS);
    Queried.Size = (USHORT)(V1Bytes - 1);
    CHECK(!IntcDspBusComplete(&Queried));
    Queried.Size = sizeof(ADSP_BUS_INTERFACE);
    Queried.TriggerDSP = NULL;
    CHECK(!IntcDspBusComplete(&Queried));

    /* (N15 c) the bus's two registers: the member changes bit 1 of CGCTL (0x48) and bit 2 of PGCTL (0x44) alone, in both directions, through the rule the bus itself compiles (dspgating.h) */
    FakeBusInit(&Bus, 2, TRUE);
    CHECK_EQ(FakeBusQueryInterface(&Bus, &Queried, sizeof(Queried), ADSP_BUS_INTERFACE_VERSION), STATUS_SUCCESS);
    /* (f) a bus made without the member, as one built before it: the member is empty and the driver's binding says so */
    CHECK(Queried.Size >= RTL_SIZEOF_THROUGH_FIELD(ADSP_BUS_INTERFACE, SetDspPowerGating));
    CHECK(Queried.SetDspPowerGating == NULL);
    CHECK(!IntcDspBusPowerGating(&Queried));
    FakeBusPowerGatingEnable(&Bus);
    RtlZeroMemory(&Queried, sizeof(Queried));
    CHECK_EQ(FakeBusQueryInterface(&Bus, &Queried, sizeof(Queried), ADSP_BUS_INTERFACE_VERSION), STATUS_SUCCESS);
    CHECK(IntcDspBusPowerGating(&Queried));
    /* as the bus's start leaves them: the TCSEL low bits clear, so ADSPPGD clear, the controller's clock gate set, the DSP's clear */
    Bus.Config.Pgctl = 0x1234FF78;
    Bus.Config.Cgctl = 0x9ABC00C1;
    CHECK_EQ(Queried.SetDspPowerGating(Queried.Context, FALSE), STATUS_SUCCESS);
    CHECK_EQ(Bus.Config.Pgctl, 0x1234FF78 | HDA_PGCTL_ADSPPGD);
    CHECK_EQ(Bus.Config.Cgctl, 0x9ABC00C1 & ~HDA_CGCTL_ADSPDCGE);
    CHECK_EQ(Bus.GatingCalls, 1);
    CHECK_EQ(Bus.GatingAllowCalls, 0);
    /* allowed again: bit 1 of 0x48 set, bit 2 of 0x44 cleared, and nothing else */
    Bus.Config.Pgctl = 0x1234FF7C;
    Bus.Config.Cgctl = 0x9ABC00C3;
    CHECK_EQ(Queried.SetDspPowerGating(Queried.Context, TRUE), STATUS_SUCCESS);
    CHECK_EQ(Bus.Config.Pgctl, 0x1234FF7C & ~HDA_PGCTL_ADSPPGD);
    CHECK_EQ(Bus.Config.Cgctl, 0x9ABC00C3 | HDA_CGCTL_ADSPDCGE);
    CHECK_EQ(Bus.GatingCalls, 2);
    CHECK_EQ(Bus.GatingAllowCalls, 1);
    /* (f) the member empty with the size saying it is there, and a bus whose structure ends before it: neither binds, and no boot is tried on it (intcdsp.c IntcDspBoot) */
    Queried.SetDspPowerGating = NULL;
    CHECK(!IntcDspBusPowerGating(&Queried));
    FakeBusInit(&Bus, 2, TRUE);
    FakeBusPowerGatingEnable(&Bus);
    FakeBusTruncate(&Bus, RTL_SIZEOF_THROUGH_FIELD(ADSP_BUS_INTERFACE, DetachDmaBuffer));
    RtlZeroMemory(&Queried, sizeof(Queried));
    CHECK_EQ(FakeBusQueryInterface(&Bus, &Queried, sizeof(Queried), ADSP_BUS_INTERFACE_VERSION), STATUS_SUCCESS);
    CHECK(IntcDspBusStreams(&Queried));
    CHECK(!IntcDspBusPowerGating(&Queried));
}

/*
 * The gating that does not change (N15-22, the owner's machine: PGCTL read 0 after the write) and the
 * block of a boot whose window does not answer. (a) the bus's member, over the fake's config space
 * through its two PCI members: the dword form read back as written is success with nothing else
 * written, and the four readbacks are kept for the driver; a byte that does not take a write gets the
 * byte form once, and one that still does not is STATUS_UNSUCCESSFUL; a byte that takes one byte
 * alone is rescued by the byte form; (b) a word the bus does not move whole is the protocol error
 * with nothing more written; (c) the boot ends at a gating that fails, before the processing enable
 * and the power request, the gating allowed again on the way out, and the processing enable's
 * register is kept for the block when the boot went past it; (d) the block reads nothing and writes
 * nothing for any status but the dead window's; over a config space it reads the fourteen dwords,
 * finds the power-management capability or says the list has none, and its write control reads as
 * written and restores its byte, or says the write did not land.
 */
static
void
CheckGatingReadback(
    HARNESS* H,
    const SOF_RI_IMAGE* Image)
{
    FAKE_BUS Bus;
    ADSP_BUS_INTERFACE Queried;
    HDA_DSP_GATING_READBACK Readback;
    BUS_INTERFACE_STANDARD Pci;
    INTCDSP_DEAD_WINDOW Block;
    FAKE_DSP_FAULTS Faults;
    CHAR Text[12];
    UINT32 Index;

    /* (a) as written by the dword form: two words read, two written, two read back, the byte form not tried */
    FakeBusInit(&Bus, 2, TRUE);
    FakeBusPowerGatingEnable(&Bus);
    RtlZeroMemory(&Queried, sizeof(Queried));
    CHECK_EQ(FakeBusQueryInterface(&Bus, &Queried, sizeof(Queried), ADSP_BUS_INTERFACE_VERSION), STATUS_SUCCESS);
    CHECK(Queried.Size >= RTL_SIZEOF_THROUGH_FIELD(ADSP_BUS_INTERFACE, GetDspPowerGatingReadback));
    CHECK(Queried.GetDspPowerGatingReadback != NULL);
    Bus.Config.Pgctl = 0x1234FF78;
    Bus.Config.Cgctl = 0x9ABC00C1;
    CHECK_EQ(Queried.SetDspPowerGating(Queried.Context, FALSE), STATUS_SUCCESS);
    CHECK_EQ(Bus.Config.Pgctl, 0x1234FF78 | HDA_PGCTL_ADSPPGD);
    CHECK_EQ(Bus.Config.Cgctl, 0x9ABC00C1 & ~(UINT32)HDA_CGCTL_ADSPDCGE);
    memset(&Readback, 0xA5, sizeof(Readback));
    Queried.GetDspPowerGatingReadback(Queried.Context, &Readback);
    CHECK(!Readback.ByteForm);
    CHECK_EQ(Readback.PgctlAfterDword, 0x1234FF78 | HDA_PGCTL_ADSPPGD);
    CHECK_EQ(Readback.CgctlAfterDword, 0x9ABC00C1 & ~(UINT32)HDA_CGCTL_ADSPDCGE);
    CHECK_EQ(Readback.PgctlAfterByte, 0);
    CHECK_EQ(Readback.CgctlAfterByte, 0);
    CHECK_EQ(Bus.PciReads, 4);
    CHECK_EQ(Bus.PciWrites, 2);
    /* allowed again: the reverse, as written */
    CHECK_EQ(Queried.SetDspPowerGating(Queried.Context, TRUE), STATUS_SUCCESS);
    CHECK_EQ(Bus.Config.Pgctl, 0x1234FF78);
    CHECK_EQ(Bus.Config.Cgctl, 0x9ABC00C1 | HDA_CGCTL_ADSPDCGE);
    CHECK(!Bus.GatingReadback.ByteForm);

    /* the byte at 0x44, which carries ADSPPGD, does not take a write: the byte form is tried once, the four readbacks are kept, the status is the bus's own */
    FakeBusPciNoStick(&Bus, HDA_PCIREG_PGCTL);
    Bus.Config.Pgctl = 0x00000000;
    Bus.Config.Cgctl = 0x00000002;
    CHECK_EQ(Queried.SetDspPowerGating(Queried.Context, FALSE), STATUS_UNSUCCESSFUL);
    CHECK_EQ(Bus.Config.Pgctl, 0x00000000);
    CHECK_EQ(Bus.Config.Cgctl, 0x00000000);
    Queried.GetDspPowerGatingReadback(Queried.Context, &Readback);
    CHECK(Readback.ByteForm);
    CHECK_EQ(Readback.PgctlAfterDword, 0x00000000);
    CHECK_EQ(Readback.CgctlAfterDword, 0x00000000);
    CHECK_EQ(Readback.PgctlAfterByte, 0x00000000);
    CHECK_EQ(Readback.CgctlAfterByte, 0x00000000);
    CHECK_EQ(Bus.PciReads, 14);
    CHECK_EQ(Bus.PciWrites, 8);
    /* the byte at 0x48, which carries ADSPDCGE, does not take one: allowed again, the gating cannot be put back */
    FakeBusInit(&Bus, 2, TRUE);
    FakeBusPowerGatingEnable(&Bus);
    RtlZeroMemory(&Queried, sizeof(Queried));
    CHECK_EQ(FakeBusQueryInterface(&Bus, &Queried, sizeof(Queried), ADSP_BUS_INTERFACE_VERSION), STATUS_SUCCESS);
    FakeBusPciNoStick(&Bus, HDA_PCIREG_CGCTL);
    Bus.Config.Pgctl = 0x00000004;
    Bus.Config.Cgctl = 0x00000000;
    CHECK_EQ(Queried.SetDspPowerGating(Queried.Context, TRUE), STATUS_UNSUCCESSFUL);
    CHECK_EQ(Bus.Config.Pgctl, 0x00000000);
    CHECK_EQ(Bus.Config.Cgctl, 0x00000000);
    CHECK(Bus.GatingReadback.ByteForm);
    CHECK_EQ(Bus.GatingReadback.PgctlAfterByte, 0x00000000);

    /* the byte takes one byte alone: the dword form is refused, the byte form lands, and the call is a success that says so */
    FakeBusInit(&Bus, 2, TRUE);
    FakeBusPowerGatingEnable(&Bus);
    RtlZeroMemory(&Queried, sizeof(Queried));
    CHECK_EQ(FakeBusQueryInterface(&Bus, &Queried, sizeof(Queried), ADSP_BUS_INTERFACE_VERSION), STATUS_SUCCESS);
    FakeBusPciByteOnly(&Bus, HDA_PCIREG_PGCTL);
    Bus.Config.Pgctl = 0x00000100;
    Bus.Config.Cgctl = 0x00000000;
    CHECK_EQ(Queried.SetDspPowerGating(Queried.Context, FALSE), STATUS_SUCCESS);
    CHECK_EQ(Bus.Config.Pgctl, 0x00000104);
    CHECK_EQ(Bus.Config.Cgctl, 0x00000000);
    CHECK(Bus.GatingReadback.ByteForm);
    CHECK_EQ(Bus.GatingReadback.PgctlAfterDword, 0x00000100);
    CHECK_EQ(Bus.GatingReadback.PgctlAfterByte, 0x00000104);

    /* (b) a word the bus does not move whole: the protocol error, the second word not read and nothing written; and one that does not write: nothing read back */
    FakeBusInit(&Bus, 2, TRUE);
    FakeBusPowerGatingEnable(&Bus);
    RtlZeroMemory(&Queried, sizeof(Queried));
    CHECK_EQ(FakeBusQueryInterface(&Bus, &Queried, sizeof(Queried), ADSP_BUS_INTERFACE_VERSION), STATUS_SUCCESS);
    FakeBusPciNoRead(&Bus, HDA_PCIREG_PGCTL);
    CHECK_EQ(Queried.SetDspPowerGating(Queried.Context, FALSE), STATUS_DEVICE_PROTOCOL_ERROR);
    CHECK_EQ(Bus.PciReads, 2);
    CHECK_EQ(Bus.PciWrites, 0);
    CHECK(!Bus.GatingReadback.ByteForm);
    CHECK_EQ(Bus.GatingReadback.PgctlAfterDword, 0);

    /* (c) the bus refuses the gating: the boot ends there, before the processing enable and the power request, with no step logged */
    HarnessBoot(H, Image, NULL);
    H->GatingStatus = FwloadDspWindowDead;
    CHECK_EQ(FwloadBoot(H->Ctx), FwloadDspWindowDead);
    CHECK_EQ(H->Ctx->Status, FwloadDspWindowDead);
    CHECK_EQ(H->Ctx->State, FwloadStateFailed);
    CHECK_EQ(H->Ctx->FailedState, FwloadStatePoweringUp);
    CHECK_EQ(H->LogCount, 0);
    CHECK(!H->Ctx->PpCtlEnabled && !H->Ctx->SubsystemOn && !H->Ctx->CoreOn && !H->Ctx->StreamSubmitted);
    CHECK(!H->Ctx->GatingDisabled);
    CHECK(H->Dsp->PowerOrderCount >= 2);
    CHECK_EQ(H->Dsp->PowerOrder[0], FAKE_POWER_GATING_OFF);
    CHECK_EQ(H->Dsp->PowerOrder[H->Dsp->PowerOrderCount - 1], FAKE_POWER_GATING_ON);
    for (Index = 1; Index + 1 < H->Dsp->PowerOrderCount; Index++)
        CHECK(H->Dsp->PowerOrder[Index] != FAKE_POWER_SUBSYSTEM_ON && H->Dsp->PowerOrder[Index] != FAKE_POWER_GPROCEN_ON &&
            H->Dsp->PowerOrder[Index] != FAKE_POWER_CORE_ON);
    for (Index = 0; Index < H->Dsp->WriteCount; Index++)
        CHECK(!(H->Dsp->WriteLog[Index].Bar == FWLOAD_BAR_PP && (H->Dsp->WriteLog[Index].Value & FWLOAD_PPCTL_GPROCEN)) &&
            !(H->Dsp->WriteLog[Index].Bar == FWLOAD_BAR_DSP && H->Dsp->WriteLog[Index].Offset == H->Ctx->Platform->SubsystemControl));
    CHECK_EQ(H->Dsp->PpControl & FWLOAD_PPCTL_GPROCEN, 0);
    CHECK_EQ(FwloadPowerDown(H->Ctx), FwloadOk);
    H->GatingStatus = FwloadOk;

    /* the processing enable's register as the loader read it right after its update: kept for the block, with the policy's PIE, also for a boot that ends at the power-up */
    HarnessBoot(H, Image, NULL);
    CHECK(!H->Ctx->PpCtlEnabled);
    CHECK_EQ(FwloadBoot(H->Ctx), FwloadOk);
    CHECK(H->Ctx->PpCtlEnabled);
    CHECK_EQ(H->Ctx->PpCtlAfterEnable, FWLOAD_PPCTL_GPROCEN);
    CHECK_EQ(FwloadPowerDown(H->Ctx), FwloadOk);
    HarnessBoot(H, Image, NULL);
    H->Ctx->UseInterrupts = TRUE;
    CHECK_EQ(FwloadBoot(H->Ctx), FwloadOk);
    CHECK_EQ(H->Ctx->PpCtlAfterEnable, FWLOAD_PPCTL_GPROCEN | FWLOAD_PPCTL_PIE);
    CHECK_EQ(FwloadPowerDown(H->Ctx), FwloadOk);
    Faults = *FakeDspDefaultFaults();
    Faults.WindowDead = TRUE;
    HarnessBoot(H, Image, &Faults);
    CHECK_EQ(FwloadBoot(H->Ctx), FwloadDspWindowDead);
    CHECK(H->Ctx->PpCtlEnabled);
    CHECK_EQ(H->Ctx->PpCtlAfterEnable, FWLOAD_PPCTL_GPROCEN);
    CHECK_EQ(FwloadPowerDown(H->Ctx), FwloadInterruptTimeout);

    /* (d) the block: nothing read and nothing written for any status but the dead window's; the one write it makes must never reach a healthy boot */
    FakeBusInit(&Bus, 2, TRUE);
    FakeBusPciInterface(&Bus, &Pci);
    CHECK(!IntcDspDeadWindowCollect(FwloadOk, &Pci, &Block));
    CHECK(!IntcDspDeadWindowCollect(FwloadTimeoutPowerUp, &Pci, &Block));
    CHECK(!IntcDspDeadWindowCollect(FwloadRomError, &Pci, &Block));
    CHECK(!IntcDspDeadWindowCollect(FwloadCancelled, &Pci, &Block));
    CHECK_EQ(Bus.PciReads, 0);
    CHECK_EQ(Bus.PciWrites, 0);

    /* over a config space: the fourteen dwords through the bus, the capability behind another one, the control that sticks and restores */
    Bus.PciConfig[0x00] = 0x86;
    Bus.PciConfig[0x01] = 0x80;
    Bus.PciConfig[0x02] = 0x50;
    Bus.PciConfig[0x03] = 0x7F;
    Bus.PciConfig[0x04] = 0x06;
    Bus.PciConfig[0x0C] = 0x10;
    Bus.PciConfig[0x0D] = 0x20;
    Bus.PciConfig[0x34] = 0x60;
    Bus.PciConfig[0x40] = 0xAA;
    Bus.PciConfig[0x41] = 0x55;
    Bus.Config.Pgctl = 0x00000004;
    Bus.Config.Cgctl = 0x00000000;
    Bus.PciConfig[0x4C] = 0x01;
    Bus.PciConfig[0x50] = 0x02;
    Bus.PciConfig[0x54] = 0x03;
    Bus.PciConfig[0x58] = 0x04;
    Bus.PciConfig[0x5C] = 0x05;
    Bus.PciConfig[0x60] = 0x11;
    Bus.PciConfig[0x61] = 0x70;
    Bus.PciConfig[0x64] = 0x99;
    Bus.PciConfig[0x70] = 0x01;
    Bus.PciConfig[0x74] = 0x03;
    Bus.PciConfig[0x75] = 0x08;
    memset(&Block, 0xA5, sizeof(Block));
    CHECK(IntcDspDeadWindowCollect(FwloadDspWindowDead, &Pci, &Block));
    CHECK_EQ(Block.Config[0], 0x7F508086);
    CHECK_EQ(Block.Config[1], 0x00000006);
    CHECK_EQ(Block.Config[2], 0x00002010);
    CHECK_EQ(Block.Config[3], 0x00000060);
    CHECK_EQ(Block.Config[4], 0x000055AA);
    CHECK_EQ(Block.Config[5], 0x00000004);
    CHECK_EQ(Block.Config[6], 0x00000000);
    CHECK_EQ(Block.Config[7], 0x00000001);
    CHECK_EQ(Block.Config[8], 0x00000002);
    CHECK_EQ(Block.Config[9], 0x00000003);
    CHECK_EQ(Block.Config[10], 0x00000004);
    CHECK_EQ(Block.Config[11], 0x00000005);
    CHECK_EQ(Block.Config[12], 0x00007011);
    CHECK_EQ(Block.Config[13], 0x00000099);
    for (Index = 0; Index < INTCDSP_PCI_DIAG_COUNT; Index++)
        CHECK(Block.ConfigRead[Index]);
    CHECK_EQ(IntcDspPciDiagOffset(0), 0x00);
    CHECK_EQ(IntcDspPciDiagOffset(3), 0x34);
    CHECK_EQ(IntcDspPciDiagOffset(4), 0x40);
    CHECK_EQ(IntcDspPciDiagOffset(13), 0x64);
    CHECK(Block.Pm.Present);
    CHECK_EQ(Block.Pm.Pointer, 0x60);
    CHECK_EQ(Block.Pm.Control, 0x0803);
    CHECK_EQ(Block.Pm.State, 3);
    CHECK(Block.Control.Done);
    CHECK_EQ(Block.Control.Original, 0x10);
    CHECK_EQ(Block.Control.Wrote, 0x08);
    CHECK_EQ(Block.Control.Read, 0x08);
    CHECK_EQ(Block.Control.Restored, 0x10);
    CHECK_EQ(Bus.PciConfig[0x0C], 0x10);
    /* the gating words are the model's: the dwords of 0x44 and 0x48 are what the bus wrote there */
    CHECK_EQ(Bus.Config.Pgctl, 0x00000004);
    /* the text of a dword as the lines print it */
    CHECK_STR(IntcDspPciDwordText(0x7F508086, TRUE, Text), "0x7f508086");
    CHECK_STR(IntcDspPciDwordText(0, TRUE, Text), "0x00000000");
    CHECK_STR(IntcDspPciDwordText(4, FALSE, Text), "unread");
    /* an original of 0x08: 0x10 is written */
    Bus.PciConfig[0x0C] = 0x08;
    CHECK(IntcDspDeadWindowCollect(FwloadDspWindowDead, &Pci, &Block));
    CHECK(Block.Control.Done);
    CHECK_EQ(Block.Control.Original, 0x08);
    CHECK_EQ(Block.Control.Wrote, 0x10);
    CHECK_EQ(Block.Control.Read, 0x10);
    CHECK_EQ(Block.Control.Restored, 0x08);
    CHECK_EQ(Bus.PciConfig[0x0C], 0x08);
    /* a write that does not land through the path: the control says so, and the original is still there */
    Bus.PciConfig[0x0C] = 0x10;
    FakeBusPciNoStick(&Bus, 0x0C);
    CHECK(IntcDspDeadWindowCollect(FwloadDspWindowDead, &Pci, &Block));
    CHECK(Block.Control.Done);
    CHECK_EQ(Block.Control.Original, 0x10);
    CHECK_EQ(Block.Control.Wrote, 0x08);
    CHECK_EQ(Block.Control.Read, 0x10);
    CHECK_EQ(Block.Control.Restored, 0x10);
    CHECK_EQ(Bus.PciConfig[0x0C], 0x10);

    /* the capability list: none; one without the power-management capability; a pointer into the header; one not aligned; one that loops; a word the bus does not answer */
    Bus.PciConfig[0x34] = 0x00;
    IntcDspPciPowerManagement(&Pci, &Block.Pm);
    CHECK(!Block.Pm.Present);
    CHECK_EQ(Block.Pm.Pointer, 0x00);
    Bus.PciConfig[0x34] = 0x50;
    IntcDspPciPowerManagement(&Pci, &Block.Pm);
    CHECK(!Block.Pm.Present);
    CHECK_EQ(Block.Pm.Pointer, 0x50);
    Bus.PciConfig[0x34] = 0x20;
    IntcDspPciPowerManagement(&Pci, &Block.Pm);
    CHECK(!Block.Pm.Present);
    Bus.PciConfig[0x34] = 0x62;
    IntcDspPciPowerManagement(&Pci, &Block.Pm);
    CHECK(!Block.Pm.Present);
    Bus.PciConfig[0x34] = 0x60;
    Bus.PciConfig[0x61] = 0x60;
    IntcDspPciPowerManagement(&Pci, &Block.Pm);
    CHECK(!Block.Pm.Present);
    CHECK_EQ(Block.Pm.Pointer, 0x60);
    Bus.PciConfig[0x61] = 0x70;
    FakeBusPciNoRead(&Bus, 0x64);
    CHECK(IntcDspDeadWindowCollect(FwloadDspWindowDead, &Pci, &Block));
    CHECK(!Block.ConfigRead[13]);
    CHECK(Block.ConfigRead[12] && Block.ConfigRead[0]);
    CHECK(Block.Pm.Present);
    CHECK_EQ(Block.Pm.State, 3);
}

/*
 * The platform's power gating of the DSP around a boot (N15, the first boot on silicon, image p5e3: every BAR4
 * read all ones, before and after the power-up). (a) the order at a boot: the gating off, then the global
 * processing enable, then the subsystem's power request, and no read reaches a gated window; (b) the gating
 * allowed again after a successful boot, after a failed one and after a cancelled one, as the way out's last
 * step, and never taken off when the operation is absent; (d) the processing capabilities' interrupt enable
 * (PIE) is set with GPROCEN only under the boot's interrupt policy, and both are cleared at the power-down;
 * (e) a DSP that answers all ones fails at the power-up with its own status, after the CPA wait, the gating
 * allowed again.
 */
static
void
CheckPowerGating(
    HARNESS* H,
    const SOF_RI_IMAGE* Image)
{
    FAKE_DSP_FAULTS Faults;
    UINT32 Count, Index;

    /* (a) a DSP the platform gates: the window decodes from the moment the gating is taken off, before the first register write */
    Faults = *FakeDspDefaultFaults();
    Faults.WindowGated = TRUE;
    HarnessBoot(H, Image, &Faults);
    CHECK(H->Dsp->Gated);
    CHECK_EQ(FwloadBoot(H->Ctx), FwloadOk);
    CHECK(!H->Dsp->Gated && !H->Ctx->GatingDisabled);
    CHECK_EQ(H->Dsp->GatedReads, 0);
    CHECK_EQ(H->Dsp->GatedWrites, 0);
    CHECK_EQ(H->Dsp->PowerOrderCount, 8);
    CHECK_EQ(H->Dsp->PowerOrder[0], FAKE_POWER_GATING_OFF);
    CHECK_EQ(H->Dsp->PowerOrder[1], FAKE_POWER_GPROCEN_ON);
    CHECK_EQ(H->Dsp->PowerOrder[2], FAKE_POWER_SUBSYSTEM_ON);
    CHECK_EQ(H->Dsp->PowerOrder[7], FAKE_POWER_GATING_ON);
    /* the writes in that order: the processing enable first, the subsystem's power request second; the gating came before them in the power order */
    CHECK(H->Dsp->WriteCount >= 2 && H->Dsp->WriteLog[0].Bar == FWLOAD_BAR_PP && H->Dsp->WriteLog[0].Offset == FWLOAD_PP_OFFSET_PPCTL &&
        (H->Dsp->WriteLog[0].Value & FWLOAD_PPCTL_GPROCEN));
    CHECK(H->Dsp->WriteLog[1].Bar == FWLOAD_BAR_DSP && H->Dsp->WriteLog[1].Offset == FwloadPlatformForDevice(FWLOAD_DEV_ARL_S)->SubsystemControl &&
        (H->Dsp->WriteLog[1].Value & 0x00010000));
    CHECK_EQ(H->Dsp->UnexpectedAccesses, 0);

    /* (b) a successful boot allowed the gating again as its last step: the power-down from ready does not ask twice */
    CHECK_EQ(FwloadPowerDown(H->Ctx), FwloadOk);
    CHECK(!H->Ctx->GatingDisabled);
    CHECK_EQ(H->Dsp->PowerOrderCount, 12);
    CHECK_EQ(H->Dsp->PowerOrder[11], FAKE_POWER_GPROCEN_OFF);

    /* a failed boot, the firmware that never announces itself: the way out ends with the processing enable off and the gating allowed again */
    Faults = *FakeDspDefaultFaults();
    Faults.NeverFwReady = TRUE;
    HarnessBoot(H, Image, &Faults);
    CHECK_EQ(FwloadBoot(H->Ctx), FwloadTimeoutFwReady);
    CHECK_EQ(H->Ctx->State, FwloadStateFailed);
    CHECK_EQ(H->Ctx->CleanupStatus, FwloadOk);
    CHECK(!H->Ctx->GatingDisabled);
    Count = H->Dsp->PowerOrderCount;
    CHECK(Count >= 3);
    CHECK_EQ(H->Dsp->PowerOrder[0], FAKE_POWER_GATING_OFF);
    CHECK_EQ(H->Dsp->PowerOrder[Count - 1], FAKE_POWER_GATING_ON);
    CHECK_EQ(H->Dsp->PowerOrder[Count - 2], FAKE_POWER_GPROCEN_OFF);
    /* the power-down of the failed context masks the enables and clears the processing enable once more, and asks for the gating no more */
    CHECK_EQ(FwloadPowerDown(H->Ctx), FwloadOk);
    CHECK(H->Dsp->PowerOrderCount > Count);
    CHECK_EQ(H->Dsp->PowerOrder[H->Dsp->PowerOrderCount - 1], FAKE_POWER_GPROCEN_OFF);
    for (Index = Count; Index < H->Dsp->PowerOrderCount; Index++)
        CHECK(H->Dsp->PowerOrder[Index] != FAKE_POWER_GATING_ON && H->Dsp->PowerOrder[Index] != FAKE_POWER_GATING_OFF);

    /* a cancelled boot: the same way out */
    HarnessBoot(H, Image, NULL);
    H->CancelAfterDelays = 1;
    CHECK_EQ(FwloadBoot(H->Ctx), FwloadCancelled);
    H->CancelAfterDelays = 0;
    CHECK(!H->Ctx->GatingDisabled);
    Count = H->Dsp->PowerOrderCount;
    CHECK_EQ(H->Dsp->PowerOrder[Count - 1], FAKE_POWER_GATING_ON);
    CHECK_EQ(FwloadPowerDown(H->Ctx), FwloadOk);

    /* a ROM that never comes ready, three attempts: the two events of every boot and the retries' fit the model's order log */
    Faults = *FakeDspDefaultFaults();
    Faults.NeverRomReady = TRUE;
    HarnessBoot(H, Image, &Faults);
    CHECK_EQ(FwloadBoot(H->Ctx), FwloadTimeoutRomStatus);
    CHECK(!H->Ctx->GatingDisabled);
    Count = H->Dsp->PowerOrderCount;
    CHECK_EQ(Count, 17);
    CHECK_EQ(H->Dsp->PowerOrder[Count - 1], FAKE_POWER_GATING_ON);
    CHECK_EQ(FwloadPowerDown(H->Ctx), FwloadOk);

    /* (f) the operation absent, a driver built without it: nothing is taken off and nothing allowed again; a gated DSP stays all ones and fails at the power-up, a DSP that is not gated boots as before */
    H->NoGating = TRUE;
    Faults = *FakeDspDefaultFaults();
    Faults.WindowGated = TRUE;
    HarnessBoot(H, Image, &Faults);
    CHECK(H->Ctx->Ops->DspPowerGating == NULL);
    CHECK_EQ(FwloadBoot(H->Ctx), FwloadDspWindowDead);
    CHECK(H->Dsp->Gated && H->Dsp->GatedReads >= 2 && H->Dsp->GatedWrites >= 1);
    CHECK(!H->Ctx->GatingDisabled);
    CHECK(!FakeDspPowerEventSeen(H->Dsp, FAKE_POWER_GATING_OFF));
    /* the power-down of the all-ones context meets the enable that never reads masked: the cleanup's timeout, as on the owner's machine */
    CHECK_EQ(FwloadPowerDown(H->Ctx), FwloadInterruptTimeout);
    HarnessBoot(H, Image, NULL);
    CHECK_EQ(FwloadBoot(H->Ctx), FwloadOk);
    CHECK(!H->Ctx->GatingDisabled);
    CHECK(!FakeDspPowerEventSeen(H->Dsp, FAKE_POWER_GATING_OFF));
    CHECK_EQ(FwloadPowerDown(H->Ctx), FwloadOk);
    H->NoGating = FALSE;

    /* (d) the interrupt enable of the processing capabilities: not set while the boot polls */
    HarnessBoot(H, Image, NULL);
    CHECK_EQ(FwloadBoot(H->Ctx), FwloadOk);
    CHECK_EQ(H->Dsp->PpControl, FWLOAD_PPCTL_GPROCEN);
    CHECK(!H->Ctx->InterruptsEnabled);
    CHECK_EQ(FwloadPowerDown(H->Ctx), FwloadOk);
    CHECK_EQ(H->Dsp->PpControl & (FWLOAD_PPCTL_GPROCEN | FWLOAD_PPCTL_PIE), 0);
    /* set with the processing enable under the boot's interrupt policy, and both cleared at the power-down */
    HarnessBoot(H, Image, NULL);
    H->Ctx->UseInterrupts = TRUE;
    CHECK_EQ(FwloadBoot(H->Ctx), FwloadOk);
    CHECK(H->Ctx->InterruptsEnabled);
    CHECK_EQ(H->Dsp->PpControl, FWLOAD_PPCTL_GPROCEN | FWLOAD_PPCTL_PIE);
    CHECK_EQ(FwloadPowerDown(H->Ctx), FwloadOk);
    CHECK_EQ(H->Dsp->PpControl & (FWLOAD_PPCTL_GPROCEN | FWLOAD_PPCTL_PIE), 0);
    /* a failed boot under the policy clears both too */
    Faults = *FakeDspDefaultFaults();
    Faults.NeverFwReady = TRUE;
    HarnessBoot(H, Image, &Faults);
    H->Ctx->UseInterrupts = TRUE;
    CHECK_EQ(FwloadBoot(H->Ctx), FwloadTimeoutFwReady);
    CHECK(!H->Ctx->GatingDisabled && !H->Ctx->InterruptsEnabled);
    CHECK_EQ(H->Dsp->PpControl & (FWLOAD_PPCTL_GPROCEN | FWLOAD_PPCTL_PIE), 0);
    CHECK_EQ(FwloadPowerDown(H->Ctx), FwloadOk);

    /* (e) a DSP whose window answers all ones whatever the gating: the power-up fails with its own status after the CPA wait, the cleanup meets the all-ones enable too, and the gating is allowed again */
    Faults = *FakeDspDefaultFaults();
    Faults.WindowDead = TRUE;
    HarnessBoot(H, Image, &Faults);
    CHECK_EQ(FwloadBoot(H->Ctx), FwloadDspWindowDead);
    CHECK_EQ(H->Ctx->Status, FwloadDspWindowDead);
    CHECK_EQ(H->Ctx->State, FwloadStateFailed);
    CHECK_EQ(H->Ctx->FailedState, FwloadStatePoweringUp);
    CHECK(H->Dsp->Gated && H->Dsp->GatedReads >= 2 && H->Dsp->GatedWrites >= 1);
    CHECK_EQ(H->Ctx->CleanupStatus, FwloadInterruptTimeout);
    CHECK(!H->Ctx->GatingDisabled);
    Count = H->Dsp->PowerOrderCount;
    CHECK_EQ(H->Dsp->PowerOrder[0], FAKE_POWER_GATING_OFF);
    CHECK_EQ(H->Dsp->PowerOrder[1], FAKE_POWER_GPROCEN_ON);
    CHECK_EQ(H->Dsp->PowerOrder[Count - 1], FAKE_POWER_GATING_ON);
    CHECK_EQ(H->Dsp->PowerOrder[Count - 2], FAKE_POWER_GPROCEN_OFF);
    CHECK_EQ(H->Dsp->UnexpectedAccesses, 0);
    /* the power-down of the all-ones context meets the enable that never reads masked: the cleanup's timeout, and the state is off */
    CHECK_EQ(FwloadPowerDown(H->Ctx), FwloadInterruptTimeout);
    CHECK_EQ(H->Ctx->State, FwloadStateOff);
    /* the same status from the SPA bits that do not read back as written, with the power up and the window decoding */
    Faults = *FakeDspDefaultFaults();
    Faults.SpaDropped = TRUE;
    HarnessBoot(H, Image, &Faults);
    CHECK_EQ(FwloadBoot(H->Ctx), FwloadDspWindowDead);
    CHECK_EQ(H->Ctx->FailedState, FwloadStatePoweringUp);
    CHECK(!H->Ctx->GatingDisabled);
    CHECK_EQ(H->Dsp->PowerOrder[H->Dsp->PowerOrderCount - 1], FAKE_POWER_GATING_ON);
    CHECK_EQ(FwloadPowerDown(H->Ctx), FwloadOk);
    /* and from the core's control word alone: the ROM init's core power-up refuses it, the request written and nothing else; no attempt follows, and the cleanup's own power-down of the all-ones word is the power-up timeout */
    Faults = *FakeDspDefaultFaults();
    Faults.CoreWindowDead = TRUE;
    HarnessBoot(H, Image, &Faults);
    CHECK_EQ(FwloadBoot(H->Ctx), FwloadDspWindowDead);
    CHECK_EQ(H->Ctx->FailedState, FwloadStateRomInit);
    CHECK_EQ(H->Ctx->Attempt, 1);
    CHECK_EQ(H->Dsp->RomRequests, 1);
    CHECK(!H->Ctx->GatingDisabled);
    CHECK_EQ(H->Ctx->CleanupStatus, FwloadTimeoutPowerUp);
    CHECK_EQ(H->Dsp->PowerOrder[H->Dsp->PowerOrderCount - 1], FAKE_POWER_GATING_ON);
    CHECK_EQ(FwloadPowerDown(H->Ctx), FwloadOk);
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

    /* R8 of the branch review: the payload copy is byte-exact for an odd length and leaves the byte after it alone */
    {
        static const SIZE_T OddLengths[] = { 1, 3, 4097, REAL_PAYLOAD_BYTES - 1 };
        SIZE_T Odd;

        for (Odd = 0; Odd < sizeof(OddLengths) / sizeof(OddLengths[0]); Odd++)
        {
            memset(Dma.Virtual, 0xA5, Dma.Capacity);
            CHECK_EQ(FwloadClDmaPrepare(&Dma, Data + Image->PayloadOffset, OddLengths[Odd]), FwloadOk);
            CHECK(memcmp(Dma.Virtual, Data + Image->PayloadOffset, OddLengths[Odd]) == 0);
            CHECK_EQ(Dma.Virtual[OddLengths[Odd]], 0xA5);
            CHECK_EQ(Dma.Bytes, OddLengths[Odd]);
            CHECK_EQ(Dma.FragmentCount, 1);
        }
    }

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

    /* R11 of the branch review: the real payload in a buffer that straddles the 4 GiB boundary, at the boundary the driver asks for: two descriptors, none crossing, and the model boots from it */
    H->Segment[0].Address = 0xFFF00000ULL;
    H->Segment[0].Bytes = H->Bytes;
    H->SegmentCount = 1;
    Dma.SegmentCount = 1;
    Dma.BdlCapacity = STREAM_BDL_ENTRIES;
    Dma.BdlBytes = STREAM_BDL_ENTRIES * sizeof(FWLOAD_BDL_ENTRY);
    Dma.EntryBoundary = FWLOAD_CL_DMA_BOUNDARY_BYTES;
    Status = FwloadClDmaPrepare(&Dma, Data + Image->PayloadOffset, Image->PayloadBytes);
    CHECK_EQ(Status, FwloadOk);
    CHECK_EQ(Dma.FragmentCount, 2);
    CheckDescriptorList(&Dma, 0xFFF00000ULL, FWLOAD_CL_DMA_BOUNDARY_BYTES, Image->PayloadBytes);
    CHECK(memcmp(Dma.Virtual, Data + Image->PayloadOffset, Image->PayloadBytes) == 0);
    /* without the boundary the same buffer is one descriptor that crosses it */
    Dma.EntryBoundary = 0;
    CHECK_EQ(FwloadClDmaPrepare(&Dma, Data + Image->PayloadOffset, Image->PayloadBytes), FwloadOk);
    CHECK_EQ(Dma.FragmentCount, 1);
    CHECK((UINT64)Dma.Bdl[0].AddressLow + Dma.Bdl[0].Bytes > FWLOAD_CL_DMA_BOUNDARY_BYTES);
    H->EntryBoundary = FWLOAD_CL_DMA_BOUNDARY_BYTES;
    HarnessBoot(H, Image, NULL);
    CHECK_EQ(FwloadBoot(H->Ctx), FwloadOk);
    CHECK_EQ(H->Ctx->Dma.FragmentCount, 2);
    CHECK_EQ(H->Dsp->DmaErrors, 0);
    H->EntryBoundary = 0;
    H->Segment[0].Address = 0x10000000ULL;
    H->Segment[0].Bytes = 0x100000;
    CheckBoundaryRows(Data + Image->PayloadOffset);

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

    for (Index = 0; Index <= (UINT32)FwloadDspWindowDead; Index++)
    {
        CHECK(FwloadStatusName((FWLOAD_STATUS)Index)[0] != 0);
        for (Other = Index + 1; Other <= (UINT32)FwloadDspWindowDead; Other++)
            CHECK(strcmp(FwloadStatusName((FWLOAD_STATUS)Index), FwloadStatusName((FWLOAD_STATUS)Other)) != 0);
    }
    for (Index = 0; Index <= (UINT32)FwloadStateFailed; Index++)
        CHECK(FwloadStateName((FWLOAD_STATE)Index)[0] != 0);
    CHECK_STR(FwloadStatusName(FwloadOk), "ok");
    CHECK_STR(FwloadStatusName(FwloadDspWindowDead), "the DSP's window does not answer");
    CHECK_STR(FwloadStatusName(FwloadRomError), "ROM error");
    CHECK_STR(FwloadStatusName(FwloadFwCrash), "firmware crash");
    CHECK_STR(FwloadStatusName(FwloadVersionMismatch), "version mismatch");
    CHECK_STR(FwloadStatusName(FwloadInterruptStorm), "interrupt storm");
    CHECK_STR(FwloadStatusName((FWLOAD_STATUS)999), "unknown");
    CHECK_STR(FwloadStateName(FwloadStateReady), "ready");
    CHECK_STR(FwloadStateName(FwloadStateFailed), "failed");
    CHECK_STR(FwloadStateName((FWLOAD_STATE)999), "unknown");
    CHECK(FwloadPlatformForDevice(FWLOAD_DEV_ARL_S) == FwloadPlatformForDevice(FWLOAD_DEV_ARL_S));
    CHECK_EQ(FwloadPlatformForDevice(FWLOAD_DEV_ARL_S)->RomStatus, 0x163200);
    CHECK_EQ(FwloadPlatformForDevice(FWLOAD_DEV_ARL_S)->IpcRequest, 0x73210);
    CHECK_EQ(FwloadPlatformForDevice(FWLOAD_DEV_ARL_S)->IpcReply, 0x73214);
    CHECK_EQ(FwloadPlatformForDevice(FWLOAD_DEV_ARL_S)->Window0Offset, 0x180000);
    CHECK_EQ(FwloadPlatformForDevice(FWLOAD_DEV_ARL_S)->UplinkOffset, 0x181000);
}

/*
 * The platform table: the two controllers resolve to their records and
 * their firmware files, both records carry the ACE 1.0 map of mtl.h (every
 * offset, the same ones), a controller the table does not name resolves to
 * none, and the real sof-mtl.ri boots under the 7E28 record with the model
 * seeing no access outside the map (and under the Arrow Lake-S record too,
 * the map being one). The boot decision's four answers.
 */
static
void
CheckPlatforms(
    HARNESS* H,
    PSOF_RI_MODULE Modules)
{
    /* the map of mtl.h, at v6.12 and at master alike, in the record's order */
    static const UINT32 Map[] =
    {
        0x1000, 0x178D04, 0x1108, 0x00000001, 0x00000040, 0x1140, 0x73210, 0x73380, 0x73214, 0x73228,
        0x73200, 0x73204, 0x73300, 0x163200, 0x163204, 0x180000, 0x8000, 0x181000, 0x188000
    };
    static const size_t Fields[] =
    {
        offsetof(FWLOAD_PLATFORM, SubsystemControl), offsetof(FWLOAD_PLATFORM, CoreControl), offsetof(FWLOAD_PLATFORM, InterruptPointer),
        offsetof(FWLOAD_PLATFORM, InterruptEnableHostIpc), offsetof(FWLOAD_PLATFORM, InterruptEnableSoundwire), offsetof(FWLOAD_PLATFORM, HostIpcInterruptEnable),
        offsetof(FWLOAD_PLATFORM, IpcRequest), offsetof(FWLOAD_PLATFORM, IpcRequestData), offsetof(FWLOAD_PLATFORM, IpcReply), offsetof(FWLOAD_PLATFORM, IpcControl),
        offsetof(FWLOAD_PLATFORM, IpcTarget), offsetof(FWLOAD_PLATFORM, IpcTargetAck), offsetof(FWLOAD_PLATFORM, IpcTargetData), offsetof(FWLOAD_PLATFORM, RomStatus),
        offsetof(FWLOAD_PLATFORM, RomError), offsetof(FWLOAD_PLATFORM, Window0Offset), offsetof(FWLOAD_PLATFORM, WindowStride), offsetof(FWLOAD_PLATFORM, UplinkOffset),
        offsetof(FWLOAD_PLATFORM, DownlinkOffset)
    };
    /* controllers of the same family the INF lists as probe-only, and ids that are none */
    static const UINT32 Unnamed[] = { 0x7728, 0xA828, 0xE328, 0xE428, 0, 0xFFFFFFFF };
    const FWLOAD_PLATFORM* Records[] = { FwloadPlatformForDevice(FWLOAD_DEV_MTL), FwloadPlatformForDevice(FWLOAD_DEV_ARL_S) };
    static const char* const Firmware[] = { "sof-mtl.ri", "sof-arl-s.ri" };
    SOF_RI_IMAGE MtlImage;
    char Path[512];
    UINT8* Data;
    FILE* File;
    UINT32 I, K;

    CHECK_EQ(sizeof(Map) / sizeof(Map[0]), sizeof(Fields) / sizeof(Fields[0]));
    for (K = 0; K < 2; K++)
    {
        CHECK(Records[K] != NULL);
        if (!Records[K])
            continue;
        CHECK_EQ(Records[K]->DeviceId, K == 0 ? FWLOAD_DEV_MTL : FWLOAD_DEV_ARL_S);
        CHECK(Records[K]->FirmwareName != NULL);
        if (Records[K]->FirmwareName)
            CHECK_STR(Records[K]->FirmwareName, Firmware[K]);
        for (I = 0; I < sizeof(Map) / sizeof(Map[0]); I++)
            CHECK_EQ(*(const UINT32*)((const char*)Records[K] + Fields[I]), Map[I]);
        /* the same record each time, and the other one for the other controller */
        CHECK(FwloadPlatformForDevice(Records[K]->DeviceId) == Records[K]);
    }
    for (I = 0; I < sizeof(Unnamed) / sizeof(Unnamed[0]); I++)
        CHECK(FwloadPlatformForDevice(Unnamed[I]) == NULL);

    /* the boot decision: the first condition that fails is the reason; all three hold is the boot */
    CHECK_EQ(FwloadBootDecision(NULL, TRUE, TRUE), FwloadBootNoRecord);
    CHECK_EQ(FwloadBootDecision(NULL, FALSE, FALSE), FwloadBootNoRecord);
    CHECK_EQ(FwloadBootDecision(Records[0], FALSE, TRUE), FwloadBootNoFirmware);
    CHECK_EQ(FwloadBootDecision(Records[1], FALSE, TRUE), FwloadBootNoFirmware);
    CHECK_EQ(FwloadBootDecision(Records[0], TRUE, FALSE), FwloadBootNotRequested);
    CHECK_EQ(FwloadBootDecision(Records[1], TRUE, FALSE), FwloadBootNotRequested);
    CHECK_EQ(FwloadBootDecision(Records[0], TRUE, TRUE), FwloadBootYes);
    CHECK_EQ(FwloadBootDecision(Records[1], TRUE, TRUE), FwloadBootYes);
    CHECK_STR(FwloadBootDecisionName(FwloadBootYes), "booted");
    CHECK_STR(FwloadBootDecisionName(FwloadBootNoRecord), "no record for this controller");
    CHECK_STR(FwloadBootDecisionName(FwloadBootNoFirmware), "the firmware file is not resident");
    CHECK_STR(FwloadBootDecisionName(FwloadBootNotRequested), "BootDsp is 0");
    CHECK_STR(FwloadBootDecisionName((FWLOAD_BOOT_DECISION)999), "unknown");

    /* the real sof-mtl.ri under the 7E28 record: parsed like the Arrow Lake-S image, booted through the model with no access outside the map */
    snprintf(Path, sizeof(Path), "%s/sof-mtl.ri", FWLOAD_FIRMWARE_DIR);
    File = fopen(Path, "rb");
    if (File == NULL)
    {
        fprintf(stderr, "cannot open %s\n", Path);
        exit(2);
    }
    Data = (UINT8*)malloc(MTL_IMAGE_BYTES);
    if (Data == NULL)
        exit(2);
    CHECK_EQ(fread(Data, 1, MTL_IMAGE_BYTES, File), MTL_IMAGE_BYTES);
    CHECK(fgetc(File) == EOF);
    fclose(File);
    CHECK_EQ(Fnv1a64(Data, MTL_IMAGE_BYTES), MTL_IMAGE_FNV1A64);
    memset(&MtlImage, 0, sizeof(MtlImage));
    memset(Modules, 0, 64 * sizeof(Modules[0]));
    CHECK_EQ(SofRiParse(Data, MTL_IMAGE_BYTES, Modules, 64, &MtlImage), FwloadOk);
    CHECK(MtlImage.ModuleCount != 0);
    printf("fwload: sof-mtl.ri: image '%s' version %u.%u.%u.%u, %u modules, payload 0x%llx + 0x%llx\n",
           MtlImage.Name, MtlImage.Major, MtlImage.Minor, MtlImage.Hotfix, MtlImage.Build, MtlImage.ModuleCount,
           (unsigned long long)MtlImage.PayloadOffset, (unsigned long long)MtlImage.PayloadBytes);
    if (Records[0] && Records[1])
    {
        for (K = 0; K < 2; K++)
        {
            HarnessBootPlatform(H, Records[K], &MtlImage, NULL);
            CHECK_EQ(FwloadBoot(H->Ctx), FwloadOk);
            CHECK_EQ(H->Ctx->State, FwloadStateReady);
            CHECK_EQ(H->Ctx->Status, FwloadOk);
            CHECK_EQ(H->Ctx->Dma.Bytes, MtlImage.PayloadBytes);
            CHECK_EQ(H->Dsp->DmaErrors, 0);
            CHECK_EQ(H->Dsp->UnexpectedAccesses, 0);
            CHECK_EQ(H->Dsp->RomRequests, 1);
        }
    }
    free(Data);
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
    CHECK(H->Ctx->Ops == &H->Ops && memcmp(&H->Ops, &HarnessOps, sizeof(HarnessOps)) == 0);
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
        HarnessOps.Lock(Bad[Index]);
        HarnessOps.Unlock(Bad[Index]);
        HarnessOps.DspPowerGating(Bad[Index], FALSE);
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
    CheckImageResident(&H, Data);
    CheckHappyPath(&H, Image);
    CheckOpsContext(&H, Image);
    CheckTimeouts(&H, Image);
    CheckFwConfigReply(&H, Image);
    CheckStalledClock(&H, Image);
    CheckRomErrors(&H, Image);
    CheckFirmwareFaults(&H, Image);
    CheckInterruptPointer(&H, Image);
    CheckInterruptService(&H, Image);
    CheckNotificationStorm(&H, Image);
    CheckRetry(&H, Image);
    CheckCancellation(&H, Image);
    CheckPowerGating(&H, Image);
    CheckGatingReadback(&H, Image);
    CheckBadPayload(&H, Data, Image);
    CheckDma(&H, Data, Image);
    CheckBusBound();
    CheckSyntheticImage(&H);
    CheckMalformedReal(Data);
    CheckNames();
    CheckPlatforms(&H, Modules);
    free(Data);
    free(Image);
    free(Modules);
    HarnessFrees(&H);
    /* the detector case's calls are the only ones ever made without the harness */
    CHECK_EQ(ContextFaults, 2 * HOP_COUNT);
    printf("fwload: %d checks, %d failures\n", Checks, Failures);
    return Failures ? 1 : 0;
}
