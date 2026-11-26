/*
 * PROJECT:     LiberNT Intel Smart Sound DSP endpoint driver
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Internal definitions: the adapter common object, the topology and WaveRT miniports and the WaveRT stream of an endpoint of the Intel audio DSP
 * COPYRIGHT:   Copyright 2026 LiberNT contributors
 */

#pragma once

#include <ntddk.h>
#include <ks.h>
#include <portcls.h>
#include <ksmedia.h>
#include <hdaudio.h>
#include <ddk/ntstrsafe.h>

/* intcdsp.sys's audio interface and the pure format rules both endpoint drivers share (drivers/wdm/audio/intcdsp) */
#include "audioformat.h"
#include "ksendpoint.h"
#include "tables.h"

#define TAG_DSPPCM 'MCPD'
/* The endpoints of a PCM: render and capture at most */
#define DSPPCM_ENDPOINTS_MAX      2
/* A subdevice name: the PCM's name and a suffix of at most six characters */
#define DSPPCM_NAME_CHARS         64

#define DSPPCM_LOG(fmt, ...) \
    DbgPrintEx(DPFLTR_IHVAUDIO_ID, DPFLTR_ERROR_LEVEL, "dsppcm.sys: " fmt, ##__VA_ARGS__)

PVOID
__cdecl
operator new(size_t Size, POOL_TYPE PoolType, ULONG Tag) noexcept;

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

/*
 * The adapter of one audio child: the interface the child answers
 * (GUID_INTELDSP_AUDIO_INTERFACE, asked of the child's own PDO as hdaudio.sys
 * asks the codec bus of its), the endpoints it names, and one topology and one
 * wave subdevice per endpoint, joined by a physical connection.
 */
class CAdapterCommon : public CUnknownImpl<IAdapterPowerManagement>
{
  public:
    STDMETHODIMP QueryInterface(REFIID InterfaceId, PVOID *Interface);
    NTSTATUS NTAPI Initialize(IN PDEVICE_OBJECT DeviceObject, IN PIRP Irp, IN PRESOURCELIST ResourceList);
    IMP_IAdapterPowerManagement;
    CAdapterCommon(IUnknown* OuterUnknown) :
        Interface(), m_EndpointCount(0), m_Endpoint(), m_WavePins(), m_Ranges(), m_RangePointers(), m_WaveFilter(),
        m_WaveName(), m_TopoName(), m_PowerState(PowerDeviceUnspecified)
    {
    }
    virtual ~CAdapterCommon()
    {
    }

    const INTELDSP_AUDIO_INTERFACE*
    GetInterface() const
    {
        return &Interface;
    }

    ULONG
    GetEndpointCount() const
    {
        return m_EndpointCount;
    }

    const INTELDSP_ENDPOINT*
    GetEndpoint(ULONG Index) const
    {
        return Index < m_EndpointCount ? &m_Endpoint[Index] : NULL;
    }

    /* The index of the endpoint of a direction, MAXULONG when the PCM has none */
    ULONG
    GetEndpointIndex(BOOLEAN Capture) const
    {
        ULONG Index;

        for (Index = 0; Index < m_EndpointCount; Index++)
            if (Capture ? m_Endpoint[Index].Capture : m_Endpoint[Index].Render)
                return Index;
        return MAXULONG;
    }

    /* The wave filter of an endpoint: the static one with its streaming pin's data ranges filled */
    const PCFILTER_DESCRIPTOR*
    GetWaveFilter(ULONG Index) const
    {
        return Index < m_EndpointCount ? &m_WaveFilter[Index] : NULL;
    }

  private:
    INTELDSP_AUDIO_INTERFACE Interface;
    ULONG m_EndpointCount;
    INTELDSP_ENDPOINT m_Endpoint[DSPPCM_ENDPOINTS_MAX];
    PCPIN_DESCRIPTOR m_WavePins[DSPPCM_ENDPOINTS_MAX][2];
    KSDATARANGE_AUDIO m_Ranges[DSPPCM_ENDPOINTS_MAX][DSPPCM_PIN_RANGES_MAX];
    PKSDATARANGE m_RangePointers[DSPPCM_ENDPOINTS_MAX][DSPPCM_PIN_RANGES_MAX];
    PCFILTER_DESCRIPTOR m_WaveFilter[DSPPCM_ENDPOINTS_MAX];
    WCHAR m_WaveName[DSPPCM_ENDPOINTS_MAX][DSPPCM_NAME_CHARS];
    WCHAR m_TopoName[DSPPCM_ENDPOINTS_MAX][DSPPCM_NAME_CHARS];
    DEVICE_POWER_STATE m_PowerState;
};

typedef struct
{
    ULONG_PTR Reserved[4]; // used by portcls
    CAdapterCommon *AdapterCommon;
} DSPPCM_DEVICE_EXTENSION, *PDSPPCM_DEVICE_EXTENSION;

class CMiniportTopology : public CUnknownImpl<IMiniportTopology>
{
  public:
    STDMETHODIMP QueryInterface(REFIID InterfaceId, PVOID *Interface);
    IMP_IMiniportTopology;
    CMiniportTopology(IUnknown* OuterUnknown, ULONG EndpointIndex, CAdapterCommon* Adapter, PPCFILTER_DESCRIPTOR Filter)
        : m_Port(0), m_Adapter(Adapter), m_FilterDescription(Filter), m_EndpointIndex(EndpointIndex)
    {
    }
    virtual ~CMiniportTopology()
    {
    }

    CAdapterCommon*
    GetAdapter() const
    {
        return m_Adapter;
    }

    ULONG
    GetEndpointIndex() const
    {
        return m_EndpointIndex;
    }

    PPORTTOPOLOGY
    GetPort() const
    {
        return m_Port;
    }

  private:
    PPORTTOPOLOGY m_Port;
    CAdapterCommon* m_Adapter;
    PPCFILTER_DESCRIPTOR m_FilterDescription;
    ULONG m_EndpointIndex;
};

class CMiniportWaveRT : public CUnknownImpl<IMiniportWaveRT>
{
  public:
    STDMETHODIMP QueryInterface(REFIID InterfaceId, PVOID *Interface);
    IMP_IMiniportWaveRT;
    CMiniportWaveRT(IUnknown* OuterUnknown, ULONG EndpointIndex, CAdapterCommon* Adapter, PPCFILTER_DESCRIPTOR Filter)
        : m_Port(0), m_Adapter(Adapter), m_FilterDescription(Filter), m_EndpointIndex(EndpointIndex)
    {
    }
    virtual ~CMiniportWaveRT()
    {
    }

    CAdapterCommon*
    GetAdapter() const
    {
        return m_Adapter;
    }

    ULONG
    GetEndpointIndex() const
    {
        return m_EndpointIndex;
    }

  private:
    PPORTWAVERT m_Port;
    CAdapterCommon* m_Adapter;
    PPCFILTER_DESCRIPTOR m_FilterDescription;
    ULONG m_EndpointIndex;
};

class CMiniportWaveRTStream : public CUnknownImpl<IMiniportWaveRTStreamNotification>
{
  public:
    STDMETHODIMP QueryInterface(REFIID InterfaceId, PVOID *Interface);
    CMiniportWaveRTStream(IUnknown* OuterUnknown, ULONG EndpointIndex, BOOLEAN Capture, const INTELDSP_AUDIO_INTERFACE* Interface,
                          HANDLE Stream)
        : m_Capture(Capture), m_Stream(Stream), m_BufferSize(0), m_FifoSize(0)
    {
        RtlCopyMemory(&m_Interface, Interface, sizeof(m_Interface));
    }
    virtual ~CMiniportWaveRTStream()
    {
        /* the stream's tag goes back to the pool when the pin's last reference goes */
        DsppcmStreamClose(&m_Interface, m_Stream);
    }

    IMP_IMiniportWaveRTStream;
    IMP_IMiniportWaveRTStreamNotification;

  private:
    INTELDSP_AUDIO_INTERFACE m_Interface;
    BOOLEAN m_Capture;
    HANDLE m_Stream;
    ULONG m_BufferSize;
    ULONG m_FifoSize;
};

NTSTATUS
DSPPCM_NewAdapterCommon(
    OUT CAdapterCommon** OutAdapter);

NTSTATUS
DSPPCM_NewMiniportTopology(
    OUT PMINIPORTTOPOLOGY* OutMiniport,
    IN ULONG EndpointIndex,
    IN CAdapterCommon* Adapter,
    IN PPCFILTER_DESCRIPTOR FilterDescription);

NTSTATUS
DSPPCM_NewMiniportWaveRT(
    OUT PMINIPORTWAVERT* OutMiniport,
    IN ULONG EndpointIndex,
    IN CAdapterCommon* Adapter,
    IN PPCFILTER_DESCRIPTOR FilterDescription);

NTSTATUS
DSPPCM_AllocateStream(
    OUT PMINIPORTWAVERTSTREAM* Stream,
    IN CMiniportWaveRT* Miniport,
    IN ULONG Pin,
    IN BOOLEAN Capture,
    IN PKSDATAFORMAT DataFormat);

/* The settings of the topology's nodes, kept in the child's own key and restored at its start (prophnd.cpp) */
VOID
DSPPCM_SaveSetting(
    IN CMiniportTopology* Miniport,
    IN PCWSTR ValueName,
    IN LONG Value);

VOID
DSPPCM_RestoreSettings(
    IN CMiniportTopology* Miniport);
