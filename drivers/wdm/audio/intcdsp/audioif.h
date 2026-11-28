/*
 * PROJECT:     LiberNT Intel Smart Sound DSP driver
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     GUID_INTELDSP_AUDIO_INTERFACE, version 1: what intcdsp.sys answers on every audio child it enumerates, and dsppcm.sys, the function driver of such a child, asks for against its own PDO (plan §7.2); every piece of hardware work of an endpoint stays behind it
 * COPYRIGHT:   Copyright 2026 LiberNT contributors
 */

#ifndef _DRIVERS_INTCDSP_AUDIOIF_H_
#define _DRIVERS_INTCDSP_AUDIOIF_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <ntddk.h>
#include <wdm.h>
/* the KS types the interface names (KSSTATE, PKSDATAFORMAT, the hardware latency and position registers), with the headers portcls.h sets up before them in the order they need */
#include <portcls.h>

/*
 * The interface is this project's own; no ABI of another system is
 * declared here. Its shape follows the private ADSP bus interface
 * (adspbus.h): the standard INTERFACE header, then the routines in a fixed
 * order, which is the ABI of version 1. A later version appends members
 * only; a caller asks for the version and the size it was built with
 * (WdfFdoQueryForInterface, or IRP_MN_QUERY_INTERFACE sent to its own PDO
 * as hdaudio.sys does: drivers/wdm/audio/drivers/hdaudio/common.cpp,
 * AcquireBusInterface).
 */

/* {798afb7e-6311-4b18-91c7-1df3390c7d33}: generated for this interface alone */
DEFINE_GUID(GUID_INTELDSP_AUDIO_INTERFACE,
    0x798afb7e, 0x6311, 0x4b18, 0x91, 0xc7, 0x1d, 0xf3, 0x39, 0x0c, 0x7d, 0x33);

#define INTELDSP_AUDIO_INTERFACE_VERSION   2   /* 2: TeardownPoll (N8-02) */
/* a PCM's name as the topology gives it: softplg.h SOFTPLG_TEXT_BYTES (44 characters and the terminator) */
#define INTELDSP_ENDPOINT_NAME_BYTES       45
/* the formats one endpoint offers: a host copier of the P3 topologies carries three, a shipped one up to nine */
#define INTELDSP_ENDPOINT_FORMATS_MAX      16

/* One format an endpoint carries, in the units a wave format names them (mmreg.h WAVEFORMATEX) */
typedef struct _INTELDSP_FORMAT
{
    ULONG SampleRate;
    ULONG Channels;
    /* the valid bits of a sample and the word they live in: 24 valid bits live in a 32-bit word, left-justified (hdaudio_format.h HDAUDIO_SampleContainerBits) */
    ULONG ValidBits;
    ULONG ContainerBits;
} INTELDSP_FORMAT, *PINTELDSP_FORMAT;

/*
 * An endpoint: one direction of a PCM of the selected topology. The name
 * is the PCM's; a PCM with a playback copier and a capture copier has two
 * endpoints, the render one first. The formats are the distinct ones of
 * the host copier's host-side formats (its inputs for render, its outputs
 * for capture) that the controller can carry.
 */
typedef struct _INTELDSP_ENDPOINT
{
    CHAR Name[INTELDSP_ENDPOINT_NAME_BYTES];
    BOOLEAN Render;
    BOOLEAN Capture;
    ULONG FormatCount;
    INTELDSP_FORMAT Formats[INTELDSP_ENDPOINT_FORMATS_MAX];
    /* the volume node's range and step in 1/65536 decibels, 0 being unity: the driver's own, until the gain module's range is known (plan C5) */
    LONG VolumeMinimum;
    LONG VolumeMaximum;
    LONG VolumeStep;
} INTELDSP_ENDPOINT, *PINTELDSP_ENDPOINT;

/* The controls of an endpoint's topology nodes */
typedef enum _INTELDSP_CONTROL
{
    /* the volume node's level in 1/65536 decibels, 0 the unity */
    IntcDspControlVolume = 0,
    /* the mute node's state, FALSE unmuted */
    IntcDspControlMute = 1
} INTELDSP_CONTROL;

typedef _Must_inspect_result_ NTSTATUS(*PINTELDSP_GET_ENDPOINT_COUNT)(_In_ PVOID Context, _Out_ PULONG Count);
typedef _Must_inspect_result_ NTSTATUS(*PINTELDSP_GET_ENDPOINT)(_In_ PVOID Context, _In_ ULONG Index, _Out_ PINTELDSP_ENDPOINT Endpoint);
/* the format check, the controller's idle reference, a stream of the pool and its reset (plan §5.10 OpenStream); refused while the DSP is not booted */
typedef _Must_inspect_result_ NTSTATUS(*PINTELDSP_OPEN_STREAM)(_In_ PVOID Context, _In_ ULONG EndpointIndex, _In_ BOOLEAN Capture, _In_ PKSDATAFORMAT DataFormat, _Out_ PHANDLE Stream);
/* the cyclic buffer with its notifications, then the PCM's pipelines created, its widgets initialized and bound (plan §5.10 AllocateBuffer); the stream routines take the interface's Context first, as the bus interface's do (adspbus.h) */
typedef _Must_inspect_result_ NTSTATUS(*PINTELDSP_ALLOCATE_BUFFER)(_In_ PVOID Context, _In_ HANDLE Stream, _In_ ULONG NotificationCount, _In_ ULONG RequestedBytes, _Out_ PMDL* Mdl, _Out_ PULONG Bytes, _Out_ PULONG OffsetFromFirstPage, _Out_ PULONG FifoSize);
/* KSSTATE_RUN: the pipelines paused and running, then the stream; any other state: the stream paused, its buffer and the firmware's objects kept until FreeBuffer */
typedef _Must_inspect_result_ NTSTATUS(*PINTELDSP_SET_STATE)(_In_ PVOID Context, _In_ HANDLE Stream, _In_ KSSTATE State);
typedef _Must_inspect_result_ NTSTATUS(*PINTELDSP_GET_POSITION_REGISTER)(_In_ PVOID Context, _In_ HANDLE Stream, _Out_ PULONG* Position);
/* the controller's wall clock, the same register for every stream */
typedef _Must_inspect_result_ NTSTATUS(*PINTELDSP_GET_CLOCK_REGISTER)(_In_ PVOID Context, _Out_ PULONG* Wallclock);
typedef _Must_inspect_result_ NTSTATUS(*PINTELDSP_GET_LATENCY)(_In_ PVOID Context, _In_ HANDLE Stream, _Out_ PKSRTAUDIO_HWLATENCY Latency);
typedef _Must_inspect_result_ NTSTATUS(*PINTELDSP_STREAM_NOTIFICATION_EVENT)(_In_ PVOID Context, _In_ HANDLE Stream, _In_ PKEVENT NotificationEvent);
/* the buffer goes back to the bus and the firmware's objects of the PCM are deleted while no other stream holds it (plan §5.10 FreeBuffer); the MDL and the size are the ones AllocateBuffer gave */
typedef _Must_inspect_result_ NTSTATUS(*PINTELDSP_FREE_BUFFER)(_In_ PVOID Context, _In_ HANDLE Stream, _In_ PMDL Mdl, _In_ ULONG Bytes);
/* the stream's tag goes back to the pool (plan §5.10 CloseStream) */
typedef _Must_inspect_result_ NTSTATUS(*PINTELDSP_CLOSE_STREAM)(_In_ PVOID Context, _In_ HANDLE Stream);
typedef _Must_inspect_result_ NTSTATUS(*PINTELDSP_GET_CONTROL)(_In_ PVOID Context, _In_ ULONG EndpointIndex, _In_ INTELDSP_CONTROL Control, _Out_ PLONG Value);
typedef _Must_inspect_result_ NTSTATUS(*PINTELDSP_SET_CONTROL)(_In_ PVOID Context, _In_ ULONG EndpointIndex, _In_ INTELDSP_CONTROL Control, _In_ LONG Value);
/* the drain of the pending teardowns (N8-02): asked from the pin's position property as the idle work item asks it, so that a free that met a busy transport is finished with no further transition of the stream's own; answers how many stay pending; nothing is sent while a request is in progress */
typedef UINT32(*PINTELDSP_TEARDOWN_POLL)(_In_ PVOID Context);

typedef struct _INTELDSP_AUDIO_INTERFACE
{
    USHORT Size;
    USHORT Version;
    PVOID Context;
    PINTERFACE_REFERENCE InterfaceReference;
    PINTERFACE_DEREFERENCE InterfaceDereference;
    PINTELDSP_GET_ENDPOINT_COUNT GetEndpointCount;
    PINTELDSP_GET_ENDPOINT GetEndpoint;
    PINTELDSP_OPEN_STREAM OpenStream;
    PINTELDSP_ALLOCATE_BUFFER AllocateBuffer;
    PINTELDSP_SET_STATE SetState;
    PINTELDSP_GET_POSITION_REGISTER GetPositionRegister;
    PINTELDSP_GET_CLOCK_REGISTER GetClockRegister;
    PINTELDSP_GET_LATENCY GetLatency;
    PINTELDSP_STREAM_NOTIFICATION_EVENT RegisterNotificationEvent;
    PINTELDSP_STREAM_NOTIFICATION_EVENT UnregisterNotificationEvent;
    PINTELDSP_FREE_BUFFER FreeBuffer;
    PINTELDSP_CLOSE_STREAM CloseStream;
    PINTELDSP_GET_CONTROL GetControl;
    PINTELDSP_SET_CONTROL SetControl;
    PINTELDSP_TEARDOWN_POLL TeardownPoll;
} INTELDSP_AUDIO_INTERFACE, *PINTELDSP_AUDIO_INTERFACE;

#ifdef __cplusplus
}
#endif

#endif
