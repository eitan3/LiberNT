/*
 * PROJECT:     LiberNT Intel Smart Sound DSP driver
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Says whether the ADSP bus interface a query returned binds this driver, which DMA addresses the controller allows the code-loader buffer, and reads the names a topology selection uses from the bus
 * COPYRIGHT:   Copyright 2026 LiberNT contributors
 */

#include "adspbus.h"

BOOLEAN
IntcDspBusComplete(
    _In_ const ADSP_BUS_INTERFACE* Bus)
{
    return Bus->Size >= RTL_SIZEOF_THROUGH_FIELD(ADSP_BUS_INTERFACE, ReleaseInterruptLock) && Bus->Context &&
        Bus->GetResources && Bus->SetDSPPowerState && Bus->RegisterInterrupt && Bus->UnregisterInterrupt &&
        Bus->GetRenderStream && Bus->FreeStream && Bus->PrepareDSP && Bus->CleanupDSP && Bus->TriggerDSP &&
        Bus->DSPEnableSPIB && Bus->AcquireInterruptLock && Bus->ReleaseInterruptLock;
}

UINT64
IntcDspDmaHighBound(
    _In_ const ADSP_BUS_INTERFACE* Bus)
{
    ADSP_PLATFORM_INFO Info;

    /* the members of version 2 are the bus's only when it reported the whole structure; a bus that reports no capability gets the range every controller addresses */
    if (Bus->Size < RTL_SIZEOF_THROUGH_FIELD(ADSP_BUS_INTERFACE, GetPlatformInfo) || Bus->GetPlatformInfo == NULL)
        return MAXULONG32;
    RtlZeroMemory(&Info, sizeof(Info));
    if (!NT_SUCCESS(Bus->GetPlatformInfo(Bus->Context, &Info)))
        return MAXULONG32;
    return Info.Dma64Bit ? MAXULONG64 : MAXULONG32;
}

BOOLEAN
IntcDspTopologySources(
    _In_ const ADSP_BUS_INTERFACE* Bus,
    _Out_ PADSP_TOPOLOGY_SOURCES Sources)
{
    RtlZeroMemory(Sources, sizeof(*Sources));
    /* a bus built before the member was appended reports a shorter structure: the selection goes on without the machine's strings and names */
    if (Bus->Size < RTL_SIZEOF_THROUGH_FIELD(ADSP_BUS_INTERFACE, GetTopologySources) || Bus->GetTopologySources == NULL)
        return FALSE;
    return NT_SUCCESS(Bus->GetTopologySources(Bus->Context, Sources));
}
