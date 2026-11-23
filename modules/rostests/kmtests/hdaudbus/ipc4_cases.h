/*
 * PROJECT:     LiberNT kernel-mode tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Golden IPC4 message vectors, a firmware register window and an extended manifest, shared by the kmtest and the host test
 * COPYRIGHT:   Copyright 2026 LiberNT contributors
 */

#ifndef _IPC4_CASES_H_
#define _IPC4_CASES_H_

#include <sofipc4.h>
#include "dsp_cases.h"

typedef VOID (*IPC4_ENCODE)(_Out_ SOFIPC4_MSG* Msg);

typedef struct _IPC4_VECTOR_CASE
{
    const char* Name;
    IPC4_ENCODE Encode;
    UINT32 Type;
    UINT32 Target;
    UINT32 Primary;
    UINT32 Extension;
} IPC4_VECTOR_CASE;

static VOID EncodeCreatePipeline(SOFIPC4_MSG* Msg)
{
    SofIpc4PipelineCreate(Msg, 0x5A, 3, 0x2AB, 2, 0x1234, SOFIPC4_PIPELINE_CREATE_LP | SOFIPC4_PIPELINE_CREATE_HAS_PAYLOAD);
}

static VOID EncodeCreatePipelineZero(SOFIPC4_MSG* Msg)
{
    SofIpc4PipelineCreate(Msg, 0, 0, 0, 0, 0, 0);
}

static VOID EncodeCreatePipelineMasked(SOFIPC4_MSG* Msg)
{
    SofIpc4PipelineCreate(Msg, 0x1FF, 0x0F, 0xFFFF, 0x1F, 0x1FFFF, SOFIPC4_PIPELINE_CREATE_LP);
}

static VOID EncodeDeletePipeline(SOFIPC4_MSG* Msg)
{
    SofIpc4PipelineDelete(Msg, 9);
}

static VOID EncodeSetStateRunning(SOFIPC4_MSG* Msg)
{
    SofIpc4PipelineSetState(Msg, 7, SOFIPC4_PIPELINE_STATE_RUNNING,
        SOFIPC4_SET_STATE_MULTI_PIPELINE | SOFIPC4_SET_STATE_SYNC_STOP_START);
}

static VOID EncodeSetStateReset(SOFIPC4_MSG* Msg)
{
    SofIpc4PipelineSetState(Msg, 0, SOFIPC4_PIPELINE_STATE_RESET, 0);
}

static VOID EncodeGetState(SOFIPC4_MSG* Msg)
{
    SofIpc4PipelineGetState(Msg, 12);
}

static VOID EncodeGetContextSize(SOFIPC4_MSG* Msg)
{
    SofIpc4PipelineGetContextSize(Msg, 12);
}

static VOID EncodeChainDma(SOFIPC4_MSG* Msg)
{
    SofIpc4ChainDma(Msg, 5, 9, 0x1234, SOFIPC4_CHAIN_DMA_ALLOCATE | SOFIPC4_CHAIN_DMA_ENABLE | SOFIPC4_CHAIN_DMA_SCS);
}

static VOID EncodeInitInstance(SOFIPC4_MSG* Msg)
{
    SofIpc4ModuleInitInstance(Msg, 0x9ABC, 0x31, 6, 0x1234, 3,
        SOFIPC4_INIT_INSTANCE_PROC_DOMAIN | SOFIPC4_INIT_INSTANCE_EXTENDED_INIT);
}

static VOID EncodeBind(SOFIPC4_MSG* Msg)
{
    SofIpc4ModuleBind(Msg, 0x10, 2, 0x20, 4, 5, 3, FALSE);
}

static VOID EncodeUnbind(SOFIPC4_MSG* Msg)
{
    SofIpc4ModuleBind(Msg, 0x10, 2, 0x20, 4, 5, 3, TRUE);
}

static VOID EncodeConfigSet(SOFIPC4_MSG* Msg)
{
    SofIpc4ModuleConfig(Msg, 0x9ABC, 7, 0x31234567, FALSE);
}

static VOID EncodeConfigGet(SOFIPC4_MSG* Msg)
{
    SofIpc4ModuleConfig(Msg, 0x9ABC, 7, 0x31234567, TRUE);
}

static VOID EncodeLargeConfigSet(SOFIPC4_MSG* Msg)
{
    SofIpc4ModuleLargeConfig(Msg, 0x9ABC, 1, 0x5A, 0xABCDE,
        SOFIPC4_LARGE_CONFIG_INIT_BLOCK | SOFIPC4_LARGE_CONFIG_FINAL_BLOCK, FALSE);
}

static VOID EncodeLargeConfigGet(SOFIPC4_MSG* Msg)
{
    SofIpc4ModuleLargeConfig(Msg, 0x9ABC, 1, 0x5A, 0xABCDE,
        SOFIPC4_LARGE_CONFIG_INIT_BLOCK | SOFIPC4_LARGE_CONFIG_FINAL_BLOCK, TRUE);
}

static VOID EncodeDeleteInstance(SOFIPC4_MSG* Msg)
{
    SofIpc4ModuleDeleteInstance(Msg, 0x9ABC, 1);
}

static VOID EncodeSetD0ix(SOFIPC4_MSG* Msg)
{
    SofIpc4ModuleSetD0ix(Msg, SOFIPC4_D0IX_WAKE | SOFIPC4_D0IX_PREVENT_POWER_GATING);
}

static VOID EncodeLoadLibrary(SOFIPC4_MSG* Msg)
{
    SofIpc4ModuleLoadLibrary(Msg, 4, 9, 0x1000);
}

/* The expected words were produced by filling the IPC4 structures of the SOF firmware headers (BSD-3, src/include/ipc4) through their own bit fields; the masked case pins the encoder's own field masks. */
static const IPC4_VECTOR_CASE Ipc4VectorCases[] =
{
    { "create pipeline", EncodeCreatePipeline, SofIpc4GlbCreatePipeline, SOFIPC4_TARGET_FW_GEN_MSG, 0x115A1AAB, 0x20212341 },
    { "create pipeline, all zero", EncodeCreatePipelineZero, SofIpc4GlbCreatePipeline, SOFIPC4_TARGET_FW_GEN_MSG, 0x11000000, 0x00000000 },
    { "create pipeline, fields masked", EncodeCreatePipelineMasked, SofIpc4GlbCreatePipeline, SOFIPC4_TARGET_FW_GEN_MSG, 0x11FF3FFF, 0x00FFFFF1 },
    { "delete pipeline", EncodeDeletePipeline, SofIpc4GlbDeletePipeline, SOFIPC4_TARGET_FW_GEN_MSG, 0x12090000, 0x00000000 },
    { "set state running", EncodeSetStateRunning, SofIpc4GlbSetPipelineState, SOFIPC4_TARGET_FW_GEN_MSG, 0x13070004, 0x00000003 },
    { "set state reset", EncodeSetStateReset, SofIpc4GlbSetPipelineState, SOFIPC4_TARGET_FW_GEN_MSG, 0x13000002, 0x00000000 },
    { "get state", EncodeGetState, SofIpc4GlbGetPipelineState, SOFIPC4_TARGET_FW_GEN_MSG, 0x1400000C, 0x00000000 },
    { "get context size", EncodeGetContextSize, SofIpc4GlbGetPipelineContextSize, SOFIPC4_TARGET_FW_GEN_MSG, 0x150C0000, 0x00000000 },
    { "chain DMA", EncodeChainDma, SofIpc4GlbChainDma, SOFIPC4_TARGET_FW_GEN_MSG, 0x0E070905, 0x00001234 },
    { "init module instance", EncodeInitInstance, SofIpc4ModInitInstance, SOFIPC4_TARGET_MODULE_MSG, 0x40319ABC, 0x33061234 },
    { "bind", EncodeBind, SofIpc4ModBind, SOFIPC4_TARGET_MODULE_MSG, 0x45020010, 0x1D040020 },
    { "unbind", EncodeUnbind, SofIpc4ModUnbind, SOFIPC4_TARGET_MODULE_MSG, 0x46020010, 0x1D040020 },
    { "config set", EncodeConfigSet, SofIpc4ModConfigSet, SOFIPC4_TARGET_MODULE_MSG, 0x42079ABC, 0x31234567 },
    { "config get", EncodeConfigGet, SofIpc4ModConfigGet, SOFIPC4_TARGET_MODULE_MSG, 0x41079ABC, 0x31234567 },
    { "large config set", EncodeLargeConfigSet, SofIpc4ModLargeConfigSet, SOFIPC4_TARGET_MODULE_MSG, 0x44019ABC, 0x35AABCDE },
    { "large config get", EncodeLargeConfigGet, SofIpc4ModLargeConfigGet, SOFIPC4_TARGET_MODULE_MSG, 0x43019ABC, 0x35AABCDE },
    { "delete instance", EncodeDeleteInstance, SofIpc4ModDeleteInstance, SOFIPC4_TARGET_MODULE_MSG, 0x4B019ABC, 0x00000000 },
    { "set D0ix", EncodeSetD0ix, SofIpc4ModSetD0ix, SOFIPC4_TARGET_MODULE_MSG, 0x48000000, 0x00000005 },
    { "load library", EncodeLoadLibrary, SofIpc4GlbLoadLibrary, SOFIPC4_TARGET_FW_GEN_MSG, 0x18090004, 0x00001000 }
};

typedef struct _IPC4_REPLY_CASE
{
    const char* Name;
    UINT32 Type;
    UINT32 Target;
    UINT32 Primary;
    UINT32 Extension;
    UINT32 Status;
    UINT32 ExtensionValue;
} IPC4_REPLY_CASE;

static const IPC4_REPLY_CASE Ipc4ReplyCases[] =
{
    { "set state reply names the failed pipeline", SofIpc4GlbSetPipelineState, SOFIPC4_TARGET_FW_GEN_MSG,
        0x33000004, 0x0000004D, 4, 0x4D },
    { "get state reply carries the state", SofIpc4GlbGetPipelineState, SOFIPC4_TARGET_FW_GEN_MSG,
        0x34000000, 0x00000003, 0, 3 },
    { "context size reply carries the pages", SofIpc4GlbGetPipelineContextSize, SOFIPC4_TARGET_FW_GEN_MSG,
        0x35000000, 0x00000ABE, 0, 0xABE },
    { "module reply", SofIpc4ModInitInstance, SOFIPC4_TARGET_MODULE_MSG, 0x60ABCDEF, 0x00000000, 0xABCDEF, 0 }
};

/* struct ipc4_fw_registers: FSR state 0x0abcdef1 module 5 running, LEC 0x1234, power status, ROM info, ABI version 1, core 3 at 0x77 */
static const unsigned char Ipc4FwRegisterWindow[] =
{
    DSP_U32(0xDABCDEF1), DSP_U32(0x00001234), DSP_U32(0x000533D9), DSP_U32(0x00000055),
    DSP_U32(0x00000066), DSP_U32(0x00000000), DSP_U32(0x065433A5), DSP_U32(0x00000001),
    0x00, 0x00, 0x00, 0x77, 0x00, 0x00, 0x00, 0x00
};

/* struct ext_man_header + ext_man_fw_version (2.12.3 build 45) + ext_man_config_data (IPC message size 512) */
static const unsigned char Ipc4ExtManifest[] =
{
    0x58, 0x4D, 0x61, 0x6E, 0x70, 0x00, 0x00, 0x00, 0x10, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x48, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x0C, 0x00, 0x03, 0x00, 0x2D, 0x00,
    0x4A, 0x61, 0x6E, 0x20, 0x30, 0x32, 0x20, 0x32, 0x30, 0x32, 0x36, 0x00,
    0x31, 0x32, 0x3A, 0x33, 0x34, 0x3A, 0x35, 0x36, 0x00, 0x00, 0x73, 0x6F,
    0x66, 0x00, 0x00, 0x00, 0x03, 0x0C, 0x00, 0x03, 0xEF, 0xBE, 0xAD, 0xDE,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x03, 0x00, 0x00, 0x00, 0x05, 0x00, 0x00, 0x00, 0x18, 0x00, 0x00, 0x00,
    0x02, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00,
    0x00, 0x02, 0x00, 0x00
};

#endif
