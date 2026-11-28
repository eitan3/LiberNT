/*
 * PROJECT:     LiberNT host-native tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     A fake hdaudbus DSP child that answers the ADSP bus interface query the way the framework's query handler does, in version 1 or 2 and with the 64-bit DMA capability the case asks for; from version 2's stream members it keeps a pool of render and capture streams with their tags, buffers, states and period events, and a log of what the driver did to them
 * COPYRIGHT:   Copyright 2026 LiberNT contributors
 */

#ifdef FAKE_BUS_KERNEL
#include <kmt_test.h>
#else
#include "shim.h"
#include <stdlib.h>
#endif
#include "fake_bus.h"

#ifdef FAKE_BUS_KERNEL
/* the kernel's PCI bus interface takes the calling convention of its members; the host's stub has none */
#define FAKE_BUS_CALL NTAPI
#else
#define FAKE_BUS_CALL
#endif

/* The byte of the model's config space at Offset: bytes 0x44 to 0x4B are the two gating words' (little endian, as the model and the target are), every other byte is the array's */
static UINT8* FakePciByte(PFAKE_BUS Bus, ULONG Offset)
{
    if (Offset >= HDA_PCIREG_PGCTL && Offset < HDA_PCIREG_PGCTL + sizeof(Bus->Config))
        return (UINT8*)&Bus->Config + (Offset - HDA_PCIREG_PGCTL);
    return &Bus->PciConfig[Offset];
}

/* Whether the test marked the byte at Offset in one of the three masks */
static BOOLEAN FakePciMarked(const UINT32* Marks, ULONG Offset)
{
    return (Marks[Offset >> 5] & (1u << (Offset & 31))) != 0;
}

/*
 * The fake's two members of the PCI bus interface over the model's config space. A read moves its
 * bytes whole unless it touches a byte marked as not answering: then it moves nothing. A write lands
 * in every byte but those marked as not taking one; a byte marked as taking one byte alone takes a
 * transfer of one byte and refuses a longer one. The bus sees every write go whole, as the real
 * interface reports the bytes it moved, not what the register did with them.
 */
static ULONG FAKE_BUS_CALL FakeBusGetBusData(PVOID Context, ULONG WhichSpace, PVOID Buffer, ULONG Offset, ULONG Length)
{
    PFAKE_BUS Bus = (PFAKE_BUS)Context;
    ULONG Index;

    (VOID)WhichSpace;
    Bus->PciReads++;
    if (Offset > sizeof(Bus->PciConfig) || Length > sizeof(Bus->PciConfig) - Offset)
        return 0;
    for (Index = 0; Index < Length; Index++)
    {
        if (FakePciMarked(Bus->PciNoRead, Offset + Index))
            return 0;
        ((PUCHAR)Buffer)[Index] = *FakePciByte(Bus, Offset + Index);
    }
    return Length;
}

static ULONG FAKE_BUS_CALL FakeBusSetBusData(PVOID Context, ULONG WhichSpace, PVOID Buffer, ULONG Offset, ULONG Length)
{
    PFAKE_BUS Bus = (PFAKE_BUS)Context;
    ULONG Index;

    (VOID)WhichSpace;
    Bus->PciWrites++;
    if (Offset > sizeof(Bus->PciConfig) || Length > sizeof(Bus->PciConfig) - Offset)
        return 0;
    for (Index = 0; Index < Length; Index++)
    {
        if (FakePciMarked(Bus->PciNoStick, Offset + Index) ||
            (Length > 1 && FakePciMarked(Bus->PciByteOnly, Offset)))
            continue;
        *FakePciByte(Bus, Offset + Index) = ((const UCHAR*)Buffer)[Index];
    }
    return Length;
}

/*
 * The bus's member for the DSP's power gating (N15): the bus's own rule over the model's config
 * space, with its readback (dspgating.h HdaDspPowerGatingWrite), through the fake's two PCI members,
 * as adsp.cpp applies it to the controller's config space
 */
static NTSTATUS FakeBusPowerGating(PVOID Context, BOOLEAN AllowGating)
{
    PFAKE_BUS Bus = (PFAKE_BUS)Context;
    BUS_INTERFACE_STANDARD Pci;

    if (Bus == NULL)
        return STATUS_NO_SUCH_DEVICE;
    Bus->GatingCalls++;
    if (AllowGating)
        Bus->GatingAllowCalls++;
    FakeBusPciInterface(Bus, &Pci);
    return HdaDspPowerGatingWrite(&Pci, AllowGating ? 1 : 0, &Bus->GatingReadback);
}

/* The member that hands out what the last call read */
static VOID FakeBusGatingReadback(PVOID Context, PHDA_DSP_GATING_READBACK Readback)
{
    *Readback = ((PFAKE_BUS)Context)->GatingReadback;
}

VOID
FakeBusPowerGatingEnable(
    PFAKE_BUS Bus)
{
    Bus->Interface.SetDspPowerGating = FakeBusPowerGating;
    Bus->Interface.GetDspPowerGatingReadback = FakeBusGatingReadback;
}

VOID
FakeBusPciInterface(
    PFAKE_BUS Bus,
    PBUS_INTERFACE_STANDARD Pci)
{
    RtlZeroMemory(Pci, sizeof(*Pci));
    Pci->Context = Bus;
    Pci->GetBusData = FakeBusGetBusData;
    Pci->SetBusData = FakeBusSetBusData;
}

VOID
FakeBusPciNoRead(
    PFAKE_BUS Bus,
    UINT32 Offset)
{
    Bus->PciNoRead[Offset >> 5] |= 1u << (Offset & 31);
}

VOID
FakeBusPciNoStick(
    PFAKE_BUS Bus,
    UINT32 Offset)
{
    Bus->PciNoStick[Offset >> 5] |= 1u << (Offset & 31);
}

VOID
FakeBusPciByteOnly(
    PFAKE_BUS Bus,
    UINT32 Offset)
{
    Bus->PciByteOnly[Offset >> 5] |= 1u << (Offset & 31);
}

#ifndef FAKE_BUS_KERNEL
/* The bus whose MDLs the host's stubs of the kernel's free act on: the last made, which in a harness is the stream manager's */
static FAKE_BUS* FakeBusMdlOwner;
#endif

/* The members of version 1 answer success and do nothing */
static NTSTATUS NoResources(PVOID Context, PCI_BAR* HdaBar, PCI_BAR* AdspBar, PVOID* Ppcap, PNHLT_INFO NhltInfo, BUS_INTERFACE_STANDARD* PciConfig)
{
    return STATUS_SUCCESS;
}

static NTSTATUS NoPowerState(PVOID Context, DEVICE_POWER_STATE NewPowerState)
{
    return STATUS_SUCCESS;
}

static NTSTATUS NoRegisterInterrupt(PVOID Context, PADSP_INTERRUPT_CALLBACK Callback, PVOID CallbackContext)
{
    return STATUS_SUCCESS;
}

static NTSTATUS NoUnregisterInterrupt(PVOID Context)
{
    return STATUS_SUCCESS;
}

static NTSTATUS NoStream(PVOID Context, HDAUDIO_STREAM_FORMAT StreamFormat, HANDLE* Handle, UINT8* StreamTag)
{
    return STATUS_SUCCESS;
}

static NTSTATUS NoFreeStream(PVOID Context, HANDLE Handle)
{
    return STATUS_SUCCESS;
}

static NTSTATUS NoPrepareStream(PVOID Context, HANDLE Handle, unsigned int ByteSize, int Frags, PVOID* BdlBuf)
{
    return STATUS_SUCCESS;
}

static NTSTATUS NoCleanupStream(PVOID Context, HANDLE Handle)
{
    return STATUS_SUCCESS;
}

static UINT32 NoPosition(PVOID Context, HANDLE Handle)
{
    return 0;
}

static void NoStartStop(PVOID Context, HANDLE Handle, int StartStop)
{
}

static void NoEnableSpib(PVOID Context, HANDLE Handle, UINT32 Value)
{
}

static void NoDisableSpib(PVOID Context, HANDLE Handle)
{
}

static void NoLock(PVOID Context)
{
}

static void NoReference(PVOID Context)
{
}

static
NTSTATUS
FakeBusPlatformInfo(
    PVOID Context,
    PADSP_PLATFORM_INFO Info)
{
    PFAKE_BUS Bus = (PFAKE_BUS)Context;

    Bus->InfoCalls++;
    *Info = Bus->Info;
    return Bus->InfoStatus;
}

/* The host's blocks: the fake's MDL and its pages (the kernel twin's MDL carries real pages, FakeBusAllocateBuffer) */
#ifndef FAKE_BUS_KERNEL
static
PVOID
FakeBusAllocate(
    SIZE_T Bytes)
{
    return calloc(1, Bytes);
}

static
VOID
FakeBusFree(
    PVOID Memory)
{
    free(Memory);
}
#endif

/* One line of the log: what the driver did to a stream, if it names one */
static
VOID
FakeBusLog(
    PFAKE_BUS Bus,
    UINT32 Kind,
    PFAKE_BUS_STREAM Stream,
    UINT32 Value)
{
    PFAKE_BUS_EVENT Event;

    if (Kind <= FAKE_BUS_EVENT_LAST)
        Bus->Counts[Kind]++;
    if (Bus->LogCount >= FAKE_BUS_EVENTS_LOG)
        return;
    Event = &Bus->Log[Bus->LogCount++];
    Event->Kind = (UINT8)Kind;
    Event->Capture = Stream ? Stream->Capture : 0;
    Event->Tag = Stream ? Stream->Tag : 0;
    Event->Reserved = 0;
    Event->Value = Value;
}

/* The first stream of a direction's pool that is free, with its tag; NULL when the pool is exhausted or the test made the allocation fail */
static
PFAKE_BUS_STREAM
FakeBusTake(
    PFAKE_BUS Bus,
    BOOLEAN Capture)
{
    PFAKE_BUS_STREAM Stream;
    UINT32 Index, Count = Capture ? Bus->CaptureStreams : Bus->RenderStreams;
    NTSTATUS Status = Capture ? Bus->CaptureStatus : Bus->RenderStatus;

    if (Status != STATUS_SUCCESS)
        return NULL;
    for (Index = 0; Index < Count && Index < FAKE_BUS_STREAMS; Index++)
    {
        Stream = &Bus->Stream[Capture ? 1 : 0][Index];
        if (Stream->Used)
            continue;
        Stream->Used = TRUE;
        Stream->Capture = Capture;
        Stream->Tag = (UINT8)(Index + 1);
        Stream->Running = FALSE;
        Stream->Buffer = FALSE;
        Stream->Spib = FALSE;
        Stream->State = (UINT8)ResetState;
        Stream->FifoSize = Bus->FifoSize;
        Stream->Registrations = 0;
        return Stream;
    }
    return NULL;
}

static
NTSTATUS
FakeBusGetStream(
    PVOID Context,
    HDAUDIO_STREAM_FORMAT StreamFormat,
    HANDLE* Handle,
    UINT8* StreamTag,
    BOOLEAN Capture)
{
    PFAKE_BUS Bus = (PFAKE_BUS)Context;
    PFAKE_BUS_STREAM Stream;
    NTSTATUS Status;

    Stream = FakeBusTake(Bus, Capture);
    if (Stream == NULL)
    {
        /* the pool is exhausted, or the test made the allocation fail: the bus's own answer to a pool without a free stream */
        Status = Capture ? Bus->CaptureStatus : Bus->RenderStatus;
        return Status != STATUS_SUCCESS ? Status : STATUS_INSUFFICIENT_RESOURCES;
    }
    Stream->Format = StreamFormat;
    RtlZeroMemory(Stream->Events, sizeof(Stream->Events));
    Stream->Position = 0;
    Stream->DmaPosition = 0;
    if (Handle)
        *Handle = (HANDLE)Stream;
    if (StreamTag)
        *StreamTag = Stream->Tag;
    FakeBusLog(Bus, FAKE_BUS_EVENT_OPEN, Stream, StreamFormat.SampleRate);
    return STATUS_SUCCESS;
}

static NTSTATUS FakeBusGetRenderStream(PVOID Context, HDAUDIO_STREAM_FORMAT StreamFormat, HANDLE* Handle, UINT8* StreamTag)
{
    return FakeBusGetStream(Context, StreamFormat, Handle, StreamTag, FALSE);
}

static NTSTATUS FakeBusGetCaptureStream(PVOID Context, HDAUDIO_STREAM_FORMAT StreamFormat, HANDLE* Handle, UINT8* StreamTag)
{
    return FakeBusGetStream(Context, StreamFormat, Handle, StreamTag, TRUE);
}

static NTSTATUS FakeBusFreeStream(PVOID Context, HANDLE Handle)
{
    PFAKE_BUS Bus = (PFAKE_BUS)Context;
    PFAKE_BUS_STREAM Stream = FakeBusStream(Bus, Handle);

    if (Stream == NULL)
        return STATUS_INVALID_HANDLE;
    /* the bus refuses a stream that still runs or still holds its buffer */
    if (Stream->Running || Stream->Buffer)
        return STATUS_INVALID_DEVICE_REQUEST;
    FakeBusLog(Bus, FAKE_BUS_EVENT_FREE, Stream, 0);
    RtlZeroMemory(Stream->Events, sizeof(Stream->Events));
    Stream->Used = FALSE;
    return STATUS_SUCCESS;
}

static NTSTATUS FakeBusPowerState(PVOID Context, DEVICE_POWER_STATE NewPowerState)
{
    PFAKE_BUS Bus = (PFAKE_BUS)Context;

    Bus->PowerStates++;
    Bus->LastPowerState = NewPowerState;
    FakeBusLog(Bus, FAKE_BUS_EVENT_POWER, NULL, (UINT32)NewPowerState);
    return STATUS_SUCCESS;
}

PFAKE_BUS_STREAM
FakeBusOwned(
    PFAKE_BUS Bus,
    HANDLE Handle)
{
    PFAKE_BUS_STREAM Stream = FakeBusStream(Bus, Handle);

    return Stream != NULL && Stream->Used ? Stream : NULL;
}

/* The fake's record of an MDL it handed out, NULL for any other pointer */
static PFAKE_BUS_MDL FakeBusMdlOf(PFAKE_BUS Bus, PMDL Mdl) {
    UINT32 Index;

    for (Index = 0; Index < Bus->MdlCount; Index++)
        if (Bus->Mdls[Index].Mdl == Mdl)
            return &Bus->Mdls[Index];
    return NULL;
}

/* The record of an MDL gone: the last takes its place */
static VOID FakeBusMdlForget(PFAKE_BUS Bus, PFAKE_BUS_MDL Entry) {
    *Entry = Bus->Mdls[--Bus->MdlCount];
    RtlZeroMemory(&Bus->Mdls[Bus->MdlCount], sizeof(Bus->Mdls[0]));
}

static NTSTATUS FakeBusAllocateBuffer(PVOID Context, HANDLE Handle, ULONG NotificationCount, SIZE_T RequestedBufferSize,
    PMDL* BufferMdl, SIZE_T* AllocatedBufferSize, SIZE_T* OffsetFromFirstPage, UCHAR* StreamId, PULONG FifoSize)
{
    PFAKE_BUS Bus = (PFAKE_BUS)Context;
    PFAKE_BUS_STREAM Stream = FakeBusOwned(Bus, Handle);
    PFAKE_BUS_MDL Entry;
    PMDL Mdl;

    if (Stream == NULL)
        return STATUS_INVALID_HANDLE;
    if (Stream->Running || Stream->Buffer)
        return STATUS_INVALID_DEVICE_REQUEST;
    if (Bus->BufferStatus != STATUS_SUCCESS)
        return Bus->BufferStatus;
    if (RequestedBufferSize == 0 || NotificationCount == 0 || NotificationCount > 2)
        return STATUS_INVALID_PARAMETER;
    if (Bus->MdlCount >= FAKE_BUS_MDLS)
        return STATUS_INSUFFICIENT_RESOURCES;
    /* the MDL is opaque to the driver's stream manager, which maps it through its own operations: the fake keeps the pages beside it */
#ifdef FAKE_BUS_KERNEL
    /* the kernel twin's MDL is the bus's own kind, pages from MmAllocatePagesForMdl, so that the manager's direct free of a held buffer (MmFreePagesFromMdl, ExFreePool) is the real one (R5-01) */
    {
        PHYSICAL_ADDRESS Zero, Max;

        Zero.QuadPart = 0;
        Max.QuadPart = MAXULONG64;
        Mdl = MmAllocatePagesForMdl(Zero, Max, Zero, RequestedBufferSize);
    }
    if (Mdl == NULL)
        return STATUS_NO_MEMORY;
#else
    Mdl = (PMDL)FakeBusAllocate(sizeof(MDL));
    if (Mdl == NULL)
        return STATUS_NO_MEMORY;
#endif
#ifdef FAKE_BUS_KERNEL
    Stream->Memory = MmGetSystemAddressForMdlSafe(Mdl, NormalPagePriority);
#else
    Stream->Memory = FakeBusAllocate(RequestedBufferSize);
#endif
    if (Stream->Memory == NULL)
    {
#ifdef FAKE_BUS_KERNEL
        MmFreePagesFromMdl(Mdl);
        ExFreePool(Mdl);
#else
        FakeBusFree(Mdl);
#endif
        return STATUS_NO_MEMORY;
    }
    Stream->Mdl = Mdl;
    Stream->Buffer = TRUE;
    Stream->Bytes = RequestedBufferSize;
    Stream->Offset = 0;
    Stream->State = (UINT8)ResetState;
    Stream->Position = 0;
    Stream->DmaPosition = 0;
    Entry = &Bus->Mdls[Bus->MdlCount++];
    Entry->Mdl = Mdl;
    Entry->Memory = Stream->Memory;
    Entry->Bytes = RequestedBufferSize;
    Entry->Pins = 0;
    Entry->Detached = FALSE;
    *BufferMdl = Mdl;
    *AllocatedBufferSize = RequestedBufferSize;
    *OffsetFromFirstPage = 0;
    *StreamId = Stream->Tag;
    *FifoSize = Stream->FifoSize;
    FakeBusLog(Bus, FAKE_BUS_EVENT_BUFFER, Stream, (UINT32)RequestedBufferSize);
    return STATUS_SUCCESS;
}

/* The pages of a buffer the bus handed out, returned once: the stream's or, for a detached buffer, the pin's free through the manager */
static VOID FakeBusPagesReturn(PFAKE_BUS Bus, PFAKE_BUS_MDL Entry, PFAKE_BUS_STREAM Stream, UINT32 Kind) {
#ifdef FAKE_BUS_KERNEL
    MmFreePagesFromMdl(Entry->Mdl);
    ExFreePool(Entry->Mdl);
#else
    FakeBusFree(Entry->Memory);
    FakeBusFree(Entry->Mdl);
#endif
    FakeBusLog(Bus, Kind, Stream, (UINT32)Entry->Bytes);
    FakeBusMdlForget(Bus, Entry);
}

static NTSTATUS FakeBusFreeBuffer(PVOID Context, HANDLE Handle, PMDL BufferMdl, SIZE_T BufferSize)
{
    PFAKE_BUS Bus = (PFAKE_BUS)Context;
    PFAKE_BUS_STREAM Stream = FakeBusOwned(Bus, Handle);
    PFAKE_BUS_MDL Entry;

    if (Stream == NULL)
        return STATUS_INVALID_HANDLE;
    if (Stream->Running || !Stream->Buffer)
        return STATUS_INVALID_DEVICE_REQUEST;
    Entry = FakeBusMdlOf(Bus, Stream->Mdl);
    /* a free under a pin: the real bus frees the pages under the mapping (R5-01's defect); the fake refuses it, and the log says so */
    if (Entry != NULL && Entry->Pins != 0)
    {
        FakeBusLog(Bus, FAKE_BUS_EVENT_BUFFER_FREE_UNDER_PIN, Stream, (UINT32)Stream->Bytes);
        return STATUS_INVALID_DEVICE_REQUEST;
    }
    if (Entry != NULL)
        FakeBusPagesReturn(Bus, Entry, Stream, FAKE_BUS_EVENT_BUFFER_FREE);
    else
    {
#ifdef FAKE_BUS_KERNEL
        MmFreePagesFromMdl(Stream->Mdl);
        ExFreePool(Stream->Mdl);
#else
        FakeBusFree(Stream->Memory);
        FakeBusFree(Stream->Mdl);
#endif
        FakeBusLog(Bus, FAKE_BUS_EVENT_BUFFER_FREE, Stream, (UINT32)Stream->Bytes);
    }
    Stream->Memory = NULL;
    Stream->Mdl = NULL;
    Stream->Buffer = FALSE;
    Stream->Bytes = 0;
    return STATUS_SUCCESS;
}

/* The buffer taken off the stream and handed to the caller, nothing freed (the bus's DetachDmaBuffer, R5-01): the slot is free for its tag again, the MDL stays the pin's */
static NTSTATUS FakeBusDetachBuffer(PVOID Context, HANDLE Handle, PMDL* BufferMdl)
{
    PFAKE_BUS Bus = (PFAKE_BUS)Context;
    PFAKE_BUS_STREAM Stream = FakeBusOwned(Bus, Handle);
    PFAKE_BUS_MDL Entry;

    if (Stream == NULL)
        return STATUS_INVALID_HANDLE;
    if (Stream->Running || !Stream->Buffer)
        return STATUS_INVALID_DEVICE_REQUEST;
    Entry = FakeBusMdlOf(Bus, Stream->Mdl);
    if (Entry == NULL)
        return STATUS_INVALID_DEVICE_REQUEST;
    *BufferMdl = Stream->Mdl;
    Entry->Detached = TRUE;
    FakeBusLog(Bus, FAKE_BUS_EVENT_BUFFER_DETACH, Stream, (UINT32)Stream->Bytes);
    Stream->Memory = NULL;
    Stream->Mdl = NULL;
    Stream->Buffer = FALSE;
    Stream->Bytes = 0;
    return STATUS_SUCCESS;
}

static NTSTATUS FakeBusSetState(PVOID Context, HANDLE Handle, HDAUDIO_STREAM_STATE StreamState)
{
    PFAKE_BUS Bus = (PFAKE_BUS)Context;
    PFAKE_BUS_STREAM Stream = FakeBusOwned(Bus, Handle);

    if (Stream == NULL)
        return STATUS_INVALID_HANDLE;
    /* the bus sets a stream up from its buffer before it runs */
    if (StreamState == RunState && !Stream->Buffer)
        return STATUS_INVALID_DEVICE_REQUEST;
    if (StreamState == RunState && !Stream->Running)
    {
        Stream->Running = TRUE;
        Stream->Runs++;
    }
    else if ((StreamState == PauseState || StreamState == StopState) && Stream->Running)
    {
        Stream->Running = FALSE;
        Stream->Stops++;
    }
    else if (StreamState == ResetState)
    {
        Stream->Position = 0;
        Stream->DmaPosition = 0;
    }
    Stream->State = (UINT8)StreamState;
    FakeBusLog(Bus, StreamState == RunState ? FAKE_BUS_EVENT_RUN : (StreamState == ResetState ? FAKE_BUS_EVENT_RESET : FAKE_BUS_EVENT_STOP),
        Stream, (UINT32)StreamState);
    return STATUS_SUCCESS;
}

static NTSTATUS FakeBusPositionRegister(PVOID Context, HANDLE Handle, PULONG* Position)
{
    PFAKE_BUS Bus = (PFAKE_BUS)Context;
    PFAKE_BUS_STREAM Stream = FakeBusOwned(Bus, Handle);

    if (Stream == NULL)
        return STATUS_INVALID_HANDLE;
    *Position = &Stream->Position;
    return STATUS_SUCCESS;
}

static NTSTATUS FakeBusDmaPositionRegister(PVOID Context, HANDLE Handle, PULONG* Position)
{
    PFAKE_BUS Bus = (PFAKE_BUS)Context;
    PFAKE_BUS_STREAM Stream = FakeBusOwned(Bus, Handle);

    if (Stream == NULL)
        return STATUS_INVALID_HANDLE;
    *Position = &Stream->DmaPosition;
    return STATUS_SUCCESS;
}

static void FakeBusWallClockRegister(PVOID Context, PULONG* Wallclock)
{
    PFAKE_BUS Bus = (PFAKE_BUS)Context;

    Bus->Stream[0][0].WallClock = 0;
    *Wallclock = &Bus->Stream[0][0].WallClock;
}

static NTSTATUS FakeBusRegister(PVOID Context, HANDLE Handle, PKEVENT NotificationEvent)
{
    PFAKE_BUS Bus = (PFAKE_BUS)Context;
    PFAKE_BUS_STREAM Stream = FakeBusOwned(Bus, Handle);
    UINT32 Index;

    if (Stream == NULL)
        return STATUS_INVALID_HANDLE;
    for (Index = 0; Index < FAKE_BUS_EVENTS; Index++)
    {
        if (Stream->Events[Index] != NULL)
            continue;
        Stream->Events[Index] = NotificationEvent;
        Stream->Registrations++;
        FakeBusLog(Bus, FAKE_BUS_EVENT_REGISTER, Stream, 0);
        return STATUS_SUCCESS;
    }
    return STATUS_INSUFFICIENT_RESOURCES;
}

static NTSTATUS FakeBusUnregister(PVOID Context, HANDLE Handle, PKEVENT NotificationEvent)
{
    PFAKE_BUS Bus = (PFAKE_BUS)Context;
    PFAKE_BUS_STREAM Stream = FakeBusOwned(Bus, Handle);
    UINT32 Index;

    if (Stream == NULL)
        return STATUS_INVALID_HANDLE;
    for (Index = 0; Index < FAKE_BUS_EVENTS; Index++)
    {
        if (Stream->Events[Index] != NotificationEvent)
            continue;
        Stream->Events[Index] = NULL;
        FakeBusLog(Bus, FAKE_BUS_EVENT_UNREGISTER, Stream, 0);
        return STATUS_SUCCESS;
    }
    return STATUS_INVALID_PARAMETER;
}

static UINT32 FakeBusStreamPosition(PVOID Context, HANDLE Handle)
{
    PFAKE_BUS Bus = (PFAKE_BUS)Context;
    PFAKE_BUS_STREAM Stream = FakeBusOwned(Bus, Handle);

    return Stream != NULL ? (UINT32)Stream->Position : 0;
}

static void FakeBusEnableSpib(PVOID Context, HANDLE Handle, UINT32 Value)
{
    PFAKE_BUS Bus = (PFAKE_BUS)Context;
    PFAKE_BUS_STREAM Stream = FakeBusOwned(Bus, Handle);

    if (Stream == NULL)
        return;
    Stream->Spib = TRUE;
    Stream->SpibValue = Value;
    FakeBusLog(Bus, FAKE_BUS_EVENT_SPIB, Stream, Value);
}

static void FakeBusDisableSpib(PVOID Context, HANDLE Handle)
{
    PFAKE_BUS Bus = (PFAKE_BUS)Context;
    PFAKE_BUS_STREAM Stream = FakeBusOwned(Bus, Handle);

    if (Stream == NULL)
        return;
    Stream->Spib = FALSE;
    Stream->SpibValue = 0;
    FakeBusLog(Bus, FAKE_BUS_EVENT_SPIB_OFF, Stream, 0);
}

VOID
FakeBusInit(
    _Out_ PFAKE_BUS Bus,
    ULONG Version,
    BOOLEAN Dma64Bit)
{
    RtlZeroMemory(Bus, sizeof(*Bus));
    Bus->Interface.Size = Version >= 2 ? sizeof(ADSP_BUS_INTERFACE) : (USHORT)RTL_SIZEOF_THROUGH_FIELD(ADSP_BUS_INTERFACE, ReleaseInterruptLock);
    Bus->Interface.Version = (USHORT)Version;
    Bus->Interface.Context = Bus;
    Bus->Interface.InterfaceReference = NoReference;
    Bus->Interface.InterfaceDereference = NoReference;
    Bus->Interface.GetResources = NoResources;
    Bus->Interface.SetDSPPowerState = NoPowerState;
    Bus->Interface.RegisterInterrupt = NoRegisterInterrupt;
    Bus->Interface.UnregisterInterrupt = NoUnregisterInterrupt;
    Bus->Interface.GetRenderStream = NoStream;
    Bus->Interface.GetCaptureStream = NoStream;
    Bus->Interface.FreeStream = NoFreeStream;
    Bus->Interface.PrepareDSP = NoPrepareStream;
    Bus->Interface.CleanupDSP = NoCleanupStream;
    Bus->Interface.TriggerDSP = NoStartStop;
    Bus->Interface.StreamPosition = NoPosition;
    Bus->Interface.DSPEnableSPIB = NoEnableSpib;
    Bus->Interface.DSPDisableSPIB = NoDisableSpib;
    Bus->Interface.AcquireInterruptLock = NoLock;
    Bus->Interface.ReleaseInterruptLock = NoLock;
    Bus->Interface.GetPlatformInfo = FakeBusPlatformInfo;
    Bus->Info.Dma64Bit = Dma64Bit;
    Bus->InfoStatus = STATUS_SUCCESS;
    Bus->RenderStreams = FAKE_BUS_STREAMS;
    Bus->CaptureStreams = FAKE_BUS_STREAMS;
    Bus->RenderStatus = STATUS_SUCCESS;
    Bus->CaptureStatus = STATUS_SUCCESS;
    Bus->BufferStatus = STATUS_SUCCESS;
    Bus->FifoSize = 64;
    /* the stream members of version 2: the pool and its log; the members before them stay the quiet ones of version 1, which no stream case reads */
    Bus->Interface.SetDSPPowerState = FakeBusPowerState;
    Bus->Interface.GetRenderStream = FakeBusGetRenderStream;
    Bus->Interface.GetCaptureStream = FakeBusGetCaptureStream;
    Bus->Interface.FreeStream = FakeBusFreeStream;
    Bus->Interface.StreamPosition = FakeBusStreamPosition;
    Bus->Interface.DSPEnableSPIB = FakeBusEnableSpib;
    Bus->Interface.DSPDisableSPIB = FakeBusDisableSpib;
    Bus->Interface.AllocateDmaBufferWithNotification = FakeBusAllocateBuffer;
    Bus->Interface.FreeDmaBufferWithNotification = FakeBusFreeBuffer;
    Bus->Interface.DetachDmaBuffer = FakeBusDetachBuffer;
    Bus->Interface.SetDmaEngineState = FakeBusSetState;
    Bus->Interface.GetLinkPositionRegister = FakeBusPositionRegister;
    Bus->Interface.GetPositionRegister = FakeBusDmaPositionRegister;
    Bus->Interface.GetWallClockRegister = FakeBusWallClockRegister;
    Bus->Interface.RegisterNotificationEvent = FakeBusRegister;
    Bus->Interface.UnregisterNotificationEvent = FakeBusUnregister;
#ifndef FAKE_BUS_KERNEL
    FakeBusMdlOwner = Bus;
#endif
}

NTSTATUS
FakeBusQueryInterface(
    _In_ PFAKE_BUS Bus,
    _Out_writes_bytes_(Size) PVOID Interface,
    ULONG Size,
    ULONG Version)
{
    if (Bus->Interface.Size > Size || Bus->Interface.Version > Version)
        return STATUS_INVALID_BUFFER_SIZE;
    RtlCopyMemory(Interface, &Bus->Interface, Bus->Interface.Size);
    return STATUS_SUCCESS;
}

VOID
FakeBusTruncate(
    _Inout_ PFAKE_BUS Bus,
    SIZE_T Size)
{
    Bus->Interface.Size = (USHORT)Size;
}

PFAKE_BUS_STREAM
FakeBusStream(
    _In_ const FAKE_BUS* Bus,
    HANDLE Handle)
{
    PFAKE_BUS_STREAM Stream = (PFAKE_BUS_STREAM)Handle;
    UINT32 Direction, Index;

    for (Direction = 0; Direction < 2; Direction++)
        for (Index = 0; Index < FAKE_BUS_STREAMS; Index++)
            if (Stream == (PFAKE_BUS_STREAM)&Bus->Stream[Direction][Index])
                return Stream;
    return NULL;
}

VOID
FakeBusSignal(
    _Inout_ PFAKE_BUS Bus,
    HANDLE Handle)
{
    PFAKE_BUS_STREAM Stream = FakeBusOwned(Bus, Handle);
    UINT32 Index;

    if (Stream == NULL)
        return;
    for (Index = 0; Index < FAKE_BUS_EVENTS; Index++)
    {
        if (Stream->Events[Index] == NULL)
            continue;
#ifdef FAKE_BUS_KERNEL
        KeSetEvent(Stream->Events[Index], IO_NO_INCREMENT, FALSE);
#else
        Stream->Events[Index]->Signaled = TRUE;
        Stream->Events[Index]->Signals++;
#endif
    }
}

#ifndef FAKE_BUS_KERNEL
FAKE_BUS_WAIT_HOOK FakeBusWaitHook;
PVOID FakeBusWaitContext;
LONG FakeBusLockDepth;
KIRQL FakeBusIrql;
LONG FakeBusLockContended;

/* The host's mutex of the stream manager: made as nothing, taken without a wait (KeWaitForSingleObject below), and given back with the depth it was taken to.
 * Its limit (R8-10): a depth counter with no owner. It proves that every take has its release, which a test reads as FakeBusLockDepth == 0 at its end; it would not see a take by the holder itself, a release by another, or a wait that should have blocked, since the host runs one thread and a real mutex's contention is not modelled */
VOID
KeInitializeMutex(
    PKMUTEX Mutex,
    ULONG Level)
{
    (VOID)Mutex;
    (VOID)Level;
}

VOID
KeReleaseMutex(
    PKMUTEX Mutex,
    BOOLEAN Wait)
{
    (VOID)Mutex;
    (VOID)Wait;
    FakeBusLockDepth--;
}

/* The host runs at PASSIVE_LEVEL unless a test raises it */
KIRQL
KeGetCurrentIrql(
    VOID)
{
    return FakeBusIrql;
}

/* The host's event primitives, as the kernel's: an event is cleared by hand, and a wait is the test's model's to answer */
VOID
KeClearEvent(
    PKEVENT Event)
{
    Event->Signaled = FALSE;
}

NTSTATUS
KeWaitForSingleObject(
    PVOID Object,
    KWAIT_REASON WaitReason,
    KPROCESSOR_MODE WaitMode,
    BOOLEAN Alertable,
    PLARGE_INTEGER Timeout)
{
    (VOID)WaitReason;
    (VOID)WaitMode;
    (VOID)Alertable;
    (VOID)Object;
    /* a wait without a timeout is the stream manager's lock, which nothing else holds on a host that runs one thread */
    if (Timeout == NULL)
    {
        FakeBusLockDepth++;
        return STATUS_SUCCESS;
    }
    /* a wait of no time is the try of the teardown poll (N14-05): taken when free, STATUS_TIMEOUT when a test says a transition holds it */
    if (Timeout->QuadPart == 0)
    {
        if (FakeBusLockContended)
            return STATUS_TIMEOUT;
        FakeBusLockDepth++;
        return STATUS_SUCCESS;
    }
    if (FakeBusWaitHook == NULL)
        return STATUS_TIMEOUT;
    return FakeBusWaitHook(FakeBusWaitContext, (PKEVENT)Object, Timeout);
}
#endif

VOID
FakeBusPinOpen(
    _Inout_ PFAKE_BUS Bus,
    PMDL Mdl)
{
    PFAKE_BUS_MDL Entry = FakeBusMdlOf(Bus, Mdl);

    if (Entry != NULL)
        Entry->Pins++;
}

VOID
FakeBusPinClose(
    _Inout_ PFAKE_BUS Bus,
    PMDL Mdl)
{
    PFAKE_BUS_MDL Entry = FakeBusMdlOf(Bus, Mdl);

    if (Entry != NULL && Entry->Pins != 0)
        Entry->Pins--;
}

PVOID
FakeBusMdlMemory(
    _In_ const FAKE_BUS* Bus,
    PMDL Mdl)
{
    const FAKE_BUS_MDL* Entry = FakeBusMdlOf((PFAKE_BUS)Bus, Mdl);

    return Entry != NULL ? Entry->Memory : NULL;
}

UINT32
FakeBusMdlPins(
    _In_ const FAKE_BUS* Bus,
    PMDL Mdl)
{
    const FAKE_BUS_MDL* Entry = FakeBusMdlOf((PFAKE_BUS)Bus, Mdl);

    return Entry != NULL ? Entry->Pins : 0;
}

UINT32
FakeBusMdlCount(
    _In_ const FAKE_BUS* Bus)
{
    return Bus->MdlCount;
}

#ifndef FAKE_BUS_KERNEL
/*
 * The kernel's free of a held buffer's pages, as the manager makes it
 * (intcdsp/stream.c IntcDspHeldRelease: MmFreePagesFromMdl, then ExFreePool of
 * the MDL): on the host the pages are the fake's block behind the MDL, and
 * the MDL its block. The record of the MDL goes with its pages; the MDL's own
 * block goes with ExFreePool, a plain free, so a second free of the same MDL
 * is the sanitizer's to catch.
 */
VOID
MmFreePagesFromMdl(
    PMDL Mdl)
{
    PFAKE_BUS_MDL Entry;

    if (FakeBusMdlOwner == NULL || (Entry = FakeBusMdlOf(FakeBusMdlOwner, Mdl)) == NULL)
        return;
    FakeBusLog(FakeBusMdlOwner, FAKE_BUS_EVENT_BUFFER_HELD_FREE, NULL, (UINT32)Entry->Bytes);
    free(Entry->Memory);
    Entry->Memory = NULL;
}

VOID
ExFreePool(
    PVOID Memory)
{
    if (FakeBusMdlOwner != NULL)
    {
        PFAKE_BUS_MDL Entry = FakeBusMdlOf(FakeBusMdlOwner, (PMDL)Memory);

        if (Entry != NULL)
            FakeBusMdlForget(FakeBusMdlOwner, Entry);
    }
    free(Memory);
}
#endif

UINT32
FakeBusCount(
    _In_ const FAKE_BUS* Bus,
    UINT32 Kind)
{
    return Kind <= FAKE_BUS_EVENT_LAST ? Bus->Counts[Kind] : 0;
}

const FAKE_BUS_EVENT*
FakeBusFind(
    _In_ const FAKE_BUS* Bus,
    UINT32 Kind,
    UINT32 Index)
{
    for (; Index < Bus->LogCount; Index++)
        if (Bus->Log[Index].Kind == Kind)
            return &Bus->Log[Index];
    return NULL;
}
