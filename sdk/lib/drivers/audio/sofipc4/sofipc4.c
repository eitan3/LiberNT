/*
 * PROJECT:     LiberNT audio driver libraries
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Encodes and decodes SOF IPC4 messages, notifications, firmware registers and the extended manifest, without I/O
 * COPYRIGHT:   Copyright 2026 LiberNT contributors
 */

#include <ntddk.h>
#include "sofipc4.h"

#define SOFIPC4_MODULE_INSTANCE_PRIMARY(ModuleId, InstanceId) \
    (((ModuleId) & 0xFFFF) | (((InstanceId) & 0xFF) << 16))

static
UINT16
SofIpc4ReadU16(
    const UINT8* Data)
{
    return (UINT16)(Data[0] | ((UINT16)Data[1] << 8));
}

static
UINT32
SofIpc4ReadU32(
    const UINT8* Data)
{
    return (UINT32)Data[0] | ((UINT32)Data[1] << 8) | ((UINT32)Data[2] << 16) |
        ((UINT32)Data[3] << 24);
}

static
void
SofIpc4CopyText(
    _Out_writes_bytes_(Capacity) CHAR* Destination,
    _In_reads_bytes_(Length) const UINT8* Source,
    SIZE_T Length,
    SIZE_T Capacity)
{
    SIZE_T Index;

    for (Index = 0; Index < Length && Index + 1 < Capacity && Source[Index] != 0; Index++)
        Destination[Index] = (CHAR)Source[Index];
    Destination[Index] = 0;
}

VOID
SofIpc4InitRequest(
    SOFIPC4_MSG* Msg,
    UINT32 Type,
    UINT32 Target)
{
    Msg->Primary = ((Type & SOFIPC4_TYPE_MASK) << SOFIPC4_TYPE_SHIFT) |
        ((Target & 1) ? SOFIPC4_MSG_TGT_BIT : 0);
    Msg->Extension = 0;
}

UINT32
SofIpc4MsgType(
    const SOFIPC4_MSG* Msg)
{
    return (Msg->Primary >> SOFIPC4_TYPE_SHIFT) & SOFIPC4_TYPE_MASK;
}

UINT32
SofIpc4MsgIsReply(
    const SOFIPC4_MSG* Msg)
{
    return (Msg->Primary & SOFIPC4_RSP_BIT) ? SOFIPC4_DIR_MSG_REPLY : SOFIPC4_DIR_MSG_REQUEST;
}

UINT32
SofIpc4MsgTarget(
    const SOFIPC4_MSG* Msg)
{
    return (Msg->Primary & SOFIPC4_MSG_TGT_BIT) ? SOFIPC4_TARGET_MODULE_MSG : SOFIPC4_TARGET_FW_GEN_MSG;
}

const char*
SofIpc4GlobalTypeName(
    UINT32 Type)
{
    static const char* const Names[] =
    {
        "BOOT_CONFIG", "ROM_CONTROL", "IPCGATEWAY_CMD", NULL, NULL, NULL, NULL, NULL, NULL, NULL,
        NULL, NULL, NULL, "PERF_MEASUREMENTS_CMD", "CHAIN_DMA", "LOAD_MULTIPLE_MODULES",
        "UNLOAD_MULTIPLE_MODULES", "CREATE_PIPELINE", "DELETE_PIPELINE", "SET_PIPELINE_STATE",
        "GET_PIPELINE_STATE", "GET_PIPELINE_CONTEXT_SIZE", "SAVE_PIPELINE", "RESTORE_PIPELINE",
        "LOAD_LIBRARY", "LOAD_LIBRARY_PREPARE", "INTERNAL_MESSAGE", "NOTIFICATION", NULL, NULL,
        NULL, "ENTER_GDB"
    };

    if (Type >= sizeof(Names) / sizeof(Names[0]) || Names[Type] == NULL)
        return "UNKNOWN";
    return Names[Type];
}

const char*
SofIpc4ModuleName(
    UINT32 Type)
{
    static const char* const Names[] =
    {
        "MOD_INIT_INSTANCE", "MOD_CONFIG_GET", "MOD_CONFIG_SET", "MOD_LARGE_CONFIG_GET",
        "MOD_LARGE_CONFIG_SET", "MOD_BIND", "MOD_UNBIND", "MOD_SET_DX", "MOD_SET_D0IX",
        "MOD_ENTER_MODULE_RESTORE", "MOD_EXIT_MODULE_RESTORE", "MOD_DELETE_INSTANCE"
    };

    if (Type >= sizeof(Names) / sizeof(Names[0]))
        return "UNKNOWN";
    return Names[Type];
}

const char*
SofIpc4StatusName(
    UINT32 Status)
{
    switch (Status)
    {
        case SofIpc4Success: return "success";
        case SofIpc4ErrorInvalidParam: return "invalid parameter";
        case SofIpc4UnknownMessageType: return "unknown message type";
        case SofIpc4OutOfMemory: return "out of memory";
        case SofIpc4Busy: return "busy";
        case SofIpc4BadState: return "bad state";
        case SofIpc4Failure: return "failure";
        case SofIpc4InvalidRequest: return "invalid request";
        case SofIpc4InvalidResourceId: return "invalid resource id";
        case SofIpc4ResourceIdExists: return "resource id exists";
        case SofIpc4InvalidResourceState: return "invalid resource state";
        case SofIpc4PowerTransitionFailed: return "power transition failed";
        case SofIpc4InvalidManifest: return "invalid manifest";
        case SofIpc4Unavailable: return "unavailable";
        case SofIpc4LoadAddressOutOfRange: return "load address out of range";
        case SofIpc4CseValidationFailed: return "CSE validation failed";
        case SofIpc4ModMgmtError: return "module management error";
        case SofIpc4ModLoadClFailed: return "module load failed";
        case SofIpc4ModLoadInvalidHash: return "module hash invalid";
        case SofIpc4ModInstanceExists: return "module instance exists";
        case SofIpc4ModNotInitialized: return "module not initialized";
        case SofIpc4ModInvalidId: return "invalid module id";
        case SofIpc4ModInstInvalidId: return "invalid module instance id";
        case SofIpc4QueueInvalidId: return "invalid queue id";
        case SofIpc4QueueDstInvalidId: return "invalid destination queue id";
        case SofIpc4InvalidCoreId: return "invalid core id";
        case SofIpc4ProbeDmaInjectionBufferTooSmall: return "probe injection buffer too small";
        case SofIpc4ProbeDmaExtractionBufferTooSmall: return "probe extraction buffer too small";
        case SofIpc4InvalidConfigParamId: return "invalid config parameter id";
        case SofIpc4InvalidConfigDataLen: return "invalid config data length";
        case SofIpc4InvalidConfigDataStruct: return "invalid config data structure";
        case SofIpc4GatewayNotInitialized: return "gateway not initialized";
        case SofIpc4GatewayNotExist: return "gateway does not exist";
        case SofIpc4GatewayStateNotSet: return "gateway state not set";
        case SofIpc4GatewayNotAllocated: return "gateway not allocated";
        case SofIpc4SclkAlreadyRunning: return "SCLK already running";
        case SofIpc4MclkAlreadyRunning: return "MCLK already running";
        case SofIpc4NoRunningSclk: return "no running SCLK";
        case SofIpc4NoRunningMclk: return "no running MCLK";
        case SofIpc4PipelineStateNotSet: return "pipeline state not set";
        case SofIpc4InvalidChainStateTransition: return "invalid chain DMA state transition";
        default: return "UNKNOWN";
    }
}

BOOLEAN
SofIpc4ParseReply(
    const SOFIPC4_MSG* Reply,
    UINT32 ExpectedType,
    UINT32 ExpectedTarget,
    UINT32* Status)
{
    *Status = 0;
    if (SofIpc4MsgIsReply(Reply) != SOFIPC4_DIR_MSG_REPLY)
        return FALSE;
    if (SofIpc4MsgType(Reply) != ExpectedType)
        return FALSE;
    if (SofIpc4MsgTarget(Reply) != ExpectedTarget)
        return FALSE;
    *Status = Reply->Primary & SOFIPC4_PRIMARY_PAYLOAD_MASK;
    return TRUE;
}

VOID
SofIpc4PipelineCreate(
    SOFIPC4_MSG* Msg,
    UINT32 InstanceId,
    UINT32 Priority,
    UINT32 MemoryPages,
    UINT32 CoreId,
    UINT32 Attributes,
    UINT32 Flags)
{
    SofIpc4InitRequest(Msg, SofIpc4GlbCreatePipeline, SOFIPC4_TARGET_FW_GEN_MSG);
    Msg->Primary |= (MemoryPages & SOFIPC4_PIPELINE_CREATE_MEM_MAX) |
        ((Priority & SOFIPC4_PIPELINE_PRIORITY_MAX) << 11) |
        ((InstanceId & SOFIPC4_PIPELINE_INSTANCE_MAX) << 16);
    Msg->Extension = ((Flags & SOFIPC4_PIPELINE_CREATE_LP) ? 1 : 0) |
        ((Attributes & 0xFFFF) << 4) |
        ((CoreId & SOFIPC4_PIPELINE_CREATE_CORE_MAX) << 20) |
        ((Flags & SOFIPC4_PIPELINE_CREATE_HAS_PAYLOAD) ? SOFIPC4_PIPELINE_CREATE_HAS_PAYLOAD : 0);
}

VOID
SofIpc4PipelineDelete(
    SOFIPC4_MSG* Msg,
    UINT32 InstanceId)
{
    SofIpc4InitRequest(Msg, SofIpc4GlbDeletePipeline, SOFIPC4_TARGET_FW_GEN_MSG);
    Msg->Primary |= (InstanceId & SOFIPC4_PIPELINE_INSTANCE_MAX) << 16;
}

VOID
SofIpc4PipelineSetState(
    SOFIPC4_MSG* Msg,
    UINT32 InstanceId,
    UINT32 State,
    UINT32 Flags)
{
    SofIpc4InitRequest(Msg, SofIpc4GlbSetPipelineState, SOFIPC4_TARGET_FW_GEN_MSG);
    Msg->Primary |= (State & 0xFFFF) | ((InstanceId & SOFIPC4_PIPELINE_INSTANCE_MAX) << 16);
    Msg->Extension = (Flags & (SOFIPC4_SET_STATE_MULTI_PIPELINE | SOFIPC4_SET_STATE_SYNC_STOP_START));
}

VOID
SofIpc4PipelineGetState(
    SOFIPC4_MSG* Msg,
    UINT32 InstanceId)
{
    SofIpc4InitRequest(Msg, SofIpc4GlbGetPipelineState, SOFIPC4_TARGET_FW_GEN_MSG);
    Msg->Primary |= InstanceId & SOFIPC4_PIPELINE_INSTANCE_MAX;
}

VOID
SofIpc4PipelineGetContextSize(
    SOFIPC4_MSG* Msg,
    UINT32 InstanceId)
{
    SofIpc4InitRequest(Msg, SofIpc4GlbGetPipelineContextSize, SOFIPC4_TARGET_FW_GEN_MSG);
    Msg->Primary |= (InstanceId & SOFIPC4_PIPELINE_INSTANCE_MAX) << 16;
}

VOID
SofIpc4ChainDma(
    SOFIPC4_MSG* Msg,
    UINT32 HostDmaId,
    UINT32 LinkDmaId,
    UINT32 FifoSize,
    UINT32 Flags)
{
    SofIpc4InitRequest(Msg, SofIpc4GlbChainDma, SOFIPC4_TARGET_FW_GEN_MSG);
    Msg->Primary |= (HostDmaId & SOFIPC4_CHAIN_DMA_ID_MAX) |
        ((LinkDmaId & SOFIPC4_CHAIN_DMA_ID_MAX) << 8) |
        (Flags & (SOFIPC4_CHAIN_DMA_ALLOCATE | SOFIPC4_CHAIN_DMA_ENABLE | SOFIPC4_CHAIN_DMA_SCS));
    Msg->Extension = FifoSize & SOFIPC4_CHAIN_DMA_FIFO_MAX;
}

UINT32
SofIpc4PipelineStateReply(
    const SOFIPC4_MSG* Reply)
{
    return Reply->Extension & 0x1F;
}

UINT32
SofIpc4PipelineContextSizeReply(
    const SOFIPC4_MSG* Reply)
{
    return Reply->Extension & 0xFFFF;
}

UINT32
SofIpc4PipelineSetStateReplyFailedId(
    const SOFIPC4_MSG* Reply)
{
    return Reply->Extension & SOFIPC4_EXTENSION_DATA_MASK;
}

VOID
SofIpc4ModuleInitInstance(
    SOFIPC4_MSG* Msg,
    UINT32 ModuleId,
    UINT32 InstanceId,
    UINT32 PipelineId,
    UINT32 ParamBlockSize,
    UINT32 CoreId,
    UINT32 Flags)
{
    SofIpc4InitRequest(Msg, SofIpc4ModInitInstance, SOFIPC4_TARGET_MODULE_MSG);
    Msg->Primary |= SOFIPC4_MODULE_INSTANCE_PRIMARY(ModuleId, InstanceId);
    Msg->Extension = (ParamBlockSize & 0xFFFF) | ((PipelineId & 0xFF) << 16) |
        ((CoreId & 0xF) << 24) |
        (Flags & (SOFIPC4_INIT_INSTANCE_PROC_DOMAIN | SOFIPC4_INIT_INSTANCE_EXTENDED_INIT));
}

VOID
SofIpc4ModuleBind(
    SOFIPC4_MSG* Msg,
    UINT32 ModuleId,
    UINT32 InstanceId,
    UINT32 DestinationModuleId,
    UINT32 DestinationInstanceId,
    UINT32 DestinationQueue,
    UINT32 SourceQueue,
    BOOLEAN Unbind)
{
    SofIpc4InitRequest(Msg, Unbind ? SofIpc4ModUnbind : SofIpc4ModBind, SOFIPC4_TARGET_MODULE_MSG);
    Msg->Primary |= SOFIPC4_MODULE_INSTANCE_PRIMARY(ModuleId, InstanceId);
    Msg->Extension = ((DestinationModuleId & 0xFFFF) | ((DestinationInstanceId & 0xFF) << 16) |
        ((DestinationQueue & SOFIPC4_QUEUE_ID_MAX) << 24) |
        ((SourceQueue & SOFIPC4_QUEUE_ID_MAX) << 27));
}

VOID
SofIpc4ModuleConfig(
    SOFIPC4_MSG* Msg,
    UINT32 ModuleId,
    UINT32 InstanceId,
    UINT32 ParamIdData,
    BOOLEAN Get)
{
    SofIpc4InitRequest(Msg, Get ? SofIpc4ModConfigGet : SofIpc4ModConfigSet, SOFIPC4_TARGET_MODULE_MSG);
    Msg->Primary |= SOFIPC4_MODULE_INSTANCE_PRIMARY(ModuleId, InstanceId);
    Msg->Extension = ParamIdData & SOFIPC4_EXTENSION_DATA_MASK;
}

VOID
SofIpc4ModuleLargeConfig(
    SOFIPC4_MSG* Msg,
    UINT32 ModuleId,
    UINT32 InstanceId,
    UINT32 LargeParamId,
    UINT32 DataOffSize,
    UINT32 BlockFlags,
    BOOLEAN Get)
{
    SofIpc4InitRequest(Msg, Get ? SofIpc4ModLargeConfigGet : SofIpc4ModLargeConfigSet,
        SOFIPC4_TARGET_MODULE_MSG);
    Msg->Primary |= SOFIPC4_MODULE_INSTANCE_PRIMARY(ModuleId, InstanceId);
    Msg->Extension = (DataOffSize & SOFIPC4_LARGE_CONFIG_DATA_MAX) |
        ((LargeParamId & SOFIPC4_LARGE_CONFIG_PARAM_ID_MAX) << 20) |
        (BlockFlags & (SOFIPC4_LARGE_CONFIG_FINAL_BLOCK | SOFIPC4_LARGE_CONFIG_INIT_BLOCK));
}

VOID
SofIpc4ModuleDeleteInstance(
    SOFIPC4_MSG* Msg,
    UINT32 ModuleId,
    UINT32 InstanceId)
{
    SofIpc4InitRequest(Msg, SofIpc4ModDeleteInstance, SOFIPC4_TARGET_MODULE_MSG);
    Msg->Primary |= SOFIPC4_MODULE_INSTANCE_PRIMARY(ModuleId, InstanceId);
}

VOID
SofIpc4ModuleSetD0ix(
    SOFIPC4_MSG* Msg,
    UINT32 Flags)
{
    SofIpc4InitRequest(Msg, SofIpc4ModSetD0ix, SOFIPC4_TARGET_MODULE_MSG);
    Msg->Extension = Flags & (SOFIPC4_D0IX_WAKE | SOFIPC4_D0IX_STREAMING |
        SOFIPC4_D0IX_PREVENT_POWER_GATING | SOFIPC4_D0IX_PREVENT_LOCAL_CLOCK_GATING);
}

VOID
SofIpc4ModuleLoadLibrary(
    SOFIPC4_MSG* Msg,
    UINT32 DmaId,
    UINT32 LibraryId,
    UINT32 LoadOffset)
{
    SofIpc4InitRequest(Msg, SofIpc4GlbLoadLibrary, SOFIPC4_TARGET_FW_GEN_MSG);
    Msg->Primary |= (DmaId & SOFIPC4_LOAD_LIBRARY_DMA_ID_MAX) |
        ((LibraryId & SOFIPC4_LOAD_LIBRARY_ID_MAX) << 16);
    Msg->Extension = LoadOffset & SOFIPC4_EXTENSION_DATA_MASK;
}

BOOLEAN
SofIpc4IsNotification(
    const SOFIPC4_MSG* Msg)
{
    return SofIpc4MsgType(Msg) == SofIpc4GlbNotification && !(Msg->Primary & SOFIPC4_RSP_BIT);
}

UINT32
SofIpc4NotificationType(
    const SOFIPC4_MSG* Msg)
{
    return (Msg->Primary >> SOFIPC4_NOTIFICATION_TYPE_SHIFT) & SOFIPC4_NOTIFICATION_TYPE_MASK;
}

const char*
SofIpc4NotificationName(
    UINT32 Type)
{
    switch (Type)
    {
        case SOFIPC4_NOTIFY_PHRASE_DETECTED: return "PHRASE_DETECTED";
        case SOFIPC4_NOTIFY_RESOURCE_EVENT: return "RESOURCE_EVENT";
        case SOFIPC4_NOTIFY_LOG_BUFFER_STATUS: return "LOG_BUFFER_STATUS";
        case SOFIPC4_NOTIFY_TIMESTAMP_CAPTURED: return "TIMESTAMP_CAPTURED";
        case SOFIPC4_NOTIFY_FW_READY: return "FW_READY";
        case SOFIPC4_NOTIFY_AUD_CLASS_RESULT: return "AUD_CLASS_RESULT";
        case SOFIPC4_NOTIFY_EXCEPTION_CAUGHT: return "EXCEPTION_CAUGHT";
        case SOFIPC4_NOTIFY_MODULE_NOTIFICATION: return "MODULE_NOTIFICATION";
        case SOFIPC4_NOTIFY_UAOL_EVENT: return "UAOL_EVENT";
        case SOFIPC4_NOTIFY_PROBE_DATA_AVAILABLE: return "PROBE_DATA_AVAILABLE";
        case SOFIPC4_NOTIFY_WATCHDOG_TIMEOUT: return "WATCHDOG_TIMEOUT";
        case SOFIPC4_NOTIFY_MANAGEMENT_SERVICE: return "MANAGEMENT_SERVICE";
        default: return "UNKNOWN";
    }
}

BOOLEAN
SofIpc4IsFwReady(
    const SOFIPC4_MSG* Msg)
{
    return Msg->Primary == SOFIPC4_FW_READY_PRIMARY;
}

SOFIPC4_PARSE_STATUS
SofIpc4ParseFwRegisters(
    const UINT8* Window,
    SIZE_T Size,
    SOFIPC4_FW_REGS* Registers)
{
    static const SOFIPC4_FW_REGS Empty;
    UINT32 Fsr, Fps, Rom;
    SIZE_T Core;

    *Registers = Empty;
    if (Window == NULL || Size == 0)
        return SofIpc4ParseNoData;
    if (Size < SOFIPC4_FW_REGS_BOOT_BYTES)
        return SofIpc4ParseTruncated;

    Fsr = SofIpc4ReadU32(Window);
    Registers->State = Fsr & 0x0FFFFFFF;
    Registers->Module = (Fsr >> 28) & 0x7;
    Registers->Running = Fsr >> 31;
    Registers->LastError = SofIpc4ReadU32(Window + 4);

    Fps = SofIpc4ReadU32(Window + 8);
    Registers->CurrentAState = Fps & 0xF;
    Registers->CachedImrUsage = (Fps >> 4) & 0x1;
    Registers->CurrentFState = (Fps >> 5) & 0x7;
    Registers->WakeTickPeriod = (Fps >> 8) & 0x1F;
    Registers->ActivePipelines = (Fps >> 13) & 0x3F;

    Registers->LastNativeError = SofIpc4ReadU32(Window + 12);
    Registers->Ltr = SofIpc4ReadU32(Window + 16);

    Rom = SofIpc4ReadU32(Window + 24);
    Registers->RomFuseValues = Rom & 0xFF;
    Registers->RomLoadMethod = (Rom >> 8) & 0x1;
    Registers->RomDownlinkIpcUseDma = (Rom >> 9) & 0x1;
    Registers->RomImplementationRevisionMinor = (Rom >> 12) & 0xF;
    Registers->RomImplementationRevisionMajor = (Rom >> 16) & 0xF;
    Registers->RomImplementationVersionMinor = (Rom >> 20) & 0xF;
    Registers->RomImplementationVersionMajor = (Rom >> 24) & 0xF;
    Registers->RomPlatformType = (Rom >> 16) & 0xFFF;

    Registers->AbiVersion = SofIpc4ReadU32(Window + 28);
    for (Core = 0; Core < SOFIPC4_FW_REGS_CORES; Core++)
        Registers->SlaveCoreStatus[Core] = Window[32 + Core];
    return SofIpc4ParseOk;
}

SOFIPC4_PARSE_STATUS
SofIpc4ParseExtManifest(
    const UINT8* Data,
    SIZE_T Size,
    SOFIPC4_EXT_MAN* ExtMan)
{
    static const SOFIPC4_EXT_MAN Empty;
    SOFIPC4_PARSE_STATUS Status = SofIpc4ParseOk;
    SIZE_T Offset, FullSize;

    *ExtMan = Empty;
    if (Data == NULL || Size == 0)
        return SofIpc4ParseNoData;
    if (Size < SOFIPC4_EXT_MAN_HEADER_BYTES)
        return SofIpc4ParseTruncated;
    if (SofIpc4ReadU32(Data) != SOFIPC4_EXT_MAN_MAGIC)
        return SofIpc4ParseBadSignature;

    FullSize = SofIpc4ReadU32(Data + 4);
    Offset = SofIpc4ReadU32(Data + 8);
    ExtMan->HeaderVersion = SofIpc4ReadU32(Data + 12);
    if (FullSize < SOFIPC4_EXT_MAN_HEADER_BYTES || FullSize > Size ||
        Offset < SOFIPC4_EXT_MAN_HEADER_BYTES || Offset > FullSize)
        return SofIpc4ParseBadLength;
    ExtMan->FullSize = (UINT32)FullSize;

    while (Offset + SOFIPC4_EXT_MAN_ELEM_HEADER_BYTES <= FullSize)
    {
        UINT32 Type = SofIpc4ReadU32(Data + Offset);
        SIZE_T ElemSize = SofIpc4ReadU32(Data + Offset + 4);

        if (ElemSize < SOFIPC4_EXT_MAN_ELEM_HEADER_BYTES || ElemSize > FullSize - Offset)
        {
            Status = SofIpc4ParseTruncated;
            break;
        }
        ExtMan->ElementCount++;

        if (Type == SOFIPC4_EXT_MAN_VERSION_ELEMENT && ElemSize >= 72 && !ExtMan->HasVersion)
        {
            const UINT8* Version = Data + Offset + 8;

            ExtMan->HasVersion = TRUE;
            ExtMan->Major = SofIpc4ReadU16(Version + 4);
            ExtMan->Minor = SofIpc4ReadU16(Version + 6);
            ExtMan->Micro = SofIpc4ReadU16(Version + 8);
            ExtMan->Build = SofIpc4ReadU16(Version + 10);
            SofIpc4CopyText(ExtMan->Date, Version + 12, SOFIPC4_EXT_MAN_DATE_BYTES, sizeof(ExtMan->Date));
            SofIpc4CopyText(ExtMan->Time, Version + 24, SOFIPC4_EXT_MAN_TIME_BYTES, sizeof(ExtMan->Time));
            SofIpc4CopyText(ExtMan->Tag, Version + 34, SOFIPC4_EXT_MAN_TAG_BYTES, sizeof(ExtMan->Tag));
            ExtMan->AbiVersion = SofIpc4ReadU32(Version + 40);
            ExtMan->SrcHash = SofIpc4ReadU32(Version + 44);
            ExtMan->Flags = SofIpc4ReadU32(Version + 60);
        }
        else if (Type == SOFIPC4_EXT_MAN_CONFIG_DATA_ELEMENT)
        {
            SIZE_T Pair;

            for (Pair = SOFIPC4_EXT_MAN_ELEM_HEADER_BYTES;
                 Pair + 8 <= ElemSize && !ExtMan->HasIpcMessageSize;
                 Pair += 8)
            {
                if (SofIpc4ReadU32(Data + Offset + Pair) == SOFIPC4_EXT_MAN_CONFIG_IPC_MSG_SIZE)
                {
                    ExtMan->HasIpcMessageSize = TRUE;
                    ExtMan->IpcMessageSize = SofIpc4ReadU32(Data + Offset + Pair + 4);
                }
            }
        }
        Offset += ElemSize;
    }
    if (Offset != FullSize && Status == SofIpc4ParseOk)
        Status = SofIpc4ParseTruncated;
    return Status;
}

const char*
SofIpc4ParseStatusName(
    SOFIPC4_PARSE_STATUS Status)
{
    static const char* const Names[] =
    {
        "ok", "no data", "bad signature", "bad length", "truncated"
    };

    if ((UINT32)Status >= sizeof(Names) / sizeof(Names[0]))
        return "unknown";
    return Names[Status];
}
