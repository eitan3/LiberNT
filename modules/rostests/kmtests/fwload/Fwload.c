/*
 * PROJECT:     LiberNT kernel-mode tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     The firmware loader reads a synthetic ACE image and boots the DSP model from it inside the kernel, and reports the model's faults
 * COPYRIGHT:   Copyright 2026 LiberNT contributors
 */

#include <kmt_test.h>
#include <fwload.h>
#include <fake_dsp.h>

#define NDEBUG
#include <debug.h>

#include "fwload_cases.h"

#define FWLOAD_TEST_TAG     'dlFw'
#define FWLOAD_TEST_PAYLOAD 0xA000

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
    FwloadInitContext(Context, FakeDspOps(Dsp), FwloadPlatformArlS(), Parsed, Dma, NULL);
    ok_eq_int(FwloadBoot(Context), Expected);
    ok_eq_uint(Context->State, ExpectedState);
    ok_eq_uint(Dsp->UnexpectedAccesses, 0);
    ok_eq_uint(Dsp->StreamSubmits, Dsp->StreamReleases);
    ok(FakeDspPowerEventSeen(Dsp, FAKE_POWER_GPROCEN_ON) != FALSE, "GPROCEN never enabled\n");
    if (ExpectedState != FwloadStateReady)
    {
        ok(FakeDspPowerEventSeen(Dsp, FAKE_POWER_SUBSYSTEM_OFF) != FALSE, "subsystem left on\n");
        ok(FakeDspPowerEventSeen(Dsp, FAKE_POWER_GPROCEN_OFF) != FALSE, "GPROCEN left on\n");
    }
    ok_eq_int(FwloadPowerDown(Context), FwloadOk);
    ok_eq_uint(Context->State, FwloadStateOff);
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
    FwloadCaseBuildImage(Image);
    ok_eq_int(SofRiParse(Image, FWLOAD_CASE_IMAGE_BYTES, Modules, 4, &Parsed), FwloadOk);

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
    Faults = *FakeDspDefaultFaults();
    Faults.AbiVersion = 2;
    BootWith(Dsp, Context, &Parsed, &Dma, &Faults, FwloadAbiMismatch, FwloadStateFailed);
    Faults = *FakeDspDefaultFaults();
    Faults.IpcStatus = SofIpc4Busy;
    BootWith(Dsp, Context, &Parsed, &Dma, &Faults, FwloadIpcError, FwloadStateFailed);
    Faults = *FakeDspDefaultFaults();
    Faults.ReportedVersionShift = 1;
    BootWith(Dsp, Context, &Parsed, &Dma, &Faults, FwloadVersionMismatch, FwloadStateFailed);
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
    ok_eq_int(Dsp->Ops.StreamSubmit(Dsp, &Dma), FwloadHardwareFault);
    ok((Dsp->DmaErrors & FAKE_DSP_DMA_BYTES) != 0, "short descriptor list accepted\n");
    ok_eq_int(FwloadClDmaEntryCount(&Segment, 1, Parsed.PayloadBytes, 0x1000), 10);
    ok_eq_int(FwloadClDmaEntryCount(&Segment, 1, Parsed.PayloadBytes, 0), 1);
    ok_eq_int(FwloadClDmaPrepare(&Dma, Image + Parsed.PayloadOffset, 0), FwloadBadLength);

    ExFreePoolWithTag(Virtual, FWLOAD_TEST_TAG);
    ExFreePoolWithTag(Context, FWLOAD_TEST_TAG);
    ExFreePoolWithTag(Dsp, FWLOAD_TEST_TAG);
    ExFreePoolWithTag(Image, FWLOAD_TEST_TAG);
}
