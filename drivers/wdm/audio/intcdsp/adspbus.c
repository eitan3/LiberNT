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

BOOLEAN
IntcDspBusStreams(
    _In_ const ADSP_BUS_INTERFACE* Bus)
{
    /* the last of the stream members: a bus that reports through it has room for all of them; and the buffer's detach, without which a stream let go under a live pin could not keep its buffer (R5-01) */
    return INTCDSP_BUS_HAS(Bus, UnregisterNotificationEvent) && INTCDSP_BUS_HAS(Bus, AllocateDmaBufferWithNotification) &&
        INTCDSP_BUS_HAS(Bus, FreeDmaBufferWithNotification) && INTCDSP_BUS_HAS(Bus, SetDmaEngineState) &&
        INTCDSP_BUS_HAS(Bus, RegisterNotificationEvent) && INTCDSP_BUS_HAS(Bus, DetachDmaBuffer) && Bus->DetachDmaBuffer != NULL;
}

BOOLEAN
IntcDspBusPowerGating(
    _In_ const ADSP_BUS_INTERFACE* Bus)
{
    /* the last member of the interface: a bus that reports through it has room for it, and one without it cannot keep a gated DSP answering */
    return INTCDSP_BUS_HAS(Bus, SetDspPowerGating);
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
