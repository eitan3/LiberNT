/*
 * PROJECT:     LiberNT Intel Smart Sound DSP driver
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Boots the Intel ACE 1.0 audio DSP of Arrow Lake-S/HX with Sound Open Firmware through the hdaudbus ADSP interface
 * COPYRIGHT:   Copyright 2026 LiberNT contributors
 */

#ifndef _DRIVERS_INTCDSP_INTCDSP_H_
#define _DRIVERS_INTCDSP_INTCDSP_H_

#include <ntddk.h>
#include <initguid.h>
#include <wdm.h>
#include <hdaudio.h>
#include <wdf.h>
#include <fwload.h>
#include <nhlt_parse.h>

#define INTCDSP_POOL_TAG 'PcDI'

/* CSAUDIO\ADSP&CTLR_VEN_8086&CTLR_DEV_7F50: the DSP child hdaudbus creates for this controller (drivers/wdm/audio/hdaudbus/buspdo.cpp) */
#define INTCDSP_VEN_INTEL         0x8086
#define INTCDSP_DEV_ARL_S         0x7F50

/* Firmware and the opt-in value: the INF ships the file next to LICENCE.Intel, the value is written into the service key */
#define INTCDSP_FIRMWARE_PATH     L"\\SystemRoot\\System32\\drivers\\intel\\sof\\sof-arl-s.ri"
#define INTCDSP_SETTINGS_PATH     L"\\Registry\\Machine\\System\\CurrentControlSet\\Services\\intcdsp\\Settings"
#define INTCDSP_SETTINGS_VALUE    L"BootDsp"
#define INTCDSP_FIRMWARE_MAX_BYTES (8 * 1024 * 1024)
#define INTCDSP_MODULE_MAX        256

/* The descriptor list of an hdaudbus stream: BDL_SIZE bytes of 16-byte entries (drivers/wdm/audio/hdaudbus/hda_registers.h BDL_SIZE, HDA_MAX_BDL_ENTRIES) */
#define INTCDSP_BDL_ENTRIES       256
#define INTCDSP_BDL_BYTES         4096

/* Processing capabilities header and registers, hdaudbus hda_registers.h HDA_CAP_HDR_* and HDA_REG_PP_PPCTL / PPSTS (Intel HD Audio specification) */
#define INTCDSP_PP_OFFSET_PPCTL   0x04
#define INTCDSP_PP_OFFSET_PPSTS   0x08
#define INTCDSP_CAP_ID_PP         0x3
#define INTCDSP_CAP_ID_SHIFT      16
#define INTCDSP_CAP_ID_MASK       (0xFFF << INTCDSP_CAP_ID_SHIFT)
#define INTCDSP_CAP_VERSION_SHIFT 28
#define INTCDSP_CAP_NEXT_MASK     0xFFFF

/* Interrupt enables of the host IPC: the bit of the register the interrupt pointer names (mtl.h MTL_IRQ_INTEN_L_HOST_IPC_MASK), of HfHIPCIE (MTL_DSP_REG_HfHIPCIE_IE_MASK) and of HFIPCXCTL (MTL_DSP_REG_HFIPCXCTL_BUSY, _DONE); the reply and target registers and their busy and done bits are the fwload platform table's */
#define INTCDSP_INTEN_HOST_IPC    0x00000001UL
#define INTCDSP_HIPCIE_IE         0x00000001UL
#define INTCDSP_IPCCTL_BUSY_INT   0x00000001UL
#define INTCDSP_IPCCTL_DONE_INT   0x00000002UL

/* The lowest BAR4 length the ACE 1.0 register map and both mailbox windows need: window 1 (mtl.h MTL_SRAM_WINDOW_OFFSET(1), 0x8000 bytes per window) plus the downlink mailbox it holds (MTL_DSP_MBOX_DOWNLINK_OFFSET, 0x1000 bytes); studied under LICENSING.md rule 7 */
#define INTCDSP_BAR4_MIN_BYTES    0x189000

/* GUID_ADSP_BUS_INTERFACE of the hdaudbus DSP child, drivers/wdm/audio/hdaudbus/adsp.h (CoolStar, BSD-3-Clause) */
DEFINE_GUID(GUID_ADSP_BUS_INTERFACE,
    0x752a2cae, 0x3455, 0x4d18, 0xa1, 0x84, 0x8b, 0x34, 0xb2, 0x26, 0x32, 0xce);

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
} ADSP_BUS_INTERFACE, *PADSP_BUS_INTERFACE;

typedef struct _INTCDSP_DEVICE
{
    WDFDEVICE Device;
    ADSP_BUS_INTERFACE Bus;
    BOOLEAN BusValid;
    UINT16 VenId;
    UINT16 DevId;
    const FWLOAD_PLATFORM* Platform;
    PCI_BAR HdaBar;
    PCI_BAR AdspBar;
    PVOID Ppcap;
    NHLT_INFO Nhlt;
    ULONG BootDsp;
    UINT32 PpCapHeader;
    UINT32 PpCtl;
    UINT32 PpSts;
    UINT32 Adspcs;
    UINT32 IntPointer;
    UINT32 HfpIe;
    UINT32 IpcCtl;
    UINT32 Fsr;
    UINT32 RomError;
    NHLT_SUMMARY NhltSummary;
    NHLT_PARSE_STATUS NhltStatus;
    PUCHAR Image;
    SIZE_T ImageBytes;
    SOF_RI_IMAGE Ri;
    PSOF_RI_MODULE Modules;
    PUCHAR DmaBuffer;
    SIZE_T DmaBytes;
    FWLOAD_DMA_SEGMENT Segment;
    FWLOAD_CONTEXT* Fw;
    HANDLE Stream;
    UINT8 StreamTag;
    ULONG StreamFragments;
    BOOLEAN StreamPrepared;
    BOOLEAN Booted;
    BOOLEAN IdleHeld;
    FWLOAD_STATUS LastStatus;
    BOOLEAN InterruptArmed;
    BOOLEAN InterruptRegistered;
    ULONG IntEnableOffset;
    volatile LONG Interrupts;
} INTCDSP_DEVICE, *PINTCDSP_DEVICE;

WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(INTCDSP_DEVICE, IntcDspGetData)

#define IntcDspLog(fmt, ...) \
    DbgPrintEx(DPFLTR_IHVAUDIO_ID, DPFLTR_ERROR_LEVEL, "intcdsp.sys: " fmt, ##__VA_ARGS__)

EVT_WDF_DRIVER_DEVICE_ADD IntcDspEvtDeviceAdd;

const FWLOAD_OPS*
IntcDspOpsTable(
    VOID);

VOID
IntcDspInitClock(
    VOID);

/* The performance counter frequency the clock was initialised with, in ticks per second */
UINT64
IntcDspClockFrequency(
    VOID);

PADSP_INTERRUPT_CALLBACK
IntcDspInterruptRoutine(
    VOID);

VOID
IntcDspMaskInterrupts(
    _In_ PINTCDSP_DEVICE Dev);

/* 0xFFFFFFFF when the register the interrupt pointer names lies outside BAR4 */
UINT32
IntcDspInterruptEnableOffset(
    _In_ PINTCDSP_DEVICE Dev);

NTSTATUS
IntcDspReadBootDsp(
    _Out_ PULONG Value);

NTSTATUS
IntcDspLoadFirmware(
    _Inout_ PINTCDSP_DEVICE Dev);

VOID
IntcDspFreeFirmware(
    _Inout_ PINTCDSP_DEVICE Dev);

VOID
IntcDspProbe(
    _Inout_ PINTCDSP_DEVICE Dev);

NTSTATUS
IntcDspBoot(
    _Inout_ PINTCDSP_DEVICE Dev);

VOID
IntcDspPowerDown(
    _Inout_ PINTCDSP_DEVICE Dev);

VOID
IntcDspReleaseStream(
    _Inout_ PINTCDSP_DEVICE Dev);

#endif
