/*
 * PROJECT:     LiberNT host-native tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     A DSP model of the ACE 1.0 boot path: registers, virtual-time ROM and firmware progress, the code-loader consumer, FW_READY and the first IPC, with injectable faults
 * COPYRIGHT:   Copyright 2026 LiberNT contributors
 */

/*
 * The behaviour modelled is the public one: the register set and the state codes of the
 * ACE 1.0 ROM (Linux mtl.h), the ROM error codes (hda.h), the IPC4 notification and reply
 * formats (SOF src/include/ipc4) and the firmware configuration tuples (SOF
 * src/include/sof/tlv.h). The timings are model values; no hardware was measured.
 */

#ifdef FAKE_DSP_KERNEL
#include <kmt_test.h>
#else
#include "shim.h"
#endif
#include "fake_dsp.h"

#define FAKE_DSP_UNSTABLE_US       900
#define FAKE_DSP_STREAM_CONSUME_US 100
#define FAKE_DSP_ROM_STEP_US       50
#define FAKE_DSP_CSS_BYTES_READ    48

#define FAKE_DSP_CONSUME    16
/* The ROM's own reading of the container layout (rimage manifest.h, cse.h, css.h): independent of the host reader */
#define FAKE_RI_CPD_ENTRY_OFFSET    12
#define FAKE_RI_CSS_VENDOR          16
#define FAKE_RI_CSS_ID              28
#define FAKE_RI_FW_HDR_LEN          4
#define FAKE_RI_FW_HDR_MAJOR        28
#define FAKE_RI_FW_HDR_MINOR        30
#define FAKE_RI_FW_HDR_HOTFIX       32
#define FAKE_RI_FW_HDR_BUILD        34
#define FAKE_RI_FW_HDR_MODULE_COUNT 36
#define FAKE_RI_MOD_SEGMENT         80
#define FAKE_RI_MOD_SEGMENT_BYTES   12
#define FAKE_RI_SEG_LOAD            2
#define FAKE_RI_MODULE_BYTES        116
#define FAKE_RI_PAGE_BYTES          4096
#define FAKE_DSP_FW_MAJOR           2
/* The firmware's own configuration tuple types (SOF src/include/ipc4/base_fw.h), independent of the host reader */
#define FAKE_TUPLE_FW_VERSION        0
#define FAKE_TUPLE_DL_MAILBOX       6
#define FAKE_TUPLE_UL_MAILBOX       7
#define FAKE_TUPLE_TRACE_LOG        8
#define FAKE_TUPLE_MAX_PPL          9
#define FAKE_TUPLE_MAX_LIBS         17
#define FAKE_TUPLE_CONTEXT_SAVE     29
#define FAKE_DSP_SUBSYSTEM_CPA 0x01000000UL
#define FAKE_DSP_SUBSYSTEM_SPA 0x00010000UL
#define FAKE_DSP_CORE_CPA       0x00000100UL
#define FAKE_DSP_CORE_SPA       0x00000001UL
#define FAKE_DSP_IPC_BUSY       0x80000000UL
#define FAKE_DSP_IPC_DONE       0x80000000UL

static
VOID
FakeDspRomStart(
    PFAKE_DSP Dsp);

static
UINT32
FakeDspLe32(
    const UINT8* Data)
{
    return (UINT32)Data[0] | ((UINT32)Data[1] << 8) | ((UINT32)Data[2] << 16) | ((UINT32)Data[3] << 24);
}

static
UINT16
FakeDspLe16(
    const UINT8* Data)
{
    return (UINT16)((UINT16)Data[0] | ((UINT16)Data[1] << 8));
}

static
VOID
FakeDspPut32(
    UINT8* Data,
    UINT32 Value)
{
    Data[0] = (UINT8)Value;
    Data[1] = (UINT8)(Value >> 8);
    Data[2] = (UINT8)(Value >> 16);
    Data[3] = (UINT8)(Value >> 24);
}

static
VOID
FakeDspPut16(
    UINT8* Data,
    UINT16 Value)
{
    Data[0] = (UINT8)Value;
    Data[1] = (UINT8)(Value >> 8);
}

static
VOID
FakeDspCopy(
    UINT8* Destination,
    const UINT8* Source,
    SIZE_T Bytes)
{
    SIZE_T Index;

    for (Index = 0; Index < Bytes; Index++)
        Destination[Index] = Source[Index];
}

static
VOID
FakeDspSet(
    UINT8* Destination,
    UINT8 Value,
    SIZE_T Bytes)
{
    SIZE_T Index;

    for (Index = 0; Index < Bytes; Index++)
        Destination[Index] = Value;
}

static
VOID
FakeDspPower(
    PFAKE_DSP Dsp,
    UINT32 Event)
{
    if (Dsp->PowerOrderCount < sizeof(Dsp->PowerOrder) / sizeof(Dsp->PowerOrder[0]))
        Dsp->PowerOrder[Dsp->PowerOrderCount++] = Event;
    Dsp->PowerEvents |= 1UL << Event;
}

static
VOID
FakeDspEventLog(
    PFAKE_DSP Dsp,
    UINT32 Kind)
{
    if (Dsp->EventLogCount >= FAKE_DSP_EVENT_LOG)
        return;
    Dsp->EventLog[Dsp->EventLogCount] = Kind;
    Dsp->EventLogAt[Dsp->EventLogCount] = Dsp->Clock;
    Dsp->EventLogCount++;
}

static
BOOLEAN
FakeDspSchedule(
    PFAKE_DSP Dsp,
    UINT64 At,
    UINT32 Kind,
    UINT32 Value)
{
    FAKE_DSP_EVENT* Event = NULL;
    FAKE_DSP_EVENT** Link;
    UINT32 Index;

    /* a pool slot is free while it carries no kind */
    for (Index = 0; Index < FAKE_DSP_EVENT_POOL; Index++)
    {
        if (Dsp->EventPool[Index].Kind == 0)
        {
            Event = &Dsp->EventPool[Index];
            break;
        }
    }
    if (Event == NULL)
        return FALSE;
    Event->At = At;
    Event->Kind = Kind;
    Event->Value = Value;
    Event->Next = NULL;
    for (Link = &Dsp->Event; *Link != NULL && (*Link)->At <= At; Link = &(*Link)->Next)
        ;
    Event->Next = *Link;
    *Link = Event;
    Dsp->EventCount++;
    return TRUE;
}

/* A free pool slot is one with no kind; a fired or cancelled event gets its kind cleared */
static
VOID
FakeDspRelease(
    FAKE_DSP_EVENT* Event)
{
    Event->Kind = 0;
    Event->Next = NULL;
}

/* A core that is powered down loses its ROM and firmware progress: everything but the power events is dropped */
static
VOID
FakeDspCancelRom(
    PFAKE_DSP Dsp)
{
    FAKE_DSP_EVENT** Link = &Dsp->Event;

    while (*Link != NULL)
    {
        FAKE_DSP_EVENT* Event = *Link;

        if (Event->Kind < FAKE_EVENT_SUBSYSTEM_ON || Event->Kind > FAKE_EVENT_CORE_OFF)
        {
            *Link = Event->Next;
            Dsp->EventCount--;
            FakeDspRelease(Event);
        }
        else
            Link = &Event->Next;
    }
}

/* Maps a physical address of the code-loader buffer to the offset in the host's virtual copy */
static
BOOLEAN
FakeDspSource(
    PFAKE_DSP Dsp,
    UINT64 Address,
    SIZE_T* VirtualOffset)
{
    SIZE_T Index, Base = 0;
    const FWLOAD_CL_DMA* Dma = Dsp->Dma;

    for (Index = 0; Index < Dma->SegmentCount; Index++)
    {
        if (Address >= Dma->Segment[Index].Address &&
            Address - Dma->Segment[Index].Address < Dma->Segment[Index].Bytes)
        {
            *VirtualOffset = Base + (SIZE_T)(Address - Dma->Segment[Index].Address);
            return TRUE;
        }
        Base += Dma->Segment[Index].Bytes;
    }
    return FALSE;
}

/* The ROM's view of the streamed payload: the directory, the signature header, the firmware header and the module windows */
static
UINT32
FakeDspConsume(
    PFAKE_DSP Dsp)
{
    const FWLOAD_CL_DMA* Dma = Dsp->Dma;
    SIZE_T Index, Total = 0, Offset;
    UINT32 Entries, Modules, Css = 0, Halt = 0;
    const UINT8* Image;

    if (!Dsp->StreamRunning || Dma->Virtual == NULL)
        return FWLOAD_ROM_BASE_FW_NOT_FOUND;
    for (Index = 0; Index < Dma->FragmentCount; Index++)
        Total += Dma->Bdl[Index].Bytes;
    Dsp->LoadedBytes = (UINT32)Total;
    if (Total < SOF_RI_FW_HDR_OFFSET + SOF_RI_FW_HDR_BYTES)
        return FWLOAD_ROM_BASE_FW_NOT_FOUND;
    Image = Dma->Virtual;
    if (FakeDspLe32(Image) != SOF_RI_CPD_MAGIC || Total < SOF_RI_CPD_HEADER_BYTES_MIN)
        return FWLOAD_ROM_BASE_FW_NOT_FOUND;
    Entries = FakeDspLe32(Image + 4);
    Offset = Image[10];
    for (Index = 0; Index < Entries && Index < 8; Index++)
    {
        SIZE_T Entry = Offset + Index * SOF_RI_CPD_ENTRY_BYTES;

        if (Entry + SOF_RI_CPD_ENTRY_BYTES > Total)
            return FWLOAD_ROM_BASE_FW_NOT_FOUND;
        if (Image[Entry] == 'A' && Image[Entry + 1] == 'D' && Image[Entry + 2] == 'S' &&
            Image[Entry + 3] == 'P' && Image[Entry + 4] == '.' && Image[Entry + 5] == 'm' &&
            Image[Entry + 6] == 'a' && Image[Entry + 7] == 'n')
            Css = FakeDspLe32(Image + Entry + FAKE_RI_CPD_ENTRY_OFFSET);
    }
    if (Css < SOF_RI_CPD_HEADER_BYTES_MIN || (SIZE_T)Css + FAKE_DSP_CSS_BYTES_READ > Total)
        return FWLOAD_ROM_BASE_FW_NOT_FOUND;
    if (FakeDspLe32(Image + Css) != SOF_RI_CSS_MODULE_TYPE ||
        FakeDspLe32(Image + Css + FAKE_RI_CSS_ID) != SOF_RI_CSS_HEADER_ID ||
        FakeDspLe32(Image + Css + FAKE_RI_CSS_VENDOR) != 0x8086)
        return FWLOAD_ROM_CSE_VALIDATION_FAILED;
    if (FakeDspLe32(Image + SOF_RI_FW_HDR_OFFSET) != SOF_RI_FW_HDR_MAGIC)
        return FWLOAD_ROM_BASE_FW_NOT_FOUND;
    /* a model rule: the ROM of this family takes firmware of major version 2 only */
    if (FakeDspLe16(Image + SOF_RI_FW_HDR_OFFSET + FAKE_RI_FW_HDR_MAJOR) != FAKE_DSP_FW_MAJOR)
        return FWLOAD_ROM_BASEFW_INCOMPAT;
    Dsp->Version[0] = (UINT16)(FakeDspLe16(Image + SOF_RI_FW_HDR_OFFSET + FAKE_RI_FW_HDR_MAJOR) +
        Dsp->Faults.ReportedVersionShift);
    Dsp->Version[1] = FakeDspLe16(Image + SOF_RI_FW_HDR_OFFSET + FAKE_RI_FW_HDR_MINOR);
    Dsp->Version[2] = FakeDspLe16(Image + SOF_RI_FW_HDR_OFFSET + FAKE_RI_FW_HDR_HOTFIX);
    Dsp->Version[3] = FakeDspLe16(Image + SOF_RI_FW_HDR_OFFSET + FAKE_RI_FW_HDR_BUILD);
    Modules = FakeDspLe32(Image + SOF_RI_FW_HDR_OFFSET + FAKE_RI_FW_HDR_MODULE_COUNT);
    Offset = SOF_RI_FW_HDR_OFFSET + FakeDspLe32(Image + SOF_RI_FW_HDR_OFFSET + FAKE_RI_FW_HDR_LEN);
    if (Offset + (SIZE_T)Modules * FAKE_RI_MODULE_BYTES > Total)
        return FWLOAD_ROM_BASE_FW_NOT_FOUND;
    for (Index = 0; Index < Modules && Halt == 0; Index++)
    {
        UINT32 Segment;

        for (Segment = 0; Segment < 3; Segment++)
        {
            SIZE_T At = Offset + Index * FAKE_RI_MODULE_BYTES + FAKE_RI_MOD_SEGMENT + Segment * FAKE_RI_MOD_SEGMENT_BYTES;
            UINT32 Flags = FakeDspLe32(Image + At);
            UINT32 Pages = (Flags >> 16) & 0xFFFF;
            SIZE_T End = (SIZE_T)FakeDspLe32(Image + At + 8) + (SIZE_T)Pages * FAKE_RI_PAGE_BYTES;

            if (Pages && (Flags & (1 << FAKE_RI_SEG_LOAD)) && End > Total)
                Halt = FWLOAD_ROM_IMR_TO_SMALL;
        }
    }
    return Halt;
}

static
VOID
FakeDspPutTuple(
    PFAKE_DSP Dsp,
    UINT32* At,
    UINT32 Type,
    UINT32 Value,
    UINT32 Length)
{
    UINT8* Window = Dsp->Window + (Dsp->Platform->UplinkOffset - Dsp->Platform->Window0Offset);

    FakeDspPut32(Window + *At, Type);
    FakeDspPut32(Window + *At + 4, Length);
    if (Length == 8)
    {
        FakeDspPut16(Window + *At + 8, (UINT16)Value);
        FakeDspPut16(Window + *At + 10, (UINT16)(Value >> 16));
        FakeDspPut16(Window + *At + 12, Dsp->Version[2]);
        FakeDspPut16(Window + *At + 14, Dsp->Version[3]);
    }
    else
        FakeDspPut32(Window + *At + 8, Value);
    *At += 8 + Length;
}

/* The firmware's configuration reply: the tuples the base firmware reports, its version taken from the streamed image */
static
VOID
FakeDspWriteFwConfig(
    PFAKE_DSP Dsp)
{
    UINT32 At = 0;

    FakeDspSet(Dsp->Window + (Dsp->Platform->UplinkOffset - Dsp->Platform->Window0Offset), 0, FWLOAD_MAILBOX_BYTES);
    if (Dsp->Faults.TupleFault != FAKE_DSP_TUPLES_NO_VERSION)
        FakeDspPutTuple(Dsp, &At, FAKE_TUPLE_FW_VERSION,
            (UINT32)Dsp->Version[0] | ((UINT32)Dsp->Version[1] << 16), 8);
    FakeDspPutTuple(Dsp, &At, 1, 1, 4);
    FakeDspPutTuple(Dsp, &At, FAKE_TUPLE_DL_MAILBOX, FWLOAD_MAILBOX_BYTES, 4);
    FakeDspPutTuple(Dsp, &At, FAKE_TUPLE_UL_MAILBOX, FWLOAD_MAILBOX_BYTES, 4);
    FakeDspPutTuple(Dsp, &At, FAKE_TUPLE_TRACE_LOG, 0x2000, 4);
    FakeDspPutTuple(Dsp, &At, FAKE_TUPLE_MAX_PPL, 32, 4);
    FakeDspPutTuple(Dsp, &At, FAKE_TUPLE_MAX_LIBS, 16, 4);
    FakeDspPutTuple(Dsp, &At, FAKE_TUPLE_CONTEXT_SAVE, 0, 4);
    if (Dsp->Faults.TupleFault == FAKE_DSP_TUPLES_LENGTH_OVERRUN)
    {
        UINT8* Window = Dsp->Window + (Dsp->Platform->UplinkOffset - Dsp->Platform->Window0Offset);

        FakeDspPut32(Window + At, 0x55);
        FakeDspPut32(Window + At + 4, 0x7FFFFFF0);
    }
    else if (Dsp->Faults.TupleFault == FAKE_DSP_TUPLES_NO_TERMINATOR)
    {
        FakeDspSet(Dsp->Window + (Dsp->Platform->UplinkOffset - Dsp->Platform->Window0Offset) + At, 0xFF,
            FWLOAD_MAILBOX_BYTES - At);
    }
}

static
VOID
FakeDspFire(
    PFAKE_DSP Dsp,
    FAKE_DSP_EVENT* Event)
{
    SOFIPC4_MSG Message;
    UINT32 Kind = Event->Kind;
    UINT32 Value = Event->Value;
    UINT32 Halt;

    FakeDspRelease(Event);
    if (Kind < 16)
        FakeDspEventLog(Dsp, Kind);
    switch (Kind)
    {
        case FAKE_EVENT_SUBSYSTEM_ON:
            Dsp->SubsystemControl |= FAKE_DSP_SUBSYSTEM_CPA;
            break;
        case FAKE_EVENT_SUBSYSTEM_OFF:
            Dsp->SubsystemControl &= ~FAKE_DSP_SUBSYSTEM_CPA;
            break;
        case FAKE_EVENT_CORE_ON:
            Dsp->CoreControl |= FAKE_DSP_CORE_CPA;
            Dsp->CorePowered = TRUE;
            Dsp->CorePowerUps++;
            if (Dsp->RomRequestPending)
                FakeDspRomStart(Dsp);
            break;
        case FAKE_EVENT_CORE_OFF:
            Dsp->CoreControl &= ~FAKE_DSP_CORE_CPA;
            Dsp->CorePowered = FALSE;
            FakeDspCancelRom(Dsp);
            Dsp->RomStarted = FALSE;
            Dsp->RomRequestPending = FALSE;
            Dsp->IpcReply &= ~FAKE_DSP_IPC_DONE;
            Dsp->ReplyPending = FALSE;
            Dsp->RomStatus = FWLOAD_FSR_INIT;
            break;
        case FAKE_EVENT_ROM_ACK:
            Dsp->IpcReply |= FAKE_DSP_IPC_DONE;
            break;
        case FAKE_EVENT_ROM_READY:
            Dsp->RomStatus = FWLOAD_FSR_INIT_DONE;
            break;
        case FAKE_DSP_CONSUME:
            if (Dsp->Halted != 0)
                break;
            Halt = FakeDspConsume(Dsp);
            if (Halt != 0 && !Dsp->Faults.NoHaltOnBadImage)
                FakeDspSchedule(Dsp, Dsp->Clock, FAKE_EVENT_HALT, Halt);
            break;
        case FAKE_EVENT_FW_ENTERED:
            if (Dsp->Halted != 0)
                break;
            Dsp->RomStatus = FWLOAD_FSR_FW_ENTERED;
            Dsp->FirmwareEntered = TRUE;
            Dsp->FwEnteredAt = Dsp->Clock;
            if (!Dsp->Faults.NeverFwReady)
                FakeDspSchedule(Dsp, Dsp->Clock + Dsp->Faults.FwReadyUs, FAKE_EVENT_FW_READY, 0);
            break;
        case FAKE_EVENT_FW_READY:
            Dsp->FwReadyAt = Dsp->Clock;
            FakeDspPut32(Dsp->Window + 0, 0x20000005);
            FakeDspPut32(Dsp->Window + 24, 0x0120315A);
            FakeDspPut32(Dsp->Window + 28, Dsp->Faults.AbiVersion);
            Message.Primary = Dsp->Faults.CrashNotification != 0 ?
                (SofIpc4GlbNotification << SOFIPC4_TYPE_SHIFT) |
                    (Dsp->Faults.CrashNotification << SOFIPC4_NOTIFICATION_TYPE_SHIFT) : SOFIPC4_FW_READY_PRIMARY;
            /* a notification the firmware posts: the target register goes busy with the header, the data register carries the extension, the target acknowledgement its busy bit */
            Dsp->IpcTarget = FAKE_DSP_IPC_BUSY | Message.Primary;
            Dsp->IpcTargetData = 0;
            Dsp->IpcTargetAck |= FAKE_DSP_IPC_BUSY;
            Dsp->NotificationPending = TRUE;
            Dsp->NotificationIsFwReady = Dsp->Faults.CrashNotification == 0;
            Dsp->WindowReadsAtNotification = Dsp->WindowReads;
            Dsp->Notifications++;
            break;
        case FAKE_EVENT_IPC_REPLY:
            FakeDspWriteFwConfig(Dsp);
            Message.Primary = (SofIpc4ModConfigGet << SOFIPC4_TYPE_SHIFT) | SOFIPC4_RSP_BIT |
                SOFIPC4_MSG_TGT_BIT | (Value & SOFIPC4_PRIMARY_PAYLOAD_MASK);
            /* a reply to a host request: the reply register goes done with the header, the data register carries the extension */
            Dsp->IpcReply = FAKE_DSP_IPC_DONE | Message.Primary;
            Dsp->IpcTargetData = 0;
            Dsp->ReplyPending = TRUE;
            Dsp->UplinkReadsAtReply = Dsp->UplinkReads;
            Dsp->Replies++;
            break;
        case FAKE_EVENT_HALT:
            Dsp->RomError = Value;
            Dsp->RomStatus = Value < 0x10000 ? (Value | FWLOAD_FSR_HALTED) : Value;
            Dsp->Halted++;
            break;
        default:
            /* the remaining kinds carry a ROM state in Value; a halted ROM stays where it stopped */
            if (Dsp->Halted == 0)
                Dsp->RomStatus = Value;
            break;
    }
}

/* Each pending event fires at its own scheduled time, so that what it schedules follows from that time */
static
VOID
FakeDspAdvance(
    PFAKE_DSP Dsp)
{
    UINT64 Now = Dsp->Clock;

    while (Dsp->Event != NULL && Dsp->Event->At <= Now)
    {
        FAKE_DSP_EVENT* Event = Dsp->Event;

        Dsp->Event = Event->Next;
        Dsp->EventCount--;
        Dsp->Clock = Event->At;
        FakeDspFire(Dsp, Event);
    }
    Dsp->Clock = Now;
}

/* The ROM from its reset vector to INIT_DONE, or to a halt, for one purge request */
static
VOID
FakeDspRomStart(
    PFAKE_DSP Dsp)
{
    UINT64 Now = Dsp->Clock;

    Dsp->RomStarted = TRUE;
    Dsp->RomRequestPending = FALSE;
    FakeDspSchedule(Dsp, Now, 100 + FWLOAD_FSR_ROM_RESET_VECTOR_DONE, FWLOAD_FSR_ROM_RESET_VECTOR_DONE);
    FakeDspSchedule(Dsp, Now + FAKE_DSP_ROM_STEP_US, 100 + FWLOAD_FSR_ROM_PURGE_BOOT, FWLOAD_FSR_ROM_PURGE_BOOT);
    if (Dsp->DroppedRequests < Dsp->Faults.DroppedRomRequests)
    {
        Dsp->DroppedRequests++;
        return;
    }
    if (!Dsp->Faults.NeverAckRom)
        FakeDspSchedule(Dsp, Now + Dsp->Faults.RomAckUs, FAKE_EVENT_ROM_ACK, 0);
    if (Dsp->Faults.HaltStage == FAKE_DSP_HALT_AFTER_PURGE)
        FakeDspSchedule(Dsp, Now + Dsp->Faults.RomReadyUs, FAKE_EVENT_HALT, Dsp->Faults.HaltCode);
    else if (!Dsp->Faults.NeverRomReady)
        FakeDspSchedule(Dsp, Now + Dsp->Faults.RomReadyUs, FAKE_EVENT_ROM_READY, 0);
}

static
UINT32
FakeDspRead32(
    PVOID Context,
    UINT32 Bar,
    UINT32 Offset)
{
    PFAKE_DSP Dsp = (PFAKE_DSP)Context;
    const FWLOAD_PLATFORM* Platform = Dsp->Platform;

    FakeDspAdvance(Dsp);
    if (Bar == FWLOAD_BAR_DSP)
    {
        if (Offset == Platform->SubsystemControl)
        {
            if (Dsp->FirstCpaPollAt == 0)
                Dsp->FirstCpaPollAt = Dsp->Clock;
            /* the power status reads unstable for a moment after the request */
            if (Dsp->SubsystemSpaAt != 0 && Dsp->Clock - Dsp->SubsystemSpaAt < FAKE_DSP_UNSTABLE_US &&
                (Dsp->SubsystemControl & FAKE_DSP_SUBSYSTEM_SPA))
            {
                Dsp->UnstableCpaReads++;
                return (Dsp->SubsystemControl & ~FAKE_DSP_SUBSYSTEM_CPA) |
                    ((Dsp->UnstableCpaReads & 1) ? FAKE_DSP_SUBSYSTEM_CPA : 0);
            }
            return Dsp->SubsystemControl;
        }
        if (Offset == Platform->CoreControl)
        {
            /* the loader reads the control register while it writes the owner bits; only the poll after the power request counts */
            if (Dsp->CoreSpaAt != 0 && Dsp->FirstCoreCpaPollAt == 0)
                Dsp->FirstCoreCpaPollAt = Dsp->Clock;
            return Dsp->CoreControl;
        }
        if (Offset == Platform->InterruptPointer)
        {
            Dsp->InterruptPointerReads++;
            /* a register of the DSP's own power domain: all ones until the subsystem reports power active */
            if (!(Dsp->SubsystemControl & FAKE_DSP_SUBSYSTEM_CPA))
            {
                Dsp->InterruptPointerReadsUnpowered++;
                if (Dsp->Faults.PointerNeedsPower)
                    return FAKE_DSP_POINTER_UNPOWERED;
            }
            return Dsp->Faults.InterruptPointerOverride ? Dsp->Faults.InterruptPointer : FAKE_DSP_INTERRUPT_POINTER;
        }
        if (Offset == FAKE_DSP_INTERRUPT_POINTER)
            return Dsp->InterruptEnable;
        if (Offset == Platform->HostIpcInterruptEnable)
            return Dsp->HostIpcEnable;
        if (Offset == Platform->IpcRequest)
            return Dsp->IpcRequest;
        if (Offset == Platform->IpcRequestData)
            return Dsp->IpcRequestData;
        if (Offset == Platform->IpcReply)
            return Dsp->IpcReply;
        if (Offset == Platform->IpcTarget)
            return Dsp->IpcTarget;
        if (Offset == Platform->IpcTargetData)
            return Dsp->IpcTargetData;
        if (Offset == Platform->IpcTargetAck)
            return Dsp->IpcTargetAck;
        if (Offset == Platform->IpcControl)
            return Dsp->IpcControl;
        if (Offset == Platform->RomStatus)
            return Dsp->RomStatus;
        if (Offset == Platform->RomError)
            return Dsp->RomError;
    }
    else if (Bar == FWLOAD_BAR_PP && Offset == FWLOAD_PP_OFFSET_PPCTL)
        return Dsp->PpControl;
    Dsp->UnexpectedAccesses++;
    return 0xFFFFFFFFUL;
}

static
VOID
FakeDspWrite32(
    PVOID Context,
    UINT32 Bar,
    UINT32 Offset,
    UINT32 Value)
{
    PFAKE_DSP Dsp = (PFAKE_DSP)Context;
    const FWLOAD_PLATFORM* Platform = Dsp->Platform;
    SOFIPC4_MSG Request;
    UINT32 Type;

    FakeDspAdvance(Dsp);
    if (Dsp->WriteCount < FAKE_DSP_WRITE_LOG)
    {
        Dsp->WriteLog[Dsp->WriteCount].Bar = Bar;
        Dsp->WriteLog[Dsp->WriteCount].Offset = Offset;
        Dsp->WriteLog[Dsp->WriteCount].Value = Value;
        Dsp->WriteLog[Dsp->WriteCount].At = Dsp->Clock;
        Dsp->WriteLog[Dsp->WriteCount].Powered = (Dsp->SubsystemControl & FAKE_DSP_SUBSYSTEM_CPA) != 0;
        Dsp->WriteCount++;
    }
    if (Bar == FWLOAD_BAR_PP)
    {
        if (Offset == FWLOAD_PP_OFFSET_PPCTL)
        {
            Dsp->PpControl = Value;
            FakeDspPower(Dsp, (Value & FWLOAD_PPCTL_GPROCEN) ? FAKE_POWER_GPROCEN_ON : FAKE_POWER_GPROCEN_OFF);
            return;
        }
    }
    else if (Bar == FWLOAD_BAR_DSP)
    {
        if (Offset == Platform->SubsystemControl)
        {
            if ((Value & FAKE_DSP_SUBSYSTEM_SPA) && !(Dsp->SubsystemControl & FAKE_DSP_SUBSYSTEM_SPA))
            {
                Dsp->SubsystemSpaAt = Dsp->Clock;
                FakeDspPower(Dsp, FAKE_POWER_SUBSYSTEM_ON);
                if (!Dsp->Faults.SubsystemNeverPowers)
                    FakeDspSchedule(Dsp, Dsp->Clock + Dsp->Faults.SubsystemPowerUs, FAKE_EVENT_SUBSYSTEM_ON, 0);
            }
            else if (!(Value & FAKE_DSP_SUBSYSTEM_SPA) && (Dsp->SubsystemControl & FAKE_DSP_SUBSYSTEM_SPA))
            {
                FakeDspPower(Dsp, FAKE_POWER_SUBSYSTEM_OFF);
                FakeDspSchedule(Dsp, Dsp->Clock + Dsp->Faults.SubsystemPowerUs, FAKE_EVENT_SUBSYSTEM_OFF, 0);
            }
            Dsp->SubsystemControl = (Value & ~FAKE_DSP_SUBSYSTEM_CPA) | (Dsp->SubsystemControl & FAKE_DSP_SUBSYSTEM_CPA);
            return;
        }
        if (Offset == Platform->CoreControl)
        {
            if ((Value & FAKE_DSP_CORE_SPA) && !(Dsp->CoreControl & FAKE_DSP_CORE_SPA))
            {
                Dsp->CoreSpaAt = Dsp->Clock;
                FakeDspPower(Dsp, FAKE_POWER_CORE_ON);
                if (!Dsp->Faults.CoreNeverPowers)
                    FakeDspSchedule(Dsp, Dsp->Clock + Dsp->Faults.CorePowerUs, FAKE_EVENT_CORE_ON, 0);
            }
            else if (!(Value & FAKE_DSP_CORE_SPA) && (Dsp->CoreControl & FAKE_DSP_CORE_SPA))
            {
                FakeDspPower(Dsp, FAKE_POWER_CORE_OFF);
                FakeDspSchedule(Dsp, Dsp->Clock + Dsp->Faults.CorePowerUs, FAKE_EVENT_CORE_OFF, 0);
            }
            Dsp->CoreControl = (Value & ~FAKE_DSP_CORE_CPA) | (Dsp->CoreControl & FAKE_DSP_CORE_CPA);
            return;
        }
        if (Offset == FAKE_DSP_INTERRUPT_POINTER)
        {
            if ((Value & 0x1) == 0)
                FakeDspPower(Dsp, FAKE_POWER_INTERRUPTS_OFF);
            Dsp->InterruptEnable = Value;
            return;
        }
        if (Offset == Platform->HostIpcInterruptEnable)
        {
            Dsp->HostIpcEnable = Value;
            return;
        }
        if (Offset == Platform->IpcRequestData)
        {
            Dsp->IpcRequestData = Value;
            return;
        }
        if (Offset == Platform->IpcControl)
        {
            Dsp->IpcControl = Value;
            return;
        }
        if (Offset == Platform->IpcReply)
        {
            /* the done bit is write-1-to-clear; the header beside it stays readable until the next reply */
            if (Value & FAKE_DSP_IPC_DONE)
            {
                if (!(Dsp->IpcReply & FAKE_DSP_IPC_DONE))
                    Dsp->IpcProtocolErrors |= FAKE_DSP_IPC_ACK_WITHOUT_DONE;
                if (Dsp->ReplyPending && Dsp->UplinkReads == Dsp->UplinkReadsAtReply)
                    Dsp->IpcProtocolErrors |= FAKE_DSP_IPC_ACK_BEFORE_READ;
                Dsp->ReplyPending = FALSE;
                Dsp->IpcReply &= ~FAKE_DSP_IPC_DONE;
            }
            return;
        }
        if (Offset == Platform->IpcTarget)
        {
            /* the host-done sequence writes the busy bit back */
            if (Value & FAKE_DSP_IPC_BUSY)
            {
                if (!(Dsp->IpcTarget & FAKE_DSP_IPC_BUSY))
                    Dsp->IpcProtocolErrors |= FAKE_DSP_IPC_ACK_WITHOUT_BUSY;
                if (Dsp->NotificationPending && Dsp->NotificationIsFwReady && Dsp->WindowReads == Dsp->WindowReadsAtNotification)
                    Dsp->IpcProtocolErrors |= FAKE_DSP_IPC_ACK_BEFORE_READ;
                Dsp->NotificationPending = FALSE;
                Dsp->IpcTarget &= ~FAKE_DSP_IPC_BUSY;
            }
            return;
        }
        if (Offset == Platform->IpcTargetAck)
        {
            Dsp->IpcTargetAck = Value;
            return;
        }
        if (Offset == Platform->IpcRequest)
        {
            Dsp->IpcRequest = Value;
            if (!(Value & FAKE_DSP_IPC_BUSY))
                return;
            if (!Dsp->FirmwareEntered)
            {
                Dsp->RomRequests++;
                Dsp->LastRomRequest = Value;
                /* the ROM acts on a purge request for the stream that was submitted */
                Dsp->RomRequestPending = (Value & FWLOAD_ROM_IPC_CONTROL) != 0 &&
                    (Value & FWLOAD_ROM_IPC_PURGE_FW) != 0 &&
                    ((Value >> FWLOAD_ROM_STREAM_SHIFT) & 0xF) == (Dsp->Dma != NULL ? Dsp->Dma->StreamTag - 1 : 0);
                if (Dsp->CorePowered && Dsp->RomRequestPending && !Dsp->RomStarted)
                    FakeDspRomStart(Dsp);
                return;
            }
            Dsp->IpcRequests++;
            Dsp->LastIpcPrimary = Value;
            Dsp->LastIpcExtension = Dsp->IpcRequestData;
            Request.Primary = Value;
            Request.Extension = Dsp->IpcRequestData;
            Type = SofIpc4MsgType(&Request);
            if (Type != SofIpc4ModConfigGet || SofIpc4MsgTarget(&Request) != SOFIPC4_TARGET_MODULE_MSG ||
                (Value & SOFIPC4_PRIMARY_PAYLOAD_MASK) != 0 ||
                (Dsp->IpcRequestData >> FWLOAD_PARAM_ID_SHIFT) != FWLOAD_PARAM_ID_FW_CONFIG)
            {
                Dsp->IpcRequestErrors++;
                Type = SofIpc4ErrorInvalidParam;
            }
            else
                Type = Dsp->Faults.IpcStatus;
            if (!Dsp->Faults.NeverIpcReply)
                FakeDspSchedule(Dsp, Dsp->Clock + Dsp->Faults.IpcReplyUs, FAKE_EVENT_IPC_REPLY, Type);
            return;
        }
        if (Offset == Platform->RomStatus || Offset == Platform->RomError)
            return;
    }
    Dsp->UnexpectedAccesses++;
}

static
BOOLEAN
FakeDspReadMemory(
    PVOID Context,
    UINT32 Offset,
    UINT8* Buffer,
    SIZE_T Bytes)
{
    PFAKE_DSP Dsp = (PFAKE_DSP)Context;

    FakeDspAdvance(Dsp);
    /* a read the loader attempts counts, whether or not the injected fault lets it succeed */
    if (Offset == Dsp->Platform->UplinkOffset)
        Dsp->UplinkReads++;
    if (Offset == Dsp->Platform->Window0Offset)
        Dsp->WindowReads++;
    if (Dsp->Faults.MemoryReadFails)
        return FALSE;
    if (Offset < Dsp->Platform->Window0Offset ||
        Offset - Dsp->Platform->Window0Offset + Bytes > FAKE_DSP_WINDOW_BYTES)
    {
        Dsp->UnexpectedAccesses++;
        return FALSE;
    }
    FakeDspCopy(Buffer, Dsp->Window + (Offset - Dsp->Platform->Window0Offset), Bytes);
    return TRUE;
}

static
UINT64
FakeDspNowUs(
    PVOID Context)
{
    return ((PFAKE_DSP)Context)->Clock;
}

static
VOID
FakeDspDelayUs(
    PVOID Context,
    UINT32 Microseconds)
{
    PFAKE_DSP Dsp = (PFAKE_DSP)Context;

    Dsp->Clock += Microseconds;
    FakeDspAdvance(Dsp);
}

/* The controller's view of the stream: the descriptor list must describe exactly the payload, in order, within 4 KiB bounds */
static
FWLOAD_STATUS
FakeDspStreamSubmit(
    PVOID Context,
    PFWLOAD_CL_DMA Dma)
{
    PFAKE_DSP Dsp = (PFAKE_DSP)Context;
    SIZE_T Index, Total = 0;
    UINT32 Errors = 0;

    FakeDspAdvance(Dsp);
    Dsp->Dma = Dma;
    Dsp->StreamSubmitted = TRUE;
    Dsp->StreamSubmits++;
    if (Dma->StreamTag == 0)
        Errors |= FAKE_DSP_DMA_TAG;
    if (Dma->StreamFormat != FWLOAD_CL_STREAM_FORMAT)
        Errors |= FAKE_DSP_DMA_FORMAT;
    if ((SIZE_T)Dma->LastValidIndex + 1 != Dma->FragmentCount)
        Errors |= FAKE_DSP_DMA_LVI;
    if (Dma->Spib != Dma->Bytes)
        Errors |= FAKE_DSP_DMA_SPIB;
    for (Index = 0; Index < Dma->FragmentCount; Index++)
    {
        const FWLOAD_BDL_ENTRY* Entry = &Dma->Bdl[Index];
        UINT64 Address = (UINT64)Entry->AddressLow | ((UINT64)Entry->AddressHigh << 32);
        SIZE_T Virtual;

        if (Entry->Bytes == 0)
            Errors |= FAKE_DSP_DMA_COVERAGE;
        if (Dma->EntryBoundary != 0 &&
            ((SIZE_T)Address % Dma->EntryBoundary) + Entry->Bytes > Dma->EntryBoundary)
            Errors |= FAKE_DSP_DMA_BOUNDARY;
        if (Entry->InterruptOnCompletion != 0)
            Errors |= FAKE_DSP_DMA_IOC;
        if (!FakeDspSource(Dsp, Address, &Virtual) || Virtual != Total)
            Errors |= FAKE_DSP_DMA_COVERAGE;
        Total += Entry->Bytes;
    }
    if (Total != Dma->Bytes || Dma->Bytes == 0 || Dma->Bytes > Dma->Capacity)
        Errors |= FAKE_DSP_DMA_BYTES;
    Dsp->DmaErrors |= Errors;
    return Errors ? FwloadHardwareFault : FwloadOk;
}

static
FWLOAD_STATUS
FakeDspStreamTrigger(
    PVOID Context,
    BOOLEAN Start)
{
    PFAKE_DSP Dsp = (PFAKE_DSP)Context;
    UINT64 Now;

    FakeDspAdvance(Dsp);
    if (!Dsp->StreamSubmitted)
        return FwloadHardwareFault;
    if (!Start)
    {
        Dsp->StreamStops++;
        Dsp->StreamRunning = FALSE;
        return FwloadOk;
    }
    Dsp->StreamStarts++;
    Dsp->StreamRunning = TRUE;
    Now = Dsp->Clock;
    FakeDspSchedule(Dsp, Now, 100 + FWLOAD_FSR_ROM_FW_MANIFEST_IN_DMA_BUFF, FWLOAD_FSR_ROM_FW_MANIFEST_IN_DMA_BUFF);
    FakeDspSchedule(Dsp, Now + FAKE_DSP_STREAM_CONSUME_US, FAKE_DSP_CONSUME, 0);
    FakeDspSchedule(Dsp, Now + FAKE_DSP_STREAM_CONSUME_US + FAKE_DSP_ROM_STEP_US, 100 + FWLOAD_FSR_ROM_LOAD_FW_MAN_TO_IMR, FWLOAD_FSR_ROM_LOAD_FW_MAN_TO_IMR);
    FakeDspSchedule(Dsp, Now + FAKE_DSP_STREAM_CONSUME_US + 2 * FAKE_DSP_ROM_STEP_US, 100 + FWLOAD_FSR_ROM_FW_LOADING_DONE, FWLOAD_FSR_ROM_FW_LOADING_DONE);
    FakeDspSchedule(Dsp, Now + FAKE_DSP_STREAM_CONSUME_US + 3 * FAKE_DSP_ROM_STEP_US, 100 + FWLOAD_FSR_ROM_FW_CODE_LOADED, FWLOAD_FSR_ROM_FW_CODE_LOADED);
    if (Dsp->Faults.HaltStage == FAKE_DSP_HALT_DURING_LOAD)
        FakeDspSchedule(Dsp, Now + FAKE_DSP_STREAM_CONSUME_US + 4 * FAKE_DSP_ROM_STEP_US, FAKE_EVENT_HALT, Dsp->Faults.HaltCode);
    else if (!Dsp->Faults.NeverFwEntered)
        FakeDspSchedule(Dsp, Now + Dsp->Faults.FwLoadUs, FAKE_EVENT_FW_ENTERED, 0);
    return FwloadOk;
}

static
VOID
FakeDspStreamRelease(
    PVOID Context)
{
    PFAKE_DSP Dsp = (PFAKE_DSP)Context;

    FakeDspPower(Dsp, FAKE_POWER_STREAM_RELEASE);
    Dsp->StreamReleases++;
    Dsp->StreamSubmitted = FALSE;
    Dsp->StreamRunning = FALSE;
}

static
VOID
FakeDspLogOp(
    PVOID Context,
    const char* Text)
{
    PFAKE_DSP Dsp = (PFAKE_DSP)Context;
    UINT32 Index;

    if (Dsp->TextLogCount >= FAKE_DSP_TEXT_LOG)
        return;
    for (Index = 0; Index + 1 < sizeof(Dsp->TextLog[0]) && Text[Index] != 0; Index++)
        Dsp->TextLog[Dsp->TextLogCount][Index] = Text[Index];
    Dsp->TextLog[Dsp->TextLogCount][Index] = 0;
    Dsp->TextLogCount++;
}

static const FWLOAD_OPS FakeDspOpsTable =
{
    FakeDspRead32,
    FakeDspWrite32,
    FakeDspReadMemory,
    FakeDspNowUs,
    FakeDspDelayUs,
    FakeDspStreamSubmit,
    FakeDspStreamTrigger,
    FakeDspStreamRelease,
    FakeDspLogOp
};

static const FAKE_DSP_FAULTS FakeDspDefaults =
{
    100, 200, 100, 500, 2000, 200, 50, 0, FAKE_DSP_HALT_NONE, 0, 0, SOFIPC4_FW_REGS_ABI_VER,
    SofIpc4Success, FAKE_DSP_TUPLES_GOOD, 0, 0, FALSE, FALSE,
    FALSE, FALSE, FALSE, FALSE, FALSE, FALSE, FALSE, FALSE, FALSE
};

const FAKE_DSP_FAULTS*
FakeDspDefaultFaults(
    VOID)
{
    return &FakeDspDefaults;
}

VOID
FakeDspInit(
    PFAKE_DSP Dsp,
    const FWLOAD_PLATFORM* Platform,
    const FAKE_DSP_FAULTS* Faults)
{
    FakeDspSet((UINT8*)Dsp, 0, sizeof(*Dsp));
    Dsp->Faults = Faults != NULL ? *Faults : FakeDspDefaults;
    Dsp->Platform = Platform;
    Dsp->RomStatus = FWLOAD_FSR_INIT;
}

const FWLOAD_OPS*
FakeDspOps(
    VOID)
{
    return &FakeDspOpsTable;
}

BOOLEAN
FakeDspPowerEventSeen(
    const FAKE_DSP* Dsp,
    UINT32 Event)
{
    return (Dsp->PowerEvents & (1UL << Event)) != 0;
}

UINT32
FakeDspEventTime(
    const FAKE_DSP* Dsp,
    UINT32 Event)
{
    UINT32 Index;

    for (Index = 0; Index < Dsp->EventLogCount; Index++)
        if (Dsp->EventLog[Index] == Event)
            return (UINT32)Dsp->EventLogAt[Index];
    return 0;
}

UINT32
FakeDspWriteCount(
    const FAKE_DSP* Dsp,
    UINT32 Bar,
    UINT32 Offset)
{
    UINT32 Index, Count = 0;

    for (Index = 0; Index < Dsp->WriteCount; Index++)
        if (Dsp->WriteLog[Index].Bar == Bar && Dsp->WriteLog[Index].Offset == Offset)
            Count++;
    return Count;
}

const FAKE_DSP_WRITE*
FakeDspWriteFind(
    const FAKE_DSP* Dsp,
    UINT32 Bar,
    UINT32 Offset,
    UINT32 ValueMask)
{
    UINT32 Index;

    for (Index = 0; Index < Dsp->WriteCount; Index++)
        if (Dsp->WriteLog[Index].Bar == Bar && Dsp->WriteLog[Index].Offset == Offset &&
            (Dsp->WriteLog[Index].Value & ValueMask) == ValueMask)
            return &Dsp->WriteLog[Index];
    return NULL;
}

/* The protocol violations the model saw, and a message the loader left without its acknowledgement */
UINT32
FakeDspIpcErrors(
    const FAKE_DSP* Dsp)
{
    UINT32 Errors = Dsp->IpcProtocolErrors;

    if (Dsp->IpcReply & FAKE_DSP_IPC_DONE)
        Errors |= FAKE_DSP_IPC_REPLY_LEFT_PENDING;
    if (Dsp->IpcTarget & FAKE_DSP_IPC_BUSY)
        Errors |= FAKE_DSP_IPC_NOTIFY_LEFT_BUSY;
    return Errors;
}
