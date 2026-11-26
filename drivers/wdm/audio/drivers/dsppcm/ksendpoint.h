/*
 * PROJECT:     LiberNT Intel Smart Sound DSP endpoint driver
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     The endpoint driver's half free of I/O: the data ranges a streaming pin offers from an endpoint's formats, and each wave pin method as one call into the audio interface; the C++ miniports call these, and the host tests run them over a fake interface
 * COPYRIGHT:   Copyright 2026 LiberNT contributors
 */

#ifndef _DRIVERS_DSPPCM_KSENDPOINT_H_
#define _DRIVERS_DSPPCM_KSENDPOINT_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <ntddk.h>
#include <wdm.h>
#include <portcls.h>
#include "audioformat.h"

/* One data range per distinct rate, channel count and container of an endpoint (the valid bits within a container vary: 24 and 32 share the 32-bit word) */
#define DSPPCM_PIN_RANGES_MAX    INTELDSP_ENDPOINT_FORMATS_MAX

/*
 * The streaming pin's data ranges from an endpoint's formats: PCM with
 * WAVEFORMATEX (ksmedia.h KSDATARANGE_AUDIO), one per distinct rate, channel
 * count and container, in the order the endpoint first names each (its
 * sorted order, the deepest container first at each rate and channel count,
 * when intcdsp.sys built it). The count is returned, 0 for an endpoint
 * without formats.
 */
ULONG
DsppcmPinRangesBuild(
    _In_ const INTELDSP_ENDPOINT* Endpoint,
    _Out_writes_(DSPPCM_PIN_RANGES_MAX) PKSDATARANGE_AUDIO Ranges,
    _Out_writes_(DSPPCM_PIN_RANGES_MAX) PKSDATARANGE* RangePointers);

/*
 * The index of the endpoint's format a data format names: the format is
 * parsed (audioformat.h) and looked up; STATUS_NOT_SUPPORTED when the
 * endpoint carries no such format.
 */
NTSTATUS
DsppcmPinFormatFind(
    _In_ const INTELDSP_ENDPOINT* Endpoint,
    _In_ const KSDATAFORMAT* DataFormat,
    _Out_ PULONG Index);

/* The wave pin's methods: each is one call into the interface and answers its status; nothing is done twice and nothing is swallowed */

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
    _Out_ MEMORY_CACHING_TYPE* CacheType);

NTSTATUS
DsppcmStreamRegisterNotificationEvent(
    _In_ const INTELDSP_AUDIO_INTERFACE* Interface,
    _In_ HANDLE Stream,
    _In_ PKEVENT NotificationEvent);

NTSTATUS
DsppcmStreamUnregisterNotificationEvent(
    _In_ const INTELDSP_AUDIO_INTERFACE* Interface,
    _In_ HANDLE Stream,
    _In_ PKEVENT NotificationEvent);

/* KSSTATE as the pin is told it: the interface's one routine takes it (RUN runs, the other states pause) */
NTSTATUS
DsppcmStreamSetState(
    _In_ const INTELDSP_AUDIO_INTERFACE* Interface,
    _In_ HANDLE Stream,
    _In_ KSSTATE State);

NTSTATUS
DsppcmStreamPositionRegister(
    _In_ const INTELDSP_AUDIO_INTERFACE* Interface,
    _In_ HANDLE Stream,
    _Out_ PKSRTAUDIO_HWREGISTER Register);

NTSTATUS
DsppcmStreamClockRegister(
    _In_ const INTELDSP_AUDIO_INTERFACE* Interface,
    _Out_ PKSRTAUDIO_HWREGISTER Register);

NTSTATUS
DsppcmStreamLatency(
    _In_ const INTELDSP_AUDIO_INTERFACE* Interface,
    _In_ HANDLE Stream,
    _Out_ PKSRTAUDIO_HWLATENCY Latency);

NTSTATUS
DsppcmStreamFree(
    _In_ const INTELDSP_AUDIO_INTERFACE* Interface,
    _In_ HANDLE Stream,
    _In_ PMDL Mdl,
    _In_ ULONG BufferSize);

NTSTATUS
DsppcmStreamClose(
    _In_ const INTELDSP_AUDIO_INTERFACE* Interface,
    _In_ HANDLE Stream);

/*
 * The position of a stream from its position register, as the codec driver
 * reports it (drivers/wdm/audio/drivers/hdaudio/stream.cpp GetPosition):
 * the register's value modulo the buffer size; on capture the read cursor
 * trails the link position by the FIFO, on render the write cursor leads it
 * by the FIFO, for the engine to keep that part of the buffer untouched.
 */
NTSTATUS
DsppcmStreamPosition(
    _In_ const INTELDSP_AUDIO_INTERFACE* Interface,
    _In_ HANDLE Stream,
    _In_ ULONG BufferSize,
    _In_ ULONG FifoSize,
    _In_ BOOLEAN Capture,
    _Out_ PKSAUDIO_POSITION Position);

/* The reading of a position register: the macro of the kernel, a plain load in the host tests */
#ifndef DSPPCM_READ_POSITION
#define DSPPCM_READ_POSITION(Register)    READ_REGISTER_ULONG((PULONG)(Register))
#endif

#ifdef __cplusplus
}
#endif

#endif
