/*
 * PROJECT:     LiberNT Intel Smart Sound DSP driver
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     The stream manager and the loopback self-test: one stream object per open render or capture stream of a topology's PCM, the firmware messages of each transition in one order, the stream's buffer and state through the bus, a release on every way out, the sine, the comparison and the lines of the self-test; free of I/O
 * COPYRIGHT:   Copyright 2026 LiberNT contributors
 */

/* The behaviour this code keeps, and where the plan cites it, is said in stream.h */

#include "stream.h"

static
VOID
IntcDspStreamLog(
    _In_ const INTCDSP_STREAMS* Streams,
    _In_z_ const char* Format,
    ...)
{
    va_list Arguments;

    if (Streams->Ops == NULL || Streams->Ops->Log == NULL)
        return;
    va_start(Arguments, Format);
    Streams->Ops->Log(Streams->Ops->Context, Format, Arguments);
    va_end(Arguments);
}

static
VOID
IntcDspTrace(
    _Inout_ PINTCDSP_STREAMS Streams,
    _In_opt_ const INTCDSP_STREAM* Stream,
    UINT32 Kind,
    UINT32 Primary,
    UINT32 Extension,
    UINT32 Value)
{
    PINTCDSP_TRACE Trace;

    if (Streams->TraceCount >= INTCDSP_TRACE_MAX)
        return;
    Trace = &Streams->Trace[Streams->TraceCount++];
    Trace->Kind = (UINT8)Kind;
    Trace->Capture = Stream ? Stream->Capture : 0;
    Trace->Tag = Stream ? Stream->Tag : 0;
    Trace->Reserved = 0;
    Trace->Primary = Primary;
    Trace->Extension = Extension;
    Trace->Value = Value;
}

/* The lock of the transitions (stream.h): taken at PASSIVE_LEVEL, where a mutex may be waited for; a caller that may run higher is refused before it */
static
VOID
IntcDspStreamsLock(
    _Inout_ PINTCDSP_STREAMS Streams)
{
    KeWaitForSingleObject(&Streams->Lock, Executive, KernelMode, FALSE, NULL);
}

static
VOID
IntcDspStreamsUnlock(
    _Inout_ PINTCDSP_STREAMS Streams)
{
    KeReleaseMutex(&Streams->Lock, FALSE);
}

/* The teardowns pending, drained before each transition's own work (N7-01); defined with the free */
static
VOID
IntcDspStreamsDrain(
    _Inout_ PINTCDSP_STREAMS Streams);

/* The idle timer of the device (E2), cancelled and armed by the manager under its lock; defined with the idle power-down */
static
VOID
IntcDspStreamsIdleCancelLocked(
    _Inout_ PINTCDSP_STREAMS Streams);

static
VOID
IntcDspStreamsIdleClosedLocked(
    _Inout_ PINTCDSP_STREAMS Streams);

/* The manager's clock, the loader's, for the open's boot (E1) and the self-test's waits; defined with the self-test */
static
BOOLEAN
IntcDspHasClock(
    _In_ const INTCDSP_STREAMS* Streams);

static
UINT64
IntcDspNow(
    _In_ const INTCDSP_STREAMS* Streams);

/* The status the driver answers for a transport outcome */
static
NTSTATUS
IntcDspIpcStatus(
    SOFIPC_STATUS Result)
{
    switch (Result)
    {
        case SofIpcTimeout:
            return STATUS_IO_TIMEOUT;
        case SofIpcBusy:
            return STATUS_RETRY;
        case SofIpcBadLength:
            return STATUS_INVALID_PARAMETER;
        case SofIpcBadState:
            return STATUS_DEVICE_NOT_READY;
        default:
            return STATUS_DEVICE_PROTOCOL_ERROR;
    }
}

/* Whether the link can still carry a request: every outcome but a refused payload and a busy caller faults it, and nothing is sent on a faulted link until the next boot */
static
BOOLEAN
IntcDspLinkLost(
    _In_ const INTCDSP_STREAMS* Streams)
{
    return Streams->Ipc->Link != SofIpcLinkReady && Streams->Ipc->Link != SofIpcLinkBusy;
}

/*
 * A notification the transport kept beside a reply (E1, INFO_NEEDED 2):
 * the firmware posted while a request was outstanding, which nothing
 * polls for at run time (SofIpcPollNotifications has no caller in the
 * driver). One line per request that saw any, with the newest one's type
 * from the transport's ring, beside the request's own line.
 */
static
VOID
IntcDspNoteNotification(
    _In_ const INTCDSP_STREAMS* Streams)
{
    const SOFIPC_CONTEXT* Ipc = Streams->Ipc;
    UINT32 Type = SOFIPC_NO_TYPE;

    if (Ipc->RingCount != 0)
        Type = Ipc->Ring[(Ipc->RingHead + SOFIPC_NOTIFICATION_RING - 1) % SOFIPC_NOTIFICATION_RING].Type;
    IntcDspStreamLog(Streams, "ipc: a notification of type 0x%08x beside the reply\n", Type);
}

static
BOOLEAN
IntcDspIsModule(
    const SOFTPLG_IPC4_WIDGET* Widget)
{
    return Widget->Kind != SoftplgKindNone && Widget->Kind != SoftplgKindScheduler;
}

static
BOOLEAN
IntcDspIsHostCopier(
    const SOFTPLG_IPC4_WIDGET* Widget)
{
    return Widget->Kind == SoftplgKindHostCopierPlayback || Widget->Kind == SoftplgKindHostCopierCapture;
}

static
BOOLEAN
IntcDspReached(
    _In_ const INTCDSP_STREAMS* Streams,
    UINT32 Pcm,
    UINT32 Widget)
{
    return SoftplgModelReachable(Streams->Model, Pcm, Widget);
}

static
const SOFTPLG_IPC4_WIDGET*
IntcDspScheduler(
    _In_ const INTCDSP_STREAMS* Streams,
    UINT32 Pipeline)
{
    return &Streams->Model->Widgets[Streams->Model->Pipelines[Pipeline].Scheduler];
}

/*
 * The pipelines a PCM reaches, in the order they are started in (the sinks
 * lead: the highest scheduler priority first) or paused in (the sources
 * lead); equal priorities keep the order of the topology. The count is
 * returned, 0xFFFFFFFF when the PCM reaches more than a stream may.
 */
static
UINT32
IntcDspPipelineOrder(
    _In_ const INTCDSP_STREAMS* Streams,
    UINT32 Pcm,
    BOOLEAN SinksFirst,
    _Out_writes_(INTCDSP_PCM_PIPELINES_MAX) UINT8* Order)
{
    const SOFTPLG_IPC4_PCM* Model = &Streams->Model->Pcms[Pcm];
    UINT32 Count = 0, Word, Bit, I, J, Best, Priority, BestPriority;
    UINT8 Swap;

    for (Word = 0; Word < SOFTPLG_PIPELINE_MASK_WORDS; Word++)
    {
        for (Bit = 0; Bit < 32; Bit++)
        {
            if ((Model->PipelineMask[Word] & (1u << Bit)) == 0)
                continue;
            if (Word * 32 + Bit >= Streams->Model->PipelineCount || Count == INTCDSP_PCM_PIPELINES_MAX)
                return 0xFFFFFFFF;
            Order[Count++] = (UINT8)(Word * 32 + Bit);
        }
    }
    for (I = 0; I < Count; I++)
    {
        Best = I;
        BestPriority = IntcDspScheduler(Streams, Order[Best])->Priority;
        for (J = I + 1; J < Count; J++)
        {
            Priority = IntcDspScheduler(Streams, Order[J])->Priority;
            if (Priority != BestPriority ? (SinksFirst ? Priority > BestPriority : Priority < BestPriority)
                                         : (SinksFirst ? Order[J] < Order[Best] : Order[J] > Order[Best]))
            {
                Best = J;
                BestPriority = Priority;
            }
        }
        Swap = Order[I];
        Order[I] = Order[Best];
        Order[Best] = Swap;
    }
    return Count;
}

/* One request through the transport; the trace keeps it as sent, with the firmware's status or the transport's outcome */
static
NTSTATUS
IntcDspSend(
    _Inout_ PINTCDSP_STREAMS Streams,
    _In_ const INTCDSP_STREAM* Stream,
    SOFIPC_CLASS Class,
    _In_ const SOFIPC4_MSG* Msg,
    _In_reads_bytes_opt_(Bytes) const UINT8* Payload,
    SIZE_T Bytes)
{
    SOFIPC_REQUEST Request;
    SOFIPC_STATUS Result;
    UINT32 Notifications;

    RtlZeroMemory(&Request, sizeof(Request));
    Request.Class = Class;
    Request.Msg = *Msg;
    Request.Payload = Payload;
    Request.PayloadBytes = Bytes;
    Notifications = Streams->Ipc->Notifications;
    Result = SofIpcRequest(Streams->Ipc, &Request);
    if (Streams->Ipc->Notifications != Notifications)
        IntcDspNoteNotification(Streams);
    IntcDspTrace(Streams, Stream, INTCDSP_TRACE_IPC, Msg->Primary, Msg->Extension,
                 Result == SofIpcOk ? Streams->Ipc->Last.Status : (UINT32)Result);
    if (Result == SofIpcOk)
        return STATUS_SUCCESS;
    IntcDspStreamLog(Streams, "ipc: %s (%u) 0x%08x|0x%08x %s after %u us; the link is %s\n", SofIpcTypeName(Msg->Primary),
                     SofIpc4MsgType(Msg), Msg->Primary | FWLOAD_IPC_BUSY, Msg->Extension, SofIpcStatusName(Result),
                     (UINT32)Streams->Ipc->Last.ElapsedUs, SofIpcLinkName(Streams->Ipc->Link));
    return IntcDspIpcStatus(Result);
}

/*
 * A host copier's configuration sent again as one large-configuration set:
 * the stream it names is Tag, or none (the placeholder index) with
 * SOFTPLG_STREAM_TAG_NONE. The copier exists in the firmware: it was
 * initialized before the stream was open, and it is named here; or its
 * stream is going while another stream still holds the PCM, and it is told
 * so here, so that the gateway never points at a stream the pool gave to
 * another driver.
 */
static
NTSTATUS
IntcDspNameStream(
    _Inout_ PINTCDSP_STREAMS Streams,
    _In_ const INTCDSP_STREAM* Stream,
    UINT32 Widget,
    UINT32 Tag)
{
    const SOFTPLG_IPC4_WIDGET* Copier = &Streams->Model->Widgets[Widget];
    SOFTPLG_IPC4_CONFIG Config;
    SOFIPC4_MSG Msg;
    SOFIPC_STATUS Result;
    UINT32 Bytes, Notifications, Flags = SOFIPC4_LARGE_CONFIG_INIT_BLOCK | SOFIPC4_LARGE_CONFIG_FINAL_BLOCK;

    if (SoftplgModelConfig(Streams->Model, Widget, Stream->Capture ? 0 : Stream->Format, Stream->Capture ? Stream->Format : 0,
                           Tag, &Config) != SoftplgModelOk)
        return STATUS_INVALID_PARAMETER;
    Bytes = Config.Count * sizeof(UINT32);
    Notifications = Streams->Ipc->Notifications;
    Result = SofIpcLargeConfigSet(Streams->Ipc, Copier->ModuleId, Streams->WidgetInstance[Widget], INTCDSP_COPIER_STREAM_PARAM,
                                  Config.Words, Bytes);
    if (Streams->Ipc->Notifications != Notifications)
        IntcDspNoteNotification(Streams);
    SofIpc4ModuleLargeConfig(&Msg, Copier->ModuleId, Streams->WidgetInstance[Widget], INTCDSP_COPIER_STREAM_PARAM, Bytes, Flags, FALSE);
    IntcDspTrace(Streams, Stream, INTCDSP_TRACE_IPC, Msg.Primary, Msg.Extension,
                 Result == SofIpcOk ? Streams->Ipc->Last.Status : (UINT32)Result);
    if (Result == SofIpcOk)
        return STATUS_SUCCESS;
    IntcDspStreamLog(Streams, "ipc: %s (%u) 0x%08x|0x%08x %s after %u us; the link is %s\n", SofIpcTypeName(Msg.Primary),
                     SofIpc4MsgType(&Msg), Msg.Primary | FWLOAD_IPC_BUSY, Msg.Extension, SofIpcStatusName(Result),
                     (UINT32)Streams->Ipc->Last.ElapsedUs, IntcDspLinkLost(Streams) ? "faulted" : "ready");
    return IntcDspIpcStatus(Result);
}

/* The stream a host copier of a PCM names, when one is open for its direction: its tag, and the index of the host copier's format it carries */
static
VOID
IntcDspCopierStream(
    _In_ const INTCDSP_STREAMS* Streams,
    UINT32 Pcm,
    _In_ const SOFTPLG_IPC4_WIDGET* Widget,
    _Out_ UINT32* Tag,
    _Out_ UINT32* InputFormat,
    _Out_ UINT32* OutputFormat)
{
    UINT32 S;

    *Tag = SOFTPLG_STREAM_TAG_NONE;
    *InputFormat = 0;
    *OutputFormat = 0;
    if (!IntcDspIsHostCopier(Widget))
        return;
    for (S = 0; S < INTCDSP_STREAMS_MAX; S++)
    {
        const INTCDSP_STREAM* Other = &Streams->Stream[S];

        if (Other->Pcm != Pcm || Other->Capture != (Widget->Kind == SoftplgKindHostCopierCapture) ||
            Other->State == IntcDspStreamStateFree || Other->State == IntcDspStreamStateReleased)
            continue;
        *Tag = Other->Tag;
        if (Widget->Kind == SoftplgKindHostCopierPlayback)
            *InputFormat = Other->Format;
        else
            *OutputFormat = Other->Format;
        return;
    }
}

/*
 * What the PCM holds in the firmware, deleted: every pipeline of it paused
 * if it runs and reset, every instance the state says is initialized
 * deleted, every created pipeline deleted, the sources first. Only what
 * the state records goes, so a build that stopped halfway is undone by the
 * same walk. A failure leaves the rest recorded for the next attempt.
 */
static
NTSTATUS
IntcDspPathTearDown(
    _Inout_ PINTCDSP_STREAMS Streams,
    _In_ const INTCDSP_STREAM* Stream,
    _In_reads_(Count) const UINT8* Order,
    UINT32 Count)
{
    const SOFTPLG_IPC4_MODEL* Model = Streams->Model;
    SOFIPC4_MSG Msg;
    NTSTATUS Status = STATUS_SUCCESS;
    UINT32 I, W, P;
    BOOLEAN Inside;

    for (I = 0; I < Count && NT_SUCCESS(Status); I++)
    {
        P = Order[Count - 1 - I];
        if (Streams->PipelineInstance[P] == 0 || Streams->PipelineState[P] == SOFIPC4_PIPELINE_STATE_RESET)
            continue;
        /* a pipeline still running is paused first: reset follows paused */
        if (Streams->PipelineState[P] == SOFIPC4_PIPELINE_STATE_RUNNING)
        {
            SofIpc4PipelineSetState(&Msg, Streams->PipelineInstance[P], SOFIPC4_PIPELINE_STATE_PAUSED, 0);
            Status = IntcDspSend(Streams, Stream, SofIpcClassPipeline, &Msg, NULL, 0);
            if (NT_SUCCESS(Status))
                Streams->PipelineState[P] = SOFIPC4_PIPELINE_STATE_PAUSED;
        }
        if (NT_SUCCESS(Status))
        {
            SofIpc4PipelineSetState(&Msg, Streams->PipelineInstance[P], SOFIPC4_PIPELINE_STATE_RESET, 0);
            Status = IntcDspSend(Streams, Stream, SofIpcClassPipeline, &Msg, NULL, 0);
            if (NT_SUCCESS(Status))
                Streams->PipelineState[P] = SOFIPC4_PIPELINE_STATE_RESET;
        }
    }
    for (W = 0; W < Model->WidgetCount && NT_SUCCESS(Status); W++)
    {
        if ((Streams->WidgetFlags[W] & INTCDSP_WIDGET_INSTANCE) == 0 || Model->Widgets[W].Pipeline >= Model->PipelineCount)
            continue;
        Inside = FALSE;
        for (I = 0; I < Count; I++)
            Inside |= Order[I] == Model->Widgets[W].Pipeline;
        if (!Inside)
            continue;
        SofIpc4ModuleDeleteInstance(&Msg, Model->Widgets[W].ModuleId, Streams->WidgetInstance[W]);
        Status = IntcDspSend(Streams, Stream, SofIpcClassModule, &Msg, NULL, 0);
        if (NT_SUCCESS(Status))
        {
            Streams->WidgetFlags[W] = 0;
            Streams->InstanceDeletes++;
        }
    }
    for (I = 0; I < Count && NT_SUCCESS(Status); I++)
    {
        /* the pipelines were created in the topology's order: the last of them goes first */
        P = Order[Count - 1 - I];
        if (Streams->PipelineInstance[P] == 0)
            continue;
        SofIpc4PipelineDelete(&Msg, Streams->PipelineInstance[P]);
        Status = IntcDspSend(Streams, Stream, SofIpcClassPipeline, &Msg, NULL, 0);
        if (NT_SUCCESS(Status))
        {
            Streams->PipelineInstance[P] = 0;
            Streams->PipelineState[P] = SOFIPC4_PIPELINE_STATE_INVALID;
            Streams->PipelineHeld[P] = 0;
            Streams->PipelineRunning[P] = 0;
            Streams->PipelineDeletes++;
        }
    }
    if (NT_SUCCESS(Status))
    {
        Streams->Path[Stream->Pcm].Built = FALSE;
        Streams->Path[Stream->Pcm].Undo = FALSE;
    }
    else
    {
        /* what could not be deleted the firmware still holds: the path stays built and its undo is finished by the next transition of the PCM (FA-03) */
        Streams->Path[Stream->Pcm].Built = TRUE;
        Streams->Path[Stream->Pcm].Undo = TRUE;
    }
    return Status;
}

static
NTSTATUS
IntcDspStuck(
    _Inout_ PINTCDSP_STREAMS Streams,
    NTSTATUS Status);

/* The tries an undo is given while the transport answers busy (STATUS_RETRY): the first and the retries (FA-03) */
#define INTCDSP_UNDO_TRIES 3

/*
 * A teardown sent again while the transport answers busy, the tries of
 * INTCDSP_UNDO_TRIES in all (FA-03): the walk goes on from what the state
 * still records, so a try finishes what the ones before it could not; the
 * last status is the undo's.
 */
static
NTSTATUS
IntcDspPathTearDownRetried(
    _Inout_ PINTCDSP_STREAMS Streams,
    _In_ const INTCDSP_STREAM* Stream,
    _In_reads_(Count) const UINT8* Order,
    UINT32 Count)
{
    NTSTATUS Status;
    UINT32 Try;

    Status = IntcDspPathTearDown(Streams, Stream, Order, Count);
    for (Try = 1; Try < INTCDSP_UNDO_TRIES && Status == STATUS_RETRY; Try++)
        Status = IntcDspPathTearDown(Streams, Stream, Order, Count);
    return Status;
}

/*
 * The undo a path still owes, finished before anything is built or named
 * on the PCM and before a stream lets go of its hold (FA-03): a teardown
 * the transport answered busy to is sent again, the tries of
 * INTCDSP_UNDO_TRIES. Busy again, what the firmware holds stays recorded
 * and the transition fails with STATUS_RETRY; any other failure forces the
 * fault.
 */
static
NTSTATUS
IntcDspPathUndoFinish(
    _Inout_ PINTCDSP_STREAMS Streams,
    _In_ const INTCDSP_STREAM* Stream)
{
    PINTCDSP_PATH Path = &Streams->Path[Stream->Pcm];
    UINT8 Order[INTCDSP_PCM_PIPELINES_MAX];
    NTSTATUS Status;
    UINT32 Count;

    if (!Path->Undo)
        return STATUS_SUCCESS;
    Count = IntcDspPipelineOrder(Streams, Stream->Pcm, FALSE, Order);
    Status = Count != 0xFFFFFFFF ? IntcDspPathTearDownRetried(Streams, Stream, Order, Count) : STATUS_NOT_SUPPORTED;
    if (!NT_SUCCESS(Status))
    {
        if (Status == STATUS_RETRY)
        {
            IntcDspStreamLog(Streams, "stream %u: the undo of the PCM's pipelines is still busy after %u tries; the firmware keeps what the state records and the next attempt deletes it\n",
                             Stream->Tag, (UINT32)INTCDSP_UNDO_TRIES);
            return Status;
        }
        IntcDspStreamLog(Streams, "stream %u: the undo of the PCM's pipelines could not be finished (0x%08x); released as after a fault\n",
                         Stream->Tag, (UINT32)Status);
        return IntcDspStuck(Streams, Status);
    }
    return Status;
}

/*
 * What the firmware is told about a PCM, once for all its streams: its
 * pipelines created (the instance ids in creation order, from 1), every
 * module widget the PCM reaches initialized in the topology's order, a host
 * copier with the tag of the stream open for its direction, and every edge
 * between two of them bound. On a failure the walk is undone while the link
 * still carries requests.
 */
static
NTSTATUS
IntcDspPathBuild(
    _Inout_ PINTCDSP_STREAMS Streams,
    _In_ const INTCDSP_STREAM* Stream)
{
    const SOFTPLG_IPC4_MODEL* Model = Streams->Model;
    const SOFTPLG_IPC4_WIDGET* Widget;
    SOFTPLG_IPC4_CONFIG Config;
    SOFIPC4_MSG Msg;
    NTSTATUS Status, Step;
    UINT8 Order[INTCDSP_PCM_PIPELINES_MAX];
    UINT32 Count, I, W, E, Instance, Tag, InputFormat, OutputFormat, Source, Sink;

    Count = IntcDspPipelineOrder(Streams, Stream->Pcm, FALSE, Order);
    if (Count == 0xFFFFFFFF || Count == 0)
        return STATUS_NOT_SUPPORTED;
    /* a pipeline another PCM holds is not created twice: no P3 topology shares one, and widgets shared by two PCMs would be initialized twice (INFO_NEEDED) */
    for (I = 0; I < Count; I++)
    {
        if (Streams->PipelineInstance[Order[I]] != 0)
        {
            IntcDspStreamLog(Streams, "stream %u: the PCM reaches pipeline %u, which another PCM holds; the stream is not allocated\n",
                             Stream->Tag, Model->Pipelines[Order[I]].Id);
            return STATUS_INVALID_DEVICE_STATE;
        }
    }
    Status = STATUS_SUCCESS;
    for (I = 0; I < Count && NT_SUCCESS(Status); I++)
    {
        const SOFTPLG_IPC4_WIDGET* Scheduler = IntcDspScheduler(Streams, Order[I]);

        if (Streams->NextPipelineInstance > SOFIPC4_PIPELINE_INSTANCE_MAX)
        {
            Status = STATUS_INSUFFICIENT_RESOURCES;
            break;
        }
        /* the create-pipeline message's fields are the scheduler's tokens: priority, memory pages, core and the low-power flag (plan Appendix B); the id is the boot's only once the firmware took the pipeline */
        Instance = Streams->NextPipelineInstance;
        SofIpc4PipelineCreate(&Msg, Instance, Scheduler->Priority, Scheduler->MemUsage, Scheduler->SchedCoreId, 0,
                              (Scheduler->LpMode & SOFIPC4_PIPELINE_CREATE_LP) ? SOFIPC4_PIPELINE_CREATE_LP : 0);
        Status = IntcDspSend(Streams, Stream, SofIpcClassPipeline, &Msg, NULL, 0);
        if (NT_SUCCESS(Status))
        {
            Streams->NextPipelineInstance = Instance + 1;
            Streams->PipelineInstance[Order[I]] = Instance;
            Streams->PipelineState[Order[I]] = SOFIPC4_PIPELINE_STATE_UNINITIALIZED;
            Streams->PipelineHeld[Order[I]] = 1;
        }
    }
    for (W = 0; W < Model->WidgetCount && NT_SUCCESS(Status); W++)
    {
        Widget = &Model->Widgets[W];
        if (!IntcDspReached(Streams, Stream->Pcm, W) || !IntcDspIsModule(Widget) || Widget->Pipeline >= Model->PipelineCount ||
            Streams->PipelineInstance[Widget->Pipeline] == 0)
            continue;
        if (Streams->ModuleInstances[Widget->ModuleId] > SOFIPC4_INSTANCE_ID_MAX)
        {
            Status = STATUS_INSUFFICIENT_RESOURCES;
            break;
        }
        Instance = Streams->ModuleInstances[Widget->ModuleId];
        IntcDspCopierStream(Streams, Stream->Pcm, Widget, &Tag, &InputFormat, &OutputFormat);
        if (SoftplgModelConfig(Model, W, InputFormat, OutputFormat, Tag, &Config) != SoftplgModelOk)
        {
            Status = STATUS_INVALID_DEVICE_STATE;
            break;
        }
        SofIpc4ModuleInitInstance(&Msg, Widget->ModuleId, Instance, Streams->PipelineInstance[Widget->Pipeline], Config.Count,
                                  Widget->CoreId, 0);
        Status = IntcDspSend(Streams, Stream, SofIpcClassModule, &Msg, (const UINT8*)Config.Words,
                             (SIZE_T)Config.Count * sizeof(UINT32));
        if (NT_SUCCESS(Status))
        {
            Streams->ModuleInstances[Widget->ModuleId] = Instance + 1;
            Streams->WidgetInstance[W] = (UINT8)Instance;
            Streams->WidgetFlags[W] = INTCDSP_WIDGET_INSTANCE |
                (IntcDspIsHostCopier(Widget) && Tag != SOFTPLG_STREAM_TAG_NONE ? INTCDSP_WIDGET_NAMED : 0);
        }
    }
    for (E = 0; E < Model->EdgeCount && NT_SUCCESS(Status); E++)
    {
        Source = Model->Edges[E].Source;
        Sink = Model->Edges[E].Sink;
        if (Source >= Model->WidgetCount || Sink >= Model->WidgetCount ||
            (Streams->WidgetFlags[Source] & INTCDSP_WIDGET_INSTANCE) == 0 || (Streams->WidgetFlags[Sink] & INTCDSP_WIDGET_INSTANCE) == 0)
            continue;
        SofIpc4ModuleBind(&Msg, Model->Widgets[Source].ModuleId, Streams->WidgetInstance[Source], Model->Widgets[Sink].ModuleId,
                          Streams->WidgetInstance[Sink], 0, 0, FALSE);
        Status = IntcDspSend(Streams, Stream, SofIpcClassModule, &Msg, NULL, 0);
    }
    if (NT_SUCCESS(Status))
    {
        Streams->Path[Stream->Pcm].Built = TRUE;
        return STATUS_SUCCESS;
    }
    if (IntcDspLinkLost(Streams))
        return Status;
    /* undone while the link still carries requests: the walk deletes what the state says was built; what it cannot delete is not kept (R1-04) */
    Step = IntcDspPathTearDownRetried(Streams, Stream, Order, Count);
    if (!NT_SUCCESS(Step))
    {
        if (Step == STATUS_RETRY)
        {
            /* a busy transport is not a fault (FA-03): the firmware keeps what the state records and the next transition of the PCM deletes it */
            IntcDspStreamLog(Streams, "stream %u: the PCM's pipelines could not be built (0x%08x) and the transport answered busy to the undo of what was built; the firmware keeps it and the next attempt deletes it\n",
                             Stream->Tag, (UINT32)Status);
            return Status;
        }
        IntcDspStreamLog(Streams, "stream %u: the PCM's pipelines could not be built (0x%08x) and what was built could not be deleted again (0x%08x); released as after a fault\n",
                         Stream->Tag, (UINT32)Status, (UINT32)Step);
        /* the status is the undo's, the one that lost the link (N7-03): the build's busy status beside a faulted link said nothing of what came */
        return IntcDspStuck(Streams, Step);
    }
    IntcDspStreamLog(Streams, "stream %u: the PCM's pipelines could not be built (0x%08x); what was built is deleted again\n",
                     Stream->Tag, (UINT32)Status);
    return Status;
}

static
HANDLE
IntcDspStreamHandle(
    _In_ const INTCDSP_STREAMS* Streams,
    _In_ const INTCDSP_STREAM* Stream);

/* The held buffer kept under a handle's full word, NULL for none (INTCDSP_HELD) */
static
PINTCDSP_HELD
IntcDspHeldOf(
    _Inout_ PINTCDSP_STREAMS Streams,
    HANDLE Handle)
{
    UINT32 I;

    for (I = 0; I < Streams->HeldCount; I++)
        if (Streams->Held[I].Owner == (UINT_PTR)Handle)
            return &Streams->Held[I];
    return NULL;
}

/*
 * A held buffer given back: the pages and the MDL as the bus's free returns
 * them (hdac_stream.cpp hdac_stream_free_buffer: MmFreePagesFromMdl, then the
 * MDL), at PASSIVE_LEVEL under the manager's mutex, which is a KMUTEX; no
 * bus call is made, so it works after a power-down and after the release of
 * the hardware. The record goes, the last taking its place. The caller
 * holds the lock. Pin: the pin's own free returned it, which is said in a
 * line (E1, N9-03's way back), with how many held buffers are left; a
 * buffer the manager returns on a transition's way out is freed silently.
 */
static
VOID
IntcDspHeldRelease(
    _Inout_ PINTCDSP_STREAMS Streams,
    _Inout_ PINTCDSP_HELD Held,
    BOOLEAN Pin)
{
    PMDL Mdl = Held->Mdl;
    UINT8 Tag = Held->Tag;
    BOOLEAN Capture = Held->Capture;
    UINT32 Bytes = (UINT32)Held->Bytes;
    UINT32 Left;

    *Held = Streams->Held[--Streams->HeldCount];
    RtlZeroMemory(&Streams->Held[Streams->HeldCount], sizeof(Streams->Held[0]));
    Left = Streams->HeldCount;
    MmFreePagesFromMdl(Mdl);
    ExFreePool(Mdl);
    {
        INTCDSP_STREAM Stream;

        RtlZeroMemory(&Stream, sizeof(Stream));
        Stream.Tag = Tag;
        Stream.Capture = Capture;
        IntcDspTrace(Streams, &Stream, INTCDSP_TRACE_BUFFER_FREE, 0, 0, Bytes);
    }
    if (Pin)
        IntcDspStreamLog(Streams, "stream %u: its pin's free returned the buffer (%u bytes); %u held left\n", Tag, Bytes, Left);
}

/* The held buffer of a handle's word given back, if the manager holds one: the buffer a transition kept for nobody (the allocate that did not hand it over, the pin's free that forced a fault), Pin when it is the pin's own free that returns it */
static
VOID
IntcDspHeldGiveBack(
    _Inout_ PINTCDSP_STREAMS Streams,
    HANDLE Handle,
    BOOLEAN Pin)
{
    PINTCDSP_HELD Held = IntcDspHeldOf(Streams, Handle);

    if (Held != NULL)
        IntcDspHeldRelease(Streams, Held, Pin);
}

/*
 * The buffer of a stream let go under a live pin (R5-01): the mapping is
 * dropped first, the software bound is switched off, and the buffer is
 * detached from the bus's stream, not freed, and kept under the word of the
 * owner's handle for the pin's free (IntcDspStreamFree); the pages and the
 * MDL are the pin's until then. The bus's table of a full manager leaves the
 * buffer allocated with a line: a leak is the safe side, a free under a
 * mapping is not.
 */
static
VOID
IntcDspStreamHoldBuffer(
    _Inout_ PINTCDSP_STREAMS Streams,
    _Inout_ PINTCDSP_STREAM Stream)
{
    const ADSP_BUS_INTERFACE* Bus = Streams->Bus;
    PMDL Mdl = NULL;
    NTSTATUS Status;

    if (Stream->Mdl == NULL)
        return;
    if (Streams->Ops != NULL && Streams->Ops->Unmap != NULL)
        Streams->Ops->Unmap(Streams->Ops->Context, Stream->Mdl);
    Bus->DSPDisableSPIB(Bus->Context, Stream->Handle);
    IntcDspTrace(Streams, Stream, INTCDSP_TRACE_SPIB_OFF, 0, 0, 0);
    Status = Bus->DetachDmaBuffer(Bus->Context, Stream->Handle, &Mdl);
    if (!NT_SUCCESS(Status) || Mdl == NULL)
    {
        IntcDspStreamLog(Streams, "stream %u: the bus does not detach its buffer of %I64u bytes (0x%08x); it stays with the stream, allocated\n",
                         Stream->Tag, (UINT64)Stream->Bytes, (UINT32)Status);
    }
    else if (Streams->HeldCount >= INTCDSP_HELD_MAX)
    {
        IntcDspStreamLog(Streams, "stream %u: its buffer of %I64u bytes is held for nobody, the manager's table of held buffers being full; left allocated\n",
                         Stream->Tag, (UINT64)Stream->Bytes);
    }
    else
    {
        PINTCDSP_HELD Held = &Streams->Held[Streams->HeldCount++];

        Held->Owner = (UINT_PTR)IntcDspStreamHandle(Streams, Stream);
        Held->Mdl = Mdl;
        Held->Bytes = Stream->Bytes;
        Held->Tag = Stream->Tag;
        Held->Capture = Stream->Capture;
        IntcDspTrace(Streams, Stream, INTCDSP_TRACE_BUFFER_HELD, 0, 0, (UINT32)Stream->Bytes);
    }
    Stream->Mdl = NULL;
    Stream->Bytes = 0;
}

/* The buffer of a stream given back to the bus: its mapping is dropped first, then the software bound off and the pages freed; the owner's release, from the pin's free of a live stream and from a transition that failed without a fault, where the pin never had the buffer */
static
VOID
IntcDspStreamReleaseBuffer(
    _Inout_ PINTCDSP_STREAMS Streams,
    _Inout_ PINTCDSP_STREAM Stream)
{
    const ADSP_BUS_INTERFACE* Bus = Streams->Bus;

    if (Stream->Mdl == NULL)
        return;
    if (Streams->Ops != NULL && Streams->Ops->Unmap != NULL)
        Streams->Ops->Unmap(Streams->Ops->Context, Stream->Mdl);
    Bus->DSPDisableSPIB(Bus->Context, Stream->Handle);
    IntcDspTrace(Streams, Stream, INTCDSP_TRACE_SPIB_OFF, 0, 0, 0);
    Bus->FreeDmaBufferWithNotification(Bus->Context, Stream->Handle, Stream->Mdl, Stream->Bytes);
    IntcDspTrace(Streams, Stream, INTCDSP_TRACE_BUFFER_FREE, 0, 0, (UINT32)Stream->Bytes);
    Stream->Mdl = NULL;
    Stream->Bytes = 0;
}

/* Everything the bus gave a stream, given back: it is stopped, its buffer detached and held for its pin (R5-01), its tag freed */
static
VOID
IntcDspStreamLetGo(
    _Inout_ PINTCDSP_STREAMS Streams,
    _Inout_ PINTCDSP_STREAM Stream)
{
    const ADSP_BUS_INTERFACE* Bus = Streams->Bus;

    if (Stream->State == IntcDspStreamStateRunning)
    {
        Bus->SetDmaEngineState(Bus->Context, Stream->Handle, PauseState);
        IntcDspTrace(Streams, Stream, INTCDSP_TRACE_STOP, 0, 0, PauseState);
    }
    IntcDspStreamHoldBuffer(Streams, Stream);
    if (Stream->Handle != NULL)
    {
        IntcDspTrace(Streams, Stream, INTCDSP_TRACE_FREE, 0, 0, 0);
        Bus->FreeStream(Bus->Context, Stream->Handle);
        Stream->Handle = NULL;
        Streams->Freed++;
    }
}

/*
 * Everything the streams hold, given back: each stream what the bus gave
 * it, its buffer detached and held for its pin (R5-01), the paths what they
 * built, the instance ids; a stream object in use is Released from here and
 * waits for its close. A stream whose teardown was pending holds its buffer
 * for nobody, its pin's free having been answered, so the buffer is returned
 * here, after its tag (N7-01). The fault and the power-down's release end
 * here, the caller holding the lock; each is the device letting its streams
 * go (E1, D4), so every stream released here is marked Lost, its pin to be
 * answered STATUS_DEVICE_REMOVED, and told so in one line. Fault: the kind
 * of the fault and the time the failing exchange took, a line per stream
 * before the pin's; NULL at a power-down. The count of the teardowns that
 * were pending is returned. One line counts the release: the streams let
 * go, the buffers it held for their pins, the tags it gave back.
 */
static
UINT32
IntcDspStreamsLetGo(
    _Inout_ PINTCDSP_STREAMS Streams,
    _In_opt_z_ const char* Fault,
    UINT32 FaultMs,
    BOOLEAN NoRequest)
{
    const SOFTPLG_IPC4_MODEL* Model = Streams->Model;
    UINT32 Pending = 0, LetGo = 0, Held = 0, Tags = Streams->Freed, Before, S;

    for (S = 0; S < INTCDSP_STREAMS_MAX; S++)
    {
        PINTCDSP_STREAM Stream = &Streams->Stream[S];

        if (Stream->State == IntcDspStreamStateFree || Stream->State == IntcDspStreamStateReleased)
            continue;
        if (Fault != NULL)
        {
            /* the poll's fault has no exchange to time: an exception the firmware posted with no request outstanding (IntcDspStreamsPoll) */
            if (NoRequest)
                IntcDspStreamLog(Streams, "fault during stream %u: %s with no request outstanding, read at a stream's period\n", Stream->Tag, Fault);
            else
                IntcDspStreamLog(Streams, "fault during stream %u: %s after %u ms\n", Stream->Tag, Fault, FaultMs);
        }
        IntcDspStreamLog(Streams, "stream %u: the pin is told: device removed\n", Stream->Tag);
        Before = Streams->HeldCount;
        IntcDspStreamLetGo(Streams, Stream);
        if (Streams->HeldCount > Before)
            Held++;
        if (Stream->State == IntcDspStreamStateTeardown)
        {
            IntcDspHeldGiveBack(Streams, IntcDspStreamHandle(Streams, Stream), FALSE);
            Pending++;
        }
        Stream->State = IntcDspStreamStateReleased;
        Stream->Lost = TRUE;
        Stream->Events = 0;
        LetGo++;
    }
    RtlZeroMemory(Streams->Path, (SIZE_T)Model->PcmCount * sizeof(INTCDSP_PATH));
    RtlZeroMemory(Streams->PipelineInstance, (SIZE_T)Model->PipelineCount * 4 * sizeof(UINT32));
    RtlZeroMemory(Streams->WidgetInstance, (SIZE_T)Model->WidgetCount * 2);
    RtlZeroMemory(Streams->ModuleInstances, INTCDSP_STREAM_MODULES_MAX);
    Streams->NextPipelineInstance = 1;
    IntcDspStreamLog(Streams, "release: %u streams let go, %u buffers held for their pins, %u tags back\n", LetGo, Held, Streams->Freed - Tags);
    return Pending;
}

/*
 * The kind of a fault for its lines (E1): the transport's outcome that
 * faulted the link, or the undo step's own when the link still carries
 * requests and the release was forced (IntcDspStuck); the firmware's own
 * status by name when it answered an error.
 */
static
const char*
IntcDspFaultKind(
    _In_ const INTCDSP_STREAMS* Streams)
{
    const SOFIPC_CONTEXT* Ipc = Streams->Ipc;
    SOFIPC_STATUS Outcome;

    if (Ipc == NULL)
        return "unknown";
    Outcome = IntcDspLinkLost(Streams) ? Ipc->Failure : Ipc->Last.Outcome;
    if (Outcome == SofIpcFwStatus)
        return SofIpc4StatusName(Ipc->Last.Status);
    return SofIpcStatusName(Outcome);
}

/*
 * A request faulted the link: the firmware's objects are gone once the
 * driver powers the DSP down, so every stream gives back what the bus gave
 * it, the paths forget what they built, and the driver is told. The manager
 * refuses every transition until a boot initializes it again, at the next
 * open that finds a Boot operation (E1) or at the next D0 entry. NoRequest:
 * the fault is the poll's (IntcDspStreamsPoll), an exception with no
 * exchange to time; its lines say so instead of a time.
 */
static
VOID
IntcDspStreamsFaultFrom(
    _Inout_ PINTCDSP_STREAMS Streams,
    BOOLEAN NoRequest)
{
    UINT32 Pending, Ms = 0;

    if (Streams->Faulted)
        return;
    Streams->Faulted = TRUE;
    Streams->Faults++;
    /* the driver powers the DSP down for this fault: the next open boots it, and the idle timer has nothing left to time */
    Streams->NeedsBoot = TRUE;
    IntcDspStreamsIdleCancelLocked(Streams);
    if (Streams->Ipc != NULL && !NoRequest)
        Ms = (UINT32)((Streams->Ipc->Last.ElapsedUs + 500) / 1000);
    Pending = IntcDspStreamsLetGo(Streams, IntcDspFaultKind(Streams), Ms, NoRequest);
    IntcDspStreamLog(Streams, "streams: released after the fault; nothing is sent until the next stream open or D0 entry\n");
    if (Pending != 0)
        IntcDspStreamLog(Streams, "streams: %u teardowns were pending at the release; their buffers are returned\n", Pending);
    if (Streams->Ops != NULL && Streams->Ops->Fault != NULL)
        Streams->Ops->Fault(Streams->Ops->Context);
}

/* Every fault whose time is a request's: the request's own (IntcDspFailed, IntcDspStuck) */
static
VOID
IntcDspStreamsFault(
    _Inout_ PINTCDSP_STREAMS Streams)
{
    IntcDspStreamsFaultFrom(Streams, FALSE);
}

VOID
IntcDspStreamsPoll(
    _Inout_ PINTCDSP_STREAMS Streams)
{
    if (KeGetCurrentIrql() != PASSIVE_LEVEL)
        return;
    IntcDspStreamsLock(Streams);
    /* the manager up and its link ready; the transport's poll answers FALSE alike on a crash it read and on a request lock it left alone, so the link is asked which */
    if (Streams->Ready && !Streams->Faulted && Streams->Ipc != NULL && Streams->Ipc->Link == SofIpcLinkReady &&
        !SofIpcPollNotifications(Streams->Ipc) && IntcDspLinkLost(Streams))
    {
        IntcDspStreamLog(Streams, "ipc: %s with no request outstanding, read at a stream's period; the link is faulted\n", IntcDspFaultKind(Streams));
        IntcDspStreamsFaultFrom(Streams, TRUE);
    }
    IntcDspStreamsUnlock(Streams);
}

VOID
IntcDspStreamsRelease(
    _Inout_ PINTCDSP_STREAMS Streams)
{
    UINT32 Pending;

    IntcDspStreamsLock(Streams);
    /* the idle timer is not left to fire on a device going off (E2) */
    IntcDspStreamsIdleCancelLocked(Streams);
    /* only a manager that was started holds anything: its streams were opened while it was ready */
    if (Streams->Ready)
    {
        /* the DSP goes off with the model kept: the next open boots it (E1) */
        Streams->NeedsBoot = TRUE;
        Pending = IntcDspStreamsLetGo(Streams, NULL, 0, FALSE);
        IntcDspStreamLog(Streams, "streams: released for the power-down; nothing is sent until the next stream open or D0 entry\n");
        if (Pending != 0)
            IntcDspStreamLog(Streams, "streams: %u teardowns were pending at the release; their buffers are returned\n", Pending);
    }
    Streams->Ready = FALSE;
    Streams->Generation++;
    IntcDspStreamsUnlock(Streams);
}

/*
 * The device's own boot sequence begins and ends here (N10-04, stream.h
 * DeviceBoot): the driver's D0 entry calls the first before its
 * IntcDspBootSequence and the second after it. The flag is set and
 * cleared under the lock, and the lock is let go before the sequence
 * runs, since the sequence takes it itself at the manager's start. So the
 * lock has the one order it had: the entry waits here, at the lock, for
 * an open that holds it (a Boot operation of an open included) and holds
 * nothing while its boot runs; an open that comes during that boot finds
 * the flag set and is refused without its own boot (IntcDspStreamOpen).
 * Without it an open arriving after a D0 exit, while the entry's boot is
 * still running, would boot the same device a second time.
 */
VOID
IntcDspStreamsDeviceBootBegin(
    _Inout_ PINTCDSP_STREAMS Streams)
{
    IntcDspStreamsLock(Streams);
    Streams->DeviceBoot = TRUE;
    IntcDspStreamsUnlock(Streams);
}

VOID
IntcDspStreamsDeviceBootEnd(
    _Inout_ PINTCDSP_STREAMS Streams)
{
    IntcDspStreamsLock(Streams);
    Streams->DeviceBoot = FALSE;
    IntcDspStreamsUnlock(Streams);
}

/* The end of a transition a request failed in: the fault's release when the link was lost, the status otherwise */
static
NTSTATUS
IntcDspFailed(
    _Inout_ PINTCDSP_STREAMS Streams,
    NTSTATUS Status)
{
    if (IntcDspLinkLost(Streams))
        IntcDspStreamsFault(Streams);
    return Status;
}

/*
 * The end of a transition that could not undo, or finish, what it told
 * the firmware while the link still carries requests (R1-04): the firmware
 * holds what the manager no longer records, so the uniform release is
 * forced as after a lost link, every stream's buffer and tag go back and
 * the driver powers the DSP down; the status is the step's. A busy
 * transport (STATUS_RETRY) is not such a failure (FA-03): the undo was
 * sent again a bounded number of times, what the firmware holds stays
 * recorded for the next attempt and the transition fails with the status
 * alone; the fault is forced for failures that are not busy only.
 */
static
NTSTATUS
IntcDspStuck(
    _Inout_ PINTCDSP_STREAMS Streams,
    NTSTATUS Status)
{
    if (Status == STATUS_RETRY)
        return Status;
    IntcDspStreamsFault(Streams);
    return Status;
}

SIZE_T
IntcDspStreamsBytes(
    _In_ const SOFTPLG_IPC4_MODEL* Model)
{
    return (SIZE_T)Model->PcmCount * sizeof(INTCDSP_PATH) + (SIZE_T)Model->PipelineCount * 4 * sizeof(UINT32) +
        (SIZE_T)Model->WidgetCount * 2 + INTCDSP_STREAM_MODULES_MAX;
}

/* How many buffers the manager holds for their pins (R5-01): the removal's line says it */
UINT32
IntcDspStreamsHeld(
    _In_ const INTCDSP_STREAMS* Streams)
{
    return Streams->HeldCount;
}

BOOLEAN
IntcDspIdleDue(
    UINT32 Allocated,
    UINT64 NowUs,
    UINT64 LastCloseUs,
    ULONG IdleSec)
{
    return Allocated == 0 && IdleSec != 0 && NowUs - LastCloseUs >= (UINT64)IdleSec * 1000000ULL;
}

/* The streams allocated on the manager, under the lock: the PCMs' open streams, counted by path (INTCDSP_PATH.Open) */
static
UINT32
IntcDspStreamsAllocatedLocked(
    _In_ const INTCDSP_STREAMS* Streams)
{
    UINT32 P, Allocated = 0;

    if (Streams->Model == NULL || Streams->Path == NULL)
        return 0;
    for (P = 0; P < Streams->Model->PcmCount; P++)
        Allocated += Streams->Path[P].Open;
    return Allocated;
}

UINT32
IntcDspStreamsAllocated(
    _In_ const INTCDSP_STREAMS* Streams)
{
    return IntcDspStreamsAllocatedLocked(Streams);
}

/*
 * The idle timer, cancelled (E2), under the lock: the device's timer is
 * stopped while the manager says it is armed, and then it is not.
 */
static
VOID
IntcDspStreamsIdleCancelLocked(
    _Inout_ PINTCDSP_STREAMS Streams)
{
    if (!Streams->IdleArmed)
        return;
    Streams->IdleArmed = FALSE;
    if (Streams->Ops != NULL && Streams->Ops->IdleCancel != NULL)
        Streams->Ops->IdleCancel(Streams->Ops->Context);
}

/*
 * A stream stopped being allocated (E2), under the lock: the close, or the
 * drain of a pending teardown. The time is kept for the decision, and when
 * no stream is left the timer is armed once for the setting's time, by a
 * manager that is up with a clock; a timer already armed (a close that
 * left the count above zero cannot be this one's) is left as it is.
 */
static
VOID
IntcDspStreamsIdleClosedLocked(
    _Inout_ PINTCDSP_STREAMS Streams)
{
    UINT64 Now = IntcDspNow(Streams);

    Streams->LastCloseUs = Now;
    if (Streams->IdleSec == 0 || Streams->IdleArmed || !Streams->Ready || Streams->Faulted || !IntcDspHasClock(Streams) ||
        Streams->Ops == NULL || Streams->Ops->IdleArm == NULL)
        return;
    if (IntcDspStreamsAllocatedLocked(Streams) != 0)
        return;
    Streams->IdleDueUs = Now + (UINT64)Streams->IdleSec * 1000000ULL;
    Streams->IdleArmed = TRUE;
    Streams->Ops->IdleArm(Streams->Ops->Context, Streams->IdleDueUs);
}

VOID
IntcDspStreamsIdleSec(
    _Inout_ PINTCDSP_STREAMS Streams,
    ULONG IdleSec)
{
    IntcDspStreamsLock(Streams);
    Streams->IdleSec = IdleSec;
    if (IdleSec == 0)
        IntcDspStreamsIdleCancelLocked(Streams);
    IntcDspStreamsUnlock(Streams);
}

VOID
IntcDspStreamsIdle(
    _Inout_ PINTCDSP_STREAMS Streams)
{
    UINT64 Now;

    if (KeGetCurrentIrql() != PASSIVE_LEVEL)
        return;
    IntcDspStreamsLock(Streams);
    Now = IntcDspNow(Streams);
    /* armed for a later time: a newer arming replaced the timer whose fire queued this, and the work item is not its */
    if (Streams->IdleArmed && Now < Streams->IdleDueUs)
    {
        IntcDspStreamsUnlock(Streams);
        return;
    }
    Streams->IdleArmed = FALSE;
    /* the pending teardowns first, as every transition's prologue does (N8-02): a release that faults the link here ends in the fault's release, and the driver powers the DSP down for it */
    IntcDspStreamsDrain(Streams);
    if (!Streams->Ready || Streams->Faulted)
    {
        IntcDspStreamsUnlock(Streams);
        return;
    }
    /* the clock is read after the drain: a teardown it finished may have released the last stream, and that close's time is the drain's, not the fire's; the timer it armed is for later, and this work item is not its */
    Now = IntcDspNow(Streams);
    /* the count is read again: an open may have arrived since the timer fired */
    if (!IntcDspIdleDue(IntcDspStreamsAllocatedLocked(Streams), Now, Streams->LastCloseUs, Streams->IdleSec))
    {
        IntcDspStreamsUnlock(Streams);
        return;
    }
    Streams->IdleDown = TRUE;
    Streams->IdleDowns++;
    IntcDspStreamLog(Streams, "idle power down after %u s\n", (UINT32)((Now - Streams->LastCloseUs) / 1000000ULL));
    if (Streams->Ops != NULL && Streams->Ops->IdleDown != NULL)
        Streams->Ops->IdleDown(Streams->Ops->Context);
    IntcDspStreamsUnlock(Streams);
}

UINT32
IntcDspStreamsTeardownPoll(
    _Inout_ PINTCDSP_STREAMS Streams)
{
    UINT32 S, Pending = 0;

    if (KeGetCurrentIrql() != PASSIVE_LEVEL)
        return 0;
    IntcDspStreamsLock(Streams);
    IntcDspStreamsDrain(Streams);
    for (S = 0; S < INTCDSP_STREAMS_MAX; S++)
        if (Streams->Stream[S].State == IntcDspStreamStateTeardown)
            Pending++;
    IntcDspStreamsUnlock(Streams);
    return Pending;
}

/* The module ids the manager keeps an instance count for are stream.h's (INTCDSP_STREAM_MODULES_MAX): a module widget's index by its id */
UINT32
IntcDspStreamsModuleRange(
    _In_ const SOFTPLG_IPC4_MODEL* Model)
{
    UINT32 W;

    for (W = 0; W < Model->WidgetCount; W++)
        if (IntcDspIsModule(&Model->Widgets[W]) && Model->Widgets[W].ModuleId >= INTCDSP_STREAM_MODULES_MAX)
            return W;
    return SOFTPLG_NO_WIDGET;
}

VOID
IntcDspStreamsInit(
    _Out_ PINTCDSP_STREAMS Streams,
    _Out_writes_bytes_(Bytes) UINT8* Buffer,
    SIZE_T Bytes,
    _In_ const SOFTPLG_IPC4_MODEL* Model,
    _In_ SOFIPC_CONTEXT* Ipc,
    _In_ const ADSP_BUS_INTERFACE* Bus,
    _In_opt_ const INTCDSP_STREAMS_OPS* Ops)
{
    SIZE_T Offset = 0, Needed = IntcDspStreamsBytes(Model);

    /* the lock is made once for a manager and kept through its re-initializations, and so is the generation, which every handle of the manager before is of (stream.h) */
    if (Streams->Generation == 0)
        KeInitializeMutex(&Streams->Lock, 0);
    IntcDspStreamsLock(Streams);
    Streams->Generation++;
    RtlZeroMemory(&Streams->Ops, sizeof(*Streams) - FIELD_OFFSET(INTCDSP_STREAMS, Ops));
    Streams->Ops = Ops;
    Streams->Bus = Bus;
    Streams->Ipc = Ipc;
    Streams->Model = Model;
    Streams->NextPipelineInstance = 1;
    if (Bytes < Needed)
    {
        IntcDspStreamsUnlock(Streams);
        return;
    }
    RtlZeroMemory(Buffer, Needed);
    Streams->Path = (PINTCDSP_PATH)(Buffer + Offset);
    Offset += (SIZE_T)Model->PcmCount * sizeof(INTCDSP_PATH);
    Streams->PipelineInstance = (PUINT32)(Buffer + Offset);
    Offset += (SIZE_T)Model->PipelineCount * sizeof(UINT32);
    Streams->PipelineState = (PUINT32)(Buffer + Offset);
    Offset += (SIZE_T)Model->PipelineCount * sizeof(UINT32);
    Streams->PipelineHeld = (PUINT32)(Buffer + Offset);
    Offset += (SIZE_T)Model->PipelineCount * sizeof(UINT32);
    Streams->PipelineRunning = (PUINT32)(Buffer + Offset);
    Offset += (SIZE_T)Model->PipelineCount * sizeof(UINT32);
    Streams->WidgetInstance = Buffer + Offset;
    Offset += Model->WidgetCount;
    Streams->WidgetFlags = Buffer + Offset;
    Offset += Model->WidgetCount;
    Streams->ModuleInstances = Buffer + Offset;
    /* the bus must carry the stream members, the transport must be ready, and every widget must name a module the manager can count */
    Streams->Ready = Ipc != NULL && Ipc->Fw != NULL && Ipc->Link == SofIpcLinkReady && Bus != NULL && IntcDspBusStreams(Bus) &&
        IntcDspStreamsModuleRange(Model) == SOFTPLG_NO_WIDGET;
    if (!Streams->Ready)
        IntcDspStreamLog(Streams, "streams: not started (%s)\n",
                         Bus != NULL && !IntcDspBusStreams(Bus) ? "the bus carries no stream members" :
                         Ipc == NULL || Ipc->Fw == NULL || Ipc->Link != SofIpcLinkReady ? "the IPC link is not ready" :
                         "a widget names a module the manager cannot keep");
    IntcDspStreamsUnlock(Streams);
}

/* The handle of a stream object: its index and one in the low bits, the generation above them */
static
HANDLE
IntcDspStreamHandle(
    _In_ const INTCDSP_STREAMS* Streams,
    _In_ const INTCDSP_STREAM* Stream)
{
    return (HANDLE)(UINT_PTR)(((UINT_PTR)Stream->Generation << INTCDSP_STREAM_INDEX_BITS) |
                              (UINT_PTR)(Stream - Streams->Stream + 1));
}

PINTCDSP_STREAM
IntcDspStreamOf(
    _In_ const INTCDSP_STREAMS* Streams,
    HANDLE Handle)
{
    UINT_PTR Word = (UINT_PTR)Handle;
    UINT32 Index = (UINT32)(Word & INTCDSP_STREAM_INDEX_MASK);
    PINTCDSP_STREAM Stream;

    /* the index is one-based: no value below the first object names one */
    if (Index == 0 || Index > INTCDSP_STREAMS_MAX)
        return NULL;
    Stream = (PINTCDSP_STREAM)&Streams->Stream[Index - 1];
    if ((Word >> INTCDSP_STREAM_INDEX_BITS) != Streams->Generation || Stream->Generation != Streams->Generation ||
        Stream->State == IntcDspStreamStateFree)
        return NULL;
    return Stream;
}

BOOLEAN
IntcDspStreamStale(
    _In_ const INTCDSP_STREAMS* Streams,
    HANDLE Handle)
{
    UINT_PTR Word = (UINT_PTR)Handle;
    UINT32 Index = (UINT32)(Word & INTCDSP_STREAM_INDEX_MASK);

    /* the index is one-based, as IntcDspStreamOf reads it: a word without an object's index is no handle, stale or not */
    return Index != 0 && Index <= INTCDSP_STREAMS_MAX && (Word >> INTCDSP_STREAM_INDEX_BITS) != Streams->Generation;
}

/*
 * The pin's answer for a stream the device let go (E1, D4): a handle of
 * an older generation (IntcDspStreamStale), or a stream of this generation
 * the fault's or the power-down's release let go (Lost). Every call of the
 * pin on such a stream is STATUS_DEVICE_REMOVED, asked before anything of
 * the manager's readiness, but two: its buffer's free, answered as the Fix
 * F contract says, and its close, answered STATUS_SUCCESS. A stream
 * Released by its own free (the drain of a pending teardown, N7-01) is not
 * Lost, and the pin's calls on it are answered as before.
 */
static
BOOLEAN
IntcDspStreamRemoved(
    _In_ const INTCDSP_STREAMS* Streams,
    _In_opt_ const INTCDSP_STREAM* Stream,
    HANDLE Handle)
{
    return IntcDspStreamStale(Streams, Handle) || (Stream != NULL && Stream->Lost);
}

UINT32
IntcDspStreamPcm(
    _In_ const INTCDSP_STREAMS* Streams,
    _In_z_ const char* Name)
{
    const SOFTPLG_IPC4_MODEL* Model = Streams->Model;
    UINT32 P;

    if (Model == NULL)
        return SOFTPLG_NO_PCM;
    for (P = 0; P < Model->PcmCount; P++)
        if (strncmp(Name, Model->Pcms[P].Name, SOFTPLG_TEXT_BYTES) == 0)
            return P;
    return SOFTPLG_NO_PCM;
}

/* The host copier a direction's stream serves and the index of its format the stream carries: of a playback copier's input formats for render, of a capture copier's output formats for capture; the controller must be able to carry it */
static
NTSTATUS
IntcDspStreamFormat(
    _In_ const INTCDSP_STREAMS* Streams,
    UINT32 Pcm,
    BOOLEAN Capture,
    ULONG SampleRate,
    ULONG Channels,
    ULONG ValidBits,
    ULONG ContainerBits,
    _Out_ UINT32* Copier,
    _Out_ UINT32* Format)
{
    const SOFTPLG_IPC4_MODEL* Model = Streams->Model;
    const SOFTPLG_IPC4_PCM* P = &Model->Pcms[Pcm];
    const SOFTPLG_IPC4_WIDGET* Widget;
    const SOFTPLG_IPC4_FORMAT* F;
    UINT32 Index, First, Count;

    *Copier = Capture ? P->CaptureCopier : P->PlaybackCopier;
    if (*Copier == SOFTPLG_NO_WIDGET || *Copier >= Model->WidgetCount)
        return STATUS_NOT_SUPPORTED;
    Widget = &Model->Widgets[*Copier];
    if (Widget->FirstFormat == SOFTPLG_NO_FORMAT)
        return STATUS_NOT_SUPPORTED;
    First = Widget->FirstFormat + (Capture ? Widget->InputFormats : 0);
    Count = Capture ? Widget->OutputFormats : Widget->InputFormats;
    for (Index = 0; Index < Count; Index++)
    {
        F = &Model->Formats[First + Index];
        if (F->Expressible && F->Words[SOFTPLG_FORMAT_WORD_RATE] == SampleRate && F->Words[SOFTPLG_FORMAT_WORD_DEPTH] == ContainerBits &&
            SOFTPLG_FORMAT_CHANNELS_OF(F->Words[SOFTPLG_FORMAT_WORD_CFG]) == Channels &&
            SOFTPLG_FORMAT_VALID_OF(F->Words[SOFTPLG_FORMAT_WORD_CFG]) == ValidBits)
        {
            *Format = Index;
            return STATUS_SUCCESS;
        }
    }
    return STATUS_NOT_SUPPORTED;
}

ULONG
IntcDspStreamFrameBytes(
    ULONG Channels,
    ULONG ContainerBits)
{
    return Channels * (ContainerBits / 8);
}

static
NTSTATUS
IntcDspStreamOpenLocked(
    _Inout_ PINTCDSP_STREAMS Streams,
    UINT32 Pcm,
    BOOLEAN Capture,
    ULONG SampleRate,
    ULONG Channels,
    ULONG ValidBits,
    ULONG ContainerBits,
    _Out_ PHANDLE Stream)
{
    PINTCDSP_STREAM Found = NULL;
    HDAUDIO_STREAM_FORMAT Format;
    UINT8 Order[INTCDSP_PCM_PIPELINES_MAX];
    NTSTATUS Status;
    UINT32 S;

    if (!Streams->Ready || Streams->Faulted)
    {
        *Stream = NULL;
        return STATUS_DEVICE_NOT_READY;
    }
    if (Pcm >= Streams->Model->PcmCount)
    {
        *Stream = NULL;
        return STATUS_INVALID_PARAMETER;
    }
    if (IntcDspPipelineOrder(Streams, Pcm, TRUE, Order) == 0xFFFFFFFF)
    {
        IntcDspStreamLog(Streams, "stream: the PCM '%s' reaches more than %u pipelines; refused\n", Streams->Model->Pcms[Pcm].Name,
                         (UINT32)INTCDSP_PCM_PIPELINES_MAX);
        *Stream = NULL;
        return STATUS_NOT_SUPPORTED;
    }
    for (S = 0; S < INTCDSP_STREAMS_MAX && Found == NULL; S++)
        if (Streams->Stream[S].State == IntcDspStreamStateFree)
            Found = &Streams->Stream[S];
    if (Found == NULL)
    {
        *Stream = NULL;
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    /* the format is matched before anything is taken */
    Status = IntcDspStreamFormat(Streams, Pcm, Capture, SampleRate, Channels, ValidBits, ContainerBits, &Found->Copier, &Found->Format);
    if (!NT_SUCCESS(Status))
    {
        IntcDspStreamLog(Streams, "stream: %u Hz %u ch %u in %u bits is not a format the %s copier of '%s' offers; refused\n",
                         (UINT32)SampleRate, (UINT32)Channels, (UINT32)ValidBits, (UINT32)ContainerBits,
                         Capture ? "capture" : "playback", Streams->Model->Pcms[Pcm].Name);
        *Stream = NULL;
        return Status;
    }
    RtlZeroMemory(&Format, sizeof(Format));
    Format.SampleRate = SampleRate;
    Format.ValidBitsPerSample = (USHORT)ValidBits;
    Format.ContainerSize = (USHORT)ContainerBits;
    Format.NumberOfChannels = (USHORT)Channels;
    if (Capture)
        Status = Streams->Bus->GetCaptureStream(Streams->Bus->Context, Format, &Found->Handle, &Found->Tag);
    else
        Status = Streams->Bus->GetRenderStream(Streams->Bus->Context, Format, &Found->Handle, &Found->Tag);
    if (!NT_SUCCESS(Status))
    {
        IntcDspStreamLog(Streams, "stream: the bus gives no %s stream (0x%08x)\n", Capture ? "capture" : "render", (UINT32)Status);
        *Stream = NULL;
        return Status;
    }
    Found->Capture = Capture;
    Found->Pcm = Pcm;
    Found->StreamFormat = Format;
    Found->Mdl = NULL;
    Found->Bytes = 0;
    Found->OffsetFromFirstPage = 0;
    Found->Events = 0;
    Found->State = IntcDspStreamStateOpen;
    Found->Generation = Streams->Generation;
    /* the object is the new stream's: whatever the device did to its user before (E1, the Lost marker) is forgotten */
    Found->Lost = FALSE;
    Streams->Path[Pcm].Open++;
    IntcDspTrace(Streams, Found, INTCDSP_TRACE_OPEN, 0, 0, SampleRate);
    /* a stream is allocated: the DSP stays up while the pin holds it, running or not (P5-R5) */
    IntcDspStreamsIdleCancelLocked(Streams);
    *Stream = IntcDspStreamHandle(Streams, Found);
    return STATUS_SUCCESS;
}

NTSTATUS
IntcDspStreamOpen(
    _Inout_ PINTCDSP_STREAMS Streams,
    UINT32 Pcm,
    BOOLEAN Capture,
    ULONG SampleRate,
    ULONG Channels,
    ULONG ValidBits,
    ULONG ContainerBits,
    _Out_ PHANDLE Stream)
{
    NTSTATUS Status, Boot = STATUS_SUCCESS;
    UINT64 Start = 0;
    BOOLEAN Booted = FALSE, Clocked = FALSE, Recovering = FALSE;

    *Stream = NULL;
    if (KeGetCurrentIrql() != PASSIVE_LEVEL)
        return STATUS_INVALID_DEVICE_STATE;
    IntcDspStreamsLock(Streams);
    /*
     * The boot at the open (E1): the manager is not ready, or a request
     * faulted it. The Boot operation boots the DSP and starts the manager
     * again on it, inside this lock (the mutex is recursive); a second open
     * while the first boots waits here and then finds the manager ready. No
     * operation: the open is refused as it always was. The device's own
     * boot in progress (N10-04): the same refusal, the operation not run.
     * After the operation
     * the manager is asked again, never the operation's status: not ready
     * still, the open is refused and the next one boots again.
     */
    if (!Streams->Ready || Streams->Faulted)
    {
        if (Streams->DeviceBoot)
        {
            /* the device's own boot sequence is in progress (N10-04): the Boot operation is not run beside it, and the lock is not kept waiting for a boot that takes it; the open is refused as one without the operation is, and the next open finds the manager as that sequence left it */
            IntcDspStreamLog(Streams, "boot at stream open not run: the D0 entry's boot is in progress; no stream is taken\n");
            IntcDspStreamsUnlock(Streams);
            return STATUS_DEVICE_NOT_READY;
        }
        if (Streams->Ops == NULL || Streams->Ops->Boot == NULL)
        {
            IntcDspStreamsUnlock(Streams);
            return STATUS_DEVICE_NOT_READY;
        }
        /* a recovery after a fault or a power-down (E1), or a first boot at an open: after an idle power-down the line is a boot's, since nothing went wrong (E2) */
        Recovering = Streams->NeedsBoot && !Streams->IdleDown;
        Clocked = IntcDspHasClock(Streams);
        Start = IntcDspNow(Streams);
        Boot = Streams->Ops->Boot(Streams->Ops->Context);
        Booted = TRUE;
        if (!Streams->Ready || Streams->Faulted)
        {
            /* not ready still: the need of a boot stays and the line says so; the refusal is the locked open's own, the same status it gives every manager that is not ready */
            Streams->NeedsBoot = TRUE;
            IntcDspStreamLog(Streams, "boot at stream open failed: the DSP is not ready after the boot (0x%08x); no stream is taken\n", (UINT32)Boot);
        }
    }
    IntcDspStreamsDrain(Streams);
    Status = IntcDspStreamOpenLocked(Streams, Pcm, Capture, SampleRate, Channels, ValidBits, ContainerBits, Stream);
    if (Booted && NT_SUCCESS(Status))
    {
        /* the boot's wall time at this open (E1): a recovery after a fault or a power-down, or a first boot at an open (E2) */
        const char* Line = Recovering ? "recovered: open ok" : "boot at stream open: ok";

        if (Clocked)
            IntcDspStreamLog(Streams, "%s after %u ms\n", Line, (UINT32)((IntcDspNow(Streams) - Start + 500) / 1000));
        else
            IntcDspStreamLog(Streams, "%s\n", Line);
    }
    IntcDspStreamsUnlock(Streams);
    return Status;
}

/* The buffer of a stream, then the firmware told about the PCM; on a failure the buffer is released and the stream is as an open left it */
static
NTSTATUS
IntcDspStreamBuffer(
    _Inout_ PINTCDSP_STREAMS Streams,
    _Inout_ PINTCDSP_STREAM Stream,
    SIZE_T Bytes,
    ULONG NotificationCount)
{
    const ADSP_BUS_INTERFACE* Bus = Streams->Bus;
    PINTCDSP_PATH Path = &Streams->Path[Stream->Pcm];
    UCHAR StreamId = 0;
    NTSTATUS Status, Step;
    UINT8 Order[INTCDSP_PCM_PIPELINES_MAX];
    UINT32 Count;

    /* a path whose undo a busy transport cut short is finished before the buffer is taken and anything is built or named on it (FA-03) */
    Status = IntcDspPathUndoFinish(Streams, Stream);
    if (!NT_SUCCESS(Status))
        return Status;

    Status = Bus->AllocateDmaBufferWithNotification(Bus->Context, Stream->Handle, NotificationCount, Bytes, &Stream->Mdl, &Stream->Bytes,
                                                     &Stream->OffsetFromFirstPage, &StreamId, &Stream->FifoSize);
    if (!NT_SUCCESS(Status))
    {
        IntcDspStreamLog(Streams, "stream %u: the bus gives no buffer of %u bytes (0x%08x)\n", Stream->Tag, (UINT32)Bytes, (UINT32)Status);
        return Status;
    }
    IntcDspTrace(Streams, Stream, INTCDSP_TRACE_BUFFER, 0, 0, (UINT32)Stream->Bytes);
    /* the whole cycle is valid: WaveRT never rewinds, so the software position in bounds stays at the buffer size (plan §5.9) */
    Bus->DSPEnableSPIB(Bus->Context, Stream->Handle, (UINT32)Stream->Bytes);
    IntcDspTrace(Streams, Stream, INTCDSP_TRACE_SPIB, 0, 0, (UINT32)Stream->Bytes);
    Path->Held++;
    if (!Path->Built)
        Status = IntcDspPathBuild(Streams, Stream);
    else if ((Streams->WidgetFlags[Stream->Copier] & INTCDSP_WIDGET_NAMED) == 0)
    {
        /* the copier was initialized before this stream was open: its gateway names the stream now */
        Status = IntcDspNameStream(Streams, Stream, Stream->Copier, Stream->Tag);
        if (NT_SUCCESS(Status))
            Streams->WidgetFlags[Stream->Copier] |= INTCDSP_WIDGET_NAMED;
    }
    if (NT_SUCCESS(Status))
    {
        Stream->State = IntcDspStreamStatePrepared;
        return STATUS_SUCCESS;
    }
    /* a fault on the way released the tag of every stream and forgot the path (the build's own undo may have forced it, R1-04): nothing is left to undo; the buffer this allocate did not hand to its pin is held for nobody, so its pages go back here (R5-01) */
    if (Streams->Faulted)
    {
        IntcDspHeldGiveBack(Streams, IntcDspStreamHandle(Streams, Stream), FALSE);
        return Status;
    }
    Path->Held--;
    if (IntcDspLinkLost(Streams))
    {
        /* the fault released the tag of every stream */
        Status = IntcDspFailed(Streams, Status);
        IntcDspHeldGiveBack(Streams, IntcDspStreamHandle(Streams, Stream), FALSE);
        return Status;
    }
    if (Path->Held == 0 && Path->Built)
    {
        Count = IntcDspPipelineOrder(Streams, Stream->Pcm, FALSE, Order);
        Step = Count != 0xFFFFFFFF ? IntcDspPathTearDownRetried(Streams, Stream, Order, Count) : STATUS_NOT_SUPPORTED;
        if (!NT_SUCCESS(Step))
        {
            if (Step == STATUS_RETRY)
            {
                /* a busy transport is not a fault (FA-03): what the firmware holds stays recorded and the next transition of the PCM deletes it; the buffer goes back and the stream is as an open left it */
                IntcDspStreamLog(Streams, "stream %u: the PCM's pipelines could not be deleted after the failed build (0x%08x): the transport answered busy; the firmware keeps what the state records and the next attempt deletes it\n",
                                 Stream->Tag, (UINT32)Step);
                IntcDspStreamReleaseBuffer(Streams, Stream);
                return Step;
            }
            /* what was built could not be deleted again: nothing of it is kept */
            IntcDspStreamLog(Streams, "stream %u: the PCM's pipelines could not be deleted after the failed build (0x%08x); released as after a fault\n",
                             Stream->Tag, (UINT32)Step);
            Status = IntcDspStuck(Streams, Step);
            IntcDspHeldGiveBack(Streams, IntcDspStreamHandle(Streams, Stream), FALSE);
            return Status;
        }
    }
    IntcDspStreamReleaseBuffer(Streams, Stream);
    IntcDspStreamLog(Streams, "stream %u: not allocated (0x%08x); its buffer is released, the tag is kept for the close\n",
                     Stream->Tag, (UINT32)Status);
    return Status;
}

static
NTSTATUS
IntcDspStreamAllocateLocked(
    _Inout_ PINTCDSP_STREAMS Streams,
    HANDLE Handle,
    SIZE_T Bytes,
    ULONG NotificationCount)
{
    PINTCDSP_STREAM Stream = IntcDspStreamOf(Streams, Handle);
    ULONG FrameBytes;

    if (IntcDspStreamRemoved(Streams, Stream, Handle))
        return STATUS_DEVICE_REMOVED;
    if (Stream == NULL)
        return STATUS_INVALID_HANDLE;
    if (!Streams->Ready || Streams->Faulted)
        return STATUS_DEVICE_NOT_READY;
    /* its free was answered while the transport was busy and the drain met it busy again (N7-01): the buffer is gone, and a new one is not taken until the teardown is done */
    if (Stream->State == IntcDspStreamStateTeardown)
        return STATUS_RETRY;
    if (Stream->State != IntcDspStreamStateOpen)
        return STATUS_INVALID_DEVICE_REQUEST;
    FrameBytes = IntcDspStreamFrameBytes(Stream->StreamFormat.NumberOfChannels, Stream->StreamFormat.ContainerSize);
    if (Bytes == 0 || FrameBytes == 0 || Bytes % FrameBytes != 0 || (NotificationCount != 1 && NotificationCount != 2))
        return STATUS_INVALID_PARAMETER;
    return IntcDspStreamBuffer(Streams, Stream, Bytes, NotificationCount);
}

NTSTATUS
IntcDspStreamAllocate(
    _Inout_ PINTCDSP_STREAMS Streams,
    HANDLE Handle,
    SIZE_T Bytes,
    ULONG NotificationCount)
{
    NTSTATUS Status;

    if (KeGetCurrentIrql() != PASSIVE_LEVEL)
        return STATUS_INVALID_DEVICE_STATE;
    IntcDspStreamsLock(Streams);
    IntcDspStreamsDrain(Streams);
    Status = IntcDspStreamAllocateLocked(Streams, Handle, Bytes, NotificationCount);
    IntcDspStreamsUnlock(Streams);
    return Status;
}

/* The pipelines a start moved to running go back to paused: a start that failed after them */
static
VOID
IntcDspPauseStarted(
    _Inout_ PINTCDSP_STREAMS Streams,
    _In_ const INTCDSP_STREAM* Stream,
    _In_reads_(Count) const UINT8* Order,
    UINT32 Count,
    UINT32 Started)
{
    SOFIPC4_MSG Msg;
    UINT32 I, P;

    for (I = 0; I < Count; I++)
    {
        if ((Started & (1u << I)) == 0)
            continue;
        P = Order[I];
        if (Streams->PipelineRunning[P] != 0)
            continue;
        SofIpc4PipelineSetState(&Msg, Streams->PipelineInstance[P], SOFIPC4_PIPELINE_STATE_PAUSED, 0);
        if (NT_SUCCESS(IntcDspSend(Streams, Stream, SofIpcClassPipeline, &Msg, NULL, 0)))
            Streams->PipelineState[P] = SOFIPC4_PIPELINE_STATE_PAUSED;
    }
}

static
NTSTATUS
IntcDspStreamRunLocked(
    _Inout_ PINTCDSP_STREAMS Streams,
    HANDLE Handle)
{
    PINTCDSP_STREAM Stream = IntcDspStreamOf(Streams, Handle);
    PINTCDSP_PATH Path;
    SOFIPC4_MSG Msg;
    NTSTATUS Status;
    UINT8 Order[INTCDSP_PCM_PIPELINES_MAX];
    UINT32 Count, I, P, Started = 0;

    if (IntcDspStreamRemoved(Streams, Stream, Handle))
        return STATUS_DEVICE_REMOVED;
    if (Stream == NULL)
        return STATUS_INVALID_HANDLE;
    if (!Streams->Ready || Streams->Faulted)
        return STATUS_DEVICE_NOT_READY;
    if (Stream->State == IntcDspStreamStateRunning)
        return STATUS_SUCCESS;
    /* its teardown is still pending after the drain (N7-01): it does not start until the drain is done */
    if (Stream->State == IntcDspStreamStateTeardown)
        return STATUS_RETRY;
    if (Stream->State != IntcDspStreamStatePrepared && Stream->State != IntcDspStreamStatePaused)
        return STATUS_INVALID_DEVICE_REQUEST;
    Path = &Streams->Path[Stream->Pcm];
    /* a stream does not start on a path a busy transport is still undoing (FA-03) */
    if (Path->Undo)
        return STATUS_RETRY;
    Count = IntcDspPipelineOrder(Streams, Stream->Pcm, TRUE, Order);
    if (Count == 0xFFFFFFFF || Count == 0)
        return STATUS_NOT_SUPPORTED;
    Status = STATUS_SUCCESS;
    /* the pipelines are paused, then running, before the host stream runs (the plan's §5.10; a first start pauses first) */
    for (I = 0; I < Count && NT_SUCCESS(Status); I++)
    {
        P = Order[I];
        if (Streams->PipelineState[P] == SOFIPC4_PIPELINE_STATE_UNINITIALIZED || Streams->PipelineState[P] == SOFIPC4_PIPELINE_STATE_RESET)
        {
            SofIpc4PipelineSetState(&Msg, Streams->PipelineInstance[P], SOFIPC4_PIPELINE_STATE_PAUSED, 0);
            Status = IntcDspSend(Streams, Stream, SofIpcClassPipeline, &Msg, NULL, 0);
            if (NT_SUCCESS(Status))
                Streams->PipelineState[P] = SOFIPC4_PIPELINE_STATE_PAUSED;
        }
        if (NT_SUCCESS(Status) && Streams->PipelineState[P] == SOFIPC4_PIPELINE_STATE_PAUSED)
        {
            SofIpc4PipelineSetState(&Msg, Streams->PipelineInstance[P], SOFIPC4_PIPELINE_STATE_RUNNING, 0);
            Status = IntcDspSend(Streams, Stream, SofIpcClassPipeline, &Msg, NULL, 0);
            if (NT_SUCCESS(Status))
            {
                Streams->PipelineState[P] = SOFIPC4_PIPELINE_STATE_RUNNING;
                Started |= 1u << I;
            }
        }
    }
    if (!NT_SUCCESS(Status))
    {
        IntcDspPauseStarted(Streams, Stream, Order, Count, Started);
        IntcDspStreamLog(Streams, "stream %u: not run (0x%08x)\n", Stream->Tag, (UINT32)Status);
        return IntcDspFailed(Streams, Status);
    }
    Status = Streams->Bus->SetDmaEngineState(Streams->Bus->Context, Stream->Handle, RunState);
    if (!NT_SUCCESS(Status))
    {
        IntcDspPauseStarted(Streams, Stream, Order, Count, Started);
        IntcDspStreamLog(Streams, "stream %u: the bus does not run it (0x%08x)\n", Stream->Tag, (UINT32)Status);
        return IntcDspFailed(Streams, Status);
    }
    Path->Running++;
    for (I = 0; I < Count; I++)
        Streams->PipelineRunning[Order[I]]++;
    Stream->State = IntcDspStreamStateRunning;
    IntcDspTrace(Streams, Stream, INTCDSP_TRACE_RUN, 0, 0, RunState);
    return STATUS_SUCCESS;
}

NTSTATUS
IntcDspStreamRun(
    _Inout_ PINTCDSP_STREAMS Streams,
    HANDLE Handle)
{
    NTSTATUS Status;

    if (KeGetCurrentIrql() != PASSIVE_LEVEL)
        return STATUS_INVALID_DEVICE_STATE;
    IntcDspStreamsLock(Streams);
    IntcDspStreamsDrain(Streams);
    Status = IntcDspStreamRunLocked(Streams, Handle);
    IntcDspStreamsUnlock(Streams);
    return Status;
}

static
NTSTATUS
IntcDspStreamPauseLocked(
    _Inout_ PINTCDSP_STREAMS Streams,
    HANDLE Handle)
{
    PINTCDSP_STREAM Stream = IntcDspStreamOf(Streams, Handle);
    PINTCDSP_PATH Path;
    SOFIPC4_MSG Msg;
    NTSTATUS Status = STATUS_SUCCESS;
    UINT8 Order[INTCDSP_PCM_PIPELINES_MAX];
    UINT32 Count, I, P;

    if (IntcDspStreamRemoved(Streams, Stream, Handle))
        return STATUS_DEVICE_REMOVED;
    if (Stream == NULL)
        return STATUS_INVALID_HANDLE;
    if (!Streams->Ready || Streams->Faulted)
        return STATUS_DEVICE_NOT_READY;
    /* a stream whose teardown is pending is stopped already, the bus's stream having stopped at its free (N7-01) */
    if (Stream->State == IntcDspStreamStatePrepared || Stream->State == IntcDspStreamStatePaused || Stream->State == IntcDspStreamStateTeardown)
        return STATUS_SUCCESS;
    if (Stream->State != IntcDspStreamStateRunning)
        return STATUS_INVALID_DEVICE_REQUEST;
    Path = &Streams->Path[Stream->Pcm];
    Count = IntcDspPipelineOrder(Streams, Stream->Pcm, FALSE, Order);
    Path->Running--;
    /* a pipeline no stream of the PCM runs on any more is paused, the sources first; then the stream stops (the plan's §5.9) */
    for (I = 0; I < Count && Count != 0xFFFFFFFF && NT_SUCCESS(Status); I++)
    {
        P = Order[I];
        if (Streams->PipelineRunning[P] == 0)
            continue;
        Streams->PipelineRunning[P]--;
        if (Streams->PipelineRunning[P] != 0 || Streams->PipelineState[P] != SOFIPC4_PIPELINE_STATE_RUNNING)
            continue;
        SofIpc4PipelineSetState(&Msg, Streams->PipelineInstance[P], SOFIPC4_PIPELINE_STATE_PAUSED, 0);
        Status = IntcDspSend(Streams, Stream, SofIpcClassPipeline, &Msg, NULL, 0);
        if (NT_SUCCESS(Status))
            Streams->PipelineState[P] = SOFIPC4_PIPELINE_STATE_PAUSED;
    }
    Streams->Bus->SetDmaEngineState(Streams->Bus->Context, Stream->Handle, PauseState);
    IntcDspTrace(Streams, Stream, INTCDSP_TRACE_STOP, 0, 0, PauseState);
    Stream->State = IntcDspStreamStatePaused;
    if (!NT_SUCCESS(Status))
        IntcDspStreamLog(Streams, "stream %u: a pipeline could not be paused (0x%08x)\n", Stream->Tag, (UINT32)Status);
    return IntcDspFailed(Streams, Status);
}

NTSTATUS
IntcDspStreamPause(
    _Inout_ PINTCDSP_STREAMS Streams,
    HANDLE Handle)
{
    NTSTATUS Status;

    if (KeGetCurrentIrql() != PASSIVE_LEVEL)
        return STATUS_INVALID_DEVICE_STATE;
    IntcDspStreamsLock(Streams);
    IntcDspStreamsDrain(Streams);
    Status = IntcDspStreamPauseLocked(Streams, Handle);
    IntcDspStreamsUnlock(Streams);
    return Status;
}

/*
 * The firmware-side release of a stream that lets go of its buffer (the
 * free, and the drain of a pending teardown, N7-01): the undo a path still
 * owes is finished first, then, while another stream holds the PCM, the
 * copier's gateway is told the stream is going, and the stream lets go of
 * its hold; the last holder's pipelines are reset, the instances and the
 * pipeline deleted (the plan's §5.10). The stream's hold is released only
 * once the copier step passed, so a busy answer leaves the PCM as it was.
 * Busy names the step that met it. STATUS_RETRY: the transport answered
 * busy and what the firmware holds stays recorded; any other failure: the
 * undo's own (the forced release may have run, IntcDspStuck) or the
 * copier's and the teardown's.
 */
static
NTSTATUS
IntcDspStreamReleaseFirmware(
    _Inout_ PINTCDSP_STREAMS Streams,
    _Inout_ PINTCDSP_STREAM Stream,
    _Out_opt_ const char** Busy)
{
    PINTCDSP_PATH Path = &Streams->Path[Stream->Pcm];
    NTSTATUS Status;
    UINT8 Order[INTCDSP_PCM_PIPELINES_MAX];
    UINT32 Count, Try;

    if (Busy != NULL)
        *Busy = NULL;
    /* a path whose undo a busy transport cut short is finished before the stream lets go of its hold (FA-03) */
    Status = IntcDspPathUndoFinish(Streams, Stream);
    if (!NT_SUCCESS(Status))
    {
        if (Busy != NULL && Status == STATUS_RETRY)
            *Busy = "the undo of the PCM's pipelines";
        return Status;
    }
    Count = IntcDspPipelineOrder(Streams, Stream->Pcm, FALSE, Order);
    /* while another stream holds the PCM, the copier's gateway must not keep naming a stream that goes back to the pool */
    if (Path->Held > 1 && (Streams->WidgetFlags[Stream->Copier] & INTCDSP_WIDGET_NAMED) != 0)
    {
        Status = IntcDspNameStream(Streams, Stream, Stream->Copier, SOFTPLG_STREAM_TAG_NONE);
        for (Try = 1; Try < INTCDSP_UNDO_TRIES && Status == STATUS_RETRY; Try++)
            Status = IntcDspNameStream(Streams, Stream, Stream->Copier, SOFTPLG_STREAM_TAG_NONE);
        if (!NT_SUCCESS(Status))
        {
            if (Status == STATUS_RETRY)
            {
                /* a busy transport is not a fault (FA-03): the copier still names the stream, and the PCM is as it was */
                if (Busy != NULL)
                    *Busy = "the copier's renaming";
                return Status;
            }
            /* the copier still names a stream that goes back to the pool: nothing is kept */
            IntcDspStreamLog(Streams, "stream %u: its copier could not be told the stream is going (0x%08x); released as after a fault\n",
                             Stream->Tag, (UINT32)Status);
            return IntcDspStuck(Streams, Status);
        }
        Streams->WidgetFlags[Stream->Copier] &= (UINT8)~INTCDSP_WIDGET_NAMED;
    }
    Path->Held--;
    /* the last holder: the pipeline is reset, the instances and the pipeline deleted (the plan's §5.10) */
    if (Path->Held == 0 && Path->Built)
    {
        Status = Count != 0xFFFFFFFF ? IntcDspPathTearDownRetried(Streams, Stream, Order, Count) : STATUS_NOT_SUPPORTED;
        if (!NT_SUCCESS(Status))
        {
            Path->Held++;
            if (Status == STATUS_RETRY)
            {
                /* a busy transport is not a fault (FA-03): what the firmware holds stays recorded, and the stream keeps its hold */
                if (Busy != NULL)
                    *Busy = "the pipelines' teardown";
                return Status;
            }
            IntcDspStreamLog(Streams, "stream %u: the PCM's pipelines could not be deleted (0x%08x); released as after a fault\n", Stream->Tag,
                             (UINT32)Status);
            return IntcDspStuck(Streams, Status);
        }
    }
    return STATUS_SUCCESS;
}

/*
 * A free whose firmware-side release met a busy transport (N7-01): the pin
 * is answered STATUS_SUCCESS and forgets its buffer, so the manager takes
 * it. Everything that needs no request is done now: the mapping dropped,
 * the software bound off, the buffer detached from the bus's stream and kept
 * under the handle's word for the manager, which returns it at the drain (a
 * full held table leaves it allocated, the safe side, as for a pin). The
 * tag, the hold on the PCM and the copier's naming stay as recorded until
 * the drain sends the release again. PauseBusy: the pause before the
 * release met the busy transport too.
 */
static
NTSTATUS
IntcDspStreamTeardownPending(
    _Inout_ PINTCDSP_STREAMS Streams,
    _Inout_ PINTCDSP_STREAM Stream,
    _In_z_ const char* Busy,
    BOOLEAN PauseBusy)
{
    IntcDspStreamHoldBuffer(Streams, Stream);
    Stream->State = IntcDspStreamStateTeardown;
    Stream->Orphaned = FALSE;
    if (PauseBusy)
        IntcDspStreamLog(Streams, "stream %u: its free met a busy transport at the pause of its pipelines and at %s; the buffer is held for the manager and the teardown is pending\n",
                         Stream->Tag, Busy);
    else
        IntcDspStreamLog(Streams, "stream %u: its free met a busy transport at %s; the buffer is held for the manager and the teardown is pending\n",
                         Stream->Tag, Busy);
    return STATUS_SUCCESS;
}

/*
 * The pending teardown of a stream, finished (N7-01): the firmware-side
 * release is sent again. Busy, it is left pending, as it was; a fault's
 * release let the stream go and returned its buffer (IntcDspStreamsLetGo);
 * done, the held buffer is returned, the tag goes back to the pool, the PCM
 * forgets the stream, and the object is freed when its pin closed it, or
 * left for its close as a released stream when its pin still lives.
 */
static
NTSTATUS
IntcDspStreamTeardownDrain(
    _Inout_ PINTCDSP_STREAMS Streams,
    _Inout_ PINTCDSP_STREAM Stream)
{
    NTSTATUS Status;

    Status = IntcDspStreamReleaseFirmware(Streams, Stream, NULL);
    if (!NT_SUCCESS(Status))
        return Status;
    IntcDspHeldGiveBack(Streams, IntcDspStreamHandle(Streams, Stream), FALSE);
    Streams->Path[Stream->Pcm].Open--;
    IntcDspStreamLetGo(Streams, Stream);
    IntcDspStreamsIdleClosedLocked(Streams);
    Stream->State = Stream->Orphaned ? IntcDspStreamStateFree : IntcDspStreamStateReleased;
    IntcDspStreamLog(Streams, "stream %u: its pending teardown is done; the buffer is returned and the tag is back, %s\n",
                     Stream->Tag, Stream->Orphaned ? "its pin having closed it" : "the stream left for its pin's close");
    return STATUS_SUCCESS;
}

/*
 * The teardowns pending, finished before a transition's own work (N7-01),
 * under the lock the transition holds: the prologue of every public
 * transition. One that meets a busy transport again is left pending; one
 * whose release faults the link ends in the release, and the transition then
 * finds the manager faulted and refuses.
 */
static
VOID
IntcDspStreamsDrain(
    _Inout_ PINTCDSP_STREAMS Streams)
{
    UINT32 S;

    if (!Streams->Ready || Streams->Faulted)
        return;
    for (S = 0; S < INTCDSP_STREAMS_MAX; S++)
        if (Streams->Stream[S].State == IntcDspStreamStateTeardown)
            IntcDspStreamTeardownDrain(Streams, &Streams->Stream[S]);
}

static
NTSTATUS
IntcDspStreamFreeLocked(
    _Inout_ PINTCDSP_STREAMS Streams,
    HANDLE Handle)
{
    PINTCDSP_STREAM Stream = IntcDspStreamOf(Streams, Handle);
    NTSTATUS Status;
    const char* Busy = NULL;
    BOOLEAN PauseBusy = FALSE;

    if (IntcDspStreamRemoved(Streams, Stream, Handle))
        return STATUS_DEVICE_REMOVED;
    if (Stream == NULL)
        return STATUS_INVALID_HANDLE;
    if (!Streams->Ready || Streams->Faulted)
        return STATUS_DEVICE_NOT_READY;
    if (Stream->State == IntcDspStreamStateOpen)
        return STATUS_SUCCESS;
    /* asked again while the teardown is pending (N7-01): answered again, the prologue having sent the release */
    if (Stream->State == IntcDspStreamStateTeardown)
        return STATUS_SUCCESS;
    if (Stream->State == IntcDspStreamStateRunning)
    {
        Status = IntcDspStreamPauseLocked(Streams, Handle);
        if (!NT_SUCCESS(Status) && Status != STATUS_RETRY)
        {
            /* a fault's release held the buffer for a pin that forgets it with this answer (N7-01): the manager returns it */
            IntcDspHeldGiveBack(Streams, Handle, TRUE);
            return Status;
        }
        /* a busy pause stopped the bus's stream, and the release goes on: a busy answer again below makes the teardown pending */
        PauseBusy = Status == STATUS_RETRY;
    }
    if (Stream->State != IntcDspStreamStatePrepared && Stream->State != IntcDspStreamStatePaused)
        return STATUS_INVALID_DEVICE_REQUEST;
    Status = IntcDspStreamReleaseFirmware(Streams, Stream, &Busy);
    if (Status == STATUS_RETRY)
        return IntcDspStreamTeardownPending(Streams, Stream, Busy != NULL ? Busy : "the release", PauseBusy);
    if (!NT_SUCCESS(Status))
    {
        /* the pin's own free asked for the buffer to go and forgets the MDL whatever the answer: the forced release held it, and it is returned here (R5-01) */
        IntcDspHeldGiveBack(Streams, Handle, TRUE);
        return Status;
    }
    IntcDspStreamReleaseBuffer(Streams, Stream);
    Stream->State = IntcDspStreamStateOpen;
    return STATUS_SUCCESS;
}

NTSTATUS
IntcDspStreamFree(
    _Inout_ PINTCDSP_STREAMS Streams,
    HANDLE Handle)
{
    NTSTATUS Status;
    PINTCDSP_HELD Held;

    if (KeGetCurrentIrql() != PASSIVE_LEVEL)
        return STATUS_INVALID_DEVICE_STATE;
    IntcDspStreamsLock(Streams);
    IntcDspStreamsDrain(Streams);
    /* the free of a buffer held for its pin (R5-01): answered before anything of the manager's state is asked, so it works after a power-down, after a fault and after the release of the hardware; a second free of the word finds nothing */
    Held = IntcDspHeldOf(Streams, Handle);
    if (Held != NULL)
    {
        IntcDspHeldRelease(Streams, Held, TRUE);
        IntcDspStreamsUnlock(Streams);
        return STATUS_SUCCESS;
    }
    Status = IntcDspStreamFreeLocked(Streams, Handle);
    IntcDspStreamsUnlock(Streams);
    return Status;
}

static
NTSTATUS
IntcDspStreamCloseLocked(
    _Inout_ PINTCDSP_STREAMS Streams,
    HANDLE Handle)
{
    PINTCDSP_STREAM Stream = IntcDspStreamOf(Streams, Handle);

    /* a handle of an older generation (E1, D4): the pin closes what it held of a device that let it go, and is answered as the close of a released stream, nothing of the object its index names now being touched */
    if (IntcDspStreamStale(Streams, Handle))
        return STATUS_SUCCESS;
    if (Stream == NULL || Stream->State == IntcDspStreamStateFree)
        return STATUS_INVALID_HANDLE;
    /* a close while the teardown is pending is accepted (N7-01): the prologue sent the release again and the transport answered busy; the pin is gone, and the next drain frees the object */
    if (Stream->State == IntcDspStreamStateTeardown)
    {
        Stream->Orphaned = TRUE;
        return STATUS_SUCCESS;
    }
    if (Stream->State != IntcDspStreamStateOpen && Stream->State != IntcDspStreamStateReleased)
        return STATUS_INVALID_DEVICE_REQUEST;
    if (Stream->State == IntcDspStreamStateOpen)
    {
        Streams->Path[Stream->Pcm].Open--;
        IntcDspStreamLetGo(Streams, Stream);
        IntcDspStreamsIdleClosedLocked(Streams);
    }
    Stream->State = IntcDspStreamStateFree;
    return STATUS_SUCCESS;
}

NTSTATUS
IntcDspStreamClose(
    _Inout_ PINTCDSP_STREAMS Streams,
    HANDLE Handle)
{
    NTSTATUS Status;

    if (KeGetCurrentIrql() != PASSIVE_LEVEL)
        return STATUS_INVALID_DEVICE_STATE;
    IntcDspStreamsLock(Streams);
    IntcDspStreamsDrain(Streams);
    Status = IntcDspStreamCloseLocked(Streams, Handle);
    IntcDspStreamsUnlock(Streams);
    return Status;
}

NTSTATUS
IntcDspStreamPositionRegister(
    _In_ const INTCDSP_STREAMS* Streams,
    HANDLE Handle,
    _Out_ PULONG* Position)
{
    PINTCDSP_STREAM Stream = IntcDspStreamOf(Streams, Handle);

    *Position = NULL;
    if (IntcDspStreamRemoved(Streams, Stream, Handle))
        return STATUS_DEVICE_REMOVED;
    if (Stream == NULL || Stream->Handle == NULL)
        return STATUS_INVALID_HANDLE;
    if (!INTCDSP_BUS_HAS(Streams->Bus, GetPositionRegister))
        return STATUS_NOT_SUPPORTED;
    return Streams->Bus->GetPositionRegister(Streams->Bus->Context, Stream->Handle, Position);
}

NTSTATUS
IntcDspStreamLinkPositionRegister(
    _In_ const INTCDSP_STREAMS* Streams,
    HANDLE Handle,
    _Out_ PULONG* Position)
{
    PINTCDSP_STREAM Stream = IntcDspStreamOf(Streams, Handle);

    *Position = NULL;
    if (IntcDspStreamRemoved(Streams, Stream, Handle))
        return STATUS_DEVICE_REMOVED;
    if (Stream == NULL || Stream->Handle == NULL)
        return STATUS_INVALID_HANDLE;
    if (!INTCDSP_BUS_HAS(Streams->Bus, GetLinkPositionRegister))
        return STATUS_NOT_SUPPORTED;
    return Streams->Bus->GetLinkPositionRegister(Streams->Bus->Context, Stream->Handle, Position);
}

NTSTATUS
IntcDspStreamClockRegister(
    _In_ const INTCDSP_STREAMS* Streams,
    _Out_ PULONG* Wallclock)
{
    *Wallclock = NULL;
    if (!INTCDSP_BUS_HAS(Streams->Bus, GetWallClockRegister))
        return STATUS_NOT_SUPPORTED;
    Streams->Bus->GetWallClockRegister(Streams->Bus->Context, Wallclock);
    return *Wallclock != NULL ? STATUS_SUCCESS : STATUS_NO_SUCH_DEVICE;
}

UINT32
IntcDspStreamPosition(
    _In_ const INTCDSP_STREAMS* Streams,
    HANDLE Handle)
{
    PINTCDSP_STREAM Stream = IntcDspStreamOf(Streams, Handle);

    if (Stream == NULL || Stream->Handle == NULL)
        return 0;
    return Streams->Bus->StreamPosition(Streams->Bus->Context, Stream->Handle);
}

static
NTSTATUS
IntcDspStreamRegisterEventLocked(
    _Inout_ PINTCDSP_STREAMS Streams,
    HANDLE Handle,
    _In_ PKEVENT NotificationEvent)
{
    PINTCDSP_STREAM Stream = IntcDspStreamOf(Streams, Handle);
    NTSTATUS Status;

    if (Stream == NULL || Stream->Handle == NULL)
        return STATUS_INVALID_HANDLE;
    Status = Streams->Bus->RegisterNotificationEvent(Streams->Bus->Context, Stream->Handle, NotificationEvent);
    if (NT_SUCCESS(Status))
        Stream->Events++;
    return Status;
}

NTSTATUS
IntcDspStreamRegisterEvent(
    _Inout_ PINTCDSP_STREAMS Streams,
    HANDLE Handle,
    _In_ PKEVENT NotificationEvent)
{
    NTSTATUS Status;

    if (KeGetCurrentIrql() != PASSIVE_LEVEL)
        return STATUS_INVALID_DEVICE_STATE;
    IntcDspStreamsLock(Streams);
    Status = IntcDspStreamRegisterEventLocked(Streams, Handle, NotificationEvent);
    IntcDspStreamsUnlock(Streams);
    return Status;
}

static
NTSTATUS
IntcDspStreamUnregisterEventLocked(
    _Inout_ PINTCDSP_STREAMS Streams,
    HANDLE Handle,
    _In_ PKEVENT NotificationEvent)
{
    PINTCDSP_STREAM Stream = IntcDspStreamOf(Streams, Handle);
    NTSTATUS Status;

    if (Stream == NULL || Stream->Handle == NULL)
        return STATUS_INVALID_HANDLE;
    Status = Streams->Bus->UnregisterNotificationEvent(Streams->Bus->Context, Stream->Handle, NotificationEvent);
    if (NT_SUCCESS(Status) && Stream->Events != 0)
        Stream->Events--;
    return Status;
}

NTSTATUS
IntcDspStreamUnregisterEvent(
    _Inout_ PINTCDSP_STREAMS Streams,
    HANDLE Handle,
    _In_ PKEVENT NotificationEvent)
{
    NTSTATUS Status;

    if (KeGetCurrentIrql() != PASSIVE_LEVEL)
        return STATUS_INVALID_DEVICE_STATE;
    IntcDspStreamsLock(Streams);
    Status = IntcDspStreamUnregisterEventLocked(Streams, Handle, NotificationEvent);
    IntcDspStreamsUnlock(Streams);
    return Status;
}

/* The depths the self-test runs at, in valid bits; 24 lives in a 32-bit container */
static const ULONG IntcDspSelfTestDepths[] = { 16, 24, 32 };
#define INTCDSP_SELFTEST_DEPTHS        3
/* The periods a run waits for: the two halves of the buffer */
#define INTCDSP_SELFTEST_PERIODS       2
/* The capture buffer before the stream runs: a value no sample of the sine is */
#define INTCDSP_SELFTEST_POISON        0xCDCDCDCDu

/*
 * A 1 kHz sine at 48000 Hz, one period of 48 frames, in Q31: the sample of
 * frame N is IntcDspSelfTestSine()[N % 48], scaled to the depth. The numbers
 * are this driver's own (round((2^31 - 1) sin(2 pi k / 48))).
 */
static const INT32 IntcDspSineQ31[INTCDSP_SELFTEST_PERIOD] =
{
             0,    280302863,    555809667,    821806413,   1073741823,   1307305214,
    1518500249,   1703713325,   1859775393,   1984016188,   2074309916,   2129111627,
    2147483647,   2129111627,   2074309916,   1984016188,   1859775393,   1703713325,
    1518500249,   1307305214,   1073741823,    821806413,    555809667,    280302863,
             0,   -280302863,   -555809667,   -821806413,  -1073741823,  -1307305214,
   -1518500249,  -1703713325,  -1859775393,  -1984016188,  -2074309916,  -2129111627,
   -2147483647,  -2129111627,  -2074309916,  -1984016188,  -1859775393,  -1703713325,
   -1518500249,  -1307305214,  -1073741824,   -821806413,   -555809667,   -280302863
};

const INT32*
IntcDspSelfTestSine(
    VOID)
{
    return IntcDspSineQ31;
}

VOID
IntcDspSelfTestFill(
    _Out_writes_bytes_(Bytes) PUCHAR Buffer,
    SIZE_T Bytes,
    ULONG Channels,
    ULONG ValidBits,
    ULONG ContainerBits)
{
    /* the amplitude is a quarter of full scale: a 16-bit sample is the Q31 one shifted by 18, a 32-bit container's by 2 */
    const UINT32 Shift = ValidBits <= 16 ? 18 : 2;
    SIZE_T Frame;
    ULONG Channel;
    INT32 Sample;

    for (Frame = 0; (Frame + 1) * Channels * (ContainerBits / 8) <= Bytes; Frame++)
    {
        for (Channel = 0; Channel < Channels; Channel++)
        {
            Sample = IntcDspSineQ31[Frame % INTCDSP_SELFTEST_PERIOD] >> Shift;
            if (ContainerBits == 16)
                *(INT16*)(Buffer + (Frame * Channels + Channel) * 2) = (INT16)Sample;
            else
                *(INT32*)(Buffer + (Frame * Channels + Channel) * 4) = Sample;
        }
    }
}

static
VOID
IntcDspSelfTestPoison(
    _Out_writes_bytes_(Bytes) PUCHAR Buffer,
    SIZE_T Bytes,
    ULONG ContainerBits)
{
    SIZE_T I;

    if (ContainerBits == 16)
    {
        for (I = 0; I < Bytes / 2; I++)
            ((INT16*)Buffer)[I] = (INT16)INTCDSP_SELFTEST_POISON;
    }
    else
    {
        for (I = 0; I < Bytes / 4; I++)
            ((INT32*)Buffer)[I] = (INT32)INTCDSP_SELFTEST_POISON;
    }
}

/* The sample of a channel in a frame of a buffer; a frame past the end is the buffer's frame that far around again */
static
INT32
IntcDspSample(
    _In_reads_bytes_(Frames * FrameBytes) const PUCHAR Buffer,
    SIZE_T Frames,
    ULONG FrameBytes,
    ULONG Width,
    SIZE_T Frame,
    ULONG Channel)
{
    while (Frame >= Frames)
        Frame -= Frames;
    if (Width == 2)
        return *(const INT16*)(Buffer + Frame * FrameBytes + Channel * Width);
    return *(const INT32*)(Buffer + Frame * FrameBytes + Channel * Width);
}

/* The largest integer whose square is at most Value */
static
UINT64
IntcDspSqrt(
    UINT64 Value)
{
    UINT64 Root = 0, Bit = 1ULL << 62;

    while (Bit > Value)
        Bit >>= 2;
    while (Bit != 0)
    {
        if (Root + Bit <= Value)
        {
            Value -= Root + Bit;
            Root = (Root >> 1) + Bit;
        }
        else
            Root >>= 1;
        Bit >>= 2;
    }
    return Root;
}

/*
 * One sample's square, added to the mean square of Count samples: the
 * running total is kept as its quotient by Count and the remainder below
 * Count, which is the sum of the squares divided once, exactly, although
 * that sum would not fit a 64-bit integer (a 32-bit sample's square is 62
 * bits). A square plus a remainder below Count, at most 2^32, stays below
 * 2^63.
 */
static
VOID
IntcDspMeanAdd(
    UINT64 Square,
    UINT64 Count,
    _Inout_ PUINT64 Mean,
    _Inout_ PUINT64 Rest)
{
    UINT64 Total = *Rest + Square;

    *Mean += Total / Count;
    *Rest = Total % Count;
}

/*
 * The RMS of Count samples whose mean square is Mean, in thousandths of a
 * sample unit and exact: the thousandth of its square root is taken from a
 * 64-bit split, as (sqrt(q) 10^6 + d)^2 stays below (q 10^6 + r) 10^6 while
 * 2 sqrt(q) 10^6 d + d^2 stays below R.
 */
static
UINT64
IntcDspRms(
    UINT64 Sum,
    UINT64 Count)
{
    UINT64 Q, R, S, D, Root;

    if (Count == 0)
        return 0;
    Q = Sum / 1000000;
    Root = IntcDspSqrt(Q);
    R = (Q - Root * Root) * 1000000000000ULL + (Sum % 1000000) * 1000000ULL;
    S = Root * 1000000ULL;
    D = S == 0 ? IntcDspSqrt(R) : R / (2 * S);
    while (2 * S * D + D * D > R)
        D--;
    return S + D;
}

VOID
IntcDspStreamCompare(
    _In_reads_bytes_(Bytes) const PUCHAR Render,
    _In_reads_bytes_(Bytes) const PUCHAR Capture,
    SIZE_T Bytes,
    ULONG Channels,
    ULONG ContainerBits,
    UINT32 StopBytes,
    _Out_ PINTCDSP_COMPARE Compare)
{
    ULONG Width = ContainerBits / 8, FrameBytes = Channels * Width;
    SIZE_T Frames = FrameBytes != 0 ? Bytes / FrameBytes : 0, Shift = 0, I;
    ULONG Channel;
    UINT32 Offset, BestOffset = 0;
    UINT64 Differing, BestDiffering = 0, MaxError, BestMaxError = 0;
    /* the mean squares: each as its whole part and the remainder of the running total */
    UINT64 Samples, RenderMean = 0, RenderRest = 0, CaptureMean, CaptureRest, BestCaptureMean = 0;
    UINT64 BestFrame = 0;
    ULONG BestChannel = 0;
    INT32 RenderSample, CaptureSample, BestRender = 0, BestCapture = 0;
    INT64 Error;

    RtlZeroMemory(Compare, sizeof(*Compare));
    if (Frames == 0 || Channels == 0 || (Width != 2 && Width != 4))
        return;
    Samples = (UINT64)Frames * Channels;
    /* the capture stream stood Shift frames into its buffer when it stopped: the compare starts there and goes around the cyclic buffer */
    Shift = (StopBytes % (UINT32)Bytes) / FrameBytes;
    Shift %= Frames;
    /* the RMS of the whole render buffer */
    for (I = 0; I < Frames; I++)
    {
        for (Channel = 0; Channel < Channels; Channel++)
        {
            RenderSample = IntcDspSample(Render, Frames, FrameBytes, Width, I, Channel);
            IntcDspMeanAdd((UINT64)((INT64)RenderSample * RenderSample), Samples, &RenderMean, &RenderRest);
        }
    }
    /*
     * The first offset at which the capture is the render: the capture frame
     * Offset frames after the stop, around the cyclic buffer, is the render
     * buffer's first frame, the pipeline's latency. Every offset compares
     * the whole buffer, so a differing sample is seen wherever it lies; a
     * period of the sine makes every 48th offset match too, so the smallest
     * is taken, and with none the first offset with the fewest is reported.
     */
    for (Offset = 0; Offset < INTCDSP_COMPARE_OFFSETS && Offset < Frames; Offset++)
    {
        Differing = 0;
        MaxError = 0;
        CaptureMean = 0;
        CaptureRest = 0;
        for (I = 0; I < Frames; I++)
        {
            for (Channel = 0; Channel < Channels; Channel++)
            {
                RenderSample = IntcDspSample(Render, Frames, FrameBytes, Width, I, Channel);
                CaptureSample = IntcDspSample(Capture, Frames, FrameBytes, Width, Shift + Offset + I, Channel);
                IntcDspMeanAdd((UINT64)((INT64)CaptureSample * CaptureSample), Samples, &CaptureMean, &CaptureRest);
                if (RenderSample == CaptureSample)
                    continue;
                Error = (INT64)CaptureSample - RenderSample;
                if (Error < 0)
                    Error = -Error;
                if ((UINT64)Error > MaxError)
                    MaxError = (UINT64)Error;
                if (Differing == 0)
                {
                    BestFrame = I;
                    BestChannel = Channel;
                    BestRender = RenderSample;
                    BestCapture = CaptureSample;
                }
                Differing++;
            }
        }
        if (Differing != 0 && Offset != 0 && Differing >= BestDiffering)
            continue;
        BestDiffering = Differing;
        BestMaxError = MaxError;
        BestOffset = Offset;
        BestCaptureMean = CaptureMean;
        Compare->DifferingFrame = (UINT32)BestFrame;
        Compare->DifferingChannel = BestChannel;
        Compare->RenderSample = BestRender;
        Compare->CaptureSample = BestCapture;
        if (Differing == 0)
            break;
    }
    Compare->Offset = BestOffset;
    Compare->Samples = (UINT64)Frames * Channels;
    Compare->Differing = BestDiffering;
    Compare->MaxError = BestMaxError;
    Compare->RenderRms = IntcDspRms(RenderMean, Compare->Samples);
    Compare->CaptureRms = IntcDspRms(BestCaptureMean, Compare->Samples);
    Compare->Matched = BestDiffering == 0;
}

static
BOOLEAN
IntcDspHasClock(
    _In_ const INTCDSP_STREAMS* Streams)
{
    return Streams->Ipc != NULL && Streams->Ipc->Fw != NULL && Streams->Ipc->Fw->Ops != NULL &&
        Streams->Ipc->Fw->Ops->NowUs != NULL;
}

static
UINT64
IntcDspNow(
    _In_ const INTCDSP_STREAMS* Streams)
{
    if (!IntcDspHasClock(Streams))
        return 0;
    return Streams->Ipc->Fw->Ops->NowUs(Streams->Ipc->Fw->OpsContext);
}

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
    _Out_ PULONG Periods)
{
    PINTCDSP_STREAM Managed = IntcDspStreamOf(Streams, Stream);
    ULONG Bytes, Half, Last, Position, Moved = 0;
    UINT64 StartUs, ElapsedMs;
    LARGE_INTEGER Timeout;
    NTSTATUS Status;

    *Periods = 0;
    if (Managed == NULL || Managed->Handle == NULL || Managed->Bytes == 0 || Count == 0)
        return STATUS_INVALID_PARAMETER;
    Bytes = (ULONG)Managed->Bytes;
    Half = Bytes / Count;
    KeClearEvent(Event);
    Status = IntcDspStreamRegisterEvent(Streams, Stream, Event);
    if (!NT_SUCCESS(Status))
        return Status;
    StartUs = IntcDspNow(Streams);
    Last = Streams->Bus->StreamPosition(Streams->Bus->Context, Managed->Handle) % Bytes;
    Status = STATUS_IO_TIMEOUT;
    for (;;)
    {
        Position = Streams->Bus->StreamPosition(Streams->Bus->Context, Managed->Handle) % Bytes;
        Moved += (Position - Last + Bytes) % Bytes;
        Last = Position;
        if (Moved >= Half * Count)
        {
            Status = STATUS_SUCCESS;
            break;
        }
        ElapsedMs = (IntcDspNow(Streams) - StartUs) / 1000;
        if (ElapsedMs >= TimeoutMs)
            break;
        Timeout.QuadPart = -((LONGLONG)(TimeoutMs - (ULONG)ElapsedMs) * 10000);
        Status = KeWaitForSingleObject(Event, Executive, KernelMode, FALSE, &Timeout);
        KeClearEvent(Event);
        if (Status == STATUS_TIMEOUT)
        {
            Status = STATUS_IO_TIMEOUT;
            break;
        }
    }
    Position = Streams->Bus->StreamPosition(Streams->Bus->Context, Managed->Handle) % Bytes;
    Moved += (Position - Last + Bytes) % Bytes;
    IntcDspStreamUnregisterEvent(Streams, Stream, Event);
    *Periods = Moved / Half > Count ? Count : Moved / Half;
    return Status;
}

/* The value of a register the bus handed out, 0 when it gave none */
static
UINT32
IntcDspRegisterValue(
    _In_ const INTCDSP_STREAMS* Streams,
    HANDLE Handle,
    BOOLEAN Extended)
{
    PULONG Register = NULL;
    NTSTATUS Status = Extended ? IntcDspStreamPositionRegister(Streams, Handle, &Register)
                                : IntcDspStreamLinkPositionRegister(Streams, Handle, &Register);

    return NT_SUCCESS(Status) && Register != NULL ? *Register : 0;
}

/*
 * One depth of the self-test: the status of the step that failed, or
 * STATUS_DEVICE_PROTOCOL_ERROR when the capture does not match, after the
 * stop rule's line. Everything the run took is given back on every way out.
 */
static
NTSTATUS
IntcDspSelfTestRun(
    _Inout_ PINTCDSP_STREAMS Streams,
    _In_ const INTCDSP_SELFTEST_OPS* Ops,
    UINT32 Pcm,
    ULONG ValidBits,
    UINT32 Frames)
{
    HANDLE Render = NULL, Capture = NULL;
    PINTCDSP_STREAM RenderStream, CaptureStream;
    PVOID RenderVa = NULL, CaptureVa = NULL;
    INTCDSP_COMPARE Compare;
    NTSTATUS Status, Step;
    ULONG ContainerBits, FrameBytes, Periods = 0, TimeoutMs;
    UINT64 StartUs = 0;
    UINT32 ElapsedMs = 0, CaptureStop = 0, RenderFrame, FrameCount = Frames;
    UINT32 RenderPosition = 0, CapturePosition = 0, RenderLpib = 0, CaptureLpib = 0, RenderBuffer = 0, CaptureBuffer = 0;
    UINT32 Freed0, InstanceDeletes0, PipelineDeletes0, Pipelines, Instances, Freed;

    RtlZeroMemory(&Compare, sizeof(Compare));
    ContainerBits = ValidBits <= 16 ? 16 : 32;
    FrameBytes = IntcDspStreamFrameBytes(INTCDSP_SELFTEST_CHANNELS, ContainerBits);
    /* the periods are waited for at twice the time the buffer takes at the stream's rate, and half a second more */
    TimeoutMs = (ULONG)(((UINT64)Frames * 2000) / INTCDSP_SELFTEST_RATE) + 500;
    Status = IntcDspStreamOpen(Streams, Pcm, FALSE, INTCDSP_SELFTEST_RATE, INTCDSP_SELFTEST_CHANNELS, ValidBits, ContainerBits, &Render);
    if (NT_SUCCESS(Status))
        Status = IntcDspStreamOpen(Streams, Pcm, TRUE, INTCDSP_SELFTEST_RATE, INTCDSP_SELFTEST_CHANNELS, ValidBits, ContainerBits, &Capture);
    if (!NT_SUCCESS(Status))
        goto Release;
    RenderStream = IntcDspStreamOf(Streams, Render);
    CaptureStream = IntcDspStreamOf(Streams, Capture);
    IntcDspStreamLog(Streams, "loopback self-test: endpoint '%s', %u Hz %u ch %u-bit, render stream %u, capture stream %u, %u frames\n",
                     Streams->Model->Pcms[Pcm].Name, (UINT32)INTCDSP_SELFTEST_RATE, (UINT32)INTCDSP_SELFTEST_CHANNELS, ValidBits,
                     RenderStream->Tag, CaptureStream->Tag, Frames);
    Status = IntcDspStreamAllocate(Streams, Render, (SIZE_T)Frames * FrameBytes, INTCDSP_SELFTEST_PERIODS);
    if (NT_SUCCESS(Status))
        Status = IntcDspStreamAllocate(Streams, Capture, (SIZE_T)Frames * FrameBytes, INTCDSP_SELFTEST_PERIODS);
    if (!NT_SUCCESS(Status))
        goto Release;
    RenderVa = Ops->Map(Ops->Context, RenderStream->Mdl, RenderStream->Bytes);
    CaptureVa = Ops->Map(Ops->Context, CaptureStream->Mdl, CaptureStream->Bytes);
    if (RenderVa == NULL || CaptureVa == NULL)
    {
        Status = STATUS_INSUFFICIENT_RESOURCES;
        goto Release;
    }
    /* the buffer starts where the bus put it in the pages it mapped */
    RenderVa = (PUCHAR)RenderVa + RenderStream->OffsetFromFirstPage;
    CaptureVa = (PUCHAR)CaptureVa + CaptureStream->OffsetFromFirstPage;
    IntcDspSelfTestFill((PUCHAR)RenderVa, RenderStream->Bytes, INTCDSP_SELFTEST_CHANNELS, ValidBits, ContainerBits);
    IntcDspSelfTestPoison((PUCHAR)CaptureVa, CaptureStream->Bytes, ContainerBits);
    /* the capture stream runs first and the render stream after it: a period the firmware moves before its capture stream runs is lost (the model's row 39) */
    StartUs = IntcDspNow(Streams);
    Status = IntcDspStreamRun(Streams, Capture);
    if (NT_SUCCESS(Status))
        Status = IntcDspStreamRun(Streams, Render);
    if (NT_SUCCESS(Status))
        Status = Ops->WaitPeriods(Ops->Context, Capture, INTCDSP_SELFTEST_PERIODS, TimeoutMs, &Periods);
    ElapsedMs = (UINT32)((IntcDspNow(Streams) - StartUs) / 1000);

Release:
    /* the capture stream stands still first, so that its buffer does; a fault released both already */
    Step = IntcDspStreamPause(Streams, Capture);
    if (NT_SUCCESS(Status) && !NT_SUCCESS(Step))
        Status = Step;
    Step = IntcDspStreamPause(Streams, Render);
    if (NT_SUCCESS(Status) && !NT_SUCCESS(Step))
        Status = Step;
    RenderStream = Render ? IntcDspStreamOf(Streams, Render) : NULL;
    CaptureStream = Capture ? IntcDspStreamOf(Streams, Capture) : NULL;
    if (!Streams->Faulted)
    {
        if (RenderStream != NULL && CaptureStream != NULL)
        {
            RenderPosition = IntcDspRegisterValue(Streams, Render, TRUE);
            CapturePosition = IntcDspRegisterValue(Streams, Capture, TRUE);
            RenderLpib = IntcDspRegisterValue(Streams, Render, FALSE);
            CaptureLpib = IntcDspRegisterValue(Streams, Capture, FALSE);
            RenderBuffer = IntcDspStreamPosition(Streams, Render);
            CaptureBuffer = IntcDspStreamPosition(Streams, Capture);
            CaptureStop = CapturePosition;
        }
        if (NT_SUCCESS(Status))
        {
            IntcDspStreamCompare((PUCHAR)RenderVa, (PUCHAR)CaptureVa, RenderStream->Bytes, INTCDSP_SELFTEST_CHANNELS, ContainerBits,
                                 CaptureStop, &Compare);
            if (!Compare.Matched)
                Status = STATUS_DEVICE_PROTOCOL_ERROR;
        }
    }
    Freed0 = Streams->Freed;
    InstanceDeletes0 = Streams->InstanceDeletes;
    PipelineDeletes0 = Streams->PipelineDeletes;
    IntcDspStreamFree(Streams, Capture);
    IntcDspStreamClose(Streams, Capture);
    IntcDspStreamFree(Streams, Render);
    IntcDspStreamClose(Streams, Render);
    if (NT_SUCCESS(Status) && Streams->Faulted)
        Status = STATUS_DEVICE_PROTOCOL_ERROR;
    Pipelines = Streams->PipelineDeletes - PipelineDeletes0;
    Instances = Streams->InstanceDeletes - InstanceDeletes0;
    Freed = Streams->Freed - Freed0;
    if (Compare.Samples != 0 && Compare.Matched)
    {
        IntcDspStreamLog(Streams, "loopback self-test: %u frames captured after %u ms, offset %u frames, max abs error 0, render rms %u.%03u, capture rms %u.%03u, ok\n",
                         Frames, ElapsedMs, Compare.Offset, (UINT32)(Compare.RenderRms / 1000), (UINT32)(Compare.RenderRms % 1000),
                         (UINT32)(Compare.CaptureRms / 1000), (UINT32)(Compare.CaptureRms % 1000));
        if (!NT_SUCCESS(Status))
            IntcDspStreamLog(Streams, "loopback self-test: the capture matched, but a release step failed (0x%08x), failed\n", (UINT32)Status);
    }
    else if (Compare.Samples != 0)
    {
        /* the stop rule: the first differing sample is recorded, the test is not tuned */
        RenderFrame = (UINT32)(((UINT64)Compare.DifferingFrame + Compare.Offset) % FrameCount);
        IntcDspStreamLog(Streams, "loopback self-test: %u frames captured after %u ms, offset %u frames, max abs error %u, render rms %u.%03u, capture rms %u.%03u, mismatch at frame %u channel %u (render frame %u): render %d, capture %d, failed\n",
                         Frames, ElapsedMs, Compare.Offset, (UINT32)Compare.MaxError,
                         (UINT32)(Compare.RenderRms / 1000), (UINT32)(Compare.RenderRms % 1000),
                         (UINT32)(Compare.CaptureRms / 1000), (UINT32)(Compare.CaptureRms % 1000),
                         Compare.DifferingFrame, Compare.DifferingChannel, RenderFrame, Compare.RenderSample, Compare.CaptureSample);
    }
    else
    {
        IntcDspStreamLog(Streams, "loopback self-test: %u frames not captured: %u of %u periods after %u ms (0x%08x), failed\n",
                         Frames, Periods, (UINT32)INTCDSP_SELFTEST_PERIODS, ElapsedMs, (UINT32)Status);
    }
    IntcDspStreamLog(Streams, "loopback self-test: stopped at render position %u (LPIB %u, buffer %u), capture position %u (LPIB %u, buffer %u), %u capture periods, released: %u pipelines and %u instances deleted, %u streams freed, %s\n",
                     RenderPosition, RenderLpib, RenderBuffer, CapturePosition, CaptureLpib, CaptureBuffer, Periods, Pipelines, Instances,
                     Freed, Streams->Faulted ? "released by the fault" : "ok");
    return Status;
}

NTSTATUS
IntcDspLoopbackSelfTest(
    _Inout_ PINTCDSP_STREAMS Streams,
    _In_ const INTCDSP_SELFTEST_OPS* Ops,
    UINT32 Frames)
{
    SOFIPC_CONTEXT* Ipc = Streams->Ipc;
    NTSTATUS Status = STATUS_SUCCESS;
    UINT32 Pcm, Runs = 0, Matched = 0, D;
    UINT32 Requests, Notifications, Timeouts, Interrupts;

    if (!Streams->Ready || Streams->Faulted)
    {
        IntcDspStreamLog(Streams, "loopback self-test: not run (%s)\n", Streams->Faulted ? "the link is faulted" : "the streams are not started");
        return STATUS_DEVICE_NOT_READY;
    }
    Pcm = IntcDspStreamPcm(Streams, INTCDSP_SELFTEST_PCM);
    if (Pcm == SOFTPLG_NO_PCM || Frames == 0 || Ops == NULL || Ops->Map == NULL || Ops->WaitPeriods == NULL)
    {
        IntcDspStreamLog(Streams, "loopback self-test: not run (no PCM named '%s', or no frames, or no operations)\n", INTCDSP_SELFTEST_PCM);
        return STATUS_NOT_FOUND;
    }
    Requests = Ipc->Requests;
    Notifications = Ipc->Notifications;
    Timeouts = Ipc->Timeouts;
    Interrupts = Ipc->InterruptCount;
    for (D = 0; D < INTCDSP_SELFTEST_DEPTHS; D++)
    {
        Runs++;
        Status = IntcDspSelfTestRun(Streams, Ops, Pcm, IntcDspSelfTestDepths[D], Frames);
        if (NT_SUCCESS(Status))
            Matched++;
        /* the stop rule of the plan: the first run that does not match is the last */
        if (!NT_SUCCESS(Status))
            break;
    }
    IntcDspStreamLog(Streams, "loopback self-test: %u runs, %u matched, %u failed, %u requests, %u notifications, %u timeouts, %u DSP interrupts, %s\n",
                     Runs, Matched, Runs - Matched, Ipc->Requests - Requests, Ipc->Notifications - Notifications,
                     Ipc->Timeouts - Timeouts, Ipc->InterruptCount - Interrupts, Runs == Matched ? "ok" : "failed");
    return Status;
}
