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

/* The version of GUID_ADSP_BUS_INTERFACE this driver asks for; a bus of version 1 answers with its shorter structure (drivers/wdm/audio/hdaudbus/adsp.h) */
#define ADSP_BUS_INTERFACE_VERSION  2

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
} ADSP_BUS_INTERFACE, *PADSP_BUS_INTERFACE;

/* Whether the interface a query returned binds this driver: the bus reported at least the size of version 1 and none of its members is missing */
BOOLEAN
IntcDspBusComplete(
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
