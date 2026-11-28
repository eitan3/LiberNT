/*
 * PROJECT:     LiberNT audio driver libraries
 * LICENSE:     GPL-3.0-or-later for this file (https://spdx.org/licenses/GPL-3.0-or-later); the definitions ported from Sound Open Firmware below are BSD-3-Clause, Intel's notice kept below (LICENSING.md §2)
 * PURPOSE:     Encodes and decodes SOF IPC4 messages, notifications, firmware registers and the extended manifest, without I/O
 * COPYRIGHT:   Copyright 2026 LiberNT contributors
 */

/*
 * The IPC4 ABI definitions in this file are ported from the Sound Open Firmware
 * project (https://github.com/thesofproject/sof) at fcebbbf22d7c08159fbb8486d5f3687da4f167c3:
 * src/include/ipc4/header.h, error_status.h, notification.h, pipeline.h, module.h, fw_reg.h
 * and src/include/kernel/ext_manifest.h.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2020, 2021, 2023 Intel Corporation. All rights reserved.
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

#ifndef _AUDIO_SOFIPC4_H_
#define _AUDIO_SOFIPC4_H_

#ifdef __cplusplus
extern "C" {
#endif

#define SOFIPC4_MESSAGE_BYTES   8

/* struct ipc_cmd_hdr: an IPC4 message is the primary and the extension register */
typedef struct _SOFIPC4_MSG
{
    UINT32 Primary;
    UINT32 Extension;
} SOFIPC4_MSG, *PSOFIPC4_MSG;

/* ipc4_message_request.primary.r and ipc4_message_reply.primary.r: payload or status in bits 0-23, type in 24-28, rsp in 29, msg_tgt in 30 */
#define SOFIPC4_PRIMARY_PAYLOAD_MASK 0x00FFFFFFUL
#define SOFIPC4_TYPE_SHIFT           24
#define SOFIPC4_TYPE_MASK            0x1FUL
#define SOFIPC4_RSP_BIT              0x20000000UL
#define SOFIPC4_MSG_TGT_BIT          0x40000000UL
#define SOFIPC4_EXTENSION_DATA_MASK  0x3FFFFFFFUL

/* enum ipc4_message_target, enum ipc4_message_direction */
#define SOFIPC4_TARGET_FW_GEN_MSG    0
#define SOFIPC4_TARGET_MODULE_MSG    1
#define SOFIPC4_DIR_MSG_REQUEST      0
#define SOFIPC4_DIR_MSG_REPLY        1

/* enum ipc4_message_type: the type field of a message addressed to the firmware */
typedef enum _SOFIPC4_GLOBAL_TYPE
{
    SofIpc4GlbBootConfig = 0,
    SofIpc4GlbRomControl = 1,
    SofIpc4GlbIpcGatewayCmd = 2,
    SofIpc4GlbPerfMeasurementsCmd = 13,
    SofIpc4GlbChainDma = 14,
    SofIpc4GlbLoadMultipleModules = 15,
    SofIpc4GlbUnloadMultipleModules = 16,
    SofIpc4GlbCreatePipeline = 17,
    SofIpc4GlbDeletePipeline = 18,
    SofIpc4GlbSetPipelineState = 19,
    SofIpc4GlbGetPipelineState = 20,
    SofIpc4GlbGetPipelineContextSize = 21,
    SofIpc4GlbSavePipeline = 22,
    SofIpc4GlbRestorePipeline = 23,
    SofIpc4GlbLoadLibrary = 24,
    SofIpc4GlbLoadLibraryPrepare = 25,
    SofIpc4GlbInternalMessage = 26,
    SofIpc4GlbNotification = 27,
    SofIpc4GlbEnterGdb = 31
} SOFIPC4_GLOBAL_TYPE;

/* enum sof_ipc4_module_type: the type field of a message addressed to a module instance */
typedef enum _SOFIPC4_MODULE_TYPE
{
    SofIpc4ModInitInstance = 0,
    SofIpc4ModConfigGet = 1,
    SofIpc4ModConfigSet = 2,
    SofIpc4ModLargeConfigGet = 3,
    SofIpc4ModLargeConfigSet = 4,
    SofIpc4ModBind = 5,
    SofIpc4ModUnbind = 6,
    SofIpc4ModSetDx = 7,
    SofIpc4ModSetD0ix = 8,
    SofIpc4ModEnterModuleRestore = 9,
    SofIpc4ModExitModuleRestore = 10,
    SofIpc4ModDeleteInstance = 11
} SOFIPC4_MODULE_TYPE;

/* enum ipc4_status: the status field of a reply */
typedef enum _SOFIPC4_STATUS
{
    SofIpc4Success = 0,
    SofIpc4ErrorInvalidParam = 1,
    SofIpc4UnknownMessageType = 2,
    SofIpc4OutOfMemory = 3,
    SofIpc4Busy = 4,
    SofIpc4BadState = 5,
    SofIpc4Failure = 6,
    SofIpc4InvalidRequest = 7,
    SofIpc4InvalidResourceId = 9,
    SofIpc4ResourceIdExists = 10,
    SofIpc4InvalidResourceState = 12,
    SofIpc4PowerTransitionFailed = 13,
    SofIpc4InvalidManifest = 14,
    SofIpc4Unavailable = 15,
    SofIpc4LoadAddressOutOfRange = 42,
    SofIpc4CseValidationFailed = 44,
    SofIpc4ModMgmtError = 100,
    SofIpc4ModLoadClFailed = 101,
    SofIpc4ModLoadInvalidHash = 102,
    SofIpc4ModInstanceExists = 103,
    SofIpc4ModNotInitialized = 104,
    SofIpc4ModInvalidId = 110,
    SofIpc4ModInstInvalidId = 111,
    SofIpc4QueueInvalidId = 112,
    SofIpc4QueueDstInvalidId = 113,
    SofIpc4InvalidCoreId = 116,
    SofIpc4ProbeDmaInjectionBufferTooSmall = 117,
    SofIpc4ProbeDmaExtractionBufferTooSmall = 118,
    SofIpc4InvalidConfigParamId = 120,
    SofIpc4InvalidConfigDataLen = 121,
    SofIpc4InvalidConfigDataStruct = 122,
    SofIpc4GatewayNotInitialized = 140,
    SofIpc4GatewayNotExist = 141,
    SofIpc4GatewayStateNotSet = 142,
    SofIpc4GatewayNotAllocated = 143,
    SofIpc4SclkAlreadyRunning = 150,
    SofIpc4MclkAlreadyRunning = 151,
    SofIpc4NoRunningSclk = 152,
    SofIpc4NoRunningMclk = 153,
    SofIpc4PipelineStateNotSet = 164,
    SofIpc4InvalidChainStateTransition = 3018
} SOFIPC4_STATUS;

/* enum ipc4_pipeline_state, enum ipc4_pipeline_priority */
#define SOFIPC4_PIPELINE_STATE_INVALID        0
#define SOFIPC4_PIPELINE_STATE_UNINITIALIZED  1
#define SOFIPC4_PIPELINE_STATE_RESET          2
#define SOFIPC4_PIPELINE_STATE_PAUSED         3
#define SOFIPC4_PIPELINE_STATE_RUNNING        4
#define SOFIPC4_PIPELINE_STATE_EOS            5
#define SOFIPC4_PIPELINE_STATE_ERROR_STOP     6
#define SOFIPC4_PIPELINE_STATE_SAVED          7
#define SOFIPC4_PIPELINE_PRIORITY_MAX         7

/* ipc4_pipeline_create.extension.r */
#define SOFIPC4_PIPELINE_CREATE_LP            0x00000001UL
#define SOFIPC4_PIPELINE_CREATE_HAS_PAYLOAD   0x20000000UL
#define SOFIPC4_PIPELINE_CREATE_CORE_MAX      0xF
#define SOFIPC4_PIPELINE_CREATE_MEM_MAX       0x7FF
#define SOFIPC4_PIPELINE_INSTANCE_MAX         0xFF

/* ipc4_pipeline_set_state.extension.r */
#define SOFIPC4_SET_STATE_MULTI_PIPELINE      0x00000001UL
#define SOFIPC4_SET_STATE_SYNC_STOP_START     0x00000002UL

/* ipc4_chain_dma.primary.r */
#define SOFIPC4_CHAIN_DMA_ALLOCATE            0x00010000UL
#define SOFIPC4_CHAIN_DMA_ENABLE              0x00020000UL
#define SOFIPC4_CHAIN_DMA_SCS                 0x00040000UL
#define SOFIPC4_CHAIN_DMA_ID_MAX              0x1F
#define SOFIPC4_CHAIN_DMA_FIFO_MAX            0xFFFFFF

/* ipc4_module_init_instance.extension.r */
#define SOFIPC4_INIT_INSTANCE_PROC_DOMAIN     0x10000000UL
#define SOFIPC4_INIT_INSTANCE_EXTENDED_INIT   0x20000000UL
#define SOFIPC4_MODULE_ID_MAX                 0xFFFF
#define SOFIPC4_INSTANCE_ID_MAX               0xFF
#define SOFIPC4_QUEUE_ID_MAX                  0x7

/* ipc4_module_large_config.extension.r */
#define SOFIPC4_LARGE_CONFIG_FINAL_BLOCK      0x10000000UL
#define SOFIPC4_LARGE_CONFIG_INIT_BLOCK       0x20000000UL
#define SOFIPC4_LARGE_CONFIG_PARAM_ID_MAX     0xFF
#define SOFIPC4_LARGE_CONFIG_DATA_MAX         0xFFFFF

/*
 * The gain module -- the firmware's volume component, a topology's "gain"
 * widget: the one configuration entry a volume change carries. The layout
 * was read in the Sound Open Firmware source at the revision named above
 * (src/audio/volume/peak_volume.h for the entry and the parameter id,
 * src/audio/volume/volume_ipc4.c for how a set is applied) and is declared
 * here in this file's own words (LICENSING.md rule 7). The target volume is
 * a linear gain in Q1.31, 0 to 0x7fffffff, unity at 0x7fffffff; a change is
 * a large-config set of parameter id SOFIPC4_GAIN_PARAM_VOLUME whose one
 * block is exactly one entry, every channel when the channel id is the
 * all-channels value. The curve type 0 is no fade: the gain applies at once.
 */
#define SOFIPC4_GAIN_PARAM_VOLUME               0
#define SOFIPC4_GAIN_ALL_CHANNELS               0xFFFFFFFFUL
#define SOFIPC4_GAIN_CURVE_NONE                 0
#define SOFIPC4_GAIN_UNITY                      0x7FFFFFFFUL

typedef struct _SOFIPC4_GAIN_CONFIG
{
    UINT32 ChannelId;
    UINT32 TargetVolume;
    UINT32 CurveType;
    UINT32 Reserved;
    /* hundreds of nanoseconds */
    UINT64 CurveDuration;
} SOFIPC4_GAIN_CONFIG, *PSOFIPC4_GAIN_CONFIG;

/* ipc4_module_set_d0ix.extension.r */
#define SOFIPC4_D0IX_WAKE                     0x00000001UL
#define SOFIPC4_D0IX_STREAMING                0x00000002UL
#define SOFIPC4_D0IX_PREVENT_POWER_GATING     0x00000004UL
#define SOFIPC4_D0IX_PREVENT_LOCAL_CLOCK_GATING 0x00000008UL

/* ipc4_module_load_library.header.r */
#define SOFIPC4_LOAD_LIBRARY_DMA_ID_MAX       0x1F
#define SOFIPC4_LOAD_LIBRARY_ID_MAX           0xF

/* enum sof_ipc4_notification_type: the notif_type field (primary bits 16-23) of a global notification */
#define SOFIPC4_NOTIFY_PHRASE_DETECTED        4
#define SOFIPC4_NOTIFY_RESOURCE_EVENT         5
#define SOFIPC4_NOTIFY_LOG_BUFFER_STATUS      6
#define SOFIPC4_NOTIFY_TIMESTAMP_CAPTURED     7
#define SOFIPC4_NOTIFY_FW_READY               8
#define SOFIPC4_NOTIFY_AUD_CLASS_RESULT       9
#define SOFIPC4_NOTIFY_EXCEPTION_CAUGHT       10
#define SOFIPC4_NOTIFY_MODULE_NOTIFICATION    12
#define SOFIPC4_NOTIFY_UAOL_EVENT             13
#define SOFIPC4_NOTIFY_PROBE_DATA_AVAILABLE   14
#define SOFIPC4_NOTIFY_WATCHDOG_TIMEOUT       15
#define SOFIPC4_NOTIFY_MANAGEMENT_SERVICE     16

#define SOFIPC4_NOTIFICATION_TYPE_SHIFT       16
#define SOFIPC4_NOTIFICATION_TYPE_MASK        0xFF

/* SOF_IPC4_FW_READY: the primary word of the FW_READY notification, and its LIB_RESTORED extension flag */
#define SOFIPC4_FW_READY_PRIMARY              0x1B080000UL
#define SOFIPC4_FW_READY_LIB_RESTORED         0x00008000UL

/* struct ipc4_fw_registers: IPC4_FW_REGS_ABI_VER, and the bytes the boot code reads (up to slave_core_sts) */
#define SOFIPC4_FW_REGS_ABI_VER               1
#define SOFIPC4_FW_REGS_BOOT_BYTES            0x28
#define SOFIPC4_FW_REGS_CORES                 8

typedef struct _SOFIPC4_FW_REGS
{
    UINT32 State;
    UINT32 Module;
    UINT32 Running;
    UINT32 LastError;
    UINT32 CurrentAState;
    UINT32 CachedImrUsage;
    UINT32 CurrentFState;
    UINT32 WakeTickPeriod;
    UINT32 ActivePipelines;
    UINT32 LastNativeError;
    UINT32 Ltr;
    UINT32 RomFuseValues;
    UINT32 RomLoadMethod;
    UINT32 RomDownlinkIpcUseDma;
    UINT32 RomImplementationRevisionMajor;
    UINT32 RomImplementationRevisionMinor;
    UINT32 RomImplementationVersionMajor;
    UINT32 RomImplementationVersionMinor;
    UINT32 RomPlatformType;
    UINT32 AbiVersion;
    UINT8 SlaveCoreStatus[SOFIPC4_FW_REGS_CORES];
} SOFIPC4_FW_REGS, *PSOFIPC4_FW_REGS;

/* struct ext_man_header, ext_man_elem_header, ext_man_fw_version: EXT_MAN_MAGIC_NUMBER and element types */
#define SOFIPC4_EXT_MAN_MAGIC                 0x6E614D58UL
#define SOFIPC4_EXT_MAN_HEADER_BYTES          16
#define SOFIPC4_EXT_MAN_ELEM_HEADER_BYTES     8
#define SOFIPC4_EXT_MAN_VERSION_ELEMENT       0
#define SOFIPC4_EXT_MAN_CONFIG_DATA_ELEMENT   5
#define SOFIPC4_EXT_MAN_CONFIG_IPC_MSG_SIZE   1
#define SOFIPC4_EXT_MAN_DATE_BYTES            12
#define SOFIPC4_EXT_MAN_TIME_BYTES            10
#define SOFIPC4_EXT_MAN_TAG_BYTES             6

typedef struct _SOFIPC4_EXT_MAN
{
    UINT32 HeaderVersion;
    UINT32 FullSize;
    UINT32 ElementCount;
    BOOLEAN HasVersion;
    UINT16 Major;
    UINT16 Minor;
    UINT16 Micro;
    UINT16 Build;
    CHAR Date[SOFIPC4_EXT_MAN_DATE_BYTES + 1];
    CHAR Time[SOFIPC4_EXT_MAN_TIME_BYTES + 1];
    CHAR Tag[SOFIPC4_EXT_MAN_TAG_BYTES + 1];
    UINT32 AbiVersion;
    UINT32 SrcHash;
    UINT32 Flags;
    BOOLEAN HasIpcMessageSize;
    UINT32 IpcMessageSize;
} SOFIPC4_EXT_MAN, *PSOFIPC4_EXT_MAN;

typedef enum _SOFIPC4_PARSE_STATUS
{
    SofIpc4ParseOk = 0,
    SofIpc4ParseNoData,
    SofIpc4ParseBadSignature,
    SofIpc4ParseBadLength,
    SofIpc4ParseTruncated
} SOFIPC4_PARSE_STATUS;

VOID
SofIpc4InitRequest(
    _Out_ SOFIPC4_MSG* Msg,
    UINT32 Type,
    UINT32 Target);

UINT32 SofIpc4MsgType(_In_ const SOFIPC4_MSG* Msg);
UINT32 SofIpc4MsgIsReply(_In_ const SOFIPC4_MSG* Msg);
UINT32 SofIpc4MsgTarget(_In_ const SOFIPC4_MSG* Msg);
const char* SofIpc4GlobalTypeName(UINT32 Type);
const char* SofIpc4ModuleName(UINT32 Type);
const char* SofIpc4StatusName(UINT32 Status);

BOOLEAN
SofIpc4ParseReply(
    _In_ const SOFIPC4_MSG* Reply,
    UINT32 ExpectedType,
    UINT32 ExpectedTarget,
    _Out_ UINT32* Status);

VOID
SofIpc4PipelineCreate(
    _Out_ SOFIPC4_MSG* Msg,
    UINT32 InstanceId,
    UINT32 Priority,
    UINT32 MemoryPages,
    UINT32 CoreId,
    UINT32 Attributes,
    UINT32 Flags);

VOID SofIpc4PipelineDelete(_Out_ SOFIPC4_MSG* Msg, UINT32 InstanceId);

VOID
SofIpc4PipelineSetState(
    _Out_ SOFIPC4_MSG* Msg,
    UINT32 InstanceId,
    UINT32 State,
    UINT32 Flags);

VOID SofIpc4PipelineGetState(_Out_ SOFIPC4_MSG* Msg, UINT32 InstanceId);
VOID SofIpc4PipelineGetContextSize(_Out_ SOFIPC4_MSG* Msg, UINT32 InstanceId);

VOID
SofIpc4ChainDma(
    _Out_ SOFIPC4_MSG* Msg,
    UINT32 HostDmaId,
    UINT32 LinkDmaId,
    UINT32 FifoSize,
    UINT32 Flags);

UINT32 SofIpc4PipelineStateReply(_In_ const SOFIPC4_MSG* Reply);
UINT32 SofIpc4PipelineContextSizeReply(_In_ const SOFIPC4_MSG* Reply);
UINT32 SofIpc4PipelineSetStateReplyFailedId(_In_ const SOFIPC4_MSG* Reply);

VOID
SofIpc4ModuleInitInstance(
    _Out_ SOFIPC4_MSG* Msg,
    UINT32 ModuleId,
    UINT32 InstanceId,
    UINT32 PipelineId,
    UINT32 ParamBlockSize,
    UINT32 CoreId,
    UINT32 Flags);

VOID
SofIpc4ModuleBind(
    _Out_ SOFIPC4_MSG* Msg,
    UINT32 ModuleId,
    UINT32 InstanceId,
    UINT32 DestinationModuleId,
    UINT32 DestinationInstanceId,
    UINT32 DestinationQueue,
    UINT32 SourceQueue,
    BOOLEAN Unbind);

VOID
SofIpc4ModuleConfig(
    _Out_ SOFIPC4_MSG* Msg,
    UINT32 ModuleId,
    UINT32 InstanceId,
    UINT32 ParamIdData,
    BOOLEAN Get);

VOID
SofIpc4ModuleLargeConfig(
    _Out_ SOFIPC4_MSG* Msg,
    UINT32 ModuleId,
    UINT32 InstanceId,
    UINT32 LargeParamId,
    UINT32 DataOffSize,
    UINT32 BlockFlags,
    BOOLEAN Get);

VOID SofIpc4ModuleDeleteInstance(_Out_ SOFIPC4_MSG* Msg, UINT32 ModuleId, UINT32 InstanceId);
VOID SofIpc4ModuleSetD0ix(_Out_ SOFIPC4_MSG* Msg, UINT32 Flags);

VOID
SofIpc4ModuleLoadLibrary(
    _Out_ SOFIPC4_MSG* Msg,
    UINT32 DmaId,
    UINT32 LibraryId,
    UINT32 LoadOffset);

BOOLEAN SofIpc4IsNotification(_In_ const SOFIPC4_MSG* Msg);
UINT32 SofIpc4NotificationType(_In_ const SOFIPC4_MSG* Msg);
const char* SofIpc4NotificationName(UINT32 Type);
BOOLEAN SofIpc4IsFwReady(_In_ const SOFIPC4_MSG* Msg);

SOFIPC4_PARSE_STATUS
SofIpc4ParseFwRegisters(
    _In_reads_bytes_opt_(Size) const UINT8* Window,
    SIZE_T Size,
    _Out_ SOFIPC4_FW_REGS* Registers);

SOFIPC4_PARSE_STATUS
SofIpc4ParseExtManifest(
    _In_reads_bytes_opt_(Size) const UINT8* Data,
    SIZE_T Size,
    _Out_ SOFIPC4_EXT_MAN* ExtMan);

const char* SofIpc4ParseStatusName(SOFIPC4_PARSE_STATUS Status);

#ifdef __cplusplus
}
#endif

#endif
