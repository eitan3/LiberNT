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

/*
 * The last jack description told of an endpoint (its node and its pin). The
 * line comes once for an endpoint and then only when the answer changes: the
 * jack watcher asks again every 30 s, and every enumeration does.
 */
#define HDAUDIO_JACK_ANSWERS_TOLD 16

typedef struct
{
    PVOID Node;
    ULONG PinId;
    BOOL IsConnected;
    ULONG Decider;
    PCSTR Why;
    ULONG Sense;
}JACK_ANSWER_TOLD, *PJACK_ANSWER_TOLD;

static JACK_ANSWER_TOLD JackAnswersTold[HDAUDIO_JACK_ANSWERS_TOLD];
static volatile LONG JackAnswersLock = 0;

static
BOOLEAN
SameJackReason(
    IN PCSTR Left,
    IN PCSTR Right)
{
    while (*Left && *Left == *Right)
    {
        Left++;
        Right++;
    }
    return *Left == *Right;
}

static
BOOLEAN
JackAnswerChanged(
    IN PVOID Node,
    IN ULONG PinId,
    IN BOOL IsConnected,
    IN ULONG Decider,
    IN PCSTR Why,
    IN ULONG Sense)
{
    PJACK_ANSWER_TOLD Told;
    ULONG Index, Free = MAXULONG;
    BOOLEAN Changed = TRUE;

    /* another thread is in the table: the answer is told rather than kept back */
    if (InterlockedCompareExchange(&JackAnswersLock, 1, 0) != 0)
        return TRUE;

    for (Index = 0; Index < RTL_NUMBER_OF(JackAnswersTold); Index++)
    {
        Told = &JackAnswersTold[Index];
        if (Told->Node == Node && Told->PinId == PinId)
        {
            Changed = Told->IsConnected != IsConnected || Told->Decider != Decider ||
                      Told->Sense != Sense || !SameJackReason(Told->Why, Why);
            break;
        }
        if (Free == MAXULONG && !Told->Node)
            Free = Index;
    }

    if (Index == RTL_NUMBER_OF(JackAnswersTold))
    {
        /* an endpoint the table has no room for: told every time */
        if (Free == MAXULONG)
        {
            InterlockedExchange(&JackAnswersLock, 0);
            return TRUE;
        }
        Told = &JackAnswersTold[Free];
    }

    Told->Node = Node;
    Told->PinId = PinId;
    Told->IsConnected = IsConnected;
    Told->Decider = Decider;
    Told->Why = Why;
    Told->Sense = Sense;

    InterlockedExchange(&JackAnswersLock, 0);
    return Changed;
}

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

    /*
     * The answer is read live: a pin that is a jack with presence detect and
     * nothing plugged leaves the endpoint not connected, and any other pin, or
     * a plugged one, connects it. The pin that decided and why are told (LEG-1).
     */
    BOOL IsConnected = (Miniport->GetPinCount() == 0);
    ULONG Decider = (ULONG)-1;
    ULONG DeciderSense = 0;
    PCSTR Why = "no pin";

    for (ULONG Index = 0; Index < Miniport->GetPinCount() && !IsConnected; Index++)
    {
        PIN_CONFIGURATION_DEFAULT PinConfig;
        PIN_CAPABILITIES PinCaps;
        ULONG RawSense = 0;
        BOOLEAN Connected = TRUE;
        ULONG PinId = Miniport->GetPinId(Index);

        if (!NT_SUCCESS(Node->GetPinConfigurationDefault(PinId, &PinConfig)))
        {
            Decider = PinId;
            Why = "configuration unreadable";
            break;
        }
        if (PinConfig.PortConnectivity == 0x1)
            continue;

        Decider = PinId;
        if (!NT_SUCCESS(Node->GetPinCapabilities(PinId, &PinCaps)))
            Why = "capabilities unreadable";
        else if (PinConfig.PortConnectivity == 0x2)
            Why = "fixed device";
        else if (PinConfig.Misc & 0x1)
            Why = "jack detect override";
        else if (!PinCaps.PresenceDetectCapable)
            Why = "no presence detect";
        else if (!NT_SUCCESS(Node->GetPinSenseRaw(PinId, &RawSense)))
            Why = "pin sense unreadable";
        else
        {
            Connected = ((RawSense >> 31) & 0x1) != 0;
            Why = Connected ? "plugged" : "not plugged";
        }

        DeciderSense = RawSense;
        IsConnected = Connected;
    }

    if (JackAnswerChanged(Node, Miniport->GetPinCount() ? Miniport->GetPinId(0) : MAXULONG,
                          IsConnected, Decider, Why, DeciderSense))
    {
        DPRINT1("HDAUDIO: jack description of a topology of %u pin(s): IsConnected %u by pin %u (%s, pin sense raw %08x)\n",
                Miniport->GetPinCount(), IsConnected, Decider, Why, DeciderSense);
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

/*
 * The jack change event of a topology or of a wave filter (LEG-1): enabled,
 * its entry goes to that port's event list, where the adapter's telling of a
 * change finds it, and the list's disable takes it from. Its removal and its
 * support need nothing here.
 */
NTSTATUS
NTAPI
EventHandler_JackChange(IN PPCEVENT_REQUEST EventRequest)
{
    if (!EventRequest || !EventRequest->MajorTarget)
        return STATUS_INVALID_PARAMETER;

    switch (EventRequest->Verb)
    {
        case PCEVENT_VERB_ADD:
        {
            PUNKNOWN Miniport = (PUNKNOWN)EventRequest->MajorTarget;
            CMiniportTopology *Topology = NULL;
            CMiniportWaveRT *Wave = NULL;

            if (!EventRequest->EventEntry)
                return STATUS_INVALID_PARAMETER;
            if (NT_SUCCESS(Miniport->QueryInterface(IID_IMiniportTopology, (PVOID *)&Topology)) && Topology)
            {
                Topology->AddJackEvent(EventRequest->EventEntry);
                Topology->Release();
            }
            else if (NT_SUCCESS(Miniport->QueryInterface(IID_IMiniportWaveRT, (PVOID *)&Wave)) && Wave)
            {
                Wave->AddJackEvent(EventRequest->EventEntry);
                Wave->Release();
            }
            else
                return STATUS_INVALID_PARAMETER;
            return STATUS_SUCCESS;
        }

        case PCEVENT_VERB_REMOVE:
        case PCEVENT_VERB_SUPPORT:
            return STATUS_SUCCESS;

        default:
            return STATUS_INVALID_PARAMETER;
    }
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
 * The input amplifiers of the capture path of a capture topology: the
 * Audio Input converters (widget type 0x01, HDA 7.3.4.1) whose connection,
 * directly or through a selector or a mixer, reaches the topology's jack
 * pin, at the connection that does (capture_amps.c HdaCaptureAmpsSelect, the
 * driver's own rule over the group's widgets): the input amp of a converter
 * at that connection carries the capture gain and mute (R1-10). The set is
 * found once at the miniport's initialization (R1-12), so the walk is never
 * a request's. The caller frees Nids and Indices with TAG_HDAUDIO.
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
    PHDA_WIDGET Widgets = NULL;
    PHDA_CAPTURE_AMP Selected = NULL;
    PULONG FoundNids = NULL, FoundIndices = NULL;
    ULONG WidgetCount, SelectedCount, Found = 0, I;
    AMPLIFIER_CAPABILITIES Probe;
    NTSTATUS Status;

    if (!Miniport->GetPinCount())
        return STATUS_NOT_SUPPORTED;
    Widgets = (PHDA_WIDGET)ExAllocatePoolZero(NonPagedPool, sizeof(HDA_WIDGET) * HDA_WIDGETS_MAX, TAG_HDAUDIO);
    Selected = (PHDA_CAPTURE_AMP)ExAllocatePoolZero(NonPagedPool, sizeof(HDA_CAPTURE_AMP) * HDA_WIDGETS_MAX, TAG_HDAUDIO);
    if (!Widgets || !Selected)
    {
        Status = STATUS_INSUFFICIENT_RESOURCES;
        goto Done;
    }
    WidgetCount = Node->GetWidgets(Widgets, HDA_WIDGETS_MAX);
    SelectedCount = HdaCaptureAmpsSelect(Widgets, WidgetCount, (UCHAR)Miniport->GetPinId(0), Selected, HDA_WIDGETS_MAX);
    if (!SelectedCount)
    {
        Status = STATUS_NOT_SUPPORTED;
        goto Done;
    }
    FoundNids = (PULONG)ExAllocatePoolZero(NonPagedPool, sizeof(ULONG) * SelectedCount, TAG_HDAUDIO);
    FoundIndices = (PULONG)ExAllocatePoolZero(NonPagedPool, sizeof(ULONG) * SelectedCount, TAG_HDAUDIO);
    if (!FoundNids || !FoundIndices)
    {
        Status = STATUS_INSUFFICIENT_RESOURCES;
        goto Done;
    }
    /* a converter without an input amplifier at that connection makes no capture gain */
    for (I = 0; I < SelectedCount; I++)
    {
        if (!NT_SUCCESS(Node->GetAmplifierDetails(Selected[I].Converter, 1, &Probe)))
            continue;
        if (!Found)
            *Caps = Probe;
        FoundNids[Found] = Selected[I].Converter;
        FoundIndices[Found] = Selected[I].Connection;
        Found++;
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
    if (Selected)
        ExFreePoolWithTag(Selected, TAG_HDAUDIO);
    if (Widgets)
        ExFreePoolWithTag(Widgets, TAG_HDAUDIO);
    return Status;
}

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
HDAUDIO_InitAmplifiers(
    IN CMiniportTopology *Miniport)
{
    CFunctionGroupNode *Node = Miniport->GetNode();
    HDA_AMP_SET Set;

    RtlZeroMemory(&Set, sizeof(Set));
    /* the amplifiers of the topology's volume and mute are the codec's for its life: found once here, kept in the miniport, and no request walks the widgets, asks the codec for a pin's capabilities or allocates (R1-12) */
    HdaFreeAmpSet(Miniport->GetAmps());
    if (!Node || !NT_SUCCESS(HdaGetTopologyAmps(Miniport, Node, &Set)))
    {
        HdaFreeAmpSet(&Set);
        return;
    }
    *Miniport->GetAmps() = Set;
}

/*
 * The settings a topology's volume and mute saved, restored into the
 * codec's amplifiers at the topology's start: the master's for a render
 * topology, the capture's for a capture one (R1-11), through the set the
 * initialization found.
 */
VOID
HDAUDIO_RestoreMasterSettings(
    IN CMiniportTopology *Miniport)
{
    CFunctionGroupNode *Node = Miniport->GetNode();
    PHDA_AMP_SET Amps = Miniport->GetAmps();
    PREGISTRYKEY Key;
    ULONG Index, Right, Level = 0, Mute = 0;
    BOOLEAN HaveLevel, HaveMute;

    if (!Node || !Amps->Count || !NT_SUCCESS(HdaOpenSettingsKey(Miniport, &Key)))
        return;

    HaveLevel = HdaQuerySetting(Key, Amps->Input ? L"CaptureLevel" : L"MasterLevel", &Level);
    HaveMute = HdaQuerySetting(Key, Amps->Input ? L"CaptureMute" : L"MasterMute", &Mute);
    Key->Release();

    for (Index = 0; Index < Amps->Count; Index++)
    {
        for (Right = 0; Right < 2; Right++)
        {
            UCHAR CurrentMute, Gain;

            if (!NT_SUCCESS(HdaGetAmp(Node, Amps, Index, Right, &CurrentMute, &Gain)))
                continue;
            if (HaveLevel && Amps->Caps.NumSteps)
                Gain = HdaGainFromLevel((LONG)Level, &Amps->Caps);
            if (Amps->Caps.MuteCapable)
                CurrentMute = (HaveMute && Mute) ? 1 : 0;
            HdaSetAmp(Node, Amps, Index, Right, CurrentMute, Gain);
        }
    }
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

    /* the amplifiers the initialization found (HDAUDIO_InitAmplifiers): no walk, no codec verb but the gain's own, no allocation (R1-12) */
    PHDA_AMP_SET Amps = Miniport->GetAmps();
    if (!Amps->Count || !Amps->Caps.NumSteps)
    {
        Miniport->Release();
        return STATUS_NOT_SUPPORTED;
    }

    PAMPLIFIER_CAPABILITIES Caps = &Amps->Caps;
    LONG Step = ((LONG)Caps->Steps + 1) * 0x4000;
    LONG Channel = HdaGetRequestChannel(PropertyRequest);

    if (Channel > 0 || Channel < -1)
    {
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

        Status = HdaGetAmp(Node, Amps, 0, 0, &Mute, &Gain);
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
        for (ULONG Index = 0; Index < Amps->Count; Index++)
        {
            for (ULONG Right = 0; Right < 2; Right++)
            {
                UCHAR Mute, Current;

                if (!NT_SUCCESS(HdaGetAmp(Node, Amps, Index, Right, &Mute, &Current)))
                    Mute = 0;
                Status = HdaSetAmp(Node, Amps, Index, Right, Mute, Gain);
            }
        }
        if (NT_SUCCESS(Status))
            HdaSaveSetting(Miniport, Amps->Input ? L"CaptureLevel" : L"MasterLevel",
                           (ULONG)(((LONG)Gain - (LONG)Caps->Offset) * Step));
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

    /* the amplifiers the initialization found (HDAUDIO_InitAmplifiers): no walk, no codec verb but the mute's own, no allocation (R1-12) */
    PHDA_AMP_SET Amps = Miniport->GetAmps();
    if (!Amps->Count || !Amps->Caps.MuteCapable)
    {
        Miniport->Release();
        return STATUS_NOT_SUPPORTED;
    }

    PAMPLIFIER_CAPABILITIES Caps = &Amps->Caps;
    LONG Channel = HdaGetRequestChannel(PropertyRequest);

    if (Channel > 0 || Channel < -1)
    {
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

        Status = HdaGetAmp(Node, Amps, 0, 0, &Mute, &Gain);
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
        for (ULONG Index = 0; Index < Amps->Count; Index++)
        {
            for (ULONG Right = 0; Right < 2; Right++)
            {
                UCHAR Current, Gain;

                if (!NT_SUCCESS(HdaGetAmp(Node, Amps, Index, Right, &Current, &Gain)))
                    Gain = Caps->Offset;
                Status = HdaSetAmp(Node, Amps, Index, Right, Mute, Gain);
            }
        }
        if (NT_SUCCESS(Status))
            HdaSaveSetting(Miniport, Amps->Input ? L"CaptureMute" : L"MasterMute", Mute);
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
EventHandler_Volume(IN PPCEVENT_REQUEST EventRequest)
{
    UNIMPLEMENTED;
    return STATUS_NOT_IMPLEMENTED;
}
