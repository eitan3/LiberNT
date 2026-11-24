/*
 * PROJECT:     LiberNT host-native tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     A DSP model of the ACE 1.0 boot path for the firmware loader: the registers the loader touches, the ROM and firmware progress on a virtual clock, the code-loader stream consumer, FW_READY and the first IPC, with injectable faults
 * COPYRIGHT:   Copyright 2026 LiberNT contributors
 */

#ifndef FAKE_DSP_H
#define FAKE_DSP_H

#include <fwload.h>

/* A model register value no public document fixes: where the interrupt IP pointer points */
#define FAKE_DSP_INTERRUPT_POINTER 0x1D00
#define FAKE_DSP_WINDOW_BYTES      0x8000

/* Where the model halts the ROM */
#define FAKE_DSP_HALT_NONE          0
#define FAKE_DSP_HALT_AFTER_PURGE   1
#define FAKE_DSP_HALT_DURING_LOAD   2

/* Malformed firmware configuration replies */
#define FAKE_DSP_TUPLES_GOOD            0
#define FAKE_DSP_TUPLES_LENGTH_OVERRUN  1
#define FAKE_DSP_TUPLES_NO_VERSION      2
#define FAKE_DSP_TUPLES_NO_TERMINATOR   3

/* Contract violations of the code-loader stream the model found */
#define FAKE_DSP_DMA_TAG       0x00000001UL
#define FAKE_DSP_DMA_FORMAT    0x00000002UL
#define FAKE_DSP_DMA_LVI       0x00000004UL
#define FAKE_DSP_DMA_SPIB      0x00000008UL
#define FAKE_DSP_DMA_BOUNDARY  0x00000010UL
#define FAKE_DSP_DMA_COVERAGE  0x00000020UL
#define FAKE_DSP_DMA_IOC       0x00000040UL
#define FAKE_DSP_DMA_BYTES     0x00000080UL

/* Violations of the IPC receive protocol the model found */
#define FAKE_DSP_IPC_ACK_WITHOUT_DONE    0x00000001UL /* the done bit was written back while it was not set */
#define FAKE_DSP_IPC_ACK_BEFORE_READ     0x00000002UL /* a reply was acknowledged before its uplink mailbox, or FW_READY before its register window, was read */
#define FAKE_DSP_IPC_ACK_WITHOUT_BUSY    0x00000004UL /* the target busy bit was written back while it was not set */
#define FAKE_DSP_IPC_REPLY_LEFT_PENDING  0x00000008UL /* a reply is still waiting for its done bit */
#define FAKE_DSP_IPC_NOTIFY_LEFT_BUSY    0x00000010UL /* a notification is still waiting for its acknowledgement */

/* Power events, in the order the loader causes them */
#define FAKE_POWER_INTERRUPTS_OFF 1
#define FAKE_POWER_STREAM_RELEASE 2
#define FAKE_POWER_CORE_OFF       3
#define FAKE_POWER_SUBSYSTEM_OFF  4
#define FAKE_POWER_GPROCEN_OFF    5
#define FAKE_POWER_SUBSYSTEM_ON   6
#define FAKE_POWER_CORE_ON        7
#define FAKE_POWER_GPROCEN_ON     8

/* Progress events the model records with their time */
#define FAKE_EVENT_SUBSYSTEM_ON  1
#define FAKE_EVENT_SUBSYSTEM_OFF 2
#define FAKE_EVENT_CORE_ON       3
#define FAKE_EVENT_CORE_OFF      4
#define FAKE_EVENT_ROM_ACK       5
#define FAKE_EVENT_ROM_READY     6
#define FAKE_EVENT_FW_ENTERED    7
#define FAKE_EVENT_FW_READY      8
#define FAKE_EVENT_IPC_REPLY     9
#define FAKE_EVENT_HALT          10

typedef struct _FAKE_DSP_FAULTS
{
    UINT32 SubsystemPowerUs;
    UINT32 CorePowerUs;
    UINT32 RomAckUs;
    UINT32 RomReadyUs;
    UINT32 FwLoadUs;
    UINT32 FwReadyUs;
    UINT32 IpcReplyUs;
    UINT32 DroppedRomRequests;
    UINT32 HaltStage;
    UINT32 HaltCode;
    UINT32 CrashNotification;
    UINT32 AbiVersion;
    UINT32 IpcStatus;
    UINT32 TupleFault;
    UINT32 ReportedVersionShift;
    BOOLEAN SubsystemNeverPowers;
    BOOLEAN CoreNeverPowers;
    BOOLEAN NeverAckRom;
    BOOLEAN NeverRomReady;
    BOOLEAN NeverFwEntered;
    BOOLEAN NeverFwReady;
    BOOLEAN NeverIpcReply;
    BOOLEAN MemoryReadFails;
    BOOLEAN NoHaltOnBadImage;
} FAKE_DSP_FAULTS, *PFAKE_DSP_FAULTS;

typedef struct _FAKE_DSP_EVENT
{
    UINT64 At;
    UINT32 Kind;
    UINT32 Value;
    struct _FAKE_DSP_EVENT* Next;
} FAKE_DSP_EVENT;

typedef struct _FAKE_DSP_WRITE
{
    UINT32 Bar;
    UINT32 Offset;
    UINT32 Value;
    UINT64 At;
} FAKE_DSP_WRITE;

#define FAKE_DSP_WRITE_LOG   256
#define FAKE_DSP_EVENT_LOG   64
#define FAKE_DSP_TEXT_LOG    16
#define FAKE_DSP_EVENT_POOL  64

typedef struct _FAKE_DSP
{
    FAKE_DSP_FAULTS Faults;
    const FWLOAD_PLATFORM* Platform;
    UINT64 Clock;
    UINT32 SubsystemControl;
    UINT32 CoreControl;
    UINT32 InterruptEnable;
    UINT32 HostIpcEnable;
    UINT32 IpcRequest;
    UINT32 IpcRequestData;
    UINT32 IpcReply;
    UINT32 IpcTarget;
    UINT32 IpcTargetData;
    UINT32 IpcTargetAck;
    UINT32 IpcControl;
    UINT32 RomStatus;
    UINT32 RomError;
    UINT32 PpControl;
    UINT8 Window[FAKE_DSP_WINDOW_BYTES];
    FAKE_DSP_EVENT* Event;
    FAKE_DSP_EVENT EventPool[FAKE_DSP_EVENT_POOL];
    UINT32 EventCount;
    FWLOAD_CL_DMA* Dma;
    BOOLEAN StreamRunning;
    BOOLEAN StreamSubmitted;
    BOOLEAN CorePowered;
    BOOLEAN RomStarted;
    BOOLEAN RomRequestPending;
    BOOLEAN FirmwareEntered;
    UINT16 Version[4];
    UINT32 DmaErrors;
    UINT32 UnexpectedAccesses;
    UINT32 RomRequests;
    UINT32 DroppedRequests;
    UINT32 CorePowerUps;
    UINT32 StreamStarts;
    UINT32 StreamStops;
    UINT32 StreamReleases;
    UINT32 StreamSubmits;
    UINT32 IpcRequests;
    UINT32 IpcRequestErrors;
    UINT32 Halted;
    UINT32 LoadedBytes;
    UINT32 LastRomRequest;
    UINT32 LastIpcPrimary;
    UINT32 LastIpcExtension;
    UINT64 SubsystemSpaAt;
    UINT64 FirstCpaPollAt;
    UINT64 CoreSpaAt;
    UINT64 FirstCoreCpaPollAt;
    UINT64 FwEnteredAt;
    UINT64 FwReadyAt;
    UINT32 UnstableCpaReads;
    UINT32 PowerEvents;
    UINT32 PowerOrder[16];
    UINT32 PowerOrderCount;
    /* what the model posted into the receive registers, and what the loader read and acknowledged */
    UINT32 Replies;
    UINT32 Notifications;
    UINT32 UplinkReads;
    UINT32 WindowReads;
    UINT32 UplinkReadsAtReply;
    UINT32 WindowReadsAtNotification;
    UINT32 IpcProtocolErrors;
    BOOLEAN ReplyPending;
    BOOLEAN NotificationPending;
    BOOLEAN NotificationIsFwReady;
    FAKE_DSP_WRITE WriteLog[FAKE_DSP_WRITE_LOG];
    UINT32 WriteCount;
    UINT32 EventLog[FAKE_DSP_EVENT_LOG];
    UINT64 EventLogAt[FAKE_DSP_EVENT_LOG];
    UINT32 EventLogCount;
    CHAR TextLog[FAKE_DSP_TEXT_LOG][48];
    UINT32 TextLogCount;
} FAKE_DSP, *PFAKE_DSP;

const FAKE_DSP_FAULTS* FakeDspDefaultFaults(VOID);
VOID FakeDspInit(PFAKE_DSP Dsp, const FWLOAD_PLATFORM* Platform, const FAKE_DSP_FAULTS* Faults);
const FWLOAD_OPS* FakeDspOps(VOID);
BOOLEAN FakeDspPowerEventSeen(const FAKE_DSP* Dsp, UINT32 Event);
UINT32 FakeDspEventTime(const FAKE_DSP* Dsp, UINT32 Event);
UINT32 FakeDspWriteCount(const FAKE_DSP* Dsp, UINT32 Bar, UINT32 Offset);
const FAKE_DSP_WRITE* FakeDspWriteFind(const FAKE_DSP* Dsp, UINT32 Bar, UINT32 Offset, UINT32 ValueMask);
UINT32 FakeDspIpcErrors(const FAKE_DSP* Dsp);

#endif
