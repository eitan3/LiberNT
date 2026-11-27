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
    /* a faulted request cost the firmware its objects and the bus its streams; the object waits for its close */
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
     * A buffer goes back to the bus, or is held for its pin: whatever the
     * driver mapped of it is unmapped here, before the bus frees its pages
     * and before the buffer is held (R5-01). The self-test maps the pages it
     * writes and reads; the stream manager calls this for every buffer it
     * releases or holds, on the way out of a transition, on a fault and on a
     * power-down alike.
     */
    VOID (*Unmap)(_In_opt_ PVOID Context, _In_ PMDL Mdl);
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
    const INTCDSP_STREAMS_OPS* Ops;
    const ADSP_BUS_INTERFACE* Bus;
    SOFIPC_CONTEXT* Ipc;
    const SOFTPLG_IPC4_MODEL* Model;
    /* initialized for a booted DSP and its model, and no request has faulted the link since */
    BOOLEAN Ready;
    BOOLEAN Faulted;
    UINT32 Faults;
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
 * until its next initialization, and every handle is of an older generation.
 * The driver calls it before the DSP goes off. The streams are not
 * re-created at the next D0 entry: a pin that held one finds its handle
 * refused (STATUS_INVALID_HANDLE) for the rest of its life.
 */
VOID
IntcDspStreamsRelease(
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

/* The PCM of a name, or SOFTPLG_NO_PCM */
UINT32
IntcDspStreamPcm(
    _In_ const INTCDSP_STREAMS* Streams,
    _In_z_ const char* Name);

/*
 * The transitions of the plan's §5.10, each PASSIVE_LEVEL and serialized
 * with every other transition by the manager's lock (the transport's lock
 * serializes the requests within them); a call at a raised IRQL is refused
 * with STATUS_INVALID_DEVICE_STATE, and a handle of another generation with
 * STATUS_INVALID_HANDLE, before anything is read. Open: the format is
 * matched against the host copier's formats of that direction and the bus
 * gives a stream of its pool its tag. Allocate: the buffer, then the PCM's
 * pipelines created, its widgets initialized and bound, a copier that
 * waited for this stream named. Run: the pipelines paused and running,
 * then the stream. Pause: the stream, then the pipelines when no stream of
 * the PCM runs. Free: a copier that named the stream told so while another
 * stream holds the PCM, else the pipeline reset, the instances and the
 * pipeline deleted; the buffer. Close: the tag.
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
