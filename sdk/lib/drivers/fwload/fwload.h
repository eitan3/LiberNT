/*
 * PROJECT:     LiberNT audio driver libraries
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Boots an Intel ACE-class audio DSP with Sound Open Firmware over an HD Audio code-loader stream: the .ri container reader, the DMA preparation and the boot state machine, all free of I/O
 * COPYRIGHT:   Copyright 2026 LiberNT contributors
 */

/*
 * The firmware container (.ri) layout in this file is ported from the Sound Open
 * Firmware project (https://github.com/thesofproject/sof) at fcebbbf22d7c08159fbb8486d5f3687da4f167c3:
 * tools/rimage/src/include/rimage/cavs/cavs_ext_manifest.h (the $AE1 header and its module entries),
 * tools/rimage/src/include/rimage/sof/user/manifest.h (sof_man_fw_header, sof_man_module,
 * sof_man_mod_config, sof_man_segment_desc), tools/rimage/src/include/rimage/cse.h (the partition
 * directory) and tools/rimage/src/include/rimage/css.h (the manifest signature header).
 * The IPC4 firmware configuration tuples are ported from src/include/sof/tlv.h and
 * src/include/ipc4/base_fw.h of the same revision.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2016, 2017, 2021, 2023 Intel Corporation. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 * this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 * this list of conditions and the following disclaimer in the documentation
 * and/or other materials provided with the distribution.
 *
 * 3. Neither the name of the copyright holder nor the names of its
 * contributors may be used to endorse or promote products derived from this
 * software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * The boot flow and the register names below were learned from Linux v6.12
 * sound/soc/sof/intel/hda-loader.c (cl_dsp_init, hda_cl_copy_fw, hda_dsp_cl_boot_firmware,
 * hda_cl_prepare), mtl.c (mtl_dsp_cl_init, mtl_dsp_pre_fw_run, mtl_dsp_core_power_up,
 * mtl_dsp_core_power_down, mtl_power_down_dsp, mtl_enable_interrupts), mtl.h (the register
 * offsets and the ROM state codes of the ACE 1.0 family), hda.h (the FSR layout, the ROM error
 * codes, the code-loader stream constants) and ipc4-loader.c (sof_ipc4_fw_parse_ext_man,
 * sof_ipc4_query_fw_configuration), studied under LICENSING.md rule 7: own declarations,
 * own comments, no text copied. The Arrow Lake-S descriptor (arl_s_chip_info in pci-mtl.c)
 * selects mtl_dsp_cl_init, mtl_power_down_dsp, the HFIPCX registers and the HFFLGPXQWY status
 * register used here.
 */

#ifndef _DRIVERS_FWLOAD_FWLOAD_H_
#define _DRIVERS_FWLOAD_FWLOAD_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <sofipc4.h>

/* Register windows of the controller, Linux hda.h HDA_DSP_*_BAR */
#define FWLOAD_BAR_HDA              0
#define FWLOAD_BAR_PP               1
#define FWLOAD_BAR_SPIB             2
#define FWLOAD_BAR_DRSM             3
#define FWLOAD_BAR_DSP              4

/* PPCTL bits of the processing capabilities, Linux hda.h SOF_HDA_PPCTL_* */
#define FWLOAD_PPCTL_GPROCEN        0x40000000UL
#define FWLOAD_PPCTL_PIE            0x80000000UL
#define FWLOAD_PP_OFFSET_PPCTL      0x04

/* Code-loader stream: Linux hda.h FW_CL_STREAM_NUMBER and HDA_CL_STREAM_FORMAT */
#define FWLOAD_CL_STREAM_TAG        1
#define FWLOAD_CL_STREAM_FORMAT     0x40

/* ROM control word written to the request register: Linux hda.h HDA_DSP_ROM_IPC_* */
#define FWLOAD_ROM_IPC_CONTROL      0x01000000UL
#define FWLOAD_ROM_IPC_PURGE_FW     0x00004000UL
#define FWLOAD_ROM_STREAM_SHIFT     9

/* FSR layout: Linux hda.h FSR_*_MASK */
#define FWLOAD_FSR_STATE_MASK       0x00FFFFFFUL
#define FWLOAD_FSR_WAIT_STATE_MASK  0x0F000000UL
#define FWLOAD_FSR_WAIT_STATE_SHIFT 24
#define FWLOAD_FSR_MODULE_MASK      0x70000000UL
#define FWLOAD_FSR_MODULE_SHIFT     28
#define FWLOAD_FSR_HALTED           0x80000000UL

/* Module-independent state codes, Linux hda.h FSR_STATE_* */
#define FWLOAD_FSR_INIT             0x0
#define FWLOAD_FSR_INIT_DONE        0x1
#define FWLOAD_FSR_FW_ENTERED       0x5

/* ROM state codes of the ACE 1.0 family, Linux mtl.h FSR_STATE_ROM_* */
#define FWLOAD_FSR_ROM_RESET_VECTOR_DONE         0x8
#define FWLOAD_FSR_ROM_PURGE_BOOT                0x9
#define FWLOAD_FSR_ROM_RESTORE_BOOT              0xA
#define FWLOAD_FSR_ROM_FW_ENTRY_POINT            0xB
#define FWLOAD_FSR_ROM_VALIDATE_PUB_KEY          0xC
#define FWLOAD_FSR_ROM_POWER_DOWN_HPSRAM         0xD
#define FWLOAD_FSR_ROM_POWER_DOWN_ULPSRAM        0xE
#define FWLOAD_FSR_ROM_POWER_UP_ULPSRAM_STACK    0xF
#define FWLOAD_FSR_ROM_POWER_UP_HPSRAM_DMA       0x10
#define FWLOAD_FSR_ROM_BEFORE_EP_POINTER_READ    0x11
#define FWLOAD_FSR_ROM_VALIDATE_MANIFEST         0x12
#define FWLOAD_FSR_ROM_VALIDATE_FW_MODULE        0x13
#define FWLOAD_FSR_ROM_PROTECT_IMR_REGION        0x14
#define FWLOAD_FSR_ROM_PUSH_MODEL_ROUTINE        0x15
#define FWLOAD_FSR_ROM_PULL_MODEL_ROUTINE        0x16
#define FWLOAD_FSR_ROM_VALIDATE_PKG_DIR          0x17
#define FWLOAD_FSR_ROM_VALIDATE_CPD              0x18
#define FWLOAD_FSR_ROM_VALIDATE_CSS_MAN_HEADER   0x19
#define FWLOAD_FSR_ROM_VALIDATE_BLOB_SVN         0x1A
#define FWLOAD_FSR_ROM_VERIFY_IFWI_PARTITION     0x1B
#define FWLOAD_FSR_ROM_REMOVE_ACCESS_CONTROL     0x1C
#define FWLOAD_FSR_ROM_AUTH_BYPASS               0x1D
#define FWLOAD_FSR_ROM_AUTH_ENABLED              0x1E
#define FWLOAD_FSR_ROM_INIT_DMA                  0x1F
#define FWLOAD_FSR_ROM_PURGE_FW_ENTRY            0x20
#define FWLOAD_FSR_ROM_PURGE_FW_END              0x21
#define FWLOAD_FSR_ROM_CLEAN_UP_BSS_DONE         0x22
#define FWLOAD_FSR_ROM_IMR_RESTORE_ENTRY         0x23
#define FWLOAD_FSR_ROM_IMR_RESTORE_END           0x24
#define FWLOAD_FSR_ROM_FW_MANIFEST_IN_DMA_BUFF   0x25
#define FWLOAD_FSR_ROM_LOAD_CSE_MAN_TO_IMR       0x26
#define FWLOAD_FSR_ROM_LOAD_FW_MAN_TO_IMR        0x27
#define FWLOAD_FSR_ROM_LOAD_FW_CODE_TO_IMR       0x28
#define FWLOAD_FSR_ROM_FW_LOADING_DONE           0x29
#define FWLOAD_FSR_ROM_FW_CODE_LOADED            0x2A
#define FWLOAD_FSR_ROM_VERIFY_IMAGE_TYPE         0x2B
#define FWLOAD_FSR_ROM_AUTH_API_INIT             0x2C
#define FWLOAD_FSR_ROM_AUTH_API_PROC             0x2D
#define FWLOAD_FSR_ROM_AUTH_API_FIRST_BUSY       0x2E
#define FWLOAD_FSR_ROM_AUTH_API_FIRST_RESULT     0x2F
#define FWLOAD_FSR_ROM_AUTH_API_CLEANUP          0x30

/* ROM error codes reported in the FSR, Linux hda.h HDA_DSP_ROM_* */
#define FWLOAD_ROM_CSE_ERROR                 40
#define FWLOAD_ROM_CSE_WRONG_RESPONSE        41
#define FWLOAD_ROM_IMR_TO_SMALL              42
#define FWLOAD_ROM_BASE_FW_NOT_FOUND         43
#define FWLOAD_ROM_CSE_VALIDATION_FAILED     44
#define FWLOAD_ROM_IPC_FATAL_ERROR           45
#define FWLOAD_ROM_L2_CACHE_ERROR            46
#define FWLOAD_ROM_LOAD_OFFSET_TO_SMALL      47
#define FWLOAD_ROM_API_PTR_INVALID           50
#define FWLOAD_ROM_BASEFW_INCOMPAT           51
#define FWLOAD_ROM_UNHANDLED_INTERRUPT       0xBEE00000UL
#define FWLOAD_ROM_MEMORY_HOLE_ECC           0xECC00000UL
#define FWLOAD_ROM_KERNEL_EXCEPTION          0xCAFE0000UL
#define FWLOAD_ROM_USER_EXCEPTION            0xBEEF0000UL
#define FWLOAD_ROM_UNEXPECTED_RESET          0xDECAF000UL
#define FWLOAD_ROM_NULL_FW_ENTRY             0x4C4C4E55UL

/* Timeouts and delays, Linux hda.h HDA_DSP_*_TIMEOUT and mtl.c */
#define FWLOAD_TIMEOUT_POWER_UP_US      50000
#define FWLOAD_TIMEOUT_ROM_INIT_US      500000
#define FWLOAD_TIMEOUT_ROM_STATUS_US    300000
#define FWLOAD_TIMEOUT_BASE_FW_US       3000000
#define FWLOAD_TIMEOUT_FW_READY_US      1000000
#define FWLOAD_TIMEOUT_IPC_REPLY_US     500000
#define FWLOAD_POLL_INTERVAL_US         500
#define FWLOAD_POWER_SETTLE_US          1000
#define FWLOAD_CORE_POWER_DOWN_US       50000
#define FWLOAD_BOOT_ATTEMPTS            3

/* Base firmware IPC: Linux include/sound/sof/ipc4/header.h SOF_IPC4_* */
#define FWLOAD_BASEFW_MODULE_ID             0
#define FWLOAD_BASEFW_INSTANCE_ID           0
#define FWLOAD_PARAM_ID_FW_CONFIG           7
#define FWLOAD_PARAM_ID_SHIFT               20
#define FWLOAD_MAILBOX_BYTES                4096

/* Firmware configuration tuples, Linux sof_ipc4_fw_config_params and sof_ipc4_tuple */
#define FWLOAD_TUPLE_HEADER_BYTES           8
#define FWLOAD_TUPLE_FW_VERSION             0
#define FWLOAD_TUPLE_DL_MAILBOX_BYTES       6
#define FWLOAD_TUPLE_UL_MAILBOX_BYTES       7
#define FWLOAD_TUPLE_TRACE_LOG_BYTES        8
#define FWLOAD_TUPLE_MAX_PPL_COUNT          9
#define FWLOAD_TUPLE_MAX_LIBS_COUNT         17
#define FWLOAD_TUPLE_FW_CONTEXT_SAVE        29

/* Container layout, rimage manifest.h and cavs_ext_manifest.h */
#define SOF_RI_EXT_MAN_MAGIC            0x31454124UL /* "$AE1" */
#define SOF_RI_EXT_MAN_HEADER_BYTES     16
#define SOF_RI_EXT_MODULE_HEADER_BYTES  32
#define SOF_RI_CPD_MAGIC                0x44504324UL /* "$CPD" */
#define SOF_RI_CPD_HEADER_BYTES_MIN     16
#define SOF_RI_CPD_ENTRY_BYTES          24
#define SOF_RI_CSS_HEADER_ID            0x324E4D24UL /* "$MN2" */
#define SOF_RI_CSS_MODULE_TYPE          4
#define SOF_RI_FW_HDR_OFFSET            0x2000
#define SOF_RI_FW_HDR_MAGIC             0x314D4124UL /* "$AM1" */
#define SOF_RI_FW_HDR_BYTES             52
#define SOF_RI_MODULE_MAGIC             0x454D4124UL /* "$AME" */
/* sof_man_module: id 4, name 8, uuid 16, type 4, hash 32, entry point 4, configuration 4, affinity 4, limits 4, three sof_man_segment_desc of 12 bytes */
#define SOF_RI_MODULE_BYTES             116
#define SOF_RI_MODULE_CONFIG_BYTES      44
#define SOF_RI_PAGE_BYTES               4096
#define SOF_RI_MAX_EXT_MAN_MODULES      256
#define SOF_RI_MAX_CPD_ENTRIES          8
#define SOF_RI_MAX_PARTITIONS           4

typedef enum _FWLOAD_STATUS
{
    FwloadOk = 0,
    FwloadNoData,
    FwloadBadSignature,
    FwloadBadLength,
    FwloadTruncated,
    FwloadUnsupported,
    FwloadTooManyModules,
    FwloadCancelled,
    FwloadTimeoutPowerUp,
    FwloadTimeoutRomInit,
    FwloadTimeoutRomStatus,
    FwloadTimeoutBaseFw,
    FwloadTimeoutFwReady,
    FwloadTimeoutIpcReply,
    FwloadRomError,
    FwloadFwCrash,
    FwloadAbiMismatch,
    FwloadVersionMismatch,
    FwloadIpcError,
    FwloadIpcBadReply,
    FwloadDmaTooManyFragments,
    FwloadDmaTooSmall,
    FwloadHardwareFault,
    FwloadBadState
} FWLOAD_STATUS;

typedef enum _FWLOAD_STATE
{
    FwloadStateOff = 0,
    FwloadStatePoweringUp,
    FwloadStateRomInit,
    FwloadStateLoading,
    FwloadStateFwReady,
    FwloadStateConfiguring,
    FwloadStateReady,
    FwloadStatePoweringDown,
    FwloadStateFailed
} FWLOAD_STATE;

/* sof_man_segment_desc: a module's text, data and bss window in the payload */
typedef struct _SOF_RI_SEGMENT
{
    UINT32 Flags;
    UINT32 VBaseAddress;
    UINT32 FileOffset;
    UINT32 Pages;
    UINT32 Type;
    BOOLEAN Load;
    BOOLEAN Alloc;
} SOF_RI_SEGMENT, *PSOF_RI_SEGMENT;

/* sof_man_module and its sof_man_mod_config */
typedef struct _SOF_RI_MODULE
{
    CHAR Name[9];
    UINT8 Uuid[16];
    UINT32 Type;
    UINT32 LoadType;
    UINT32 EntryPoint;
    UINT16 CfgOffset;
    UINT16 CfgCount;
    UINT32 AffinityMask;
    UINT16 InstanceMaxCount;
    UINT16 InstanceBssSize;
    SOF_RI_SEGMENT Segment[3];
    /* the sof_man_mod_config at index CfgOffset, valid when CfgCount is not zero */
    BOOLEAN HasConfig;
    UINT32 Par[4];
    UINT32 IsBytes;
    UINT32 Cps;
    UINT32 Ibs;
    UINT32 Obs;
    UINT32 ModuleFlags;
    UINT32 Cpc;
    UINT32 Obls;
} SOF_RI_MODULE, *PSOF_RI_MODULE;

typedef struct _SOF_RI_PARTITION
{
    CHAR Name[13];
    UINT32 Offset;
    UINT32 Length;
} SOF_RI_PARTITION, *PSOF_RI_PARTITION;

typedef struct _SOF_RI_IMAGE
{
    /* Extended manifest header ($AE1) */
    UINT32 ExtManVersionMajor;
    UINT32 ExtManVersionMinor;
    UINT32 ExtManModuleCount;
    UINT32 PayloadOffset;
    SIZE_T PayloadBytes;
    SIZE_T UsedBytes;
    /* CSE partition directory at the payload start, present in signed images */
    BOOLEAN HasPartitionDirectory;
    CHAR PartitionDirectoryName[5];
    UINT32 PartitionEntryCount;
    SOF_RI_PARTITION Partition[SOF_RI_MAX_PARTITIONS];
    UINT32 PartitionCount;
    /* Manifest signature header ($MN2) of the .man partition */
    BOOLEAN HasSignature;
    UINT32 SignatureVendor;
    UINT32 SignatureDate;
    UINT32 SignatureMajor;
    UINT32 SignatureMinor;
    UINT32 SignatureHotfix;
    UINT32 SignatureBuild;
    UINT32 SignatureSvn;
    /* sof_man_fw_header ($AM1) */
    UINT32 HeaderOffset;
    CHAR Name[9];
    UINT32 Major;
    UINT32 Minor;
    UINT32 Hotfix;
    UINT32 Build;
    UINT32 PreloadPageCount;
    UINT32 ImageFlags;
    UINT32 FeatureMask;
    UINT32 HwBufferBase;
    UINT32 HwBufferLength;
    UINT32 LoadOffset;
    UINT32 ModuleCount;
    UINT32 ConfigOffset;
    PSOF_RI_MODULE Modules;
    UINT32 ModuleCapacity;
    /* Where parsing stopped, and the buffer the image was read from */
    SIZE_T BadOffset;
    const UINT8* Data;
    SIZE_T Size;
} SOF_RI_IMAGE, *PSOF_RI_IMAGE;

/* One buffer descriptor of an HD Audio stream: address, length and the interrupt-on-completion flag */
typedef struct _FWLOAD_BDL_ENTRY
{
    UINT32 AddressLow;
    UINT32 AddressHigh;
    UINT32 Bytes;
    UINT32 InterruptOnCompletion;
} FWLOAD_BDL_ENTRY, *PFWLOAD_BDL_ENTRY;

typedef struct _FWLOAD_DMA_SEGMENT
{
    UINT64 Address;
    SIZE_T Bytes;
} FWLOAD_DMA_SEGMENT, *PFWLOAD_DMA_SEGMENT;

/* The code-loader DMA buffer: a virtual buffer, its physical layout and the descriptor list the stream walks */
typedef struct _FWLOAD_CL_DMA
{
    UINT8* Virtual;
    SIZE_T Capacity;
    const FWLOAD_DMA_SEGMENT* Segment;
    SIZE_T SegmentCount;
    FWLOAD_BDL_ENTRY* Bdl;
    SIZE_T BdlCapacity;
    SIZE_T BdlBytes;
    SIZE_T FragmentCount;
    SIZE_T Bytes;
    UINT32 EntryBoundary;
    UINT8 StreamTag;
    UINT16 StreamFormat;
    UINT16 LastValidIndex;
    UINT32 Spib;
} FWLOAD_CL_DMA, *PFWLOAD_CL_DMA;

/* Register map of one DSP family; FwloadPlatformArlS() fills the ACE 1.0 map */
typedef struct _FWLOAD_PLATFORM
{
    UINT32 SubsystemControl;        /* SPA bit 16, CPA bit 24 */
    UINT32 CoreControl;             /* owner bits 25:24, SPA bit 0, CPA bit 8 */
    UINT32 InterruptPointer;        /* bits 20:0 name the interrupt enable register */
    UINT32 InterruptEnableHostIpc;  /* bit 0 */
    UINT32 InterruptEnableSoundwire;/* bit 6 */
    UINT32 HostIpcInterruptEnable;  /* bit 0 */
    UINT32 IpcRequest;              /* busy bit 31 */
    UINT32 IpcRequestData;
    UINT32 IpcReply;                /* done bit 31 */
    UINT32 IpcControl;              /* interrupt enable for busy bit 0 and done bit 1 */
    UINT32 RomStatus;
    UINT32 RomError;
    UINT32 Window0Offset;
    UINT32 WindowStride;
    UINT32 UplinkOffset;
    UINT32 DownlinkOffset;
    UINT32 MailboxBytes;
} FWLOAD_PLATFORM, *PFWLOAD_PLATFORM;

typedef struct _FWLOAD_TIMEOUTS
{
    UINT32 PowerUpUs;
    UINT32 RomInitUs;
    UINT32 RomStatusUs;
    UINT32 BaseFwUs;
    UINT32 FwReadyUs;
    UINT32 IpcReplyUs;
    UINT32 PollIntervalUs;
    UINT32 PowerSettleUs;
    UINT32 CorePowerDownUs;
    UINT32 BootAttempts;
} FWLOAD_TIMEOUTS, *PFWLOAD_TIMEOUTS;

typedef struct _FWLOAD_CONTEXT FWLOAD_CONTEXT, *PFWLOAD_CONTEXT;

/*
 * Everything the boot code does to the outside world. The hardware operations table of P2b
 * implements it with MMIO, the DMA adapter and the hdaudbus stream interface; the host tests
 * implement it with a DSP model. Time is injected too: NowUs is monotonic, DelayUs advances it.
 * The table holds functions only, so it can stay static const: every call receives the context
 * FwloadInitContext was given.
 */
typedef struct _FWLOAD_OPS
{
    UINT32 (*Read32)(PVOID Context, UINT32 Bar, UINT32 Offset);
    VOID (*Write32)(PVOID Context, UINT32 Bar, UINT32 Offset, UINT32 Value);
    BOOLEAN (*ReadMemory)(PVOID Context, UINT32 Offset, UINT8* Buffer, SIZE_T Bytes);
    UINT64 (*NowUs)(PVOID Context);
    VOID (*DelayUs)(PVOID Context, UINT32 Microseconds);
    FWLOAD_STATUS (*StreamSubmit)(PVOID Context, PFWLOAD_CL_DMA Dma);
    FWLOAD_STATUS (*StreamTrigger)(PVOID Context, BOOLEAN Start);
    VOID (*StreamRelease)(PVOID Context);
    BOOLEAN (*IpcIncoming)(PVOID Context, PSOFIPC4_MSG Message);
    VOID (*Log)(PVOID Context, const char* Text);
} FWLOAD_OPS, *PFWLOAD_OPS;

/* What the firmware answered after FW_READY */
typedef struct _FWLOAD_FW_CONFIG
{
    SOFIPC4_FW_REGS Registers;
    BOOLEAN HasVersion;
    UINT16 Major;
    UINT16 Minor;
    UINT16 Hotfix;
    UINT16 Build;
    BOOLEAN HasDownlinkMailbox;
    UINT32 DownlinkMailboxBytes;
    BOOLEAN HasUplinkMailbox;
    UINT32 UplinkMailboxBytes;
    BOOLEAN HasTraceLog;
    UINT32 TraceLogBytes;
    BOOLEAN HasMaxPipelines;
    UINT32 MaxPipelines;
    BOOLEAN HasMaxLibraries;
    UINT32 MaxLibraries;
    BOOLEAN HasContextSave;
    UINT32 ContextSave;
    UINT32 TupleCount;
} FWLOAD_FW_CONFIG, *PFWLOAD_FW_CONFIG;

struct _FWLOAD_CONTEXT
{
    const FWLOAD_OPS* Ops;
    PVOID OpsContext;
    const FWLOAD_PLATFORM* Platform;
    const SOF_RI_IMAGE* Image;
    FWLOAD_CL_DMA Dma;
    FWLOAD_TIMEOUTS Timeouts;
    FWLOAD_STATE State;
    FWLOAD_STATUS Status;
    FWLOAD_STATE FailedState;
    FWLOAD_STATUS CleanupStatus;
    UINT32 RomStatus;
    UINT32 RomError;
    UINT32 LastIpcStatus;
    UINT32 Attempt;
    UINT64 BootUs;
    BOOLEAN Cancel;
    BOOLEAN StreamSubmitted;
    BOOLEAN InterruptsEnabled;
    BOOLEAN SubsystemOn;
    BOOLEAN CoreOn;
    FWLOAD_FW_CONFIG FwConfig;
    UINT8 Mailbox[FWLOAD_MAILBOX_BYTES];
};

VOID FwloadDefaultTimeouts(_Out_ PFWLOAD_TIMEOUTS Timeouts);
const FWLOAD_PLATFORM* FwloadPlatformArlS(VOID);
const char* FwloadStatusName(FWLOAD_STATUS Status);
const char* FwloadStateName(FWLOAD_STATE State);
const char* FwloadRomErrorName(UINT32 Code);
const char* FwloadFsrStateName(UINT32 State);

FWLOAD_STATUS
SofRiParse(
    _In_reads_bytes_opt_(Size) const UINT8* Data,
    SIZE_T Size,
    _Out_writes_opt_(ModuleCapacity) PSOF_RI_MODULE Modules,
    UINT32 ModuleCapacity,
    _Out_ PSOF_RI_IMAGE Image);

FWLOAD_STATUS
FwloadClDmaPrepare(
    _Inout_ PFWLOAD_CL_DMA Dma,
    _In_reads_bytes_(PayloadBytes) const UINT8* Payload,
    SIZE_T PayloadBytes);

SIZE_T
FwloadClDmaEntryCount(
    _In_reads_(SegmentCount) const FWLOAD_DMA_SEGMENT* Segment,
    SIZE_T SegmentCount,
    SIZE_T PayloadBytes,
    UINT32 EntryBoundary);

VOID
FwloadInitContext(
    _Out_ PFWLOAD_CONTEXT Context,
    const FWLOAD_OPS* Ops,
    PVOID OpsContext,
    const FWLOAD_PLATFORM* Platform,
    const SOF_RI_IMAGE* Image,
    const FWLOAD_CL_DMA* Dma,
    const FWLOAD_TIMEOUTS* Timeouts);

FWLOAD_STATUS FwloadBoot(_Inout_ PFWLOAD_CONTEXT Context);
FWLOAD_STATUS FwloadPowerDown(_Inout_ PFWLOAD_CONTEXT Context);

#ifdef __cplusplus
}
#endif

#endif
