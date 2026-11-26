/*
 * PROJECT:     LiberNT Intel Smart Sound DSP endpoint driver
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     WaveRT miniport of an endpoint
 * COPYRIGHT:   Copyright 2026 LiberNT contributors
 */

#include "private.h"

#define NDEBUG
#include <debug.h>

NTSTATUS
NTAPI
CMiniportWaveRT::QueryInterface(IN REFIID refiid, OUT PVOID *Output)
{
    if (IsEqualGUIDAligned(refiid, IID_IMiniport) ||
        IsEqualGUIDAligned(refiid, IID_IUnknown) ||
        IsEqualGUIDAligned(refiid, IID_IMiniportWaveRT))
    {
        *Output = PVOID(PMINIPORTWAVERT(this));
        PUNKNOWN(*Output)->AddRef();
        return STATUS_SUCCESS;
    }
    return STATUS_UNSUCCESSFUL;
}

NTSTATUS
NTAPI
CMiniportWaveRT::GetDescription(OUT PPCFILTER_DESCRIPTOR* Description)
{
    *Description = m_FilterDescription;
    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
CMiniportWaveRT::DataRangeIntersection(
    IN ULONG PinId,
    IN PKSDATARANGE DataRange,
    IN PKSDATARANGE MatchingDataRange,
    IN ULONG OutputBufferLength,
    OUT PVOID ResultantFormat OPTIONAL,
    OUT PULONG ResultantFormatLength)
{
    const INTELDSP_ENDPOINT* Endpoint = m_Adapter->GetEndpoint(m_EndpointIndex);
    ULONG Index;

    /* the codec driver's shape (miniport.cpp), with the endpoint's formats for the converter nodes' */
    if (!ResultantFormatLength || !Endpoint || PinId != DSPPCM_WAVE_STREAM_PIN ||
        !DataRange || !MatchingDataRange ||
        MatchingDataRange->FormatSize < sizeof(KSDATARANGE_AUDIO))
    {
        return STATUS_INVALID_PARAMETER;
    }
    if (!IsEqualGUIDAligned(DataRange->MajorFormat, MatchingDataRange->MajorFormat) ||
        !IsEqualGUIDAligned(DataRange->SubFormat, MatchingDataRange->SubFormat) ||
        !IsEqualGUIDAligned(DataRange->Specifier, MatchingDataRange->Specifier))
    {
        return STATUS_NO_MATCH;
    }
    /* the proposed format is a data format the endpoint must carry: a packed 24-bit stream is refused here as the codec refuses it */
    if (!NT_SUCCESS(DsppcmPinFormatFind(Endpoint, (PKSDATAFORMAT)DataRange, &Index)))
        return STATUS_NO_MATCH;
    *ResultantFormatLength = DataRange->FormatSize;
    if (!OutputBufferLength || !ResultantFormat)
        return STATUS_BUFFER_OVERFLOW;
    if (OutputBufferLength < DataRange->FormatSize)
        return STATUS_BUFFER_TOO_SMALL;
    RtlCopyMemory(ResultantFormat, DataRange, DataRange->FormatSize);
    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
CMiniportWaveRT::Init(
    IN PUNKNOWN UnknownAdapter,
    IN PRESOURCELIST ResourceList,
    IN PPORTWAVERT Port)
{
    UNREFERENCED_PARAMETER(UnknownAdapter);
    UNREFERENCED_PARAMETER(ResourceList);
    m_Port = Port;
    m_Port->AddRef();
    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
CMiniportWaveRT::NewStream(
    OUT PMINIPORTWAVERTSTREAM* Stream,
    IN PPORTWAVERTSTREAM PortStream,
    IN ULONG Pin,
    IN BOOLEAN Capture,
    IN PKSDATAFORMAT DataFormat)
{
    UNREFERENCED_PARAMETER(PortStream);
    return DSPPCM_AllocateStream(Stream, this, Pin, Capture, DataFormat);
}

NTSTATUS
NTAPI
CMiniportWaveRT::GetDeviceDescription(
    OUT PDEVICE_DESCRIPTION DeviceDescription)
{
    UNREFERENCED_PARAMETER(DeviceDescription);
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS
DSPPCM_NewMiniportWaveRT(
    OUT PMINIPORTWAVERT* OutMiniport,
    IN ULONG EndpointIndex,
    IN CAdapterCommon* Adapter,
    IN PPCFILTER_DESCRIPTOR FilterDescription)
{
    CMiniportWaveRT *This;

    This = new (NonPagedPool, TAG_DSPPCM) CMiniportWaveRT(NULL, EndpointIndex, Adapter, FilterDescription);
    if (!This)
        return STATUS_INSUFFICIENT_RESOURCES;
    This->AddRef();
    *OutMiniport = This;
    return STATUS_SUCCESS;
}
