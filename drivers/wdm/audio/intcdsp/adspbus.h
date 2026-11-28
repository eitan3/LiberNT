/*
 * PROJECT:     LiberNT Intel Smart Sound DSP driver
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     The ADSP bus interface as the DSP child binds it, by version and size, and the DMA address range the controller allows the code-loader buffer
 * COPYRIGHT:   Copyright 2026 LiberNT contributors
 */

#ifndef _DRIVERS_INTCDSP_ADSPBUS_H_
#define _DRIVERS_INTCDSP_ADSPBUS_H_

#include <ntddk.h>
#include <wdm.h>
#include <hdaudio.h>
#include <policy.h>
#include <dspgating.h>

/* The version of GUID_ADSP_BUS_INTERFACE this driver asks for: a bus of version 1 or 2 answers with a shorter structure (drivers/wdm/audio/hdaudbus/adsp.h); version 3 carries DetachDmaBuffer, without which no stream is opened (IntcDspBusStreams), so an older bus is refused by the framework's query (R5-01) */
#define ADSP_BUS_INTERFACE_VERSION  3

typedef struct _PCI_BAR
{
    union
    {
        PVOID Base;
        UINT8* BasePtr;
    } Base;
    ULONG Len;
} PCI_BAR, *PPCI_BAR;

typedef struct _NHLT_INFO
{
    PVOID Nhlt;
    UINT64 NhltSz;
} NHLT_INFO, *PNHLT_INFO;

/* The private bus interface hdaudbus exposes on the CSAUDIO\ADSP child: drivers/wdm/audio/hdaudbus/adsp.h (CoolStar, BSD-3-Clause); the member order is the ABI and its BOOL is windef's int */
typedef _Must_inspect_result_ NTSTATUS(*PGET_ADSP_RESOURCES)(_In_ PVOID Context, _Out_ PCI_BAR* HdaBar, _Out_ PCI_BAR* AdspBar, PVOID* Ppcap, PNHLT_INFO NhltInfo, _Out_ BUS_INTERFACE_STANDARD* PciConfig);
typedef _Must_inspect_result_ NTSTATUS(*PDSP_SET_POWER_STATE)(_In_ PVOID Context, _In_ DEVICE_POWER_STATE NewPowerState);
typedef _Must_inspect_result_ int(*PADSP_INTERRUPT_CALLBACK)(PVOID Context);
typedef _Must_inspect_result_ NTSTATUS(*PREGISTER_ADSP_INTERRUPT)(_In_ PVOID Context, _In_ PADSP_INTERRUPT_CALLBACK Callback, _In_ PVOID CallbackContext);
typedef _Must_inspect_result_ NTSTATUS(*PUNREGISTER_ADSP_INTERRUPT)(_In_ PVOID Context);
typedef _Must_inspect_result_ NTSTATUS(*PGET_STREAM)(_In_ PVOID Context, HDAUDIO_STREAM_FORMAT StreamFormat, PHANDLE Handle, _Out_ UINT8* StreamTag);
typedef _Must_inspect_result_ NTSTATUS(*PFREE_STREAM)(_In_ PVOID Context, _In_ HANDLE Handle);
typedef _Must_inspect_result_ NTSTATUS(*PDSP_PREPARE_STREAM)(_In_ PVOID Context, _In_ HANDLE Handle, _In_ unsigned int ByteSize, _In_ int Frags, _Out_ PVOID* BdlBuf);
typedef _Must_inspect_result_ NTSTATUS(*PDSP_CLEANUP_STREAM)(_In_ PVOID Context, _In_ HANDLE Handle);
typedef _Must_inspect_result_ UINT32(*PDSP_STREAM_POSITION)(_In_ PVOID Context, _In_ HANDLE Handle);
typedef void(*PDSP_START_STOP_STREAM)(_In_ PVOID Context, _In_ HANDLE Handle, int StartStop);
typedef void(*PDSP_ENABLE_SPIB)(_In_ PVOID Context, _In_ HANDLE Handle, UINT32 Value);
typedef void(*PDSP_DISABLE_SPIB)(_In_ PVOID Context, _In_ HANDLE Handle);
/* Added by this driver's request: the bus driver's interrupt spin lock, raised to DIRQL, which its interrupt service routine holds while it calls the DSP callback */
typedef void(*PADSP_INTERRUPT_LOCK)(_In_ PVOID Context);
/* Version 2 of the interface: what the controller offers, as the bus reports it (drivers/wdm/audio/hdaudbus/adsp.h ADSP_PLATFORM_INFO) */
typedef struct _ADSP_PLATFORM_INFO
{
    /* The controller addresses 64 bits of DMA address (hda_registers.h HDA_GCAP_64OK) */
    BOOLEAN Dma64Bit;
} ADSP_PLATFORM_INFO, *PADSP_PLATFORM_INFO;
typedef _Must_inspect_result_ NTSTATUS(*PGET_ADSP_PLATFORM_INFO)(_In_ PVOID Context, _Out_ PADSP_PLATFORM_INFO Info);
/* Appended to version 2 for the topology selection: the machine's DMI strings (drivers/wdm/audio/hdaudbus/policy.h DSP_DMI_STRINGS, which this driver compiles for the decision itself) and the controller's _DSD topology names (sof-tplg.h SOF_TPLG), each at most 31 characters and terminated */
#define ADSP_TOPOLOGY_NAME_BYTES  32
typedef struct _ADSP_TOPOLOGY_SOURCES
{
    BOOLEAN DmiValid;
    DSP_DMI_STRINGS Dmi;
    BOOLEAN DsdValid;
    CHAR SpeakerTplg[ADSP_TOPOLOGY_NAME_BYTES];
    CHAR HpTplg[ADSP_TOPOLOGY_NAME_BYTES];
    CHAR MicTplg[ADSP_TOPOLOGY_NAME_BYTES];
} ADSP_TOPOLOGY_SOURCES, *PADSP_TOPOLOGY_SOURCES;
typedef _Must_inspect_result_ NTSTATUS(*PGET_ADSP_TOPOLOGY_SOURCES)(_In_ PVOID Context, _Out_ PADSP_TOPOLOGY_SOURCES Sources);
/* Appended for this driver's PCM streams (drivers/wdm/audio/hdaudbus/adsp.h): the stream routines the codec interfaces have, through the bus's one code path for every stream. The buffer of a stream: its pages in an MDL, the descriptor list over them (the interrupt on completion at the end of the buffer, or of each of its halves for two notifications) and the stream set up and stopped; the stream runs, pauses or resets by the codec interface's state names (hdaudio.h HDAUDIO_STREAM_STATE); its link position register (LPIB) and the controller's extended DMA-position register of it are handed out for the audio engine to read, as is the 24 MHz wall clock; a period's event is set by the bus's interrupt DPC */
typedef _Must_inspect_result_ NTSTATUS(*PADSP_ALLOCATE_DMA_BUFFER_WITH_NOTIFICATION)(_In_ PVOID Context, _In_ HANDLE Handle, _In_ ULONG NotificationCount, _In_ SIZE_T RequestedBufferSize, _Out_ PMDL* BufferMdl, _Out_ PSIZE_T AllocatedBufferSize, _Out_ PSIZE_T OffsetFromFirstPage, _Out_ PUCHAR StreamId, _Out_ PULONG FifoSize);
typedef _Must_inspect_result_ NTSTATUS(*PADSP_FREE_DMA_BUFFER_WITH_NOTIFICATION)(_In_ PVOID Context, _In_ HANDLE Handle, _In_ PMDL BufferMdl, _In_ SIZE_T BufferSize);
/* Appended for version 3 (R5-01): a stream's buffer taken off the stream and handed back, nothing freed; the manager holds it for the pin that still maps it across a power-down or a fault, and the pin's own free returns the pages and the MDL */
typedef _Must_inspect_result_ NTSTATUS(*PADSP_DETACH_DMA_BUFFER)(_In_ PVOID Context, _In_ HANDLE Handle, _Out_ PMDL* BufferMdl);
typedef _Must_inspect_result_ NTSTATUS(*PADSP_SET_DMA_ENGINE_STATE)(_In_ PVOID Context, _In_ HANDLE Handle, _In_ HDAUDIO_STREAM_STATE StreamState);
typedef _Must_inspect_result_ NTSTATUS(*PADSP_GET_STREAM_POSITION_REGISTER)(_In_ PVOID Context, _In_ HANDLE Handle, _Out_ PULONG* Position);
typedef void(*PADSP_GET_WALL_CLOCK_REGISTER)(_In_ PVOID Context, _Out_ PULONG* Wallclock);
typedef _Must_inspect_result_ NTSTATUS(*PADSP_STREAM_NOTIFICATION_EVENT)(_In_ PVOID Context, _In_ HANDLE Handle, _In_ PKEVENT NotificationEvent);
/* Appended after version 3 (drivers/wdm/audio/hdaudbus/adsp.h, dspgating.h): the platform's clock and power gating of the DSP, two bits of PCI config space that only the bus reaches; FALSE keeps the DSP powered around a boot, TRUE lets the platform gate it again. A bus built before it reports a shorter structure and one that has it may leave it empty: without it no firmware is booted (IntcDspBusPowerGating), since on silicon a gated DSP answers all ones to every read */
typedef _Must_inspect_result_ NTSTATUS(*PADSP_SET_DSP_POWER_GATING)(_In_ PVOID Context, _In_ BOOLEAN AllowGating);
/* Appended after SetDspPowerGating (drivers/wdm/audio/hdaudbus/adsp.h, dspgating.h HDA_DSP_GATING_READBACK): what the bus's last call of it read of the two registers after each of its forms, for the driver's line when a bit did not change; the call itself reads them back, tries the byte form once for a bit that did not take, and answers STATUS_UNSUCCESSFUL when it still did not. A bus built before it reports a shorter structure */
typedef VOID(*PADSP_GET_DSP_POWER_GATING_READBACK)(_In_ PVOID Context, _Out_ PHDA_DSP_GATING_READBACK Readback);

typedef struct _ADSP_BUS_INTERFACE
{
    USHORT Size;
    USHORT Version;
    PVOID Context;
    PINTERFACE_REFERENCE InterfaceReference;
    PINTERFACE_DEREFERENCE InterfaceDereference;
    UINT16 CtlrDevId;
    PGET_ADSP_RESOURCES GetResources;
    PDSP_SET_POWER_STATE SetDSPPowerState;
    PREGISTER_ADSP_INTERRUPT RegisterInterrupt;
    PUNREGISTER_ADSP_INTERRUPT UnregisterInterrupt;
    PGET_STREAM GetRenderStream;
    PGET_STREAM GetCaptureStream;
    PFREE_STREAM FreeStream;
    PDSP_PREPARE_STREAM PrepareDSP;
    PDSP_CLEANUP_STREAM CleanupDSP;
    PDSP_START_STOP_STREAM TriggerDSP;
    PDSP_STREAM_POSITION StreamPosition;
    PDSP_ENABLE_SPIB DSPEnableSPIB;
    PDSP_DISABLE_SPIB DSPDisableSPIB;
    PADSP_INTERRUPT_LOCK AcquireInterruptLock;
    PADSP_INTERRUPT_LOCK ReleaseInterruptLock;
    PGET_ADSP_PLATFORM_INFO GetPlatformInfo;
    PGET_ADSP_TOPOLOGY_SOURCES GetTopologySources;
    PADSP_ALLOCATE_DMA_BUFFER_WITH_NOTIFICATION AllocateDmaBufferWithNotification;
    PADSP_FREE_DMA_BUFFER_WITH_NOTIFICATION FreeDmaBufferWithNotification;
    PADSP_SET_DMA_ENGINE_STATE SetDmaEngineState;
    PADSP_GET_STREAM_POSITION_REGISTER GetLinkPositionRegister;
    PADSP_GET_STREAM_POSITION_REGISTER GetPositionRegister;
    PADSP_GET_WALL_CLOCK_REGISTER GetWallClockRegister;
    PADSP_STREAM_NOTIFICATION_EVENT RegisterNotificationEvent;
    PADSP_STREAM_NOTIFICATION_EVENT UnregisterNotificationEvent;
    PADSP_DETACH_DMA_BUFFER DetachDmaBuffer;
    PADSP_SET_DSP_POWER_GATING SetDspPowerGating;
    PADSP_GET_DSP_POWER_GATING_READBACK GetDspPowerGatingReadback;
} ADSP_BUS_INTERFACE, *PADSP_BUS_INTERFACE;

/* Whether the interface a query returned carries one appended member: a bus built before it reports a shorter structure, and one that has it may leave it empty */
#define INTCDSP_BUS_HAS(Bus, Member) \
    ((Bus)->Size >= RTL_SIZEOF_THROUGH_FIELD(ADSP_BUS_INTERFACE, Member) && (Bus)->Member != NULL)

/* Whether the interface a query returned binds this driver: the bus reported at least the size of version 1 and none of its members is missing */
BOOLEAN
IntcDspBusComplete(
    _In_ const ADSP_BUS_INTERFACE* Bus);

/* Whether the interface carries the members a PCM stream needs: the buffer with its notifications, the stream's state and the period events; FALSE leaves streams refused with a line at the first open */
BOOLEAN
IntcDspBusStreams(
    _In_ const ADSP_BUS_INTERFACE* Bus);

/*
 * Whether the interface carries the member a firmware boot needs: the DSP's power gating,
 * without which the boot is refused with a line and the DSP left alone; FALSE for a bus
 * built before it and for one that has it but leaves it empty
 */
BOOLEAN
IntcDspBusPowerGating(
    _In_ const ADSP_BUS_INTERFACE* Bus);

/* The highest address the code-loader buffer may occupy: the whole range for a controller that addresses 64 bits, the first 4 GiB for any other */
UINT64
IntcDspDmaHighBound(
    _In_ const ADSP_BUS_INTERFACE* Bus);

/* The names a topology selection reads, from a bus that carries the appended member; FALSE for a bus that does not, which leaves the names empty */
BOOLEAN
IntcDspTopologySources(
    _In_ const ADSP_BUS_INTERFACE* Bus,
    _Out_ PADSP_TOPOLOGY_SOURCES Sources);

#endif
