/*
 * PROJECT:     LiberNT kernel-mode tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     The firmware loader reads a synthetic ACE image and boots the DSP model from it inside the kernel, reports the model's faults, and binds the DSP child's bus interface by size and DMA capability
 * COPYRIGHT:   Copyright 2026 LiberNT contributors
 */

#include <kmt_test.h>
#include <fwload.h>
#include <fake_dsp.h>
#include <fake_bus.h>

#define NDEBUG
#include <debug.h>

#include "fwload_cases.h"

#define FWLOAD_TEST_TAG     'dlFw'
#define FWLOAD_TEST_PAYLOAD 0xA000

/* The performance counter the driver's clock is built from (the p2b2 image's D2): the counter is the return value, the out parameter receives the frequency; the counter advances over a stall */
static
VOID
CheckPerformanceCounter(
    VOID)
{
    LARGE_INTEGER Frequency, Before, After;

    Frequency.QuadPart = 0;
    Before = KeQueryPerformanceCounter(&Frequency);
    KeStallExecutionProcessor(1000);
    After = KeQueryPerformanceCounter(NULL);
    ok(Frequency.QuadPart > 0, "KeQueryPerformanceCounter reported frequency %I64d\n", Frequency.QuadPart);
    ok(After.QuadPart > Before.QuadPart, "the counter did not advance over a stall: %I64d, %I64d\n", Before.QuadPart, After.QuadPart);
    if (Frequency.QuadPart > 0)
        ok(After.QuadPart - Before.QuadPart >= Frequency.QuadPart / 1000, "the counter advanced %I64d ticks over a 1 ms stall at %I64d Hz\n",
           After.QuadPart - Before.QuadPart, Frequency.QuadPart);
}

static
VOID
CheckImage(
    UINT8* Image)
{
    SOF_RI_IMAGE Parsed;
    SOF_RI_MODULE Modules[4];
    UINT32 Index;

    FwloadCaseBuildImage(Image);
    ok_eq_int(SofRiParse(Image, FWLOAD_CASE_IMAGE_BYTES, Modules, 4, &Parsed), FwloadOk);
    ok_eq_str(Parsed.Name, "ADSPFW");
    ok_eq_uint(Parsed.ExtManModuleCount, 1);
    ok_eq_uint(Parsed.PayloadOffset, FWLOAD_CASE_PAYLOAD_OFFSET);
    ok_eq_size(Parsed.UsedBytes, FWLOAD_TEST_PAYLOAD);
    ok_eq_uint(Parsed.ModuleCount, 1);
    ok_eq_uint(Parsed.Major, 2);
    ok_eq_uint(Parsed.Minor, 15);
    ok_eq_uint(Parsed.PreloadPageCount, 2);
    ok_eq_uint(Parsed.PartitionCount, 2);
    ok(Parsed.HasSignature != FALSE, "signature header not found\n");
    ok_eq_str(Modules[0].Name, "BRNGUP");
    ok(Modules[0].HasConfig != FALSE, "module configuration not read\n");
    ok_eq_uint(Modules[0].IsBytes, 0x1000);
    ok_eq_uint(Modules[0].Segment[0].FileOffset, 0x8000);

    for (Index = 0; Index < FwloadCaseFaultCount; Index++)
    {
        FwloadCaseBuildImage(Image);
        FwloadCaseApplyFault(Image, FwloadCaseFaults[Index].Fault);
        ok_eq_int(SofRiParse(Image, FWLOAD_CASE_IMAGE_BYTES, Modules, 4, &Parsed), FwloadCaseFaults[Index].Status);
    }
    FwloadCaseBuildImage(Image);
    ok_eq_int(SofRiParse(Image, 0x9000, Modules, 4, &Parsed), FwloadTruncated);
    ok_eq_int(SofRiParse(Image, FWLOAD_CASE_IMAGE_BYTES, Modules, 0, &Parsed), FwloadTooManyModules);
    ok_eq_int(SofRiParse(NULL, FWLOAD_CASE_IMAGE_BYTES, Modules, 4, &Parsed), FwloadNoData);
    ok_eq_str(FwloadStatusName(FwloadRomError), "ROM error");
    ok_eq_str(FwloadStateName(FwloadStateReady), "ready");
    for (Index = 0; Index < sizeof(FwloadCaseRomErrors) / sizeof(FwloadCaseRomErrors[0]); Index++)
        ok_eq_str(FwloadRomErrorName(FwloadCaseRomErrors[Index].Code), FwloadCaseRomErrors[Index].Name);
    ok_eq_str(FwloadRomErrorName(0), "unknown ROM error");
}

static
VOID
BootWith(
    PFAKE_DSP Dsp,
    PFWLOAD_CONTEXT Context,
    const SOF_RI_IMAGE* Parsed,
    PFWLOAD_CL_DMA Dma,
    const FAKE_DSP_FAULTS* Faults,
    FWLOAD_STATUS Expected,
    FWLOAD_STATE ExpectedState)
{
    FakeDspInit(Dsp, FwloadPlatformArlS(), Faults);
    FwloadInitContext(Context, FakeDspOps(), Dsp, FwloadPlatformArlS(), Parsed, Dma, NULL);
    /* the driver keeps one context for the device and re-initializes it in place for every boot (R4): nothing of the boot before survives it */
    ok_eq_uint(Context->State, FwloadStateOff);
    ok_eq_int(Context->Status, FwloadOk);
    ok(!Context->Cancel && !Context->InterruptStorm && !Context->InterruptsEnabled && !Context->StreamSubmitted &&
        !Context->SubsystemOn && !Context->CoreOn, "a re-initialized context kept state of the boot before\n");
    ok_eq_uint(Context->InterruptCount, 0);
    ok_eq_uint(Context->StormCount, 0);
    ok_eq_int(FwloadBoot(Context), Expected);
    ok_eq_uint(Context->State, ExpectedState);
    ok_eq_uint(Dsp->UnexpectedAccesses, 0);
    /* every message the firmware posted was acknowledged before the boot ended */
    ok_eq_uint(FakeDspIpcErrors(Dsp), 0);
    ok_eq_uint(Dsp->StreamSubmits, Dsp->StreamReleases);
    ok(FakeDspPowerEventSeen(Dsp, FAKE_POWER_GPROCEN_ON) != FALSE, "GPROCEN never enabled\n");
    if (ExpectedState != FwloadStateReady)
    {
        ok(FakeDspPowerEventSeen(Dsp, FAKE_POWER_SUBSYSTEM_OFF) != FALSE, "subsystem left on\n");
        ok(FakeDspPowerEventSeen(Dsp, FAKE_POWER_GPROCEN_OFF) != FALSE, "GPROCEN left on\n");
    }
    {
        FWLOAD_STATE Before = Context->State;

        ok_eq_int(FwloadPowerDown(Context), FwloadOk);
        ok_eq_uint(Context->State, FwloadStateOff);
        /* the record of the state the power-down came from (D4): the one the context was in, which the context is no longer */
        if (Before != FwloadStateOff)
            ok_eq_uint(Context->PowerDownFrom, Before);
    }
}

/* The interrupt guard of the service routine (R2 of the branch review): a pending message is claimed and the enables masked under the lock the caller holds, the count of interrupts that no acknowledgement separates reaches FWLOAD_INTERRUPT_STORM_LIMIT, and the routine stops claiming the line */
static
VOID
CheckInterruptGuard(
    PFAKE_DSP Dsp,
    PFWLOAD_CONTEXT Context,
    const SOF_RI_IMAGE* Parsed,
    PFWLOAD_CL_DMA Dma)
{
    UINT32 Index;

    FakeDspInit(Dsp, FwloadPlatformArlS(), NULL);
    FwloadInitContext(Context, FakeDspOps(), Dsp, FwloadPlatformArlS(), Parsed, Dma, NULL);
    ok(!FwloadInterruptService(Context), "a line with no message pending was claimed\n");
    Dsp->IpcTarget = FWLOAD_IPC_BUSY | SOFIPC4_FW_READY_PRIMARY;
    for (Index = 1; Index < FWLOAD_INTERRUPT_STORM_LIMIT; Index++)
    {
        FakeDspOps()->Lock(Dsp);
        ok(FwloadInterruptService(Context) != FALSE, "a pending message was not claimed\n");
        FakeDspOps()->Unlock(Dsp);
    }
    ok(!Context->InterruptStorm, "the guard tripped early\n");
    FakeDspOps()->Lock(Dsp);
    ok(FwloadInterruptService(Context) != FALSE, "the last interrupt before the guard was not claimed\n");
    FakeDspOps()->Unlock(Dsp);
    ok(Context->InterruptStorm != FALSE, "the guard did not trip\n");
    ok_eq_uint(Context->InterruptCount, FWLOAD_INTERRUPT_STORM_LIMIT);
    ok_eq_uint(Context->FirstInterrupt.Pointer, FAKE_DSP_INTERRUPT_POINTER);
    FakeDspOps()->Lock(Dsp);
    ok(!FwloadInterruptService(Context), "a disarmed routine claimed the line\n");
    FakeDspOps()->Unlock(Dsp);
    ok_eq_uint(Context->InterruptCount, FWLOAD_INTERRUPT_STORM_LIMIT);
    ok_eq_uint(Dsp->InterruptWritesLocked, 3 * FWLOAD_INTERRUPT_STORM_LIMIT);
    ok_eq_uint(Dsp->InterruptWritesUnlocked, 0);
    ok_eq_uint(Dsp->InterruptLockRecursions, 0);
    ok_eq_uint(Dsp->InterruptLockDelays, 0);
    ok_eq_uint(Dsp->InterruptLockDepth, 0);
    ok_eq_uint(Dsp->UnexpectedAccesses, 0);
    ok_eq_str(FwloadStatusName(FwloadInterruptStorm), "interrupt storm");
    ok_eq_str(FwloadStatusName(FwloadInterruptTimeout), "interrupt enable timeout");
}

/* The image holder in the kernel (R6 of the branch review): the read copies the synthetic image into paged pool, the module table comes from nonpaged pool, and the second call reads nothing */
static UINT32 ImageReads;
static const UINT8* ImageSource;

static
FWLOAD_STATUS
KernelImageRead(
    PVOID Context,
    const UINT8** Data,
    SIZE_T* Bytes)
{
    UINT8* Copy;

    UNREFERENCED_PARAMETER(Context);
    ImageReads++;
    Copy = ExAllocatePoolWithTag(PagedPool, FWLOAD_CASE_IMAGE_BYTES, FWLOAD_TEST_TAG);
    if (Copy == NULL)
        return FwloadOutOfMemory;
    RtlCopyMemory(Copy, ImageSource, FWLOAD_CASE_IMAGE_BYTES);
    *Data = Copy;
    *Bytes = FWLOAD_CASE_IMAGE_BYTES;
    return FwloadOk;
}

static
PVOID
KernelImageAllocate(
    PVOID Context,
    SIZE_T Bytes)
{
    UNREFERENCED_PARAMETER(Context);
    return ExAllocatePoolWithTag(NonPagedPool, Bytes, FWLOAD_TEST_TAG);
}

static
VOID
KernelImageFree(
    PVOID Context,
    PVOID Memory)
{
    UNREFERENCED_PARAMETER(Context);
    ExFreePoolWithTag(Memory, FWLOAD_TEST_TAG);
}

static
VOID
CheckImageHolder(
    const UINT8* Source)
{
    static const FWLOAD_IMAGE_OPS Ops =
    {
        KernelImageRead,
        KernelImageAllocate,
        KernelImageFree
    };
    FWLOAD_IMAGE Holder;

    RtlZeroMemory(&Holder, sizeof(Holder));
    ImageSource = Source;
    ok_eq_int(FwloadImageEnsure(&Holder, &Ops, NULL), FwloadOk);
    ok_eq_uint(ImageReads, 1);
    ok_eq_str(Holder.Ri.Name, "ADSPFW");
    ok_eq_uint(Holder.Ri.ModuleCount, 1);
    ok(Holder.Modules != NULL && Holder.Data != Source, "no module table or no copy of the bytes\n");
    ok_eq_int(FwloadImageEnsure(&Holder, &Ops, NULL), FwloadOk);
    ok_eq_uint(ImageReads, 1);
    FwloadImageFree(&Holder, &Ops, NULL);
    ok(Holder.Data == NULL && Holder.Modules == NULL, "the holder kept something after the release\n");
}

/* R8 of the branch review: the bus interface binds by size, in version 1 or 2, and the 64-bit DMA capability it reports bounds the code-loader buffer */
static
VOID
CheckBusBound(
    VOID)
{
    const SIZE_T V1Bytes = RTL_SIZEOF_THROUGH_FIELD(ADSP_BUS_INTERFACE, ReleaseInterruptLock);
    FAKE_BUS Bus;
    ADSP_BUS_INTERFACE Queried;

    FakeBusInit(&Bus, 2, TRUE);
    ok_eq_uint(Bus.Interface.Size, (ULONG)sizeof(ADSP_BUS_INTERFACE));
    ok_eq_uint(Bus.Interface.Version, 2);
    RtlZeroMemory(&Queried, sizeof(Queried));
    ok_eq_int(FakeBusQueryInterface(&Bus, &Queried, sizeof(Queried), ADSP_BUS_INTERFACE_VERSION), STATUS_SUCCESS);
    ok(IntcDspBusComplete(&Queried) != FALSE, "a version 2 bus does not bind\n");
    ok_eq_ulonglong(IntcDspDmaHighBound(&Queried), MAXULONG64);
    ok_eq_uint(Bus.InfoCalls, 1);

    FakeBusInit(&Bus, 2, FALSE);
    RtlZeroMemory(&Queried, sizeof(Queried));
    ok_eq_int(FakeBusQueryInterface(&Bus, &Queried, sizeof(Queried), ADSP_BUS_INTERFACE_VERSION), STATUS_SUCCESS);
    ok_eq_ulonglong(IntcDspDmaHighBound(&Queried), 0xFFFFFFFFULL);

    Bus.InfoStatus = STATUS_NO_SUCH_DEVICE;
    RtlZeroMemory(&Queried, sizeof(Queried));
    ok_eq_int(FakeBusQueryInterface(&Bus, &Queried, sizeof(Queried), ADSP_BUS_INTERFACE_VERSION), STATUS_SUCCESS);
    ok_eq_ulonglong(IntcDspDmaHighBound(&Queried), 0xFFFFFFFFULL);

    /* an older bus answers with its shorter structure: it reports no capability, and the buffer stays below 4 GiB */
    FakeBusInit(&Bus, 1, TRUE);
    ok_eq_uint(Bus.Interface.Size, (ULONG)V1Bytes);
    RtlFillMemory(&Queried, sizeof(Queried), 0xA5);
    ok_eq_int(FakeBusQueryInterface(&Bus, &Queried, sizeof(Queried), ADSP_BUS_INTERFACE_VERSION), STATUS_SUCCESS);
    ok_eq_uint(Queried.Size, (ULONG)V1Bytes);
    ok_eq_uint(Queried.Version, 1);
    ok(IntcDspBusComplete(&Queried) != FALSE, "a version 1 bus does not bind\n");
    ok(Queried.GetPlatformInfo != NULL, "the tail of the caller's buffer was overwritten\n");
    ok_eq_ulonglong(IntcDspDmaHighBound(&Queried), 0xFFFFFFFFULL);
    ok_eq_uint(Bus.InfoCalls, 0);

    /* the query's rule: the bus may not exceed the caller's buffer or version, and a refused query writes nothing */
    FakeBusInit(&Bus, 2, TRUE);
    RtlFillMemory(&Queried, sizeof(Queried), 0x5A);
    ok_eq_int(FakeBusQueryInterface(&Bus, &Queried, (ULONG)V1Bytes, ADSP_BUS_INTERFACE_VERSION), STATUS_INVALID_BUFFER_SIZE);
    ok_eq_int(FakeBusQueryInterface(&Bus, &Queried, sizeof(Queried), 1), STATUS_INVALID_BUFFER_SIZE);
    ok(((PUCHAR)&Queried)[0] == 0x5A && ((PUCHAR)&Queried)[sizeof(Queried) - 1] == 0x5A, "a refused query wrote into the buffer\n");

    /* a bus shorter than version 1, or without one of its members, does not bind */
    RtlZeroMemory(&Queried, sizeof(Queried));
    ok(!IntcDspBusComplete(&Queried), "an empty interface binds\n");
    FakeBusInit(&Bus, 2, TRUE);
    ok_eq_int(FakeBusQueryInterface(&Bus, &Queried, sizeof(Queried), ADSP_BUS_INTERFACE_VERSION), STATUS_SUCCESS);
    Queried.Size = (USHORT)(V1Bytes - 1);
    ok(!IntcDspBusComplete(&Queried), "a bus shorter than version 1 binds\n");
    Queried.Size = sizeof(ADSP_BUS_INTERFACE);
    Queried.TriggerDSP = NULL;
    ok(!IntcDspBusComplete(&Queried), "a bus without a member binds\n");
}

START_TEST(Fwload)
{
    UINT8* Image;
    PFAKE_DSP Dsp;
    PFWLOAD_CONTEXT Context;
    UINT8* Virtual;
    FWLOAD_BDL_ENTRY Bdl[256];
    FWLOAD_DMA_SEGMENT Segment;
    FWLOAD_CL_DMA Dma;
    SOF_RI_IMAGE Parsed;
    SOF_RI_MODULE Modules[4];
    FAKE_DSP_FAULTS Faults;
    static const SIZE_T OddLengths[] = { 1, 3, 4097, FWLOAD_TEST_PAYLOAD - 1 };
    UINT32 Index;

    Image = ExAllocatePoolWithTag(NonPagedPool, FWLOAD_CASE_IMAGE_BYTES, FWLOAD_TEST_TAG);
    Dsp = ExAllocatePoolWithTag(NonPagedPool, sizeof(FAKE_DSP), FWLOAD_TEST_TAG);
    Context = ExAllocatePoolWithTag(NonPagedPool, sizeof(FWLOAD_CONTEXT), FWLOAD_TEST_TAG);
    Virtual = ExAllocatePoolWithTag(NonPagedPool, FWLOAD_TEST_PAYLOAD, FWLOAD_TEST_TAG);
    ok(Image != NULL && Dsp != NULL && Context != NULL && Virtual != NULL, "allocation failed\n");
    if (!(Image && Dsp && Context && Virtual))
    {
        if (Image) ExFreePoolWithTag(Image, FWLOAD_TEST_TAG);
        if (Dsp) ExFreePoolWithTag(Dsp, FWLOAD_TEST_TAG);
        if (Context) ExFreePoolWithTag(Context, FWLOAD_TEST_TAG);
        if (Virtual) ExFreePoolWithTag(Virtual, FWLOAD_TEST_TAG);
        return;
    }

    CheckImage(Image);
    CheckPerformanceCounter();
    FwloadCaseBuildImage(Image);
    ok_eq_int(SofRiParse(Image, FWLOAD_CASE_IMAGE_BYTES, Modules, 4, &Parsed), FwloadOk);
    CheckImageHolder(Image);

    RtlZeroMemory(Bdl, sizeof(Bdl));
    RtlZeroMemory(&Dma, sizeof(Dma));
    Segment.Address = 0x10000000ULL;
    Segment.Bytes = FWLOAD_TEST_PAYLOAD;
    Dma.Virtual = Virtual;
    Dma.Capacity = FWLOAD_TEST_PAYLOAD;
    Dma.Segment = &Segment;
    Dma.SegmentCount = 1;
    Dma.Bdl = Bdl;
    Dma.BdlCapacity = 256;
    Dma.BdlBytes = sizeof(Bdl);

    BootWith(Dsp, Context, &Parsed, &Dma, NULL, FwloadOk, FwloadStateReady);
    ok_eq_uint(Dsp->DmaErrors, 0);
    ok_eq_uint(Dsp->LoadedBytes, FWLOAD_TEST_PAYLOAD);
    ok_eq_uint(Dsp->RomRequests, 1);
    ok_eq_uint(Dsp->StreamStarts, 1);
    ok_eq_uint(Context->FwConfig.Major, 2);
    ok_eq_uint(Context->FwConfig.Minor, 15);
    ok_eq_uint(Context->FwConfig.TupleCount, 8);
    ok_eq_uint(Context->FwConfig.Registers.AbiVersion, SOFIPC4_FW_REGS_ABI_VER);
    ok_eq_uint(Context->Dma.FragmentCount, 1);
    ok_eq_uint(Context->Dma.StreamTag, FWLOAD_CL_STREAM_TAG);
    /* the boot polls: the three enables were masked after the power-up and again at ROM ready, never set, and once more by the power-down; every passive-level write to them ran under the interrupt lock */
    ok_eq_uint(Dsp->InterruptEnable, 0);
    ok_eq_uint(Dsp->HostIpcEnable, 0);
    ok_eq_uint(Dsp->IpcControl, 0);
    ok_eq_uint(Context->InterruptCount, 0);
    ok_eq_uint(Dsp->InterruptWritesLocked, 9);
    ok_eq_uint(Dsp->InterruptWritesUnlocked, 0);
    ok_eq_uint(Dsp->InterruptLockRecursions, 0);

    CheckInterruptGuard(Dsp, Context, &Parsed, &Dma);

    /* a boot without the operations context stops before the model sees a single access */
    FakeDspInit(Dsp, FwloadPlatformArlS(), NULL);
    FwloadInitContext(Context, FakeDspOps(), NULL, FwloadPlatformArlS(), &Parsed, &Dma, NULL);
    ok_eq_int(FwloadBoot(Context), FwloadBadState);
    ok_eq_uint(Context->State, FwloadStateOff);
    ok_eq_ulonglong(Dsp->Clock, 0);
    ok_eq_uint(Dsp->WriteCount, 0);
    ok_eq_uint(Dsp->StreamSubmits, 0);

    Faults = *FakeDspDefaultFaults();
    Faults.NeverAckRom = TRUE;
    BootWith(Dsp, Context, &Parsed, &Dma, &Faults, FwloadTimeoutRomInit, FwloadStateFailed);
    ok_eq_uint(Dsp->RomRequests, FWLOAD_BOOT_ATTEMPTS);
    Faults = *FakeDspDefaultFaults();
    Faults.NeverFwEntered = TRUE;
    BootWith(Dsp, Context, &Parsed, &Dma, &Faults, FwloadTimeoutBaseFw, FwloadStateFailed);
    Faults = *FakeDspDefaultFaults();
    Faults.NeverFwReady = TRUE;
    BootWith(Dsp, Context, &Parsed, &Dma, &Faults, FwloadTimeoutFwReady, FwloadStateFailed);
    /* R5 of the branch review: a firmware that streams notifications instead of FW_READY ends at the FW_READY budget, one notification drained per poll */
    Faults = *FakeDspDefaultFaults();
    Faults.NotificationStorm = FAKE_DSP_STORM_INSTEAD_FW_READY;
    BootWith(Dsp, Context, &Parsed, &Dma, &Faults, FwloadTimeoutFwReady, FwloadStateFailed);
    ok_eq_uint(Dsp->StormPosted, FWLOAD_TIMEOUT_FW_READY_US / FWLOAD_POLL_INTERVAL_US + 1);
    Faults = *FakeDspDefaultFaults();
    Faults.AbiVersion = 2;
    BootWith(Dsp, Context, &Parsed, &Dma, &Faults, FwloadAbiMismatch, FwloadStateFailed);
    Faults = *FakeDspDefaultFaults();
    Faults.IpcStatus = SofIpc4Busy;
    BootWith(Dsp, Context, &Parsed, &Dma, &Faults, FwloadIpcError, FwloadStateFailed);
    Faults = *FakeDspDefaultFaults();
    Faults.ReportedVersionShift = 1;
    BootWith(Dsp, Context, &Parsed, &Dma, &Faults, FwloadVersionMismatch, FwloadStateFailed);
    /* D3 of the p2b3 simulator rerun: a tuple length that is not a multiple of four and a version-only reply are named failures, not a boot with 0-byte mailboxes */
    Faults = *FakeDspDefaultFaults();
    Faults.TupleFault = FAKE_DSP_TUPLES_LENGTH_THREE;
    BootWith(Dsp, Context, &Parsed, &Dma, &Faults, FwloadIpcBadReply, FwloadStateFailed);
    ok(Context->FwConfig.BadTuple != FALSE, "the refused tuple was not named\n");
    ok_eq_uint(Context->FwConfig.BadTupleOffset, 16);
    Faults.TupleFault = FAKE_DSP_TUPLES_VERSION_ONLY;
    BootWith(Dsp, Context, &Parsed, &Dma, &Faults, FwloadTruncated, FwloadStateFailed);
    ok_eq_uint(Context->FwConfig.TupleCount, 1);
    Faults = *FakeDspDefaultFaults();
    Faults.SubsystemNeverPowers = TRUE;
    BootWith(Dsp, Context, &Parsed, &Dma, &Faults, FwloadTimeoutPowerUp, FwloadStateFailed);

    for (Index = 0; Index < sizeof(FwloadCaseRomErrors) / sizeof(FwloadCaseRomErrors[0]); Index++)
    {
        Faults = *FakeDspDefaultFaults();
        Faults.HaltStage = FAKE_DSP_HALT_AFTER_PURGE;
        Faults.HaltCode = FwloadCaseRomErrors[Index].Code;
        BootWith(Dsp, Context, &Parsed, &Dma, &Faults, FwloadRomError, FwloadStateFailed);
        ok_eq_uint(Context->RomError, FwloadCaseRomErrors[Index].Code);
    }

    /* the descriptor list of the stream is checked by the model */
    FakeDspInit(Dsp, FwloadPlatformArlS(), NULL);
    Dma.BdlCapacity = 1;
    ok_eq_int(FwloadClDmaPrepare(&Dma, Image + Parsed.PayloadOffset, Parsed.PayloadBytes), FwloadOk);
    ok_eq_uint(Dma.FragmentCount, 1);
    Dma.Bdl[0].Bytes -= 16;
    ok_eq_int(FakeDspOps()->StreamSubmit(Dsp, &Dma), FwloadHardwareFault);
    ok((Dsp->DmaErrors & FAKE_DSP_DMA_BYTES) != 0, "short descriptor list accepted\n");
    ok_eq_int(FwloadClDmaEntryCount(&Segment, 1, Parsed.PayloadBytes, 0x1000), 10);
    ok_eq_int(FwloadClDmaEntryCount(&Segment, 1, Parsed.PayloadBytes, 0), 1);
    ok_eq_int(FwloadClDmaPrepare(&Dma, Image + Parsed.PayloadOffset, 0), FwloadBadLength);

    /* R11 of the branch review: the descriptor list at the 4 GiB boundary the driver asks for, over the shared rows */
    for (Index = 0; Index < sizeof(FwloadCaseBoundaries) / sizeof(FwloadCaseBoundaries[0]); Index++)
    {
        Segment.Address = FwloadCaseBoundaries[Index].Address;
        Segment.Bytes = (SIZE_T)FwloadCaseBoundaries[Index].SegmentBytes;
        ok_eq_size(FwloadClDmaEntryCount(&Segment, 1, (SIZE_T)FwloadCaseBoundaries[Index].PayloadBytes, FwloadCaseBoundaries[Index].Boundary),
                   FwloadCaseBoundaries[Index].Entries);
    }
    /* the first row built: the segment that straddles the boundary is cut exactly there, and the model accepts the list */
    Segment.Address = 0xFFFF8000ULL;
    Segment.Bytes = 0x9000;
    Dma.Capacity = 0x9000;
    Dma.SegmentCount = 1;
    Dma.BdlCapacity = 256;
    Dma.BdlBytes = sizeof(Bdl);
    Dma.EntryBoundary = FWLOAD_CL_DMA_BOUNDARY_BYTES;
    ok_eq_int(FwloadClDmaPrepare(&Dma, Image + Parsed.PayloadOffset, 0x9000), FwloadOk);
    ok_eq_uint(Dma.FragmentCount, 2);
    ok_eq_uint(Dma.Bdl[0].AddressLow, 0xFFFF8000UL);
    ok_eq_uint(Dma.Bdl[0].AddressHigh, 0);
    ok_eq_uint(Dma.Bdl[0].Bytes, 0x8000);
    ok_eq_uint(Dma.Bdl[1].AddressLow, 0);
    ok_eq_uint(Dma.Bdl[1].AddressHigh, 1);
    ok_eq_uint(Dma.Bdl[1].Bytes, 0x1000);
    FakeDspInit(Dsp, FwloadPlatformArlS(), NULL);
    ok_eq_int(FakeDspOps()->StreamSubmit(Dsp, &Dma), FwloadOk);
    ok_eq_uint(Dsp->DmaErrors, 0);

    /* R8 of the branch review: the payload copy is byte-exact for an odd length and leaves the byte after it alone */
    Segment.Address = 0x10000000ULL;
    Segment.Bytes = FWLOAD_TEST_PAYLOAD;
    Dma.Capacity = FWLOAD_TEST_PAYLOAD;
    Dma.EntryBoundary = 0;
    for (Index = 0; Index < sizeof(OddLengths) / sizeof(OddLengths[0]); Index++)
    {
        RtlFillMemory(Virtual, FWLOAD_TEST_PAYLOAD, 0xA5);
        ok_eq_int(FwloadClDmaPrepare(&Dma, Image + Parsed.PayloadOffset, OddLengths[Index]), FwloadOk);
        ok(RtlCompareMemory(Virtual, Image + Parsed.PayloadOffset, OddLengths[Index]) == OddLengths[Index],
           "the copy of %Iu bytes differs\n", OddLengths[Index]);
        ok_eq_uint(Virtual[OddLengths[Index]], 0xA5);
        ok_eq_uint(Dma.Bytes, OddLengths[Index]);
    }

    CheckBusBound();

    ExFreePoolWithTag(Virtual, FWLOAD_TEST_TAG);
    ExFreePoolWithTag(Context, FWLOAD_TEST_TAG);
    ExFreePoolWithTag(Dsp, FWLOAD_TEST_TAG);
    ExFreePoolWithTag(Image, FWLOAD_TEST_TAG);
}
