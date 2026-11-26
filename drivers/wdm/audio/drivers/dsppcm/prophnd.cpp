/*
 * PROJECT:     LiberNT Intel Smart Sound DSP endpoint driver
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Property handlers of an endpoint's topology and wave nodes: volume and mute over the audio interface (kept by intcdsp.sys at unity until the gain module is driven, plan C5) with the codec driver's settings persistence, the jack description, the channel configuration
 * COPYRIGHT:   Copyright 2026 LiberNT contributors
 */

#include "private.h"

#define NDEBUG
#include <debug.h>

/* The endpoint a topology miniport serves, with the miniport referenced for the handler (the caller releases it) */
static
CMiniportTopology*
DsppcmTopologyOf(
    IN PUNKNOWN UnknownMiniport,
    OUT PNTSTATUS Status)
{
    CMiniportTopology* Miniport = NULL;

    if (!UnknownMiniport)
    {
        *Status = STATUS_INVALID_PARAMETER;
        return NULL;
    }
    *Status = UnknownMiniport->QueryInterface(IID_IMiniportTopology, (PVOID*)&Miniport);
    if (!NT_SUCCESS(*Status))
        return NULL;
    if (Miniport->GetAdapter() == NULL || Miniport->GetAdapter()->GetEndpoint(Miniport->GetEndpointIndex()) == NULL)
    {
        Miniport->Release();
        *Status = STATUS_INVALID_PARAMETER;
        return NULL;
    }
    return Miniport;
}

static
LONG
DsppcmRequestChannel(
    IN PPCPROPERTY_REQUEST PropertyRequest)
{
    if (PropertyRequest->Instance && PropertyRequest->InstanceSize >= sizeof(LONG))
        return *(PLONG)PropertyRequest->Instance;
    return -1;
}

/*
 * The jack of an endpoint: none, the path is the DSP's own. The description
 * says so as an integrated device without a location or a connection type,
 * always connected, as an endpoint without a jack reports itself
 * (ksmedia.h KSJACK_DESCRIPTION).
 */
NTSTATUS
NTAPI
PropertyHandler_JackDescription(IN PPCPROPERTY_REQUEST PropertyRequest)
{
    const ULONG JackDescriptionSize = sizeof(KSMULTIPLE_ITEM) + sizeof(KSJACK_DESCRIPTION);
    PKSMULTIPLE_ITEM MultipleItem;
    PKSJACK_DESCRIPTION JackDescription;

    if (PropertyRequest->ValueSize == 0)
    {
        PropertyRequest->ValueSize = JackDescriptionSize;
        return STATUS_BUFFER_OVERFLOW;
    }
    if (PropertyRequest->Verb & KSPROPERTY_TYPE_BASICSUPPORT)
    {
        if (PropertyRequest->ValueSize < sizeof(ULONG))
        {
            PropertyRequest->ValueSize = sizeof(ULONG);
            return STATUS_BUFFER_TOO_SMALL;
        }
        *(PULONG)PropertyRequest->Value = KSPROPERTY_TYPE_BASICSUPPORT | KSPROPERTY_TYPE_GET;
        PropertyRequest->ValueSize = sizeof(ULONG);
        return STATUS_SUCCESS;
    }
    if (!(PropertyRequest->Verb & KSPROPERTY_TYPE_GET))
        return STATUS_NOT_SUPPORTED;
    if (PropertyRequest->ValueSize < JackDescriptionSize)
    {
        PropertyRequest->ValueSize = JackDescriptionSize;
        return STATUS_BUFFER_TOO_SMALL;
    }
    MultipleItem = (PKSMULTIPLE_ITEM)PropertyRequest->Value;
    MultipleItem->Size = JackDescriptionSize;
    MultipleItem->Count = 1;
    JackDescription = (PKSJACK_DESCRIPTION)(MultipleItem + 1);
    JackDescription->ChannelMapping = KSAUDIO_SPEAKER_STEREO;
    JackDescription->Color = 0;
    JackDescription->ConnectionType = eConnTypeUnknown;
    JackDescription->GeoLocation = eGeoLocNotApplicable;
    JackDescription->GenLocation = eGenLocOther;
    JackDescription->PortConnection = ePortConnIntegratedDevice;
    JackDescription->IsConnected = TRUE;
    PropertyRequest->ValueSize = JackDescriptionSize;
    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
PropertyHandler_Volume(IN PPCPROPERTY_REQUEST PropertyRequest)
{
    const INTELDSP_AUDIO_INTERFACE* Interface;
    const INTELDSP_ENDPOINT* Endpoint;
    CMiniportTopology* Miniport;
    CAdapterCommon* Adapter;
    ULONG Index;
    LONG Channel, Level;
    NTSTATUS Status;

    if (PropertyRequest->Node == (ULONG)-1)
        return STATUS_INVALID_PARAMETER;
    if (PropertyRequest->ValueSize < sizeof(LONG))
        return STATUS_BUFFER_TOO_SMALL;
    Miniport = DsppcmTopologyOf(PropertyRequest->MajorTarget, &Status);
    if (!Miniport)
        return Status;
    Adapter = Miniport->GetAdapter();
    Index = Miniport->GetEndpointIndex();
    Endpoint = Adapter->GetEndpoint(Index);
    Interface = Adapter->GetInterface();
    Channel = DsppcmRequestChannel(PropertyRequest);
    if (Channel > 0 || Channel < -1)
    {
        Miniport->Release();
        return STATUS_INVALID_PARAMETER;
    }
    if (PropertyRequest->Verb & KSPROPERTY_TYPE_BASICSUPPORT)
    {
        /* the codec driver's answer (prophnd.cpp): one stepped range in 1/65536 decibels, uniform over the channels */
        if (PropertyRequest->ValueSize >= sizeof(KSPROPERTY_DESCRIPTION))
        {
            PKSPROPERTY_DESCRIPTION Desc = (PKSPROPERTY_DESCRIPTION)PropertyRequest->Value;

            Desc->AccessFlags = KSPROPERTY_TYPE_BASICSUPPORT | KSPROPERTY_TYPE_GET | KSPROPERTY_TYPE_SET;
            Desc->DescriptionSize = sizeof(KSPROPERTY_DESCRIPTION) + sizeof(KSPROPERTY_MEMBERSHEADER) + sizeof(KSPROPERTY_STEPPING_LONG);
            Desc->PropTypeSet.Set = KSPROPTYPESETID_General;
            Desc->PropTypeSet.Id = VT_I4;
            Desc->PropTypeSet.Flags = 0;
            Desc->MembersListCount = 1;
            Desc->Reserved = 0;
            if (PropertyRequest->ValueSize >= Desc->DescriptionSize)
            {
                PKSPROPERTY_MEMBERSHEADER Members = (PKSPROPERTY_MEMBERSHEADER)(Desc + 1);
                PKSPROPERTY_STEPPING_LONG Range = (PKSPROPERTY_STEPPING_LONG)(Members + 1);

                Members->MembersFlags = KSPROPERTY_MEMBER_STEPPEDRANGES;
                Members->MembersSize = sizeof(KSPROPERTY_STEPPING_LONG);
                Members->MembersCount = 1;
                Members->Flags = KSPROPERTY_MEMBER_FLAG_BASICSUPPORT_UNIFORM;
                Range->SteppingDelta = Endpoint->VolumeStep;
                Range->Reserved = 0;
                Range->Bounds.SignedMinimum = Endpoint->VolumeMinimum;
                Range->Bounds.SignedMaximum = Endpoint->VolumeMaximum;
                PropertyRequest->ValueSize = Desc->DescriptionSize;
            }
            else
            {
                PropertyRequest->ValueSize = sizeof(KSPROPERTY_DESCRIPTION);
            }
        }
        else
        {
            *(PULONG)PropertyRequest->Value = KSPROPERTY_TYPE_BASICSUPPORT | KSPROPERTY_TYPE_GET | KSPROPERTY_TYPE_SET;
            PropertyRequest->ValueSize = sizeof(ULONG);
        }
        Miniport->Release();
        return STATUS_SUCCESS;
    }
    if (PropertyRequest->Verb & KSPROPERTY_TYPE_GET)
    {
        Status = Interface->GetControl(Interface->Context, Index, IntcDspControlVolume, &Level);
        if (NT_SUCCESS(Status))
        {
            *(PLONG)PropertyRequest->Value = Level;
            PropertyRequest->ValueSize = sizeof(LONG);
        }
    }
    else if (PropertyRequest->Verb & KSPROPERTY_TYPE_SET)
    {
        Level = *(PLONG)PropertyRequest->Value;
        Status = Interface->SetControl(Interface->Context, Index, IntcDspControlVolume, Level);
        if (NT_SUCCESS(Status))
            Status = Interface->GetControl(Interface->Context, Index, IntcDspControlVolume, &Level);
        /* the level the node was left at is kept in the child's own key, as the codec driver keeps the master level */
        if (NT_SUCCESS(Status))
            DSPPCM_SaveSetting(Miniport, Endpoint->Capture ? L"CaptureLevel" : L"MasterLevel", Level);
    }
    else
    {
        Status = STATUS_NOT_SUPPORTED;
    }
    Miniport->Release();
    return Status;
}

NTSTATUS
NTAPI
PropertyHandler_Mute(IN PPCPROPERTY_REQUEST PropertyRequest)
{
    const INTELDSP_AUDIO_INTERFACE* Interface;
    const INTELDSP_ENDPOINT* Endpoint;
    CMiniportTopology* Miniport;
    CAdapterCommon* Adapter;
    ULONG Index;
    LONG Channel, Mute;
    NTSTATUS Status;

    if (PropertyRequest->Node == (ULONG)-1)
        return STATUS_INVALID_PARAMETER;
    if (PropertyRequest->ValueSize < sizeof(BOOL))
        return STATUS_BUFFER_TOO_SMALL;
    Miniport = DsppcmTopologyOf(PropertyRequest->MajorTarget, &Status);
    if (!Miniport)
        return Status;
    Adapter = Miniport->GetAdapter();
    Index = Miniport->GetEndpointIndex();
    Endpoint = Adapter->GetEndpoint(Index);
    Interface = Adapter->GetInterface();
    Channel = DsppcmRequestChannel(PropertyRequest);
    if (Channel > 0 || Channel < -1)
    {
        Miniport->Release();
        return STATUS_INVALID_PARAMETER;
    }
    if (PropertyRequest->Verb & KSPROPERTY_TYPE_BASICSUPPORT)
    {
        *(PULONG)PropertyRequest->Value = KSPROPERTY_TYPE_BASICSUPPORT | KSPROPERTY_TYPE_GET | KSPROPERTY_TYPE_SET;
        PropertyRequest->ValueSize = sizeof(ULONG);
        Miniport->Release();
        return STATUS_SUCCESS;
    }
    if (PropertyRequest->Verb & KSPROPERTY_TYPE_GET)
    {
        Status = Interface->GetControl(Interface->Context, Index, IntcDspControlMute, &Mute);
        if (NT_SUCCESS(Status))
        {
            *(PBOOL)PropertyRequest->Value = Mute ? TRUE : FALSE;
            PropertyRequest->ValueSize = sizeof(BOOL);
        }
    }
    else if (PropertyRequest->Verb & KSPROPERTY_TYPE_SET)
    {
        Mute = *(PBOOL)PropertyRequest->Value ? 1 : 0;
        Status = Interface->SetControl(Interface->Context, Index, IntcDspControlMute, Mute);
        if (NT_SUCCESS(Status))
            DSPPCM_SaveSetting(Miniport, Endpoint->Capture ? L"CaptureMute" : L"MasterMute", Mute);
    }
    else
    {
        Status = STATUS_NOT_SUPPORTED;
    }
    Miniport->Release();
    return Status;
}

/* The wave miniport of a property request, referenced for the handler (the caller releases it) */
static
CMiniportWaveRT*
DsppcmWaveOf(
    IN PUNKNOWN UnknownMiniport,
    OUT PNTSTATUS Status)
{
    CMiniportWaveRT* Miniport = NULL;

    if (!UnknownMiniport)
    {
        *Status = STATUS_INVALID_PARAMETER;
        return NULL;
    }
    *Status = UnknownMiniport->QueryInterface(IID_IMiniportWaveRT, (PVOID*)&Miniport);
    if (!NT_SUCCESS(*Status))
        return NULL;
    if (Miniport->GetAdapter() == NULL || Miniport->GetAdapter()->GetEndpoint(Miniport->GetEndpointIndex()) == NULL)
    {
        Miniport->Release();
        *Status = STATUS_INVALID_PARAMETER;
        return NULL;
    }
    return Miniport;
}

/* The speakers an endpoint's channel count places: mono, stereo, quad, 5.1 or 7.1 (ksmedia.h KSAUDIO_SPEAKER_*) */
static
DWORD
DsppcmSpeakerPositions(
    ULONG Channels)
{
    switch (Channels)
    {
        case 1:
            return KSAUDIO_SPEAKER_MONO;
        case 4:
            return KSAUDIO_SPEAKER_QUAD;
        case 6:
            return KSAUDIO_SPEAKER_5POINT1;
        case 8:
            return KSAUDIO_SPEAKER_7POINT1;
        default:
            return KSAUDIO_SPEAKER_STEREO;
    }
}

/*
 * The channel configuration of an endpoint is its topology's: read from the
 * endpoint's first format; a set is answered and changes nothing, as the
 * converter's channels are the PCM's.
 */
NTSTATUS
NTAPI
PropertyHandler_ChannelConfig(IN PPCPROPERTY_REQUEST PropertyRequest)
{
    const INTELDSP_ENDPOINT* Endpoint;
    CMiniportWaveRT* Miniport;
    NTSTATUS Status;

    if (PropertyRequest->Node == (ULONG)-1)
        return STATUS_INVALID_PARAMETER;
    if (PropertyRequest->ValueSize < sizeof(KSAUDIO_CHANNEL_CONFIG))
        return STATUS_BUFFER_TOO_SMALL;
    Miniport = DsppcmWaveOf(PropertyRequest->MajorTarget, &Status);
    if (!Miniport)
        return Status;
    Endpoint = Miniport->GetAdapter()->GetEndpoint(Miniport->GetEndpointIndex());
    ((PKSAUDIO_CHANNEL_CONFIG)PropertyRequest->Value)->ActiveSpeakerPositions =
        DsppcmSpeakerPositions(Endpoint->FormatCount != 0 ? Endpoint->Formats[0].Channels : 2);
    PropertyRequest->ValueSize = sizeof(KSAUDIO_CHANNEL_CONFIG);
    Miniport->Release();
    return STATUS_SUCCESS;
}

/* The geometry of the same speakers: their count */
NTSTATUS
NTAPI
PropertyHandler_SpeakerGeometry(IN PPCPROPERTY_REQUEST PropertyRequest)
{
    const INTELDSP_ENDPOINT* Endpoint;
    CMiniportWaveRT* Miniport;
    NTSTATUS Status;

    if (PropertyRequest->Node == (ULONG)-1)
        return STATUS_INVALID_PARAMETER;
    if (PropertyRequest->ValueSize < sizeof(LONG))
        return STATUS_BUFFER_TOO_SMALL;
    if (!(PropertyRequest->Verb & KSPROPERTY_TYPE_GET))
        return STATUS_NOT_SUPPORTED;
    Miniport = DsppcmWaveOf(PropertyRequest->MajorTarget, &Status);
    if (!Miniport)
        return Status;
    Endpoint = Miniport->GetAdapter()->GetEndpoint(Miniport->GetEndpointIndex());
    *(PLONG)PropertyRequest->Value = Endpoint->FormatCount != 0 ? (LONG)Endpoint->Formats[0].Channels : 2;
    PropertyRequest->ValueSize = sizeof(LONG);
    Miniport->Release();
    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
EventHandler_Volume(IN PPCEVENT_REQUEST EventRequest)
{
    UNREFERENCED_PARAMETER(EventRequest);
    return STATUS_NOT_IMPLEMENTED;
}

/*
 * The settings of a child: a Settings key under the child's own device key
 * (DeviceRegistryKey), one child being one PCM; opened through the topology
 * port as the codec driver opens its driver key (prophnd.cpp HdaOpenSettingsKey).
 */
static
NTSTATUS
DsppcmOpenSettingsKey(
    IN CMiniportTopology* Miniport,
    OUT PREGISTRYKEY* SettingsKey)
{
    UNICODE_STRING Name = RTL_CONSTANT_STRING(L"Settings");
    PPORTTOPOLOGY Port = Miniport->GetPort();
    PREGISTRYKEY DeviceKey;
    NTSTATUS Status;

    if (!Port)
        return STATUS_DEVICE_NOT_READY;
    Status = Port->NewRegistryKey(&DeviceKey, NULL, DeviceRegistryKey, KEY_ALL_ACCESS, NULL, 0, NULL);
    if (!NT_SUCCESS(Status))
        return Status;
    Status = DeviceKey->NewSubKey(SettingsKey, NULL, KEY_ALL_ACCESS, &Name, REG_OPTION_NON_VOLATILE, NULL);
    DeviceKey->Release();
    return Status;
}

VOID
DSPPCM_SaveSetting(
    IN CMiniportTopology* Miniport,
    IN PCWSTR ValueName,
    IN LONG Value)
{
    UNICODE_STRING Name;
    PREGISTRYKEY Key;

    if (!NT_SUCCESS(DsppcmOpenSettingsKey(Miniport, &Key)))
        return;
    RtlInitUnicodeString(&Name, ValueName);
    Key->SetValueKey(&Name, REG_DWORD, &Value, sizeof(Value));
    Key->Release();
}

static
BOOLEAN
DsppcmQuerySetting(
    IN PREGISTRYKEY Key,
    IN PCWSTR ValueName,
    OUT PLONG Value)
{
    UCHAR Buffer[sizeof(KEY_VALUE_PARTIAL_INFORMATION) + sizeof(LONG)];
    PKEY_VALUE_PARTIAL_INFORMATION Info = (PKEY_VALUE_PARTIAL_INFORMATION)Buffer;
    UNICODE_STRING Name;
    ULONG Length;

    RtlInitUnicodeString(&Name, ValueName);
    if (!NT_SUCCESS(Key->QueryValueKey(&Name, KeyValuePartialInformation, Info, sizeof(Buffer), &Length)) ||
        Info->Type != REG_DWORD || Info->DataLength != sizeof(LONG))
        return FALSE;
    *Value = *(PLONG)Info->Data;
    return TRUE;
}

/* The levels a topology's nodes were last left at are put back on the endpoint, as the codec driver restores its master settings at Init */
VOID
DSPPCM_RestoreSettings(
    IN CMiniportTopology* Miniport)
{
    const INTELDSP_AUDIO_INTERFACE* Interface = Miniport->GetAdapter()->GetInterface();
    const INTELDSP_ENDPOINT* Endpoint = Miniport->GetAdapter()->GetEndpoint(Miniport->GetEndpointIndex());
    ULONG Index = Miniport->GetEndpointIndex();
    PREGISTRYKEY Key;
    LONG Level = 0, Mute = 0;
    BOOLEAN HaveLevel, HaveMute;
    BOOLEAN Capture;

    if (!Endpoint)
        return;
    Capture = Endpoint->Capture;
    if (!NT_SUCCESS(DsppcmOpenSettingsKey(Miniport, &Key)))
        return;
    HaveLevel = DsppcmQuerySetting(Key, Capture ? L"CaptureLevel" : L"MasterLevel", &Level);
    HaveMute = DsppcmQuerySetting(Key, Capture ? L"CaptureMute" : L"MasterMute", &Mute);
    Key->Release();
    if (HaveLevel)
        Interface->SetControl(Interface->Context, Index, IntcDspControlVolume, Level);
    if (HaveMute)
        Interface->SetControl(Interface->Context, Index, IntcDspControlMute, Mute);
}
