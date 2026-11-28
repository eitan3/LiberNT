/*
 * PROJECT:     LiberNT kernel-mode tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Shared rows of the IPC4 transport tests: the request words of the pipeline self-test, a budget per message class and the block plan of large configurations, checked on the host and in the kernel
 * COPYRIGHT:   Copyright 2026 LiberNT contributors
 */

#ifndef KMTESTS_INTCDS_IPC4_TRANSPORT_CASES_H_
#define KMTESTS_INTCDS_IPC4_TRANSPORT_CASES_H_

#include <sofipc.h>

/* The self-test's pipeline: instance 1 of the plan, priority 0, core 0 and no memory pages, as it holds no module */
#define IPC4_CASE_PIPELINE_INSTANCE   1
#define IPC4_CASE_PIPELINE_PAGES      0

/* One exchange of the self-test: the words of its request as the codec builds it, the busy bit clear, and the step as the log names it */
typedef struct _IPC4_TRANSPORT_CASE
{
    const char* Step;
    UINT32 Type;
    UINT32 Primary;
    UINT32 Extension;
} IPC4_TRANSPORT_CASE;

/* create pipeline 1, then paused, running, paused, reset, delete: six exchanges, every reply a success. A state request carries the state in bits 15:0 and the instance in bits 23:16 */
static const IPC4_TRANSPORT_CASE Ipc4TransportSelfTest[] =
{
    { "create pipeline 1",   SofIpc4GlbCreatePipeline,    0x11010000, 0 },
    { "pipeline 1 paused",   SofIpc4GlbSetPipelineState,  0x13010003, 0 },
    { "pipeline 1 running",  SofIpc4GlbSetPipelineState,  0x13010004, 0 },
    { "pipeline 1 paused",   SofIpc4GlbSetPipelineState,  0x13010003, 0 },
    { "pipeline 1 reset",    SofIpc4GlbSetPipelineState,  0x13010002, 0 },
    { "delete pipeline 1",   SofIpc4GlbDeletePipeline,    0x12010000, 0 }
};
#define IPC4_TRANSPORT_SELF_TEST_EXCHANGES   6

/* One request of every message class with its own budget, none answered by the model */
typedef struct _IPC4_TRANSPORT_TIMEOUT_CASE
{
    SOFIPC_CLASS Class;
    UINT32 Type;
    UINT32 Primary;
    UINT32 Extension;
    UINT32 TimeoutUs;
} IPC4_TRANSPORT_TIMEOUT_CASE;

static const IPC4_TRANSPORT_TIMEOUT_CASE Ipc4TransportTimeouts[] =
{
    { SofIpcClassPipeline,    SofIpc4GlbSetPipelineState,   0x13020003,          0, 500000 },
    { SofIpcClassModule,      SofIpc4ModDeleteInstance,     0x4B000000,          0,  70000 },
    { SofIpcClassLargeConfig, SofIpc4ModLargeConfigSet,     0x44000000, 0x00300008,    3000 }
};
#define IPC4_TRANSPORT_TIMEOUT_CASES   3

/* The module and the parameter the large-configuration rows address: module 3, instance 2, parameter 2 */
#define IPC4_CASE_LARGE_MODULE         3
#define IPC4_CASE_LARGE_INSTANCE       2
#define IPC4_CASE_LARGE_PARAM          2
#define IPC4_CASE_LARGE_PRIMARY        0x44020003

/* One block of a large configuration as it must be sent: the size field (the total on the first block, the offset on every later one), the flags and the bytes it carries */
typedef struct _IPC4_TRANSPORT_BLOCK_CASE
{
    UINT32 SizeField;
    UINT32 Flags;
    UINT32 Bytes;
} IPC4_TRANSPORT_BLOCK_CASE;

typedef struct _IPC4_TRANSPORT_LARGE_CASE
{
    UINT32 TotalBytes;
    const IPC4_TRANSPORT_BLOCK_CASE* Block;
    UINT32 Blocks;
} IPC4_TRANSPORT_LARGE_CASE;

static const IPC4_TRANSPORT_BLOCK_CASE Ipc4TransportBlocksThree[] =
{
    { 10000, SOFIPC4_LARGE_CONFIG_INIT_BLOCK, 4096 },
    {  4096, 0,                                 4096 },
    {  8192, SOFIPC4_LARGE_CONFIG_FINAL_BLOCK, 1808 }
};
static const IPC4_TRANSPORT_BLOCK_CASE Ipc4TransportBlocksTwo[] =
{
    { 8192, SOFIPC4_LARGE_CONFIG_INIT_BLOCK,                 4096 },
    { 4096, SOFIPC4_LARGE_CONFIG_FINAL_BLOCK,                4096 }
};
static const IPC4_TRANSPORT_BLOCK_CASE Ipc4TransportBlocksOne[] =
{
    { 4096, SOFIPC4_LARGE_CONFIG_INIT_BLOCK | SOFIPC4_LARGE_CONFIG_FINAL_BLOCK, 4096 }
};
static const IPC4_TRANSPORT_BLOCK_CASE Ipc4TransportBlocksShort[] =
{
    {  100, SOFIPC4_LARGE_CONFIG_INIT_BLOCK | SOFIPC4_LARGE_CONFIG_FINAL_BLOCK,  100 }
};

static const IPC4_TRANSPORT_LARGE_CASE Ipc4TransportLarge[] =
{
    { 10000, Ipc4TransportBlocksThree, 3 },
    {  8192, Ipc4TransportBlocksTwo,   2 },
    {  4096, Ipc4TransportBlocksOne,   1 },
    {   100, Ipc4TransportBlocksShort, 1 }
};
#define IPC4_TRANSPORT_LARGE_CASES   4

/* A configuration read: the firmware's total, the capacity offered, the blocks expected and the bytes returned */
typedef struct _IPC4_TRANSPORT_READ_CASE
{
    UINT32 TotalBytes;
    SIZE_T Capacity;
    UINT32 Blocks;
    SIZE_T Bytes;
    SOFIPC_STATUS Status;
} IPC4_TRANSPORT_READ_CASE;

static const IPC4_TRANSPORT_READ_CASE Ipc4TransportReads[] =
{
    { 6000, 8192, 2, 6000, SofIpcOk },
    { 6000, 6000, 2, 6000, SofIpcOk },
    { 6000, 4096, 1,    0, SofIpcBadLength },
    {  100, 8192, 1,  100, SofIpcOk },
    { 8192, 8192, 2, 8192, SofIpcOk }
};
#define IPC4_TRANSPORT_READ_CASES   5

/*
 * The golden trace of the loopback pair through the stream manager (stream.h
 * is included before this header for the trace kinds): the model's guest
 * values, copier = module 5, gain = module 7, pipeline 1, the render stream
 * tag 2 and the capture stream tag 3, 4096 frames of 32 bits in two channels
 * (32768 bytes a buffer). Kind is the manager's trace kind, Capture and Tag
 * the stream a line is about, Primary and Extension a request's words as
 * sent. The order is the plan's §5.10 and the simulator's model table: open,
 * allocate (the render stream builds the PCM once for both), run (the
 * capture stream first), pause, free (the capture stream's copier is told
 * its stream is going while the render stream still holds the PCM: one
 * large-configuration set), close.
 */
typedef struct _IPC4_STREAM_CASE
{
    const char* Step;
    UINT32 Kind;
    UINT32 Capture;
    UINT32 Tag;
    UINT32 Primary;
    UINT32 Extension;
} IPC4_STREAM_CASE;

#define IPC4_STREAM_CASE_RENDER_TAG     2
#define IPC4_STREAM_CASE_CAPTURE_TAG    3
#define IPC4_STREAM_CASE_BUFFER_BYTES   32768

static const IPC4_STREAM_CASE Ipc4StreamTrace[] =
{
    { "open: the render stream",                 INTCDSP_TRACE_OPEN,        0, IPC4_STREAM_CASE_RENDER_TAG,  0, 0 },
    { "open: the capture stream",                INTCDSP_TRACE_OPEN,        1, IPC4_STREAM_CASE_CAPTURE_TAG, 0, 0 },
    { "allocate: the render buffer",             INTCDSP_TRACE_BUFFER,      0, IPC4_STREAM_CASE_RENDER_TAG,  0, 0 },
    { "allocate: the render bound",              INTCDSP_TRACE_SPIB,        0, IPC4_STREAM_CASE_RENDER_TAG,  0, 0 },
    { "allocate: create pipeline 1",             INTCDSP_TRACE_IPC,         0, IPC4_STREAM_CASE_RENDER_TAG,  0x11010000, 0 },
    { "allocate: init the host-output copier",   INTCDSP_TRACE_IPC,         0, IPC4_STREAM_CASE_RENDER_TAG,  0x40000005, 0x00010015 },
    { "allocate: init the host-input copier",    INTCDSP_TRACE_IPC,         0, IPC4_STREAM_CASE_RENDER_TAG,  0x40010005, 0x00010015 },
    { "allocate: init the gain",                 INTCDSP_TRACE_IPC,         0, IPC4_STREAM_CASE_RENDER_TAG,  0x40000007, 0x00010010 },
    { "allocate: bind the copier to the gain",   INTCDSP_TRACE_IPC,         0, IPC4_STREAM_CASE_RENDER_TAG,  0x45000005, 0x00000007 },
    { "allocate: bind the gain to the copier",   INTCDSP_TRACE_IPC,         0, IPC4_STREAM_CASE_RENDER_TAG,  0x45000007, 0x00010005 },
    { "allocate: the capture buffer",            INTCDSP_TRACE_BUFFER,      1, IPC4_STREAM_CASE_CAPTURE_TAG, 0, 0 },
    { "allocate: the capture bound",             INTCDSP_TRACE_SPIB,        1, IPC4_STREAM_CASE_CAPTURE_TAG, 0, 0 },
    { "run: pipeline 1 paused",                  INTCDSP_TRACE_IPC,         1, IPC4_STREAM_CASE_CAPTURE_TAG, 0x13010003, 0 },
    { "run: pipeline 1 running",                 INTCDSP_TRACE_IPC,         1, IPC4_STREAM_CASE_CAPTURE_TAG, 0x13010004, 0 },
    { "run: the capture stream",                 INTCDSP_TRACE_RUN,         1, IPC4_STREAM_CASE_CAPTURE_TAG, 0, 0 },
    { "run: the render stream",                  INTCDSP_TRACE_RUN,         0, IPC4_STREAM_CASE_RENDER_TAG,  0, 0 },
    { "pause: the capture stream",               INTCDSP_TRACE_STOP,        1, IPC4_STREAM_CASE_CAPTURE_TAG, 0, 0 },
    { "pause: pipeline 1 paused",                INTCDSP_TRACE_IPC,         0, IPC4_STREAM_CASE_RENDER_TAG,  0x13010003, 0 },
    { "pause: the render stream",                INTCDSP_TRACE_STOP,        0, IPC4_STREAM_CASE_RENDER_TAG,  0, 0 },
    { "free: the copier told its stream is going", INTCDSP_TRACE_IPC,       1, IPC4_STREAM_CASE_CAPTURE_TAG, 0x44010005, 0x30000054 },
    { "free: the capture bound off",             INTCDSP_TRACE_SPIB_OFF,    1, IPC4_STREAM_CASE_CAPTURE_TAG, 0, 0 },
    { "free: the capture buffer",                INTCDSP_TRACE_BUFFER_FREE, 1, IPC4_STREAM_CASE_CAPTURE_TAG, 0, 0 },
    { "free: pipeline 1 reset",                  INTCDSP_TRACE_IPC,         0, IPC4_STREAM_CASE_RENDER_TAG,  0x13010002, 0 },
    { "free: delete the host-output copier",     INTCDSP_TRACE_IPC,         0, IPC4_STREAM_CASE_RENDER_TAG,  0x4b000005, 0 },
    { "free: delete the host-input copier",      INTCDSP_TRACE_IPC,         0, IPC4_STREAM_CASE_RENDER_TAG,  0x4b010005, 0 },
    { "free: delete the gain",                   INTCDSP_TRACE_IPC,         0, IPC4_STREAM_CASE_RENDER_TAG,  0x4b000007, 0 },
    { "free: delete pipeline 1",                 INTCDSP_TRACE_IPC,         0, IPC4_STREAM_CASE_RENDER_TAG,  0x12010000, 0 },
    { "free: the render bound off",              INTCDSP_TRACE_SPIB_OFF,    0, IPC4_STREAM_CASE_RENDER_TAG,  0, 0 },
    { "free: the render buffer",                 INTCDSP_TRACE_BUFFER_FREE, 0, IPC4_STREAM_CASE_RENDER_TAG, 0, 0 },
    { "close: the capture tag",                  INTCDSP_TRACE_FREE,        1, IPC4_STREAM_CASE_CAPTURE_TAG, 0, 0 },
    { "close: the render tag",                   INTCDSP_TRACE_FREE,        0, IPC4_STREAM_CASE_RENDER_TAG,  0, 0 }
};
#define IPC4_STREAM_TRACE_ROWS          31

/* The index in the golden trace where each transition starts */
#define IPC4_STREAM_TRACE_OPEN          0
#define IPC4_STREAM_TRACE_ALLOCATE      2
#define IPC4_STREAM_TRACE_RUN           12
#define IPC4_STREAM_TRACE_PAUSE         16
#define IPC4_STREAM_TRACE_FREE          19
#define IPC4_STREAM_TRACE_CLOSE         29

/* The requests of the allocate transition that can each be the one the firmware never answers: the steps of the build */
#define IPC4_STREAM_TRACE_ALLOCATE_FIRST_REQUEST  4
#define IPC4_STREAM_TRACE_ALLOCATE_REQUESTS       6
#define IPC4_STREAM_TRACE_RUN_FIRST_REQUEST       12
#define IPC4_STREAM_TRACE_RUN_REQUESTS            2

/* The tag in word 17 of each copier's init-instance configuration: type << 8 | tag - 1 (ipc4model.h SOFTPLG_COPIER_NODE_ID_WORD) */
#define IPC4_STREAM_CASE_PLAYBACK_NODE    ((0 << 8) | (IPC4_STREAM_CASE_RENDER_TAG - 1))
#define IPC4_STREAM_CASE_CAPTURE_NODE     ((1 << 8) | (IPC4_STREAM_CASE_CAPTURE_TAG - 1))
#define IPC4_STREAM_CASE_PLACEHOLDER_NODE ((1 << 8) | 0xFF)

/*
 * The loopback topology as a model built by hand, for the kernel test that
 * has no file to read: the same numbers the builder gets from
 * sof-ipc4-loopback.tplg (t_softplg.c's fixture walk): copier = module 5,
 * gain = module 7, pipeline 1 under its scheduler, the PCM 'Loopback' with
 * the playback copier 0 and the capture copier 1, the host formats 16, 24
 * and 32 valid bits at 48 kHz in two channels (a 32-bit container's 384
 * bytes, a 16-bit one's 192), the edges copier 0 -> gain -> copier 1.
 */
typedef struct _IPC4_STREAM_MODEL
{
    SOFTPLG_IPC4_MODEL Model;
    SOFTPLG_IPC4_WIDGET Widgets[4];
    SOFTPLG_IPC4_EDGE Edges[2];
    SOFTPLG_IPC4_PCM Pcms[1];
    SOFTPLG_IPC4_PIPELINE Pipelines[1];
    SOFTPLG_IPC4_FORMAT Formats[10];
    UINT32 Reachable;
} IPC4_STREAM_MODEL;

static
VOID
Ipc4StreamLoopbackModel(
    _Out_ IPC4_STREAM_MODEL* M)
{
    static const UINT32 Words[10][6] =
    {
        { 48000, 16, 0xFFFFFF10, 1, 0, 0x11002 }, { 48000, 32, 0xFFFFFF10, 1, 0, 0x11802 }, { 48000, 32, 0xFFFFFF10, 1, 0, 0x12002 },
        { 48000, 32, 0xFFFFFF10, 1, 0, 0x12002 },
        { 48000, 32, 0xFFFFFF10, 1, 0, 0x12002 }, { 48000, 32, 0xFFFFFF10, 1, 0, 0x12002 }, { 48000, 32, 0xFFFFFF10, 1, 0, 0x11802 },
        { 48000, 16, 0xFFFFFF10, 1, 0, 0x11002 },
        { 48000, 32, 0xFFFFFF10, 1, 0, 0x12002 }, { 48000, 32, 0xFFFFFF10, 1, 0, 0x12002 }
    };
    UINT32 F;

    RtlZeroMemory(M, sizeof(*M));
    M->Model.AbiMajor = SOFTPLG_IPC4_ABI_MAJOR;
    M->Model.AbiMinor = SOFTPLG_IPC4_ABI_MINOR;
    M->Model.WidgetCount = 4;
    M->Model.EdgeCount = 2;
    M->Model.PcmCount = 1;
    M->Model.PipelineCount = 1;
    M->Model.FormatCount = 10;
    M->Model.Widgets = M->Widgets;
    M->Model.Edges = M->Edges;
    M->Model.Pcms = M->Pcms;
    M->Model.Pipelines = M->Pipelines;
    M->Model.Formats = M->Formats;
    for (F = 0; F < 10; F++)
    {
        RtlCopyMemory(M->Formats[F].Words, Words[F], sizeof(Words[F]));
        M->Formats[F].BufferBytes = (Words[F][1] == 16) ? 192 : 384;
        M->Formats[F].Expressible = TRUE;
    }
    /* widget 0: the host-output copier, three host formats in, the pipeline's out */
    M->Widgets[0].Kind = SoftplgKindHostCopierPlayback;
    M->Widgets[0].Pipeline = 0;
    M->Widgets[0].PipelineId = 1;
    M->Widgets[0].ModuleId = 5;
    M->Widgets[0].Cpc = 0x66F;
    M->Widgets[0].NodeType = 0;
    M->Widgets[0].InputFormats = 3;
    M->Widgets[0].OutputFormats = 1;
    M->Widgets[0].FirstFormat = 0;
    /* widget 1: the host-input copier, the pipeline's format in, three host formats out */
    M->Widgets[1].Kind = SoftplgKindHostCopierCapture;
    M->Widgets[1].Pipeline = 0;
    M->Widgets[1].PipelineId = 1;
    M->Widgets[1].ModuleId = 5;
    M->Widgets[1].Cpc = 0x66F;
    M->Widgets[1].NodeType = 1;
    M->Widgets[1].InputFormats = 1;
    M->Widgets[1].OutputFormats = 3;
    M->Widgets[1].FirstFormat = 4;
    /* widget 2: the gain, a base configuration of 10 words */
    M->Widgets[2].Kind = SoftplgKindProcessor;
    M->Widgets[2].Pipeline = 0;
    M->Widgets[2].PipelineId = 1;
    M->Widgets[2].ModuleId = 7;
    M->Widgets[2].InputFormats = 1;
    M->Widgets[2].OutputFormats = 1;
    M->Widgets[2].FirstFormat = 8;
    M->Widgets[2].Config.Count = SOFTPLG_IPC4_BASE_CONFIG_WORDS;
    /* widget 3: the scheduler of pipeline 1 */
    M->Widgets[3].Kind = SoftplgKindScheduler;
    M->Widgets[3].Pipeline = 0;
    M->Widgets[3].PipelineId = 1;
    M->Pipelines[0].Id = 1;
    M->Pipelines[0].Scheduler = 3;
    M->Edges[0].Source = 0;
    M->Edges[0].Sink = 2;
    M->Edges[1].Source = 2;
    M->Edges[1].Sink = 1;
    M->Reachable = 7;
    M->Model.Reachable = &M->Reachable;
    M->Pcms[0].Name[0] = 'L';
    M->Pcms[0].Name[1] = 'o';
    M->Pcms[0].Name[2] = 'o';
    M->Pcms[0].Name[3] = 'p';
    M->Pcms[0].Name[4] = 'b';
    M->Pcms[0].Name[5] = 'a';
    M->Pcms[0].Name[6] = 'c';
    M->Pcms[0].Name[7] = 'k';
    M->Pcms[0].Playback = 1;
    M->Pcms[0].Capture = 1;
    M->Pcms[0].PlaybackCopier = 0;
    M->Pcms[0].CaptureCopier = 1;
    M->Pcms[0].PipelineMask[0] = 1;
    M->Pcms[0].Reachable = &M->Reachable;
    M->Pcms[0].Reached = 3;
}

#endif
