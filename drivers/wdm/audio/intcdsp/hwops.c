/*
 * PROJECT:     LiberNT Intel Smart Sound DSP driver
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     The hardware side of the firmware loader's operations table: BAR4 register and window access, the clock, the hdaudbus code-loader stream and the interrupt callback
 * COPYRIGHT:   Copyright 2026 LiberNT contributors
 */

#include "intcdsp.h"

#define INTCDSP_STALL_MAX_US     2000

static UINT64 IntcDspFrequency;

static
UINT32
IntcDspRead32(
    _In_ PINTCDSP_DEVICE Dev,
    _In_ UINT32 Offset)
{
    return READ_REGISTER_ULONG((volatile ULONG*)(Dev->AdspBar.Base.BasePtr + Offset));
}

static
UINT32
IntcDspOpRead32(
    PVOID Context,
    UINT32 Bar,
    UINT32 Offset)
{
    PINTCDSP_DEVICE Dev = Context;
    PUCHAR Base;

    switch (Bar)
    {
    case FWLOAD_BAR_HDA:
        if (Offset > Dev->HdaBar.Len || Dev->HdaBar.Len - Offset < sizeof(ULONG))
            return 0xFFFFFFFF;
        Base = Dev->HdaBar.Base.BasePtr;
        break;
    case FWLOAD_BAR_PP:
        if (!Dev->Ppcap)
            return 0xFFFFFFFF;
        Base = Dev->Ppcap;
        break;
    case FWLOAD_BAR_DSP:
        if (Offset > Dev->AdspBar.Len || Dev->AdspBar.Len - Offset < sizeof(ULONG))
            return 0xFFFFFFFF;
        Base = Dev->AdspBar.Base.BasePtr;
        break;
    default:
        return 0xFFFFFFFF;
    }
    return READ_REGISTER_ULONG((volatile ULONG*)(Base + Offset));
}

static
VOID
IntcDspOpWrite32(
    PVOID Context,
    UINT32 Bar,
    UINT32 Offset,
    UINT32 Value)
{
    PINTCDSP_DEVICE Dev = Context;
    PUCHAR Base;

    switch (Bar)
    {
    case FWLOAD_BAR_HDA:
        if (Offset > Dev->HdaBar.Len || Dev->HdaBar.Len - Offset < sizeof(ULONG))
            return;
        Base = Dev->HdaBar.Base.BasePtr;
        break;
    case FWLOAD_BAR_PP:
        if (!Dev->Ppcap)
            return;
        Base = Dev->Ppcap;
        break;
    case FWLOAD_BAR_DSP:
        if (Offset > Dev->AdspBar.Len || Dev->AdspBar.Len - Offset < sizeof(ULONG))
            return;
        Base = Dev->AdspBar.Base.BasePtr;
        break;
    default:
        return;
    }
    WRITE_REGISTER_ULONG((volatile ULONG*)(Base + Offset), Value);
}

static
BOOLEAN
IntcDspOpReadMemory(
    PVOID Context,
    UINT32 Offset,
    UINT8* Buffer,
    SIZE_T Bytes)
{
    PINTCDSP_DEVICE Dev = Context;
    volatile ULONG* Source;
    PULONG Target;
    SIZE_T Index;

    if ((Bytes & (sizeof(ULONG) - 1)) != 0 || Offset > Dev->AdspBar.Len || Bytes > Dev->AdspBar.Len - Offset)
        return FALSE;
    Source = (volatile ULONG*)(Dev->AdspBar.Base.BasePtr + Offset);
    Target = (PULONG)Buffer;
    for (Index = 0; Index < Bytes / sizeof(ULONG); Index++)
        Target[Index] = READ_REGISTER_ULONG(Source + Index);
    return TRUE;
}

static
UINT64
IntcDspOpNowUs(
    PVOID Context)
{
    LARGE_INTEGER Now;
    LONGLONG Ticks, Frequency;

    UNREFERENCED_PARAMETER(Context);
    /* the counter is the return value; the out parameter, when given, receives the frequency instead */
    Now = KeQueryPerformanceCounter(NULL);
    Ticks = Now.QuadPart;
    Frequency = (LONGLONG)IntcDspFrequency;
    return (UINT64)(Ticks / Frequency) * 1000000ULL + (UINT64)(Ticks % Frequency) * 1000000ULL / IntcDspFrequency;
}

static
VOID
IntcDspOpDelayUs(
    PVOID Context,
    UINT32 Microseconds)
{
    LARGE_INTEGER Interval;

    UNREFERENCED_PARAMETER(Context);
    if (Microseconds <= INTCDSP_STALL_MAX_US)
    {
        KeStallExecutionProcessor(Microseconds);
        return;
    }
    Interval.QuadPart = -10 * (LONGLONG)Microseconds;
    KeDelayExecutionThread(KernelMode, FALSE, &Interval);
}

static
FWLOAD_STATUS
IntcDspOpStreamSubmit(
    PVOID Context,
    PFWLOAD_CL_DMA Dma)
{
    PINTCDSP_DEVICE Dev = Context;

    if (!Dev->StreamPrepared || Dev->StreamTag != Dma->StreamTag || Dev->StreamFragments != Dma->FragmentCount)
        return FwloadBadState;
    Dev->Bus.DSPEnableSPIB(Dev->Bus.Context, Dev->Stream, Dma->Spib);
    IntcDspLog("code-loader stream %u: %I64u bytes in %u descriptors (LVI %u), SPIB %lu, format 0x%04x\n",
               Dma->StreamTag, (ULONGLONG)Dma->Bytes, (ULONG)Dma->FragmentCount, Dma->LastValidIndex, Dma->Spib,
               Dma->StreamFormat);
    return FwloadOk;
}

static
FWLOAD_STATUS
IntcDspOpStreamTrigger(
    PVOID Context,
    BOOLEAN Start)
{
    PINTCDSP_DEVICE Dev = Context;

    if (!Dev->StreamPrepared)
        return FwloadBadState;
    Dev->Bus.TriggerDSP(Dev->Bus.Context, Dev->Stream, Start);
    return FwloadOk;
}

static
VOID
IntcDspOpStreamRelease(
    PVOID Context)
{
    IntcDspReleaseStream((PINTCDSP_DEVICE)Context);
}

static
VOID
IntcDspOpLog(
    PVOID Context,
    const char* Text)
{
    UNREFERENCED_PARAMETER(Context);
    IntcDspLog("%s\n", Text);
}

/* The bus driver's interrupt lock, the one its service routine holds while it calls the callback: the loader takes it around every passive-level read-modify-write of the interrupt enables */
static
VOID
IntcDspOpLock(
    PVOID Context)
{
    PINTCDSP_DEVICE Dev = Context;

    Dev->Bus.AcquireInterruptLock(Dev->Bus.Context);
}

static
VOID
IntcDspOpUnlock(
    PVOID Context)
{
    PINTCDSP_DEVICE Dev = Context;

    Dev->Bus.ReleaseInterruptLock(Dev->Bus.Context);
}

static const FWLOAD_OPS IntcDspOps =
{
    IntcDspOpRead32,
    IntcDspOpWrite32,
    IntcDspOpReadMemory,
    IntcDspOpNowUs,
    IntcDspOpDelayUs,
    IntcDspOpStreamSubmit,
    IntcDspOpStreamTrigger,
    IntcDspOpStreamRelease,
    IntcDspOpLog,
    IntcDspOpLock,
    IntcDspOpUnlock
};

const FWLOAD_OPS*
IntcDspOpsTable(
    VOID)
{
    return &IntcDspOps;
}

VOID
IntcDspInitClock(
    VOID)
{
    LARGE_INTEGER Frequency;

    /* the out parameter is where the HAL reports the frequency */
    KeQueryPerformanceCounter(&Frequency);
    IntcDspFrequency = (UINT64)Frequency.QuadPart;
}

UINT64
IntcDspClockFrequency(
    VOID)
{
    return IntcDspFrequency;
}

/* The register a raw interrupt pointer names, 0xFFFFFFFF when it names none: the loader's rules (all ones, zero, an odd address, the field's all ones) and the BAR4 range */
static
UINT32
IntcDspInterruptOffset(
    _In_ PINTCDSP_DEVICE Dev,
    _In_ UINT32 Pointer)
{
    UINT32 Offset = FwloadInterruptOffset(Pointer);

    if (Offset == 0xFFFFFFFF || Offset > Dev->AdspBar.Len || Dev->AdspBar.Len - Offset < sizeof(ULONG))
        return 0xFFFFFFFF;
    return Offset;
}

static
int
IntcDspInterruptCallback(
    PVOID Context)
{
    PINTCDSP_DEVICE Dev = Context;

    /* the callback stays registered for the device's lifetime; a boot arms it for its own duration, and the bus driver calls it under the lock the operations table hands the loader */
    if (!Dev->InterruptArmed)
        return FALSE;
    return FwloadInterruptService(&Dev->Fw) ? TRUE : FALSE;
}

PADSP_INTERRUPT_CALLBACK
IntcDspInterruptRoutine(
    VOID)
{
    return IntcDspInterruptCallback;
}

VOID
IntcDspProbe(
    _Inout_ PINTCDSP_DEVICE Dev)
{
    const FWLOAD_PLATFORM* Platform = FwloadPlatformArlS();
    UINT32 CapId, Offset, State;

    CapId = (Dev->PpCapHeader & INTCDSP_CAP_ID_MASK) >> INTCDSP_CAP_ID_SHIFT;
    IntcDspLog("controller %04x:%04x: BAR0 %p (0x%lx bytes), BAR4 %p (0x%lx bytes), PP capability %p: header 0x%08x (id %u, version %u, next 0x%04x), PPCTL 0x%08x (GPROCEN %u), PPSTS 0x%08x\n",
               Dev->VenId, Dev->DevId, Dev->HdaBar.Base.Base, Dev->HdaBar.Len, Dev->AdspBar.Base.Base, Dev->AdspBar.Len,
               Dev->Ppcap, Dev->PpCapHeader, CapId, Dev->PpCapHeader >> INTCDSP_CAP_VERSION_SHIFT,
               Dev->PpCapHeader & INTCDSP_CAP_NEXT_MASK, Dev->PpCtl,
               (Dev->PpCtl & FWLOAD_PPCTL_GPROCEN) ? 1 : 0, Dev->PpSts);

    Dev->NhltStatus = NhltParseNoTable;
    if (Dev->Nhlt.Nhlt && Dev->Nhlt.NhltSz)
    {
        RtlZeroMemory(&Dev->NhltSummary, sizeof(Dev->NhltSummary));
        Dev->NhltStatus = NhltParseTable((const UINT8*)Dev->Nhlt.Nhlt, Dev->Nhlt.NhltSz, &Dev->NhltSummary);
        IntcDspLog("NHLT table %p (%I64u bytes) from the _DSM: %s, %u of %u endpoints parsed, links HDA %u DSP %u PDM %u SSP %u SDW %u other %u\n",
                   Dev->Nhlt.Nhlt, Dev->Nhlt.NhltSz, NhltParseStatusName(Dev->NhltStatus),
                   Dev->NhltSummary.EndpointsParsed, Dev->NhltSummary.EndpointsCount,
                   Dev->NhltSummary.LinkCounts[NHLT_LINKTYPE_HDA], Dev->NhltSummary.LinkCounts[NHLT_LINKTYPE_DSP],
                   Dev->NhltSummary.LinkCounts[NHLT_LINKTYPE_PDM], Dev->NhltSummary.LinkCounts[NHLT_LINKTYPE_SSP],
                   Dev->NhltSummary.LinkCounts[NHLT_LINKTYPE_SDW], Dev->NhltSummary.OtherLinkCount);
    }
    else
    {
        IntcDspLog("NHLT table: the bus driver reports none\n");
    }

    /* BAR4 is read only for the controller this driver boots (R9): on the others, probe-only, the DSP's registers are left alone while the DSP is powered down */
    if (!Dev->Platform)
    {
        IntcDspLog("controller %04x has no register map: BAR4 is not read, the DSP is left alone\n", Dev->DevId);
        return;
    }

    if (!Dev->AdspBar.Base.Base || Dev->AdspBar.Len < Platform->RomError + sizeof(ULONG))
    {
        IntcDspLog("BAR4 (%p, 0x%lx bytes) is too short for the ACE 1.0 register map (needs 0x%x bytes); no DSP register read\n",
                   Dev->AdspBar.Base.Base, Dev->AdspBar.Len, Platform->RomError + (UINT32)sizeof(ULONG));
        return;
    }

    IntcDspLog("reading BAR4 at +0x%x +0x%x +0x%x +0x%x +0x%x +0x%x\n",
               Platform->SubsystemControl, Platform->InterruptPointer, Platform->HostIpcInterruptEnable,
               Platform->IpcControl, Platform->RomStatus, Platform->RomError);
    Dev->Adspcs = IntcDspRead32(Dev, Platform->SubsystemControl);
    Dev->IntPointer = IntcDspRead32(Dev, Platform->InterruptPointer);
    Dev->HfpIe = IntcDspRead32(Dev, Platform->HostIpcInterruptEnable);
    Dev->IpcCtl = IntcDspRead32(Dev, Platform->IpcControl);
    IntcDspLog("BAR4 +0x%x ADSPCS 0x%08x (SPA %u, CPA %u), +0x%x HFINTIPPTR 0x%08x, +0x%x HfHIPCIE 0x%08x, +0x%x HFIPCXCTL 0x%08x\n",
               Platform->SubsystemControl, Dev->Adspcs, (Dev->Adspcs >> 16) & 1, (Dev->Adspcs >> 24) & 1,
               Platform->InterruptPointer, Dev->IntPointer, Platform->HostIpcInterruptEnable, Dev->HfpIe,
               Platform->IpcControl, Dev->IpcCtl);
    Offset = IntcDspInterruptOffset(Dev, Dev->IntPointer);
    if (Offset != 0xFFFFFFFF)
        IntcDspLog("BAR4 +0x%x (the interrupt enable register the pointer names) 0x%08x\n", Offset, IntcDspRead32(Dev, Offset));
    else
        IntcDspLog("HFINTIPPTR 0x%08x names no 32-bit register inside BAR4: the host IPC interrupt enable cannot be reached\n", Dev->IntPointer);

    Dev->Fsr = IntcDspRead32(Dev, Platform->RomStatus);
    Dev->RomError = IntcDspRead32(Dev, Platform->RomError);
    State = Dev->Fsr & FWLOAD_FSR_STATE_MASK;
    IntcDspLog("BAR4 +0x%x FSR 0x%08x: halted %u, module %u, wait state 0x%x, state 0x%02x %s; +0x%x ROM error 0x%08x %s\n",
               Platform->RomStatus, Dev->Fsr, (Dev->Fsr & FWLOAD_FSR_HALTED) ? 1 : 0,
               (Dev->Fsr & FWLOAD_FSR_MODULE_MASK) >> FWLOAD_FSR_MODULE_SHIFT,
               (Dev->Fsr & FWLOAD_FSR_WAIT_STATE_MASK) >> FWLOAD_FSR_WAIT_STATE_SHIFT, State,
               (Dev->Fsr == 0xFFFFFFFF) ? "(all ones, DSP powered down)" : FwloadFsrStateName(State),
               Platform->RomError, Dev->RomError,
               (Dev->Fsr & FWLOAD_FSR_HALTED) ? FwloadRomErrorName(Dev->RomError) : "");
}
