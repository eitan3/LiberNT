/*
 * PROJECT:     LiberNT Intel Smart Sound DSP endpoint driver
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Topology miniport of an endpoint
 * COPYRIGHT:   Copyright 2026 LiberNT contributors
 */

#include "private.h"

#define NDEBUG
#include <debug.h>

NTSTATUS
NTAPI
CMiniportTopology::QueryInterface(IN REFIID refiid, OUT PVOID *Output)
{
    if (IsEqualGUIDAligned(refiid, IID_IMiniport) ||
        IsEqualGUIDAligned(refiid, IID_IUnknown) ||
        IsEqualGUIDAligned(refiid, IID_IMiniportTopology))
    {
        *Output = PVOID(PMINIPORTTOPOLOGY(this));
        PUNKNOWN(*Output)->AddRef();
        return STATUS_SUCCESS;
    }
    return STATUS_UNSUCCESSFUL;
}

NTSTATUS
NTAPI
CMiniportTopology::GetDescription(OUT PPCFILTER_DESCRIPTOR* Description)
{
    *Description = m_FilterDescription;
    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
CMiniportTopology::DataRangeIntersection(
    IN ULONG PinId,
    IN PKSDATARANGE DataRange,
    IN PKSDATARANGE MatchingDataRange,
    IN ULONG OutputBufferLength,
    OUT PVOID ResultantFormat OPTIONAL,
    OUT PULONG ResultantFormatLength)
{
    UNREFERENCED_PARAMETER(PinId);
    UNREFERENCED_PARAMETER(DataRange);
    UNREFERENCED_PARAMETER(MatchingDataRange);
    UNREFERENCED_PARAMETER(OutputBufferLength);
    UNREFERENCED_PARAMETER(ResultantFormat);
    UNREFERENCED_PARAMETER(ResultantFormatLength);
    /* the codec driver's answer (topology.cpp): a bridge pin carries no format to intersect */
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS
NTAPI
CMiniportTopology::Init(
    IN PUNKNOWN UnknownAdapter,
    IN PRESOURCELIST ResourceList,
    IN PPORTTOPOLOGY Port)
{
    UNREFERENCED_PARAMETER(UnknownAdapter);
    UNREFERENCED_PARAMETER(ResourceList);
    m_Port = Port;
    m_Port->AddRef();
    /* the levels the endpoints' nodes were last left at, as the codec driver restores its master settings */
    DSPPCM_RestoreSettings(this);
    return STATUS_SUCCESS;
}

NTSTATUS
DSPPCM_NewMiniportTopology(
    OUT PMINIPORTTOPOLOGY* OutMiniport,
    IN ULONG EndpointIndex,
    IN CAdapterCommon* Adapter,
    IN PPCFILTER_DESCRIPTOR FilterDescription)
{
    CMiniportTopology *This;

    This = new (NonPagedPool, TAG_DSPPCM) CMiniportTopology(NULL, EndpointIndex, Adapter, FilterDescription);
    if (!This)
        return STATUS_INSUFFICIENT_RESOURCES;
    This->AddRef();
    *OutMiniport = This;
    return STATUS_SUCCESS;
}
