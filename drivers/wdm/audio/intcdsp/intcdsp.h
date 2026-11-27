/*
 * PROJECT:     LiberNT Intel Smart Sound DSP driver
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Boots the Intel ACE 1.0 audio DSP of Meteor Lake and Arrow Lake-S/HX with Sound Open Firmware through the hdaudbus ADSP interface
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
#include <sofipc.h>
#include <nhlt_parse.h>
#include <ipc4model.h>

#define INTCDSP_POOL_TAG 'PcDI'

/* CSAUDIO\ADSP&CTLR_VEN_8086&CTLR_DEV_<id>: the DSP children hdaudbus creates for the controllers (drivers/wdm/audio/hdaudbus/buspdo.cpp); the platform table names the ones this driver boots by the device IDs of fwload.h (FWLOAD_DEV_MTL, FWLOAD_DEV_ARL_S) and the firmware file of each */
#define INTCDSP_VEN_INTEL         0x8086

/* The directory the firmware files ship in (the image puts them there); the file is the record's name, and BootDsp the opt-in value written into the service key */
#define INTCDSP_FIRMWARE_DIR      L"\\SystemRoot\\System32\\drivers\\intel\\sof\\"
#define INTCDSP_FIRMWARE_PATH_BYTES 96
#define INTCDSP_SETTINGS_PATH     L"\\Registry\\Machine\\System\\CurrentControlSet\\Services\\intcdsp\\Settings"
#define INTCDSP_SETTINGS_VALUE    L"BootDsp"
#define INTCDSP_SETTINGS_SELFTEST L"SelfTest"
#define INTCDSP_SETTINGS_USEINTERRUPTS L"UseInterrupts"
#define INTCDSP_SETTINGS_IPCTIMEOUTMS  L"IpcTimeoutMs"
/* The topology file name, REG_SZ: the first source of the topology selection */
#define INTCDSP_SETTINGS_TOPOLOGY L"Topology"
/* The budget of every IPC message class when the value is absent, zero or past a minute: the plan's default */
#define INTCDSP_IPC_TIMEOUT_MS_DEFAULT  500
#define INTCDSP_IPC_TIMEOUT_MS_MAX      60000
#define INTCDSP_SELFTEST_IPC            1
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
#include "ipclock.h"
#include "stream.h"
#include "audioif.h"

/* The loopback self-test after a boot with a topology (SelfTest = 2): a render and a capture stream of 4096 frames each, the plan's VM-acceptance row */
#define INTCDSP_SELFTEST_LOOPBACK      2
#define INTCDSP_SELFTEST_FRAMES        4096
#define INTCDSP_SELFTEST_MAPPINGS      2
#define INTCDSP_STREAM_LINE_BYTES      384

/*
 * The state of the DSP's software for the device (E1, p5-e1-design.md §1):
 * set by the driver alone. Booted stays the hardware's word, and the
 * manager's Ready and Faulted stay the open's (stream.h); this is the
 * device's own word, set after every boot sequence, fault and power
 * transition.
 */
typedef enum _INTCDSP_DSP_STATE
{
    /* probe only, or no topology the firmware can run: an open answers STATUS_DEVICE_NOT_READY as it always did */
    IntcDspNoDsp = 0,
    /* booted, the manager started, no fault */
    IntcDspReady,
    /* one thread is inside the boot sequence at an open, under the manager's mutex */
    IntcDspBooting,
    /* powered down with the topology model kept, after a fault or a D0 exit: the next open boots */
    IntcDspNeedsBoot
} INTCDSP_DSP_STATE;

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
    /* the firmware image, read once at start and kept until the hardware is released (R6); the file is the device's record's, its path beside the driver's directory */
    WCHAR FirmwarePath[INTCDSP_FIRMWARE_PATH_BYTES];
    FWLOAD_IMAGE Image;
    FWLOAD_STATUS FirmwareStatus;
    NTSTATUS FirmwareReadStatus;
    /* the code-loader buffer of the image's payload: contiguous below the controller's DMA bound, made at start with the image and kept for the hardware (R1-03); the segment is its physical range */
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
    /* the state after the last boot sequence, fault or power transition (E1) */
    INTCDSP_DSP_STATE DspState;
    /* the topology the device's boot runs: its model in one nonpaged buffer, kept for the device and rebuilt at the next boot only when none is resident (R6) */
    PUCHAR Topology;
    SIZE_T TopologyBytes;
    PSOFTPLG_IPC4_MODEL TopologyModel;
    CHAR TopologyName[DSP_TOPOLOGY_NAME_BYTES];
    FWLOAD_STATUS LastStatus;
    BOOLEAN InterruptArmed;
    BOOLEAN InterruptRegistered;
    /* the IPC transport after a boot: its settings, its request lock and whether the interrupt callback hands the line to it */
    ULONG SelfTest;
    ULONG UseInterrupts;
    ULONG IpcTimeoutMs;
    INTCDSP_REQUEST_LOCK RequestLock;
    SOFIPC_CONTEXT Ipc;
    BOOLEAN IpcArmed;
    /* the stream manager after a boot with a topology: its state in one nonpaged buffer kept for the device, its operations' kernel side (the mappings of the buffers the self-test maps, its period event) */
    INTCDSP_STREAMS Streams;
    PUCHAR StreamsState;
    SIZE_T StreamsStateBytes;
    INTCDSP_STREAMS_OPS StreamOps;
    INTCDSP_SELFTEST_OPS SelfTestOps;
    /* the poll point of an asynchronous exception (E1c): the DPC the interrupt callback queues at each of its runs, a period's included, and the work item that DPC queues when the uplink's pending bit is set; the work item is the device's child and goes with it */
    KDPC PeriodDpc;
    WDFWORKITEM PollWorkItem;
    /* the audio children: the topology the child list last reported, by the generation of the model (topology.c counts one up whenever the model is built or dropped) and by whether a model was resident (E1: powered or not) */
    ULONG TopologyGeneration;
    ULONG AudioReportedGeneration;
    BOOLEAN AudioReportedResident;
    struct
    {
        PMDL Mdl;
        PVOID Virtual;
    } Mapped[INTCDSP_SELFTEST_MAPPINGS];
    KEVENT SelfTestEvent;
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

/* The DPC of the poll point of an asynchronous exception (E1c, intcdsp.c): the interrupt callback queues it at each of its runs, a period's included */
VOID
IntcDspPeriodDpc(
    _In_ PKDPC Dpc,
    _In_opt_ PVOID DeferredContext,
    _In_opt_ PVOID SystemArgument1,
    _In_opt_ PVOID SystemArgument2);

/* One line of the interrupt registers and the power state, read through the loader so that the pointer means what the loader makes of it: for the first boot on silicon, the branch review's list */
VOID
IntcDspLogInterruptState(
    _In_ PINTCDSP_DEVICE Dev,
    _In_ const char* Tag);

NTSTATUS
IntcDspReadSetting(
    _In_ PCWSTR Name,
    _Out_ PULONG Value);

/* The operations the transport adds to the loader's table: the downlink mailbox and the request lock */
const SOFIPC_OPS*
IntcDspIpcOpsTable(
    VOID);

/* The firmware file of the device's record: its path under INTCDSP_FIRMWARE_DIR, empty for a controller without a record */
VOID
IntcDspFirmwarePath(
    _Inout_ PINTCDSP_DEVICE Dev);

NTSTATUS
IntcDspLoadFirmware(
    _Inout_ PINTCDSP_DEVICE Dev);

VOID
IntcDspFreeFirmware(
    _Inout_ PINTCDSP_DEVICE Dev);

/* A whole file of at most MaxBytes into paged pool */
NTSTATUS
IntcDspReadFile(
    _In_ PCWSTR Path,
    _Out_ PUCHAR* Data,
    _Out_ PSIZE_T Bytes,
    ULONGLONG MaxBytes);

/* The topology of a booted device, selected, read and kept as its model; FALSE powers the DSP down with no endpoint */
BOOLEAN
IntcDspTopologyLoad(
    _Inout_ PINTCDSP_DEVICE Dev);

VOID
IntcDspProbe(
    _Inout_ PINTCDSP_DEVICE Dev);

NTSTATUS
IntcDspBoot(
    _Inout_ PINTCDSP_DEVICE Dev,
    BOOLEAN AtOpen);

/*
 * The boot sequence the D0 entry, a boot at an open and a recovery share
 * (E1): the boot, then the topology (resident after the first boot of the
 * device) or a power-down, then the stream manager. The self-tests run at a
 * D0 entry alone. The status is the manager's after it all and
 * Dev->DspState says which of the four the device is in.
 */
NTSTATUS
IntcDspBootSequence(
    _Inout_ PINTCDSP_DEVICE Dev,
    BOOLEAN AtOpen);

/* The stream manager of a booted device with a topology, and the loopback self-test (SelfTest = 2) over it, at a D0 entry alone */
NTSTATUS
IntcDspStreamsStart(
    _Inout_ PINTCDSP_DEVICE Dev,
    BOOLEAN AtOpen);

/* The audio children of the device (audioif.c): the child list at its creation, and the report of one child per PCM of the topology after the streams start, none without one */
VOID
IntcDspAudioInitDevice(
    _In_ PWDFDEVICE_INIT DeviceInit);

VOID
IntcDspAudioUpdate(
    _Inout_ PINTCDSP_DEVICE Dev);

VOID
IntcDspPowerDown(
    _Inout_ PINTCDSP_DEVICE Dev);

VOID
IntcDspReleaseStream(
    _Inout_ PINTCDSP_DEVICE Dev);

#endif
