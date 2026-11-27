/*
 * PROJECT:     LiberNT host-native tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     A fake hdaudbus DSP child that answers the ADSP bus interface query the way the framework's query handler does, in version 1 or 2 and with the 64-bit DMA capability the case asks for; from version 2's stream members it keeps a pool of render and capture streams with their tags, buffers, states and period events, and a log of what the driver did to them
 * COPYRIGHT:   Copyright 2026 LiberNT contributors
 */

#ifndef FAKE_BUS_H
#define FAKE_BUS_H

#include <adspbus.h>

/* The stream pools of the fake: the tags each direction hands out, 1 upward; a test narrows them to exhaust one */
#define FAKE_BUS_STREAMS            8
/* Period events one fake stream holds */
#define FAKE_BUS_EVENTS             4
/* What the driver did to the fake's streams, oldest first, until the log is full */
#define FAKE_BUS_EVENTS_LOG         128

#define FAKE_BUS_EVENT_OPEN         1
#define FAKE_BUS_EVENT_FREE         2
#define FAKE_BUS_EVENT_BUFFER       3
#define FAKE_BUS_EVENT_BUFFER_FREE  4
#define FAKE_BUS_EVENT_RUN          5
#define FAKE_BUS_EVENT_STOP         6
#define FAKE_BUS_EVENT_RESET        7
#define FAKE_BUS_EVENT_SPIB         8
#define FAKE_BUS_EVENT_SPIB_OFF     9
#define FAKE_BUS_EVENT_REGISTER     10
#define FAKE_BUS_EVENT_UNREGISTER   11
#define FAKE_BUS_EVENT_POWER        12
#define FAKE_BUS_EVENT_BUFFER_DETACH  13  /* the buffer taken off the stream, kept for its pin */
#define FAKE_BUS_EVENT_BUFFER_FREE_UNDER_PIN  14  /* a free of a buffer a pin still holds: refused, nothing freed */
#define FAKE_BUS_EVENT_BUFFER_HELD_FREE  15  /* the pages of a held buffer returned by its pin's free */
#define FAKE_BUS_EVENT_LAST          FAKE_BUS_EVENT_BUFFER_HELD_FREE

/* The buffers a fake may have handed out and not seen returned: one per stream slot of eight boots, more than the manager's table of held buffers (four boots), so that a case can fill that table (R5-01's held for nobody) */
#define FAKE_BUS_MDLS                (FAKE_BUS_STREAMS * 2 * 8)

/* An MDL the fake handed out, with its pages and the pins that hold it: kept after the tag went back, which the stream slot is not (R5-01) */
typedef struct _FAKE_BUS_MDL
{
    PMDL Mdl;
    PVOID Memory;
    SIZE_T Bytes;
    UINT32 Pins;
    /* detached from the stream: the slot no longer names it, the pin still does */
    BOOLEAN Detached;
} FAKE_BUS_MDL, *PFAKE_BUS_MDL;

typedef struct _FAKE_BUS_EVENT
{
    UINT8 Kind;
    UINT8 Capture;
    UINT8 Tag;
    UCHAR Reserved;
    UINT32 Value;
} FAKE_BUS_EVENT, *PFAKE_BUS_EVENT;

typedef struct _FAKE_BUS_STREAM
{
    BOOLEAN Used;
    BOOLEAN Capture;
    BOOLEAN Running;
    BOOLEAN Buffer;
    BOOLEAN Spib;
    UINT8 Tag;
    /* the last state the driver set, by hdaudio.h HDAUDIO_STREAM_STATE */
    UINT8 State;
    ULONG FifoSize;
    ULONG Runs;
    ULONG Stops;
    HDAUDIO_STREAM_FORMAT Format;
    /* the pages of the buffer, and their count; the MDL beside them is opaque to the driver's stream manager */
    PVOID Memory;
    PMDL Mdl;
    SIZE_T Bytes;
    SIZE_T Offset;
    UINT32 SpibValue;
    /* the registers the driver is handed the address of: the position buffer's entry, the extended position register, the wall clock (ULONG, as the bus hands them out) */
    ULONG Position;
    ULONG DmaPosition;
    ULONG WallClock;
    PKEVENT Events[FAKE_BUS_EVENTS];
    UINT32 Registrations;
} FAKE_BUS_STREAM, *PFAKE_BUS_STREAM;

typedef struct _FAKE_BUS
{
    ADSP_BUS_INTERFACE Interface;
    ADSP_PLATFORM_INFO Info;
    /* what the capability query answers besides the information */
    NTSTATUS InfoStatus;
    UINT32 InfoCalls;
    /* the stream pools: how many streams each direction has (the tags 1 to that number), and the status the allocation answers instead of a stream when it is not success */
    UINT32 RenderStreams;
    UINT32 CaptureStreams;
    NTSTATUS RenderStatus;
    NTSTATUS CaptureStatus;
    /* the buffer allocation answers this when it is not success */
    NTSTATUS BufferStatus;
    UINT32 FifoSize;
    FAKE_BUS_STREAM Stream[2][FAKE_BUS_STREAMS];
    FAKE_BUS_MDL Mdls[FAKE_BUS_MDLS];
    UINT32 MdlCount;
    FAKE_BUS_EVENT Log[FAKE_BUS_EVENTS_LOG];
    UINT32 LogCount;
    /* how often each kind of event came, whether or not the log still holds it */
    UINT32 Counts[FAKE_BUS_EVENT_LAST + 1];
    UINT32 PowerStates;
    DEVICE_POWER_STATE LastPowerState;
} FAKE_BUS, *PFAKE_BUS;

/* A bus of the given version of the interface; version 2 reports the DMA capability Dma64Bit and, when the size says so, the stream members; every pool has FAKE_BUS_STREAMS streams */
VOID
FakeBusInit(
    _Out_ PFAKE_BUS Bus,
    ULONG Version,
    BOOLEAN Dma64Bit);

/* The query as the framework's handler decides it: the bus's structure and version may not exceed what the caller asks for, and the bus's own size in bytes is what the caller receives */
NTSTATUS
FakeBusQueryInterface(
    _In_ PFAKE_BUS Bus,
    _Out_writes_bytes_(Size) PVOID Interface,
    ULONG Size,
    ULONG Version);

/* A bus whose structure ends after the given member: the driver binds by size, so a bus that reports through one member and not the next is what a driver built later meets */
VOID
FakeBusTruncate(
    _Inout_ PFAKE_BUS Bus,
    SIZE_T Size);

/* The fake stream behind a handle, NULL for any other pointer */
PFAKE_BUS_STREAM
FakeBusStream(
    _In_ const FAKE_BUS* Bus,
    HANDLE Handle);

/* The fake stream behind a handle while the driver holds it (taken and not freed), NULL otherwise */
PFAKE_BUS_STREAM
FakeBusOwned(
    _In_ PFAKE_BUS Bus,
    HANDLE Handle);

/* A period of the stream: sets every event the driver registered on it, as the bus's interrupt DPC would */
VOID
FakeBusSignal(
    _Inout_ PFAKE_BUS Bus,
    HANDLE Handle);

/* How often the log holds an event of the kind */
UINT32
FakeBusCount(
    _In_ const FAKE_BUS* Bus,
    UINT32 Kind);

/* The first event of the kind at or after Index, or NULL */
const FAKE_BUS_EVENT*
FakeBusFind(
    _In_ const FAKE_BUS* Bus,
    UINT32 Kind,
    UINT32 Index);

/* A pin takes a buffer the fake handed out: the test's record of the owner, which may still map the pages (R5-01); a free of the buffer while a pin holds it is refused and logged */
VOID
FakeBusPinOpen(
    _Inout_ PFAKE_BUS Bus,
    PMDL Mdl);

/* The pin unmaps the buffer and lets it go before its free, as the wave pin does: a free of the stream's buffer after this is the owner's own */
VOID
FakeBusPinClose(
    _Inout_ PFAKE_BUS Bus,
    PMDL Mdl);

/* The pages behind an MDL the fake handed out and nobody returned, NULL for any other: the pin's probe through its MDL reads them, and NULL says the pages are gone */
PVOID
FakeBusMdlMemory(
    _In_ const FAKE_BUS* Bus,
    PMDL Mdl);

/* How many pins hold the buffer, 0 for an MDL the fake does not know */
UINT32
FakeBusMdlPins(
    _In_ const FAKE_BUS* Bus,
    PMDL Mdl);

/* How many buffers the fake handed out and has not seen returned */
UINT32
FakeBusMdlCount(
    _In_ const FAKE_BUS* Bus);

#ifndef FAKE_BUS_KERNEL
/*
 * The host's event waits of the stream manager's period wait:
 * KeClearEvent and KeWaitForSingleObject are defined in fake_bus.c for the
 * host, and a wait is answered by the test's model of the bus's interrupt
 * and of time through this hook, with its context; with no hook, every
 * wait ends at once in STATUS_TIMEOUT.
 */
typedef NTSTATUS (*FAKE_BUS_WAIT_HOOK)(_In_opt_ PVOID Context, _Inout_ PKEVENT Event, _In_opt_ PLARGE_INTEGER Timeout);
extern FAKE_BUS_WAIT_HOOK FakeBusWaitHook;
extern PVOID FakeBusWaitContext;
/*
 * The stream manager's lock on the host, which runs one thread: a wait
 * without a timeout (the manager's mutex, intcdsp/stream.h) takes it and
 * KeReleaseMutex gives it back, the depth saying how often it is held; and
 * the IRQL the kernel would report, which a test raises to ask what a
 * transition does above PASSIVE_LEVEL.
 */
extern LONG FakeBusLockDepth;
extern KIRQL FakeBusIrql;
#endif

#endif
