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

/* The lowest BAR4 length the ACE 1.0 register map and both mailbox windows need: window 1 (mtl.h MTL_SRAM_WINDOW_OFFSET(1), 0x8000 bytes per window) plus the downlink mailbox it holds (MTL_DSP_MBOX_DOWNLINK_OFFSET, 0x1000 bytes); studied under LICENSING.md rule 7 */
#define INTCDSP_BAR4_MIN_BYTES    0x189000

/* GUID_ADSP_BUS_INTERFACE of the hdaudbus DSP child, drivers/wdm/audio/hdaudbus/adsp.h (CoolStar, BSD-3-Clause) */
DEFINE_GUID(GUID_ADSP_BUS_INTERFACE,
    0x752a2cae, 0x3455, 0x4d18, 0xa1, 0x84, 0x8b, 0x34, 0xb2, 0x26, 0x32, 0xce);

#include "adspbus.h"

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
    /* the firmware image, read once at start and kept until the hardware is released (R6) */
    FWLOAD_IMAGE Image;
    FWLOAD_STATUS FirmwareStatus;
    NTSTATUS FirmwareReadStatus;
    PUCHAR DmaBuffer;
    SIZE_T DmaBytes;
    FWLOAD_DMA_SEGMENT Segment;
    /* the loader's context is part of the device and re-initialized in place for every boot: nothing is allocated per boot, so no boot can leak or dangle one (R4) */
    FWLOAD_CONTEXT Fw;
    HANDLE Stream;
    UINT8 StreamTag;
    ULONG StreamFragments;
    BOOLEAN StreamPrepared;
    BOOLEAN Booted;
    BOOLEAN IdleHeld;
    FWLOAD_STATUS LastStatus;
    BOOLEAN InterruptArmed;
    BOOLEAN InterruptRegistered;
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

/* One line of the interrupt registers and the power state, read through the loader so that the pointer means what the loader makes of it: for the first boot on silicon, the branch review's list */
VOID
IntcDspLogInterruptState(
    _In_ PINTCDSP_DEVICE Dev,
    _In_ const char* Tag);

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
