/*
 * PROJECT:         ReactOS HDAudio Driver
 * LICENSE:         GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:         Property handlers
 * COPYRIGHT:       Copyright 2025-2026 Oleg Dubinskiy <oleg.dubinskiy@reactos.org>
 */

#include "private.h"

#define NDEBUG
#include <debug.h>

// FIXME: halfplemented

NTSTATUS
NTAPI
PropertyHandler_JackDescription(IN PPCPROPERTY_REQUEST PropertyRequest)
{
    const ULONG JackDescriptionSize = sizeof(KSMULTIPLE_ITEM) + sizeof(KSJACK_DESCRIPTION);

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

        PULONG AccessFlags = (PULONG)PropertyRequest->Value;
        *AccessFlags = KSPROPERTY_TYPE_BASICSUPPORT | KSPROPERTY_TYPE_GET;
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

    PUNKNOWN UnknownMiniport = (PUNKNOWN)PropertyRequest->MajorTarget;
    if (!UnknownMiniport)
        return STATUS_INVALID_PARAMETER;

    CMiniportTopology *Miniport = NULL;
    NTSTATUS Status = UnknownMiniport->QueryInterface(IID_IMiniportTopology, (PVOID*)&Miniport);
    if (!NT_SUCCESS(Status) || !Miniport)
        return Status;

    CFunctionGroupNode *Node = (CFunctionGroupNode*)Miniport->GetNode();
    if (!Node)
    {
        Miniport->Release();
        return STATUS_INVALID_PARAMETER;
    }

    BOOL IsConnected = (Miniport->GetPinCount() == 0);
    for (ULONG Index = 0; Index < Miniport->GetPinCount() && !IsConnected; Index++)
    {
        PIN_CONFIGURATION_DEFAULT PinConfig;
        PIN_CAPABILITIES PinCaps;
        ULONG DevicePresent = 0;
        ULONG PinId = Miniport->GetPinId(Index);

        if (!NT_SUCCESS(Node->GetPinConfigurationDefault(PinId, &PinConfig)))
        {
            IsConnected = TRUE;
            break;
        }
        if (PinConfig.PortConnectivity == 0x1)
            continue;

        if (!NT_SUCCESS(Node->GetPinCapabilities(PinId, &PinCaps)) ||
            PinConfig.PortConnectivity == 0x2 ||
            (PinConfig.Misc & 0x1) ||
            !PinCaps.PresenceDetectCapable ||
            !NT_SUCCESS(Node->GetPinSense(PinId, &DevicePresent)) ||
            DevicePresent)
        {
            IsConnected = TRUE;
        }
    }

    PIN_CONFIGURATION_DEFAULT PinConfiguration;
    Status = Node->GetPinConfigurationDefault(Miniport->GetPinCount() ? Miniport->GetPinId(0) : Node->GetStartNodeId(),
                                              &PinConfiguration);
    if (NT_SUCCESS(Status))
    {
        PKSMULTIPLE_ITEM MultipleItem = (PKSMULTIPLE_ITEM)PropertyRequest->Value;
        MultipleItem->Size = JackDescriptionSize;
        MultipleItem->Count = 1;
        PKSJACK_DESCRIPTION JackDescription = (PKSJACK_DESCRIPTION)(MultipleItem + 1);
        JackDescription->ChannelMapping = KSAUDIO_SPEAKER_STEREO; // FIXME
        JackDescription->Color = PinConfiguration.Color;
        JackDescription->ConnectionType = (EPcxConnectionType)PinConfiguration.ConnectionType;
        JackDescription->GeoLocation = (EPcxGeoLocation)PinConfiguration.Location;
        JackDescription->GenLocation = (EPcxGenLocation)(PinConfiguration.Location << 4);
        JackDescription->PortConnection = (EPxcPortConnection)PinConfiguration.PortConnectivity;
        JackDescription->IsConnected = IsConnected;
        PropertyRequest->ValueSize = JackDescriptionSize;
    }

    Miniport->Release();
    return Status;
}

NTSTATUS
NTAPI
PropertyHandler_ChannelConfig(IN PPCPROPERTY_REQUEST PropertyRequest)
{
    if (PropertyRequest->Node == (ULONG)-1)
        return STATUS_INVALID_PARAMETER;

    if (PropertyRequest->ValueSize < sizeof(KSAUDIO_CHANNEL_CONFIG))
        return STATUS_BUFFER_TOO_SMALL;

    PUNKNOWN UnknownMiniport = (PUNKNOWN)PropertyRequest->MajorTarget;
    if (!UnknownMiniport)
        return STATUS_INVALID_PARAMETER;

    CMiniportWaveRT *Miniport = NULL;
    NTSTATUS Status = UnknownMiniport->QueryInterface(IID_IMiniportWaveRT, (PVOID*)&Miniport);
    if (!NT_SUCCESS(Status) || !Miniport)
        return Status;

    CFunctionGroupNode *Node = (CFunctionGroupNode*)Miniport->GetNode();
    if (!Node)
    {
        Miniport->Release();
        return STATUS_INVALID_PARAMETER;
    }

    PKSAUDIO_CHANNEL_CONFIG ChannelConfig = (PKSAUDIO_CHANNEL_CONFIG)PropertyRequest->Value;
    if (PropertyRequest->Verb & KSPROPERTY_TYPE_GET)
    {
        UNIMPLEMENTED;
        ChannelConfig->ActiveSpeakerPositions = KSAUDIO_SPEAKER_7POINT1;
        Miniport->Release();
        return STATUS_SUCCESS;
    }
    else if (PropertyRequest->Verb & KSPROPERTY_TYPE_SET)
    {
        UNIMPLEMENTED;
        Miniport->Release();
        return STATUS_SUCCESS;
    }
    Miniport->Release();
    return STATUS_NOT_SUPPORTED;
}

NTSTATUS
NTAPI
PropertyHandler_SpeakerGeometry(IN PPCPROPERTY_REQUEST PropertyRequest)
{
    if (PropertyRequest->Node == (ULONG)-1)
        return STATUS_INVALID_PARAMETER;

    if (PropertyRequest->ValueSize < sizeof(LONG))
        return STATUS_BUFFER_TOO_SMALL;

    if (PropertyRequest->Verb & KSPROPERTY_TYPE_GET)
    {
        UNIMPLEMENTED;
        *(PLONG)PropertyRequest->Value = -1;
        PropertyRequest->ValueSize = sizeof(LONG);
        return STATUS_SUCCESS;
    }
    else if (PropertyRequest->Verb & KSPROPERTY_TYPE_SET)
    {
        UNIMPLEMENTED;
        return STATUS_SUCCESS;
    }
    return STATUS_NOT_SUPPORTED;
}

static
NTSTATUS
HdaGetMasterAmps(
    IN CFunctionGroupNode *Node,
    OUT PULONG *Nids,
    OUT PULONG Count,
    OUT PAMPLIFIER_CAPABILITIES Caps)
{
    PULONG Nodes = NULL;
    ULONG Total = 0, Found = 0, Index;
    AMPLIFIER_CAPABILITIES Probe;
    PNODE_CONTEXT Context;
    NTSTATUS Status;

    Status = Node->GetNodesWithType(0x00, &Total, &Nodes);
    if (!NT_SUCCESS(Status))
        return Status;

    for (Index = 0; Index < Total; Index++)
    {
        Context = Node->FindNodeId(Nodes[Index]);
        if (Context && Context->Digital)
            continue;
        if (!NT_SUCCESS(Node->GetAmplifierDetails(Nodes[Index], 0, &Probe)))
            continue;
        if (!Found)
            *Caps = Probe;
        Nodes[Found++] = Nodes[Index];
    }

    if (!Found)
    {
        if (Nodes)
            ExFreePoolWithTag(Nodes, TAG_HDAUDIO);
        return STATUS_NOT_SUPPORTED;
    }

    *Nids = Nodes;
    *Count = Found;
    return STATUS_SUCCESS;
}

/*
 * The input amplifiers of the capture path of a capture topology: the input
 * converters whose connection, directly or through a selector, reaches the
 * topology's jack pin (HDA 7.3.4.1: the input amp of the converter carries the
 * capture gain and mute). The caller frees Nids and Indices with TAG_HDAUDIO.
 */
static
NTSTATUS
HdaGetCaptureAmps(
    IN CMiniportTopology *Miniport,
    IN CFunctionGroupNode *Node,
    OUT PULONG *Nids,
    OUT PULONG *Indices,
    OUT PULONG Count,
    OUT PAMPLIFIER_CAPABILITIES Caps)
{
    PULONG Adcs = NULL, Pins = NULL, FoundNids = NULL, FoundIndices = NULL;
    ULONG AdcCount = 0, PinCount = 0, Found = 0, AdcIndex, ConnectionIndex, PinId, Reachable;
    AMPLIFIER_CAPABILITIES Probe;
    PNODE_CONTEXT AdcContext;
    NTSTATUS Status;

    if (!Miniport->GetPinCount())
        return STATUS_NOT_SUPPORTED;
    PinId = Miniport->GetPinId(0);

    Status = Node->GetNodesWithType(0x02, &AdcCount, &Adcs);
    if (!NT_SUCCESS(Status))
        return Status;
    Status = Node->GetNodesWithType(0x04, &PinCount, &Pins);
    if (!NT_SUCCESS(Status))
        goto Done;

    FoundNids = (PULONG)ExAllocatePoolZero(NonPagedPool, sizeof(ULONG) * AdcCount, TAG_HDAUDIO);
    FoundIndices = (PULONG)ExAllocatePoolZero(NonPagedPool, sizeof(ULONG) * AdcCount, TAG_HDAUDIO);
    if (!FoundNids || !FoundIndices)
    {
        Status = STATUS_INSUFFICIENT_RESOURCES;
        goto Done;
    }

    for (AdcIndex = 0; AdcIndex < AdcCount; AdcIndex++)
    {
        if (!NT_SUCCESS(Node->GetAmplifierDetails(Adcs[AdcIndex], 1, &Probe)))
            continue;

        AdcContext = Node->FindNodeId(Adcs[AdcIndex]);
        if (!AdcContext)
            continue;

        /* The converter's connection that leads to the jack, directly or through a selector */
        for (ConnectionIndex = 0; ConnectionIndex < AdcContext->ConnectionCount; ConnectionIndex++)
        {
            ULONG Connection = AdcContext->Connections[ConnectionIndex];

            if (Connection == PinId)
            {
                Reachable = 1;
            }
            else
            {
                PNODE_CONTEXT Connected = Node->FindNodeId(Connection);

                if (!Connected)
                    continue;
                Status = Miniport->GetAdapter()->IsInputNodeConnectedToPin(
                    (PVOID)Node, AdcContext->Digital, Connection, PinId, PinCount, &Reachable);
                if (!NT_SUCCESS(Status))
                    goto Done;
            }
            if (!Reachable)
                continue;

            if (!Found)
                *Caps = Probe;
            FoundNids[Found] = Adcs[AdcIndex];
            FoundIndices[Found] = ConnectionIndex;
            Found++;
            break;
        }
    }

    if (!Found)
    {
        Status = STATUS_NOT_SUPPORTED;
        goto Done;
    }

    *Nids = FoundNids;
    *Indices = FoundIndices;
    *Count = Found;
    Status = STATUS_SUCCESS;
    FoundNids = NULL;
    FoundIndices = NULL;

Done:
    if (FoundIndices)
        ExFreePoolWithTag(FoundIndices, TAG_HDAUDIO);
    if (FoundNids)
        ExFreePoolWithTag(FoundNids, TAG_HDAUDIO);
    if (Pins)
        ExFreePoolWithTag(Pins, TAG_HDAUDIO);
    if (Adcs)
        ExFreePoolWithTag(Adcs, TAG_HDAUDIO);
    return Status;
}

/* The amplifiers a topology's volume and mute act on: the DACs' output amps, or the capture amps */
typedef struct _HDA_AMP_SET
{
    PULONG Nids;
    PULONG Indices;
    ULONG Count;
    AMPLIFIER_CAPABILITIES Caps;
    BOOLEAN Input;
} HDA_AMP_SET, *PHDA_AMP_SET;

static
NTSTATUS
HdaGetTopologyAmps(
    IN CMiniportTopology *Miniport,
    IN CFunctionGroupNode *Node,
    OUT PHDA_AMP_SET Set)
{
    RtlZeroMemory(Set, sizeof(*Set));

    if (Miniport->IsCaptureTopology())
    {
        Set->Input = TRUE;
        return HdaGetCaptureAmps(Miniport, Node, &Set->Nids, &Set->Indices, &Set->Count, &Set->Caps);
    }

    return HdaGetMasterAmps(Node, &Set->Nids, &Set->Count, &Set->Caps);
}

static
VOID
HdaFreeAmpSet(
    IN PHDA_AMP_SET Set)
{
    if (Set->Indices)
        ExFreePoolWithTag(Set->Indices, TAG_HDAUDIO);
    if (Set->Nids)
        ExFreePoolWithTag(Set->Nids, TAG_HDAUDIO);
    RtlZeroMemory(Set, sizeof(*Set));
}

static
NTSTATUS
HdaGetAmp(
    IN CFunctionGroupNode *Node,
    IN PHDA_AMP_SET Set,
    IN ULONG Index,
    IN ULONG Right,
    OUT PUCHAR Mute,
    OUT PUCHAR Gain)
{
    return Node->GetAmplifierGainMute(Set->Nids[Index], Set->Input, Right, Set->Input ? Set->Indices[Index] : 0, Mute, Gain);
}

static
NTSTATUS
HdaSetAmp(
    IN CFunctionGroupNode *Node,
    IN PHDA_AMP_SET Set,
    IN ULONG Index,
    IN ULONG Right,
    IN UCHAR Mute,
    IN UCHAR Gain)
{
    return Node->SetAmplifierGainMute(Set->Nids[Index], Set->Input, Right, Set->Input ? Set->Indices[Index] : 0, Mute, Gain);
}

static
LONG
HdaGetRequestChannel(IN PPCPROPERTY_REQUEST PropertyRequest)
{
    if (PropertyRequest->Instance && PropertyRequest->InstanceSize >= sizeof(LONG))
        return *(PLONG)PropertyRequest->Instance;
    return -1;
}

static
UCHAR
HdaGainFromLevel(
    IN LONG Level,
    IN PAMPLIFIER_CAPABILITIES Caps)
{
    LONG Step = ((LONG)Caps->Steps + 1) * 0x4000;
    LONG Gain = (Level >= 0 ? Level + Step / 2 : Level - Step / 2) / Step + (LONG)Caps->Offset;

    if (Gain < 0)
        Gain = 0;
    if (Gain > (LONG)Caps->NumSteps)
        Gain = Caps->NumSteps;
    return (UCHAR)Gain;
}

static
NTSTATUS
HdaOpenSettingsKey(
    IN CMiniportTopology *Miniport,
    OUT PREGISTRYKEY *SettingsKey)
{
    UNICODE_STRING Name = RTL_CONSTANT_STRING(L"Settings");
    PPORTTOPOLOGY Port = Miniport->GetPort();
    PREGISTRYKEY DriverKey;
    NTSTATUS Status;

    if (!Port)
        return STATUS_DEVICE_NOT_READY;

    Status = Port->NewRegistryKey(&DriverKey, NULL, DriverRegistryKey, KEY_ALL_ACCESS, NULL, 0, NULL);
    if (!NT_SUCCESS(Status))
        return Status;

    Status = DriverKey->NewSubKey(SettingsKey, NULL, KEY_ALL_ACCESS, &Name, REG_OPTION_NON_VOLATILE, NULL);
    DriverKey->Release();
    return Status;
}

static
VOID
HdaSaveSetting(
    IN CMiniportTopology *Miniport,
    IN PCWSTR ValueName,
    IN ULONG Value)
{
    UNICODE_STRING Name;
    PREGISTRYKEY Key;

    if (!NT_SUCCESS(HdaOpenSettingsKey(Miniport, &Key)))
        return;

    RtlInitUnicodeString(&Name, ValueName);
    Key->SetValueKey(&Name, REG_DWORD, &Value, sizeof(Value));
    Key->Release();
}

static
BOOLEAN
HdaQuerySetting(
    IN PREGISTRYKEY Key,
    IN PCWSTR ValueName,
    OUT PULONG Value)
{
    ULONG Buffer[(sizeof(KEY_VALUE_PARTIAL_INFORMATION) + 2 * sizeof(ULONG) - 1) / sizeof(ULONG)];
    PKEY_VALUE_PARTIAL_INFORMATION Info = (PKEY_VALUE_PARTIAL_INFORMATION)Buffer;
    UNICODE_STRING Name;
    ULONG Length;

    RtlInitUnicodeString(&Name, ValueName);
    if (!NT_SUCCESS(Key->QueryValueKey(&Name, KeyValuePartialInformation, Info, sizeof(Buffer), &Length)) ||
        Info->Type != REG_DWORD || Info->DataLength != sizeof(ULONG))
    {
        return FALSE;
    }

    *Value = *(PULONG)Info->Data;
    return TRUE;
}

VOID
HDAUDIO_RestoreMasterSettings(
    IN CMiniportTopology *Miniport)
{
    CFunctionGroupNode *Node = Miniport->GetNode();
    AMPLIFIER_CAPABILITIES Caps;
    PREGISTRYKEY Key;
    PULONG Nids = NULL;
    ULONG Count = 0, Index, Right, Level = 0, Mute = 0;
    BOOLEAN HaveLevel, HaveMute;

    if (!Node || !NT_SUCCESS(HdaOpenSettingsKey(Miniport, &Key)))
        return;

    HaveLevel = HdaQuerySetting(Key, L"MasterLevel", &Level);
    HaveMute = HdaQuerySetting(Key, L"MasterMute", &Mute);
    Key->Release();

    if (!NT_SUCCESS(HdaGetMasterAmps(Node, &Nids, &Count, &Caps)))
        return;

    for (Index = 0; Index < Count; Index++)
    {
        for (Right = 0; Right < 2; Right++)
        {
            UCHAR CurrentMute, Gain;

            if (!NT_SUCCESS(Node->GetAmplifierGainMute(Nids[Index], 0, Right, 0, &CurrentMute, &Gain)))
                continue;
            if (HaveLevel && Caps.NumSteps)
                Gain = HdaGainFromLevel((LONG)Level, &Caps);
            if (Caps.MuteCapable)
                CurrentMute = (HaveMute && Mute) ? 1 : 0;
            Node->SetAmplifierGainMute(Nids[Index], 0, Right, 0, CurrentMute, Gain);
        }
    }

    ExFreePoolWithTag(Nids, TAG_HDAUDIO);
}

NTSTATUS
NTAPI
PropertyHandler_Volume(IN PPCPROPERTY_REQUEST PropertyRequest)
{
    if (PropertyRequest->Node == (ULONG)-1)
        return STATUS_INVALID_PARAMETER;

    if (PropertyRequest->ValueSize < sizeof(LONG))
        return STATUS_BUFFER_TOO_SMALL;

    PUNKNOWN UnknownMiniport = (PUNKNOWN)PropertyRequest->MajorTarget;
    if (!UnknownMiniport)
        return STATUS_INVALID_PARAMETER;

    CMiniportTopology *Miniport = NULL;
    NTSTATUS Status = UnknownMiniport->QueryInterface(IID_IMiniportTopology, (PVOID*)&Miniport);
    if (!NT_SUCCESS(Status) || !Miniport)
        return Status;

    CFunctionGroupNode *Node = (CFunctionGroupNode*)Miniport->GetNode();
    if (!Node)
    {
        Miniport->Release();
        return STATUS_INVALID_PARAMETER;
    }

    HDA_AMP_SET Amps;
    Status = HdaGetTopologyAmps(Miniport, Node, &Amps);
    if (!NT_SUCCESS(Status) || !Amps.Caps.NumSteps)
    {
        HdaFreeAmpSet(&Amps);
        Miniport->Release();
        return STATUS_NOT_SUPPORTED;
    }

    PAMPLIFIER_CAPABILITIES Caps = &Amps.Caps;
    LONG Step = ((LONG)Caps->Steps + 1) * 0x4000;
    LONG Channel = HdaGetRequestChannel(PropertyRequest);

    if (Channel > 0 || Channel < -1)
    {
        HdaFreeAmpSet(&Amps);
        Miniport->Release();
        return STATUS_INVALID_PARAMETER;
    }

    if (PropertyRequest->Verb & KSPROPERTY_TYPE_BASICSUPPORT)
    {
        if (PropertyRequest->ValueSize >= sizeof(KSPROPERTY_DESCRIPTION))
        {
            PKSPROPERTY_DESCRIPTION Desc = (PKSPROPERTY_DESCRIPTION)PropertyRequest->Value;

            Desc->AccessFlags = KSPROPERTY_TYPE_BASICSUPPORT | KSPROPERTY_TYPE_GET | KSPROPERTY_TYPE_SET;
            Desc->DescriptionSize = sizeof(KSPROPERTY_DESCRIPTION) + sizeof(KSPROPERTY_MEMBERSHEADER) +
                                    sizeof(KSPROPERTY_STEPPING_LONG);
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
                Range->SteppingDelta = Step;
                Range->Reserved = 0;
                Range->Bounds.SignedMinimum = -(LONG)Caps->Offset * Step;
                Range->Bounds.SignedMaximum = ((LONG)Caps->NumSteps - (LONG)Caps->Offset) * Step;
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
        Status = STATUS_SUCCESS;
    }
    else if (PropertyRequest->Verb & KSPROPERTY_TYPE_GET)
    {
        UCHAR Mute, Gain;

        Status = HdaGetAmp(Node, &Amps, 0, 0, &Mute, &Gain);
        if (NT_SUCCESS(Status))
        {
            *(PLONG)PropertyRequest->Value = ((LONG)Gain - (LONG)Caps->Offset) * Step;
            PropertyRequest->ValueSize = sizeof(LONG);
        }
    }
    else if (PropertyRequest->Verb & KSPROPERTY_TYPE_SET)
    {
        UCHAR Gain = HdaGainFromLevel(*(PLONG)PropertyRequest->Value, Caps);

        Status = STATUS_SUCCESS;
        for (ULONG Index = 0; Index < Amps.Count; Index++)
        {
            for (ULONG Right = 0; Right < 2; Right++)
            {
                UCHAR Mute, Current;

                if (!NT_SUCCESS(HdaGetAmp(Node, &Amps, Index, Right, &Mute, &Current)))
                    Mute = 0;
                Status = HdaSetAmp(Node, &Amps, Index, Right, Mute, Gain);
            }
        }
        if (NT_SUCCESS(Status))
            HdaSaveSetting(Miniport, Amps.Input ? L"CaptureLevel" : L"MasterLevel",
                           (ULONG)(((LONG)Gain - (LONG)Caps->Offset) * Step));
    }
    else
    {
        Status = STATUS_NOT_SUPPORTED;
    }

    HdaFreeAmpSet(&Amps);
    Miniport->Release();
    return Status;
}

NTSTATUS
NTAPI
PropertyHandler_Mute(IN PPCPROPERTY_REQUEST PropertyRequest)
{
    if (PropertyRequest->Node == (ULONG)-1)
        return STATUS_INVALID_PARAMETER;

    if (PropertyRequest->ValueSize < sizeof(BOOL))
        return STATUS_BUFFER_TOO_SMALL;

    PUNKNOWN UnknownMiniport = (PUNKNOWN)PropertyRequest->MajorTarget;
    if (!UnknownMiniport)
        return STATUS_INVALID_PARAMETER;

    CMiniportTopology *Miniport = NULL;
    NTSTATUS Status = UnknownMiniport->QueryInterface(IID_IMiniportTopology, (PVOID*)&Miniport);
    if (!NT_SUCCESS(Status) || !Miniport)
        return Status;

    CFunctionGroupNode *Node = (CFunctionGroupNode*)Miniport->GetNode();
    if (!Node)
    {
        Miniport->Release();
        return STATUS_INVALID_PARAMETER;
    }

    HDA_AMP_SET Amps;
    Status = HdaGetTopologyAmps(Miniport, Node, &Amps);
    if (!NT_SUCCESS(Status) || !Amps.Caps.MuteCapable)
    {
        HdaFreeAmpSet(&Amps);
        Miniport->Release();
        return STATUS_NOT_SUPPORTED;
    }

    PAMPLIFIER_CAPABILITIES Caps = &Amps.Caps;
    LONG Channel = HdaGetRequestChannel(PropertyRequest);

    if (Channel > 0 || Channel < -1)
    {
        HdaFreeAmpSet(&Amps);
        Miniport->Release();
        return STATUS_INVALID_PARAMETER;
    }

    if (PropertyRequest->Verb & KSPROPERTY_TYPE_BASICSUPPORT)
    {
        *(PULONG)PropertyRequest->Value = KSPROPERTY_TYPE_BASICSUPPORT | KSPROPERTY_TYPE_GET | KSPROPERTY_TYPE_SET;
        PropertyRequest->ValueSize = sizeof(ULONG);
        Status = STATUS_SUCCESS;
    }
    else if (PropertyRequest->Verb & KSPROPERTY_TYPE_GET)
    {
        UCHAR Mute, Gain;

        Status = HdaGetAmp(Node, &Amps, 0, 0, &Mute, &Gain);
        if (NT_SUCCESS(Status))
        {
            *(PBOOL)PropertyRequest->Value = Mute ? TRUE : FALSE;
            PropertyRequest->ValueSize = sizeof(BOOL);
        }
    }
    else if (PropertyRequest->Verb & KSPROPERTY_TYPE_SET)
    {
        UCHAR Mute = *(PBOOL)PropertyRequest->Value ? 1 : 0;

        Status = STATUS_SUCCESS;
        for (ULONG Index = 0; Index < Amps.Count; Index++)
        {
            for (ULONG Right = 0; Right < 2; Right++)
            {
                UCHAR Current, Gain;

                if (!NT_SUCCESS(HdaGetAmp(Node, &Amps, Index, Right, &Current, &Gain)))
                    Gain = Caps->Offset;
                Status = HdaSetAmp(Node, &Amps, Index, Right, Mute, Gain);
            }
        }
        if (NT_SUCCESS(Status))
            HdaSaveSetting(Miniport, Amps.Input ? L"CaptureMute" : L"MasterMute", Mute);
    }
    else
    {
        Status = STATUS_NOT_SUPPORTED;
    }

    HdaFreeAmpSet(&Amps);
    Miniport->Release();
    return Status;
}

NTSTATUS
NTAPI
EventHandler_Volume(IN PPCEVENT_REQUEST EventRequest)
{
    UNIMPLEMENTED;
    return STATUS_NOT_IMPLEMENTED;
}
