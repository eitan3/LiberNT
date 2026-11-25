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

#endif
