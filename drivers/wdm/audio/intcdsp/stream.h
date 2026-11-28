/*
 * PROJECT:     LiberNT Intel Smart Sound DSP driver
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     The stream manager: one stream object per open render or capture stream of a topology's PCM, the firmware messages of each transition in one order, the stream's buffer and state through the bus, and a release on every way out; free of I/O, so that the host tests and the kernel test run the driver's own code
 * COPYRIGHT:   Copyright 2026 LiberNT contributors
 */

/*
 * No structure or constant of an ABI is declared here: the messages are
 * built by sofipc4.h, the copier configurations by ipc4model.h, the bus
 * calls are adspbus.h, and each of those names its studied source. What
 * this file fixes is behaviour, and the behaviour is the plan's (p3-plan.md
 * §5.8, §5.9, §5.10), which cites Linux v6.12 sound/soc/sof for it:
 *
 *  - the order of a transition: the pipeline is paused and running before
 *    the host stream runs, and paused after the stream stopped (§5.10;
 *    Linux ipc4-pcm.c starts the pipelines first);
 *  - a pipeline is created once, its widgets initialized in the order the
 *    topology lists them and every edge between them bound; a host copier's
 *    configuration names its stream by the tag minus one (§5.6, §5.10);
 *  - the cyclic buffer: pages and descriptors through the bus, the stream
 *    set up and stopped, the software position in bounds (SPIB) at the
 *    buffer size so that the whole cycle is valid (§5.9).
 *
 * What this file chooses, as the plan leaves it open, says so where it
 * stands: the instance ids, the path a PCM's streams share, and the one
 * message that names a stream after its copier was initialized.
 */

#ifndef _DRIVERS_INTCDSP_STREAM_H_
#define _DRIVERS_INTCDSP_STREAM_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <ntddk.h>
#include <wdm.h>
#include <hdaudio.h>
#include <sofipc.h>
#include <ipc4model.h>
#include "adspbus.h"

/* The stream objects of a device: one per open stream, taken and returned by index */
#define INTCDSP_STREAMS_MAX            16
/* The module ids the manager keeps an instance count for: the ids of a firmware image's module table are its indices, fewer than this (intcdsp.h INTCDSP_MODULE_MAX); a model naming one at or beyond it is refused (IntcDspStreamsModuleRange) */
#define INTCDSP_STREAM_MODULES_MAX    256
/* The low bits of a stream handle name its object, the index and one, and the bits above them carry the generation the stream was opened in (INTCDSP_STREAMS.Generation) */
#define INTCDSP_STREAM_INDEX_BITS      8
#define INTCDSP_STREAM_INDEX_MASK      0xFFu
/* The pipelines one PCM's stream may reach: more is refused at the open */
#define INTCDSP_PCM_PIPELINES_MAX      8
/* The parameter id of the large-configuration set that names a host copier's stream once the copier exists: INFO_NEEDED 11 of the model notes (the simulator's qtest sends 0; the firmware's own value is silicon's to say) */
#define INTCDSP_COPIER_STREAM_PARAM    0
/* What the manager did, newest last, until the ring is full: the messages it sent and the bus calls it made */
#define INTCDSP_TRACE_MAX              96

/* The kinds of a trace line; Primary, Extension and Value say what the kind needs */
#define INTCDSP_TRACE_IPC              1   /* a request as sent, the busy bit clear; Value: the firmware's status, or the transport's outcome when no reply came */
#define INTCDSP_TRACE_OPEN             2   /* the bus gave a stream its tag; Value: the sampling rate */
#define INTCDSP_TRACE_FREE             3   /* the tag went back to the pool */
#define INTCDSP_TRACE_BUFFER           4   /* the buffer; Value: its bytes */
#define INTCDSP_TRACE_BUFFER_FREE      5
#define INTCDSP_TRACE_BUFFER_HELD      11  /* the buffer detached and held for its pin; Value: its bytes */
#define INTCDSP_TRACE_RUN              6   /* the stream runs */
#define INTCDSP_TRACE_STOP             7   /* the stream is paused or stopped */
#define INTCDSP_TRACE_RESET            8
#define INTCDSP_TRACE_SPIB             9   /* SPIB at the buffer size; Value: the size */
#define INTCDSP_TRACE_SPIB_OFF         10

typedef struct _INTCDSP_TRACE
{
    UINT8 Kind;
    UINT8 Capture;
    UINT8 Tag;
    UCHAR Reserved;
    UINT32 Primary;
    UINT32 Extension;
    UINT32 Value;
} INTCDSP_TRACE, *PINTCDSP_TRACE;

typedef enum _INTCDSP_STREAM_STATE
{
    /* the object is not in use */
    IntcDspStreamStateFree = 0,
    /* a tag is held; no buffer */
    IntcDspStreamStateOpen,
    /* the buffer is allocated and the firmware holds the pipeline */
    IntcDspStreamStatePrepared,
    IntcDspStreamStateRunning,
    /* stopped, the buffer and the pipeline kept */
    IntcDspStreamStatePaused,
    /*
     * The stream's teardown is pending (N7-01): its pin's free was answered
     * while the transport was busy, so the firmware-side release could not
     * complete. The buffer is detached from the bus's stream and held for
     * nobody under the handle's word (INTCDSP_HELD), to be returned at the
     * drain; the tag, the hold on the PCM and the copier's naming stay as the
     * state records them. The drain (IntcDspStreamsDrain, at the next
     * transition, or IntcDspStreamsLetGo at a release) finishes it.
     */
    IntcDspStreamStateTeardown,
    /*
     * The object holds nothing, no tag and no buffer, and waits for its
     * close: after a fault cost the firmware its objects and the bus its
     * streams, or after a pending teardown was drained while its pin still
     * lives (N7-01). A stream the device let go (a fault's or the
     * power-down's release) carries Lost, and its pin is answered
     * STATUS_DEVICE_REMOVED until its close (E1, D4); one released by the
     * drain of its own pending teardown is answered as before.
     */
    IntcDspStreamStateReleased
} INTCDSP_STREAM_STATE;

/* One open stream of a PCM: a render stream serves the PCM's playback copier, a capture stream its capture copier */
typedef struct _INTCDSP_STREAM
{
    INTCDSP_STREAM_STATE State;
    /* the generation of the manager the stream was opened in, which its handle carries too */
    UINT32 Generation;
    BOOLEAN Capture;
    UINT8 Tag;
    UINT32 Pcm;
    UINT32 Copier;
    /* the index of the host copier's format the stream carries: of its input formats for render, of its output formats for capture */
    UINT32 Format;
    HDAUDIO_STREAM_FORMAT StreamFormat;
    HANDLE Handle;
    ULONG FifoSize;
    PMDL Mdl;
    SIZE_T Bytes;
    SIZE_T OffsetFromFirstPage;
    /* the period events the driver registered through the manager */
    UINT32 Events;
    /* its pin closed it while its teardown was still pending (N7-01): the drain frees the object instead of leaving it for the close */
    BOOLEAN Orphaned;
    /* the device let the stream go, a fault's or the power-down's release (E1, D4): its pin is answered STATUS_DEVICE_REMOVED at every call but its buffer's free and its close; cleared at the open that takes the object */
    BOOLEAN Lost;
} INTCDSP_STREAM, *PINTCDSP_STREAM;

/*
 * The streams of one PCM share what they tell the firmware: a PCM whose
 * playback copier and capture copier sit in one pipeline (the loopback)
 * would else create it twice, and a copier cannot be initialized with the
 * tag of a stream that is not open yet. So the PCM's pipelines and widgets
 * are built by the first stream that allocates, held while any does, and
 * deleted when the last frees; a host copier initialized before its stream
 * was open gets the placeholder index, and the stream is named by one
 * large-configuration set when it allocates (the model's row 24).
 */
typedef struct _INTCDSP_PATH
{
    /* streams open on the PCM, streams holding its firmware objects, streams running */
    UINT32 Open;
    UINT32 Held;
    UINT32 Running;
    BOOLEAN Built;
    /* the firmware holds what the state still records of a path whose teardown a busy transport cut short: the next transition of the PCM finishes it before anything is built or named on it (FA-03) */
    BOOLEAN Undo;
} INTCDSP_PATH, *PINTCDSP_PATH;

/* The flags of a widget of a built path */
#define INTCDSP_WIDGET_INSTANCE       0x01  /* initialized; WidgetInstance names its instance id */
#define INTCDSP_WIDGET_NAMED          0x02  /* a host copier whose gateway names a stream */

typedef struct _INTCDSP_STREAMS_OPS
{
    _In_opt_ PVOID Context;
    /* one line of the driver's log, the arguments as a va_list so that the manager's line is printed once */
    VOID (*Log)(_In_opt_ PVOID Context, _In_z_ _Printf_format_string_ const char* Format, va_list Arguments);
    /*
     * A request faulted the link: the driver powers the DSP down, so every
     * object the firmware held is gone with the pipelines. The manager has
     * released its streams' buffers and tags, and released the stream
     * objects, when it calls this.
     */
    VOID (*Fault)(_In_opt_ PVOID Context);
    /*
     * The boot at an open (E1): the manager is not ready, or a request
     * faulted it, and the open boots the DSP and starts the manager again
     * through this, under the manager's lock, which is recursive: the boot
     * sequence's own start of the manager (IntcDspStreamsInit) runs inside
     * the open. Called once per open that finds the manager not ready; the
     * status is the boot's, and the open then asks the manager again: ready
     * and not faulted, the open goes on; otherwise the open is refused with
     * STATUS_DEVICE_NOT_READY and the manager stays in need of a boot. NULL:
     * no boot at an open, the open refused as before (STATUS_DEVICE_NOT_READY).
     */
    NTSTATUS (*Boot)(_In_opt_ PVOID Context);
    /*
     * A buffer goes back to the bus, or is held for its pin: whatever the
     * driver mapped of it is unmapped here, before the bus frees its pages
     * and before the buffer is held (R5-01). The self-test maps the pages it
     * writes and reads; the stream manager calls this for every buffer it
     * releases or holds, on the way out of a transition, on a fault and on a
     * power-down alike.
     */
    VOID (*Unmap)(_In_opt_ PVOID Context, _In_ PMDL Mdl);
    /*
     * The idle timer of the device (E2): armed for DueUs, the manager's
     * clock (IntcDspNow), by the close that leaves no stream allocated
     * when the setting asks (INTCDSP_STREAMS.IdleSec), and cancelled by
     * the open that takes a stream. The driver's timer queues a work item
     * when it fires and nothing else; the work item calls
     * IntcDspStreamsIdle, which decides again under the lock. NULL: no
     * idle timer, and the DSP stays up while the device is D0.
     */
    VOID (*IdleArm)(_In_opt_ PVOID Context, UINT64 DueUs);
    VOID (*IdleCancel)(_In_opt_ PVOID Context);
    /*
     * The idle power-down (E2): under the lock, with no stream allocated
     * and the idle time passed since the last close, the manager logged its
     * line and calls this; the driver powers the DSP down as a D0 exit does,
     * the PCM child kept, and the device is in need of a boot. NULL: the
     * decision is made and logged, and nothing is powered down.
     */
    VOID (*IdleDown)(_In_opt_ PVOID Context);
    /*
     * The driver's clock, in microseconds (E2): the manager's before the
     * first boot attaches the transport, whose clock the self-test's waits
     * and a booted manager's lines read (IntcDspNow takes this one first).
     * With it the line of the first boot at an open, on a device whose D0
     * entries never booted, carries its time. NULL: the transport's clock.
     */
    UINT64 (*NowUs)(_In_opt_ PVOID Context);
} INTCDSP_STREAMS_OPS, *PINTCDSP_STREAMS_OPS;

/*
     * A stream's buffer held for its pin (R5-01): the manager let the stream
     * go (a power-down, a fault, a transition that forced the release) while
     * the pin that owns the buffer still maps it, so the buffer was detached
     * from the bus's stream, not freed, and is kept under the full word of the
     * owner's handle until the pin's free arrives (IntcDspStreamFree), which
     * returns the pages and the MDL as the bus's free would. The tag went
     * back at once: no tag is kept across a suspend.
     */
typedef struct _INTCDSP_HELD
{
    /* the owner's handle word as it was: the generation above the index, so a handle of another boot names nothing */
    UINT_PTR Owner;
    PMDL Mdl;
    SIZE_T Bytes;
    UINT8 Tag;
    BOOLEAN Capture;
} INTCDSP_HELD, *PINTCDSP_HELD;

/* A PCM's level for its gain module (E3, stream.c IntcDspStreamsGainSet): zeroed is unity */
typedef struct _INTCDSP_GAIN
{
    /* in 1/65536 dB from unity down, the level the node keeps */
    LONG Level;
    BOOLEAN Mute;
    /* a change no gain instance was told: the PCM's next path build sends it */
    BOOLEAN Kept;
} INTCDSP_GAIN, *PINTCDSP_GAIN;

#define INTCDSP_GAIN_PCMS_MAX            16

/* The buffers a manager may hold for their pins at once: one per stream object of four boots, more than a machine's pins outlive */
#define INTCDSP_HELD_MAX              (INTCDSP_STREAMS_MAX * 4)

typedef struct _INTCDSP_STREAMS
{
    /*
     * The lock of the transitions: a kernel mutex, taken by each public
     * transition of the manager at PASSIVE_LEVEL before anything of a stream
     * is read, and released on its every way out. It is made once for a
     * manager and kept through its re-initializations, since a stale
     * handle's call may be leaving it as the device comes back. The order
     * is this lock, then the transport's request lock inside a request
     * (ipclock.c): nothing takes them the other way round, and the
     * transport's lock is never held while this one is waited for. The
     * register reads (the positions and the clock) take no lock: they may
     * run at DISPATCH_LEVEL, and the bus refuses a handle that went back to
     * its pool.
     */
    KMUTEX Lock;
    /*
     * The generation: counted up when the manager is initialized and when
     * it releases its streams for a power-down. A stream object carries the
     * generation it was opened in, and so does its handle; a handle of
     * another generation names nothing (IntcDspStreamOf), so a stale pin
     * acts neither on the stream that took its object nor on a free one.
     */
    UINT32 Generation;
    /*
     * The buffers held for their pins (INTCDSP_HELD), and how many. They sit
     * before Ops on purpose: the initialization zeroes the manager from Ops
     * on, and the generation moves at a power-down, while a held buffer
     * outlives both, since its pin frees it by the word it was given. The
     * driver's device extension holds the manager by value, so nothing is
     * allocated to hold a buffer at a power-down (R5-01).
     */
    INTCDSP_HELD Held[INTCDSP_HELD_MAX];
    UINT32 HeldCount;
    /*
     * The device's own boot sequence is in progress, the D0 entry's
     * (intcdsp.c IntcDspBootSequence): set and cleared under the lock
     * before and after the sequence (IntcDspStreamsDeviceBootBegin and
     * IntcDspStreamsDeviceBootEnd), which takes the lock itself at its
     * start of the manager. While it is set the Boot operation of an open
     * is not run (IntcDspStreamOpen): two boots of one device never run at
     * the same time, and the open, which holds the lock, does not wait for
     * that boot. It sits before Ops as the held buffers do: the sequence's
     * own initialization zeroes the manager from Ops on and must not clear
     * it.
     */
    BOOLEAN DeviceBoot;
    /*
     * The level kept for the gain module of each PCM (E3): in 1/65536 dB
     * from unity down (0 with no mute is unity), the mute flag, and whether
     * a change found no gain instance to be sent to and waits for the path
     * build of the PCM's next stream. Kept before Ops like the held
     * buffers: the level is the user's and outlives every boot, a
     * power-down and a fault, and the gain widget of the PCM is initialized
     * at it. A PCM at or beyond INTCDSP_GAIN_PCMS_MAX keeps none.
     */
    INTCDSP_GAIN Gain[INTCDSP_GAIN_PCMS_MAX];
    const INTCDSP_STREAMS_OPS* Ops;
    const ADSP_BUS_INTERFACE* Bus;
    SOFIPC_CONTEXT* Ipc;
    const SOFTPLG_IPC4_MODEL* Model;
    /* initialized for a booted DSP and its model, and no request has faulted the link since */
    BOOLEAN Ready;
    BOOLEAN Faulted;
    /* the DSP is powered down with the topology model kept, after a fault or a power-down: the next open boots (E1); cleared by the initialization */
    BOOLEAN NeedsBoot;
    UINT32 Faults;
    /*
     * The idle power-down (E2): the setting's seconds, 0 = off; whether the
     * device's timer is armed, for IdleDueUs; the time of the close that
     * left no stream allocated; whether the device went down by idle, so
     * the next open's line is a boot's and not a recovery's; how many idle
     * power-downs the boot saw. The timer is armed only when the allocated
     * streams (IntcDspStreamsAllocated) drop to zero and cancelled when
     * one is taken; the init zeroes all of it with the rest from Ops on.
     */
    ULONG IdleSec;
    BOOLEAN IdleArmed;
    BOOLEAN IdleDown;
    UINT64 IdleDueUs;
    UINT64 LastCloseUs;
    UINT32 IdleDowns;
    /* what the manager told the firmware and the bus since its initialization, for the self-test's lines: the streams whose tags went back to the pool, the instances and the pipelines deleted */
    UINT32 Freed;
    UINT32 InstanceDeletes;
    UINT32 PipelineDeletes;
    /* the firmware's instance ids: pipelines in creation order from 1 (the plan's §5.5), a module's instances from 0, none reused before the next boot */
    UINT32 NextPipelineInstance;
    PUINT8 ModuleInstances;
    /* the firmware's instance id and the state it was told, per pipeline of the model */
    PUINT32 PipelineInstance;
    PUINT32 PipelineState;
    PUINT32 PipelineHeld;
    PUINT32 PipelineRunning;
    PINTCDSP_PATH Path;
    PUINT8 WidgetInstance;
    PUINT8 WidgetFlags;
    INTCDSP_STREAM Stream[INTCDSP_STREAMS_MAX];
    INTCDSP_TRACE Trace[INTCDSP_TRACE_MAX];
    UINT32 TraceCount;
} INTCDSP_STREAMS, *PINTCDSP_STREAMS;

/* The bytes of the state the manager keeps for a model, to be given to IntcDspStreamsInit as its buffer */
SIZE_T
IntcDspStreamsBytes(
    _In_ const SOFTPLG_IPC4_MODEL* Model);

/* How many buffers the manager holds for their pins (INTCDSP_HELD), read without the lock for the removal's one line (R5-01) */
UINT32
IntcDspStreamsHeld(
    _In_ const INTCDSP_STREAMS* Streams);

/*
 * The idle decision (E2), a pure function of what it is given: the DSP
 * goes down when no stream is allocated (IntcDspStreamsAllocated: a pin
 * that is open but not running keeps it up, P5-R5), the setting is on and
 * IdleSec seconds passed since the last close that left no stream, the
 * boundary included. The clock is the manager's, in microseconds.
 */
BOOLEAN
IntcDspIdleDue(
    UINT32 Allocated,
    UINT64 NowUs,
    UINT64 LastCloseUs,
    ULONG IdleSec);

/*
 * The streams allocated on the manager: the sum of the PCMs' open streams
 * (Open, Prepared, Running, Paused and Teardown, stream.h INTCDSP_PATH),
 * read under the lock.
 */
UINT32
IntcDspStreamsAllocated(
    _In_ const INTCDSP_STREAMS* Streams);

/*
 * The idle setting of a booted manager (E2): IdleSec seconds without a
 * stream, 0 = off, under the lock. Turning it off cancels an armed timer;
 * turning it on arms none until the next close that leaves no stream.
 */
VOID
IntcDspStreamsIdleSec(
    _Inout_ PINTCDSP_STREAMS Streams,
    ULONG IdleSec);

/*
 * The idle power-down's work item (E2): the device's timer fired and
 * queued the work item that calls this at PASSIVE_LEVEL. Under the lock the
 * timer's arming ends (a timer armed again for a later time is not this
 * one's), the pending teardowns are finished as every transition's prologue
 * does (N8-02), and the decision is made again with what is true now: an
 * open that arrived since the fire keeps the DSP up, and nothing is asked
 * of a manager that is not up or faulted. Due, one line says how long the
 * DSP was idle and the IdleDown operation powers it down, inside the lock,
 * so that no open takes a stream from a manager about to let it go.
 */
VOID
IntcDspStreamsIdle(
    _Inout_ PINTCDSP_STREAMS Streams);

/*
 * The pending teardowns finished with no transition of their own (N8-02,
 * the public poll): under the lock the prologue's drain, as every
 * transition runs it; how many stay pending is returned, a release that
 * met the busy transport again left as it was. A manager that is not up,
 * or faulted, is not asked, and a call at a raised IRQL drains nothing.
 */
UINT32
IntcDspStreamsTeardownPoll(
    _Inout_ PINTCDSP_STREAMS Streams);

/*
 * The gain module's word for a level (E3): a linear gain in Q1.31, unity
 * 0x7fffffff (sofipc4.h), from a level in 1/65536 dB from unity down -- the
 * whole decibels are a table and a level between two of them the straight
 * line between their words; 0 and above unity, -96 dB and below the
 * table's -96 dB, a muted level 0.
 */
UINT32
IntcDspGainOfLevel(
    LONG Level,
    BOOLEAN Mute);

/* The volume entry of a gain word as the messages carry it: every channel, no fade (sofipc4.h SOFIPC4_GAIN_CONFIG) */
VOID
IntcDspGainEntry(
    UINT32 Gain,
    _Out_ SOFIPC4_GAIN_CONFIG* Entry);

/*
 * A sample in its container as the firmware's volume component forwards it
 * under a Q1.31 gain: a 16-bit sample widened to 32 bits as a host copier
 * takes it, the product rounded and shifted, saturated, and back in the
 * container; unity is the sample itself. The self-test's expected capture.
 */
INT32
IntcDspGainApply(
    INT32 Sample,
    ULONG ContainerBits,
    ULONG ValidBits,
    UINT32 Gain);

/*
 * A PCM's level for its gain module (E3), under the lock: kept on the
 * manager whatever the DSP's state; sent at once to every gain widget the
 * PCM reaches that has an instance (the PCM's streams are allocated) with
 * the line `gain: <dB> dB, mute <n>`; with the DSP not up or no instance,
 * kept for the path build of the PCM's next stream, which initializes the
 * widget at the level, sends it and logs the line. The status of the
 * request that failed, the fault's release when the link was lost.
 */
_Must_inspect_result_
NTSTATUS
IntcDspStreamsGainSet(
    _Inout_ PINTCDSP_STREAMS Streams,
    UINT32 Pcm,
    LONG Level,
    BOOLEAN Mute);

/*
 * The first module widget of a model whose module id the manager cannot
 * count (INTCDSP_STREAM_MODULES_MAX), SOFTPLG_NO_WIDGET when every one's
 * is within; a widget without a module names any id. The topology's load
 * refuses such a model after its validation (R1-05), and the manager does
 * not start on one.
 */
UINT32
IntcDspStreamsModuleRange(
    _In_ const SOFTPLG_IPC4_MODEL* Model);

/*
 * The manager of a booted DSP: its model, the transport, the bus and the
 * operations; the buffer holds the per-model state and stays the manager's.
 * A manager zeroed by its caller gets its lock here; the driver makes the
 * lock at the device's creation, before any power transition can release
 * what a manager never started.
 */
VOID
IntcDspStreamsInit(
    _Out_ PINTCDSP_STREAMS Streams,
    _Out_writes_bytes_(Bytes) UINT8* Buffer,
    SIZE_T Bytes,
    _In_ const SOFTPLG_IPC4_MODEL* Model,
    _In_ SOFIPC_CONTEXT* Ipc,
    _In_ const ADSP_BUS_INTERFACE* Bus,
    _In_opt_ const INTCDSP_STREAMS_OPS* Ops);

/*
 * The power-down's release: every stream gives the bus what it gave it,
 * as after a fault (IntcDspStreamsFault's release, without the firmware
 * messages), the paths forget what they built, the manager is not ready
 * until its next initialization and needs a boot, and every handle is of
 * an older generation. The driver calls it before the DSP goes off. The
 * streams are not re-created at the next D0 entry: a pin that held one
 * finds its handle answered STATUS_DEVICE_REMOVED (E1, D4) for the rest
 * of its life, its buffer's free and its close excepted.
 */
VOID
IntcDspStreamsRelease(
    _Inout_ PINTCDSP_STREAMS Streams);

/*
 * The device's own boot sequence, begun and ended by the driver around
 * its D0 entry's boot (N10-04). Each takes the lock to set or clear the
 * flag and lets it go at once: the sequence runs unlocked and takes the
 * lock itself at the manager's start. The entry's begin waits, at the
 * lock, for an open that holds it (a Boot operation of an open included)
 * and holds nothing while its boot runs; an open that comes during that
 * boot finds the flag set and is refused without its own boot. The lock
 * keeps its one order (above).
 */
VOID
IntcDspStreamsDeviceBootBegin(
    _Inout_ PINTCDSP_STREAMS Streams);

VOID
IntcDspStreamsDeviceBootEnd(
    _Inout_ PINTCDSP_STREAMS Streams);

/*
 * The poll of an asynchronous exception (E1c): the firmware posted a crash
 * notification while no request was outstanding, which no wait would meet.
 * The work item the driver queues at a stream's period calls it at
 * PASSIVE_LEVEL. Under the lock, the transport's own poll reads what was
 * posted, behind a request in progress (it takes the request lock without
 * a wait and leaves the lock alone when a request holds it); a crash
 * notification faults the link there and the fault is the manager's as
 * after a request that failed: every stream Released and Lost, its pin
 * told, the driver told. A notification that is not a crash is kept, as
 * beside a reply. Nothing is sent and nothing is booted. A manager that is
 * not up, or whose link is not ready, is not asked.
 */
VOID
IntcDspStreamsPoll(
    _Inout_ PINTCDSP_STREAMS Streams);

/*
 * The stream object a handle names: NULL for a handle of another
 * generation, for a free object, and for any other value. A handle is the
 * manager's own word, never a pointer (INTCDSP_STREAM_INDEX_BITS).
 */
PINTCDSP_STREAM
IntcDspStreamOf(
    _In_ const INTCDSP_STREAMS* Streams,
    HANDLE Handle);

/*
 * Whether a handle is of an older generation (E1, D4): the word's
 * generation differs from the manager's and its index is in range. The pin
 * that holds it is answered STATUS_DEVICE_REMOVED, not STATUS_INVALID_HANDLE:
 * the device let its stream go, and the pin is told so. A word without an
 * object's index is no handle, stale or not; a closed handle's word is of
 * the manager's generation, and names nothing.
 */
BOOLEAN
IntcDspStreamStale(
    _In_ const INTCDSP_STREAMS* Streams,
    HANDLE Handle);

/* The PCM of a name, or SOFTPLG_NO_PCM */
UINT32
IntcDspStreamPcm(
    _In_ const INTCDSP_STREAMS* Streams,
    _In_z_ const char* Name);

/*
 * The transitions of the plan's §5.10, each PASSIVE_LEVEL and serialized
 * with every other transition by the manager's lock (the transport's lock
 * serializes the requests within them); a call at a raised IRQL is refused
 * with STATUS_INVALID_DEVICE_STATE, and a handle of another generation or
 * a stream the device let go (Lost) with STATUS_DEVICE_REMOVED (E1, D4),
 * before anything is read. Open: when the manager is not ready, or a
 * request faulted it, the Boot operation is called first, under the lock
 * (E1); without it, or when the boot leaves the manager not ready, the
 * open is refused with STATUS_DEVICE_NOT_READY. Then the format is
 * matched against the host copier's formats of that direction and the bus
 * gives a stream of its pool its tag. Allocate: the buffer, then the PCM's
 * pipelines created, its widgets initialized and bound, a copier that
 * waited for this stream named. Run: the pipelines paused and running,
 * then the stream. Pause: the stream, then the pipelines when no stream of
 * the PCM runs. Free: a copier that named the stream told so while another
 * stream holds the PCM, else the pipeline reset, the instances and the
 * pipeline deleted; the buffer. Close: the tag. Every transition first
 * drains the teardowns a busy transport left pending (N7-01,
 * IntcDspStreamStateTeardown): a free whose release met a busy transport
 * answers the pin with STATUS_SUCCESS, holds the buffer for the manager and
 * keeps the stream's tag and hold for the drain, which sends the release
 * again at the next transition, returns the buffer and the tag and closes
 * the stream; its pin's Close is accepted meanwhile, and a stream whose
 * pin closed it is freed by the drain.
 */
NTSTATUS
IntcDspStreamOpen(
    _Inout_ PINTCDSP_STREAMS Streams,
    UINT32 Pcm,
    BOOLEAN Capture,
    ULONG SampleRate,
    ULONG Channels,
    ULONG ValidBits,
    ULONG ContainerBits,
    _Out_ PHANDLE Stream);

NTSTATUS
IntcDspStreamAllocate(
    _Inout_ PINTCDSP_STREAMS Streams,
    HANDLE Stream,
    SIZE_T Bytes,
    ULONG NotificationCount);

NTSTATUS
IntcDspStreamRun(
    _Inout_ PINTCDSP_STREAMS Streams,
    HANDLE Stream);

NTSTATUS
IntcDspStreamPause(
    _Inout_ PINTCDSP_STREAMS Streams,
    HANDLE Stream);

NTSTATUS
IntcDspStreamFree(
    _Inout_ PINTCDSP_STREAMS Streams,
    HANDLE Stream);

NTSTATUS
IntcDspStreamClose(
    _Inout_ PINTCDSP_STREAMS Streams,
    HANDLE Stream);

/* Position and clock of a stream for a WaveRT endpoint: the registers are the bus's, read by the audio engine; the position buffer's entry is read here */
NTSTATUS
IntcDspStreamPositionRegister(
    _In_ const INTCDSP_STREAMS* Streams,
    HANDLE Stream,
    _Out_ PULONG* Position);

NTSTATUS
IntcDspStreamLinkPositionRegister(
    _In_ const INTCDSP_STREAMS* Streams,
    HANDLE Stream,
    _Out_ PULONG* Position);

NTSTATUS
IntcDspStreamClockRegister(
    _In_ const INTCDSP_STREAMS* Streams,
    _Out_ PULONG* Wallclock);

UINT32
IntcDspStreamPosition(
    _In_ const INTCDSP_STREAMS* Streams,
    HANDLE Stream);

NTSTATUS
IntcDspStreamRegisterEvent(
    _Inout_ PINTCDSP_STREAMS Streams,
    HANDLE Stream,
    _In_ PKEVENT NotificationEvent);

NTSTATUS
IntcDspStreamUnregisterEvent(
    _Inout_ PINTCDSP_STREAMS Streams,
    HANDLE Stream,
    _In_ PKEVENT NotificationEvent);

/*
 * The periods of a stream, waited for on the position buffer's entry:
 * the event is registered on the stream, the bus's interrupt DPC sets it
 * at every period, and each wake adds how far the entry moved since the
 * wake before, so that two periods that came between two wakes still
 * count, and the entry's wrap at the buffer's end is one step forward.
 * The wait ends with Count periods or when TimeoutMs passed, whichever
 * comes first; Periods tells how many whole periods came, at most Count.
 */
NTSTATUS
IntcDspStreamWaitPeriods(
    _Inout_ PINTCDSP_STREAMS Streams,
    HANDLE Stream,
    UINT32 Count,
    UINT32 TimeoutMs,
    _Inout_ PKEVENT Event,
    _Out_ PULONG Periods);

/* The frame size a stream format gives, in bytes */
ULONG
IntcDspStreamFrameBytes(
    ULONG Channels,
    ULONG ContainerBits);

/*
 * The loopback self-test of the plan's §8 C3 over the streams: a render
 * and a capture stream of the PCM named opened, both buffers allocated, a
 * 1 kHz sine written into the render buffer, the capture stream run first
 * and the render stream after it, the capture's periods waited for, both
 * stopped, the capture compared with the render from the first matching
 * frame, three lines per bit depth and a summary. Stops at the first run
 * that does not match, as the plan's stop rule says, and releases
 * everything on every way out.
 */
typedef struct _INTCDSP_SELFTEST_OPS
{
    _In_opt_ PVOID Context;
    /* the pages of an allocated buffer as the CPU sees them: the driver keeps what it mapped of each buffer until the manager's Unmap operation is called for it */
    PVOID (*Map)(_In_opt_ PVOID Context, _In_ PMDL Mdl, SIZE_T Bytes);
    /*
     * Waits for Count periods of the stream, up to TimeoutMs: an event is
     * registered on the stream, the bus's interrupt DPC sets it at every
     * period, and Periods tells how many came
     */
    NTSTATUS (*WaitPeriods)(_In_opt_ PVOID Context, HANDLE Stream, UINT32 Count, UINT32 TimeoutMs, _Out_ PULONG Periods);
} INTCDSP_SELFTEST_OPS, *PINTCDSP_SELFTEST_OPS;

/* The 1 kHz sine of the self-test at 48000 Hz: 48 frames a period, a channel's sample in the 31 highest bits of a 32-bit word */
#define INTCDSP_SELFTEST_RATE          48000
#define INTCDSP_SELFTEST_PERIOD        48
#define INTCDSP_SELFTEST_CHANNELS      2
/* The PCM the self-test runs on */
#define INTCDSP_SELFTEST_PCM           "Loopback"
/* the offsets in frames the comparison tries before it calls the capture unmatched */
#define INTCDSP_COMPARE_OFFSETS        1024

/* What the comparison found */
typedef struct _INTCDSP_COMPARE
{
    /* the offset that matched, else the one with the fewest differing samples: the capture frame this many frames into the window is the render buffer's first frame */
    UINT32 Offset;
    /* samples of all channels compared at Offset, and how many of them differ */
    UINT64 Samples;
    UINT64 Differing;
    /* the largest |capture - render| at Offset, and the first differing sample there */
    UINT64 MaxError;
    UINT32 DifferingFrame;
    UINT32 DifferingChannel;
    INT32 RenderSample;
    INT32 CaptureSample;
    /* the RMS of the render buffer and of the capture's compared frames, in thousandths of a sample unit */
    UINT64 RenderRms;
    UINT64 CaptureRms;
    BOOLEAN Matched;
} INTCDSP_COMPARE, *PINTCDSP_COMPARE;
/* The sine of one period, in Q31: the sample of frame N is IntcDspSelfTestSine()[N % INTCDSP_SELFTEST_PERIOD] */
const INT32*
IntcDspSelfTestSine(
    VOID);

/* Fills a buffer with the sine, interleaved, in the container the valid bits live in */
VOID
IntcDspSelfTestFill(
    _Out_writes_bytes_(Bytes) PUCHAR Buffer,
    SIZE_T Bytes,
    ULONG Channels,
    ULONG ValidBits,
    ULONG ContainerBits);

/*
 * The capture against the render. StopBytes is where the capture stream
 * stood when it was stopped: the capture buffer is read as the frames that
 * end there, so that a stream which ran past the last period before it
 * stopped is still one window. For every offset up to
 * INTCDSP_COMPARE_OFFSETS the window from that frame on is compared with
 * the render buffer from its first frame: the smallest offset without a
 * differing sample matches (the pipeline's latency), and with none the
 * one with the fewest is reported, with its first differing sample.
 */
VOID
IntcDspStreamCompare(
    _In_reads_bytes_(Bytes) const PUCHAR Render,
    _In_reads_bytes_(Bytes) const PUCHAR Capture,
    SIZE_T Bytes,
    ULONG Channels,
    ULONG ContainerBits,
    UINT32 StopBytes,
    _Out_ PINTCDSP_COMPARE Compare);

/*
 * The capture against the render as a Q1.31 gain forwards it (E3): every
 * sample of the capture must be the render's sample under the gain
 * (IntcDspGainApply) within one unit of the valid depth, the rounding of
 * the firmware's arithmetic; at unity the same compare exactly
 * (IntcDspStreamCompare). The rest as there.
 */
VOID
IntcDspStreamCompareGain(
    _In_reads_bytes_(Bytes) const PUCHAR Render,
    _In_reads_bytes_(Bytes) const PUCHAR Capture,
    SIZE_T Bytes,
    ULONG Channels,
    ULONG ContainerBits,
    ULONG ValidBits,
    UINT32 StopBytes,
    UINT32 Gain,
    _Out_ PINTCDSP_COMPARE Compare);

/* Runs the self-test at 16, 24 and 32 valid bits, Frames frames a buffer; with GainStep the three depths again at -6 dB, the PCM's level kept again for it at the end (SelfTest = 3) */
NTSTATUS
IntcDspLoopbackSelfTestAt(
    _Inout_ PINTCDSP_STREAMS Streams,
    _In_ const INTCDSP_SELFTEST_OPS* Ops,
    UINT32 Frames,
    BOOLEAN GainStep);

/*
 * Runs the self-test at 16, 24 and 32 valid bits, Frames frames a buffer.
 * STATUS_SUCCESS when every depth matched; the status of the step that
 * failed otherwise, with its line in the log. The lines are the plan's
 * VM-acceptance row's, one pair per depth, with a third line for the
 * positions and the release.
 */
NTSTATUS
IntcDspLoopbackSelfTest(
    _Inout_ PINTCDSP_STREAMS Streams,
    _In_ const INTCDSP_SELFTEST_OPS* Ops,
    UINT32 Frames);

#ifdef __cplusplus
}
#endif

#endif
