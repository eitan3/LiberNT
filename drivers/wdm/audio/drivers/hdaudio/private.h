/*
 * PROJECT:         ReactOS HDAudio Driver
 * LICENSE:         GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:         Internal definitions
 * COPYRIGHT:       Copyright 2025 Johannes Anderwald <johannes.anderwald@reactos.org>
 *                  Copyright 2025-2026 Oleg Dubinskiy <oleg.dubinskiy@reactos.org>
 */

#pragma once

#include <ntddk.h>
#include <ks.h>
#include <portcls.h>
#include <ksmedia.h>
#include <hdaudio.h>
#include <ddk/ntstrsafe.h>

#include "hda_verbs.h"
#include "hdaudio_format.h"
#include "tables.h"
#include "capture_amps.h"

#define TAG_HDAUDIO 'UADH'
#define HDAUDIO_MAX_FORMAT_RANGES 3
#define HDAUDIO_SUBDEVICE_HDMI_OUT 0x10

PVOID
__cdecl
operator new(size_t Size, POOL_TYPE PoolType, ULONG Tag) noexcept;

typedef struct
{
    UCHAR AudioFormatSupported32Bit : 1;
    UCHAR AudioFormatSupported24Bit : 1;
    UCHAR AudioFormatSupported20Bit : 1;
    UCHAR AudioFormatSupported16Bit : 1;
    UCHAR AudioFormatSupported8Bit : 1;
    UCHAR Supported8Khz : 1;
    UCHAR Supported11Khz : 1;
    UCHAR Supported16Khz : 1;
    UCHAR Supported22Khz : 1;
    UCHAR Supported32Khz : 1;
    UCHAR Supported44Khz : 1;
    UCHAR Supported48Khz : 1;
    UCHAR Supported88Khz : 1;
    UCHAR Supported96Khz : 1;
    UCHAR Supported176Khz : 1;
    UCHAR Supported192Khz : 1;
    UCHAR Supported384Khz : 1;
    UCHAR AC3FormatSupported : 1;
    UCHAR Float32FormatSupported : 1;
    UCHAR PCMFormatSupported : 1;
}NODE_PCM_RATES, *PNODE_PCM_RATES;

typedef struct
{
    UCHAR PortConnectivity : 2;
    UCHAR Location : 6;
    UCHAR DefaultDevice : 4;
    UCHAR ConnectionType :4 ;
    UCHAR Color : 4;
    UCHAR Misc : 4;
    UCHAR DefaultAssociation : 4;
    UCHAR Sequence : 4;

}PIN_CONFIGURATION_DEFAULT, *PPIN_CONFIGURATION_DEFAULT;

typedef struct
{
    UCHAR MuteCapable : 1;
    UCHAR Steps : 7;
    UCHAR NumSteps : 7;
    UCHAR Offset : 7;
}AMPLIFIER_CAPABILITIES, *PAMPLIFIER_CAPABILITIES;


typedef struct
{
    LIST_ENTRY ListEntry;
    UCHAR Visited;
    UCHAR Digital;
    UCHAR NodeId;
    UCHAR PinDefaultAssociation;
    UCHAR ChannelCount;
    UCHAR NodeType;
    UCHAR UnsolCapable;
    UCHAR ConnectionCount;
    USHORT Connections[1];
}NODE_CONTEXT, *PNODE_CONTEXT;

typedef struct
{
    UCHAR EAPDCapable : 1;
    UCHAR InputCapable : 1;
    UCHAR OutputCapable : 1;
    UCHAR HeadphoneDriveCapable : 1;
    UCHAR PresenceDetectCapable : 1;
    UCHAR HdmiCapable : 1;
    UCHAR DisplayPortCapable : 1;
} PIN_CAPABILITIES, *PPIN_CAPABILITIES;

class CFunctionGroupNode;

/* A pin complex with presence detect, followed by the driver while the device runs (LEG-1) */
typedef struct
{
    CFunctionGroupNode *Node;
    UCHAR CodecAddress;
    UCHAR PinId;
    UCHAR Tag;
    UCHAR Present;
    UCHAR Registered;
} HDAUDIO_JACK, *PHDAUDIO_JACK;

#define HDAUDIO_MAX_JACKS             16
#define HDAUDIO_MAX_JACK_TOPOLOGIES   8
#define HDAUDIO_UNSOLICITED_SLOTS     8

template <typename... Interfaces> class CUnknownImpl : public Interfaces...
{
  private:
    volatile LONG m_Ref;

  protected:
    CUnknownImpl() : m_Ref(0)
    {
    }
    virtual ~CUnknownImpl()
    {
    }

  public:
    STDMETHODIMP_(ULONG) AddRef()
    {
        ULONG Ref = InterlockedIncrement(&m_Ref);
        ASSERT(Ref < 0x10000);
        return Ref;
    }
    STDMETHODIMP_(ULONG) Release()
    {
        ULONG Ref = InterlockedDecrement(&m_Ref);
        ASSERT(Ref < 0x10000);
        if (!Ref)
        {
            delete this;
            return 0;
        }
        return Ref;
    }
};

class CAdapterCommon : public CUnknownImpl<IAdapterPowerManagement>
{
  public:
    STDMETHODIMP QueryInterface(REFIID InterfaceId, PVOID *Interface);
    NTSTATUS NTAPI Initialize(IN PDEVICE_OBJECT DeviceObject, IN PIRP Irp, IN PRESOURCELIST ResourceList);
    NTSTATUS NTAPI AcquireBusInterface(IN PDEVICE_OBJECT DeviceObject);
    VOID NTAPI GetResourceInformation();
    NTSTATUS NTAPI GetVendorRevisionId();
    NTSTATUS NTAPI TransferInitVerbs(IN PDEVICE_OBJECT DeviceObject);
    NTSTATUS NTAPI ReadRegistryKey(IN PDEVICE_OBJECT DeviceObject, IN LPCWSTR KeyName, IN ULONG BufferLength, OUT PREGISTRYKEY * ParentKey, OUT PULONG Type, OUT PVOID Value);
    NTSTATUS NTAPI TransferVerb(ULONG Verb, PULONG Response);
    NTSTATUS NTAPI InstallDevice(
        IN PDEVICE_OBJECT DeviceObject,
        IN PIRP Irp,
        IN LPWSTR Name,
        IN BOOLEAN bTopology,
        IN REFGUID PortClassId,
        IN PRESOURCELIST ResourceList,
        IN ULONG AssociatedPinsCount,
        IN PULONG AssociatedPinIds,
        IN PVOID FunctionGroupNode,
        IN PPCFILTER_DESCRIPTOR FilterDescription,
        OUT PUNKNOWN *OutPortUnknown);
    NTSTATUS NTAPI ChooseSubdeviceName(UCHAR DefaultDevice, UCHAR Wave, OUT LPWSTR *OutSubdeviceName);
    UCHAR NTAPI GetSubdeviceType(IN PVOID Node, IN ULONG PinNode, IN UCHAR DefaultDevice);
    NTSTATUS NTAPI
    BuildWaveOutFormat(
        IN ULONG AssociatedPinCount,
        IN PULONG AssociatedPins,
        IN PVOID Node,
        IN UCHAR Digital);
    NTSTATUS NTAPI
    BuildWaveInFormat(
        IN ULONG AssociatedPinCount,
        IN PULONG AssociatedPins,
        IN PVOID Node);
    NTSTATUS NTAPI
    BuildWaveFormat(
        IN PVOID Node,
        IN ULONG NodeCount,
        IN PULONG TargetWidgets,
        IN BOOLEAN Capture);
    NTSTATUS NTAPI
    BuildInstallFilter(
        IN PDEVICE_OBJECT DeviceObject,
        IN PIRP Irp,
        IN ULONG bOutput,
        IN PVOID OutNode,
        IN PRESOURCELIST ResourceList,
        IN ULONG AssociatedPinCount,
        IN PULONG AssociatedPins,
        IN UCHAR Digital);
    VOID NTAPI ClearRef(IN ULONG RefValue, IN ULONG NodeCount, IN PULONG Nodes);
    BOOLEAN NTAPI FindPinConverter(IN PVOID Node, IN ULONG NodeId, IN UCHAR Digital, IN ULONG Depth, IN ULONG Required, OUT PULONG Converter);
    NTSTATUS
    NTAPI
    ProcessDacNodes(
        IN PDEVICE_OBJECT DeviceObject,
        IN PIRP Irp,
        IN PRESOURCELIST ResourceList,
        IN UCHAR DefaultDevice,
        IN ULONG DacNodeCount,
        IN PULONG DacNodes,
        IN PVOID Node);
    NTSTATUS
    NTAPI
    ProcessAdcNodes(
        IN PDEVICE_OBJECT DeviceObject,
        IN PIRP Irp,
        IN PRESOURCELIST ResourceList,
        IN UCHAR DefaultDevice,
        IN ULONG AdcNodeCount,
        IN PULONG AdcNodes,
        IN PVOID Node);
    NTSTATUS
    NTAPI
    ProcessOutputNodes(
        IN PDEVICE_OBJECT DeviceObject,
        IN PIRP Irp,
        IN PRESOURCELIST ResourceList,
        IN ULONG PinNodeCount,
        IN PULONG PinNodes,
        IN PVOID Node,
        IN ULONG OutputNodeCount,
        IN PULONG OutputNodes);
    NTSTATUS
    NTAPI
    ProcessInputNodes(
        IN PDEVICE_OBJECT DeviceObject,
        IN PIRP Irp,
        IN PRESOURCELIST ResourceList,
        IN ULONG PinNodeCount,
        IN PULONG PinNodes,
        IN PVOID Node,
        IN ULONG InputNodeCount,
        IN PULONG InputNodes);

    NTSTATUS
    NTAPI
    AssociatePins(
        IN PDEVICE_OBJECT DeviceObject,
        IN PIRP Irp,
        IN PRESOURCELIST ResourceList,
        IN ULONG PinId,
        IN UCHAR DefaultAssociation,
        IN PVOID Node,
        IN ULONG PinNodeCount,
        IN PULONG PinNodes,
        IN UCHAR Digital);

    NTSTATUS
    NTAPI
    IsInputNodeConnectedToPin(
        IN PVOID Node,
        IN UCHAR Digital,
        ULONG InputNodeId,
        ULONG PinNodeId,
        ULONG PinNodeCount,
        OUT PULONG Result);

    VOID
    NTAPI
    CollectPinWithType(
        IN ULONG DeviceType,
        IN ULONG PinNodeCount,
        IN PULONG PinIds,
        IN PULONG DefaultDeviceTypeList,
        OUT PULONG ResultListCount,
        OUT PULONG ResultList);


    NTSTATUS
    NTAPI
    FindConnectedWidgets(
        IN ULONG NodeType,
        IN ULONG PinNodeId,
        IN ULONG NodeId,
        IN PVOID Node,
        IN ULONG NodeIndex,
        IN ULONG NodeCount,
        OUT PULONG OutNodeCount,
        OUT PULONG OutNodes,
        IN UCHAR Digital);

    NTSTATUS NTAPI
    GetInterface(PHDAUDIO_BUS_INTERFACE_V2 Interface);

    UCHAR
    GetCodecAddress()
    {
        return m_CodecAddress;
    }

    /* The jack pins' presence (LEG-1) */
    VOID NTAPI RegisterJackPortEvents(PPORTEVENTS PortEvents);
    BOOLEAN NTAPI RegisterJackPin(IN PVOID Node, IN ULONG PinId, IN PIN_CAPABILITIES PinCaps);
    VOID NTAPI StopJacks();
    VOID NTAPI RecordUnsolicited(HDAUDIO_CODEC_RESPONSE Response);

    IMP_IAdapterPowerManagement;
    CAdapterCommon(IUnknown* OuterUnknown) :
        Interface(), m_CodecAddress(0), m_FunctionGroupStartNode(0), RevisionId(0), VendorId(0), m_Jacks(),
        m_JackCount(0), m_JackPortEvents(), m_JackWorkItem(NULL), m_Unsolicited(), m_UnsolicitedIndex(0),
        m_UnsolicitedTold(0), m_JackPending(0), m_JackWorkActive(0), m_JackCallbacks(0), m_JackStopping(FALSE),
        m_WaveRTOutPortUnknown(NULL), m_WaveRTInPortUnknown(NULL), m_TopoOutPortUnknown(NULL),
        m_TopoInPortUnknown(NULL), m_WaveInDataRange(), m_WaveInDataRanges(), m_WaveOutDataRange(),
        m_WaveOutDataRanges(), m_ClaimedConverters(), m_ClaimedConverterCount(0), m_SubdeviceNames(),
        m_PowerState(PowerDeviceUnspecified)
    {
        RtlCopyMemory(m_WaveInPins, WaveInPins, sizeof(m_WaveInPins));
        RtlCopyMemory(m_WaveOutPins, WaveOutPins, sizeof(m_WaveOutPins));

        m_WaveInFilterDescription = WaveInFilterDescription[0];
        m_WaveInFilterDescription.Pins = m_WaveInPins;
        m_WaveInPins[0].KsPinDescriptor.DataRanges = m_WaveInDataRanges;
        m_WaveInPins[0].KsPinDescriptor.DataRangesCount = 0;

        m_WaveOutFilterDescription = WaveOutFilterDescription[0];
        m_WaveOutFilterDescription.Pins = m_WaveOutPins;
        m_WaveOutPins[0].KsPinDescriptor.DataRanges = m_WaveOutDataRanges;
        m_WaveOutPins[0].KsPinDescriptor.DataRangesCount = 0;
    }
    virtual ~CAdapterCommon()
    {
    }

    friend NTSTATUS NTAPI PropertyHandler_JackDescription(IN PPCPROPERTY_REQUEST PropertyRequest);
    friend NTSTATUS NTAPI PropertyHandler_ChannelConfig(IN PPCPROPERTY_REQUEST PropertyRequest);
    friend NTSTATUS NTAPI PropertyHandler_SpeakerGeometry(IN PPCPROPERTY_REQUEST PropertyRequest);
    friend NTSTATUS NTAPI PropertyHandler_Volume(IN PPCPROPERTY_REQUEST PropertyRequest);
    friend NTSTATUS NTAPI PropertyHandler_Mute(IN PPCPROPERTY_REQUEST PropertyRequest);
    friend NTSTATUS NTAPI EventHandler_Volume(IN PPCEVENT_REQUEST EventRequest);

  private:
    static VOID NTAPI JackWorkRoutine(PDEVICE_OBJECT DeviceObject, PVOID Context);
    VOID NTAPI ProcessJackChanges();
    friend VOID NTAPI HDAUDIO_UnsolicitedResponseCallback(HDAUDIO_CODEC_RESPONSE Param, PVOID Context);

    HDAUDIO_BUS_INTERFACE_V2 Interface;
    UCHAR m_CodecAddress;
    UCHAR m_FunctionGroupStartNode;
    UINT16 RevisionId;
    UINT16 VendorId;
    HDAUDIO_JACK m_Jacks[HDAUDIO_MAX_JACKS];
    ULONG m_JackCount;
    PPORTEVENTS m_JackPortEvents[HDAUDIO_MAX_JACK_TOPOLOGIES];
    PIO_WORKITEM m_JackWorkItem;
    HDAUDIO_CODEC_RESPONSE m_Unsolicited[HDAUDIO_UNSOLICITED_SLOTS];
    volatile LONG m_UnsolicitedIndex;
    LONG m_UnsolicitedTold;
    volatile LONG m_JackPending;
    volatile LONG m_JackWorkActive;
    volatile LONG m_JackCallbacks;
    BOOLEAN m_JackStopping;
    PUNKNOWN m_WaveRTOutPortUnknown;
    PUNKNOWN m_WaveRTInPortUnknown;
    PUNKNOWN m_TopoOutPortUnknown;
    PUNKNOWN m_TopoInPortUnknown;
    KSDATARANGE_AUDIO m_WaveInDataRange[HDAUDIO_MAX_FORMAT_RANGES];
    PKSDATARANGE m_WaveInDataRanges[HDAUDIO_MAX_FORMAT_RANGES];
    PCPIN_DESCRIPTOR m_WaveInPins[RTL_NUMBER_OF(WaveInPins)];
    PCFILTER_DESCRIPTOR m_WaveInFilterDescription;
    KSDATARANGE_AUDIO m_WaveOutDataRange[HDAUDIO_MAX_FORMAT_RANGES];
    PKSDATARANGE m_WaveOutDataRanges[HDAUDIO_MAX_FORMAT_RANGES];
    PCPIN_DESCRIPTOR m_WaveOutPins[RTL_NUMBER_OF(WaveOutPins)];
    PCFILTER_DESCRIPTOR m_WaveOutFilterDescription;
    ULONG m_ClaimedConverters[32];
    ULONG m_ClaimedConverterCount;
    UCHAR m_SubdeviceNames[2][HDAUDIO_SUBDEVICE_HDMI_OUT + 1];
    DEVICE_POWER_STATE m_PowerState;
};

class CFunctionGroupNode
{
  public:
    CFunctionGroupNode(UCHAR CodecAddress, UCHAR NodeId, CAdapterCommon *AdapterCommon)
        : m_Adapter(AdapterCommon), m_StartNodeId(NodeId), m_CodecAddress(CodecAddress), m_SubNodeCount(0)
    {
        InitializeListHead(&m_Nodes);
    }
    virtual ~CFunctionGroupNode()
    {
        while (!IsListEmpty(&m_Nodes))
        {
            PLIST_ENTRY Entry = RemoveHeadList(&m_Nodes);
            PNODE_CONTEXT NodeContext = CONTAINING_RECORD(Entry, NODE_CONTEXT, ListEntry);
            ExFreePoolWithTag(NodeContext, TAG_HDAUDIO);
        }
    }

    NTSTATUS NTAPI QueryWidgetCount();
    NTSTATUS NTAPI EnumWidgets();
    NTSTATUS NTAPI EnumConnections();
    VOID NTAPI PrintNodeInfo(UCHAR NodeType, ULONG NodeId);
    NTSTATUS NTAPI GetNodesWithType(IN UCHAR NodeType, OUT PULONG NodeCount, OUT PULONG *NodesAddress);
    /* The widgets of the group as the capture amplifier's selection sees them (capture_amps.h HDA_CAPTURE_AMP): one record each, its connection list the context's; the count written, at most Max */
    ULONG NTAPI GetWidgets(OUT PHDA_WIDGET Widgets, ULONG Max);
    NTSTATUS NTAPI GetSupportedPCMSizeRates(ULONG NodeId, OUT PNODE_PCM_RATES OutRates);
    NTSTATUS NTAPI GetPinConfigurationDefault(IN ULONG NodeId, IN PPIN_CONFIGURATION_DEFAULT PinConfiguration);
    NTSTATUS NTAPI AddNode(ULONG NodeId);
    PNODE_CONTEXT NTAPI FindNodeId(ULONG NodeId);
    NTSTATUS NTAPI GetAmplifierDetails(IN ULONG NodeId, IN ULONG Input, OUT PAMPLIFIER_CAPABILITIES Caps);
    NTSTATUS NTAPI GetAmplifierGainMute(IN ULONG NodeId, IN ULONG Input, IN ULONG Right, IN ULONG Index, OUT PUCHAR Mute, OUT PUCHAR Gain);
    NTSTATUS NTAPI SetAmplifierGainMute(IN ULONG NodeId, IN ULONG Input, IN ULONG Right, IN ULONG Index, IN UCHAR Mute, IN UCHAR Gain);
    NTSTATUS NTAPI ActivateAmplifier(IN ULONG NodeId, IN ULONG Input, IN ULONG Index);
    NTSTATUS NTAPI GetPinCapabilities(IN ULONG NodeId, OUT PPIN_CAPABILITIES PinCaps);
    NTSTATUS NTAPI GetPinSense(IN ULONG NodeId, IN PULONG DevicePresent);
    NTSTATUS NTAPI GetPinSenseRaw(IN ULONG NodeId, OUT PULONG RawSense);
    NTSTATUS NTAPI GetVolumeKnob(IN ULONG NodeId, OUT PUCHAR Direct, OUT PUCHAR Volume);
    NTSTATUS NTAPI SetVolumeKnob(IN ULONG NodeId, IN UCHAR Direct, IN UCHAR Volume);
    NTSTATUS NTAPI GetVolumeCapabilities(IN ULONG NodeId, IN PUCHAR Delta, IN PUCHAR NumSteps);
    NTSTATUS NTAPI GetPinNodesWithDefaultAssociation(IN UCHAR DefaultAssociation, IN UCHAR Digital, OUT PULONG NodeCount, OUT PULONG * Nodes);
    NTSTATUS NTAPI GetAssociatedPinNodes(IN ULONG PinId, IN UCHAR DefaultAssociation, IN UCHAR Digital, OUT PULONG NodeCount, OUT PULONG * Nodes);
    VOID NTAPI ClearVisitedState();

    NTSTATUS
    NTAPI
    GetFunctionGroupType(
        IN ULONG NodeId,
        OUT PUCHAR UnsolCapable,
        OUT PUCHAR NodeType);

    NTSTATUS
    NTAPI
    EnableUnsolicitedResponse(
        IN ULONG NodeId,
        IN UCHAR Enable,
        IN UCHAR Tag);

    NTSTATUS
    NTAPI
    SetPowerState(IN ULONG NodeId, IN UCHAR PowerState);

    NTSTATUS
    NTAPI
    SetPinWidgetControl(IN ULONG NodeId, IN UCHAR HPhnEnable, IN UCHAR OutEnable, IN UCHAR InEnable, IN UCHAR VRefEn2, IN UCHAR VRefEn1);

    NTSTATUS
    NTAPI
    SetStreamFormat(IN ULONG NodeId, IN USHORT Format);

    NTSTATUS
    NTAPI
    SetConverterStream(IN ULONG NodeId, IN UCHAR StreamId);

    NTSTATUS
    NTAPI
    EnableDigitalConverter(IN ULONG NodeId);

    NTSTATUS
    NTAPI
    SetConnectionSelect(IN ULONG NodeId, IN UCHAR Index);

    NTSTATUS
    NTAPI
    SetEAPDBTL(IN ULONG NodeId, IN UCHAR LRSwap, IN UCHAR EAPD, IN UCHAR BTL);

    NTSTATUS
    NTAPI
    SetGPIO(IN ULONG NodeId, IN UCHAR GPIOData, IN UCHAR GPIOEnableMask, IN UCHAR GPIODirection);

    UCHAR
    GetStartNodeId()
    {
        return m_StartNodeId;
    }

    UCHAR
    GetStripeBit()
    {
        return m_Stripe;
    }

    VOID
    SetStripeBit(UCHAR Stripe)
    {
        m_Stripe = Stripe;
    }

  private:
    CAdapterCommon *m_Adapter;
    BOOLEAN m_Stripe;
    UCHAR m_StartNodeId;
    UCHAR m_CodecAddress;
    ULONG m_StartSubNode;
    ULONG m_SubNodeCount;
    LIST_ENTRY m_Nodes;
};

/*
 * The amplifiers a topology's volume and mute act on (prophnd.cpp): the
 * DACs' output amps, or the Audio Input converters' input amps that make the
 * capture gain, each at the connection that reaches the jack. The set is
 * found once at the miniport's initialization (HDAUDIO_InitAmplifiers) and
 * kept until the miniport goes: no request walks the widgets, asks the codec
 * for the pin's capabilities, or allocates (R1-12).
 */
typedef struct _HDA_AMP_SET
{
    PULONG Nids;
    PULONG Indices;
    ULONG Count;
    AMPLIFIER_CAPABILITIES Caps;
    BOOLEAN Input;
} HDA_AMP_SET, *PHDA_AMP_SET;

/* A set's arrays go back to the pool and the set is empty again */
VOID
HdaFreeAmpSet(
    IN PHDA_AMP_SET Set);

class CMiniportTopology : public CUnknownImpl<IMiniportTopology>
{
  public:
    STDMETHODIMP QueryInterface(REFIID InterfaceId, PVOID *Interface);
    IMP_IMiniportTopology;
    CMiniportTopology(
        IUnknown *OuterUnknown,
        ULONG AssociatedPinCount,
        PULONG AssociatedPinIds,
        CFunctionGroupNode *Node,
        CAdapterCommon *Adapter,
        PPCFILTER_DESCRIPTOR Filter)
        : m_Port(0), m_PortEvents(NULL), m_Node(Node), m_Adapter(Adapter), m_FilterDescription(Filter), m_PinCount(0), mAmps()
    {
        for (ULONG Index = 0; Index < AssociatedPinCount && Index < ARRAYSIZE(m_PinIds); Index++)
            m_PinIds[m_PinCount++] = AssociatedPinIds[Index];
    }
    virtual ~CMiniportTopology()
    {
        HdaFreeAmpSet(&mAmps);
        if (m_PortEvents)
            m_PortEvents->Release();
    }

    CFunctionGroupNode*
    GetNode()
    {
        return m_Node;
    }

    CAdapterCommon*
    GetAdapter()
    {
        return m_Adapter;
    }

    PPORTTOPOLOGY
    GetPort()
    {
        return m_Port;
    }

    /* The jack change event a client enabled on this topology goes to the port's event list, where the adapter's telling finds it (LEG-1) */
    VOID
    AddJackEvent(PKSEVENT_ENTRY EventEntry)
    {
        if (m_PortEvents)
            m_PortEvents->AddEventToEventList(EventEntry);
    }

    ULONG
    GetPinCount()
    {
        return m_PinCount;
    }

    /* Whether this topology serves a capture jack, so its volume and mute act on the input amplifiers */
    BOOLEAN
    IsCaptureTopology()
    {
        PIN_CAPABILITIES PinCaps;

        return m_PinCount && m_Node && NT_SUCCESS(m_Node->GetPinCapabilities(m_PinIds[0], &PinCaps)) &&
               PinCaps.InputCapable;
    }

    ULONG
    GetPinId(ULONG Index)
    {
        return m_PinIds[Index];
    }

    /* The amplifiers of the topology's volume and mute, found at the initialization; an empty set when the topology has none */
    PHDA_AMP_SET
    GetAmps()
    {
        return &mAmps;
    }

  private:
    PPORTTOPOLOGY m_Port;
    PPORTEVENTS m_PortEvents;
    CFunctionGroupNode *m_Node;
    CAdapterCommon *m_Adapter;
    PPCFILTER_DESCRIPTOR m_FilterDescription;
    ULONG m_PinCount;
    ULONG m_PinIds[16];
    HDA_AMP_SET mAmps;
};

class CMiniportWaveRT : public CUnknownImpl<IMiniportWaveRT>
{
  public:
    STDMETHODIMP QueryInterface(REFIID InterfaceId, PVOID *Interface);
    IMP_IMiniportWaveRT;
    CMiniportWaveRT(
        IUnknown *OuterUnknown,
        ULONG AssociatedPinCount,
        PULONG AssociatedPinIds,
        CFunctionGroupNode *Node,
        CAdapterCommon *Adapter,
        PPCFILTER_DESCRIPTOR Filter)
        : m_Port(0), m_PortEvents(NULL), m_AssociatedPinCount(AssociatedPinCount), m_AssociatedPins(AssociatedPinIds), m_Node(Node),
          m_Adapter(Adapter), m_FilterDescription(Filter)
    {
    }
    virtual ~CMiniportWaveRT()
    {
        if (m_PortEvents)
            m_PortEvents->Release();
        if (m_AssociatedPins)
            ExFreePoolWithTag(m_AssociatedPins, TAG_HDAUDIO);
    }

    /* The jack change event a client enabled on this wave filter goes to the port's event list, where the adapter's telling finds it (LEG-1) */
    VOID
    AddJackEvent(PKSEVENT_ENTRY EventEntry)
    {
        if (m_PortEvents)
            m_PortEvents->AddEventToEventList(EventEntry);
    }

    CFunctionGroupNode*
    GetNode()
    {
        return m_Node;
    }

  private:
    PPORTWAVERT m_Port;
    PPORTEVENTS m_PortEvents;
    ULONG m_AssociatedPinCount;
    PULONG m_AssociatedPins;
    CFunctionGroupNode *m_Node;
    CAdapterCommon *m_Adapter;
    PPCFILTER_DESCRIPTOR m_FilterDescription;
};

class CMiniportWaveRTStream : public CUnknownImpl<IMiniportWaveRTStreamNotification>
{
  public:
    STDMETHODIMP
    QueryInterface(REFIID InterfaceId, PVOID *Interface);

    CMiniportWaveRTStream(IUnknown *OuterUnknown, CAdapterCommon * Adapter, CFunctionGroupNode * OutNode,
        ULONG Pin,
        BOOLEAN Capture,
        PHDAUDIO_STREAM_FORMAT StreamFormat,
        PHDAUDIO_CONVERTER_FORMAT Converter,
        PHDAUDIO_BUS_INTERFACE_V2 Interface,
        HANDLE DmaEngine,
        ULONG NodeCount,
        PULONG Nodes,
        PPCFILTER_DESCRIPTOR FilterDescription)
        : m_OutNode(OutNode), m_Capture(Capture), m_DmaEngine(DmaEngine),
          m_BufferSize(0), m_NodeCount(NodeCount), m_Nodes(Nodes)
    {
       RtlCopyMemory(&m_Interface, Interface, sizeof(HDAUDIO_BUS_INTERFACE_V2));
       RtlCopyMemory(&m_StreamFormat, StreamFormat, sizeof(HDAUDIO_STREAM_FORMAT));
       RtlCopyMemory(&m_Converter, Converter, sizeof(HDAUDIO_CONVERTER_FORMAT));
    }
    virtual ~CMiniportWaveRTStream()
    {
        if (!NT_SUCCESS(m_Interface.FreeDmaEngine(m_Interface.Context, m_DmaEngine)))
            DbgPrint("HDAUDIO: FreeDmaEngine failed\n");
    }

    IMP_IMiniportWaveRTStream;
    IMP_IMiniportWaveRTStreamNotification;

  private:
    CFunctionGroupNode *m_OutNode;
    BOOLEAN m_Capture;
    HDAUDIO_STREAM_FORMAT m_StreamFormat;
    HDAUDIO_CONVERTER_FORMAT m_Converter;
    HANDLE m_DmaEngine;
    HDAUDIO_BUS_INTERFACE_V2 m_Interface;
    UCHAR m_StreamId;
    ULONG m_FifoSize;
    ULONG m_BufferSize;
    ULONG m_NodeCount;
    PULONG m_Nodes;
};

NTSTATUS
HDAUDIO_AllocateCommonAdapter(
    OUT CAdapterCommon ** OutAdapter);

NTSTATUS
HDAUDIO_InitializeCommonAdapter(
    CAdapterCommon* CommonAdapter,
    IN PDEVICE_OBJECT DeviceObject,
    IN PIRP Irp,
    IN PRESOURCELIST ResourceList
);

typedef struct
{
    ULONG_PTR Reserved[4]; // used by portcls
    CAdapterCommon *AdapterCommon;
}HDAUDIO_DEVICE_EXTENSION, *PHDAUDIO_DEVICE_EXTENSION;

NTSTATUS
HDAUDIO_NewMiniportTopology(
    OUT PMINIPORTTOPOLOGY* OutMiniport,
    IN ULONG AssociatedPinsCount,
    IN PULONG AssociatedPinIds,
    IN CFunctionGroupNode * Node,
    IN CAdapterCommon * Adapter,
    IN PPCFILTER_DESCRIPTOR FilterDescription);

VOID
HDAUDIO_InitAmplifiers(
    IN CMiniportTopology *Miniport);

VOID
HDAUDIO_RestoreMasterSettings(
    IN CMiniportTopology *Miniport);

NTSTATUS
HDAUDIO_NewMiniportWaveRT(
    OUT PMINIPORTWAVERT *OutMiniport,
    IN ULONG AssociatedPinsCount,
    IN PULONG AssociatedPinIds,
    IN CFunctionGroupNode * Node,
    IN CAdapterCommon * Adapter,
    IN PPCFILTER_DESCRIPTOR FilterDescription);

NTSTATUS
HDAUDIO_EnumFunctionGroupWidgets(
    IN UCHAR CodecAddress,
    IN UCHAR FunctionGroupStartNodeId,
    IN CAdapterCommon *Adapter,
    OUT CFunctionGroupNode **OutFunctionGroupNode);

NTSTATUS
HDAUDIO_AllocateStream(
    OUT PMINIPORTWAVERTSTREAM *Stream,
    IN CAdapterCommon *Adapter,
    IN CFunctionGroupNode *Node,
    IN ULONG Pin,
    IN BOOLEAN Capture,
    IN PKSDATAFORMAT DataFormat,
    IN ULONG NodeCount,
    IN PULONG Nodes,
    IN PPCFILTER_DESCRIPTOR FilterDescription);

NTSTATUS
HDAUDIO_ValidateDataFormat(
    IN PKSDATAFORMAT DataFormat,
    IN PKSDATARANGE_AUDIO PinRange,
    OUT PHDAUDIO_STREAM_FORMAT StreamFormat OPTIONAL);

NTSTATUS
HDAUDIO_ValidateNodeFormats(
    IN CFunctionGroupNode *Node,
    IN ULONG NodeCount,
    IN PULONG Nodes,
    IN PHDAUDIO_STREAM_FORMAT StreamFormat);
