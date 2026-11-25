/*
 * PROJECT:     LiberNT host-native tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     A fake hdaudbus DSP child that answers the ADSP bus interface query the way the framework's query handler does, in version 1 or 2 and with the 64-bit DMA capability the case asks for
 * COPYRIGHT:   Copyright 2026 LiberNT contributors
 */

#ifdef FAKE_BUS_KERNEL
#include <kmt_test.h>
#else
#include "shim.h"
#endif
#include "fake_bus.h"

/* The members of version 1 answer success and do nothing */
static NTSTATUS NoResources(PVOID Context, PCI_BAR* HdaBar, PCI_BAR* AdspBar, PVOID* Ppcap, PNHLT_INFO NhltInfo, BUS_INTERFACE_STANDARD* PciConfig)
{
    return STATUS_SUCCESS;
}

static NTSTATUS NoPowerState(PVOID Context, DEVICE_POWER_STATE NewPowerState)
{
    return STATUS_SUCCESS;
}

static NTSTATUS NoRegisterInterrupt(PVOID Context, PADSP_INTERRUPT_CALLBACK Callback, PVOID CallbackContext)
{
    return STATUS_SUCCESS;
}

static NTSTATUS NoUnregisterInterrupt(PVOID Context)
{
    return STATUS_SUCCESS;
}

static NTSTATUS NoStream(PVOID Context, HDAUDIO_STREAM_FORMAT StreamFormat, HANDLE* Handle, UINT8* StreamTag)
{
    return STATUS_SUCCESS;
}

static NTSTATUS NoFreeStream(PVOID Context, HANDLE Handle)
{
    return STATUS_SUCCESS;
}

static NTSTATUS NoPrepareStream(PVOID Context, HANDLE Handle, unsigned int ByteSize, int Frags, PVOID* BdlBuf)
{
    return STATUS_SUCCESS;
}

static NTSTATUS NoCleanupStream(PVOID Context, HANDLE Handle)
{
    return STATUS_SUCCESS;
}

static UINT32 NoPosition(PVOID Context, HANDLE Handle)
{
    return 0;
}

static void NoStartStop(PVOID Context, HANDLE Handle, int StartStop)
{
}

static void NoEnableSpib(PVOID Context, HANDLE Handle, UINT32 Value)
{
}

static void NoDisableSpib(PVOID Context, HANDLE Handle)
{
}

static void NoLock(PVOID Context)
{
}

static void NoReference(PVOID Context)
{
}

static
NTSTATUS
FakeBusPlatformInfo(
    PVOID Context,
    PADSP_PLATFORM_INFO Info)
{
    PFAKE_BUS Bus = (PFAKE_BUS)Context;

    Bus->InfoCalls++;
    *Info = Bus->Info;
    return Bus->InfoStatus;
}

VOID
FakeBusInit(
    _Out_ PFAKE_BUS Bus,
    ULONG Version,
    BOOLEAN Dma64Bit)
{
    RtlZeroMemory(Bus, sizeof(*Bus));
    Bus->Interface.Size = Version >= 2 ? sizeof(ADSP_BUS_INTERFACE) : (USHORT)RTL_SIZEOF_THROUGH_FIELD(ADSP_BUS_INTERFACE, ReleaseInterruptLock);
    Bus->Interface.Version = (USHORT)Version;
    Bus->Interface.Context = Bus;
    Bus->Interface.InterfaceReference = NoReference;
    Bus->Interface.InterfaceDereference = NoReference;
    Bus->Interface.GetResources = NoResources;
    Bus->Interface.SetDSPPowerState = NoPowerState;
    Bus->Interface.RegisterInterrupt = NoRegisterInterrupt;
    Bus->Interface.UnregisterInterrupt = NoUnregisterInterrupt;
    Bus->Interface.GetRenderStream = NoStream;
    Bus->Interface.GetCaptureStream = NoStream;
    Bus->Interface.FreeStream = NoFreeStream;
    Bus->Interface.PrepareDSP = NoPrepareStream;
    Bus->Interface.CleanupDSP = NoCleanupStream;
    Bus->Interface.TriggerDSP = NoStartStop;
    Bus->Interface.StreamPosition = NoPosition;
    Bus->Interface.DSPEnableSPIB = NoEnableSpib;
    Bus->Interface.DSPDisableSPIB = NoDisableSpib;
    Bus->Interface.AcquireInterruptLock = NoLock;
    Bus->Interface.ReleaseInterruptLock = NoLock;
    Bus->Interface.GetPlatformInfo = FakeBusPlatformInfo;
    Bus->Info.Dma64Bit = Dma64Bit;
    Bus->InfoStatus = STATUS_SUCCESS;
}

NTSTATUS
FakeBusQueryInterface(
    _In_ PFAKE_BUS Bus,
    _Out_writes_bytes_(Size) PVOID Interface,
    ULONG Size,
    ULONG Version)
{
    if (Bus->Interface.Size > Size || Bus->Interface.Version > Version)
        return STATUS_INVALID_BUFFER_SIZE;
    RtlCopyMemory(Interface, &Bus->Interface, Bus->Interface.Size);
    return STATUS_SUCCESS;
}
