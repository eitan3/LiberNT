/*
 * PROJECT:     LiberNT Intel Smart Sound DSP endpoint driver
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     The endpoint driver's half free of I/O: the data ranges a streaming pin offers from an endpoint's formats, and each wave pin method as one call into the audio interface; the C++ miniports call these, and the host tests run them over a fake interface
 * COPYRIGHT:   Copyright 2026 LiberNT contributors
 */

#include "ksendpoint.h"

ULONG
DsppcmPinRangesBuild(
    _In_ const INTELDSP_ENDPOINT* Endpoint,
    _Out_writes_(DSPPCM_PIN_RANGES_MAX) PKSDATARANGE_AUDIO Ranges,
    _Out_writes_(DSPPCM_PIN_RANGES_MAX) PKSDATARANGE* RangePointers)
{
    const INTELDSP_FORMAT* Format;
    ULONG I, Count = 0;

    RtlZeroMemory(Ranges, DSPPCM_PIN_RANGES_MAX * sizeof(Ranges[0]));
    RtlZeroMemory(RangePointers, DSPPCM_PIN_RANGES_MAX * sizeof(RangePointers[0]));
    for (I = 0; I < Endpoint->FormatCount && I < INTELDSP_ENDPOINT_FORMATS_MAX; I++)
    {
        ULONG R;

        Format = &Endpoint->Formats[I];
        /* one range per distinct rate, channel count and container, in the order the endpoint first names each: the formats normally come sorted (audioif.c IntcDspAudioFormatOrder), but the set does not depend on it */
        for (R = 0; R < Count; R++)
        {
            if (Ranges[R].MaximumSampleFrequency == Format->SampleRate && Ranges[R].MaximumChannels == Format->Channels &&
                Ranges[R].MaximumBitsPerSample == Format->ContainerBits)
                break;
        }
        if (R != Count)
            continue;
        if (Count == DSPPCM_PIN_RANGES_MAX)
            break;
        Ranges[Count].DataRange.FormatSize = sizeof(KSDATARANGE_AUDIO);
        Ranges[Count].DataRange.Flags = 0;
        Ranges[Count].DataRange.SampleSize = 0;
        Ranges[Count].DataRange.Reserved = 0;
        Ranges[Count].DataRange.MajorFormat = IntelDspAudioTypeAudio;
        Ranges[Count].DataRange.SubFormat = IntelDspAudioSubtypePcm;
        Ranges[Count].DataRange.Specifier = IntelDspAudioSpecifierWaveFormatEx;
        Ranges[Count].MaximumChannels = Format->Channels;
        /* the word the samples live in is what a wave format's bits per sample name; the valid bits within it are the pin's to check at the open */
        Ranges[Count].MinimumBitsPerSample = Format->ContainerBits;
        Ranges[Count].MaximumBitsPerSample = Format->ContainerBits;
        Ranges[Count].MinimumSampleFrequency = Format->SampleRate;
        Ranges[Count].MaximumSampleFrequency = Format->SampleRate;
        RangePointers[Count] = (PKSDATARANGE)&Ranges[Count];
        Count++;
    }
    return Count;
}

NTSTATUS
DsppcmPinFormatFind(
    _In_ const INTELDSP_ENDPOINT* Endpoint,
    _In_ const KSDATAFORMAT* DataFormat,
    _Out_ PULONG Index)
{
    ULONG SampleRate, Channels, ValidBits, ContainerBits;
    NTSTATUS Status;

    Status = IntelDspAudioFormatParse(DataFormat, &SampleRate, &Channels, &ValidBits, &ContainerBits);
    if (!NT_SUCCESS(Status))
        return Status;
    return IntelDspAudioFormatFind(Endpoint, SampleRate, Channels, ValidBits, ContainerBits, Index);
}

BOOLEAN
DsppcmPnpReleasesAdapter(
    _In_ UCHAR MinorFunction)
{
    return MinorFunction == IRP_MN_REMOVE_DEVICE || MinorFunction == IRP_MN_SURPRISE_REMOVAL;
}

NTSTATUS
DsppcmStreamAllocate(
    _In_ const INTELDSP_AUDIO_INTERFACE* Interface,
    _In_ HANDLE Stream,
    _In_ ULONG NotificationCount,
    _In_ ULONG RequestedSize,
    _Out_ PMDL* Mdl,
    _Out_ PULONG ActualSize,
    _Out_ PULONG OffsetFromFirstPage,
    _Out_ PULONG FifoSize,
    _Out_ MEMORY_CACHING_TYPE* CacheType)
{
    /* the codec driver's caching for the cyclic buffer (drivers/wdm/audio/drivers/hdaudio/stream.cpp AllocateBufferWithNotification) */
    *CacheType = MmWriteCombined;
    return Interface->AllocateBuffer(Interface->Context, Stream, NotificationCount, RequestedSize, Mdl, ActualSize, OffsetFromFirstPage,
                                     FifoSize);
}

NTSTATUS
DsppcmStreamRegisterNotificationEvent(
    _In_ const INTELDSP_AUDIO_INTERFACE* Interface,
    _In_ HANDLE Stream,
    _In_ PKEVENT NotificationEvent)
{
    return Interface->RegisterNotificationEvent(Interface->Context, Stream, NotificationEvent);
}

NTSTATUS
DsppcmStreamUnregisterNotificationEvent(
    _In_ const INTELDSP_AUDIO_INTERFACE* Interface,
    _In_ HANDLE Stream,
    _In_ PKEVENT NotificationEvent)
{
    return Interface->UnregisterNotificationEvent(Interface->Context, Stream, NotificationEvent);
}

NTSTATUS
DsppcmStreamSetState(
    _In_ const INTELDSP_AUDIO_INTERFACE* Interface,
    _In_ HANDLE Stream,
    _In_ KSSTATE State)
{
    return Interface->SetState(Interface->Context, Stream, State);
}

NTSTATUS
DsppcmStreamPositionRegister(
    _In_ const INTELDSP_AUDIO_INTERFACE* Interface,
    _In_ HANDLE Stream,
    _Out_ PKSRTAUDIO_HWREGISTER Register)
{
    /* the codec driver's answer (stream.cpp GetPositionRegister): a 32-bit register */
    Register->Width = 32;
    return Interface->GetPositionRegister(Interface->Context, Stream, (PULONG*)&Register->Register);
}

NTSTATUS
DsppcmStreamClockRegister(
    _In_ const INTELDSP_AUDIO_INTERFACE* Interface,
    _Out_ PKSRTAUDIO_HWREGISTER Register)
{
    Register->Width = 32;
    return Interface->GetClockRegister(Interface->Context, (PULONG*)&Register->Register);
}

NTSTATUS
DsppcmStreamLatency(
    _In_ const INTELDSP_AUDIO_INTERFACE* Interface,
    _In_ HANDLE Stream,
    _Out_ PKSRTAUDIO_HWLATENCY Latency)
{
    return Interface->GetLatency(Interface->Context, Stream, Latency);
}

NTSTATUS
DsppcmStreamFree(
    _In_ const INTELDSP_AUDIO_INTERFACE* Interface,
    _In_ HANDLE Stream,
    _In_ PMDL Mdl,
    _In_ ULONG BufferSize)
{
    return Interface->FreeBuffer(Interface->Context, Stream, Mdl, BufferSize);
}

NTSTATUS
DsppcmStreamClose(
    _In_ const INTELDSP_AUDIO_INTERFACE* Interface,
    _In_ HANDLE Stream)
{
    return Interface->CloseStream(Interface->Context, Stream);
}

NTSTATUS
DsppcmPositionFromValue(
    _In_ ULONG Value,
    _In_ ULONG BufferSize,
    _In_ ULONG FifoSize,
    _In_ BOOLEAN Capture,
    _Out_ PKSAUDIO_POSITION Position)
{
    ULONG Offset;

    RtlZeroMemory(Position, sizeof(*Position));
    /* a position is asked of a stream with its buffer */
    if (BufferSize == 0)
        return STATUS_INVALID_PARAMETER;
    Offset = Value % BufferSize;
    if (Capture)
    {
        /* the capture's read cursor is the link position less the FIFO's share, around the cyclic buffer */
        Position->PlayOffset = (Offset + (BufferSize - FifoSize % BufferSize)) % BufferSize;
        Position->WriteOffset = Position->PlayOffset;
        return STATUS_SUCCESS;
    }
    Position->PlayOffset = Offset;
    /* the controller may have prefetched up to one FIFO beyond the link position: that part stays outside the writable area */
    Position->WriteOffset = (Offset + FifoSize) % BufferSize;
    return STATUS_SUCCESS;
}

NTSTATUS
DsppcmStreamPosition(
    _In_ const INTELDSP_AUDIO_INTERFACE* Interface,
    _In_ HANDLE Stream,
    _In_ ULONG BufferSize,
    _In_ ULONG FifoSize,
    _In_ BOOLEAN Capture,
    _Out_ PKSAUDIO_POSITION Position)
{
    PULONG Register = NULL;
    NTSTATUS Status;

    RtlZeroMemory(Position, sizeof(*Position));
    /* a position is asked of a stream with its buffer */
    if (BufferSize == 0)
        return STATUS_INVALID_PARAMETER;
    Status = Interface->GetPositionRegister(Interface->Context, Stream, &Register);
    if (!NT_SUCCESS(Status))
        return Status;
    if (Register == NULL)
        return STATUS_DEVICE_NOT_READY;
    return DsppcmPositionFromValue(DSPPCM_READ_POSITION(Register), BufferSize, FifoSize, Capture, Position);
}
