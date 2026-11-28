/*
 * PROJECT:     LiberNT Intel Smart Sound DSP endpoint driver
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     The WaveRT stream of an endpoint: every method one call into the audio interface (ksendpoint.c), as the codec driver's stream is one call into the codec bus
 * COPYRIGHT:   Copyright 2026 LiberNT contributors
 */

#include "private.h"

#define NDEBUG
#include <debug.h>

NTSTATUS
NTAPI
CMiniportWaveRTStream::QueryInterface(IN REFIID refiid, OUT PVOID *Output)
{
    if (IsEqualGUIDAligned(refiid, IID_IMiniportWaveRTStreamNotification) ||
        IsEqualGUIDAligned(refiid, IID_IUnknown) ||
        IsEqualGUIDAligned(refiid, IID_IMiniportWaveRTStream))
    {
        *Output = PVOID(PMINIPORTWAVERTSTREAM(this));
        PUNKNOWN(*Output)->AddRef();
        return STATUS_SUCCESS;
    }
    return STATUS_UNSUCCESSFUL;
}

NTSTATUS
NTAPI
CMiniportWaveRTStream::AllocateAudioBuffer(
    ULONG RequestedBufferSize,
    PMDL* AudioBufferMdl,
    ULONG* ActualSize,
    ULONG* OffsetFromFirstPage,
    MEMORY_CACHING_TYPE* CacheType)
{
    return AllocateBufferWithNotification(1, RequestedBufferSize, AudioBufferMdl, ActualSize, OffsetFromFirstPage, CacheType);
}

NTSTATUS
NTAPI
CMiniportWaveRTStream::GetPositionRegister(
    OUT KSRTAUDIO_HWREGISTER* Register)
{
    return DsppcmStreamPositionRegister(&m_Interface, m_Stream, Register);
}

NTSTATUS
NTAPI
CMiniportWaveRTStream::GetClockRegister(
    OUT KSRTAUDIO_HWREGISTER* Register)
{
    return DsppcmStreamClockRegister(&m_Interface, Register);
}

VOID
NTAPI
CMiniportWaveRTStream::GetHWLatency(
    IN KSRTAUDIO_HWLATENCY* hwLatency)
{
    DsppcmStreamLatency(&m_Interface, m_Stream, hwLatency);
}

VOID
NTAPI
CMiniportWaveRTStream::FreeAudioBuffer(
    PMDL AudioBufferMdl,
    ULONG BufferSize)
{
    FreeBufferWithNotification(AudioBufferMdl, BufferSize);
}

NTSTATUS
NTAPI
CMiniportWaveRTStream::GetPosition(
    OUT PKSAUDIO_POSITION Position)
{
    return DsppcmStreamPosition(&m_Interface, m_Stream, m_BufferSize, m_FifoSize, m_Capture, Position);
}

NTSTATUS
NTAPI
CMiniportWaveRTStream::SetFormat(
    IN PKSDATAFORMAT DataFormat)
{
    /* the stream's format is the one it was opened with, as for the codec driver */
    UNREFERENCED_PARAMETER(DataFormat);
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS
NTAPI
CMiniportWaveRTStream::SetState(
    IN KSSTATE State)
{
    NTSTATUS Status;

    Status = DsppcmStreamSetState(&m_Interface, m_Stream, State);
    if (!NT_SUCCESS(Status))
        DSPPCM_LOG("stream: state %u not set, 0x%08lx\n", (ULONG)State, Status);
    return Status;
}

NTSTATUS
NTAPI
CMiniportWaveRTStream::AllocateBufferWithNotification(
    ULONG NotificationCount,
    ULONG RequestedBufferSize,
    PMDL* AudioBufferMdl,
    ULONG* ActualSize,
    ULONG* OffsetFromFirstPage,
    MEMORY_CACHING_TYPE* CacheType)
{
    NTSTATUS Status;

    Status = DsppcmStreamAllocate(&m_Interface, m_Stream, NotificationCount, RequestedBufferSize, AudioBufferMdl, ActualSize,
                                  OffsetFromFirstPage, &m_FifoSize, CacheType);
    if (NT_SUCCESS(Status))
        m_BufferSize = *ActualSize;
    else
        DSPPCM_LOG("stream: no buffer of %lu bytes with %lu notifications, 0x%08lx\n", RequestedBufferSize, NotificationCount, Status);
    return Status;
}

NTSTATUS
NTAPI
CMiniportWaveRTStream::RegisterNotificationEvent(
    PKEVENT NotificationEvent)
{
    return DsppcmStreamRegisterNotificationEvent(&m_Interface, m_Stream, NotificationEvent);
}

NTSTATUS
NTAPI
CMiniportWaveRTStream::UnregisterNotificationEvent(PKEVENT NotificationEvent)
{
    return DsppcmStreamUnregisterNotificationEvent(&m_Interface, m_Stream, NotificationEvent);
}

VOID
NTAPI
CMiniportWaveRTStream::FreeBufferWithNotification(PMDL AudioBufferMdl, ULONG BufferSize)
{
    NTSTATUS Status;

    Status = DsppcmStreamFree(&m_Interface, m_Stream, AudioBufferMdl, BufferSize);
    if (!NT_SUCCESS(Status))
        DSPPCM_LOG("stream: the buffer is not released, 0x%08lx\n", Status);
    m_BufferSize = 0;
}

/*
 * A pin's stream: the format is the endpoint's (the pin's rules, with 24
 * valid bits in a 32-bit word only), then the interface opens the stream
 * with it and the stream object keeps the interface by value.
 */
NTSTATUS
DSPPCM_AllocateStream(
    OUT PMINIPORTWAVERTSTREAM* Stream,
    IN CMiniportWaveRT* Miniport,
    IN ULONG Pin,
    IN BOOLEAN Capture,
    IN PKSDATAFORMAT DataFormat)
{
    CAdapterCommon* Adapter = Miniport->GetAdapter();
    const INTELDSP_AUDIO_INTERFACE* Interface = Adapter->GetInterface();
    const INTELDSP_ENDPOINT* Endpoint = Adapter->GetEndpoint(Miniport->GetEndpointIndex());
    CMiniportWaveRTStream *This = NULL;
    HANDLE Opened = NULL;
    ULONG Index, SampleRate, Channels, ValidBits, ContainerBits;
    NTSTATUS Status;
    BOOLEAN Remembered;

    if (!Stream || !DataFormat || Pin != DSPPCM_WAVE_STREAM_PIN)
        return STATUS_INVALID_PARAMETER;
    if (!Endpoint || !(Capture ? Endpoint->Capture : Endpoint->Render))
        return STATUS_NOT_SUPPORTED;
    Status = DsppcmPinFormatFind(Endpoint, DataFormat, &Index);
    if (!NT_SUCCESS(Status))
    {
        DSPPCM_LOG("endpoint '%s' %s: the pin's format is refused, 0x%08lx\n", Endpoint->Name, Capture ? "capture" : "render", Status);
        return Status;
    }
    /* a model the firmware refused is remembered by the manager, and the opens after the first are refused without a line (N12-02): the pin's line is the first refusal's alone (N14-04), so the flag is read before the open that may set it */
    Remembered = Interface->ModelRefused(Interface->Context);
    Status = Interface->OpenStream(Interface->Context, Miniport->GetEndpointIndex(), Capture, DataFormat, &Opened);
    if (!NT_SUCCESS(Status))
    {
        if (!Remembered)
            DSPPCM_LOG("endpoint '%s' %s: the DSP gives no stream, 0x%08lx\n", Endpoint->Name, Capture ? "capture" : "render", Status);
        return Status;
    }
    This = new (NonPagedPool, TAG_DSPPCM) CMiniportWaveRTStream(NULL, Miniport->GetEndpointIndex(), Capture, Interface, Opened);
    if (!This)
    {
        DsppcmStreamClose(Interface, Opened);
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    This->AddRef();
    *Stream = This;
    if (NT_SUCCESS(IntelDspAudioFormatParse(DataFormat, &SampleRate, &Channels, &ValidBits, &ContainerBits)))
        DSPPCM_LOG("endpoint '%s' %s: pin created, %lu Hz %lu ch %lu in %lu bits\n", Endpoint->Name, Capture ? "capture" : "render",
                   SampleRate, Channels, ValidBits, ContainerBits);
    return STATUS_SUCCESS;
}
