/*
 * PROJECT:     LiberNT Intel Smart Sound DSP endpoint driver
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     The adapter of an audio child: the interface the child answers, the endpoints it names, one topology and one wave subdevice per endpoint joined by a physical connection
 * COPYRIGHT:   Copyright 2026 LiberNT contributors
 */

#include "private.h"

#define NDEBUG
#include <debug.h>

PVOID
__cdecl
operator new(size_t Size, POOL_TYPE PoolType, ULONG Tag) noexcept
{
    PVOID P = ExAllocatePoolWithTag(PoolType, Size, Tag);

    if (P)
        RtlZeroMemory(P, Size);
    return P;
}

void __cdecl
operator delete(PVOID ptr)
{
    if (ptr)
        ExFreePool(ptr);
}

void __cdecl
operator delete(PVOID ptr, UINT_PTR Unused)
{
    if (ptr)
        ExFreePool(ptr);
}

static
PCWSTR
DsppcmDirectionName(
    BOOLEAN Capture)
{
    return Capture ? L"capture" : L"render";
}

/* The endpoint's PCM name, widened: the topology's names are ASCII (intcdsp.sys refuses a file name that is not) */
static
VOID
DsppcmCopyName(
    _Out_writes_(Chars) PWCHAR Target,
    SIZE_T Chars,
    _In_z_ const CHAR* Name)
{
    SIZE_T I;

    for (I = 0; I + 1 < Chars && Name[I] != 0; I++)
        Target[I] = (WCHAR)(UCHAR)Name[I];
    Target[I] = 0;
}

NTSTATUS
NTAPI
CAdapterCommon::QueryInterface(IN REFIID refiid, OUT PVOID *Output)
{
    if (IsEqualGUIDAligned(refiid, IID_IAdapterPowerManagement) ||
        IsEqualGUIDAligned(refiid, IID_IPinCount) ||
        IsEqualGUIDAligned(refiid, IID_IUnknown))
    {
        *Output = PVOID(PADAPTERPOWERMANAGEMENT(this));
        PUNKNOWN(*Output)->AddRef();
        return STATUS_SUCCESS;
    }
    return STATUS_UNSUCCESSFUL;
}

VOID NTAPI
CAdapterCommon::PowerChangeState(IN POWER_STATE NewState)
{
    /* the DSP's power is intcdsp.sys's, behind the interface; the endpoint follows the child's state in its record only */
    if (NewState.DeviceState >= PowerDeviceD0 && NewState.DeviceState <= PowerDeviceD3)
        m_PowerState = NewState.DeviceState;
}

NTSTATUS
NTAPI
CAdapterCommon::QueryDeviceCapabilities(IN PDEVICE_CAPABILITIES PowerDeviceCaps)
{
    UNREFERENCED_PARAMETER(PowerDeviceCaps);
    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
CAdapterCommon::QueryPowerChangeState(IN POWER_STATE NewStateQuery)
{
    if (NewStateQuery.DeviceState < PowerDeviceD0 || NewStateQuery.DeviceState > PowerDeviceD3)
        return STATUS_INVALID_PARAMETER;
    return STATUS_SUCCESS;
}

/* The interface intcdsp.sys answers on this driver's own PDO: IRP_MN_QUERY_INTERFACE sent down the stack as hdaudio.cpp sends the codec bus's (common.cpp AcquireBusInterface) */
static
NTSTATUS
DsppcmAcquireInterface(
    IN PDEVICE_OBJECT DeviceObject,
    OUT PINTELDSP_AUDIO_INTERFACE Interface)
{
    PIO_STACK_LOCATION Stack;
    PIRP Irp;
    NTSTATUS Status;

    Irp = IoAllocateIrp(DeviceObject->StackSize, FALSE);
    if (Irp == NULL)
        return STATUS_INSUFFICIENT_RESOURCES;
    Stack = IoGetNextIrpStackLocation(Irp);
    Stack->MajorFunction = IRP_MJ_PNP;
    Stack->MinorFunction = IRP_MN_QUERY_INTERFACE;
    Stack->Parameters.QueryInterface.InterfaceType = &GUID_INTELDSP_AUDIO_INTERFACE;
    Stack->Parameters.QueryInterface.Size = sizeof(*Interface);
    Stack->Parameters.QueryInterface.Version = INTELDSP_AUDIO_INTERFACE_VERSION;
    Stack->Parameters.QueryInterface.Interface = (PINTERFACE)Interface;
    Irp->IoStatus.Information = 0;
    Irp->IoStatus.Status = STATUS_NOT_SUPPORTED;
    Status = PcForwardIrpSynchronous(DeviceObject, Irp);
    IoFreeIrp(Irp);
    return Status;
}

/*
 * One endpoint's wave filter: the static one of its direction, with the
 * streaming pin's data ranges from the endpoint's formats. STATUS_NOT_FOUND
 * for an endpoint without a format, which gets no subdevice.
 */
static
NTSTATUS
DsppcmBuildWaveFilter(
    IN ULONG Index,
    IN const INTELDSP_ENDPOINT* Endpoint,
    OUT PCFILTER_DESCRIPTOR* Filter,
    OUT PCPIN_DESCRIPTOR* Pins,
    OUT PKSDATARANGE_AUDIO Ranges,
    OUT PKSDATARANGE* RangePointers)
{
    ULONG Count;

    RtlCopyMemory(Pins, Endpoint->Capture ? WaveInPins : WaveOutPins, 2 * sizeof(PCPIN_DESCRIPTOR));
    Count = DsppcmPinRangesBuild(Endpoint, Ranges, RangePointers);
    if (Count == 0)
        return STATUS_NOT_FOUND;
    Pins[DSPPCM_WAVE_STREAM_PIN].KsPinDescriptor.DataRanges = RangePointers;
    Pins[DSPPCM_WAVE_STREAM_PIN].KsPinDescriptor.DataRangesCount = Count;
    *Filter = *(Endpoint->Capture ? WaveInFilterDescription : WaveOutFilterDescription);
    Filter->Pins = Pins;
    return STATUS_SUCCESS;
}

/*
 * The two subdevices of an endpoint, as the codec driver installs them
 * (common.cpp InstallDevice and BuildInstallFilter): the topology port with
 * its miniport, the WaveRT port with its, each registered with its name,
 * then the physical connection from the wave filter's bridge pin to the
 * topology's wave pin (render), or the other way (capture).
 */
static
NTSTATUS
DsppcmInstallEndpoint(
    IN PDEVICE_OBJECT DeviceObject,
    IN PIRP Irp,
    IN PRESOURCELIST ResourceList,
    IN CAdapterCommon* Adapter,
    IN ULONG Index,
    IN const PCFILTER_DESCRIPTOR* WaveFilter,
    IN PWCHAR TopoName,
    IN PWCHAR WaveName)
{
    const INTELDSP_ENDPOINT* Endpoint = Adapter->GetEndpoint(Index);
    PMINIPORTTOPOLOGY MiniportTopology = NULL;
    PMINIPORTWAVERT MiniportWaveRT = NULL;
    PPORT PortTopology = NULL, PortWave = NULL;
    PUNKNOWN UnknownTopology = NULL, UnknownWave = NULL;
    NTSTATUS Status;

    Status = PcNewPort(&PortTopology, CLSID_PortTopology);
    if (!NT_SUCCESS(Status))
        return Status;
    Status = DSPPCM_NewMiniportTopology(&MiniportTopology, Index, (CAdapterCommon*)Adapter, const_cast<PCFILTER_DESCRIPTOR*>(
        Endpoint->Capture ? TopoInFilterDescription : TopoOutFilterDescription));
    if (NT_SUCCESS(Status))
        Status = PortTopology->Init(DeviceObject, Irp, (PUNKNOWN)MiniportTopology, (PUNKNOWN)Adapter, ResourceList);
    if (NT_SUCCESS(Status))
        Status = PcRegisterSubdevice(DeviceObject, TopoName, PortTopology);
    if (NT_SUCCESS(Status))
        Status = PortTopology->QueryInterface(IID_IUnknown, (PVOID*)&UnknownTopology);
    if (NT_SUCCESS(Status))
        DSPPCM_LOG("endpoint '%s' %s: registered as '%S'\n", Endpoint->Name, DsppcmDirectionName(Endpoint->Capture), TopoName);
    else
        DSPPCM_LOG("endpoint '%s' %s: the topology '%S' is not registered, 0x%08lx\n", Endpoint->Name, DsppcmDirectionName(Endpoint->Capture),
                   TopoName, Status);

    if (NT_SUCCESS(Status))
        Status = PcNewPort(&PortWave, CLSID_PortWaveRT);
    if (NT_SUCCESS(Status))
        Status = DSPPCM_NewMiniportWaveRT(&MiniportWaveRT, Index, (CAdapterCommon*)Adapter, const_cast<PCFILTER_DESCRIPTOR*>(WaveFilter));
    if (NT_SUCCESS(Status))
        Status = PortWave->Init(DeviceObject, Irp, (PUNKNOWN)MiniportWaveRT, (PUNKNOWN)Adapter, ResourceList);
    if (NT_SUCCESS(Status))
        Status = PcRegisterSubdevice(DeviceObject, WaveName, PortWave);
    if (NT_SUCCESS(Status))
        Status = PortWave->QueryInterface(IID_IUnknown, (PVOID*)&UnknownWave);
    if (NT_SUCCESS(Status))
        DSPPCM_LOG("endpoint '%s' %s: registered as '%S'\n", Endpoint->Name, DsppcmDirectionName(Endpoint->Capture), WaveName);
    else
        DSPPCM_LOG("endpoint '%s' %s: the wave filter '%S' is not registered, 0x%08lx\n", Endpoint->Name, DsppcmDirectionName(Endpoint->Capture),
                   WaveName, Status);

    /* the connection of the two: the wave's bridge pin to the topology's wave pin, as the codec's filters are joined */
    if (NT_SUCCESS(Status))
    {
        if (Endpoint->Capture)
            Status = PcRegisterPhysicalConnection(DeviceObject, UnknownTopology, DSPPCM_TOPO_WAVE_PIN, UnknownWave, DSPPCM_WAVE_BRIDGE_PIN);
        else
            Status = PcRegisterPhysicalConnection(DeviceObject, UnknownWave, DSPPCM_WAVE_BRIDGE_PIN, UnknownTopology, DSPPCM_TOPO_WAVE_PIN);
        if (!NT_SUCCESS(Status))
            DSPPCM_LOG("endpoint '%s' %s: the physical connection of its filters is not registered, 0x%08lx\n", Endpoint->Name,
                       DsppcmDirectionName(Endpoint->Capture), Status);
    }

    /* the registration holds the ports; the references of this function go */
    if (UnknownWave)
        UnknownWave->Release();
    if (UnknownTopology)
        UnknownTopology->Release();
    if (PortWave)
        PortWave->Release();
    if (PortTopology)
        PortTopology->Release();
    if (MiniportWaveRT)
        MiniportWaveRT->Release();
    if (MiniportTopology)
        MiniportTopology->Release();
    return Status;
}

NTSTATUS
NTAPI
CAdapterCommon::Initialize(IN PDEVICE_OBJECT DeviceObject, IN PIRP Irp, IN PRESOURCELIST ResourceList)
{
    ULONG Count = 0, Index, Ranges;
    NTSTATUS Status;

    Status = DsppcmAcquireInterface(DeviceObject, &Interface);
    if (!NT_SUCCESS(Status))
    {
        DSPPCM_LOG("no audio interface on the device's stack: 0x%08lx; no endpoint\n", Status);
        return Status;
    }
    Status = Interface.GetEndpointCount(Interface.Context, &Count);
    if (!NT_SUCCESS(Status))
    {
        DSPPCM_LOG("the audio interface names no endpoint: 0x%08lx; no endpoint\n", Status);
        return Status;
    }
    if (Count > DSPPCM_ENDPOINTS_MAX)
        Count = DSPPCM_ENDPOINTS_MAX;
    for (Index = 0; Index < Count; Index++)
    {
        Status = Interface.GetEndpoint(Interface.Context, Index, &m_Endpoint[Index]);
        if (!NT_SUCCESS(Status) || m_Endpoint[Index].Name[0] == 0)
            break;
        Status = DsppcmBuildWaveFilter(Index, &m_Endpoint[Index], &m_WaveFilter[Index], m_WavePins[Index], m_Ranges[Index], m_RangePointers[Index]);
        if (!NT_SUCCESS(Status))
        {
            DSPPCM_LOG("endpoint '%s' %s: no format to offer; no endpoint\n", m_Endpoint[Index].Name, DsppcmDirectionName(m_Endpoint[Index].Capture));
            continue;
        }
        /* the subdevice names: the PCM's name, then Topo or Wave for render, InTopo or InWave for capture (the codec's eLineInWave alike) */
        DsppcmCopyName(m_TopoName[Index], DSPPCM_NAME_CHARS, m_Endpoint[Index].Name);
        DsppcmCopyName(m_WaveName[Index], DSPPCM_NAME_CHARS, m_Endpoint[Index].Name);
        Status = RtlStringCchCatW(m_TopoName[Index], DSPPCM_NAME_CHARS, m_Endpoint[Index].Capture ? L"InTopo" : L"Topo");
        if (NT_SUCCESS(Status))
            Status = RtlStringCchCatW(m_WaveName[Index], DSPPCM_NAME_CHARS, m_Endpoint[Index].Capture ? L"InWave" : L"Wave");
        if (!NT_SUCCESS(Status))
            continue;
        Ranges = m_WavePins[Index][DSPPCM_WAVE_STREAM_PIN].KsPinDescriptor.DataRangesCount;
        DSPPCM_LOG("endpoint '%s' %s: %lu formats in %lu data ranges, %ld..%ld in 1/65536 dB by %ld\n", m_Endpoint[Index].Name,
                   DsppcmDirectionName(m_Endpoint[Index].Capture), m_Endpoint[Index].FormatCount, Ranges, m_Endpoint[Index].VolumeMinimum,
                   m_Endpoint[Index].VolumeMaximum, m_Endpoint[Index].VolumeStep);
        m_EndpointCount = Index + 1;
    }
    /* the subdevices of every endpoint that has formats: one failure leaves the earlier ones registered and is reported */
    for (Index = 0; Index < m_EndpointCount; Index++)
    {
        Status = DsppcmInstallEndpoint(DeviceObject, Irp, ResourceList, this, Index, &m_WaveFilter[Index], m_TopoName[Index], m_WaveName[Index]);
        if (!NT_SUCCESS(Status))
            break;
    }
    return m_EndpointCount != 0 ? Status : STATUS_NOT_FOUND;
}

NTSTATUS
DSPPCM_NewAdapterCommon(OUT CAdapterCommon **OutAdapter)
{
    CAdapterCommon *This;

    This = new (NonPagedPool, TAG_DSPPCM) CAdapterCommon(NULL);
    if (!This)
        return STATUS_INSUFFICIENT_RESOURCES;
    This->AddRef();
    *OutAdapter = This;
    return STATUS_SUCCESS;
}
