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

/* The transport's three settings: absent, unreadable or out of range leaves the plan's default */
static
VOID
IntcDspReadIpcSettings(
    _Inout_ PINTCDSP_DEVICE Dev)
{
    ULONG Value;

    Dev->SelfTest = 0;
    Dev->UseInterrupts = 0;
    Dev->IpcTimeoutMs = INTCDSP_IPC_TIMEOUT_MS_DEFAULT;
    if (NT_SUCCESS(IntcDspReadSetting(INTCDSP_SETTINGS_SELFTEST, &Value)))
        Dev->SelfTest = Value;
    if (NT_SUCCESS(IntcDspReadSetting(INTCDSP_SETTINGS_USEINTERRUPTS, &Value)))
        Dev->UseInterrupts = Value;
    if (NT_SUCCESS(IntcDspReadSetting(INTCDSP_SETTINGS_IPCTIMEOUTMS, &Value)) && Value != 0 && Value <= INTCDSP_IPC_TIMEOUT_MS_MAX)
        Dev->IpcTimeoutMs = Value;
    IntcDspLog("SelfTest = %lu, UseInterrupts = %lu, IpcTimeoutMs = %lu ms\n", Dev->SelfTest, Dev->UseInterrupts, Dev->IpcTimeoutMs);
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
    SofIpcDefaultTimeouts(Timeouts, Dev->IpcTimeoutMs);
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

/* A request faulted the link: as every faulted link ends, the DSP is powered down and booted again at the next D0 entry */
static
VOID
IntcDspStreamFault(
    _In_opt_ PVOID Context)
{
    PINTCDSP_DEVICE Dev = (PINTCDSP_DEVICE)Context;

    IntcDspPowerDown(Dev);
    Dev->Booted = FALSE;
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
    _Inout_ PINTCDSP_DEVICE Dev)
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
    Dev->StreamOps.Unmap = IntcDspStreamUnmap;
    Dev->SelfTestOps.Context = Dev;
    Dev->SelfTestOps.Map = IntcDspSelfTestMap;
    Dev->SelfTestOps.WaitPeriods = IntcDspSelfTestWait;
    KeInitializeEvent(&Dev->SelfTestEvent, SynchronizationEvent, FALSE);
    IntcDspStreamsInit(&Dev->Streams, Dev->StreamsState, Dev->StreamsStateBytes, Dev->TopologyModel, &Dev->Ipc, &Dev->Bus, &Dev->StreamOps);
    /* a bus without the stream members, or a link that is not ready: the DSP stays as the earlier checkpoints left it, with no stream and no self-test */
    if (!Dev->Streams.Ready)
        return STATUS_SUCCESS;
    if (Dev->SelfTest == INTCDSP_SELFTEST_LOOPBACK)
    {
        Status = IntcDspLoopbackSelfTest(&Dev->Streams, &Dev->SelfTestOps, INTCDSP_SELFTEST_FRAMES);
        /* the manager logged the lines; a fault powered the DSP down, a capture that does not match leaves it up, as the plan's stop rule says */
        IntcDspLog("loopback self-test: %s (0x%08lx), the DSP is %s\n", NT_SUCCESS(Status) ? "ran" : "failed", Status,
                   Dev->Booted ? "left running" : "powered down");
    }
    return STATUS_SUCCESS;
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
    if (Dev->DmaBuffer)
        MmFreeContiguousMemorySpecifyCache(Dev->DmaBuffer, (ULONG)Dev->DmaBytes, MmNonCached);
    Dev->DmaBuffer = NULL;
    Dev->DmaBytes = 0;
    RtlZeroMemory(&Dev->Segment, sizeof(Dev->Segment));
}

NTSTATUS
IntcDspBoot(
    _Inout_ PINTCDSP_DEVICE Dev)
{
    const FWLOAD_PLATFORM* Platform = Dev->Platform;
    FWLOAD_TIMEOUTS Timeouts;
    FWLOAD_CL_DMA Dma;
    HDAUDIO_STREAM_FORMAT Format;
    PHYSICAL_ADDRESS Low, High, Boundary;
    const SOFIPC4_FW_REGS* Registers;
    PVOID Bdl = NULL;
    SIZE_T Entries;
    FWLOAD_STATUS Status;
    NTSTATUS NtStatus;

    if (Dev->Booted)
        return STATUS_SUCCESS;
    if (!Platform)
    {
        IntcDspLog("controller %04x has no register map; the DSP is left alone\n", Dev->DevId);
        return STATUS_SUCCESS;
    }
    if (!Dev->AdspBar.Base.Base || Dev->AdspBar.Len < INTCDSP_BAR4_MIN_BYTES || !Dev->Ppcap)
    {
        IntcDspLog("BAR4 (%p, 0x%lx bytes, needs 0x%x) or the processing capabilities (%p) are missing; the DSP is left alone\n",
                   Dev->AdspBar.Base.Base, Dev->AdspBar.Len, INTCDSP_BAR4_MIN_BYTES, Dev->Ppcap);
        return STATUS_SUCCESS;
    }

    NtStatus = IntcDspLoadFirmware(Dev);
    if (!NT_SUCCESS(NtStatus))
    {
        Dev->LastStatus = Dev->FirmwareStatus;
        return NtStatus;
    }

    Dev->DmaBytes = (SIZE_T)ROUND_TO_PAGES((ULONG)Dev->Image.Ri.PayloadBytes);
    Low.QuadPart = 0;
    High.QuadPart = (LONGLONG)IntcDspDmaHighBound(&Dev->Bus);
    Boundary.QuadPart = 0;
    Dev->DmaBuffer = MmAllocateContiguousMemorySpecifyCache((ULONG)Dev->DmaBytes, Low, High, Boundary, MmNonCached);
    if (!Dev->DmaBuffer)
    {
        NtStatus = STATUS_INSUFFICIENT_RESOURCES;
        goto Fail;
    }
    Dev->Segment.Address = MmGetPhysicalAddress(Dev->DmaBuffer).QuadPart;
    Dev->Segment.Bytes = Dev->DmaBytes;
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
        /* the transport hangs off the booted loader from here; the self-test is the opt-in check of the plan */
        IntcDspIpcAttach(Dev);
        if (Dev->SelfTest == INTCDSP_SELFTEST_IPC)
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
    MmFreeContiguousMemorySpecifyCache(Dev->DmaBuffer, (ULONG)Dev->DmaBytes, MmNonCached);
    Dev->DmaBuffer = NULL;
    Dev->DmaBytes = 0;
    RtlZeroMemory(&Dev->Segment, sizeof(Dev->Segment));
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
        return Status;
    }
    Dev->DevId = Dev->Bus.CtlrDevId;
    Dev->VenId = INTCDSP_VEN_INTEL;
    Dev->Platform = (Dev->DevId == INTCDSP_DEV_ARL_S) ? FwloadPlatformArlS() : NULL;

    Ppcap = Dev->Ppcap;
    if (Ppcap)
    {
        Dev->PpCapHeader = READ_REGISTER_ULONG(Ppcap);
        Dev->PpCtl = READ_REGISTER_ULONG(Ppcap + INTCDSP_PP_OFFSET_PPCTL / sizeof(ULONG));
        Dev->PpSts = READ_REGISTER_ULONG(Ppcap + INTCDSP_PP_OFFSET_PPSTS / sizeof(ULONG));
    }
    IntcDspProbe(Dev);
    IntcDspLoadFirmware(Dev);

    Status = IntcDspReadSetting(INTCDSP_SETTINGS_VALUE, &Dev->BootDsp);
    if (NT_SUCCESS(Status))
        IntcDspLog("BootDsp = %lu: %s\n", Dev->BootDsp,
                   Dev->BootDsp ? (Dev->Platform ? "the DSP is booted at the next D0 entry" : "no register map for this controller, probe only")
                                : "probe only, the DSP is not touched");
    else
        IntcDspLog("BootDsp is not set (%s): probe only, the DSP is not touched\n",
                   (Status == STATUS_OBJECT_NAME_NOT_FOUND) ? "value absent" : "unreadable");
    return STATUS_SUCCESS;
}

static
NTSTATUS
IntcDspEvtDeviceReleaseHardware(
    _In_ WDFDEVICE Device,
    _In_ WDFCMRESLIST ResourcesTranslated)
{
    PINTCDSP_DEVICE Dev = IntcDspGetData(Device);

    UNREFERENCED_PARAMETER(ResourcesTranslated);
    IntcDspPowerDown(Dev);
    IntcDspReleaseStream(Dev);
    if (Dev->DmaBuffer)
        MmFreeContiguousMemorySpecifyCache(Dev->DmaBuffer, (ULONG)Dev->DmaBytes, MmNonCached);
    Dev->DmaBuffer = NULL;
    Dev->DmaBytes = 0;
    IntcDspFreeFirmware(Dev);
    if (Dev->Topology)
        ExFreePoolWithTag(Dev->Topology, INTCDSP_POOL_TAG);
    Dev->Topology = NULL;
    Dev->TopologyBytes = 0;
    Dev->TopologyModel = NULL;
    Dev->TopologyName[0] = 0;
    if (Dev->StreamsState)
        ExFreePoolWithTag(Dev->StreamsState, INTCDSP_POOL_TAG);
    Dev->StreamsState = NULL;
    Dev->StreamsStateBytes = 0;
    /* the topology is gone with the hardware: the next start reports the children the new one names, or none */
    Dev->TopologyGeneration = 1;
    Dev->AudioReportedGeneration = 0;
    Dev->AudioReportedReady = FALSE;
    if (Dev->BusValid && Dev->Bus.InterfaceDereference)
        Dev->Bus.InterfaceDereference(Dev->Bus.Context);
    Dev->BusValid = FALSE;
    RtlZeroMemory(&Dev->Bus, sizeof(Dev->Bus));
    Dev->Ppcap = NULL;
    Dev->Nhlt.Nhlt = NULL;
    Dev->Nhlt.NhltSz = 0;
    Dev->Platform = NULL;
    return STATUS_SUCCESS;
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
    if (Dev->BootDsp)
    {
        IntcDspBoot(Dev);
        /* the plan's fail-safe: a booted DSP with no topology the firmware can run is powered down again and gets no endpoint */
        if (Dev->Booted && !IntcDspTopologyLoad(Dev))
            IntcDspPowerDown(Dev);
        /* the streams hang off the booted loader and its topology from here; the self-test is the opt-in check of the plan, and a fault of it powers the DSP down itself */
        if (Dev->Booted)
            IntcDspStreamsStart(Dev);
    }
    /* the audio children follow the topology the device holds: one per PCM when its streams are open for them, none otherwise */
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
        IntcDspPowerDown(Dev);
    return STATUS_SUCCESS;
}

static
VOID
IntcDspEvtDeviceContextCleanup(
    _In_ WDFOBJECT Object)
{
    PINTCDSP_DEVICE Dev = IntcDspGetData(Object);

    Dev->InterruptArmed = FALSE;
}

NTSTATUS
IntcDspEvtDeviceAdd(
    _In_ WDFDRIVER Driver,
    _Inout_ PWDFDEVICE_INIT DeviceInit)
{
    WDF_OBJECT_ATTRIBUTES Attributes;
    WDF_PNPPOWER_EVENT_CALLBACKS PnpPower;
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
