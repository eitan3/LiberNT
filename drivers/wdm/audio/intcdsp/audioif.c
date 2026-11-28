/*
 * PROJECT:     LiberNT Intel Smart Sound DSP driver
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     The audio children of the DSP device and the interface they answer (GUID_INTELDSP_AUDIO_INTERFACE, version 1): one child per PCM of the selected topology, the endpoint records from the PCM's host copiers, and every stream transition over the stream manager
 * COPYRIGHT:   Copyright 2026 LiberNT contributors
 */

#include "intcdsp.h"
#include <ntstrsafe.h>
#include "audioformat.h"

/*
 * What this file adds to the manager (stream.h), which keeps the
 * behaviour: the children (drivers/wdm/audio/hdaudbus/buspdo.cpp and fdo.cpp
 * are the tree's pattern of a KMDF child list with a query interface on each
 * PDO), the endpoint records a child's driver reads before it registers its
 * subdevices, and the KS state names over the manager's transitions:
 *
 *  - OpenStream: the format is the endpoint's (audioformat.h) and the
 *    manager's; the tag is the pool's;
 *  - AllocateBuffer: the manager's allocation (the buffer, then the PCM's
 *    pipelines, widgets and bindings);
 *  - SetState: RUN is the manager's run (the pipelines first, then the
 *    stream); PAUSE, STOP and ACQUIRE are its pause, which keeps the buffer
 *    and the firmware's objects for the next run or for FreeBuffer;
 *  - FreeBuffer and CloseStream: the manager's free and close.
 *
 * The controls of an endpoint's volume and mute nodes are the driver's own
 * until the gain module is told (plan C5): a set level is kept and answered,
 * and the stream stays at unity.
 */

/* The audio child's hardware ID, the plan's §7.2 (INTCDSP\PCM&CTLR_VEN_8086&CTLR_DEV_7F50) */
#define INTELDSP_CHILD_DEVICE_ID     L"INTCDSP\\PCM&CTLR_VEN_%04X&CTLR_DEV_%04X"
/* the compatible ID every controller's audio child carries, so one INF line matches them all */
#define INTELDSP_CHILD_COMPAT_ID     L"INTCDSP\\PCM"
/* the instance ID: the PCM's index in the topology */
#define INTELDSP_CHILD_INSTANCE_ID   L"%02X"
#define INTELDSP_CHILD_TEXT          L"Intel Smart Sound Technology DSP Audio"
#define INTELDSP_CHILD_LOCATION      L"Intel Audio DSP"

/* The volume node's range and step while the gain module's own are not read (plan C5), in 1/65536 decibels: from -96 dB to unity in steps of a decibel */
#define INTELDSP_VOLUME_MINIMUM      (-96L * 65536L)
#define INTELDSP_VOLUME_MAXIMUM      0L
#define INTELDSP_VOLUME_STEP         65536L

/* One PCM as its child carries it: the endpoint records the interface hands out, the controls and the latency estimate */
typedef struct _INTCDSP_AUDIO_PCM
{
    /* render first when the PCM has a playback copier, capture after it or alone; 0 when the PCM has no host copier, which gets no child */
    UINT32 Endpoints;
    INTELDSP_ENDPOINT Endpoint[2];
    LONG Volume[2];
    BOOLEAN Mute[2];
    /* the firmware's share of each endpoint's latency in bytes, an estimate (plan §5.9) */
    ULONG LatencyBytes[2];
} INTCDSP_AUDIO_PCM, *PINTCDSP_AUDIO_PCM;

/* The context of an audio child's PDO: its parent, its PCM, and the PCM's records by value, so the child never reads the parent's model after its creation */
typedef struct _INTCDSP_AUDIO_PDO
{
    PINTCDSP_DEVICE Dev;
    UINT32 Pcm;
    INTCDSP_AUDIO_PCM PcmState;
} INTCDSP_AUDIO_PDO, *PINTCDSP_AUDIO_PDO;

WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(INTCDSP_AUDIO_PDO, IntcDspAudioPdoGetData)

/* The child list's description of one child: the PCM's index */
typedef struct _INTCDSP_AUDIO_DESCRIPTION
{
    WDF_CHILD_IDENTIFICATION_DESCRIPTION_HEADER Header;
    UINT32 Pcm;
} INTCDSP_AUDIO_DESCRIPTION, *PINTCDSP_AUDIO_DESCRIPTION;

static
EVT_WDF_CHILD_LIST_CREATE_DEVICE IntcDspAudioEvtChildCreate;
static
EVT_WDF_CHILD_LIST_IDENTIFICATION_DESCRIPTION_COMPARE IntcDspAudioEvtChildCompare;

/* The order of two formats in an endpoint: the rate ascending, the channels ascending, the container descending, the valid bits descending */
static
INT32
IntcDspAudioFormatOrder(
    _In_ const INTELDSP_FORMAT* First,
    _In_ const INTELDSP_FORMAT* Second)
{
    if (First->SampleRate != Second->SampleRate)
        return First->SampleRate < Second->SampleRate ? -1 : 1;
    if (First->Channels != Second->Channels)
        return First->Channels < Second->Channels ? -1 : 1;
    if (First->ContainerBits != Second->ContainerBits)
        return First->ContainerBits > Second->ContainerBits ? -1 : 1;
    if (First->ValidBits != Second->ValidBits)
        return First->ValidBits > Second->ValidBits ? -1 : 1;
    return 0;
}

/*
 * One direction of a PCM as an endpoint: its name, the direction, and the
 * distinct formats of its host copier's host-side formats that the
 * controller can carry (the inputs of a playback copier, the outputs of a
 * capture copier), in the order of IntcDspAudioFormatOrder. A PCM without
 * a host copier of that direction gets an endpoint record without formats.
 */
static
VOID
IntcDspAudioEndpointFill(
    _In_ const SOFTPLG_IPC4_MODEL* Model,
    UINT32 Pcm,
    BOOLEAN Capture,
    _Out_ PINTELDSP_ENDPOINT Endpoint)
{
    const SOFTPLG_IPC4_PCM* P = &Model->Pcms[Pcm];
    const SOFTPLG_IPC4_WIDGET* Widget;
    UINT32 Copier = Capture ? P->CaptureCopier : P->PlaybackCopier;
    INTELDSP_FORMAT Format;
    ULONG I, F, First, Count;
    SIZE_T N;

    RtlZeroMemory(Endpoint, sizeof(*Endpoint));
    for (N = 0; N + 1 < sizeof(Endpoint->Name) && P->Name[N] != 0; N++)
        Endpoint->Name[N] = P->Name[N];
    Endpoint->Name[N] = 0;
    Endpoint->Render = !Capture;
    Endpoint->Capture = Capture;
    Endpoint->VolumeMinimum = INTELDSP_VOLUME_MINIMUM;
    Endpoint->VolumeMaximum = INTELDSP_VOLUME_MAXIMUM;
    Endpoint->VolumeStep = INTELDSP_VOLUME_STEP;
    if (Copier == SOFTPLG_NO_WIDGET || Copier >= Model->WidgetCount)
        return;
    Widget = &Model->Widgets[Copier];
    if (Widget->FirstFormat == SOFTPLG_NO_FORMAT || Widget->FirstFormat >= Model->FormatCount)
        return;
    First = Widget->FirstFormat + (Capture ? Widget->InputFormats : 0);
    Count = Capture ? Widget->OutputFormats : Widget->InputFormats;
    for (F = 0; F < Count && First + F < Model->FormatCount; F++)
    {
        const SOFTPLG_IPC4_FORMAT* Source = &Model->Formats[First + F];
        BOOLEAN Placed = FALSE;

        if (!Source->Expressible)
            continue;
        Format.SampleRate = Source->Words[SOFTPLG_FORMAT_WORD_RATE];
        Format.Channels = SOFTPLG_FORMAT_CHANNELS_OF(Source->Words[SOFTPLG_FORMAT_WORD_CFG]);
        Format.ValidBits = SOFTPLG_FORMAT_VALID_OF(Source->Words[SOFTPLG_FORMAT_WORD_CFG]);
        Format.ContainerBits = Source->Words[SOFTPLG_FORMAT_WORD_DEPTH];
        for (I = 0; I < Endpoint->FormatCount && !Placed; I++)
        {
            INT32 Order = IntcDspAudioFormatOrder(&Format, &Endpoint->Formats[I]);

            if (Order == 0)
            {
                /* the format is there already */
                Placed = TRUE;
            }
            else if (Order < 0)
            {
                if (Endpoint->FormatCount == INTELDSP_ENDPOINT_FORMATS_MAX)
                    break;
                RtlMoveMemory(&Endpoint->Formats[I + 1], &Endpoint->Formats[I],
                              (Endpoint->FormatCount - I) * sizeof(Endpoint->Formats[0]));
                Endpoint->Formats[I] = Format;
                Endpoint->FormatCount++;
                Placed = TRUE;
            }
        }
        if (!Placed && I == Endpoint->FormatCount && I < INTELDSP_ENDPOINT_FORMATS_MAX)
        {
            Endpoint->Formats[I] = Format;
            Endpoint->FormatCount++;
        }
    }
}

/*
 * The firmware's share of an endpoint's latency, in bytes, as an estimate:
 * the host copier's gateway DMA buffer and the input and output buffers of
 * every module the PCM reaches (plan §5.9); the real delay is the
 * firmware's to report (plan P5).
 */
static
ULONG
IntcDspAudioLatencyBytes(
    _In_ const SOFTPLG_IPC4_MODEL* Model,
    UINT32 Pcm,
    UINT32 Copier)
{
    const SOFTPLG_IPC4_WIDGET* Widget;
    UINT64 Bytes = 0;
    UINT32 W;

    if (Copier < Model->WidgetCount)
        Bytes = Model->Widgets[Copier].Config.Words[SOFTPLG_COPIER_DMA_BUFFER_WORD];
    for (W = 0; W < Model->WidgetCount; W++)
    {
        Widget = &Model->Widgets[W];
        if (Widget->Kind == SoftplgKindNone || Widget->Kind == SoftplgKindScheduler || !SoftplgModelReachable(Model, Pcm, W))
            continue;
        Bytes += (UINT64)Widget->Config.Words[1] + Widget->Config.Words[2];
    }
    return Bytes > MAXULONG ? MAXULONG : (ULONG)Bytes;
}

/* A PCM's records from the model: its endpoints, the controls at unity and unmuted, the latency estimate */
static
VOID
IntcDspAudioPcmFill(
    _In_ const SOFTPLG_IPC4_MODEL* Model,
    UINT32 Pcm,
    _Out_ PINTCDSP_AUDIO_PCM State)
{
    const SOFTPLG_IPC4_PCM* P = &Model->Pcms[Pcm];
    BOOLEAN Render = P->PlaybackCopier != SOFTPLG_NO_WIDGET;
    BOOLEAN Capture = P->CaptureCopier != SOFTPLG_NO_WIDGET;

    RtlZeroMemory(State, sizeof(*State));
    if (!Render && !Capture)
        return;
    State->Endpoints = Render ? (Capture ? 2u : 1u) : 1u;
    IntcDspAudioEndpointFill(Model, Pcm, Render ? FALSE : TRUE, &State->Endpoint[0]);
    if (Render && Capture)
        IntcDspAudioEndpointFill(Model, Pcm, TRUE, &State->Endpoint[1]);
    State->LatencyBytes[0] = IntcDspAudioLatencyBytes(Model, Pcm, Render ? P->PlaybackCopier : P->CaptureCopier);
    if (Render && Capture)
        State->LatencyBytes[1] = IntcDspAudioLatencyBytes(Model, Pcm, P->CaptureCopier);
}

/* The index of the endpoint of a direction, 0 when the PCM has none */
static
UINT32
IntcDspAudioEndpointIndex(
    _In_ PINTCDSP_AUDIO_PDO Pdo,
    BOOLEAN Capture)
{
    if (Pdo->PcmState.Endpoints > 0 && Pdo->PcmState.Endpoint[0].Capture == Capture)
        return 0;
    if (Pdo->PcmState.Endpoints > 1 && Pdo->PcmState.Endpoint[1].Capture == Capture)
        return 1;
    return 0;
}

static
PINTCDSP_DEVICE
IntcDspAudioDevice(
    _In_ PVOID Context)
{
    return ((PINTCDSP_AUDIO_PDO)Context)->Dev;
}

/* The interface's functions; Context is the child's PDO context */

static
_Must_inspect_result_
NTSTATUS
IntcDspAudioGetEndpointCount(
    _In_ PVOID Context,
    _Out_ PULONG Count)
{
    *Count = ((PINTCDSP_AUDIO_PDO)Context)->PcmState.Endpoints;
    return STATUS_SUCCESS;
}

static
_Must_inspect_result_
NTSTATUS
IntcDspAudioGetEndpoint(
    _In_ PVOID Context,
    _In_ ULONG Index,
    _Out_ PINTELDSP_ENDPOINT Endpoint)
{
    PINTCDSP_AUDIO_PDO Pdo = (PINTCDSP_AUDIO_PDO)Context;

    RtlZeroMemory(Endpoint, sizeof(*Endpoint));
    if (Index >= Pdo->PcmState.Endpoints || Index >= 2)
        return STATUS_INVALID_PARAMETER;
    *Endpoint = Pdo->PcmState.Endpoint[Index];
    return STATUS_SUCCESS;
}

static
_Must_inspect_result_
NTSTATUS
IntcDspAudioOpenStream(
    _In_ PVOID Context,
    _In_ ULONG EndpointIndex,
    _In_ BOOLEAN Capture,
    _In_ PKSDATAFORMAT DataFormat,
    _Out_ PHANDLE Stream)
{
    PINTCDSP_AUDIO_PDO Pdo = (PINTCDSP_AUDIO_PDO)Context;
    PINTCDSP_DEVICE Dev = Pdo->Dev;
    const INTELDSP_ENDPOINT* Endpoint;
    PINTCDSP_STREAM Opened;
    ULONG SampleRate, Channels, ValidBits, ContainerBits, Index;
    NTSTATUS Status;

    *Stream = NULL;
    if (EndpointIndex >= Pdo->PcmState.Endpoints || EndpointIndex >= 2)
        return STATUS_INVALID_PARAMETER;
    Endpoint = &Pdo->PcmState.Endpoint[EndpointIndex];
    if (!(Capture ? Endpoint->Capture : Endpoint->Render))
        return STATUS_INVALID_PARAMETER;
    Status = IntelDspAudioFormatParse(DataFormat, &SampleRate, &Channels, &ValidBits, &ContainerBits);
    if (!NT_SUCCESS(Status))
    {
        IntcDspLog("endpoint '%s' %s: the format is not well-formed PCM (0x%08lx); refused\n", Endpoint->Name,
                   Capture ? "capture" : "render", Status);
        return Status;
    }
    Status = IntelDspAudioFormatFind(Endpoint, SampleRate, Channels, ValidBits, ContainerBits, &Index);
    if (!NT_SUCCESS(Status))
    {
        IntcDspLog("endpoint '%s' %s: %lu Hz %lu ch %lu in %lu bits is not a format it offers; refused\n", Endpoint->Name,
                   Capture ? "capture" : "render", SampleRate, Channels, ValidBits, ContainerBits);
        return Status;
    }
    Status = IntcDspStreamOpen(&Dev->Streams, Pdo->Pcm, Capture, SampleRate, Channels, ValidBits, ContainerBits, Stream);
    if (!NT_SUCCESS(Status))
        return Status;
    Opened = IntcDspStreamOf(&Dev->Streams, *Stream);
    IntcDspLog("endpoint '%s' %s: stream %u opened, %lu Hz %lu ch %lu in %lu bits\n", Endpoint->Name, Capture ? "capture" : "render",
               Opened->Tag, SampleRate, Channels, ValidBits, ContainerBits);
    return Status;
}

static
_Must_inspect_result_
NTSTATUS
IntcDspAudioAllocateBuffer(
    _In_ PVOID Context,
    _In_ HANDLE Stream,
    _In_ ULONG NotificationCount,
    _In_ ULONG RequestedBytes,
    _Out_ PMDL* Mdl,
    _Out_ PULONG Bytes,
    _Out_ PULONG OffsetFromFirstPage,
    _Out_ PULONG FifoSize)
{
    PINTCDSP_STREAMS Streams = &IntcDspAudioDevice(Context)->Streams;
    PINTCDSP_STREAM Managed;
    NTSTATUS Status;

    *Mdl = NULL;
    *Bytes = 0;
    *OffsetFromFirstPage = 0;
    *FifoSize = 0;
    Status = IntcDspStreamAllocate(Streams, Stream, RequestedBytes, NotificationCount);
    if (!NT_SUCCESS(Status))
        return Status;
    Managed = IntcDspStreamOf(Streams, Stream);
    if (Managed->Bytes > MAXULONG || Managed->OffsetFromFirstPage > MAXULONG)
    {
        /* the wave pin's outputs are 32-bit: a buffer beyond their reach goes back at once */
        IntcDspStreamFree(Streams, Stream);
        return STATUS_INTEGER_OVERFLOW;
    }
    *Mdl = Managed->Mdl;
    *Bytes = (ULONG)Managed->Bytes;
    *OffsetFromFirstPage = (ULONG)Managed->OffsetFromFirstPage;
    *FifoSize = Managed->FifoSize;
    return STATUS_SUCCESS;
}

static
_Must_inspect_result_
NTSTATUS
IntcDspAudioSetState(
    _In_ PVOID Context,
    _In_ HANDLE Stream,
    _In_ KSSTATE State)
{
    PINTCDSP_STREAMS Streams = &IntcDspAudioDevice(Context)->Streams;
    PINTCDSP_STREAM Managed = IntcDspStreamOf(Streams, Stream);

    if (Managed == NULL)
    {
        /* a handle of an older generation is the device's doing, not the pin's (E1, D4): the manager's answer for a stream it let go */
        if (IntcDspStreamStale(Streams, Stream))
            return STATUS_DEVICE_REMOVED;
        return STATUS_INVALID_HANDLE;
    }
    /* a stream without a buffer is stopped already: the pin's acquire and stop before its allocation are answered, and so are those of a stream whose free was answered while its teardown is pending (N7-01) */
    if (State != KSSTATE_RUN && (Managed->State == IntcDspStreamStateOpen || Managed->State == IntcDspStreamStateTeardown))
        return STATUS_SUCCESS;
    if (State == KSSTATE_RUN)
        return IntcDspStreamRun(Streams, Stream);
    return IntcDspStreamPause(Streams, Stream);
}

static
_Must_inspect_result_
NTSTATUS
IntcDspAudioGetPositionRegister(
    _In_ PVOID Context,
    _In_ HANDLE Stream,
    _Out_ PULONG* Position)
{
    return IntcDspStreamPositionRegister(&IntcDspAudioDevice(Context)->Streams, Stream, Position);
}

static
_Must_inspect_result_
NTSTATUS
IntcDspAudioGetClockRegister(
    _In_ PVOID Context,
    _Out_ PULONG* Wallclock)
{
    return IntcDspStreamClockRegister(&IntcDspAudioDevice(Context)->Streams, Wallclock);
}

static
_Must_inspect_result_
NTSTATUS
IntcDspAudioGetLatency(
    _In_ PVOID Context,
    _In_ HANDLE Stream,
    _Out_ PKSRTAUDIO_HWLATENCY Latency)
{
    PINTCDSP_AUDIO_PDO Pdo = (PINTCDSP_AUDIO_PDO)Context;
    PINTCDSP_STREAM Managed = IntcDspStreamOf(&Pdo->Dev->Streams, Stream);

    RtlZeroMemory(Latency, sizeof(*Latency));
    /* a stream the device let go, or a handle of an older generation (E1, D4): the register getters' answer, before anything of the stream is read */
    if (IntcDspStreamStale(&Pdo->Dev->Streams, Stream) || (Managed != NULL && Managed->Lost))
        return STATUS_DEVICE_REMOVED;
    if (Managed == NULL)
        return STATUS_INVALID_HANDLE;
    Latency->FifoSize = Managed->FifoSize;
    /* an estimate of the firmware's share, logged as one */
    Latency->CodecDelay = Pdo->PcmState.LatencyBytes[IntcDspAudioEndpointIndex(Pdo, Managed->Capture)];
    IntcDspLog("stream %u: latency: fifo %lu bytes, dsp estimate %lu bytes (the copier's gateway buffer and the PCM's module buffers)\n",
               Managed->Tag, Latency->FifoSize, Latency->CodecDelay);
    return STATUS_SUCCESS;
}

static
_Must_inspect_result_
NTSTATUS
IntcDspAudioRegisterNotificationEvent(
    _In_ PVOID Context,
    _In_ HANDLE Stream,
    _In_ PKEVENT NotificationEvent)
{
    return IntcDspStreamRegisterEvent(&IntcDspAudioDevice(Context)->Streams, Stream, NotificationEvent);
}

static
_Must_inspect_result_
NTSTATUS
IntcDspAudioUnregisterNotificationEvent(
    _In_ PVOID Context,
    _In_ HANDLE Stream,
    _In_ PKEVENT NotificationEvent)
{
    return IntcDspStreamUnregisterEvent(&IntcDspAudioDevice(Context)->Streams, Stream, NotificationEvent);
}

static
_Must_inspect_result_
NTSTATUS
IntcDspAudioFreeBuffer(
    _In_ PVOID Context,
    _In_ HANDLE Stream,
    _In_ PMDL Mdl,
    _In_ ULONG Bytes)
{
    PINTCDSP_STREAMS Streams = &IntcDspAudioDevice(Context)->Streams;
    PINTCDSP_STREAM Managed = IntcDspStreamOf(Streams, Stream);

    /* only the buffer the allocation gave goes back: another one is refused before anything is released; a stream the manager let go keeps its buffer under the handle's word for this free (stream.c, R5-01), and the manager answers that word, or a handle that names nothing */
    if (Managed != NULL && Managed->State != IntcDspStreamStateReleased && (Managed->Mdl != Mdl || Managed->Bytes != Bytes))
        return STATUS_INVALID_PARAMETER;
    return IntcDspStreamFree(Streams, Stream);
}

static
_Must_inspect_result_
NTSTATUS
IntcDspAudioCloseStream(
    _In_ PVOID Context,
    _In_ HANDLE Stream)
{
    return IntcDspStreamClose(&IntcDspAudioDevice(Context)->Streams, Stream);
}

static
_Must_inspect_result_
NTSTATUS
IntcDspAudioGetControl(
    _In_ PVOID Context,
    _In_ ULONG EndpointIndex,
    _In_ INTELDSP_CONTROL Control,
    _Out_ PLONG Value)
{
    PINTCDSP_AUDIO_PDO Pdo = (PINTCDSP_AUDIO_PDO)Context;

    *Value = 0;
    if (EndpointIndex >= Pdo->PcmState.Endpoints || EndpointIndex >= 2)
        return STATUS_INVALID_PARAMETER;
    switch (Control)
    {
        case IntcDspControlVolume:
            *Value = Pdo->PcmState.Volume[EndpointIndex];
            return STATUS_SUCCESS;
        case IntcDspControlMute:
            *Value = Pdo->PcmState.Mute[EndpointIndex] ? TRUE : FALSE;
            return STATUS_SUCCESS;
        default:
            return STATUS_INVALID_PARAMETER;
    }
}

static
_Must_inspect_result_
NTSTATUS
IntcDspAudioSetControl(
    _In_ PVOID Context,
    _In_ ULONG EndpointIndex,
    _In_ INTELDSP_CONTROL Control,
    _In_ LONG Value)
{
    PINTCDSP_AUDIO_PDO Pdo = (PINTCDSP_AUDIO_PDO)Context;
    const INTELDSP_ENDPOINT* Endpoint;
    LONG Minimum, Maximum, Step;
    ULONG Down;

    if (EndpointIndex >= Pdo->PcmState.Endpoints || EndpointIndex >= 2)
        return STATUS_INVALID_PARAMETER;
    switch (Control)
    {
        case IntcDspControlVolume:
            /* the level is kept on the node's steps from unity down, the nearest one, and the PCM's gain module is told it (E3) */
            Endpoint = &Pdo->PcmState.Endpoint[EndpointIndex];
            Minimum = Endpoint->VolumeMinimum;
            Maximum = Endpoint->VolumeMaximum;
            Step = Endpoint->VolumeStep;
            if (Value > Maximum)
                Value = Maximum;
            if (Value < Minimum)
                Value = Minimum;
            if (Step > 0)
            {
                Down = ((ULONG)(Maximum - Value) + (ULONG)Step / 2) / (ULONG)Step;
                Value = Maximum - (LONG)(Down * (ULONG)Step);
                if (Value < Minimum)
                    Value = Minimum;
            }
            Pdo->PcmState.Volume[EndpointIndex] = Value;
            return IntcDspStreamsGainSet(&Pdo->Dev->Streams, Pdo->Pcm, Value, Pdo->PcmState.Mute[EndpointIndex]);
        case IntcDspControlMute:
            Pdo->PcmState.Mute[EndpointIndex] = Value ? TRUE : FALSE;
            return IntcDspStreamsGainSet(&Pdo->Dev->Streams, Pdo->Pcm, Pdo->PcmState.Volume[EndpointIndex], Pdo->PcmState.Mute[EndpointIndex]);
        default:
            return STATUS_INVALID_PARAMETER;
    }
}

static
VOID
IntcDspAudioInterfaceReference(
    _In_ PVOID Context)
{
    UNREFERENCED_PARAMETER(Context);
}

/* The drain of the pending teardowns (N8-02): the manager's public poll, the pending count as its answer */
static
UINT32
IntcDspAudioTeardownPoll(
    _In_ PVOID Context)
{
    return IntcDspStreamsTeardownPoll(&IntcDspAudioDevice(Context)->Streams);
}

/* The interface of one child: the routines in the order of the structure, the child's PDO context as theirs */
static
VOID
IntcDspAudioInterfaceFill(
    _In_ PINTCDSP_AUDIO_PDO Pdo,
    _Out_ PINTELDSP_AUDIO_INTERFACE Interface)
{
    RtlZeroMemory(Interface, sizeof(*Interface));
    Interface->Size = sizeof(*Interface);
    Interface->Version = INTELDSP_AUDIO_INTERFACE_VERSION;
    Interface->Context = Pdo;
    Interface->InterfaceReference = IntcDspAudioInterfaceReference;
    Interface->InterfaceDereference = IntcDspAudioInterfaceReference;
    Interface->GetEndpointCount = IntcDspAudioGetEndpointCount;
    Interface->GetEndpoint = IntcDspAudioGetEndpoint;
    Interface->OpenStream = IntcDspAudioOpenStream;
    Interface->AllocateBuffer = IntcDspAudioAllocateBuffer;
    Interface->SetState = IntcDspAudioSetState;
    Interface->GetPositionRegister = IntcDspAudioGetPositionRegister;
    Interface->GetClockRegister = IntcDspAudioGetClockRegister;
    Interface->GetLatency = IntcDspAudioGetLatency;
    Interface->RegisterNotificationEvent = IntcDspAudioRegisterNotificationEvent;
    Interface->UnregisterNotificationEvent = IntcDspAudioUnregisterNotificationEvent;
    Interface->FreeBuffer = IntcDspAudioFreeBuffer;
    Interface->CloseStream = IntcDspAudioCloseStream;
    Interface->GetControl = IntcDspAudioGetControl;
    Interface->SetControl = IntcDspAudioSetControl;
    Interface->TeardownPoll = IntcDspAudioTeardownPoll;
}

static
BOOLEAN
IntcDspAudioEvtChildCompare(
    _In_ WDFCHILDLIST DeviceList,
    _In_ PWDF_CHILD_IDENTIFICATION_DESCRIPTION_HEADER First,
    _In_ PWDF_CHILD_IDENTIFICATION_DESCRIPTION_HEADER Second)
{
    PINTCDSP_AUDIO_DESCRIPTION A = CONTAINING_RECORD(First, INTCDSP_AUDIO_DESCRIPTION, Header);
    PINTCDSP_AUDIO_DESCRIPTION B = CONTAINING_RECORD(Second, INTCDSP_AUDIO_DESCRIPTION, Header);

    UNREFERENCED_PARAMETER(DeviceList);
    return A->Pcm == B->Pcm;
}

/* The child list calls for the device object of a PCM: its hardware ID, the interface, and the PCM's records in its context */
static
_Must_inspect_result_
NTSTATUS
IntcDspAudioEvtChildCreate(
    _In_ WDFCHILDLIST DeviceList,
    _In_ PWDF_CHILD_IDENTIFICATION_DESCRIPTION_HEADER IdentificationDescription,
    _Inout_ PWDFDEVICE_INIT ChildInit)
{
    PINTCDSP_AUDIO_DESCRIPTION Description = CONTAINING_RECORD(IdentificationDescription, INTCDSP_AUDIO_DESCRIPTION, Header);
    PINTCDSP_DEVICE Dev = IntcDspGetData(WdfChildListGetDevice(DeviceList));
    DECLARE_CONST_UNICODE_STRING(Text, INTELDSP_CHILD_TEXT);
    DECLARE_CONST_UNICODE_STRING(Location, INTELDSP_CHILD_LOCATION);
    DECLARE_UNICODE_STRING_SIZE(DeviceId, 64);
    DECLARE_UNICODE_STRING_SIZE(CompatId, 32);
    DECLARE_UNICODE_STRING_SIZE(InstanceId, 16);
    WDF_QUERY_INTERFACE_CONFIG QueryConfig;
    WDF_DEVICE_PNP_CAPABILITIES PnpCaps;
    WDF_OBJECT_ATTRIBUTES Attributes;
    INTELDSP_AUDIO_INTERFACE Interface;
    INTCDSP_AUDIO_PCM State;
    WDFDEVICE Child;
    PINTCDSP_AUDIO_PDO Pdo;
    NTSTATUS Status;

    PAGED_CODE();
    if (Dev->TopologyModel == NULL || Description->Pcm >= Dev->TopologyModel->PcmCount)
        return STATUS_OBJECT_NAME_NOT_FOUND;
    IntcDspAudioPcmFill(Dev->TopologyModel, Description->Pcm, &State);
    if (State.Endpoints == 0)
        return STATUS_OBJECT_NAME_NOT_FOUND;

    WdfDeviceInitSetDeviceType(ChildInit, FILE_DEVICE_SOUND);
    Status = RtlUnicodeStringPrintf(&DeviceId, INTELDSP_CHILD_DEVICE_ID, (ULONG)Dev->VenId, (ULONG)Dev->DevId);
    if (!NT_SUCCESS(Status))
        return Status;
    Status = WdfPdoInitAssignDeviceID(ChildInit, &DeviceId);
    if (!NT_SUCCESS(Status))
        return Status;
    Status = WdfPdoInitAddHardwareID(ChildInit, &DeviceId);
    if (!NT_SUCCESS(Status))
        return Status;
    Status = RtlUnicodeStringPrintf(&CompatId, INTELDSP_CHILD_COMPAT_ID);
    if (NT_SUCCESS(Status))
        Status = WdfPdoInitAddCompatibleID(ChildInit, &CompatId);
    if (!NT_SUCCESS(Status))
        return Status;
    Status = RtlUnicodeStringPrintf(&InstanceId, INTELDSP_CHILD_INSTANCE_ID, Description->Pcm);
    if (NT_SUCCESS(Status))
        Status = WdfPdoInitAssignInstanceID(ChildInit, &InstanceId);
    if (!NT_SUCCESS(Status))
        return Status;
    Status = WdfPdoInitAddDeviceText(ChildInit, &Text, &Location, MAKELANGID(LANG_ENGLISH, SUBLANG_ENGLISH_US));
    if (!NT_SUCCESS(Status))
        return Status;
    WdfPdoInitSetDefaultLocale(ChildInit, MAKELANGID(LANG_ENGLISH, SUBLANG_ENGLISH_US));

    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&Attributes, INTCDSP_AUDIO_PDO);
    Status = WdfDeviceCreate(&ChildInit, &Attributes, &Child);
    if (!NT_SUCCESS(Status))
        return Status;
    Pdo = IntcDspAudioPdoGetData(Child);
    Pdo->Dev = Dev;
    Pdo->Pcm = Description->Pcm;
    Pdo->PcmState = State;

    /* a child goes when the topology that named it does */
    WDF_DEVICE_PNP_CAPABILITIES_INIT(&PnpCaps);
    PnpCaps.Removable = WdfTrue;
    PnpCaps.SurpriseRemovalOK = WdfTrue;
    PnpCaps.Address = Description->Pcm;
    PnpCaps.UINumber = Description->Pcm;
    WdfDeviceSetPnpCapabilities(Child, &PnpCaps);

    IntcDspAudioInterfaceFill(Pdo, &Interface);
    WDF_QUERY_INTERFACE_CONFIG_INIT(&QueryConfig, (PINTERFACE)&Interface, &GUID_INTELDSP_AUDIO_INTERFACE, NULL);
    Status = WdfDeviceAddQueryInterface(Child, &QueryConfig);
    if (!NT_SUCCESS(Status))
        return Status;
    IntcDspLog("audio child '%wZ\\%wZ' for pcm '%s': %u endpoint%s\n", &DeviceId, &InstanceId, State.Endpoint[0].Name, State.Endpoints,
               State.Endpoints == 1 ? "" : "s");
    return STATUS_SUCCESS;
}

VOID
IntcDspAudioInitDevice(
    _In_ PWDFDEVICE_INIT DeviceInit)
{
    WDF_CHILD_LIST_CONFIG Config;

    /* the tree's pattern (drivers/wdm/audio/hdaudbus/fdo.cpp): the default child list, a description per child and the callback that creates its device object */
    WDF_CHILD_LIST_CONFIG_INIT(&Config, sizeof(INTCDSP_AUDIO_DESCRIPTION), IntcDspAudioEvtChildCreate);
    Config.EvtChildListIdentificationDescriptionCompare = IntcDspAudioEvtChildCompare;
    WdfFdoInitSetDefaultChildListConfig(DeviceInit, &Config, WDF_NO_OBJECT_ATTRIBUTES);
}

VOID
IntcDspAudioUpdate(
    _Inout_ PINTCDSP_DEVICE Dev)
{
    /* children while a topology model is resident, powered or not (E1, D2's guarantee): the PCM child stays through a fault's power-down and a D0 exit, since the next open boots the DSP again; none without a model, as the selection decides */
    PSOFTPLG_IPC4_MODEL Model = Dev->TopologyModel;
    BOOLEAN Resident = Model != NULL;
    WDFCHILDLIST List = WdfFdoGetDefaultChildList(Dev->Device);
    INTCDSP_AUDIO_DESCRIPTION Description;
    const SOFTPLG_IPC4_PCM* Pcm;
    UINT32 P;

    if (Dev->AudioReportedGeneration == Dev->TopologyGeneration && Dev->AudioReportedResident == Resident)
        return;
    WdfChildListBeginScan(List);
    for (P = 0; Model != NULL && P < Model->PcmCount; P++)
    {
        Pcm = &Model->Pcms[P];
        if ((Pcm->PlaybackCopier == SOFTPLG_NO_WIDGET || Pcm->PlaybackCopier >= Model->WidgetCount) &&
            (Pcm->CaptureCopier == SOFTPLG_NO_WIDGET || Pcm->CaptureCopier >= Model->WidgetCount))
            continue;
        RtlZeroMemory(&Description, sizeof(Description));
        WDF_CHILD_IDENTIFICATION_DESCRIPTION_HEADER_INIT(&Description.Header, sizeof(Description));
        Description.Pcm = P;
        if (!NT_SUCCESS(WdfChildListAddOrUpdateChildDescriptionAsPresent(List, &Description.Header, NULL)))
            IntcDspLog("audio child for pcm '%s': not reported to the system\n", Pcm->Name);
    }
    WdfChildListEndScan(List);
    Dev->AudioReportedGeneration = Dev->TopologyGeneration;
    Dev->AudioReportedResident = Resident;
}
