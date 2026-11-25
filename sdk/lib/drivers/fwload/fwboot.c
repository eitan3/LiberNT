/*
 * PROJECT:     LiberNT audio driver libraries
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Boots and powers down an ACE-class DSP with a parsed Sound Open Firmware image through an injected operations table; every wait has a timeout and every error path powers the DSP down
 * COPYRIGHT:   Copyright 2026 LiberNT contributors
 */

/*
 * The sequence below follows Linux v6.12 sound/soc/sof/intel/mtl.c (mtl_dsp_cl_init,
 * mtl_dsp_core_power_up, mtl_dsp_core_power_down, mtl_power_down_dsp, mtl_enable_interrupts,
 * mtl_enable_ipc_interrupts), hda-loader.c (hda_dsp_cl_boot_firmware, hda_cl_copy_fw) and
 * hda-ctrl.c (hda_dsp_ctrl_ppcap_enable); the register offsets and bit positions are the
 * platform table of fwload.h (mtl.h), the ROM status layout and error codes come from hda.h
 * and the firmware configuration query from ipc4-loader.c (sof_ipc4_query_fw_configuration).
 * All of them were studied under LICENSING.md rule 7: the code, names and comments are ours.
 */

#include <ntddk.h>
#include "fwload.h"

/* mtl.h MTL_HFDSSCS_SPA_MASK and MTL_HFDSSCS_CPA_MASK */
#define FWLOAD_SUBSYSTEM_SPA        0x00010000UL
#define FWLOAD_SUBSYSTEM_CPA        0x01000000UL
/* mtl.h MTL_DSP2CXCTL_PRIMARY_CORE_*: owner 2 is the host CPU, OSEL bits 25:24 */
#define FWLOAD_CORE_OWNER_HOST      0x02000000UL
#define FWLOAD_CORE_SPA             0x00000001UL
#define FWLOAD_CORE_CPA             0x00000100UL
/* mtl.h MTL_IRQ_INTEN_L_HOST_IPC_MASK and MTL_HFINTIPPTR_PTR_MASK */
#define FWLOAD_INTEN_HOST_IPC       0x00000001UL
#define FWLOAD_INT_POINTER_MASK     0x001FFFFFUL
/* What the interrupt pointer names when its bits do not name a 32-bit register of BAR4 */
#define FWLOAD_NO_INTERRUPT_ENABLE  0xFFFFFFFFUL
/* mtl.h MTL_DSP_REG_HfHIPCIE_IE_MASK, MTL_DSP_REG_HFIPCXCTL_BUSY and _DONE */
#define FWLOAD_HIPCIE_IE            0x00000001UL
#define FWLOAD_IPCCTL_BUSY_INT      0x00000001UL
#define FWLOAD_IPCCTL_DONE_INT      0x00000002UL
#define FWLOAD_FW_REGS_WINDOW_BYTES 0x40

/* A wait whose clock does not advance would never reach its budget: after this many times the polls the budget needs, it is reported as a stalled clock */
#define FWLOAD_STALLED_POLL_FACTOR  2

static
UINT32
FwloadRead(
    const FWLOAD_CONTEXT* Context,
    UINT32 Bar,
    UINT32 Offset)
{
    return Context->Ops->Read32(Context->OpsContext, Bar, Offset);
}

static
VOID
FwloadWrite(
    const FWLOAD_CONTEXT* Context,
    UINT32 Bar,
    UINT32 Offset,
    UINT32 Value)
{
    Context->Ops->Write32(Context->OpsContext, Bar, Offset, Value);
}

static
VOID
FwloadUpdate(
    const FWLOAD_CONTEXT* Context,
    UINT32 Bar,
    UINT32 Offset,
    UINT32 Mask,
    UINT32 Value)
{
    UINT32 Old = FwloadRead(Context, Bar, Offset);

    FwloadWrite(Context, Bar, Offset, (Old & ~Mask) | (Value & Mask));
}

static
UINT64
FwloadNow(
    const FWLOAD_CONTEXT* Context)
{
    return Context->Ops->NowUs(Context->OpsContext);
}

static
VOID
FwloadLog(
    const FWLOAD_CONTEXT* Context,
    const char* Text)
{
    if (Context->Ops->Log != NULL)
        Context->Ops->Log(Context->OpsContext, Text);
}

/* One poll interval, or the cancellation of the boot */
static
FWLOAD_STATUS
FwloadPause(
    const FWLOAD_CONTEXT* Context)
{
    Context->Ops->DelayUs(Context->OpsContext, Context->Timeouts.PollIntervalUs);
    return Context->Cancel ? FwloadCancelled : FwloadOk;
}

static
FWLOAD_STATUS
FwloadSettle(
    const FWLOAD_CONTEXT* Context)
{
    Context->Ops->DelayUs(Context->OpsContext, Context->Timeouts.PowerSettleUs);
    return Context->Cancel ? FwloadCancelled : FwloadOk;
}

/* The polls after which a wait that is still inside its budget has a clock that does not advance: twice the polls the budget takes at the poll interval */
static
UINT64
FwloadStallPolls(
    const FWLOAD_CONTEXT* Context,
    UINT32 TimeoutUs)
{
    UINT64 Interval = Context->Timeouts.PollIntervalUs;

    return FWLOAD_STALLED_POLL_FACTOR * (UINT64)TimeoutUs / (Interval != 0 ? Interval : 1);
}

/* Why a wait that has not met its condition stops, read at the end of every iteration of every wait: a tripped interrupt guard, a cancellation, the deadline and the poll bound; FwloadOk asks for one poll more */
static
FWLOAD_STATUS
FwloadWaitStop(
    const FWLOAD_CONTEXT* Context,
    UINT64 Start,
    UINT64 Polls,
    UINT64 StallPolls,
    UINT32 TimeoutUs,
    FWLOAD_STATUS Timeout)
{
    if (Context->InterruptStorm)
        return FwloadInterruptStorm;
    if (Context->Cancel)
        return FwloadCancelled;
    if (FwloadNow(Context) - Start >= TimeoutUs)
        return Timeout;
    if (Polls >= StallPolls)
        return FwloadClockStalled;
    return FwloadOk;
}

static
FWLOAD_STATUS
FwloadWaitMask(
    const FWLOAD_CONTEXT* Context,
    UINT32 Offset,
    UINT32 Mask,
    UINT32 Expected,
    UINT32 TimeoutUs,
    FWLOAD_STATUS Timeout)
{
    UINT64 Start = FwloadNow(Context);
    UINT64 Polls = 0, StallPolls = FwloadStallPolls(Context, TimeoutUs);
    FWLOAD_STATUS Status;

    for (;;)
    {
        if ((FwloadRead(Context, FWLOAD_BAR_DSP, Offset) & Mask) == Expected)
            return FwloadOk;
        Status = FwloadWaitStop(Context, Start, Polls, StallPolls, TimeoutUs, Timeout);
        if (Status != FwloadOk)
            return Status;
        FwloadPause(Context);
        Polls++;
    }
}

/* The ROM status register: a halted DSP reports its error code instead of a state; the NULL entry code carries no halt bit */
static
FWLOAD_STATUS
FwloadRomStatus(
    FWLOAD_CONTEXT* Context,
    UINT32 Target,
    UINT32 TimeoutUs,
    FWLOAD_STATUS Timeout)
{
    UINT64 Start = FwloadNow(Context);
    UINT64 Polls = 0, StallPolls = FwloadStallPolls(Context, TimeoutUs);
    UINT32 Raw;
    FWLOAD_STATUS Status;

    for (;;)
    {
        Raw = FwloadRead(Context, FWLOAD_BAR_DSP, Context->Platform->RomStatus);
        Context->RomStatus = Raw;
        if ((Raw & FWLOAD_FSR_HALTED) || Raw == FWLOAD_ROM_NULL_FW_ENTRY)
        {
            Context->RomError = FwloadRead(Context, FWLOAD_BAR_DSP, Context->Platform->RomError);
            return FwloadRomError;
        }
        if ((Raw & FWLOAD_FSR_STATE_MASK) == Target)
            return FwloadOk;
        Status = FwloadWaitStop(Context, Start, Polls, StallPolls, TimeoutUs, Timeout);
        if (Status != FwloadOk)
            return Status;
        FwloadPause(Context);
        Polls++;
    }
}

static
FWLOAD_STATUS
FwloadSubsystemPowerUp(
    const FWLOAD_CONTEXT* Context)
{
    FWLOAD_STATUS Status;

    FwloadUpdate(Context, FWLOAD_BAR_DSP, Context->Platform->SubsystemControl,
        FWLOAD_SUBSYSTEM_SPA, FWLOAD_SUBSYSTEM_SPA);
    Status = FwloadSettle(Context);
    if (Status != FwloadOk)
        return Status;
    return FwloadWaitMask(Context, Context->Platform->SubsystemControl,
        FWLOAD_SUBSYSTEM_CPA, FWLOAD_SUBSYSTEM_CPA, Context->Timeouts.PowerUpUs, FwloadTimeoutPowerUp);
}

static
FWLOAD_STATUS
FwloadSubsystemPowerDown(
    const FWLOAD_CONTEXT* Context)
{
    FWLOAD_STATUS Status;

    FwloadUpdate(Context, FWLOAD_BAR_DSP, Context->Platform->SubsystemControl,
        FWLOAD_SUBSYSTEM_SPA, 0);
    Status = FwloadSettle(Context);
    if (Status != FwloadOk)
        return Status;
    return FwloadWaitMask(Context, Context->Platform->SubsystemControl,
        FWLOAD_SUBSYSTEM_CPA, 0, Context->Timeouts.PowerUpUs, FwloadTimeoutPowerUp);
}

/* Only the primary core is powered by the host; an already powered core is left alone */
static
FWLOAD_STATUS
FwloadCorePowerUp(
    const FWLOAD_CONTEXT* Context)
{
    UINT32 Control = Context->Platform->CoreControl;
    FWLOAD_STATUS Status;

    if (FwloadRead(Context, FWLOAD_BAR_DSP, Control) & FWLOAD_CORE_CPA)
        return FwloadOk;
    FwloadUpdate(Context, FWLOAD_BAR_DSP, Control, FWLOAD_CORE_OWNER_HOST, FWLOAD_CORE_OWNER_HOST);
    FwloadUpdate(Context, FWLOAD_BAR_DSP, Control, FWLOAD_CORE_SPA, FWLOAD_CORE_SPA);
    Status = FwloadSettle(Context);
    if (Status != FwloadOk)
        return Status;
    return FwloadWaitMask(Context, Control, FWLOAD_CORE_CPA,
        FWLOAD_CORE_CPA, Context->Timeouts.PowerUpUs, FwloadTimeoutPowerUp);
}

static
FWLOAD_STATUS
FwloadCorePowerDown(
    const FWLOAD_CONTEXT* Context)
{
    UINT32 Control = Context->Platform->CoreControl;
    FWLOAD_STATUS Status;

    if (!(FwloadRead(Context, FWLOAD_BAR_DSP, Control) & FWLOAD_CORE_CPA))
        return FwloadOk;
    FwloadUpdate(Context, FWLOAD_BAR_DSP, Control, FWLOAD_CORE_SPA, 0);
    Status = FwloadSettle(Context);
    if (Status != FwloadOk)
        return Status;
    return FwloadWaitMask(Context, Control, FWLOAD_CORE_CPA, 0,
        Context->Timeouts.CorePowerDownUs, FwloadTimeoutPowerUp);
}

/* The caller's interrupt lock, the one the service routine runs under: the passive-level read-modify-writes of the three enables take it, so that none of them interleaves with the routine's or with another */
static
VOID
FwloadLock(
    const FWLOAD_CONTEXT* Context)
{
    if (Context->Ops->Lock != NULL)
        Context->Ops->Lock(Context->OpsContext);
}

static
VOID
FwloadUnlock(
    const FWLOAD_CONTEXT* Context)
{
    if (Context->Ops->Unlock != NULL)
        Context->Ops->Unlock(Context->OpsContext);
}

/* The register a raw interrupt pointer names, or no register: it is the DSP's to answer until the subsystem is powered, all ones or zero or an odd address name none, and the register it reaches is 32-bit; mtl.h MTL_HFINTIPPTR_PTR_MASK, bits 20:0 */
UINT32
FwloadInterruptOffset(
    UINT32 Pointer)
{
    UINT32 Offset = Pointer & FWLOAD_INT_POINTER_MASK;

    if (Pointer == 0xFFFFFFFFUL || Offset == 0 || Offset == FWLOAD_INT_POINTER_MASK ||
        (Offset & (sizeof(UINT32) - 1)) != 0)
        return FWLOAD_NO_INTERRUPT_ENABLE;
    return Offset;
}

/* The interrupt enable register, read from the pointer at every call and never kept */
UINT32
FwloadInterruptEnableOffset(
    const FWLOAD_CONTEXT* Context)
{
    return FwloadInterruptOffset(FwloadRead(Context, FWLOAD_BAR_DSP, Context->Platform->InterruptPointer));
}

/* Read-modify-write of one interrupt enable: the caller holds the interrupt lock or runs under it */
static
VOID
FwloadInterruptUpdate(
    const FWLOAD_CONTEXT* Context,
    UINT32 Offset,
    UINT32 Mask,
    UINT32 Value)
{
    FwloadUpdate(Context, FWLOAD_BAR_DSP, Offset, Mask, Value);
}

/* The three enables cleared without a wait, for the service routine, which cannot wait; nothing is written through a pointer that names no register */
static
VOID
FwloadInterruptsMask(
    const FWLOAD_CONTEXT* Context)
{
    UINT32 Offset = FwloadInterruptEnableOffset(Context);

    if (Offset != FWLOAD_NO_INTERRUPT_ENABLE)
        FwloadInterruptUpdate(Context, Offset, FWLOAD_INTEN_HOST_IPC, 0);
    FwloadInterruptUpdate(Context, Context->Platform->HostIpcInterruptEnable, FWLOAD_HIPCIE_IE, 0);
    FwloadInterruptUpdate(Context, Context->Platform->IpcControl, FWLOAD_IPCCTL_BUSY_INT | FWLOAD_IPCCTL_DONE_INT, 0);
}

VOID
FwloadReadInterruptState(
    const FWLOAD_CONTEXT* Context,
    PFWLOAD_INTERRUPT_STATE State)
{
    const FWLOAD_PLATFORM* Platform = Context->Platform;
    UINT32 Offset;

    State->Pointer = FwloadRead(Context, FWLOAD_BAR_DSP, Platform->InterruptPointer);
    Offset = FwloadInterruptOffset(State->Pointer);
    State->Enable = Offset != FWLOAD_NO_INTERRUPT_ENABLE ? FwloadRead(Context, FWLOAD_BAR_DSP, Offset) : FWLOAD_NO_INTERRUPT_ENABLE;
    State->HostIpcEnable = FwloadRead(Context, FWLOAD_BAR_DSP, Platform->HostIpcInterruptEnable);
    State->IpcControl = FwloadRead(Context, FWLOAD_BAR_DSP, Platform->IpcControl);
    State->SubsystemControl = FwloadRead(Context, FWLOAD_BAR_DSP, Platform->SubsystemControl);
}

/* The interrupt enable register the interrupt IP pointer names, the host IPC enable and the IPC interrupt enables, each read back where the register allows it. Shared with the IPC transport (sofipc), whose interrupt mode sets the three again after every exchange the service routine masked */
FWLOAD_STATUS
FwloadInterruptsEnable(
    FWLOAD_CONTEXT* Context,
    BOOLEAN Enable)
{
    UINT32 Offset = FwloadInterruptEnableOffset(Context);
    UINT32 Value = Enable ? FWLOAD_INTEN_HOST_IPC : 0;
    FWLOAD_STATUS Status;

    /* nothing is written through a pointer that names no register; an interrupt that cannot be gated is not enabled */
    if (Offset != FWLOAD_NO_INTERRUPT_ENABLE)
    {
        FwloadLock(Context);
        FwloadInterruptUpdate(Context, Offset, FWLOAD_INTEN_HOST_IPC, Value);
        FwloadUnlock(Context);
        Status = FwloadWaitMask(Context, Offset, FWLOAD_INTEN_HOST_IPC, Value, Context->Timeouts.PowerUpUs, FwloadInterruptTimeout);
        if (Status != FwloadOk)
            return Status;
    }
    else if (Enable)
        return FwloadHardwareFault;
    Value = Enable ? FWLOAD_HIPCIE_IE : 0;
    FwloadLock(Context);
    FwloadInterruptUpdate(Context, Context->Platform->HostIpcInterruptEnable, FWLOAD_HIPCIE_IE, Value);
    FwloadUnlock(Context);
    Status = FwloadWaitMask(Context, Context->Platform->HostIpcInterruptEnable, FWLOAD_HIPCIE_IE,
        Value, Context->Timeouts.PowerUpUs, FwloadInterruptTimeout);
    if (Status != FwloadOk)
        return Status;
    FwloadLock(Context);
    FwloadInterruptUpdate(Context, Context->Platform->IpcControl, FWLOAD_IPCCTL_BUSY_INT | FWLOAD_IPCCTL_DONE_INT,
        Enable ? (FWLOAD_IPCCTL_BUSY_INT | FWLOAD_IPCCTL_DONE_INT) : 0);
    FwloadUnlock(Context);
    Context->InterruptsEnabled = Enable;
    return FwloadOk;
}

/*
 * The body of the DSP's interrupt service routine, with the register reads of mtl.c mtl_ipc_irq_thread: a done bit in
 * the reply register or a busy bit in the target register means a message is pending. Nothing is acknowledged here,
 * the boot's polls consume the message; the three enables are masked because a level-triggered line left asserted would
 * re-enter at DIRQL forever, and the interrupts are counted: FWLOAD_INTERRUPT_STORM_LIMIT in a row that no poll consumed
 * a message between mean the enables do not gate the line on this part, and the routine stops claiming it.
 */
BOOLEAN
FwloadInterruptService(
    FWLOAD_CONTEXT* Context)
{
    const FWLOAD_PLATFORM* Platform = Context->Platform;
    BOOLEAN Pending;

    if (Context->InterruptStorm)
        return FALSE;
    Pending = (FwloadRead(Context, FWLOAD_BAR_DSP, Platform->IpcReply) & FWLOAD_IPC_DONE) != 0 ||
        (FwloadRead(Context, FWLOAD_BAR_DSP, Platform->IpcTarget) & FWLOAD_IPC_BUSY) != 0;
    if (!Pending)
        return FALSE;
    Context->InterruptCount++;
    if (Context->InterruptCount == 1)
        FwloadReadInterruptState(Context, &Context->FirstInterrupt);
    FwloadInterruptsMask(Context);
    Context->StormCount++;
    if (Context->StormCount >= FWLOAD_INTERRUPT_STORM_LIMIT)
        Context->InterruptStorm = TRUE;
    return TRUE;
}

/* One ROM initialisation: the purge request, the core, the ROM's acknowledgement, the interrupts and the ROM status */
static
FWLOAD_STATUS
FwloadRomInit(
    FWLOAD_CONTEXT* Context)
{
    const FWLOAD_PLATFORM* Platform = Context->Platform;
    UINT32 Request = FWLOAD_IPC_BUSY | FWLOAD_ROM_IPC_CONTROL | FWLOAD_ROM_IPC_PURGE_FW |
        ((Context->Dma.StreamTag - 1) << FWLOAD_ROM_STREAM_SHIFT);
    FWLOAD_STATUS Status;

    FwloadWrite(Context, FWLOAD_BAR_DSP, Platform->IpcRequest, Request);
    Context->CoreOn = TRUE;
    Status = FwloadCorePowerUp(Context);
    if (Status != FwloadOk)
        return Status;
    Status = FwloadWaitMask(Context, Platform->IpcReply, FWLOAD_IPC_DONE,
        FWLOAD_IPC_DONE, Context->Timeouts.RomInitUs, FwloadTimeoutRomInit);
    if (Status != FwloadOk)
        return Status;
    FwloadIpcDone(Context, TRUE);
    /* the boot polls every wait, so the interrupts stay masked unless the caller asked for them (FWLOAD_CONTEXT.UseInterrupts) */
    if (Context->UseInterrupts)
    {
        Status = FwloadInterruptsEnable(Context, TRUE);
        if (Status != FwloadOk)
            return Status;
    }
    return FwloadRomStatus(Context, FWLOAD_FSR_INIT_DONE, Context->Timeouts.RomStatusUs, FwloadTimeoutRomStatus);
}

/* The code-loader stream carries the firmware before the ROM is asked to boot it: payload, descriptors and stream registers */
static
FWLOAD_STATUS
FwloadPrepareStream(
    FWLOAD_CONTEXT* Context)
{
    const SOF_RI_IMAGE* Image = Context->Image;
    FWLOAD_STATUS Status;

    Status = FwloadClDmaPrepare(&Context->Dma, Image->Data + Image->PayloadOffset, Image->PayloadBytes);
    if (Status != FwloadOk)
        return Status;
    Status = Context->Ops->StreamSubmit(Context->OpsContext, &Context->Dma);
    if (Status != FwloadOk)
        return Status;
    Context->StreamSubmitted = TRUE;
    return FwloadOk;
}

static
FWLOAD_STATUS
FwloadCopyFirmware(
    FWLOAD_CONTEXT* Context)
{
    FWLOAD_STATUS Status, Status2;

    Status = Context->Ops->StreamTrigger(Context->OpsContext, TRUE);
    if (Status == FwloadOk)
    {
        Status = FwloadRomStatus(Context, FWLOAD_FSR_FW_ENTERED, Context->Timeouts.BaseFwUs, FwloadTimeoutBaseFw);
        Status2 = Context->Ops->StreamTrigger(Context->OpsContext, FALSE);
        if (Status == FwloadOk)
            Status = Status2;
    }
    /* the code-loader stream is given back once the copy is over, whichever way it ended */
    Context->Ops->StreamRelease(Context->OpsContext);
    Context->StreamSubmitted = FALSE;
    return Status;
}

static
VOID
FwloadTupleValue(
    PFWLOAD_FW_CONFIG Config,
    UINT32 Type,
    const UINT8* Value,
    SIZE_T Bytes)
{
    UINT32 Word = (Bytes >= 4) ? ((UINT32)Value[0] | ((UINT32)Value[1] << 8) |
        ((UINT32)Value[2] << 16) | ((UINT32)Value[3] << 24)) : 0;

    switch (Type)
    {
        case FWLOAD_TUPLE_FW_VERSION:
            if (Bytes >= 8)
            {
                Config->HasVersion = TRUE;
                Config->Major = (UINT16)(Value[0] | (Value[1] << 8));
                Config->Minor = (UINT16)(Value[2] | (Value[3] << 8));
                Config->Hotfix = (UINT16)(Value[4] | (Value[5] << 8));
                Config->Build = (UINT16)(Value[6] | (Value[7] << 8));
            }
            break;
        case FWLOAD_TUPLE_DL_MAILBOX_BYTES:
            Config->HasDownlinkMailbox = TRUE;
            Config->DownlinkMailboxBytes = Word;
            break;
        case FWLOAD_TUPLE_UL_MAILBOX_BYTES:
            Config->HasUplinkMailbox = TRUE;
            Config->UplinkMailboxBytes = Word;
            break;
        case FWLOAD_TUPLE_TRACE_LOG_BYTES:
            Config->HasTraceLog = TRUE;
            Config->TraceLogBytes = Word;
            break;
        case FWLOAD_TUPLE_MAX_PPL_COUNT:
            Config->HasMaxPipelines = TRUE;
            Config->MaxPipelines = Word;
            break;
        case FWLOAD_TUPLE_MAX_LIBS_COUNT:
            Config->HasMaxLibraries = TRUE;
            Config->MaxLibraries = Word;
            break;
        case FWLOAD_TUPLE_FW_CONTEXT_SAVE:
            Config->HasContextSave = TRUE;
            Config->ContextSave = Word;
            break;
        default:
            break;
    }
}

/* The types the firmware defines for its configuration reply (SOF base_fw.h enum ipc4_fw_config_params); most of them this loader does not read, and a reply may carry them */
static
BOOLEAN
FwloadTupleTypeKnown(
    UINT32 Type)
{
    return Type <= FWLOAD_TUPLE_TYPE_MAX || Type == FWLOAD_TUPLE_MIN_HOST_BUFFER_PERIODS ||
        Type == FWLOAD_TUPLE_FW_SOF_INFO;
}

/* sof_tlv: type, length in bytes, value; a zero header ends the reply, the rest of the mailbox is untouched. A tuple that runs past the reply is a truncated list; one whose length is not a multiple of four (sof/tlv.h tlv_next stops at it), that has no value, or whose type the firmware does not define is a bad reply, and the walk says which */
static
FWLOAD_STATUS
FwloadParseFwConfig(
    FWLOAD_CONTEXT* Context,
    const UINT8* Data,
    SIZE_T Bytes)
{
    PFWLOAD_FW_CONFIG Config = &Context->FwConfig;
    SIZE_T Offset = 0;

    while (Offset + FWLOAD_TUPLE_HEADER_BYTES <= Bytes)
    {
        UINT32 Type = (UINT32)Data[Offset] | ((UINT32)Data[Offset + 1] << 8) |
            ((UINT32)Data[Offset + 2] << 16) | ((UINT32)Data[Offset + 3] << 24);
        SIZE_T Length = (SIZE_T)((UINT32)Data[Offset + 4] | ((UINT32)Data[Offset + 5] << 8) |
            ((UINT32)Data[Offset + 6] << 16) | ((UINT32)Data[Offset + 7] << 24));

        if (Type == 0 && Length == 0)
            break;
        if (Length > Bytes - Offset - FWLOAD_TUPLE_HEADER_BYTES)
            return FwloadTruncated;
        if (Length == 0 || (Length & (sizeof(UINT32) - 1)) != 0 || !FwloadTupleTypeKnown(Type))
        {
            Config->BadTuple = TRUE;
            Config->BadTupleType = Type;
            Config->BadTupleOffset = Offset;
            return FwloadIpcBadReply;
        }
        FwloadTupleValue(Config, Type, Data + Offset + FWLOAD_TUPLE_HEADER_BYTES, Length);
        Config->TupleCount++;
        Offset += FWLOAD_TUPLE_HEADER_BYTES + Length;
    }
    return Config->TupleCount ? FwloadOk : FwloadIpcBadReply;
}

/* A firmware exception or watchdog notification while the host waits for the firmware */
static
BOOLEAN
FwloadIsCrash(
    const SOFIPC4_MSG* Message)
{
    if (!SofIpc4IsNotification(Message))
        return FALSE;
    return SofIpc4NotificationType(Message) == SOFIPC4_NOTIFY_EXCEPTION_CAUGHT ||
        SofIpc4NotificationType(Message) == SOFIPC4_NOTIFY_WATCHDOG_TIMEOUT;
}

/*
 * The receive path of a DSP-to-host message on ACE 1.0, the order of mtl.c mtl_ipc_irq_thread: a done
 * bit in the reply register is looked at first and carries a reply (header = the register without the
 * done bit, extension = the target data register, mtl.h MTL_DSP_REG_HFIPCXTDDY); a busy bit in the
 * target register carries a notification the same way. Nothing is acknowledged here: the caller
 * consumes the message first and then acknowledges it, as the irq thread runs the message handlers
 * before mtl_ipc_dsp_done and mtl_ipc_host_done. Shared with the IPC transport (sofipc), so that the boot
 * and the transport read the registers in one order.
 */
BOOLEAN
FwloadIpcReceive(
    FWLOAD_CONTEXT* Context,
    PSOFIPC4_MSG Message,
    PBOOLEAN FromReply)
{
    const FWLOAD_PLATFORM* Platform = Context->Platform;
    UINT32 Header = FwloadRead(Context, FWLOAD_BAR_DSP, Platform->IpcReply);

    if (Header & FWLOAD_IPC_DONE)
        *FromReply = TRUE;
    else
    {
        Header = FwloadRead(Context, FWLOAD_BAR_DSP, Platform->IpcTarget);
        if (!(Header & FWLOAD_IPC_BUSY))
            return FALSE;
        *FromReply = FALSE;
    }
    Message->Primary = Header & FWLOAD_IPC_MSG_MASK;
    Message->Extension = FwloadRead(Context, FWLOAD_BAR_DSP, Platform->IpcTargetData);
    Context->LastIpcPrimary = Message->Primary;
    Context->LastIpcExtension = Message->Extension;
    return TRUE;
}

/* The acknowledgement of a received message once its payload was read: a reply's done bit is written back (mtl.c mtl_ipc_dsp_done), a notification's busy bit is written back and the target acknowledgement cleared (mtl_ipc_host_done). Shared with the IPC transport (sofipc) */
VOID
FwloadIpcDone(
    FWLOAD_CONTEXT* Context,
    BOOLEAN FromReply)
{
    const FWLOAD_PLATFORM* Platform = Context->Platform;

    /* an acknowledged message, the ROM's included, is the progress the interrupt guard counts from */
    Context->StormCount = 0;
    if (FromReply)
    {
        FwloadWrite(Context, FWLOAD_BAR_DSP, Platform->IpcReply, FWLOAD_IPC_DONE);
        return;
    }
    FwloadUpdate(Context, FWLOAD_BAR_DSP, Platform->IpcTarget, FWLOAD_IPC_BUSY, FWLOAD_IPC_BUSY);
    FwloadUpdate(Context, FWLOAD_BAR_DSP, Platform->IpcTargetAck, FWLOAD_IPC_BUSY, 0);
}

static
FWLOAD_STATUS
FwloadWaitFwReady(
    FWLOAD_CONTEXT* Context)
{
    const FWLOAD_PLATFORM* Platform = Context->Platform;
    UINT8 Window[FWLOAD_FW_REGS_WINDOW_BYTES];
    SOFIPC4_MSG Message;
    BOOLEAN FromReply, WindowRead;
    UINT64 Start = FwloadNow(Context);
    UINT64 Polls = 0, StallPolls = FwloadStallPolls(Context, Context->Timeouts.FwReadyUs);
    FWLOAD_STATUS Result;

    for (;;)
    {
        if (FwloadIpcReceive(Context, &Message, &FromReply))
        {
            /* the firmware's register window is read before the notification is acknowledged */
            if (!FromReply && SofIpc4IsFwReady(&Message))
            {
                WindowRead = Context->Ops->ReadMemory(Context->OpsContext, Platform->Window0Offset, Window, sizeof(Window));
                FwloadIpcDone(Context, FromReply);
                break;
            }
            /* a crash notification fails the boot; anything else, a reply nobody asked for, is acknowledged and skipped */
            FwloadIpcDone(Context, FromReply);
            if (!FromReply && FwloadIsCrash(&Message))
            {
                Context->RomError = SofIpc4NotificationType(&Message);
                return FwloadFwCrash;
            }
        }
        /* a firmware that streams notifications faster than the polls drain them must not spin the boot: every iteration ends at the same bounds */
        Result = FwloadWaitStop(Context, Start, Polls, StallPolls, Context->Timeouts.FwReadyUs, FwloadTimeoutFwReady);
        if (Result != FwloadOk)
            return Result;
        FwloadPause(Context);
        Polls++;
    }
    if (!WindowRead)
        return FwloadHardwareFault;
    if (SofIpc4ParseFwRegisters(Window, sizeof(Window), &Context->FwConfig.Registers) != SofIpc4ParseOk)
        return FwloadAbiMismatch;
    if (Context->FwConfig.Registers.AbiVersion != SOFIPC4_FW_REGS_ABI_VER)
        return FwloadAbiMismatch;
    return FwloadOk;
}

/* The first IPC of a booted firmware: the base firmware answers its configuration as tuples in the uplink mailbox */
static
FWLOAD_STATUS
FwloadQueryFwConfig(
    FWLOAD_CONTEXT* Context)
{
    const FWLOAD_PLATFORM* Platform = Context->Platform;
    SOFIPC4_MSG Message, Reply;
    BOOLEAN FromReply, Parsed, MailboxRead;
    UINT64 Start = FwloadNow(Context);
    UINT64 Polls = 0, StallPolls = FwloadStallPolls(Context, Context->Timeouts.IpcReplyUs);
    UINT32 Status = 0;
    FWLOAD_STATUS Result;

    SofIpc4ModuleConfig(&Message, FWLOAD_BASEFW_MODULE_ID, FWLOAD_BASEFW_INSTANCE_ID,
        FWLOAD_PARAM_ID_FW_CONFIG << FWLOAD_PARAM_ID_SHIFT, TRUE);
    FwloadWrite(Context, FWLOAD_BAR_DSP, Platform->IpcRequestData, Message.Extension);
    FwloadWrite(Context, FWLOAD_BAR_DSP, Platform->IpcRequest, Message.Primary | FWLOAD_IPC_BUSY);
    for (;;)
    {
        if (FwloadIpcReceive(Context, &Reply, &FromReply))
        {
            /* the reply ends the wait; a crash notification fails the boot; anything else is acknowledged and skipped */
            if (FromReply)
                break;
            FwloadIpcDone(Context, FALSE);
            if (FwloadIsCrash(&Reply))
            {
                Context->RomError = SofIpc4NotificationType(&Reply);
                return FwloadFwCrash;
            }
        }
        /* as above: the bounds are read on every iteration, a notification storm included */
        Result = FwloadWaitStop(Context, Start, Polls, StallPolls, Context->Timeouts.IpcReplyUs, FwloadTimeoutIpcReply);
        if (Result != FwloadOk)
            return Result;
        FwloadPause(Context);
        Polls++;
    }
    /* the header is parsed and the payload read before the done bit is written back, whatever the reply says */
    Parsed = SofIpc4ParseReply(&Reply, SofIpc4ModConfigGet, SOFIPC4_TARGET_MODULE_MSG, &Status);
    MailboxRead = Context->Ops->ReadMemory(Context->OpsContext, Platform->UplinkOffset,
        Context->Mailbox, FWLOAD_MAILBOX_BYTES);
    FwloadIpcDone(Context, TRUE);
    if (!Parsed)
        return FwloadIpcBadReply;
    Context->LastIpcStatus = Status;
    if (Status != SofIpc4Success)
        return FwloadIpcError;
    if (!MailboxRead)
        return FwloadHardwareFault;
    Result = FwloadParseFwConfig(Context, Context->Mailbox, FWLOAD_MAILBOX_BYTES);
    if (Result != FwloadOk)
        return Result;
    if (!Context->FwConfig.HasVersion)
        return FwloadIpcBadReply;
    if (Context->FwConfig.Major != Context->Image->Major ||
        Context->FwConfig.Minor != Context->Image->Minor ||
        Context->FwConfig.Hotfix != Context->Image->Hotfix ||
        Context->FwConfig.Build != Context->Image->Build)
        return FwloadVersionMismatch;
    /* the host sends into the downlink mailbox and reads the uplink one from here on: a reply that ends before both sizes is cut short, one that reports a size of zero is not a configuration the host can use */
    if (!Context->FwConfig.HasDownlinkMailbox || !Context->FwConfig.HasUplinkMailbox)
        return FwloadTruncated;
    if (Context->FwConfig.DownlinkMailboxBytes == 0 || Context->FwConfig.UplinkMailboxBytes == 0)
        return FwloadIpcBadReply;
    return FwloadOk;
}

/* Everything the boot switched on goes off again, in the order of mtl_power_down_dsp and the suspend path of hda-dsp.c */
static
FWLOAD_STATUS
FwloadPowerOff(
    FWLOAD_CONTEXT* Context)
{
    FWLOAD_STATUS Status = FwloadOk, Result;

    Result = FwloadInterruptsEnable(Context, FALSE);
    if (Status == FwloadOk)
        Status = Result;
    if (Context->StreamSubmitted)
    {
        Context->Ops->StreamTrigger(Context->OpsContext, FALSE);
        Context->Ops->StreamRelease(Context->OpsContext);
        Context->StreamSubmitted = FALSE;
    }
    if (Context->CoreOn)
    {
        Result = FwloadCorePowerDown(Context);
        if (Status == FwloadOk)
            Status = Result;
        Context->CoreOn = FALSE;
    }
    if (Context->SubsystemOn)
    {
        Result = FwloadSubsystemPowerDown(Context);
        if (Status == FwloadOk)
            Status = Result;
        Context->SubsystemOn = FALSE;
    }
    FwloadUpdate(Context, FWLOAD_BAR_PP, FWLOAD_PP_OFFSET_PPCTL, FWLOAD_PPCTL_GPROCEN, 0);
    Context->State = FwloadStateOff;
    return Status;
}

static
FWLOAD_STATUS
FwloadFail(
    FWLOAD_CONTEXT* Context,
    FWLOAD_STATE State,
    FWLOAD_STATUS Status)
{
    Context->FailedState = State;
    Context->CleanupStatus = FwloadPowerOff(Context);
    Context->BootUs = FwloadNow(Context) - Context->BootUs;
    Context->State = FwloadStateFailed;
    Context->Status = Status;
    return Status;
}

VOID
FwloadDefaultTimeouts(
    PFWLOAD_TIMEOUTS Timeouts)
{
    Timeouts->PowerUpUs = FWLOAD_TIMEOUT_POWER_UP_US;
    Timeouts->RomInitUs = FWLOAD_TIMEOUT_ROM_INIT_US;
    Timeouts->RomStatusUs = FWLOAD_TIMEOUT_ROM_STATUS_US;
    Timeouts->BaseFwUs = FWLOAD_TIMEOUT_BASE_FW_US;
    Timeouts->FwReadyUs = FWLOAD_TIMEOUT_FW_READY_US;
    Timeouts->IpcReplyUs = FWLOAD_TIMEOUT_IPC_REPLY_US;
    Timeouts->PollIntervalUs = FWLOAD_POLL_INTERVAL_US;
    Timeouts->PowerSettleUs = FWLOAD_POWER_SETTLE_US;
    Timeouts->CorePowerDownUs = FWLOAD_CORE_POWER_DOWN_US;
    Timeouts->BootAttempts = FWLOAD_BOOT_ATTEMPTS;
}

/* ACE 1.0 family (Meteor Lake, Arrow Lake, Arrow Lake-S): mtl.h register offsets */
const FWLOAD_PLATFORM*
FwloadPlatformArlS(
    VOID)
{
    static const FWLOAD_PLATFORM Platform =
    {
        0x1000,     /* MTL_HFDSSCS */
        0x178D04,   /* MTL_DSP2CXCTL_PRIMARY_CORE */
        0x1108,     /* MTL_HFINTIPPTR */
        0x00000001, /* MTL_IRQ_INTEN_L_HOST_IPC_MASK */
        0x00000040, /* MTL_IRQ_INTEN_L_SOUNDWIRE_MASK */
        0x1140,     /* MTL_DSP_REG_HfHIPCIE */
        0x73210,    /* MTL_DSP_REG_HFIPCXIDR */
        0x73380,    /* MTL_DSP_REG_HFIPCXIDDY */
        0x73214,    /* MTL_DSP_REG_HFIPCXIDA */
        0x73228,    /* MTL_DSP_REG_HFIPCXCTL */
        0x73200,    /* MTL_DSP_REG_HFIPCXTDR */
        0x73204,    /* MTL_DSP_REG_HFIPCXTDA */
        0x73300,    /* MTL_DSP_REG_HFIPCXTDDY */
        0x163200,   /* MTL_DSP_REG_HFFLGPXQWY */
        0x163204,   /* MTL_DSP_REG_HFFLGPXQWY_ERROR */
        0x180000,   /* MTL_SRAM_WINDOW_OFFSET(0) */
        0x8000,
        0x181000,   /* MTL_DSP_MBOX_UPLINK_OFFSET */
        0x188000,   /* MTL_DSP_MBOX_DOWNLINK_OFFSET */
        0x1000
    };

    return &Platform;
}

const char*
FwloadStatusName(
    FWLOAD_STATUS Status)
{
    static const char* const Names[] =
    {
        "ok", "no data", "bad signature", "bad length", "truncated", "unsupported", "too many modules",
        "cancelled", "power-up timeout", "ROM init timeout", "ROM status timeout", "base firmware timeout",
        "FW_READY timeout", "IPC reply timeout", "clock stalled", "ROM error", "firmware crash", "ABI mismatch", "version mismatch",
        "IPC error", "bad IPC reply", "too many DMA fragments", "DMA buffer too small",
        "hardware fault", "bad state", "interrupt storm", "interrupt enable timeout", "out of memory"
    };

    if ((UINT32)Status >= sizeof(Names) / sizeof(Names[0]))
        return "unknown";
    return Names[Status];
}

const char*
FwloadStateName(
    FWLOAD_STATE State)
{
    static const char* const Names[] =
    {
        "off", "powering up", "ROM init", "loading", "FW_READY wait", "configuring", "ready",
        "powering down", "failed"
    };

    if ((UINT32)State >= sizeof(Names) / sizeof(Names[0]))
        return "unknown";
    return Names[State];
}

const char*
FwloadRomErrorName(
    UINT32 Code)
{
    static const struct { UINT32 Code; const char* Name; } Names[] =
    {
        { FWLOAD_ROM_CSE_ERROR, "CSE error" },
        { FWLOAD_ROM_CSE_WRONG_RESPONSE, "CSE wrong response" },
        { FWLOAD_ROM_IMR_TO_SMALL, "IMR too small" },
        { FWLOAD_ROM_BASE_FW_NOT_FOUND, "base firmware not found" },
        { FWLOAD_ROM_CSE_VALIDATION_FAILED, "CSE validation failed" },
        { FWLOAD_ROM_IPC_FATAL_ERROR, "IPC fatal error" },
        { FWLOAD_ROM_L2_CACHE_ERROR, "L2 cache error" },
        { FWLOAD_ROM_LOAD_OFFSET_TO_SMALL, "load offset too small" },
        { FWLOAD_ROM_API_PTR_INVALID, "API pointer invalid" },
        { FWLOAD_ROM_BASEFW_INCOMPAT, "base firmware incompatible" },
        { FWLOAD_ROM_UNHANDLED_INTERRUPT, "unhandled interrupt" },
        { FWLOAD_ROM_MEMORY_HOLE_ECC, "memory hole ECC" },
        { FWLOAD_ROM_KERNEL_EXCEPTION, "kernel exception" },
        { FWLOAD_ROM_USER_EXCEPTION, "user exception" },
        { FWLOAD_ROM_UNEXPECTED_RESET, "unexpected reset" },
        { FWLOAD_ROM_NULL_FW_ENTRY, "null firmware entry" }
    };
    UINT32 Index;

    for (Index = 0; Index < sizeof(Names) / sizeof(Names[0]); Index++)
        if (Names[Index].Code == Code)
            return Names[Index].Name;
    return "unknown ROM error";
}

const char*
FwloadFsrStateName(
    UINT32 State)
{
    static const struct { UINT32 State; const char* Name; } Names[] =
    {
        { FWLOAD_FSR_INIT, "init" },
        { FWLOAD_FSR_INIT_DONE, "init done" },
        { FWLOAD_FSR_FW_ENTERED, "firmware entered" },
        { FWLOAD_FSR_ROM_RESET_VECTOR_DONE, "ROM reset vector done" },
        { FWLOAD_FSR_ROM_PURGE_BOOT, "ROM purge boot" },
        { FWLOAD_FSR_ROM_RESTORE_BOOT, "ROM restore boot" },
        { FWLOAD_FSR_ROM_FW_ENTRY_POINT, "ROM firmware entry point" },
        { FWLOAD_FSR_ROM_VALIDATE_PUB_KEY, "ROM validate public key" },
        { FWLOAD_FSR_ROM_POWER_DOWN_HPSRAM, "ROM power down HP-SRAM" },
        { FWLOAD_FSR_ROM_POWER_DOWN_ULPSRAM, "ROM power down ULP-SRAM" },
        { FWLOAD_FSR_ROM_POWER_UP_ULPSRAM_STACK, "ROM power up ULP-SRAM stack" },
        { FWLOAD_FSR_ROM_POWER_UP_HPSRAM_DMA, "ROM power up HP-SRAM DMA" },
        { FWLOAD_FSR_ROM_BEFORE_EP_POINTER_READ, "ROM before entry pointer read" },
        { FWLOAD_FSR_ROM_VALIDATE_MANIFEST, "ROM validate manifest" },
        { FWLOAD_FSR_ROM_VALIDATE_FW_MODULE, "ROM validate firmware module" },
        { FWLOAD_FSR_ROM_PROTECT_IMR_REGION, "ROM protect IMR region" },
        { FWLOAD_FSR_ROM_PUSH_MODEL_ROUTINE, "ROM push model routine" },
        { FWLOAD_FSR_ROM_PULL_MODEL_ROUTINE, "ROM pull model routine" },
        { FWLOAD_FSR_ROM_VALIDATE_PKG_DIR, "ROM validate package directory" },
        { FWLOAD_FSR_ROM_VALIDATE_CPD, "ROM validate CPD" },
        { FWLOAD_FSR_ROM_VALIDATE_CSS_MAN_HEADER, "ROM validate CSS manifest header" },
        { FWLOAD_FSR_ROM_VALIDATE_BLOB_SVN, "ROM validate blob SVN" },
        { FWLOAD_FSR_ROM_VERIFY_IFWI_PARTITION, "ROM verify IFWI partition" },
        { FWLOAD_FSR_ROM_REMOVE_ACCESS_CONTROL, "ROM remove access control" },
        { FWLOAD_FSR_ROM_AUTH_BYPASS, "ROM auth bypass" },
        { FWLOAD_FSR_ROM_AUTH_ENABLED, "ROM auth enabled" },
        { FWLOAD_FSR_ROM_INIT_DMA, "ROM init DMA" },
        { FWLOAD_FSR_ROM_PURGE_FW_ENTRY, "ROM purge firmware entry" },
        { FWLOAD_FSR_ROM_PURGE_FW_END, "ROM purge firmware end" },
        { FWLOAD_FSR_ROM_CLEAN_UP_BSS_DONE, "ROM clean up BSS done" },
        { FWLOAD_FSR_ROM_IMR_RESTORE_ENTRY, "ROM IMR restore entry" },
        { FWLOAD_FSR_ROM_IMR_RESTORE_END, "ROM IMR restore end" },
        { FWLOAD_FSR_ROM_FW_MANIFEST_IN_DMA_BUFF, "ROM firmware manifest in DMA buffer" },
        { FWLOAD_FSR_ROM_LOAD_CSE_MAN_TO_IMR, "ROM load CSE manifest to IMR" },
        { FWLOAD_FSR_ROM_LOAD_FW_MAN_TO_IMR, "ROM load firmware manifest to IMR" },
        { FWLOAD_FSR_ROM_LOAD_FW_CODE_TO_IMR, "ROM load firmware code to IMR" },
        { FWLOAD_FSR_ROM_FW_LOADING_DONE, "ROM firmware loading done" },
        { FWLOAD_FSR_ROM_FW_CODE_LOADED, "ROM firmware code loaded" },
        { FWLOAD_FSR_ROM_VERIFY_IMAGE_TYPE, "ROM verify image type" },
        { FWLOAD_FSR_ROM_AUTH_API_INIT, "ROM auth API init" },
        { FWLOAD_FSR_ROM_AUTH_API_PROC, "ROM auth API proc" },
        { FWLOAD_FSR_ROM_AUTH_API_FIRST_BUSY, "ROM auth API first busy" },
        { FWLOAD_FSR_ROM_AUTH_API_FIRST_RESULT, "ROM auth API first result" },
        { FWLOAD_FSR_ROM_AUTH_API_CLEANUP, "ROM auth API cleanup" }
    };
    UINT32 Index;

    for (Index = 0; Index < sizeof(Names) / sizeof(Names[0]); Index++)
        if (Names[Index].State == State)
            return Names[Index].Name;
    return "unknown state";
}

VOID
FwloadInitContext(
    PFWLOAD_CONTEXT Context,
    const FWLOAD_OPS* Ops,
    PVOID OpsContext,
    const FWLOAD_PLATFORM* Platform,
    const SOF_RI_IMAGE* Image,
    const FWLOAD_CL_DMA* Dma,
    const FWLOAD_TIMEOUTS* Timeouts)
{
    static const FWLOAD_CONTEXT Empty;
    FWLOAD_TIMEOUTS Defaults;

    *Context = Empty;
    Context->Ops = Ops;
    Context->OpsContext = OpsContext;
    Context->Platform = Platform;
    Context->Image = Image;
    if (Dma != NULL)
        Context->Dma = *Dma;
    if (Timeouts != NULL)
        Context->Timeouts = *Timeouts;
    else
    {
        FwloadDefaultTimeouts(&Defaults);
        Context->Timeouts = Defaults;
    }
}

FWLOAD_STATUS
FwloadBoot(
    FWLOAD_CONTEXT* Context)
{
    FWLOAD_STATUS Status, Status2;

    if (Context->State != FwloadStateOff || Context->Ops == NULL || Context->OpsContext == NULL ||
        Context->Platform == NULL || Context->Image == NULL || Context->Image->Data == NULL)
        return FwloadBadState;
    Context->BootUs = FwloadNow(Context);
    Context->Attempt = 0;
    Context->Status = FwloadOk;
    Context->FailedState = FwloadStateOff;

    Context->State = FwloadStatePoweringUp;
    Context->SubsystemOn = TRUE;
    FwloadUpdate(Context, FWLOAD_BAR_PP, FWLOAD_PP_OFFSET_PPCTL, FWLOAD_PPCTL_GPROCEN, FWLOAD_PPCTL_GPROCEN);
    Status = FwloadSubsystemPowerUp(Context);
    if (Status != FwloadOk)
        return FwloadFail(Context, Context->State, Status);
    FwloadLog(Context, "subsystem powered");

    /* the boot polls: the IPC interrupts are masked before anything else touches the DSP, whatever the BIOS or a former OS left set */
    Status = FwloadInterruptsEnable(Context, FALSE);
    if (Status != FwloadOk)
        return FwloadFail(Context, Context->State, Status);

    Status = FwloadPrepareStream(Context);
    if (Status != FwloadOk)
        return FwloadFail(Context, Context->State, Status);

    Context->State = FwloadStateRomInit;
    for (Context->Attempt = 1;; Context->Attempt++)
    {
        Status = FwloadRomInit(Context);
        if (Status == FwloadOk || Status == FwloadCancelled || Context->Attempt >= Context->Timeouts.BootAttempts)
            break;
        FwloadLog(Context, "ROM init attempt failed");
        Status2 = FwloadInterruptsEnable(Context, FALSE);
        Status = FwloadCorePowerDown(Context);
        Context->CoreOn = FALSE;
        if (Status == FwloadOk)
            Status = Status2;
        if (Status != FwloadOk)
            break;
    }
    if (Status != FwloadOk)
        return FwloadFail(Context, Context->State, Status);
    FwloadLog(Context, "ROM ready for the firmware");

    Context->State = FwloadStateLoading;
    Status = FwloadCopyFirmware(Context);
    if (Status != FwloadOk)
        return FwloadFail(Context, Context->State, Status);
    FwloadLog(Context, "firmware entered");

    Context->State = FwloadStateFwReady;
    Status = FwloadWaitFwReady(Context);
    if (Status != FwloadOk)
        return FwloadFail(Context, Context->State, Status);
    FwloadLog(Context, "FW_READY received");

    Context->State = FwloadStateConfiguring;
    Status = FwloadQueryFwConfig(Context);
    if (Status != FwloadOk)
        return FwloadFail(Context, Context->State, Status);
    /* a cancellation that arrived during the last poll still powers the DSP down */
    if (Context->Cancel)
        return FwloadFail(Context, Context->State, FwloadCancelled);

    Context->State = FwloadStateReady;
    Context->BootUs = FwloadNow(Context) - Context->BootUs;
    return FwloadOk;
}

FWLOAD_STATUS
FwloadPowerDown(
    FWLOAD_CONTEXT* Context)
{
    if (Context->State == FwloadStateOff)
        return FwloadOk;
    if (Context->State == FwloadStatePoweringDown)
        return FwloadBadState;
    Context->State = FwloadStatePoweringDown;
    Context->CleanupStatus = FwloadPowerOff(Context);
    Context->Status = Context->CleanupStatus;
    return Context->Status;
}
