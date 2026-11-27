/*
 * PROJECT:     LiberNT Intel Smart Sound DSP driver
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Function driver of the hdaudbus DSP child: logs the controller's facts at start and boots the DSP with Sound Open Firmware when the BootDsp value asks for it
 * COPYRIGHT:   Copyright 2026 LiberNT contributors
 */

#include "intcdsp.h"
#include <ntstrsafe.h>

static
EVT_WDF_DEVICE_PREPARE_HARDWARE IntcDspEvtDevicePrepareHardware;
static
EVT_WDF_DEVICE_RELEASE_HARDWARE IntcDspEvtDeviceReleaseHardware;
static
EVT_WDF_DEVICE_D0_ENTRY IntcDspEvtDeviceD0Entry;
static
EVT_WDF_DEVICE_D0_EXIT IntcDspEvtDeviceD0Exit;
static
VOID
IntcDspPowerOff(
    _Inout_ PINTCDSP_DEVICE Dev);
static
NTSTATUS
IntcDspStreamBoot(
    _In_opt_ PVOID Context);

/* The code-loader stream carries 48 kHz, 32 bits, one channel: Linux hda.h HDA_CL_STREAM_FORMAT (the stream descriptor format register value 0x40) */
#define INTCDSP_CL_SAMPLE_RATE       48000
#define INTCDSP_CL_BITS_PER_SAMPLE   32

/* The self-test's pipeline: instance 1, as the plan assigns it, priority 0, core 0, no module and so no memory pages */
#define INTCDSP_SELFTEST_PIPELINE    1
#define INTCDSP_SELFTEST_EXCHANGES   6

static
VOID
IntcDspIpcAttach(
    _Inout_ PINTCDSP_DEVICE Dev);

/* One REG_DWORD of the driver's Settings key; the value is left alone when it cannot be read */
NTSTATUS
IntcDspReadSetting(
    _In_ PCWSTR Name,
    _Out_ PULONG Value)
{
    OBJECT_ATTRIBUTES Attributes;
    UNICODE_STRING Path, ValueName;
    UCHAR Buffer[sizeof(KEY_VALUE_PARTIAL_INFORMATION) + sizeof(ULONG)];
    PKEY_VALUE_PARTIAL_INFORMATION Info;
    HANDLE Key;
    ULONG Result;
    NTSTATUS Status;

    RtlInitUnicodeString(&Path, INTCDSP_SETTINGS_PATH);
    RtlInitUnicodeString(&ValueName, Name);
    InitializeObjectAttributes(&Attributes, &Path, OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE, NULL, NULL);
    Status = ZwOpenKey(&Key, KEY_QUERY_VALUE, &Attributes);
    if (!NT_SUCCESS(Status))
        return Status;
    Status = ZwQueryValueKey(Key, &ValueName, KeyValuePartialInformation, Buffer, sizeof(Buffer), &Result);
    ZwClose(Key);
    if (!NT_SUCCESS(Status))
        return Status;
    Info = (PKEY_VALUE_PARTIAL_INFORMATION)Buffer;
    if (Info->Type != REG_DWORD || Info->DataLength != sizeof(ULONG))
        return STATUS_OBJECT_TYPE_MISMATCH;
    RtlCopyMemory(Value, Info->Data, sizeof(ULONG));
    return STATUS_SUCCESS;
}

/* The transport's settings: an absent, unreadable or out-of-range value leaves the plan's default */
static
VOID
IntcDspReadIpcSettings(
    _Inout_ PINTCDSP_DEVICE Dev)
{
    ULONG Value;

    Dev->SelfTest = 0;
    Dev->UseInterrupts = 0;
    Dev->IpcTimeoutMs = INTCDSP_IPC_TIMEOUT_MS_DEFAULT;
    /* the two are choices: SelfTest one of the plan's (off, the IPC self-test, the loopback self-test), UseInterrupts off or on; any other value is the default, said once (R1-09) */
    if (NT_SUCCESS(IntcDspReadSetting(INTCDSP_SETTINGS_SELFTEST, &Value)))
    {
        if (Value <= INTCDSP_SELFTEST_LOOPBACK)
            Dev->SelfTest = Value;
        else
            IntcDspLog("SelfTest = %lu is not 0, %u or %u: 0\n", Value, (ULONG)INTCDSP_SELFTEST_IPC, (ULONG)INTCDSP_SELFTEST_LOOPBACK);
    }
    if (NT_SUCCESS(IntcDspReadSetting(INTCDSP_SETTINGS_USEINTERRUPTS, &Value)))
    {
        if (Value <= 1)
            Dev->UseInterrupts = Value;
        else
            IntcDspLog("UseInterrupts = %lu is not 0 or 1: 0\n", Value);
    }
    if (NT_SUCCESS(IntcDspReadSetting(INTCDSP_SETTINGS_IPCTIMEOUTMS, &Value)) && Value != 0 && Value <= INTCDSP_IPC_TIMEOUT_MS_MAX)
        Dev->IpcTimeoutMs = Value;
    /* the D0 rule and the idle power-down (E2): BootAtStart a choice like UseInterrupts; DspIdleSec any number of seconds, 0 off */
    Dev->BootAtStart = 0;
    Dev->DspIdleSec = 0;
    if (NT_SUCCESS(IntcDspReadSetting(INTCDSP_SETTINGS_BOOTATSTART, &Value)))
    {
        if (Value <= 1)
            Dev->BootAtStart = Value;
        else
            IntcDspLog("BootAtStart = %lu is not 0 or 1: 0\n", Value);
    }
    if (NT_SUCCESS(IntcDspReadSetting(INTCDSP_SETTINGS_DSPIDLESEC, &Value)))
        Dev->DspIdleSec = Value;
    IntcDspLog("SelfTest = %lu, UseInterrupts = %lu, IpcTimeoutMs = %lu ms, BootAtStart = %lu, DspIdleSec = %lu s\n", Dev->SelfTest, Dev->UseInterrupts,
               Dev->IpcTimeoutMs, Dev->BootAtStart, Dev->DspIdleSec);
}

/* The line of an exchange that did not end well, the plan's one line per failure: the request in words and in registers, the outcome and the time */
static
VOID
IntcDspIpcLog(
    _In_ PINTCDSP_DEVICE Dev,
    SOFIPC_STATUS Result,
    _In_ const SOFIPC4_MSG* Msg)
{
    const SOFIPC_EXCHANGE* Last = &Dev->Ipc.Last;
    UINT32 Type = SofIpc4MsgType(Msg);

    if (Result == SofIpcBadState)
    {
        IntcDspLog("ipc: %s (%u) 0x%08x|0x%08x refused: the link is %s\n", SofIpcTypeName(Msg->Primary), Type,
                   Msg->Primary | FWLOAD_IPC_BUSY, Msg->Extension, SofIpcLinkName(Dev->Ipc.Link));
        return;
    }
    if (Result == SofIpcBusy)
    {
        IntcDspLog("ipc: %s (%u) 0x%08x|0x%08x busy after %I64u us: another request holds the link\n", SofIpcTypeName(Msg->Primary), Type,
                   Msg->Primary | FWLOAD_IPC_BUSY, Msg->Extension, Last->ElapsedUs);
        return;
    }
    if (Result == SofIpcFwStatus)
        IntcDspLog("ipc: %s (%u) 0x%08x|0x%08x status %u (%s) after %I64u us; the link is faulted\n", SofIpcTypeName(Msg->Primary), Type,
                   Msg->Primary | FWLOAD_IPC_BUSY, Msg->Extension, Last->Status, SofIpc4StatusName(Last->Status), Last->ElapsedUs);
    else
        IntcDspLog("ipc: %s (%u) 0x%08x|0x%08x %s after %I64u us; the link is faulted\n", SofIpcTypeName(Msg->Primary), Type,
                   Msg->Primary | FWLOAD_IPC_BUSY, Msg->Extension, SofIpcStatusName(Result), Last->ElapsedUs);
}

/* One exchange through the transport; on any outcome but success its line is logged here, once */
static
SOFIPC_STATUS
IntcDspIpcSend(
    _Inout_ PINTCDSP_DEVICE Dev,
    SOFIPC_CLASS Class,
    _In_ const SOFIPC4_MSG* Msg)
{
    SOFIPC_REQUEST Request;
    SOFIPC_STATUS Result;

    RtlZeroMemory(&Request, sizeof(Request));
    Request.Class = Class;
    Request.Msg = *Msg;
    Result = SofIpcRequest(&Dev->Ipc, &Request);
    if (Result != SofIpcOk)
        IntcDspIpcLog(Dev, Result, Msg);
    return Result;
}

/* The transport hangs off the loader's context once the boot is over: ready, or faulted at once when the guard already tripped or an enable did not read back */
static
VOID
IntcDspIpcAttach(
    _Inout_ PINTCDSP_DEVICE Dev)
{
    UINT32 Timeouts[SofIpcClassCount];
    SOFIPC_STATUS Result;

    IntcDspRequestLockInit(&Dev->RequestLock);
    /* the setting is bounded far below the conversion's limit; a budget the transport's table cannot hold would be the plan's default */
    if (SofIpcDefaultTimeouts(Timeouts, Dev->IpcTimeoutMs) != SofIpcOk)
        SofIpcDefaultTimeouts(Timeouts, INTCDSP_IPC_TIMEOUT_MS_DEFAULT);
    Dev->IpcArmed = FALSE;
    Result = SofIpcInit(&Dev->Ipc, IntcDspIpcOpsTable(), Dev, &Dev->Fw, Timeouts, Dev->UseInterrupts != 0);
    Dev->IpcArmed = Dev->UseInterrupts != 0 && Result == SofIpcOk;
    IntcDspLog("ipc: link %s, %s, %lu ms per message\n", SofIpcLinkName(Dev->Ipc.Link), Dev->IpcArmed ? "interrupts on" : "polled", Dev->IpcTimeoutMs);
}

/* The IPC self-test (SelfTest = 1): pipeline 1 created, paused, run, paused, reset and deleted, one exchange each with its line, then the summary. A failed exchange is logged by the send and the DSP is powered down, as every faulted link ends */
static
VOID
IntcDspIpcSelfTest(
    _Inout_ PINTCDSP_DEVICE Dev)
{
    static const char* const Steps[INTCDSP_SELFTEST_EXCHANGES] =
    {
        "create pipeline 1", "pipeline 1 paused", "pipeline 1 running", "pipeline 1 paused", "pipeline 1 reset", "delete pipeline 1"
    };
    SOFIPC4_MSG Messages[INTCDSP_SELFTEST_EXCHANGES];
    SOFIPC_STATUS Result = SofIpcOk;
    UINT32 Index;

    SofIpc4PipelineCreate(&Messages[0], INTCDSP_SELFTEST_PIPELINE, 0, 0, 0, 0, 0);
    SofIpc4PipelineSetState(&Messages[1], INTCDSP_SELFTEST_PIPELINE, SOFIPC4_PIPELINE_STATE_PAUSED, 0);
    SofIpc4PipelineSetState(&Messages[2], INTCDSP_SELFTEST_PIPELINE, SOFIPC4_PIPELINE_STATE_RUNNING, 0);
    SofIpc4PipelineSetState(&Messages[3], INTCDSP_SELFTEST_PIPELINE, SOFIPC4_PIPELINE_STATE_PAUSED, 0);
    SofIpc4PipelineSetState(&Messages[4], INTCDSP_SELFTEST_PIPELINE, SOFIPC4_PIPELINE_STATE_RESET, 0);
    SofIpc4PipelineDelete(&Messages[5], INTCDSP_SELFTEST_PIPELINE);
    for (Index = 0; Index < INTCDSP_SELFTEST_EXCHANGES; Index++)
    {
        Result = IntcDspIpcSend(Dev, SofIpcClassPipeline, &Messages[Index]);
        if (Result != SofIpcOk)
            break;
        IntcDspLog("ipc self-test: %s: status %u after %I64u us\n", Steps[Index], Dev->Ipc.Last.Status, Dev->Ipc.Last.ElapsedUs);
    }
    if (Result == SofIpcOk)
        IntcDspLog("ipc self-test: %u requests, %u replies, %u notifications, %u timeouts, %lu DSP interrupts, ok\n",
                   Dev->Ipc.Requests, Dev->Ipc.Replies, Dev->Ipc.Notifications, Dev->Ipc.Timeouts, Dev->Ipc.InterruptCount);
    else
        IntcDspLog("ipc self-test: %u requests, %u replies, %u notifications, %u timeouts, %lu DSP interrupts, failed at %s\n",
                   Dev->Ipc.Requests, Dev->Ipc.Replies, Dev->Ipc.Notifications, Dev->Ipc.Timeouts, Dev->Ipc.InterruptCount, Steps[Index]);
    /* the guard tripped on the transport's watch: the callback is taken off the line for good, as after a boot */
    if (Dev->Fw.InterruptStorm)
    {
        IntcDspLog("DSP interrupt storm: %lu interrupts in a row without a message consumed, the interrupt callback is disarmed and unregistered\n",
                   Dev->Fw.InterruptCount);
        if (Dev->InterruptRegistered)
        {
            Dev->Bus.UnregisterInterrupt(Dev->Bus.Context);
            Dev->InterruptRegistered = FALSE;
        }
    }
    if (Result != SofIpcOk)
    {
        IntcDspPowerDown(Dev);
        Dev->Booted = FALSE;
    }
}

/* One line of the stream manager in the driver's log, as the transport's own lines are printed */
static
VOID
IntcDspStreamLog(
    _In_opt_ PVOID Context,
    _In_z_ const char* Format,
    va_list Arguments)
{
    CHAR Line[INTCDSP_STREAM_LINE_BYTES];

    UNREFERENCED_PARAMETER(Context);
    if (RtlStringCbVPrintfA(Line, sizeof(Line), Format, Arguments) == STATUS_INVALID_PARAMETER)
        return;
    IntcDspLog("%s", Line);
}

/*
 * A request faulted the link: as every faulted link ends, the DSP is powered
 * down; it is booted again at the next stream open (E1) or at the next D0
 * entry. The manager released every stream itself before it called here,
 * under its own lock (stream.c IntcDspStreamsFault), so only the hardware
 * goes off: nothing is released twice. The topology model stays, so the
 * device is in need of a boot, not without a DSP.
 */
static
VOID
IntcDspStreamFault(
    _In_opt_ PVOID Context)
{
    PINTCDSP_DEVICE Dev = (PINTCDSP_DEVICE)Context;

    IntcDspPowerOff(Dev);
    Dev->Booted = FALSE;
    Dev->DspState = IntcDspNeedsBoot;
}

/* A buffer goes back to the bus: the self-test's mapping of it is gone first */
static
VOID
IntcDspStreamUnmap(
    _In_opt_ PVOID Context,
    _In_ PMDL Mdl)
{
    PINTCDSP_DEVICE Dev = (PINTCDSP_DEVICE)Context;
    ULONG Index;

    for (Index = 0; Index < INTCDSP_SELFTEST_MAPPINGS; Index++)
    {
        if (Dev->Mapped[Index].Mdl != Mdl)
            continue;
        MmUnmapLockedPages(Dev->Mapped[Index].Virtual, Mdl);
        Dev->Mapped[Index].Mdl = NULL;
        Dev->Mapped[Index].Virtual = NULL;
    }
}

/* The pages of a buffer as the CPU sees them: the codec streams' mapping of an MDL from MmAllocatePagesForMdlEx (appleaudio/adapter.cpp), nonpaged and non-cached, PASSIVE_LEVEL */
static
PVOID
IntcDspSelfTestMap(
    _In_opt_ PVOID Context,
    _In_ PMDL Mdl,
    SIZE_T Bytes)
{
    PINTCDSP_DEVICE Dev = (PINTCDSP_DEVICE)Context;
    PVOID Virtual;
    ULONG Index;

    UNREFERENCED_PARAMETER(Bytes);
    for (Index = 0; Index < INTCDSP_SELFTEST_MAPPINGS && Dev->Mapped[Index].Mdl != NULL; Index++)
        ;
    if (Index == INTCDSP_SELFTEST_MAPPINGS || Mdl == NULL)
        return NULL;
    Virtual = MmMapLockedPagesSpecifyCache(Mdl, KernelMode, MmNonCached, NULL, FALSE, NormalPagePriority);
    if (Virtual == NULL)
        return NULL;
    Dev->Mapped[Index].Mdl = Mdl;
    Dev->Mapped[Index].Virtual = Virtual;
    return Virtual;
}

/* Whether the device's firmware image is resident: read at start and kept for the device (R6) */
static
BOOLEAN
IntcDspFirmwareResident(
    _In_ PINTCDSP_DEVICE Dev)
{
    return Dev->Image.Data != NULL && Dev->Image.Bytes != 0;
}

/* The self-test's wait for the capture's periods: the manager's wait on the device's period event, which the bus's interrupt DPC sets */
static
NTSTATUS
IntcDspSelfTestWait(
    _In_opt_ PVOID Context,
    HANDLE Stream,
    UINT32 Count,
    UINT32 TimeoutMs,
    _Out_ PULONG Periods)
{
    PINTCDSP_DEVICE Dev = (PINTCDSP_DEVICE)Context;

    return IntcDspStreamWaitPeriods(&Dev->Streams, Stream, Count, TimeoutMs, &Dev->SelfTestEvent, Periods);
}

NTSTATUS
IntcDspStreamsStart(
    _Inout_ PINTCDSP_DEVICE Dev,
    BOOLEAN AtOpen)
{
    SIZE_T Bytes = IntcDspStreamsBytes(Dev->TopologyModel);
    NTSTATUS Status;

    /* the state is one allocation for the device, made when the first topology is measured and kept through the power cycles */
    if (Dev->StreamsState == NULL || Dev->StreamsStateBytes < Bytes)
    {
        if (Dev->StreamsState != NULL)
            ExFreePoolWithTag(Dev->StreamsState, INTCDSP_POOL_TAG);
        Dev->StreamsState = (PUCHAR)ExAllocatePoolWithTag(NonPagedPool, Bytes, INTCDSP_POOL_TAG);
        if (Dev->StreamsState == NULL)
        {
            Dev->StreamsStateBytes = 0;
            IntcDspLog("streams: no %I64u bytes of state; no stream, no self-test\n", (ULONGLONG)Bytes);
            return STATUS_INSUFFICIENT_RESOURCES;
        }
        Dev->StreamsStateBytes = Bytes;
    }
    Dev->StreamOps.Context = Dev;
    Dev->StreamOps.Log = IntcDspStreamLog;
    Dev->StreamOps.Fault = IntcDspStreamFault;
    Dev->StreamOps.Boot = IntcDspStreamBoot;
    Dev->StreamOps.Unmap = IntcDspStreamUnmap;
    Dev->SelfTestOps.Context = Dev;
    Dev->SelfTestOps.Map = IntcDspSelfTestMap;
    Dev->SelfTestOps.WaitPeriods = IntcDspSelfTestWait;
    KeInitializeEvent(&Dev->SelfTestEvent, SynchronizationEvent, FALSE);
    IntcDspStreamsInit(&Dev->Streams, Dev->StreamsState, Dev->StreamsStateBytes, Dev->TopologyModel, &Dev->Ipc, &Dev->Bus, &Dev->StreamOps);
    /* a bus without the stream members, or a link that is not ready: the DSP stays as the earlier checkpoints left it, with no stream and no self-test */
    if (!Dev->Streams.Ready)
        return STATUS_SUCCESS;
    /* the loopback self-test is the opt-in check of the plan at a D0 entry; a boot at an open runs none (E1) */
    if (!AtOpen && Dev->SelfTest == INTCDSP_SELFTEST_LOOPBACK)
    {
        Status = IntcDspLoopbackSelfTest(&Dev->Streams, &Dev->SelfTestOps, INTCDSP_SELFTEST_FRAMES);
        /* the manager logged the lines; a fault powered the DSP down, a capture that does not match leaves it up, as the plan's stop rule says */
        IntcDspLog("loopback self-test: %s (0x%08lx), the DSP is %s\n", NT_SUCCESS(Status) ? "ran" : "failed", Status,
                   Dev->Booted ? "left running" : "powered down");
    }
    return STATUS_SUCCESS;
}

/*
 * The boot sequence the D0 entry and the boot at an open share (E1,
 * p5-e1-design.md §2): one place for the three calls, so that a recovery
 * boots as a D0 entry does, the self-tests aside. The status is the
 * manager's after it all, and the device's state follows: no topology
 * model, there is nothing to give a stream (NoDsp); the DSP booted and the
 * manager ready and not faulted, Ready; otherwise the model is kept for the
 * next open's boot (NeedsBoot). Booting marks the way in at an open.
 */
NTSTATUS
IntcDspBootSequence(
    _Inout_ PINTCDSP_DEVICE Dev,
    BOOLEAN AtOpen)
{
    NTSTATUS Status = STATUS_SUCCESS;

    if (AtOpen)
        Dev->DspState = IntcDspBooting;
    IntcDspBoot(Dev, AtOpen);
    /* the plan's fail-safe: a booted DSP with no topology the firmware can run is powered down again and gets no endpoint */
    if (Dev->Booted && !IntcDspTopologyLoad(Dev))
        IntcDspPowerDown(Dev);
    /* the streams hang off the booted loader and its topology from here; the self-test is the opt-in check of the plan, and a fault of it powers the DSP down itself */
    if (Dev->Booted)
        Status = IntcDspStreamsStart(Dev, AtOpen);
    if (Dev->TopologyModel == NULL)
        Dev->DspState = IntcDspNoDsp;
    else if (Dev->Booted && Dev->Streams.Ready && !Dev->Streams.Faulted)
        Dev->DspState = IntcDspReady;
    else
        Dev->DspState = IntcDspNeedsBoot;
    /* the open asks the manager again and refuses on its word (stream.c IntcDspStreamOpen); the status is for its line */
    if (Dev->DspState != IntcDspReady && NT_SUCCESS(Status))
        Status = STATUS_DEVICE_NOT_READY;
    return Status;
}

/* The manager's Boot operation (stream.h INTCDSP_STREAMS_OPS.Boot): the sequence at an open, under the manager's lock */
static
NTSTATUS
IntcDspStreamBoot(
    _In_opt_ PVOID Context)
{
    PINTCDSP_DEVICE Dev = (PINTCDSP_DEVICE)Context;

    return IntcDspBootSequence(Dev, TRUE);
}

VOID
IntcDspReleaseStream(
    _Inout_ PINTCDSP_DEVICE Dev)
{
    NTSTATUS Status;

    if (!Dev->Stream)
        return;
    Dev->Bus.TriggerDSP(Dev->Bus.Context, Dev->Stream, FALSE);
    if (Dev->StreamPrepared)
        Dev->Bus.CleanupDSP(Dev->Bus.Context, Dev->Stream);
    Dev->StreamPrepared = FALSE;
    Status = Dev->Bus.FreeStream(Dev->Bus.Context, Dev->Stream);
    if (!NT_SUCCESS(Status))
        IntcDspLog("stream %u: not freed, 0x%08lx\n", Dev->StreamTag, Status);
    Dev->Stream = NULL;
}

VOID
IntcDspPowerDown(
    _Inout_ PINTCDSP_DEVICE Dev)
{
    /* the streams of the boot go back to the bus before the DSP is off, as after a fault; the manager is not ready from here until the next boot initializes it, and every handle of the boot is refused (stream.c IntcDspStreamsRelease) */
    IntcDspStreamsRelease(&Dev->Streams);
    IntcDspPowerOff(Dev);
}

/* The DSP off: the loader's power-down, the interrupt off the line and the idle reference given back */
static
VOID
IntcDspPowerOff(
    _Inout_ PINTCDSP_DEVICE Dev)
{
    FWLOAD_STATUS Status = FwloadOk;
    NTSTATUS NtStatus;

    Dev->InterruptArmed = FALSE;
    Dev->IpcArmed = FALSE;
    if (Dev->Fw.State != FwloadStateOff)
    {
        Status = FwloadPowerDown(&Dev->Fw);
        /* the power state after the loader's own power-down: whether the subsystem really went off is a silicon fact the log must carry; the state is the one the loader left, as its own record says (the context is off by now) */
        Dev->Adspcs = IntcDspOpsTable()->Read32(Dev, FWLOAD_BAR_DSP, Dev->Platform->SubsystemControl);
        IntcDspLog("DSP power down from state %s: %s, cleanup %s, ADSPCS 0x%08x (SPA %u, CPA %u)\n", FwloadStateName(Dev->Fw.PowerDownFrom),
                   FwloadStatusName(Status), FwloadStatusName(Dev->Fw.CleanupStatus), Dev->Adspcs, (Dev->Adspcs >> 16) & 1, (Dev->Adspcs >> 24) & 1);
    }
    Dev->Booted = FALSE;
    if (Dev->InterruptRegistered)
    {
        Dev->Bus.UnregisterInterrupt(Dev->Bus.Context);
        Dev->InterruptRegistered = FALSE;
    }
    if (Dev->IdleHeld)
    {
        NtStatus = Dev->Bus.SetDSPPowerState(Dev->Bus.Context, PowerDeviceD3);
        Dev->IdleHeld = FALSE;
        if (!NT_SUCCESS(NtStatus))
            IntcDspLog("idle reference not released: 0x%08lx\n", NtStatus);
    }
}

static
VOID
IntcDspBootStop(
    _Inout_ PINTCDSP_DEVICE Dev,
    _In_ const char* Step,
    _In_ NTSTATUS Status)
{
    IntcDspLog("boot stopped at %s: 0x%08lx; the DSP is not started\n", Step, Status);
    IntcDspReleaseStream(Dev);
    Dev->LastStatus = FwloadHardwareFault;
}

/*
 * The code-loader buffer of the firmware image: the payload's pages as one
 * contiguous range below the controller's DMA bound, made at start once the
 * image is resident and kept for the hardware like it (R6, R1-03), so that
 * no boot, and no resume, allocates contiguous memory. Its physical range
 * is the segment the loader's descriptors name.
 */
static
NTSTATUS
IntcDspDmaBufferAllocate(
    _Inout_ PINTCDSP_DEVICE Dev)
{
    PHYSICAL_ADDRESS Low, High, Boundary;

    if (Dev->DmaBuffer != NULL)
        return STATUS_SUCCESS;
    Dev->DmaBytes = (SIZE_T)ROUND_TO_PAGES((ULONG)Dev->Image.Ri.PayloadBytes);
    Low.QuadPart = 0;
    High.QuadPart = (LONGLONG)IntcDspDmaHighBound(&Dev->Bus);
    Boundary.QuadPart = 0;
    Dev->DmaBuffer = MmAllocateContiguousMemorySpecifyCache((ULONG)Dev->DmaBytes, Low, High, Boundary, MmNonCached);
    if (Dev->DmaBuffer == NULL)
    {
        Dev->DmaBytes = 0;
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    Dev->Segment.Address = MmGetPhysicalAddress(Dev->DmaBuffer).QuadPart;
    Dev->Segment.Bytes = Dev->DmaBytes;
    return STATUS_SUCCESS;
}

NTSTATUS
IntcDspBoot(
    _Inout_ PINTCDSP_DEVICE Dev,
    BOOLEAN AtOpen)
{
    const FWLOAD_PLATFORM* Platform = Dev->Platform;
    FWLOAD_TIMEOUTS Timeouts;
    FWLOAD_CL_DMA Dma;
    FWLOAD_BOOT_DECISION Decision;
    HDAUDIO_STREAM_FORMAT Format;
    const SOFIPC4_FW_REGS* Registers;
    PVOID Bdl = NULL;
    SIZE_T Entries;
    FWLOAD_STATUS Status;
    NTSTATUS NtStatus;

    if (Dev->Booted)
        return STATUS_SUCCESS;
    /* the boot policy as one decision (fwload.c FwloadBootDecision): a record for this controller, its firmware file resident and BootDsp set; the file is read first, so a controller without a record or without its file stops here, before any register is written */
    NtStatus = IntcDspLoadFirmware(Dev);
    Decision = FwloadBootDecision(Platform, NT_SUCCESS(NtStatus), Dev->BootDsp != 0);
    if (Decision != FwloadBootYes)
    {
        IntcDspLog("boot: %s; the DSP is left alone\n", FwloadBootDecisionName(Decision));
        Dev->LastStatus = Dev->FirmwareStatus;
        return NT_SUCCESS(NtStatus) ? STATUS_SUCCESS : NtStatus;
    }
    if (!Dev->AdspBar.Base.Base || Dev->AdspBar.Len < INTCDSP_BAR4_MIN_BYTES || !Dev->Ppcap)
    {
        IntcDspLog("BAR4 (%p, 0x%lx bytes, needs 0x%x) or the processing capabilities (%p) are missing; the DSP is left alone\n",
                   Dev->AdspBar.Base.Base, Dev->AdspBar.Len, INTCDSP_BAR4_MIN_BYTES, Dev->Ppcap);
        return STATUS_SUCCESS;
    }

    /* the buffer is the device's, made at start with the image (R1-03); an image this boot read for the first time makes it now, and it stays for the hardware */
    NtStatus = IntcDspDmaBufferAllocate(Dev);
    if (!NT_SUCCESS(NtStatus))
        goto Fail;
    Entries = FwloadClDmaEntryCount(&Dev->Segment, 1, Dev->Image.Ri.PayloadBytes, FWLOAD_CL_DMA_BOUNDARY_BYTES);
    if (Entries == 0 || Entries > INTCDSP_BDL_ENTRIES)
    {
        IntcDspLog("the payload needs %I64u descriptors, the stream has %u\n", (ULONGLONG)Entries, (ULONG)INTCDSP_BDL_ENTRIES);
        NtStatus = STATUS_INVALID_PARAMETER;
        goto Fail;
    }

    RtlZeroMemory(&Format, sizeof(Format));
    Format.SampleRate = INTCDSP_CL_SAMPLE_RATE;
    Format.ValidBitsPerSample = INTCDSP_CL_BITS_PER_SAMPLE;
    Format.ContainerSize = INTCDSP_CL_BITS_PER_SAMPLE;
    Format.NumberOfChannels = 1;
    Dev->Stream = NULL;
    Dev->StreamTag = 0;
    NtStatus = Dev->Bus.GetRenderStream(Dev->Bus.Context, Format, &Dev->Stream, &Dev->StreamTag);
    if (!NT_SUCCESS(NtStatus))
        goto Fail;
    Dev->StreamFragments = (ULONG)Entries;
    NtStatus = Dev->Bus.PrepareDSP(Dev->Bus.Context, Dev->Stream, (unsigned int)Dev->Image.Ri.PayloadBytes,
                                   (int)Entries, &Bdl);
    if (!NT_SUCCESS(NtStatus))
        goto Fail;
    if (!Bdl)
    {
        IntcDspLog("stream %u: the bus prepared it without a descriptor list\n", Dev->StreamTag);
        NtStatus = STATUS_DEVICE_PROTOCOL_ERROR;
        goto Fail;
    }
    Dev->StreamPrepared = TRUE;

    RtlZeroMemory(&Dma, sizeof(Dma));
    Dma.Virtual = Dev->DmaBuffer;
    Dma.Capacity = Dev->DmaBytes;
    Dma.Segment = &Dev->Segment;
    Dma.SegmentCount = 1;
    Dma.Bdl = (PFWLOAD_BDL_ENTRY)Bdl;
    Dma.BdlCapacity = INTCDSP_BDL_ENTRIES;
    Dma.BdlBytes = INTCDSP_BDL_BYTES;
    Dma.EntryBoundary = FWLOAD_CL_DMA_BOUNDARY_BYTES;
    Dma.StreamTag = Dev->StreamTag;
    Dma.StreamFormat = FWLOAD_CL_STREAM_FORMAT;
    FwloadDefaultTimeouts(&Timeouts);
    FwloadInitContext(&Dev->Fw, IntcDspOpsTable(), Dev, Platform, &Dev->Image.Ri, &Dma, &Timeouts);

    NtStatus = Dev->Bus.SetDSPPowerState(Dev->Bus.Context, PowerDeviceD0);
    if (!NT_SUCCESS(NtStatus))
        goto Fail;
    Dev->IdleHeld = TRUE;

    IntcDspLog("booting: firmware '%s' %u.%u.%u.%u, %I64u payload bytes in %u descriptors on stream %u, BAR4 %p, clock %I64u us at %I64u Hz\n",
               Dev->Image.Ri.Name, Dev->Image.Ri.Major, Dev->Image.Ri.Minor, Dev->Image.Ri.Hotfix, Dev->Image.Ri.Build, (ULONGLONG)Dev->Image.Ri.PayloadBytes,
               (ULONG)Entries, Dev->StreamTag, Dev->AdspBar.Base.Base, (ULONGLONG)IntcDspOpsTable()->NowUs(Dev),
               IntcDspClockFrequency());

    Dev->InterruptArmed = TRUE;
    NtStatus = Dev->Bus.RegisterInterrupt(Dev->Bus.Context, IntcDspInterruptRoutine(), Dev);
    Dev->InterruptRegistered = NT_SUCCESS(NtStatus);
    if (!Dev->InterruptRegistered)
        IntcDspLog("interrupt callback not registered: 0x%08lx; the boot polls on\n", NtStatus);

    /* the interrupt registers and the power state before the loader powers the subsystem, for the first boot on silicon */
    IntcDspLogInterruptState(Dev, "interrupts before power-up");
    Status = FwloadBoot(&Dev->Fw);
    Dev->InterruptArmed = FALSE;
    Dev->LastStatus = Status;
    Dev->Adspcs = IntcDspOpsTable()->Read32(Dev, FWLOAD_BAR_DSP, Dev->Platform->SubsystemControl);
    IntcDspLog("boot %s after %I64u us (clock %I64u us): state %s, failed in %s (%s), ROM status 0x%08x %s, ROM error 0x%08x %s, IPC status %u, last DSP message 0x%08x|0x%08x, attempt %u, %lu DSP interrupts, cleanup %s, ADSPCS 0x%08x\n",
               FwloadStatusName(Status), Dev->Fw.BootUs, (ULONGLONG)IntcDspOpsTable()->NowUs(Dev), FwloadStateName(Dev->Fw.State), FwloadStateName(Dev->Fw.FailedState),
               FwloadStatusName(Dev->Fw.Status), Dev->Fw.RomStatus, FwloadFsrStateName(Dev->Fw.RomStatus & FWLOAD_FSR_STATE_MASK),
               Dev->Fw.RomError, FwloadRomErrorName(Dev->Fw.RomError), Dev->Fw.LastIpcStatus, Dev->Fw.LastIpcPrimary, Dev->Fw.LastIpcExtension, Dev->Fw.Attempt,
               Dev->Fw.InterruptCount, FwloadStatusName(Dev->Fw.CleanupStatus), Dev->Adspcs);
    /* the registers as the service routine found them at the first interrupt, before it masked anything; no line when no interrupt came, which is what a poll-only boot expects */
    if (Dev->Fw.InterruptCount != 0)
    {
        IntcDspLog("first DSP interrupt: ADSPCS 0x%08x, HFINTIPPTR 0x%08x, INTEN 0x%08x, HfHIPCIE 0x%08x, HFIPCXCTL 0x%08x\n",
                   Dev->Fw.FirstInterrupt.SubsystemControl, Dev->Fw.FirstInterrupt.Pointer, Dev->Fw.FirstInterrupt.Enable,
                   Dev->Fw.FirstInterrupt.HostIpcEnable, Dev->Fw.FirstInterrupt.IpcControl);
    }
    /* the guard tripped: the enables do not gate the line on this controller, the service routine stopped claiming it, and it is taken off the line for good */
    if (Dev->Fw.InterruptStorm)
    {
        IntcDspLog("DSP interrupt storm: %lu interrupts in a row without a message consumed, the interrupt callback is disarmed and unregistered\n",
                   Dev->Fw.InterruptCount);
        if (Dev->InterruptRegistered)
        {
            Dev->Bus.UnregisterInterrupt(Dev->Bus.Context);
            Dev->InterruptRegistered = FALSE;
        }
    }
    if (Status == FwloadOk)
    {
        Registers = &Dev->Fw.FwConfig.Registers;
        Dev->Booted = TRUE;
        IntcDspLog("firmware ready: version %u.%u.%u.%u, ABI %u, ROM implementation %u.%u version %u.%u platform %u, ROM info 0x%08x, downlink mailbox %u bytes, uplink mailbox %u bytes, trace log %u bytes, %u pipelines, %u libraries, context save %u, %u tuples\n",
                   Dev->Fw.FwConfig.Major, Dev->Fw.FwConfig.Minor, Dev->Fw.FwConfig.Hotfix, Dev->Fw.FwConfig.Build,
                   Registers->AbiVersion, Registers->RomImplementationRevisionMajor, Registers->RomImplementationRevisionMinor,
                   Registers->RomImplementationVersionMajor, Registers->RomImplementationVersionMinor, Registers->RomPlatformType,
                   Registers->RomFuseValues, Dev->Fw.FwConfig.DownlinkMailboxBytes, Dev->Fw.FwConfig.UplinkMailboxBytes,
                   Dev->Fw.FwConfig.TraceLogBytes, Dev->Fw.FwConfig.MaxPipelines, Dev->Fw.FwConfig.MaxLibraries,
                   Dev->Fw.FwConfig.ContextSave, Dev->Fw.FwConfig.TupleCount);
        /* the transport hangs off the booted loader from here; the self-test is the opt-in check of the plan, at a D0 entry and never at an open (E1) */
        IntcDspIpcAttach(Dev);
        if (!AtOpen && Dev->SelfTest == INTCDSP_SELFTEST_IPC)
            IntcDspIpcSelfTest(Dev);
    }
    else
    {
        /* the tuple walk says which tuple it refused (D3): a length that is not a multiple of four, a tuple without a value, or a type the firmware does not define */
        if (Dev->Fw.FwConfig.BadTuple)
            IntcDspLog("firmware configuration: tuple type 0x%x at offset 0x%Ix of the reply is malformed\n",
                       Dev->Fw.FwConfig.BadTupleType, Dev->Fw.FwConfig.BadTupleOffset);
        IntcDspPowerDown(Dev);
    }

    IntcDspReleaseStream(Dev);
    return STATUS_SUCCESS;

Fail:
    IntcDspBootStop(Dev, "preparation", NtStatus);
    return STATUS_SUCCESS;
}

static
NTSTATUS
IntcDspEvtDevicePrepareHardware(
    _In_ WDFDEVICE Device,
    _In_ WDFCMRESLIST ResourcesRaw,
    _In_ WDFCMRESLIST ResourcesTranslated)
{
    PINTCDSP_DEVICE Dev = IntcDspGetData(Device);
    volatile ULONG* Ppcap;
    FWLOAD_BOOT_DECISION Decision;
    NTSTATUS Status;

    UNREFERENCED_PARAMETER(ResourcesRaw);
    UNREFERENCED_PARAMETER(ResourcesTranslated);

    RtlZeroMemory(&Dev->Bus, sizeof(Dev->Bus));
    Status = WdfFdoQueryForInterface(Device, &GUID_ADSP_BUS_INTERFACE, (PINTERFACE)&Dev->Bus, sizeof(Dev->Bus), ADSP_BUS_INTERFACE_VERSION, NULL);
    if (!NT_SUCCESS(Status))
    {
        IntcDspLog("no ADSP bus interface on the stack: 0x%08lx\n", Status);
        return Status;
    }
    if (!IntcDspBusComplete(&Dev->Bus))
    {
        IntcDspLog("ADSP bus interface version %u, size %u: incomplete\n", Dev->Bus.Version, Dev->Bus.Size);
        return STATUS_DEVICE_PROTOCOL_ERROR;
    }
    if (Dev->Bus.InterfaceReference)
        Dev->Bus.InterfaceReference(Dev->Bus.Context);
    Dev->BusValid = TRUE;

    Status = Dev->Bus.GetResources(Dev->Bus.Context, &Dev->HdaBar, &Dev->AdspBar, &Dev->Ppcap, &Dev->Nhlt, NULL);
    if (!NT_SUCCESS(Status))
    {
        IntcDspLog("GetResources: 0x%08lx\n", Status);
        /* the reference the interface was given with goes back on this way out too (R1-08); the release of the hardware that follows a failed preparation (N5-01) finds BusValid clear, and its own end touches the bus only while it is set */
        if (Dev->Bus.InterfaceDereference)
            Dev->Bus.InterfaceDereference(Dev->Bus.Context);
        Dev->BusValid = FALSE;
        return Status;
    }
    Dev->DevId = Dev->Bus.CtlrDevId;
    Dev->VenId = INTCDSP_VEN_INTEL;
    /* the controller's record names its register map and its firmware file, which the path is built from; NULL for a controller the table does not name */
    Dev->Platform = FwloadPlatformForDevice(Dev->DevId);
    IntcDspFirmwarePath(Dev);

    Ppcap = Dev->Ppcap;
    if (Ppcap)
    {
        Dev->PpCapHeader = READ_REGISTER_ULONG(Ppcap);
        Dev->PpCtl = READ_REGISTER_ULONG(Ppcap + INTCDSP_PP_OFFSET_PPCTL / sizeof(ULONG));
        Dev->PpSts = READ_REGISTER_ULONG(Ppcap + INTCDSP_PP_OFFSET_PPSTS / sizeof(ULONG));
    }
    IntcDspProbe(Dev);
    IntcDspLoadFirmware(Dev);
    /* the code-loader buffer of the image, kept for the hardware like it (R1-03): no boot, and no resume, allocates contiguous memory; without it the boot says so and leaves the DSP alone */
    if (IntcDspFirmwareResident(Dev) && !NT_SUCCESS(IntcDspDmaBufferAllocate(Dev)))
        IntcDspLog("the code-loader buffer of the firmware image is not made at start; a boot makes it if it can\n");

    Status = IntcDspReadSetting(INTCDSP_SETTINGS_VALUE, &Dev->BootDsp);
    if (!NT_SUCCESS(Status))
    {
        Dev->BootDsp = 0;
        IntcDspLog("BootDsp is not set (%s): probe only, the DSP is not touched\n",
                   (Status == STATUS_OBJECT_NAME_NOT_FOUND) ? "value absent" : "unreadable");
        return STATUS_SUCCESS;
    }
    Decision = FwloadBootDecision(Dev->Platform, IntcDspFirmwareResident(Dev), Dev->BootDsp != 0);
    switch (Decision)
    {
        case FwloadBootYes:
            IntcDspLog("BootDsp = %lu: the DSP is booted at the next D0 entry with '%s'\n", Dev->BootDsp, Dev->Platform->FirmwareName);
            break;
        case FwloadBootNotRequested:
            IntcDspLog("BootDsp = %lu: probe only, the DSP is not touched\n", Dev->BootDsp);
            break;
        default:
            /* the two reasons a set value cannot boot: the controller has no record, or its file is not there */
            IntcDspLog("BootDsp = %lu: %s, probe only\n", Dev->BootDsp, FwloadBootDecisionName(Decision));
            break;
    }
    return STATUS_SUCCESS;
}

static
NTSTATUS
IntcDspEvtDeviceReleaseHardware(
    _In_ WDFDEVICE Device,
    _In_ WDFCMRESLIST ResourcesTranslated)
{
    PINTCDSP_DEVICE Dev = IntcDspGetData(Device);
    UINT32 Held;

    UNREFERENCED_PARAMETER(ResourcesTranslated);
    IntcDspPowerDown(Dev);
    IntcDspReleaseStream(Dev);
    /* the buffers held for their pins across a power-down or a fault (stream.c, R5-01) outlive the hardware when a pin did not free its own: they stay allocated, with the pages and the MDL the pin may still map; a leak is the safe side, a free under a mapping is not */
    Held = IntcDspStreamsHeld(&Dev->Streams);
    if (Held != 0)
        IntcDspLog("streams: %lu buffer%s still held for its pin%s at the removal; left allocated (a free under a mapping is not)\n", Held, Held == 1 ? "" : "s", Held == 1 ? "" : "s");
    if (Dev->DmaBuffer)
        MmFreeContiguousMemorySpecifyCache(Dev->DmaBuffer, (ULONG)Dev->DmaBytes, MmNonCached);
    Dev->DmaBuffer = NULL;
    Dev->DmaBytes = 0;
    RtlZeroMemory(&Dev->Segment, sizeof(Dev->Segment));
    IntcDspFreeFirmware(Dev);
    if (Dev->Topology)
        ExFreePoolWithTag(Dev->Topology, INTCDSP_POOL_TAG);
    Dev->Topology = NULL;
    Dev->TopologyBytes = 0;
    Dev->TopologyModel = NULL;
    Dev->TopologyName[0] = 0;
    Dev->DspState = IntcDspNoDsp;
    if (Dev->StreamsState)
        ExFreePoolWithTag(Dev->StreamsState, INTCDSP_POOL_TAG);
    Dev->StreamsState = NULL;
    Dev->StreamsStateBytes = 0;
    /* the topology is gone with the hardware: the next start reports the children the new one names, or none */
    Dev->TopologyGeneration = 1;
    Dev->AudioReportedGeneration = 0;
    Dev->AudioReportedResident = FALSE;
    if (Dev->BusValid && Dev->Bus.InterfaceDereference)
        Dev->Bus.InterfaceDereference(Dev->Bus.Context);
    Dev->BusValid = FALSE;
    RtlZeroMemory(&Dev->Bus, sizeof(Dev->Bus));
    Dev->Ppcap = NULL;
    Dev->Nhlt.Nhlt = NULL;
    Dev->Nhlt.NhltSz = 0;
    Dev->FirmwarePath[0] = 0;
    Dev->Platform = NULL;
    return STATUS_SUCCESS;
}

/* Whether the firmware posted a message with no request outstanding: the uplink's pending bit, the target register's busy one in the loader's receive order (a reply's done bit is a request's own, sdk/lib/drivers/fwload fwboot.c FwloadInterruptService), read once without a wait; FALSE when the DSP is not up */
static
BOOLEAN
IntcDspUplinkPending(
    _In_ PINTCDSP_DEVICE Dev)
{
    return Dev->Fw.State == FwloadStateReady && Dev->Platform != NULL &&
        (IntcDspOpsTable()->Read32(Dev, FWLOAD_BAR_DSP, Dev->Platform->IpcTarget) & FWLOAD_IPC_BUSY) != 0;
}

/* The DPC of the poll point (E1c): the interrupt callback queues it at each of its runs, a period's included (hwops.c IntcDspInterruptCallback). It reads the uplink's pending bit once, without a wait, and queues the work item when the bit is set; nothing is sent, nothing is booted and nothing is released at DISPATCH_LEVEL */
VOID
IntcDspPeriodDpc(
    _In_ PKDPC Dpc,
    _In_opt_ PVOID DeferredContext,
    _In_opt_ PVOID SystemArgument1,
    _In_opt_ PVOID SystemArgument2)
{
    PINTCDSP_DEVICE Dev = (PINTCDSP_DEVICE)DeferredContext;

    UNREFERENCED_PARAMETER(Dpc);
    UNREFERENCED_PARAMETER(SystemArgument1);
    UNREFERENCED_PARAMETER(SystemArgument2);
    if (Dev->PollWorkItem != NULL && IntcDspUplinkPending(Dev))
        WdfWorkItemEnqueue(Dev->PollWorkItem);
}

/* The work item of the poll point (E1c): at PASSIVE_LEVEL the manager's poll (stream.c IntcDspStreamsPoll), which reads what the firmware posted and faults as after a request that failed; the device is the work item's parent */
static
VOID
IntcDspPollWork(
    _In_ WDFWORKITEM WorkItem)
{
    IntcDspStreamsPoll(&IntcDspGetData(WdfWorkItemGetParentObject(WorkItem))->Streams);
}

static
NTSTATUS
IntcDspEvtDeviceD0Entry(
    _In_ WDFDEVICE Device,
    _In_ WDF_POWER_DEVICE_STATE PreviousState)
{
    PINTCDSP_DEVICE Dev = IntcDspGetData(Device);

    UNREFERENCED_PARAMETER(PreviousState);
    if (!Dev->BusValid)
        return STATUS_SUCCESS;
    IntcDspReadSetting(INTCDSP_SETTINGS_VALUE, &Dev->BootDsp);
    IntcDspReadIpcSettings(Dev);
    /* BootDsp = 1 boots at every D0 entry (E2 changes the rule): the sequence the boot at an open shares, with the self-tests */
    if (Dev->BootDsp)
    {
        /* serialized with the boot at an open (N10-04): the manager is marked around the sequence, so that an open coming while it runs is refused instead of booting the device a second time; the mark is set and cleared under the manager's lock, which is held around nothing here and which the sequence takes itself at its start of the manager */
        IntcDspStreamsDeviceBootBegin(&Dev->Streams);
        IntcDspBootSequence(Dev, FALSE);
        IntcDspStreamsDeviceBootEnd(&Dev->Streams);
    }
    /* the audio children follow the topology the device holds: one per PCM of its model while the model is resident (audioif.c) */
    IntcDspAudioUpdate(Dev);
    return STATUS_SUCCESS;
}

static
NTSTATUS
IntcDspEvtDeviceD0Exit(
    _In_ WDFDEVICE Device,
    _In_ WDF_POWER_DEVICE_STATE TargetState)
{
    PINTCDSP_DEVICE Dev = IntcDspGetData(Device);

    UNREFERENCED_PARAMETER(TargetState);
    if (Dev->BusValid)
    {
        IntcDspPowerDown(Dev);
        /* the topology model outlives the power-down (R6): the next open boots (E1); a device without a model stays as it was */
        if (Dev->DspState != IntcDspNoDsp)
            Dev->DspState = IntcDspNeedsBoot;
    }
    return STATUS_SUCCESS;
}

static
VOID
IntcDspEvtDeviceContextCleanup(
    _In_ WDFOBJECT Object)
{
    PINTCDSP_DEVICE Dev = IntcDspGetData(Object);

    Dev->InterruptArmed = FALSE;
    /* the poll point's DPC goes with the device: one still queued does not run */
    KeRemoveQueueDpc(&Dev->PeriodDpc);
}

NTSTATUS
IntcDspEvtDeviceAdd(
    _In_ WDFDRIVER Driver,
    _Inout_ PWDFDEVICE_INIT DeviceInit)
{
    WDF_OBJECT_ATTRIBUTES Attributes;
    WDF_PNPPOWER_EVENT_CALLBACKS PnpPower;
    WDF_WORKITEM_CONFIG WorkConfig;
    WDF_OBJECT_ATTRIBUTES WorkAttributes;
    WDFDEVICE Device;
    NTSTATUS Status;

    UNREFERENCED_PARAMETER(Driver);
    WDF_PNPPOWER_EVENT_CALLBACKS_INIT(&PnpPower);
    PnpPower.EvtDevicePrepareHardware = IntcDspEvtDevicePrepareHardware;
    PnpPower.EvtDeviceReleaseHardware = IntcDspEvtDeviceReleaseHardware;
    PnpPower.EvtDeviceD0Entry = IntcDspEvtDeviceD0Entry;
    PnpPower.EvtDeviceD0Exit = IntcDspEvtDeviceD0Exit;
    WdfDeviceInitSetPnpPowerEventCallbacks(DeviceInit, &PnpPower);
    /* the audio children of a topology: the child list the device reports them through */
    IntcDspAudioInitDevice(DeviceInit);

    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&Attributes, INTCDSP_DEVICE);
    Attributes.EvtCleanupCallback = IntcDspEvtDeviceContextCleanup;
    Status = WdfDeviceCreate(&DeviceInit, &Attributes, &Device);
    if (!NT_SUCCESS(Status))
        return Status;
    /* the device's own handle, which the audio children's report takes (audioif.c) */
    IntcDspGetData(Device)->Device = Device;
    /* the lock of the stream manager's transitions, made once for the device: a power transition may release what the manager never started, before its first initialization (stream.c) */
    KeInitializeMutex(&IntcDspGetData(Device)->Streams.Lock, 0);
    /* the poll point of an asynchronous exception (E1c): the DPC and its work item, made once for the device; without the work item an exception with no request outstanding waits for the next request, as before */
    KeInitializeDpc(&IntcDspGetData(Device)->PeriodDpc, IntcDspPeriodDpc, IntcDspGetData(Device));
    WDF_WORKITEM_CONFIG_INIT(&WorkConfig, IntcDspPollWork);
    WDF_OBJECT_ATTRIBUTES_INIT(&WorkAttributes);
    WorkAttributes.ParentObject = Device;
    Status = WdfWorkItemCreate(&WorkConfig, &WorkAttributes, &IntcDspGetData(Device)->PollWorkItem);
    if (!NT_SUCCESS(Status))
    {
        IntcDspGetData(Device)->PollWorkItem = NULL;
        IntcDspLog("the poll's work item: 0x%08lx; an exception with no request outstanding waits for the next request\n", Status);
    }
    IntcDspLog("loaded for the Intel audio DSP child\n");
    return STATUS_SUCCESS;
}

NTSTATUS
DriverEntry(
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ PUNICODE_STRING RegistryPath)
{
    WDF_DRIVER_CONFIG Config;
    WDFDRIVER Driver;
    NTSTATUS Status;

    WDF_DRIVER_CONFIG_INIT(&Config, IntcDspEvtDeviceAdd);
    Config.DriverPoolTag = INTCDSP_POOL_TAG;
    Status = WdfDriverCreate(DriverObject, RegistryPath, WDF_NO_OBJECT_ATTRIBUTES, &Config, &Driver);
    if (!NT_SUCCESS(Status))
        return Status;
    IntcDspInitClock();
    return STATUS_SUCCESS;
}
