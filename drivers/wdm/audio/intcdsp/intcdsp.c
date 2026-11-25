/*
 * PROJECT:     LiberNT Intel Smart Sound DSP driver
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Function driver of the hdaudbus DSP child: logs the controller's facts at start and boots the DSP with Sound Open Firmware when the BootDsp value asks for it
 * COPYRIGHT:   Copyright 2026 LiberNT contributors
 */

#include "intcdsp.h"

static
EVT_WDF_DEVICE_PREPARE_HARDWARE IntcDspEvtDevicePrepareHardware;
static
EVT_WDF_DEVICE_RELEASE_HARDWARE IntcDspEvtDeviceReleaseHardware;
static
EVT_WDF_DEVICE_D0_ENTRY IntcDspEvtDeviceD0Entry;
static
EVT_WDF_DEVICE_D0_EXIT IntcDspEvtDeviceD0Exit;

/* The code-loader stream carries 48 kHz, 32 bits, one channel: Linux hda.h HDA_CL_STREAM_FORMAT (the stream descriptor format register value 0x40) */
#define INTCDSP_CL_SAMPLE_RATE       48000
#define INTCDSP_CL_BITS_PER_SAMPLE   32

static
NTSTATUS
IntcDspReadBootDsp(
    _Out_ PULONG Value)
{
    OBJECT_ATTRIBUTES Attributes;
    UNICODE_STRING Path, Name;
    UCHAR Buffer[sizeof(KEY_VALUE_PARTIAL_INFORMATION) + sizeof(ULONG)];
    PKEY_VALUE_PARTIAL_INFORMATION Info;
    HANDLE Key;
    ULONG Result;
    NTSTATUS Status;

    *Value = 0;
    RtlInitUnicodeString(&Path, INTCDSP_SETTINGS_PATH);
    RtlInitUnicodeString(&Name, INTCDSP_SETTINGS_VALUE);
    InitializeObjectAttributes(&Attributes, &Path, OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE, NULL, NULL);
    Status = ZwOpenKey(&Key, KEY_QUERY_VALUE, &Attributes);
    if (!NT_SUCCESS(Status))
        return Status;
    Status = ZwQueryValueKey(Key, &Name, KeyValuePartialInformation, Buffer, sizeof(Buffer), &Result);
    ZwClose(Key);
    if (!NT_SUCCESS(Status))
        return Status;
    Info = (PKEY_VALUE_PARTIAL_INFORMATION)Buffer;
    if (Info->Type != REG_DWORD || Info->DataLength != sizeof(ULONG))
        return STATUS_OBJECT_TYPE_MISMATCH;
    RtlCopyMemory(Value, Info->Data, sizeof(ULONG));
    return STATUS_SUCCESS;
}

VOID
IntcDspReleaseStream(
    _Inout_ PINTCDSP_DEVICE Dev)
{
    NTSTATUS Status;

    if (!Dev->Stream)
        return;
    Dev->Bus.TriggerDSP(Dev->Bus.Context, Dev->Stream, FALSE);
    if (Dev->StreamPrepared)
        Dev->Bus.CleanupDSP(Dev->Bus.Context, Dev->Stream);
    Dev->StreamPrepared = FALSE;
    Status = Dev->Bus.FreeStream(Dev->Bus.Context, Dev->Stream);
    if (!NT_SUCCESS(Status))
        IntcDspLog("stream %u: not freed, 0x%08lx\n", Dev->StreamTag, Status);
    Dev->Stream = NULL;
}

VOID
IntcDspPowerDown(
    _Inout_ PINTCDSP_DEVICE Dev)
{
    FWLOAD_STATUS Status = FwloadOk;
    NTSTATUS NtStatus;

    Dev->InterruptArmed = FALSE;
    if (Dev->Fw.State != FwloadStateOff)
    {
        Status = FwloadPowerDown(&Dev->Fw);
        IntcDspLog("DSP power down from state %s: %s, cleanup %s\n", FwloadStateName(Dev->Fw.State),
                   FwloadStatusName(Status), FwloadStatusName(Dev->Fw.CleanupStatus));
    }
    Dev->Booted = FALSE;
    if (Dev->InterruptRegistered)
    {
        Dev->Bus.UnregisterInterrupt(Dev->Bus.Context);
        Dev->InterruptRegistered = FALSE;
    }
    if (Dev->IdleHeld)
    {
        NtStatus = Dev->Bus.SetDSPPowerState(Dev->Bus.Context, PowerDeviceD3);
        Dev->IdleHeld = FALSE;
        if (!NT_SUCCESS(NtStatus))
            IntcDspLog("idle reference not released: 0x%08lx\n", NtStatus);
    }
}

static
VOID
IntcDspBootStop(
    _Inout_ PINTCDSP_DEVICE Dev,
    _In_ const char* Step,
    _In_ NTSTATUS Status)
{
    IntcDspLog("boot stopped at %s: 0x%08lx; the DSP is not started\n", Step, Status);
    IntcDspReleaseStream(Dev);
    Dev->LastStatus = FwloadHardwareFault;
    if (Dev->DmaBuffer)
        MmFreeContiguousMemorySpecifyCache(Dev->DmaBuffer, (ULONG)Dev->DmaBytes, MmNonCached);
    Dev->DmaBuffer = NULL;
    Dev->DmaBytes = 0;
    RtlZeroMemory(&Dev->Segment, sizeof(Dev->Segment));
    IntcDspFreeFirmware(Dev);
}

NTSTATUS
IntcDspBoot(
    _Inout_ PINTCDSP_DEVICE Dev)
{
    const FWLOAD_PLATFORM* Platform = Dev->Platform;
    FWLOAD_TIMEOUTS Timeouts;
    FWLOAD_CL_DMA Dma;
    HDAUDIO_STREAM_FORMAT Format;
    PHYSICAL_ADDRESS Low, High, Boundary;
    const SOFIPC4_FW_REGS* Registers;
    PVOID Bdl = NULL;
    SIZE_T Entries;
    FWLOAD_STATUS Status;
    NTSTATUS NtStatus;

    if (Dev->Booted)
        return STATUS_SUCCESS;
    if (!Platform)
    {
        IntcDspLog("controller %04x has no register map; the DSP is left alone\n", Dev->DevId);
        return STATUS_SUCCESS;
    }
    if (!Dev->AdspBar.Base.Base || Dev->AdspBar.Len < INTCDSP_BAR4_MIN_BYTES || !Dev->Ppcap)
    {
        IntcDspLog("BAR4 (%p, 0x%lx bytes, needs 0x%x) or the processing capabilities (%p) are missing; the DSP is left alone\n",
                   Dev->AdspBar.Base.Base, Dev->AdspBar.Len, INTCDSP_BAR4_MIN_BYTES, Dev->Ppcap);
        return STATUS_SUCCESS;
    }

    NtStatus = IntcDspLoadFirmware(Dev);
    if (!NT_SUCCESS(NtStatus))
    {
        Dev->LastStatus = FwloadNoData;
        return NtStatus;
    }

    Dev->DmaBytes = (SIZE_T)ROUND_TO_PAGES((ULONG)Dev->Ri.PayloadBytes);
    Low.QuadPart = 0;
    High.QuadPart = -1;
    Boundary.QuadPart = 0;
    Dev->DmaBuffer = MmAllocateContiguousMemorySpecifyCache((ULONG)Dev->DmaBytes, Low, High, Boundary, MmNonCached);
    if (!Dev->DmaBuffer)
    {
        NtStatus = STATUS_INSUFFICIENT_RESOURCES;
        goto Fail;
    }
    Dev->Segment.Address = MmGetPhysicalAddress(Dev->DmaBuffer).QuadPart;
    Dev->Segment.Bytes = Dev->DmaBytes;
    Entries = FwloadClDmaEntryCount(&Dev->Segment, 1, Dev->Ri.PayloadBytes, 0);
    if (Entries == 0 || Entries > INTCDSP_BDL_ENTRIES)
    {
        IntcDspLog("the payload needs %I64u descriptors, the stream has %u\n", (ULONGLONG)Entries, (ULONG)INTCDSP_BDL_ENTRIES);
        NtStatus = STATUS_INVALID_PARAMETER;
        goto Fail;
    }

    RtlZeroMemory(&Format, sizeof(Format));
    Format.SampleRate = INTCDSP_CL_SAMPLE_RATE;
    Format.ValidBitsPerSample = INTCDSP_CL_BITS_PER_SAMPLE;
    Format.ContainerSize = INTCDSP_CL_BITS_PER_SAMPLE;
    Format.NumberOfChannels = 1;
    Dev->Stream = NULL;
    Dev->StreamTag = 0;
    NtStatus = Dev->Bus.GetRenderStream(Dev->Bus.Context, Format, &Dev->Stream, &Dev->StreamTag);
    if (!NT_SUCCESS(NtStatus))
        goto Fail;
    Dev->StreamFragments = (ULONG)Entries;
    NtStatus = Dev->Bus.PrepareDSP(Dev->Bus.Context, Dev->Stream, (unsigned int)Dev->Ri.PayloadBytes,
                                   (int)Entries, &Bdl);
    if (!NT_SUCCESS(NtStatus))
        goto Fail;
    if (!Bdl)
    {
        IntcDspLog("stream %u: the bus prepared it without a descriptor list\n", Dev->StreamTag);
        NtStatus = STATUS_DEVICE_PROTOCOL_ERROR;
        goto Fail;
    }
    Dev->StreamPrepared = TRUE;

    RtlZeroMemory(&Dma, sizeof(Dma));
    Dma.Virtual = Dev->DmaBuffer;
    Dma.Capacity = Dev->DmaBytes;
    Dma.Segment = &Dev->Segment;
    Dma.SegmentCount = 1;
    Dma.Bdl = (PFWLOAD_BDL_ENTRY)Bdl;
    Dma.BdlCapacity = INTCDSP_BDL_ENTRIES;
    Dma.BdlBytes = INTCDSP_BDL_BYTES;
    Dma.EntryBoundary = 0;
    Dma.StreamTag = Dev->StreamTag;
    Dma.StreamFormat = FWLOAD_CL_STREAM_FORMAT;
    FwloadDefaultTimeouts(&Timeouts);
    FwloadInitContext(&Dev->Fw, IntcDspOpsTable(), Dev, Platform, &Dev->Ri, &Dma, &Timeouts);

    NtStatus = Dev->Bus.SetDSPPowerState(Dev->Bus.Context, PowerDeviceD0);
    if (!NT_SUCCESS(NtStatus))
        goto Fail;
    Dev->IdleHeld = TRUE;

    IntcDspLog("booting: firmware '%s' %u.%u.%u.%u, %I64u payload bytes in %u descriptors on stream %u, BAR4 %p, clock %I64u us at %I64u Hz\n",
               Dev->Ri.Name, Dev->Ri.Major, Dev->Ri.Minor, Dev->Ri.Hotfix, Dev->Ri.Build, (ULONGLONG)Dev->Ri.PayloadBytes,
               (ULONG)Entries, Dev->StreamTag, Dev->AdspBar.Base.Base, (ULONGLONG)IntcDspOpsTable()->NowUs(Dev),
               IntcDspClockFrequency());

    Dev->InterruptArmed = TRUE;
    NtStatus = Dev->Bus.RegisterInterrupt(Dev->Bus.Context, IntcDspInterruptRoutine(), Dev);
    Dev->InterruptRegistered = NT_SUCCESS(NtStatus);
    if (!Dev->InterruptRegistered)
        IntcDspLog("interrupt callback not registered: 0x%08lx; the boot polls on\n", NtStatus);

    Status = FwloadBoot(&Dev->Fw);
    Dev->InterruptArmed = FALSE;
    Dev->LastStatus = Status;
    IntcDspLog("boot %s after %I64u us (clock %I64u us): state %s, failed in %s (%s), ROM status 0x%08x %s, ROM error 0x%08x %s, IPC status %u, last DSP message 0x%08x|0x%08x, attempt %u, %lu DSP interrupts, cleanup %s\n",
               FwloadStatusName(Status), Dev->Fw.BootUs, (ULONGLONG)IntcDspOpsTable()->NowUs(Dev), FwloadStateName(Dev->Fw.State), FwloadStateName(Dev->Fw.FailedState),
               FwloadStatusName(Dev->Fw.Status), Dev->Fw.RomStatus, FwloadFsrStateName(Dev->Fw.RomStatus & FWLOAD_FSR_STATE_MASK),
               Dev->Fw.RomError, FwloadRomErrorName(Dev->Fw.RomError), Dev->Fw.LastIpcStatus, Dev->Fw.LastIpcPrimary, Dev->Fw.LastIpcExtension, Dev->Fw.Attempt,
               Dev->Fw.InterruptCount, FwloadStatusName(Dev->Fw.CleanupStatus));
    /* the guard tripped: the enables do not gate the line on this controller, the service routine stopped claiming it, and it is taken off the line for good */
    if (Dev->Fw.InterruptStorm)
    {
        IntcDspLog("DSP interrupt storm: %lu interrupts in a row without a message consumed, the interrupt callback is disarmed and unregistered\n",
                   Dev->Fw.InterruptCount);
        if (Dev->InterruptRegistered)
        {
            Dev->Bus.UnregisterInterrupt(Dev->Bus.Context);
            Dev->InterruptRegistered = FALSE;
        }
    }
    if (Status == FwloadOk)
    {
        Registers = &Dev->Fw.FwConfig.Registers;
        Dev->Booted = TRUE;
        IntcDspLog("firmware ready: version %u.%u.%u.%u, ABI %u, ROM implementation %u.%u version %u.%u platform %u, ROM info 0x%08x, downlink mailbox %u bytes, uplink mailbox %u bytes, trace log %u bytes, %u pipelines, %u libraries, context save %u, %u tuples\n",
                   Dev->Fw.FwConfig.Major, Dev->Fw.FwConfig.Minor, Dev->Fw.FwConfig.Hotfix, Dev->Fw.FwConfig.Build,
                   Registers->AbiVersion, Registers->RomImplementationRevisionMajor, Registers->RomImplementationRevisionMinor,
                   Registers->RomImplementationVersionMajor, Registers->RomImplementationVersionMinor, Registers->RomPlatformType,
                   Registers->RomFuseValues, Dev->Fw.FwConfig.DownlinkMailboxBytes, Dev->Fw.FwConfig.UplinkMailboxBytes,
                   Dev->Fw.FwConfig.TraceLogBytes, Dev->Fw.FwConfig.MaxPipelines, Dev->Fw.FwConfig.MaxLibraries,
                   Dev->Fw.FwConfig.ContextSave, Dev->Fw.FwConfig.TupleCount);
    }
    else
    {
        /* the tuple walk says which tuple it refused (D3): a length that is not a multiple of four, a tuple without a value, or a type the firmware does not define */
        if (Dev->Fw.FwConfig.BadTuple)
            IntcDspLog("firmware configuration: tuple type 0x%x at offset 0x%Ix of the reply is malformed\n",
                       Dev->Fw.FwConfig.BadTupleType, Dev->Fw.FwConfig.BadTupleOffset);
        IntcDspPowerDown(Dev);
    }

    IntcDspReleaseStream(Dev);
    MmFreeContiguousMemorySpecifyCache(Dev->DmaBuffer, (ULONG)Dev->DmaBytes, MmNonCached);
    Dev->DmaBuffer = NULL;
    Dev->DmaBytes = 0;
    RtlZeroMemory(&Dev->Segment, sizeof(Dev->Segment));
    IntcDspFreeFirmware(Dev);
    return STATUS_SUCCESS;

Fail:
    IntcDspBootStop(Dev, "preparation", NtStatus);
    return STATUS_SUCCESS;
}

static
NTSTATUS
IntcDspEvtDevicePrepareHardware(
    _In_ WDFDEVICE Device,
    _In_ WDFCMRESLIST ResourcesRaw,
    _In_ WDFCMRESLIST ResourcesTranslated)
{
    PINTCDSP_DEVICE Dev = IntcDspGetData(Device);
    volatile ULONG* Ppcap;
    NTSTATUS Status;

    UNREFERENCED_PARAMETER(ResourcesRaw);
    UNREFERENCED_PARAMETER(ResourcesTranslated);

    Status = WdfFdoQueryForInterface(Device, &GUID_ADSP_BUS_INTERFACE, (PINTERFACE)&Dev->Bus, sizeof(Dev->Bus), 1, NULL);
    if (!NT_SUCCESS(Status))
    {
        IntcDspLog("no ADSP bus interface on the stack: 0x%08lx\n", Status);
        return Status;
    }
    if (Dev->Bus.Size < sizeof(ADSP_BUS_INTERFACE) || !Dev->Bus.Context || !Dev->Bus.GetResources ||
        !Dev->Bus.SetDSPPowerState || !Dev->Bus.RegisterInterrupt || !Dev->Bus.UnregisterInterrupt ||
        !Dev->Bus.GetRenderStream || !Dev->Bus.FreeStream || !Dev->Bus.PrepareDSP || !Dev->Bus.CleanupDSP ||
        !Dev->Bus.TriggerDSP || !Dev->Bus.DSPEnableSPIB ||
        !Dev->Bus.AcquireInterruptLock || !Dev->Bus.ReleaseInterruptLock)
    {
        IntcDspLog("ADSP bus interface version %u, size %u: incomplete\n", Dev->Bus.Version, Dev->Bus.Size);
        return STATUS_DEVICE_PROTOCOL_ERROR;
    }
    if (Dev->Bus.InterfaceReference)
        Dev->Bus.InterfaceReference(Dev->Bus.Context);
    Dev->BusValid = TRUE;

    Status = Dev->Bus.GetResources(Dev->Bus.Context, &Dev->HdaBar, &Dev->AdspBar, &Dev->Ppcap, &Dev->Nhlt, NULL);
    if (!NT_SUCCESS(Status))
    {
        IntcDspLog("GetResources: 0x%08lx\n", Status);
        return Status;
    }
    Dev->DevId = Dev->Bus.CtlrDevId;
    Dev->VenId = INTCDSP_VEN_INTEL;
    Dev->Platform = (Dev->DevId == INTCDSP_DEV_ARL_S) ? FwloadPlatformArlS() : NULL;

    Ppcap = Dev->Ppcap;
    if (Ppcap)
    {
        Dev->PpCapHeader = READ_REGISTER_ULONG(Ppcap);
        Dev->PpCtl = READ_REGISTER_ULONG(Ppcap + INTCDSP_PP_OFFSET_PPCTL / sizeof(ULONG));
        Dev->PpSts = READ_REGISTER_ULONG(Ppcap + INTCDSP_PP_OFFSET_PPSTS / sizeof(ULONG));
    }
    IntcDspProbe(Dev);
    if (NT_SUCCESS(IntcDspLoadFirmware(Dev)))
        IntcDspFreeFirmware(Dev);

    Status = IntcDspReadBootDsp(&Dev->BootDsp);
    if (NT_SUCCESS(Status))
        IntcDspLog("BootDsp = %lu: %s\n", Dev->BootDsp,
                   Dev->BootDsp ? (Dev->Platform ? "the DSP is booted at the next D0 entry" : "no register map for this controller, probe only")
                                : "probe only, the DSP is not touched");
    else
        IntcDspLog("BootDsp is not set (%s): probe only, the DSP is not touched\n",
                   (Status == STATUS_OBJECT_NAME_NOT_FOUND) ? "value absent" : "unreadable");
    return STATUS_SUCCESS;
}

static
NTSTATUS
IntcDspEvtDeviceReleaseHardware(
    _In_ WDFDEVICE Device,
    _In_ WDFCMRESLIST ResourcesTranslated)
{
    PINTCDSP_DEVICE Dev = IntcDspGetData(Device);

    UNREFERENCED_PARAMETER(ResourcesTranslated);
    IntcDspPowerDown(Dev);
    IntcDspReleaseStream(Dev);
    if (Dev->DmaBuffer)
        MmFreeContiguousMemorySpecifyCache(Dev->DmaBuffer, (ULONG)Dev->DmaBytes, MmNonCached);
    Dev->DmaBuffer = NULL;
    Dev->DmaBytes = 0;
    IntcDspFreeFirmware(Dev);
    if (Dev->BusValid && Dev->Bus.InterfaceDereference)
        Dev->Bus.InterfaceDereference(Dev->Bus.Context);
    Dev->BusValid = FALSE;
    RtlZeroMemory(&Dev->Bus, sizeof(Dev->Bus));
    Dev->Ppcap = NULL;
    Dev->Nhlt.Nhlt = NULL;
    Dev->Nhlt.NhltSz = 0;
    Dev->Platform = NULL;
    return STATUS_SUCCESS;
}

static
NTSTATUS
IntcDspEvtDeviceD0Entry(
    _In_ WDFDEVICE Device,
    _In_ WDF_POWER_DEVICE_STATE PreviousState)
{
    PINTCDSP_DEVICE Dev = IntcDspGetData(Device);

    UNREFERENCED_PARAMETER(PreviousState);
    if (!Dev->BusValid)
        return STATUS_SUCCESS;
    IntcDspReadBootDsp(&Dev->BootDsp);
    if (Dev->BootDsp)
        IntcDspBoot(Dev);
    return STATUS_SUCCESS;
}

static
NTSTATUS
IntcDspEvtDeviceD0Exit(
    _In_ WDFDEVICE Device,
    _In_ WDF_POWER_DEVICE_STATE TargetState)
{
    PINTCDSP_DEVICE Dev = IntcDspGetData(Device);

    UNREFERENCED_PARAMETER(TargetState);
    if (Dev->BusValid)
        IntcDspPowerDown(Dev);
    return STATUS_SUCCESS;
}

static
VOID
IntcDspEvtDeviceContextCleanup(
    _In_ WDFOBJECT Object)
{
    PINTCDSP_DEVICE Dev = IntcDspGetData(Object);

    Dev->InterruptArmed = FALSE;
}

NTSTATUS
IntcDspEvtDeviceAdd(
    _In_ WDFDRIVER Driver,
    _Inout_ PWDFDEVICE_INIT DeviceInit)
{
    WDF_OBJECT_ATTRIBUTES Attributes;
    WDF_PNPPOWER_EVENT_CALLBACKS PnpPower;
    WDFDEVICE Device;
    NTSTATUS Status;

    UNREFERENCED_PARAMETER(Driver);
    WDF_PNPPOWER_EVENT_CALLBACKS_INIT(&PnpPower);
    PnpPower.EvtDevicePrepareHardware = IntcDspEvtDevicePrepareHardware;
    PnpPower.EvtDeviceReleaseHardware = IntcDspEvtDeviceReleaseHardware;
    PnpPower.EvtDeviceD0Entry = IntcDspEvtDeviceD0Entry;
    PnpPower.EvtDeviceD0Exit = IntcDspEvtDeviceD0Exit;
    WdfDeviceInitSetPnpPowerEventCallbacks(DeviceInit, &PnpPower);

    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&Attributes, INTCDSP_DEVICE);
    Attributes.EvtCleanupCallback = IntcDspEvtDeviceContextCleanup;
    Status = WdfDeviceCreate(&DeviceInit, &Attributes, &Device);
    if (!NT_SUCCESS(Status))
        return Status;
    IntcDspLog("loaded for the Intel audio DSP child\n");
    return STATUS_SUCCESS;
}

NTSTATUS
DriverEntry(
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ PUNICODE_STRING RegistryPath)
{
    WDF_DRIVER_CONFIG Config;
    WDFDRIVER Driver;
    NTSTATUS Status;

    WDF_DRIVER_CONFIG_INIT(&Config, IntcDspEvtDeviceAdd);
    Config.DriverPoolTag = INTCDSP_POOL_TAG;
    Status = WdfDriverCreate(DriverObject, RegistryPath, WDF_NO_OBJECT_ATTRIBUTES, &Config, &Driver);
    if (!NT_SUCCESS(Status))
        return Status;
    IntcDspInitClock();
    return STATUS_SUCCESS;
}
