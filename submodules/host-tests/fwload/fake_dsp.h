/*
 * PROJECT:     LiberNT host-native tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     A DSP model of the ACE 1.0 boot path and of the firmware's IPC after it: registers, virtual-time ROM and firmware progress, the code-loader consumer, FW_READY, the configuration query, a pipeline table and large-configuration blocks, with injectable faults and an access log
 * COPYRIGHT:   Copyright 2026 LiberNT contributors
 */

/*
 * The behaviour modelled is the public one: the register set and the state codes of the
 * ACE 1.0 ROM (Linux mtl.h), the ROM error codes (hda.h), the IPC4 notification and reply
 * formats (SOF src/include/ipc4) and the firmware configuration tuples (SOF
 * src/include/sof/tlv.h). The pipeline transitions the model accepts and the status it
 * answers to an illegal one are model values; the large-configuration block rules are those
 * of Linux v6.12 sound/soc/sof/ipc4.c (sof_ipc4_set_get_data), studied under LICENSING.md
 * rule 7. The timings are model values; no hardware was measured.
 */

#ifndef FAKE_DSP_H
#define FAKE_DSP_H

#include <fwload.h>

/* A model register value no public document fixes: where the interrupt IP pointer points */
#define FAKE_DSP_INTERRUPT_POINTER 0x1D00
#define FAKE_DSP_WINDOW_BYTES      0x8000

/* The answer of a DSP-domain register, the interrupt pointer among them, while its subsystem is powered down */
#define FAKE_DSP_POINTER_UNPOWERED 0xFFFFFFFFUL

/* Where the model halts the ROM */
#define FAKE_DSP_HALT_NONE          0
#define FAKE_DSP_HALT_AFTER_PURGE   1
#define FAKE_DSP_HALT_DURING_LOAD   2

/* Malformed firmware configuration replies; LENGTH_THREE is the qemu-ace-dsp fault bad-tuples (a length that is not a multiple of four), VERSION_ONLY its truncated-reply */
#define FAKE_DSP_TUPLES_GOOD            0
#define FAKE_DSP_TUPLES_LENGTH_OVERRUN  1
#define FAKE_DSP_TUPLES_NO_VERSION      2
#define FAKE_DSP_TUPLES_NO_TERMINATOR   3
#define FAKE_DSP_TUPLES_LENGTH_THREE    4
#define FAKE_DSP_TUPLES_LENGTH_ZERO     5
#define FAKE_DSP_TUPLES_UNKNOWN_TYPE    6
#define FAKE_DSP_TUPLES_VERSION_ONLY    7
#define FAKE_DSP_TUPLES_ZERO_MAILBOXES  8
#define FAKE_DSP_TUPLES_NO_MAILBOXES    9
#define FAKE_DSP_TUPLES_OVERSIZED_DOWNLINK 10
#define FAKE_DSP_TUPLES_OVERSIZED_UPLINK   11

/* Notification storms: the firmware posts log-buffer notifications, a new one the moment the host acknowledges one; the model stops after FAKE_DSP_STORM_MAX so that a loader without a bound still ends somewhere */
#define FAKE_DSP_STORM_NONE              0
#define FAKE_DSP_STORM_INSTEAD_FW_READY  1
#define FAKE_DSP_STORM_AFTER_FW_READY    2
#define FAKE_DSP_STORM_MAX               20000

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

/* No message type, for the faults that name one */
#define FAKE_DSP_NO_TYPE                 0xFFFFFFFFUL

/* The firmware's pipeline table of the model and the configuration its large-config parameter holds */
#define FAKE_DSP_PIPELINES               16
#define FAKE_DSP_LARGE_CONFIG_BYTES      16384
/* The newest requests of the host after the boot, their two header words */
#define FAKE_DSP_REQUEST_LOG             16

/* The firmware's module table of the model: the instances the host initialized, with their configurations (a model value, the simulator's) */
#define FAKE_DSP_MODULES                 64
#define FAKE_DSP_CONFIG_WORDS            32
#define FAKE_DSP_NO_MODULE               (-1)
/* What of a configuration the table reads: the copier's tail starts at the 21st word, and its gateway node id is word 17 (ipc4model.h SOFTPLG_IPC4_COPIER_CONFIG_WORDS, SOFTPLG_COPIER_NODE_ID_WORD): the index in bits 7:0, the gateway type in 12:8, the host output 0 and the host input 1, the placeholder index 0xFF naming no stream */
#define FAKE_DSP_COPIER_CONFIG_WORDS     21
#define FAKE_DSP_COPIER_NODE_ID_WORD     17
#define FAKE_DSP_NODE_HOST_OUTPUT        0
#define FAKE_DSP_NODE_HOST_INPUT         1
#define FAKE_DSP_NODE_INDEX_PLACEHOLDER  0xFF

/* Violations of the large-configuration block rules the model found */
#define FAKE_DSP_LARGE_FIRST_MISSING     0x00000001UL /* the first block came without the first flag, or a later one with it */
#define FAKE_DSP_LARGE_OFFSET_WRONG      0x00000002UL /* a later block did not continue where the one before ended */
#define FAKE_DSP_LARGE_LAST_MISSING      0x00000004UL /* the block that completed the data came without the last flag */
#define FAKE_DSP_LARGE_LAST_EARLY        0x00000008UL /* the last flag came on a block that did not complete the data */
#define FAKE_DSP_LARGE_SIZE_WRONG        0x00000010UL /* a block carried fewer or more bytes than its header says */
#define FAKE_DSP_LARGE_DATA_LOST         0x00000020UL /* a block's data arrived damaged */

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
    UINT32 NotificationStorm;
    UINT32 ReportedVersionShift;
    /* the raw value the interrupt pointer reads while the subsystem is powered, when InterruptPointerOverride is set */
    UINT32 InterruptPointer;
    BOOLEAN InterruptPointerOverride;
    /* the pointer answers all ones until the subsystem is powered, or until the core is: the simulator round's N5, where a DSP-domain register wakes one step after the subsystem */
    BOOLEAN PointerNeedsPower;
    BOOLEAN PointerNeedsCore;
    BOOLEAN SubsystemNeverPowers;
    BOOLEAN CoreNeverPowers;
    BOOLEAN NeverAckRom;
    BOOLEAN NeverRomReady;
    BOOLEAN NeverFwEntered;
    BOOLEAN NeverFwReady;
    BOOLEAN NeverIpcReply;
    BOOLEAN MemoryReadFails;
    BOOLEAN NoHaltOnBadImage;
    /* the firmware's IPC after the boot: a request whose type is IpcStatusForType is answered with IpcStatusValue, one whose type is NeverReplyForType is dropped, a reply whose type is shifted answers a message nobody sent */
    UINT32 IpcStatusForType;
    UINT32 IpcStatusValue;
    UINT32 NeverReplyForType;
    UINT32 ReplyTypeShift;
    /* the request after the boot, counted from 1, that is dropped like a firmware that never answers it; 0 drops none */
    UINT32 NeverReplyAtRequest;
    /* a firmware exception, or a log-buffer notification, posted the moment a request arrives; a notification posted beside the reply */
    BOOLEAN CrashDuringRequest;
    BOOLEAN NotifyDuringRequest;
    BOOLEAN NotifyBesideReply;
    BOOLEAN MemoryWriteFails;
} FAKE_DSP_FAULTS, *PFAKE_DSP_FAULTS;

typedef struct _FAKE_DSP_EVENT
{
    UINT64 At;
    UINT32 Kind;
    UINT32 Value;
    /* a reply: its header as it will be posted, whether the uplink mailbox carries its payload, and whether a notification is posted beside it */
    UINT32 Primary;
    UINT32 Extension;
    BOOLEAN HasPayload;
    BOOLEAN Beside;
    struct _FAKE_DSP_EVENT* Next;
} FAKE_DSP_EVENT;

/* One register or mailbox access, in order: a read or a write of a 32-bit register, a read or a write of a mailbox range; Value is the register value or the byte count */
#define FAKE_DSP_ACCESS_READ32      0
#define FAKE_DSP_ACCESS_WRITE32     1
#define FAKE_DSP_ACCESS_READMEMORY  2
#define FAKE_DSP_ACCESS_WRITEMEMORY 3

typedef struct _FAKE_DSP_ACCESS
{
    UINT8 Op;
    UINT8 Bar;
    UINT16 Reserved;
    UINT32 Offset;
    UINT32 Value;
} FAKE_DSP_ACCESS, *PFAKE_DSP_ACCESS;

typedef struct _FAKE_DSP_PIPELINE
{
    BOOLEAN Used;
    UINT32 State;
} FAKE_DSP_PIPELINE, *PFAKE_DSP_PIPELINE;

/* One instance of the module table */
typedef struct _FAKE_DSP_MODULE
{
    BOOLEAN Used;
    /* a configuration of 21 words and up is a copier's */
    BOOLEAN Copier;
    UINT32 Id;
    UINT32 Instance;
    UINT32 Pipeline;
    /* of a copier: its gateway's type and index */
    UINT32 GwType;
    UINT32 GwIndex;
    /* the table index of the instance bound to feed this one */
    INT32 Src;
    UINT32 Words;
    UINT32 Config[FAKE_DSP_CONFIG_WORDS];
    /* the large-configuration sets that replaced the configuration */
    UINT32 Updates;
} FAKE_DSP_MODULE, *PFAKE_DSP_MODULE;

typedef struct _FAKE_DSP_WRITE
{
    UINT32 Bar;
    UINT32 Offset;
    UINT32 Value;
    UINT64 At;
    /* whether the subsystem reported power active when the write arrived */
    BOOLEAN Powered;
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
    /* the interrupt pointer: how often it was read, and how often while the subsystem was powered down */
    UINT32 InterruptPointerReads;
    UINT32 InterruptPointerReadsUnpowered;
    /* the interrupt lock the operations table models: its depth, the writes to the three enables under it and without it, a lock taken twice and a clock operation made under it */
    UINT32 InterruptLockDepth;
    UINT32 InterruptWritesLocked;
    UINT32 InterruptWritesUnlocked;
    UINT32 InterruptLockRecursions;
    UINT32 InterruptLockDelays;
    UINT32 InterruptLockUnderflows;
    /* what the model posted into the receive registers, and what the loader read and acknowledged */
    UINT32 Replies;
    UINT32 Notifications;
    UINT32 UplinkReads;
    UINT32 WindowReads;
    UINT32 UplinkReadsAtReply;
    UINT32 WindowReadsAtNotification;
    UINT32 IpcProtocolErrors;
    BOOLEAN ReplyPending;
    /* the reply waiting carries a payload the host must read before it acknowledges */
    BOOLEAN ReplyHasPayload;
    BOOLEAN NotificationPending;
    BOOLEAN NotificationIsFwReady;
    /* the firmware's IPC after the boot: requests and replies other than the configuration query, the pipeline table, the downlink mailbox and the large-configuration parameter as the blocks assembled it */
    UINT32 TransportRequests;
    UINT32 TransportReplies;
    /* the newest FAKE_DSP_REQUEST_LOG requests after the boot: RequestLog[i % FAKE_DSP_REQUEST_LOG] is the request number i */
    UINT32 RequestLogCount;
    UINT32 RequestPrimary[FAKE_DSP_REQUEST_LOG];
    UINT32 RequestExtension[FAKE_DSP_REQUEST_LOG];
    /* the downlink writes the transport made, and the byte count of the last */
    UINT32 DownlinkWrites;
    UINT32 LastDownlinkBytes;
    FAKE_DSP_PIPELINE Pipeline[FAKE_DSP_PIPELINES];
    FAKE_DSP_MODULE Module[FAKE_DSP_MODULES];
    UINT32 ModuleInits;
    UINT32 ModuleBinds;
    UINT32 ModuleDeletes;
    UINT32 ModuleUpdates;
    UINT8 Downlink[FWLOAD_MAILBOX_BYTES];
    UINT8 LargeConfig[FAKE_DSP_LARGE_CONFIG_BYTES];
    UINT32 LargeConfigBytes;
    UINT32 LargeConfigTotal;
    UINT32 LargeConfigBlocks;
    UINT32 LargeConfigErrors;
    /* the access log and, in it, the last exchange: from the write of the request extension to the write that acknowledged the reply */
    FAKE_DSP_ACCESS* Access;
    UINT32 AccessCapacity;
    UINT32 AccessCount;
    UINT32 RequestDataIndex;
    UINT32 ExchangeStart;
    UINT32 ExchangeEnd;
    /* the storm: how many notifications were posted and whether it is running */
    BOOLEAN Storming;
    UINT32 StormPosted;
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
/* The downlink mailbox as the transport's operation writes it; not an operation of the loader, whose boot never sends a payload */
BOOLEAN
FakeDspWriteMemory(
    PVOID Context,
    UINT32 Offset,
    const UINT8* Buffer,
    SIZE_T Bytes);
/* Records every register and mailbox access into Buffer, oldest first, until it is full */
VOID
FakeDspAccessBegin(
    PFAKE_DSP Dsp,
    FAKE_DSP_ACCESS* Buffer,
    UINT32 Capacity);
/* The interrupt lock as the operations table models it, for a test that plays the interrupt service routine */
VOID FakeDspInterruptLock(PFAKE_DSP Dsp);
VOID FakeDspInterruptUnlock(PFAKE_DSP Dsp);
/* The instance of the module table a message names, by its id and instance; FAKE_DSP_NO_MODULE when there is none */
INT32
FakeDspModuleFind(
    const FAKE_DSP* Dsp,
    UINT32 Id,
    UINT32 Instance);

/* Whether a message is pending in the model's registers */
BOOLEAN FakeDspMessagePending(const FAKE_DSP* Dsp);
BOOLEAN FakeDspPowerEventSeen(const FAKE_DSP* Dsp, UINT32 Event);
UINT32 FakeDspEventTime(const FAKE_DSP* Dsp, UINT32 Event);
UINT32 FakeDspWriteCount(const FAKE_DSP* Dsp, UINT32 Bar, UINT32 Offset);
const FAKE_DSP_WRITE* FakeDspWriteFind(const FAKE_DSP* Dsp, UINT32 Bar, UINT32 Offset, UINT32 ValueMask);
UINT32 FakeDspIpcErrors(const FAKE_DSP* Dsp);

#endif
