#include "driver.h"
#include "nhlt.h"
#include "sof-tplg.h"
#include <wmistr.h>
#include <wmidata.h>

EVT_WDF_DEVICE_PREPARE_HARDWARE Fdo_EvtDevicePrepareHardware;
EVT_WDF_DEVICE_RELEASE_HARDWARE Fdo_EvtDeviceReleaseHardware;
EVT_WDF_DEVICE_D0_ENTRY Fdo_EvtDeviceD0Entry;
EVT_WDF_DEVICE_D0_ENTRY_POST_INTERRUPTS_ENABLED Fdo_EvtDeviceD0EntryPostInterrupts;
EVT_WDF_DEVICE_D0_EXIT Fdo_EvtDeviceD0Exit;
EVT_WDF_DEVICE_SELF_MANAGED_IO_INIT Fdo_EvtDeviceSelfManagedIoInit;
EVT_WDF_INTERRUPT_ENABLE Fdo_EvtInterruptEnable;
EVT_WDF_INTERRUPT_DISABLE Fdo_EvtInterruptDisable;

void CheckHDAGraphicsRegistryKeys(PFDO_CONTEXT fdoCtx);

static
VOID
Fdo_ReportNhlt(
    _In_ PFDO_CONTEXT fdoCtx)
{
    /* the summary is the checked build's report alone: nothing of it is read in a free build */
#if DBG
    const NHLT_SUMMARY* Summary = &fdoCtx->nhltSummary;
    UINT8 Index;

    SklHdAudBusReport(
        "NHLT table (0x%I64x bytes): %s, header length %u, parsed %u bytes, %u of %u endpoints\n",
        fdoCtx->nhltSz,
        NhltParseStatusName(fdoCtx->nhltStatus),
        Summary->HeaderLength,
        Summary->ParsedLength,
        Summary->EndpointsParsed,
        Summary->EndpointsCount);

    for (Index = 0; Index < Summary->EndpointsParsed && Index < NHLT_SUMMARY_ENDPOINTS; Index++) {
        const NHLT_ENDPOINT_SUMMARY* Entry = &Summary->Endpoints[Index];

        SklHdAudBusReport(
            "NHLT endpoint %u: %s vendor %04x device %04x revision %04x subsystem %08x device type %u direction %u instance %u bus %u, %u bytes, device config %u, %u formats, %u devices\n",
            Index,
            NhltLinkTypeName(Entry->LinkType),
            Entry->VendorId,
            Entry->DeviceId,
            Entry->RevisionId,
            Entry->SubsystemId,
            Entry->DeviceType,
            Entry->Direction,
            Entry->InstanceId,
            Entry->VirtualBusId,
            Entry->Length,
            Entry->DeviceConfigSize,
            Entry->FormatsCount,
            Entry->DevicesCount);
    }

    SklHdAudBusReport(
        "NHLT links: HDA %u DSP %u PDM %u SSP %u SLIMBUS %u SDW %u UAOL %u other %u; OED config %u bytes, %u bytes after the endpoints\n",
        Summary->LinkCounts[NHLT_LINKTYPE_HDA],
        Summary->LinkCounts[NHLT_LINKTYPE_DSP],
        Summary->LinkCounts[NHLT_LINKTYPE_PDM],
        Summary->LinkCounts[NHLT_LINKTYPE_SSP],
        Summary->LinkCounts[NHLT_LINKTYPE_SLIMBUS],
        Summary->LinkCounts[NHLT_LINKTYPE_SDW],
        Summary->LinkCounts[NHLT_LINKTYPE_UAOL],
        Summary->OtherLinkCount,
        Summary->OedConfigSize,
        Summary->TrailingBytes);
#endif
}

static
VOID
Fdo_WriteAudioPath(
    _In_ WDFDEVICE Device,
    _In_ DSP_AUDIO_PATH Path)
{
    DECLARE_CONST_UNICODE_STRING(SettingsName, L"Settings");
    DECLARE_CONST_UNICODE_STRING(ValueName, L"DspAudioPath");
    UNICODE_STRING pathWide;
    ANSI_STRING pathAnsi;
    WDFKEY deviceKey;
    WDFKEY settingsKey;
    NTSTATUS status;

    RtlInitAnsiString(&pathAnsi, DspAudioPathName(Path));
    status = RtlAnsiStringToUnicodeString(&pathWide, &pathAnsi, TRUE);
    if (!NT_SUCCESS(status)) {
        goto fail;
    }

    status = WdfDeviceOpenRegistryKey(Device, PLUGPLAY_REGKEY_DEVICE, KEY_SET_VALUE, NULL, &deviceKey);
    if (!NT_SUCCESS(status)) {
        goto freePath;
    }

    status = WdfRegistryCreateKey(deviceKey, &SettingsName, KEY_SET_VALUE, REG_OPTION_NON_VOLATILE,
        NULL, WDF_NO_OBJECT_ATTRIBUTES, &settingsKey);
    if (!NT_SUCCESS(status)) {
        goto closeDevice;
    }

    status = WdfRegistryAssignValue(settingsKey, &ValueName, REG_SZ, pathWide.Length + sizeof(WCHAR), pathWide.Buffer);
    WdfRegistryClose(settingsKey);

closeDevice:
    WdfRegistryClose(deviceKey);

freePath:
    RtlFreeUnicodeString(&pathWide);

fail:
    if (!NT_SUCCESS(status)) {
        SklHdAudBusPrint(DEBUG_LEVEL_ERROR, DBG_PNP,
            "Could not record the audio path under Settings: 0x%08lx\n", status);
    }
}

static
VOID
Fdo_ReadAudioPathOverride(
    _In_ WDFDEVICE Device,
    _Inout_ DSP_AUDIO_PATH_EVIDENCE* Evidence)
{
    DECLARE_CONST_UNICODE_STRING(SettingsName, L"Settings");
    DECLARE_CONST_UNICODE_STRING(ValueName, L"DspAudioPathOverride");
    WCHAR buffer[DSP_DMI_STRING_BYTES];
    char narrow[DSP_DMI_STRING_BYTES];
    WDFKEY deviceKey;
    WDFKEY settingsKey;
    ULONG type = 0, queried = 0, Index;
    NTSTATUS status;

    Evidence->OverrideSet = FALSE;
    status = WdfDeviceOpenRegistryKey(Device, PLUGPLAY_REGKEY_DEVICE, KEY_READ, NULL, &deviceKey);
    if (!NT_SUCCESS(status)) {
        return;
    }

    status = WdfRegistryOpenKey(deviceKey, &SettingsName, KEY_READ, WDF_NO_OBJECT_ATTRIBUTES, &settingsKey);
    WdfRegistryClose(deviceKey);
    if (!NT_SUCCESS(status)) {
        return;
    }

    status = WdfRegistryQueryValue(settingsKey, &ValueName, sizeof(buffer), buffer, &queried, &type);
    WdfRegistryClose(settingsKey);
    if (!NT_SUCCESS(status) || type != REG_SZ || queried < sizeof(WCHAR) || (queried & 1) != 0) {
        return;
    }

    for (Index = 0; Index < queried / sizeof(WCHAR) && Index < DSP_DMI_STRING_BYTES - 1; Index++) {
        if (buffer[Index] == 0) {
            break;
        }
        if (buffer[Index] > 0x7F) {
            return;
        }
        narrow[Index] = (char)buffer[Index];
    }
    narrow[Index] = 0;
    Evidence->OverrideSet = DspAudioPathParseName(narrow, &Evidence->Override);
}

/* The raw SMBIOS table is a data block the kernel itself answers for (MSSmBios_RawSMBiosTables) */
static
DSP_DMI_STATUS
Fdo_ReadDmi(
    _Out_ DSP_DMI_STRINGS* Strings)
{
    PVOID DataBlockObject = NULL;
    PWNODE_ALL_DATA AllData;
    PUCHAR Instance;
    DSP_DMI_STATUS dmiStatus = DmiNoData;
    ULONG BufferSize = 0;
    NTSTATUS status;

    status = IoWMIOpenBlock(&MSSmBios_RawSMBiosTables_GUID, WMIGUID_QUERY, &DataBlockObject);
    if (!NT_SUCCESS(status)) {
        return DmiNoData;
    }

    status = IoWMIQueryAllData(DataBlockObject, &BufferSize, NULL);
    if (status != STATUS_BUFFER_TOO_SMALL && !NT_SUCCESS(status)) {
        IoWMIHandleClose(DataBlockObject);
        return DmiNoData;
    }
    AllData = (PWNODE_ALL_DATA)ExAllocatePoolZero(PagedPool, BufferSize, SKLHDAUDBUS_POOL_TAG);
    if (AllData == NULL) {
        IoWMIHandleClose(DataBlockObject);
        return DmiNoData;
    }

    status = IoWMIQueryAllData(DataBlockObject, &BufferSize, AllData);
    IoWMIHandleClose(DataBlockObject);
    if (!NT_SUCCESS(status)) {
        ExFreePoolWithTag(AllData, SKLHDAUDBUS_POOL_TAG);
        return DmiNoData;
    }

    if (AllData->InstanceCount >= 1 && (AllData->WnodeHeader.Flags & WNODE_FLAG_FIXED_INSTANCE_SIZE) &&
        AllData->FixedInstanceSize >= DSP_DMI_RAW_HEADER_BYTES &&
        (SIZE_T)AllData->DataBlockOffset + AllData->FixedInstanceSize <= BufferSize) {
        Instance = (PUCHAR)AllData + AllData->DataBlockOffset;
        dmiStatus = DmiParseRawSMBiosTables(Instance, AllData->FixedInstanceSize, Strings);
    } else {
        dmiStatus = DmiBadLength;
    }
    ExFreePoolWithTag(AllData, SKLHDAUDBUS_POOL_TAG);
    return dmiStatus;
}

static
VOID
Fdo_ReportAudioPath(
    _In_ WDFDEVICE Device,
    _In_ PFDO_CONTEXT fdoCtx)
{
    DSP_AUDIO_PATH_EVIDENCE evidence;
    DSP_AUDIO_PATH path;
    DSP_DMI_STRINGS dmi;
    DSP_DMI_STATUS dmiStatus;
    UINT16 codecMask = fdoCtx->codecMask;

    if (fdoCtx->UseSGPCCodec && fdoCtx->GraphicsCodecAddress < HDA_MAX_CODECS) {
        codecMask &= (UINT16)~(1UL << fdoCtx->GraphicsCodecAddress);
    }

    RtlZeroMemory(&dmi, sizeof(dmi));
    dmiStatus = Fdo_ReadDmi(&dmi);
    fdoCtx->Dmi = dmi;
    fdoCtx->DmiStatus = dmiStatus;

    RtlZeroMemory(&evidence, sizeof(evidence));
    evidence.CodecMask = codecMask;
    evidence.NonHdaEndpoints = fdoCtx->nhltSummary.NonHdaEndpoints;
    evidence.DspPresent = (fdoCtx->ppcap != NULL);
    evidence.NhltTableValid = (fdoCtx->nhltStatus == NhltParseOk || fdoCtx->nhltStatus == NhltParseTruncated);
    evidence.Dmi = (dmiStatus == DmiOk || dmiStatus == DmiTruncated) ? &dmi : NULL;
    Fdo_ReadAudioPathOverride(Device, &evidence);
    path = DspAudioPathDecide(&evidence);
    fdoCtx->AudioPath = path;

    /* the quirk is looked up for the report alone */
#if DBG
    {
        const DSP_DMI_QUIRK* quirk = DspAudioPathDmiMatch(evidence.Dmi);

        SklHdAudBusReport(
            "DMI %s: SMBIOS %u.%u DMI %u, vendor \"%s\" product \"%s\" version \"%s\" board \"%s\" \"%s\", quirk %s\n",
            DmiStatusName(dmiStatus),
            dmi.SmbiosMajorVersion,
            dmi.SmbiosMinorVersion,
            dmi.DmiRevision,
            dmi.Strings[DSP_DMI_SYS_VENDOR],
            dmi.Strings[DSP_DMI_SYS_PRODUCT],
            dmi.Strings[DSP_DMI_SYS_VERSION],
            dmi.Strings[DSP_DMI_BOARD_VENDOR],
            dmi.Strings[DSP_DMI_BOARD_NAME],
            quirk != NULL ? DspAudioPathName(quirk->Path) : "none");
    }
#endif

    SklHdAudBusReport(
        "audio path %s on PCI %04x:%04x: codec mask 0x%04x, DSP %s, NHLT %s, %u endpoints not on the HDA link%s\n",
        DspAudioPathName(path),
        fdoCtx->venId,
        fdoCtx->devId,
        codecMask,
        evidence.DspPresent ? "present" : "absent",
        NhltParseStatusName(fdoCtx->nhltStatus),
        evidence.NonHdaEndpoints,
        evidence.OverrideSet ? ", forced by Settings\\DspAudioPathOverride" : "");

    Fdo_WriteAudioPath(Device, path);
}

static
VOID
Fdo_LogResources(
    _In_z_ PCSTR ListName,
    _In_ WDFCMRESLIST Resources
)
{
    ULONG resourceCount;

    if (Resources == NULL)
    {
        SklHdAudBusPrint(DEBUG_LEVEL_ERROR, DBG_PNP,
            "%s resource list is NULL\n", ListName);
        return;
    }

    resourceCount = WdfCmResourceListGetCount(Resources);

    SklHdAudBusPrint(DEBUG_LEVEL_INFO, DBG_PNP,
        "%s resource count: %lu\n", ListName, resourceCount);

    for (ULONG i = 0; i < resourceCount; i++)
    {
        PCM_PARTIAL_RESOURCE_DESCRIPTOR descriptor;

        descriptor = WdfCmResourceListGetDescriptor(Resources, i);
        if (descriptor == NULL)
        {
            SklHdAudBusPrint(DEBUG_LEVEL_ERROR, DBG_PNP,
                "%s resource[%lu]: descriptor is NULL\n", ListName, i);
            continue;
        }

        switch (descriptor->Type)
        {
        case CmResourceTypeMemory:
            SklHdAudBusPrint(DEBUG_LEVEL_INFO, DBG_PNP,
                "%s resource[%lu]: memory start=0x%I64x length=0x%lx "
                "flags=0x%hx share=%u\n",
                ListName,
                i,
                descriptor->u.Memory.Start.QuadPart,
                descriptor->u.Memory.Length,
                descriptor->Flags,
                descriptor->ShareDisposition);
            break;

        case CmResourceTypeInterrupt:
            SklHdAudBusPrint(DEBUG_LEVEL_INFO, DBG_PNP,
                "%s resource[%lu]: %s interrupt vector=0x%lx level=0x%lx "
                "affinity=0x%I64x flags=0x%hx share=%u\n",
                ListName,
                i,
                (descriptor->Flags & CM_RESOURCE_INTERRUPT_MESSAGE) ?
                    "message-signaled" : "line-based",
                descriptor->u.Interrupt.Vector,
                descriptor->u.Interrupt.Level,
                (ULONGLONG)descriptor->u.Interrupt.Affinity,
                descriptor->Flags,
                descriptor->ShareDisposition);
            break;

        default:
            SklHdAudBusPrint(DEBUG_LEVEL_INFO, DBG_PNP,
                "%s resource[%lu]: type=%u flags=0x%hx share=%u\n",
                ListName,
                i,
                descriptor->Type,
                descriptor->Flags,
                descriptor->ShareDisposition);
            break;
        }
    }
}

NTSTATUS
NTAPI
HDAGraphicsPowerInterfaceCallback(
    PVOID NotificationStruct,
    PVOID Context
);

NTSTATUS
NTAPI
Fdo_Initialize(
    _In_ PFDO_CONTEXT FdoCtx
);

NTSTATUS
NTAPI
Fdo_Create(
	_Inout_ PWDFDEVICE_INIT DeviceInit
)
{
    WDF_CHILD_LIST_CONFIG      config;
	WDF_OBJECT_ATTRIBUTES attributes;
	WDF_PNPPOWER_EVENT_CALLBACKS pnpPowerCallbacks;
    PFDO_CONTEXT fdoCtx;
    WDFDEVICE wdfDevice;
    NTSTATUS status;

    SklHdAudBusPrint(DEBUG_LEVEL_INFO, DBG_INIT,
        "%s\n", __func__);

    WDF_PNPPOWER_EVENT_CALLBACKS_INIT(&pnpPowerCallbacks);
    pnpPowerCallbacks.EvtDevicePrepareHardware = Fdo_EvtDevicePrepareHardware;
    pnpPowerCallbacks.EvtDeviceReleaseHardware = Fdo_EvtDeviceReleaseHardware;
    pnpPowerCallbacks.EvtDeviceD0Entry = Fdo_EvtDeviceD0Entry;
    pnpPowerCallbacks.EvtDeviceD0EntryPostInterruptsEnabled = Fdo_EvtDeviceD0EntryPostInterrupts;
    pnpPowerCallbacks.EvtDeviceD0Exit = Fdo_EvtDeviceD0Exit;
    pnpPowerCallbacks.EvtDeviceSelfManagedIoInit = Fdo_EvtDeviceSelfManagedIoInit;
    WdfDeviceInitSetPnpPowerEventCallbacks(DeviceInit, &pnpPowerCallbacks);

    //
    // WDF_ DEVICE_LIST_CONFIG describes how the framework should handle
    // dynamic child enumeration on behalf of the driver writer.
    // Since we are a bus driver, we need to specify identification description
    // for our child devices. This description will serve as the identity of our
    // child device. Since the description is opaque to the framework, we
    // have to provide bunch of callbacks to compare, copy, or free
    // any other resources associated with the description.
    //
    WDF_CHILD_LIST_CONFIG_INIT(&config,
        sizeof(PDO_IDENTIFICATION_DESCRIPTION),
        Bus_EvtDeviceListCreatePdo // callback to create a child device.
    );

    //
    // This function pointer will be called when the framework needs to copy a
    // identification description from one location to another.  An implementation
    // of this function is only necessary if the description contains description
    // relative pointer values (like  LIST_ENTRY for instance) .
    // If set to NULL, the framework will use RtlCopyMemory to copy an identification .
    // description. In this sample, it's not required to provide these callbacks.
    // they are added just for illustration.
    //
    config.EvtChildListIdentificationDescriptionDuplicate =
        Bus_EvtChildListIdentificationDescriptionDuplicate;

    //
    // This function pointer will be called when the framework needs to compare
    // two identificaiton descriptions.  If left NULL a call to RtlCompareMemory
    // will be used to compare two identificaiton descriptions.
    //
    config.EvtChildListIdentificationDescriptionCompare =
        Bus_EvtChildListIdentificationDescriptionCompare;
    //
    // This function pointer will be called when the framework needs to free a
    // identification description.  An implementation of this function is only
    // necessary if the description contains dynamically allocated memory
    // (by the driver writer) that needs to be freed. The actual identification
    // description pointer itself will be freed by the framework.
    //
    config.EvtChildListIdentificationDescriptionCleanup =
        Bus_EvtChildListIdentificationDescriptionCleanup;

    //
    // Tell the framework to use the built-in childlist to track the state
    // of the device based on the configuration we just created.
    //
    WdfFdoInitSetDefaultChildListConfig(DeviceInit,
        &config,
        WDF_NO_OBJECT_ATTRIBUTES);

    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&attributes, FDO_CONTEXT);
    status = WdfDeviceCreate(&DeviceInit, &attributes, &wdfDevice);
    if (!NT_SUCCESS(status)) {
        SklHdAudBusPrint(DEBUG_LEVEL_ERROR, DBG_INIT,
            "WdfDriverCreate failed %x\n", status);
        goto Exit;
    }

    /*{
        WDF_DEVICE_POWER_POLICY_IDLE_SETTINGS IdleSettings;

        WDF_DEVICE_POWER_POLICY_IDLE_SETTINGS_INIT(&IdleSettings, IdleCannotWakeFromS0);
        IdleSettings.IdleTimeoutType = SystemManagedIdleTimeoutWithHint;
        IdleSettings.IdleTimeout = 1000;
        IdleSettings.UserControlOfIdleSettings = IdleDoNotAllowUserControl;

        WdfDeviceAssignS0IdleSettings(wdfDevice, &IdleSettings);
    }*/

    {
        WDF_DEVICE_STATE deviceState;
        WDF_DEVICE_STATE_INIT(&deviceState);

        deviceState.NotDisableable = WdfFalse;
        WdfDeviceSetDeviceState(wdfDevice, &deviceState);
    }

    fdoCtx = Fdo_GetContext(wdfDevice);
    fdoCtx->WdfDevice = wdfDevice;
    for (UINT8 addr = 0; addr < HDA_MAX_CODECS; addr++) {
        KeInitializeEvent(&fdoCtx->verbLock[addr], SynchronizationEvent, TRUE);
    }

    status = Fdo_Initialize(fdoCtx);
    if (!NT_SUCCESS(status))
    {
        goto Exit;
    }

    CheckHDAGraphicsRegistryKeys(fdoCtx);

Exit:
    return status;
}

NTSTATUS
NTAPI
Fdo_Initialize(
    _In_ PFDO_CONTEXT FdoCtx
)
{
    NTSTATUS status;
    WDFDEVICE device;
    WDF_INTERRUPT_CONFIG interruptConfig;

    device = FdoCtx->WdfDevice;

    SklHdAudBusPrint(DEBUG_LEVEL_INFO, DBG_INIT,
        "%s\n", __func__);

    //
    // Create an interrupt object for hardware notifications
    //
    WDF_INTERRUPT_CONFIG_INIT(
        &interruptConfig,
        hda_interrupt,
        hda_dpc);
    interruptConfig.EvtInterruptEnable = Fdo_EvtInterruptEnable;
    interruptConfig.EvtInterruptDisable = Fdo_EvtInterruptDisable;

    status = WdfInterruptCreate(
        device,
        &interruptConfig,
        WDF_NO_OBJECT_ATTRIBUTES,
        &FdoCtx->Interrupt);

    if (!NT_SUCCESS(status))
    {
        SklHdAudBusPrint(DEBUG_LEVEL_ERROR, DBG_PNP,
            "Error creating WDF interrupt object - %!STATUS!",
            status);

        return status;
    }
    

    status = WdfWaitLockCreate(WDF_NO_OBJECT_ATTRIBUTES, &FdoCtx->GraphicsDevicesCollectionWaitLock);
    if (!NT_SUCCESS(status))
    {
        SklHdAudBusPrint(DEBUG_LEVEL_ERROR, DBG_PNP,
            "Error creating WDF wait lock - %!STATUS!",
            status);

        return status;
    }
    
    status = WdfCollectionCreate(WDF_NO_OBJECT_ATTRIBUTES, &FdoCtx->GraphicsDevicesCollection);

    if (!NT_SUCCESS(status))
    {
        SklHdAudBusPrint(DEBUG_LEVEL_ERROR, DBG_PNP,
            "Error creating WDF collection - %!STATUS!",
            status);
        
        return status;
    }

    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
Fdo_EvtInterruptEnable(
    _In_ WDFINTERRUPT Interrupt,
    _In_ WDFDEVICE AssociatedDevice
)
{
    PFDO_CONTEXT fdoCtx = Fdo_GetContext(AssociatedDevice);
    UINT32 interruptControl;

    if (Interrupt != fdoCtx->Interrupt ||
        !fdoCtx->InterruptResourcePresent ||
        !fdoCtx->ControllerEnabled ||
        fdoCtx->m_BAR0.Base.Base == NULL)
    {
        SklHdAudBusPrint(DEBUG_LEVEL_ERROR, DBG_PNP,
            "%s: refusing interrupt enable (handle=%p expected=%p resource=%u controller=%u BAR0=%p)\n",
            __func__,
            Interrupt,
            fdoCtx->Interrupt,
            fdoCtx->InterruptResourcePresent,
            fdoCtx->ControllerEnabled,
            fdoCtx->m_BAR0.Base.Base);
        fdoCtx->InterruptConnected = FALSE;
        return STATUS_DEVICE_NOT_READY;
    }

    hda_write32(fdoCtx,
                INTCTL,
                hda_read32(fdoCtx, INTCTL) |
                    HDA_INT_CTRL_EN |
                    HDA_INT_GLOBAL_EN);
    interruptControl = hda_read32(fdoCtx, INTCTL);
    if (interruptControl == 0xffffffff ||
        (interruptControl & (HDA_INT_CTRL_EN | HDA_INT_GLOBAL_EN)) !=
            (HDA_INT_CTRL_EN | HDA_INT_GLOBAL_EN))
    {
        hda_write32(fdoCtx, INTCTL, 0);
        fdoCtx->InterruptConnected = FALSE;
        SklHdAudBusPrint(DEBUG_LEVEL_ERROR, DBG_PNP,
            "%s: INTCTL enable did not latch (read 0x%08lx)\n",
            __func__,
            interruptControl);
        return STATUS_DEVICE_HARDWARE_ERROR;
    }

    fdoCtx->InterruptConnected = TRUE;

    SklHdAudBusPrint(DEBUG_LEVEL_INFO, DBG_PNP,
        "%s: controller interrupt enabled, INTCTL=0x%08lx\n",
        __func__,
        interruptControl);

    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
Fdo_EvtInterruptDisable(
    _In_ WDFINTERRUPT Interrupt,
    _In_ WDFDEVICE AssociatedDevice
)
{
    PFDO_CONTEXT fdoCtx = Fdo_GetContext(AssociatedDevice);

    if (Interrupt != fdoCtx->Interrupt)
    {
        SklHdAudBusPrint(DEBUG_LEVEL_ERROR, DBG_PNP,
            "%s: unexpected interrupt handle %p (expected %p)\n",
            __func__,
            Interrupt,
            fdoCtx->Interrupt);
        return STATUS_INVALID_PARAMETER;
    }

    if (fdoCtx->m_BAR0.Base.Base != NULL)
        hda_write32(fdoCtx, INTCTL, 0);

    fdoCtx->InterruptConnected = FALSE;
    SklHdAudBusPrint(DEBUG_LEVEL_INFO, DBG_PNP,
        "%s: controller interrupt disabled\n", __func__);

    return STATUS_SUCCESS;
}

/* Intel controllers whose DSP lives in PCI BAR4 behind the ACE register block and that Linux drives with IPC4 through the shared hda_dsp_probe: sound/soc/sof/intel/pci-mtl.c (HDA_MTL 0x7e28, HDA_ARL_S 0x7f50, HDA_ARL 0x7728), pci-lnl.c (HDA_LNL_P 0xa828) and pci-ptl.c (HDA_PTL 0xe428, HDA_PTL_H 0xe328); studied under LICENSING.md rule 7 */
static const UINT16 Fdo_AdspControllers[] =
{
    0x7E28, 0x7F50, 0x7728, 0xA828, 0xE428, 0xE328
};

static
BOOLEAN
Fdo_ResourceIsPciBar4(
    _In_ PFDO_CONTEXT fdoCtx,
    _In_ PHYSICAL_ADDRESS Start)
{
    /* A memory BAR decodes as the in-tree PCI bus driver reads it (drivers/bus/pci/pdo.c): bit 0 clear, bits 2:1 the width, the address above bit 3, the next register as the high half of a 64-bit BAR */
    PCI_COMMON_HEADER Header;
    ULONGLONG Base;
    ULONG Bar;

    RtlZeroMemory(&Header, sizeof(Header));
    if (fdoCtx->BusInterface.GetBusData == NULL ||
        fdoCtx->BusInterface.GetBusData(fdoCtx->BusInterface.Context, PCI_WHICHSPACE_CONFIG, &Header, 0,
            sizeof(Header)) != sizeof(Header))
        return FALSE;
    Bar = Header.u.type0.BaseAddresses[4];
    fdoCtx->Bar4Register = Bar;
    if ((Bar & PCI_ADDRESS_IO_SPACE) != 0 || (Bar & PCI_ADDRESS_MEMORY_TYPE_MASK) == PCI_TYPE_20BIT)
        return FALSE;
    Base = Bar & PCI_ADDRESS_MEMORY_ADDRESS_MASK;
    if ((Bar & PCI_ADDRESS_MEMORY_TYPE_MASK) == PCI_TYPE_64BIT)
        Base |= (ULONGLONG)Header.u.type0.BaseAddresses[5] << 32;
    return Base == (ULONGLONG)Start.QuadPart;
}

static
BOOLEAN
Fdo_PpCapSane(
    _In_ PVOID Ppcap)
{
    /* The capability header names the capability and its version, PPCTL holds the global processing enable (hda_registers.h HDA_CAP_HDR_* and HDA_REG_PP_PPCTL, Intel HD Audio specification) */
    UINT32 Header = read32(Ppcap);
    UINT32 PpCtl = read32((PVOID)((UINT8*)Ppcap + HDA_REG_PP_PPCTL));

    return Header != 0 && Header != 0xffffffff && PpCtl != 0xffffffff &&
        ((Header & HDA_CAP_HDR_ID_MASK) >> HDA_CAP_HDR_ID_OFF) == HDA_PP_CAP_ID;
}

/* The DSP child is created only for a controller whose DSP the intcdsp driver knows, whose BAR4 is really the PCI BAR4 resource and whose processing capabilities answer */
static
BOOLEAN
Fdo_WantsAdspChild(
    _In_ PFDO_CONTEXT fdoCtx)
{
    const char* Reason = NULL;
    ULONG Index;

    if (fdoCtx->venId != VEN_INTEL)
        Reason = "not an Intel controller";
    else
    {
        for (Index = 0; Index < RTL_NUMBER_OF(Fdo_AdspControllers) && Fdo_AdspControllers[Index] != fdoCtx->devId; Index++)
            ;
        if (Index == RTL_NUMBER_OF(Fdo_AdspControllers))
            Reason = "not an ACE-era Intel controller";
        else if (!fdoCtx->m_BAR4.Base.Base)
            Reason = "no BAR4 resource";
        else if (!fdoCtx->Bar4IsPciBar4)
            Reason = "the second memory resource is not the PCI BAR4 resource";
        else if (!fdoCtx->ppcap || !fdoCtx->PpCapSane)
            Reason = "no readable processing capabilities";
    }
    if (Reason)
    {
        SklHdAudBusReport(
            "ADSP child not created on PCI %04x:%04x (subclass %02x): %s\n",
            fdoCtx->venId,
            fdoCtx->devId,
            fdoCtx->subClass,
            Reason);
        return FALSE;
    }
    SklHdAudBusReport(
        "ADSP child created on PCI %04x:%04x (subclass %02x): BAR4 register 0x%08x, 0x%lx bytes mapped at %p, processing capabilities at BAR0 +0x%I64x\n",
        fdoCtx->venId,
        fdoCtx->devId,
        fdoCtx->subClass,
        fdoCtx->Bar4Register,
        fdoCtx->m_BAR4.Len,
        fdoCtx->m_BAR4.Base.Base,
        (ULONGLONG)(fdoCtx->ppcap - fdoCtx->m_BAR0.Base.baseptr));
    return TRUE;
}

NTSTATUS
NTAPI
Fdo_EvtDevicePrepareHardware(
    _In_ WDFDEVICE Device,
    _In_ WDFCMRESLIST ResourcesRaw,
    _In_ WDFCMRESLIST ResourcesTranslated
)
{
    PCM_PARTIAL_RESOURCE_DESCRIPTOR bar0Descriptor = NULL;
    PCM_PARTIAL_RESOURCE_DESCRIPTOR bar4Descriptor = NULL;
    /* the raw list's entry of BAR4, as the bus decoded the register: the one Fdo_ResourceIsPciBar4 compares the register with (R1-15) */
    PCM_PARTIAL_RESOURCE_DESCRIPTOR bar4RawDescriptor = NULL;
    WDF_INTERRUPT_INFO interruptInfo;
    NTSTATUS status;
    PFDO_CONTEXT fdoCtx;
    ULONG resourceCount;
    ULONG rawResourceCount;
    ULONG bytesRead;

    fdoCtx = Fdo_GetContext(Device);
    resourceCount = WdfCmResourceListGetCount(ResourcesTranslated);
    rawResourceCount = WdfCmResourceListGetCount(ResourcesRaw);
    fdoCtx->InterruptResourcePresent = FALSE;
    fdoCtx->InterruptConnected = FALSE;

    SklHdAudBusPrint(DEBUG_LEVEL_INFO, DBG_INIT,
        "%s\n", __func__);

    Fdo_LogResources("raw", ResourcesRaw);
    Fdo_LogResources("translated", ResourcesTranslated);

    if (rawResourceCount != resourceCount)
    {
        SklHdAudBusPrint(DEBUG_LEVEL_ERROR, DBG_PNP,
            "Resource-list count mismatch: raw=%lu translated=%lu\n",
            rawResourceCount,
            resourceCount);
    }

    status = WdfFdoQueryForInterface(Device, &GUID_BUS_INTERFACE_STANDARD, (PINTERFACE)&fdoCtx->BusInterface, sizeof(BUS_INTERFACE_STANDARD), 1, NULL);
    if (!NT_SUCCESS(status))
    {
        SklHdAudBusPrint(DEBUG_LEVEL_ERROR, DBG_PNP,
            "Failed to query PCI bus interface: 0x%08lx\n", status);
        return status;
    }

    if (fdoCtx->BusInterface.GetBusData == NULL)
    {
        SklHdAudBusPrint(DEBUG_LEVEL_ERROR, DBG_PNP,
            "PCI bus interface has no GetBusData callback\n");
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    }

    bytesRead = fdoCtx->BusInterface.GetBusData(
        fdoCtx->BusInterface.Context,
        PCI_WHICHSPACE_CONFIG,
        &fdoCtx->venId,
        FIELD_OFFSET(PCI_COMMON_HEADER, VendorID),
        sizeof(fdoCtx->venId));
    if (bytesRead != sizeof(fdoCtx->venId))
    {
        SklHdAudBusPrint(DEBUG_LEVEL_ERROR, DBG_PNP,
            "Failed to read PCI vendor ID: read %lu of %Iu bytes\n",
            bytesRead,
            sizeof(fdoCtx->venId));
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    }

    bytesRead = fdoCtx->BusInterface.GetBusData(
        fdoCtx->BusInterface.Context,
        PCI_WHICHSPACE_CONFIG,
        &fdoCtx->devId,
        FIELD_OFFSET(PCI_COMMON_HEADER, DeviceID),
        sizeof(fdoCtx->devId));
    if (bytesRead != sizeof(fdoCtx->devId))
    {
        SklHdAudBusPrint(DEBUG_LEVEL_ERROR, DBG_PNP,
            "Failed to read PCI device ID for vendor 0x%04x: read %lu of %Iu bytes\n",
            fdoCtx->venId,
            bytesRead,
            sizeof(fdoCtx->devId));
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    }

    bytesRead = fdoCtx->BusInterface.GetBusData(
        fdoCtx->BusInterface.Context,
        PCI_WHICHSPACE_CONFIG,
        &fdoCtx->revId,
        FIELD_OFFSET(PCI_COMMON_HEADER, RevisionID),
        sizeof(fdoCtx->revId));
    if (bytesRead != sizeof(fdoCtx->revId))
    {
        SklHdAudBusPrint(DEBUG_LEVEL_ERROR, DBG_PNP,
            "Failed to read PCI revision for %04x:%04x: read %lu of %Iu bytes\n",
            fdoCtx->venId,
            fdoCtx->devId,
            bytesRead,
            sizeof(fdoCtx->revId));
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    }

    bytesRead = fdoCtx->BusInterface.GetBusData(
        fdoCtx->BusInterface.Context,
        PCI_WHICHSPACE_CONFIG,
        &fdoCtx->subClass,
        FIELD_OFFSET(PCI_COMMON_HEADER, SubClass),
        sizeof(fdoCtx->subClass));
    if (bytesRead != sizeof(fdoCtx->subClass))
    {
        SklHdAudBusPrint(DEBUG_LEVEL_ERROR, DBG_PNP,
            "Failed to read PCI subclass for %04x:%04x: read %lu of %Iu bytes\n",
            fdoCtx->venId,
            fdoCtx->devId,
            bytesRead,
            sizeof(fdoCtx->subClass));
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    }

    SklHdAudBusPrint(DEBUG_LEVEL_INFO, DBG_PNP,
        "PCI HDA controller: vendor=0x%04x device=0x%04x revision=0x%02x\n",
        fdoCtx->venId,
        fdoCtx->devId,
        fdoCtx->revId);

    for (ULONG i = 0; i < resourceCount; i++)
    {
        PCM_PARTIAL_RESOURCE_DESCRIPTOR pDescriptor;

        pDescriptor = WdfCmResourceListGetDescriptor(
            ResourcesTranslated, i);

        if (pDescriptor == NULL)
        {
            SklHdAudBusPrint(DEBUG_LEVEL_ERROR, DBG_PNP,
                "Translated resource[%lu] is NULL\n", i);
            continue;
        }

        switch (pDescriptor->Type)
        {
        case CmResourceTypeMemory:
            if (pDescriptor->u.Memory.Length == 0)
            {
                SklHdAudBusPrint(DEBUG_LEVEL_ERROR, DBG_PNP,
                    "Ignoring zero-length memory resource[%lu]\n", i);
            }
            else if (bar0Descriptor == NULL)
            {
                bar0Descriptor = pDescriptor;
            }
            else if (bar4Descriptor == NULL)
            {
                bar4Descriptor = pDescriptor;
                if (i < rawResourceCount)
                {
                    bar4RawDescriptor = WdfCmResourceListGetDescriptor(ResourcesRaw, i);
                    if (bar4RawDescriptor != NULL && bar4RawDescriptor->Type != CmResourceTypeMemory)
                        bar4RawDescriptor = NULL;
                }
            }
            else
            {
                SklHdAudBusPrint(DEBUG_LEVEL_ERROR, DBG_PNP,
                    "Ignoring unexpected extra memory resource[%lu]\n", i);
            }
            break;

        case CmResourceTypeInterrupt:
            if (pDescriptor->u.Interrupt.Vector == 0)
            {
                SklHdAudBusPrint(DEBUG_LEVEL_ERROR, DBG_PNP,
                    "Ignoring unusable %s interrupt resource[%lu] with vector 0\n",
                    (pDescriptor->Flags & CM_RESOURCE_INTERRUPT_MESSAGE) ?
                        "message-signaled" : "line-based",
                    i);
            }
            else if (!fdoCtx->InterruptResourcePresent)
            {
                fdoCtx->InterruptResourcePresent = TRUE;
            }
            else
            {
                SklHdAudBusPrint(DEBUG_LEVEL_ERROR, DBG_PNP,
                    "Additional interrupt resource[%lu] will not be used by the single WDF interrupt object\n",
                    i);
            }
            break;
        }
    }

    if (bar0Descriptor == NULL)
    {
        SklHdAudBusPrint(DEBUG_LEVEL_ERROR, DBG_PNP,
            "Cannot start PCI %04x:%04x: no usable BAR0 memory resource\n",
            fdoCtx->venId,
            fdoCtx->devId);
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    }

    if (!fdoCtx->InterruptResourcePresent)
    {
        SklHdAudBusPrint(DEBUG_LEVEL_ERROR, DBG_PNP,
            "Cannot start PCI %04x:%04x: PnP assigned no usable IRQ/MSI resource; "
            "check PCI MSI policy, _OSC ownership, and legacy INTx routing\n",
            fdoCtx->venId,
            fdoCtx->devId);
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    }

    WDF_INTERRUPT_INFO_INIT(&interruptInfo);
    WdfInterruptGetInfo(fdoCtx->Interrupt, &interruptInfo);
    SklHdAudBusPrint(DEBUG_LEVEL_INFO, DBG_PNP,
        "WDF interrupt assignment: %s vector=0x%lx irql=0x%x group=%hu "
        "affinity=0x%I64x share=%u\n",
        interruptInfo.MessageSignaled ? "message-signaled" : "line-based",
        interruptInfo.Vector,
        interruptInfo.Irql,
        interruptInfo.Group,
        (ULONGLONG)interruptInfo.TargetProcessorSet,
        interruptInfo.ShareDisposition);

    if (interruptInfo.Vector == 0)
    {
        fdoCtx->InterruptResourcePresent = FALSE;
        SklHdAudBusPrint(DEBUG_LEVEL_ERROR, DBG_PNP,
            "Cannot start PCI %04x:%04x: translated interrupt exists but WDF did not assign it to the interrupt object\n",
            fdoCtx->venId,
            fdoCtx->devId);
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    }

    fdoCtx->m_BAR0.Base.Base = MmMapIoSpace(
        bar0Descriptor->u.Memory.Start,
        bar0Descriptor->u.Memory.Length,
        MmNonCached);
    if (fdoCtx->m_BAR0.Base.Base == NULL)
    {
        SklHdAudBusPrint(DEBUG_LEVEL_ERROR, DBG_PNP,
            "Failed to map BAR0 at 0x%I64x (length 0x%lx)\n",
            bar0Descriptor->u.Memory.Start.QuadPart,
            bar0Descriptor->u.Memory.Length);
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    fdoCtx->m_BAR0.Len = bar0Descriptor->u.Memory.Length;
    SklHdAudBusPrint(DEBUG_LEVEL_INFO, DBG_INIT,
        "Mapped BAR0 0x%I64x (length 0x%lx) to %p\n",
        bar0Descriptor->u.Memory.Start.QuadPart,
        fdoCtx->m_BAR0.Len,
        fdoCtx->m_BAR0.Base.Base);

    if (bar4Descriptor != NULL)
    {
        fdoCtx->m_BAR4.Base.Base = MmMapIoSpace(
            bar4Descriptor->u.Memory.Start,
            bar4Descriptor->u.Memory.Length,
            MmNonCached);
        if (fdoCtx->m_BAR4.Base.Base != NULL)
        {
            fdoCtx->m_BAR4.Len = bar4Descriptor->u.Memory.Length;
            SklHdAudBusPrint(DEBUG_LEVEL_INFO, DBG_INIT,
                "Mapped optional BAR4 0x%I64x (length 0x%lx) to %p\n",
                bar4Descriptor->u.Memory.Start.QuadPart,
                fdoCtx->m_BAR4.Len,
                fdoCtx->m_BAR4.Base.Base);
            /* the register holds the address the bus decoded, which a bridge may have translated on the way to the resource: the comparison is with the raw entry (R1-15) */
            fdoCtx->Bar4IsPciBar4 = Fdo_ResourceIsPciBar4(fdoCtx, bar4RawDescriptor != NULL ? bar4RawDescriptor->u.Memory.Start : bar4Descriptor->u.Memory.Start);
            if (!fdoCtx->Bar4IsPciBar4)
            {
                SklHdAudBusPrint(DEBUG_LEVEL_ERROR, DBG_PNP,
                    "The second memory resource 0x%I64x (raw 0x%I64x) is not the resource of PCI BAR4 (register 0x%08x); the ADSP child is not created\n",
                    bar4Descriptor->u.Memory.Start.QuadPart,
                    bar4RawDescriptor != NULL ? bar4RawDescriptor->u.Memory.Start.QuadPart : bar4Descriptor->u.Memory.Start.QuadPart,
                    fdoCtx->Bar4Register);
            }
        }
        else
        {
            SklHdAudBusPrint(DEBUG_LEVEL_ERROR, DBG_PNP,
                "Could not map optional BAR4 at 0x%I64x (length 0x%lx); continuing without the ADSP mapping\n",
                bar4Descriptor->u.Memory.Start.QuadPart,
                bar4Descriptor->u.Memory.Length);
        }
    }

    //mlcap & lctl (hda_intel_init_chip)
    if (fdoCtx->venId == VEN_INTEL) {
        //read bus capabilities

        unsigned int cur_cap;
        unsigned int offset;
        unsigned int counter = 0;

        offset = hda_read16(fdoCtx, LLCH);

#define HDAC_MAX_CAPS 10

        /* Lets walk the linked capabilities list */
        while (offset) {
            cur_cap = read32(fdoCtx->m_BAR0.Base.baseptr + offset);

            SklHdAudBusPrint(DEBUG_LEVEL_INFO, DBG_INIT,
                "Capability version: 0x%x\n",
                (cur_cap & HDA_CAP_HDR_VER_MASK) >> HDA_CAP_HDR_VER_OFF);

            SklHdAudBusPrint(DEBUG_LEVEL_INFO, DBG_INIT,
                "HDA capability ID: 0x%x\n",
                (cur_cap & HDA_CAP_HDR_ID_MASK) >> HDA_CAP_HDR_ID_OFF);

            if (cur_cap == (unsigned int)-1) {
                SklHdAudBusPrint(DEBUG_LEVEL_INFO, DBG_INIT,
                    "Invalid capability reg read\n");
                break;
            }

            switch ((cur_cap & HDA_CAP_HDR_ID_MASK) >> HDA_CAP_HDR_ID_OFF) {
            case HDA_ML_CAP_ID:
                SklHdAudBusPrint(DEBUG_LEVEL_INFO, DBG_INIT,
                    "Found ML capability\n");
                fdoCtx->mlcap = fdoCtx->m_BAR0.Base.baseptr + offset;
                break;

            case HDA_GTS_CAP_ID:
                SklHdAudBusPrint(DEBUG_LEVEL_INFO, DBG_INIT,
                    "Found GTS capability offset=%x\n", offset);
                break;

            case HDA_PP_CAP_ID:
                /* PP capability found, the Audio DSP is present */
                SklHdAudBusPrint(DEBUG_LEVEL_INFO, DBG_INIT,
                    "Found PP capability offset=%x\n", offset);
                fdoCtx->ppcap = fdoCtx->m_BAR0.Base.baseptr + offset;
                fdoCtx->PpCapSane = Fdo_PpCapSane(fdoCtx->ppcap);
                break;

            case HDA_SPB_CAP_ID:
                /* SPIB capability found, handler function */
                SklHdAudBusPrint(DEBUG_LEVEL_INFO, DBG_INIT,
                    "Found SPB capability\n");
                fdoCtx->spbcap = fdoCtx->m_BAR0.Base.baseptr + offset;
                break;

            case HDA_DRSM_CAP_ID:
                /* DMA resume  capability found, handler function */
                SklHdAudBusPrint(DEBUG_LEVEL_INFO, DBG_INIT,
                    "Found DRSM capability\n");
                break;

            default:
                SklHdAudBusPrint(DEBUG_LEVEL_ERROR, DBG_INIT, "Unknown capability %d\n", cur_cap);
                cur_cap = 0;
                break;
            }

            counter++;

            if (counter > HDAC_MAX_CAPS) {
                SklHdAudBusPrint(DEBUG_LEVEL_ERROR, DBG_INIT, "We exceeded HDAC capabilities!!!\n");
                break;
            }

            /* read the offset of next capability */
            offset = cur_cap & HDA_CAP_HDR_NXT_PTR_MASK;

        }
    }

    status = GetHDACapabilities(fdoCtx);
    if (!NT_SUCCESS(status)) {
        return status;
    }

    fdoCtx->streams = (PHDAC_STREAM)ExAllocatePoolZero(NonPagedPool, sizeof(HDAC_STREAM) * fdoCtx->numStreams, SKLHDAUDBUS_POOL_TAG);
    if (!fdoCtx->streams) {
        return STATUS_NO_MEMORY;
    }

    PHYSICAL_ADDRESS maxAddr;
    maxAddr.QuadPart = fdoCtx->is64BitOK ? MAXULONG64 : MAXULONG32;

    fdoCtx->posbuf = MmAllocateContiguousMemory(PAGE_SIZE, maxAddr);
    if (!fdoCtx->posbuf) {
        return STATUS_NO_MEMORY;
    }

    RtlZeroMemory(fdoCtx->posbuf, PAGE_SIZE);

    fdoCtx->rb = (UINT8 *)MmAllocateContiguousMemory(PAGE_SIZE, maxAddr);
    if (!fdoCtx->rb) {
        return STATUS_NO_MEMORY;
    }

    RtlZeroMemory(fdoCtx->rb, PAGE_SIZE);

    //Init Streams
    {
        UINT8 i;
        UINT8 streamTags[2] = { 0, 0 };

        for (i = 0; i < fdoCtx->numStreams; i++) {
            int isCapture = (i >= fdoCtx->captureIndexOff &&
                i < fdoCtx->captureIndexOff + fdoCtx->captureStreams);
            /* stream tag must be unique throughout
             * the stream direction group,
             * valid values 1...15
             * use separate stream tag
             */
            UINT8 tag = ++streamTags[isCapture];

            {
                UINT64 idx = i;

                PHDAC_STREAM stream = &fdoCtx->streams[i];
                stream->FdoContext = fdoCtx;
                /* offset: SDI0=0x80, SDI1=0xa0, ... SDO3=0x160 */
                stream->sdAddr = fdoCtx->m_BAR0.Base.baseptr + (0x20 * idx + 0x80);
                /* int mask: SDI0=0x01, SDI1=0x02, ... SDO3=0x80 */
                stream->int_sta_mask = 1 << i;
                stream->idx = i;
                if (fdoCtx->venId == VEN_INTEL)
                    stream->streamTag = tag;
                else
                    stream->streamTag = i + 1;

                stream->posbuf = (UINT32 *)(((UINT8 *)fdoCtx->posbuf) + (idx * 8));

                stream->spib_addr = NULL;
                if (fdoCtx->spbcap) {
                    stream->spib_addr = fdoCtx->spbcap + HDA_SPB_BASE + (HDA_SPB_INTERVAL * idx) + HDA_SPB_SPIB;
                }

                stream->bdl = (PHDAC_BDLENTRY)MmAllocateContiguousMemory(BDL_SIZE, maxAddr);
                if (stream->bdl) {
                    RtlZeroMemory(stream->bdl, BDL_SIZE);
                }
            }

            SklHdAudBusPrint(DEBUG_LEVEL_INFO, DBG_INIT,
                "Stream tag (idx %d): %d\n", i, tag);
        }
    }

    fdoCtx->nhlt = NULL;
    fdoCtx->nhltSz = 0;
    fdoCtx->nhltStatus = NhltParseNoTable;

    { //Check NHLT for Intel SST
        NTSTATUS status2 = NHLTCheckSupported(Device);
        if (NT_SUCCESS(status2)) {
            UINT64 nhltAddr;
            UINT64 nhltSz;

            status2 = NHLTQueryTableAddress(Device, &nhltAddr, &nhltSz);

            if (NT_SUCCESS(status2)) {
                PHYSICAL_ADDRESS nhltBaseAddr;
                nhltBaseAddr.QuadPart = nhltAddr;

                if (nhltAddr != 0 && nhltSz != 0) {
                    fdoCtx->nhlt = MmMapIoSpace(nhltBaseAddr, (SIZE_T)nhltSz, MmCached);
                    if (!fdoCtx->nhlt) {
                        SklHdAudBusPrint(DEBUG_LEVEL_ERROR, DBG_PNP,
                            "Could not map the NHLT table at 0x%I64x (length 0x%I64x); continuing without it\n",
                            nhltAddr,
                            nhltSz);
                    }
                    else {
                        fdoCtx->nhltSz = nhltSz;
                        fdoCtx->nhltStatus = NhltParseTable((const UINT8*)fdoCtx->nhlt, nhltSz, &fdoCtx->nhltSummary);
                        Fdo_ReportNhlt(fdoCtx);
                    }
                }
            }
        }
    }

    fdoCtx->sofTplg = NULL;
    fdoCtx->sofTplgSz = 0;

    { //Check topology for Intel SOF
        SOF_TPLG sofTplg = { 0 };
        NTSTATUS status2 = GetSOFTplg(Device, &sofTplg);
        if (NT_SUCCESS(status2) && sofTplg.magic == SOFTPLG_MAGIC) {
            fdoCtx->sofTplg = ExAllocatePoolUninitialized(NonPagedPool, sofTplg.length, SKLHDAUDBUS_POOL_TAG);
            if (!fdoCtx->sofTplg) {
                SklHdAudBusPrint(DEBUG_LEVEL_ERROR, DBG_PNP,
                    "Could not allocate the SOF topology names; continuing without them\n");
            }
            else {
                RtlCopyMemory(fdoCtx->sofTplg, &sofTplg, sofTplg.length);
                fdoCtx->sofTplgSz = sofTplg.length;
            }
        }
    }

    status = STATUS_SUCCESS;

    return status;
}

NTSTATUS
NTAPI
Fdo_EvtDeviceReleaseHardware(
    _In_ WDFDEVICE Device,
    _In_ WDFCMRESLIST ResourcesTranslated
)
{
    PFDO_CONTEXT fdoCtx;

    UNREFERENCED_PARAMETER(ResourcesTranslated);

    fdoCtx = Fdo_GetContext(Device);

    SklHdAudBusPrint(DEBUG_LEVEL_INFO, DBG_INIT,
        "%s\n", __func__);

    if (fdoCtx->GraphicsDevicesCollection) {
        for (ULONG i = 0; i < WdfCollectionGetCount(fdoCtx->GraphicsDevicesCollection); i++) {
            WDFIOTARGET ioTarget = (WDFIOTARGET)WdfCollectionGetItem(fdoCtx->GraphicsDevicesCollection, i);
            PGRAPHICSIOTARGET_CONTEXT ioTargetContext = GraphicsIoTarget_GetContext(ioTarget);

            if (ioTargetContext->graphicsPowerRegisterOutput.DeviceHandle && ioTargetContext->graphicsPowerRegisterOutput.UnregisterCb) {
                NTSTATUS status = ioTargetContext->graphicsPowerRegisterOutput.UnregisterCb(ioTargetContext->graphicsPowerRegisterOutput.DeviceHandle, fdoCtx);
                if (!NT_SUCCESS(status)) {
                    SklHdAudBusPrint(DEBUG_LEVEL_ERROR, DBG_INIT, "Warning: unregister failed with status 0x%x\n", status);
                }
            }
        }
    }

    if (fdoCtx->GraphicsNotificationHandle) {
        IoUnregisterPlugPlayNotification(fdoCtx->GraphicsNotificationHandle);
    }

    if (fdoCtx->nhlt) {
        MmUnmapIoSpace(fdoCtx->nhlt, fdoCtx->nhltSz);
        fdoCtx->nhlt = NULL;
    }

    if (fdoCtx->sofTplg)
    {
        ExFreePoolWithTag(fdoCtx->sofTplg, SKLHDAUDBUS_POOL_TAG);
        fdoCtx->sofTplg = NULL;
        fdoCtx->sofTplgSz = 0;
    }

    if (fdoCtx->posbuf)
    {
        MmFreeContiguousMemory(fdoCtx->posbuf);
        fdoCtx->posbuf = NULL;
    }
    if (fdoCtx->rb)
    {
        MmFreeContiguousMemory(fdoCtx->rb);
        fdoCtx->rb = NULL;
    }

    if (fdoCtx->streams) {
        for (UINT32 i = 0; i < fdoCtx->numStreams; i++) {
            PHDAC_STREAM stream = &fdoCtx->streams[i];
            if (stream->bdl) {
                MmFreeContiguousMemory(stream->bdl);
                stream->bdl = NULL;
            }
        }

        ExFreePoolWithTag(fdoCtx->streams, SKLHDAUDBUS_POOL_TAG);
        fdoCtx->streams = NULL;
    }

    if (fdoCtx->m_BAR0.Base.Base) {
        MmUnmapIoSpace(fdoCtx->m_BAR0.Base.Base, fdoCtx->m_BAR0.Len);
        fdoCtx->m_BAR0.Base.Base = NULL;
        fdoCtx->m_BAR0.Len = 0;
    }
    if (fdoCtx->m_BAR4.Base.Base) {
        MmUnmapIoSpace(fdoCtx->m_BAR4.Base.Base, fdoCtx->m_BAR4.Len);
        fdoCtx->m_BAR4.Base.Base = NULL;
        fdoCtx->m_BAR4.Len = 0;
        fdoCtx->Bar4IsPciBar4 = FALSE;
        fdoCtx->Bar4Register = 0;
    }

    fdoCtx->mlcap = NULL;
    fdoCtx->ppcap = NULL;
    fdoCtx->PpCapSane = FALSE;
    fdoCtx->spbcap = NULL;
    fdoCtx->InterruptConnected = FALSE;
    fdoCtx->InterruptResourcePresent = FALSE;
    fdoCtx->ControllerEnabled = FALSE;
    /* U6: the PCI bus interface was referenced by the query of every preparation (Fdo_EvtDevicePrepareHardware); the reference goes back with the hardware */
    if (fdoCtx->BusInterface.InterfaceDereference) {
        fdoCtx->BusInterface.InterfaceDereference(fdoCtx->BusInterface.Context);
    }
    RtlZeroMemory(&fdoCtx->BusInterface, sizeof(fdoCtx->BusInterface));

    return STATUS_SUCCESS;
}

#define ENABLE_HDA 1

NTSTATUS
NTAPI
Fdo_EvtDeviceD0Entry(
    _In_ WDFDEVICE Device,
    _In_ WDF_POWER_DEVICE_STATE PreviousState
)
{
    UNREFERENCED_PARAMETER(PreviousState);

    NTSTATUS status;
    PFDO_CONTEXT fdoCtx;

    fdoCtx = Fdo_GetContext(Device);

    SklHdAudBusPrint(DEBUG_LEVEL_INFO, DBG_INIT,
        "%s\n", __func__);

    status = STATUS_SUCCESS;
    fdoCtx->InterruptConnected = FALSE;

    if (!fdoCtx->InterruptResourcePresent)
    {
        SklHdAudBusPrint(DEBUG_LEVEL_ERROR, DBG_PNP,
            "%s: refusing to start PCI %04x:%04x without an interrupt resource\n",
            __func__,
            fdoCtx->venId,
            fdoCtx->devId);
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    }

    if (fdoCtx->venId == VEN_INTEL) {
        UINT32 val;
        update_pci_byte(&fdoCtx->BusInterface, HDA_PCIREG_TCSEL, 0x07, 0);
        pci_read_cfg_dword(&fdoCtx->BusInterface, INTEL_HDA_CGCTL, &val);
        val = val & ~INTEL_HDA_CGCTL_MISCBDCGE;
        pci_write_cfg_dword(&fdoCtx->BusInterface, INTEL_HDA_CGCTL, val);
    }
    else if (fdoCtx->venId == VEN_AMD || fdoCtx->venId == VEN_ATI) {
        update_pci_byte(&fdoCtx->BusInterface, ATI_SB450_HDAUDIO_MISC_CNTR2_ADDR, 0x07, ATI_SB450_HDAUDIO_ENABLE_SNOOP);
    }
    else if (fdoCtx->venId == VEN_NVIDIA) {
        update_pci_byte(&fdoCtx->BusInterface, NVIDIA_HDA_TRANSREG_ADDR, 0x0f, NVIDIA_HDA_ENABLE_COHBIT);
        update_pci_byte(&fdoCtx->BusInterface, NVIDIA_HDA_ISTRM_COH, 0x01, NVIDIA_HDA_ENABLE_COHBIT);
        update_pci_byte(&fdoCtx->BusInterface, NVIDIA_HDA_OSTRM_COH, 0x01, NVIDIA_HDA_ENABLE_COHBIT);
    }

    //Reset CORB / RIRB
    RtlZeroMemory(&fdoCtx->corb, sizeof(fdoCtx->corb));
    RtlZeroMemory(&fdoCtx->rirb, sizeof(fdoCtx->rirb));
    fdoCtx->processRirb = FALSE;

    status = StartHDAController(fdoCtx);

    if (fdoCtx->venId == VEN_INTEL) {
        UINT32 val;
        pci_read_cfg_dword(&fdoCtx->BusInterface, INTEL_HDA_CGCTL, &val);
        val = val | INTEL_HDA_CGCTL_MISCBDCGE;
        pci_write_cfg_dword(&fdoCtx->BusInterface, INTEL_HDA_CGCTL, val);

#ifdef __REACTOS__
        if (fdoCtx->mlcap)
            hda_update32(fdoCtx, VS_EM2, HDA_VS_EM2_DUM, HDA_VS_EM2_DUM);
#else
        hda_update32(fdoCtx, VS_EM2, HDA_VS_EM2_DUM, HDA_VS_EM2_DUM);
#endif
    }

    if (!NT_SUCCESS(status)) {
        SklHdAudBusPrint(DEBUG_LEVEL_ERROR, DBG_INIT,
            "%s: failed to start HDA controller: 0x%08lx\n",
            __func__,
            status);
        return status;
    }

    SklHdAudBusPrint(DEBUG_LEVEL_INFO, DBG_INIT,
        "hda bus initialized\n");

    return status;
}

NTSTATUS
NTAPI
Fdo_EvtDeviceD0EntryPostInterrupts(
    _In_ WDFDEVICE Device,
    _In_ WDF_POWER_DEVICE_STATE PreviousState
)
{
    UNREFERENCED_PARAMETER(PreviousState);

    WDF_INTERRUPT_INFO interruptInfo;
    PKINTERRUPT wdmInterrupt;
    NTSTATUS status;
    PFDO_CONTEXT fdoCtx;

    status = STATUS_SUCCESS;
    fdoCtx = Fdo_GetContext(Device);

    WDF_INTERRUPT_INFO_INIT(&interruptInfo);
    WdfInterruptGetInfo(fdoCtx->Interrupt, &interruptInfo);
    wdmInterrupt = WdfInterruptWdmGetInterrupt(fdoCtx->Interrupt);

    SklHdAudBusPrint(DEBUG_LEVEL_INFO, DBG_PNP,
        "%s: %s vector=0x%lx irql=0x%x affinity=0x%I64x WDM=%p enabled=%u\n",
        __func__,
        interruptInfo.MessageSignaled ? "message-signaled" : "line-based",
        interruptInfo.Vector,
        interruptInfo.Irql,
        (ULONGLONG)interruptInfo.TargetProcessorSet,
        wdmInterrupt,
        fdoCtx->InterruptConnected);

    if (!fdoCtx->InterruptResourcePresent ||
        !fdoCtx->InterruptConnected ||
        interruptInfo.Vector == 0 ||
        wdmInterrupt == NULL)
    {
        if (fdoCtx->m_BAR0.Base.Base != NULL)
            hda_write32(fdoCtx, INTCTL, 0);

        fdoCtx->InterruptConnected = FALSE;
        SklHdAudBusPrint(DEBUG_LEVEL_ERROR, DBG_PNP,
            "%s: KMDF did not connect a usable interrupt; codec I/O is disabled to avoid a system crash\n",
            __func__);
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    }

#if ENABLE_HDA
    for (UINT8 addr = 0; addr < HDA_MAX_CODECS; addr++) {
        KeInitializeEvent(&fdoCtx->rirb.xferEvent[addr], NotificationEvent, FALSE);
        if (((fdoCtx->codecMask >> addr) & 0x1) == 0)
            continue;

        if (fdoCtx->UseSGPCCodec && fdoCtx->GraphicsCodecAddress == addr)
            continue;

        UINT32 cmdTmpl = (addr << 28) | (AC_NODE_ROOT << 20) |
            (AC_VERB_PARAMETERS << 8);

        ULONG vendorDevice;
        NTSTATUS commandStatus = RunSingleHDACmd(
            fdoCtx,
            cmdTmpl | AC_PAR_VENDOR_ID,
            &vendorDevice);
        if (!NT_SUCCESS(commandStatus)) { //Some codecs might need a kickstart
            SklHdAudBusPrint(DEBUG_LEVEL_ERROR, DBG_INIT,
                "Codec %u vendor query failed on first attempt: 0x%08lx; retrying\n",
                addr,
                commandStatus);
            //First attempt failed. Retry
            NTSTATUS status2 = RunSingleHDACmd(fdoCtx, cmdTmpl | AC_PAR_VENDOR_ID, &vendorDevice); //If this fails, something is wrong.
            if (!NT_SUCCESS(status2)) {
                SklHdAudBusPrint(DEBUG_LEVEL_ERROR, DBG_INIT,
                    "Warning: failed to wake codec %u: 0x%08lx\n",
                    addr,
                    status2);
            }
        }
    }
#endif

    return status;
}

NTSTATUS
NTAPI
Fdo_EvtDeviceD0Exit(
    _In_ WDFDEVICE Device,
    _In_ WDF_POWER_DEVICE_STATE TargetState
)
{
    UNREFERENCED_PARAMETER(TargetState);

    NTSTATUS status;
    PFDO_CONTEXT fdoCtx;

    fdoCtx = Fdo_GetContext(Device);

    fdoCtx->InterruptConnected = FALSE;
    status = StopHDAController(fdoCtx);

    if (!NT_SUCCESS(status))
    {
        SklHdAudBusPrint(DEBUG_LEVEL_ERROR, DBG_INIT,
            "%s: failed to stop HDA controller: 0x%08lx\n",
            __func__,
            status);
    }

    SklHdAudBusPrint(DEBUG_LEVEL_INFO, DBG_INIT,
        "%s\n", __func__);

    return status;
}

void
NTAPI
Fdo_EnumerateCodec(
    PFDO_CONTEXT fdoCtx,
    UINT8 addr
)
{
    UINT32 cmdTmpl = (addr << 28) | (AC_NODE_ROOT << 20) |
        (AC_VERB_PARAMETERS << 8);
    ULONG funcType = 0, vendorDevice, subsysId, revId, nodeCount;
    NTSTATUS status = RunSingleHDACmd(fdoCtx, cmdTmpl | AC_PAR_VENDOR_ID, &vendorDevice);
    if (NT_SUCCESS(status)) {
        status = RunSingleHDACmd(fdoCtx, cmdTmpl | AC_PAR_REV_ID, &revId);
    }
    if (NT_SUCCESS(status)) {
        status = RunSingleHDACmd(fdoCtx, cmdTmpl | AC_PAR_NODE_COUNT, &nodeCount);
    }
    if (!NT_SUCCESS(status)) {
        SklHdAudBusReport(
            "HDA codec %u does not answer on the link (0x%08lx)\n", addr, status);
        return;
    }

    fdoCtx->numCodecs += 1;

    UINT8 startID = (nodeCount >> 16) & 0xFF;
    nodeCount = (nodeCount & 0x7FFF);

    UINT16 nid = startID;
    for (UINT32 i = 0; i < nodeCount; i++, nid++) {
        UINT32 cmd = (addr << 28) | (nid << 20) |
            (AC_VERB_PARAMETERS << 8) | AC_PAR_FUNCTION_TYPE;
        if (!NT_SUCCESS(RunSingleHDACmd(fdoCtx, cmd, &funcType))) {
            continue;
        }
        if ((funcType & 0xFF) != AC_GRP_AUDIO_FUNCTION &&
            (funcType & 0xFF) != AC_GRP_MODEM_FUNCTION) {
            continue;
        }

        subsysId = 0;
        cmd = (addr << 28) | (nid << 20) |
            (AC_VERB_GET_SUBSYSTEM_ID << 8);
        RunSingleHDACmd(fdoCtx, cmd, &subsysId);

        SklHdAudBusReport(
            "HDA codec %u: vendor %04x device %04x revision %04x subsystem %08x, function group %02x at node %u\n",
            addr,
            (vendorDevice >> 16) & 0xFFFF,
            vendorDevice & 0xFFFF,
            (revId >> 8) & 0xFFFF,
            subsysId,
            funcType & 0xFF,
            nid);

        PDO_IDENTIFICATION_DESCRIPTION description;
        RtlZeroMemory(&description, sizeof(description));
        //
        // Initialize the description with the information about the detected codec.
        //
        WDF_CHILD_IDENTIFICATION_DESCRIPTION_HEADER_INIT(
            &description.Header,
            sizeof(description)
        );

        description.FdoContext = fdoCtx;

        description.CodecIds.CtlrDevId = fdoCtx->devId;
        description.CodecIds.CtlrVenId = fdoCtx->venId;

        description.CodecIds.CodecAddress = addr;
        if (fdoCtx->UseSGPCCodec && addr == fdoCtx->GraphicsCodecAddress)
            description.CodecIds.IsGraphicsCodec = TRUE;
        else
            description.CodecIds.IsGraphicsCodec = FALSE;

        description.CodecIds.FunctionGroupStartNode = (UINT8)nid;

        description.CodecIds.IsDSP = FALSE;

        description.CodecIds.FuncId = funcType & 0xFF;
        description.CodecIds.VenId = (vendorDevice >> 16) & 0xFFFF;
        description.CodecIds.DevId = vendorDevice & 0xFFFF;
        description.CodecIds.SubsysId = subsysId;
        description.CodecIds.RevId = (revId >> 8) & 0xFFFF;

        //
        // Call the framework to add this child to the childlist. This call
        // will internaly call our DescriptionCompare callback to check
        // whether this device is a new device or existing device. If
        // it's a new device, the framework will call DescriptionDuplicate to create
        // a copy of this description in nonpaged pool.
        // The actual creation of the child device will happen when the framework
        // receives QUERY_DEVICE_RELATION request from the PNP manager in
        // response to InvalidateDeviceRelations call made as part of adding
        // a new child.
        //
        if (!NT_SUCCESS(WdfChildListAddOrUpdateChildDescriptionAsPresent(
            WdfFdoGetDefaultChildList(fdoCtx->WdfDevice), &description.Header,
            NULL))) { // AddressDescription
            SklHdAudBusPrint(DEBUG_LEVEL_ERROR, DBG_PNP, "WdfChildListAddOrUpdateChildDescriptionAsPresent failed\n");
        }
    }
}

NTSTATUS
NTAPI
Fdo_EvtDeviceSelfManagedIoInit(
    _In_ WDFDEVICE Device
)
{
    NTSTATUS status = STATUS_SUCCESS;
    PFDO_CONTEXT fdoCtx;

    fdoCtx = Fdo_GetContext(Device);

    WdfChildListBeginScan(WdfFdoGetDefaultChildList(Device));

    fdoCtx->numCodecs = 0;
#if ENABLE_HDA
    for (UINT8 addr = 0; addr < HDA_MAX_CODECS; addr++) {
        fdoCtx->codecs[addr] = NULL;
        if (((fdoCtx->codecMask >> addr) & 0x1) == 0)
            continue;

        if (fdoCtx->UseSGPCCodec && fdoCtx->GraphicsCodecAddress == addr)
            continue;

        Fdo_EnumerateCodec(fdoCtx, addr);
    }

    if (fdoCtx->mlcap) {
        if (!NT_SUCCESS(IoRegisterPlugPlayNotification(
            EventCategoryDeviceInterfaceChange,
            PNPNOTIFY_DEVICE_INTERFACE_INCLUDE_EXISTING_INTERFACES,
            (PVOID)&GUID_DEVINTERFACE_GRAPHICSPOWER,
            WdfDriverWdmGetDriverObject(WdfDeviceGetDriver(fdoCtx->WdfDevice)),
            HDAGraphicsPowerInterfaceCallback,
            (PVOID)fdoCtx,
            &fdoCtx->GraphicsNotificationHandle
        ))) {
            fdoCtx->GraphicsNotificationHandle = NULL;
            SklHdAudBusPrint(DEBUG_LEVEL_ERROR, DBG_PNP, "IoRegisterPlugPlayNotification failed\n");
        }

    }
#endif

    fdoCtx->dspInterruptCallback = NULL;
    if (Fdo_WantsAdspChild(fdoCtx)) { //Populate ADSP if present
        PDO_IDENTIFICATION_DESCRIPTION description;
        // U7: the description is zeroed before its fields are set, as the codec's is; the bytes not set are compared by the child list
        RtlZeroMemory(&description, sizeof(description));
        //
        // Initialize the description with the information about the detected codec.
        //
        WDF_CHILD_IDENTIFICATION_DESCRIPTION_HEADER_INIT(
            &description.Header,
            sizeof(description)
        );

        description.FdoContext = fdoCtx;

        description.CodecIds.CtlrDevId = fdoCtx->devId;
        description.CodecIds.CtlrVenId = fdoCtx->venId;

        description.CodecIds.CodecAddress = 0x10000000;
        description.CodecIds.IsDSP = TRUE;

        //
        // Call the framework to add this child to the childlist. This call
        // will internaly call our DescriptionCompare callback to check
        // whether this device is a new device or existing device. If
        // it's a new device, the framework will call DescriptionDuplicate to create
        // a copy of this description in nonpaged pool.
        // The actual creation of the child device will happen when the framework
        // receives QUERY_DEVICE_RELATION request from the PNP manager in
        // response to InvalidateDeviceRelations call made as part of adding
        // a new child.
        //
        status = WdfChildListAddOrUpdateChildDescriptionAsPresent(
            WdfFdoGetDefaultChildList(Device), &description.Header,
            NULL); // AddressDescription
    }

    WdfChildListEndScan(WdfFdoGetDefaultChildList(Device));

    Fdo_ReportAudioPath(Device, fdoCtx);

    SklHdAudBusPrint(DEBUG_LEVEL_INFO, DBG_INIT,
        "hda scan complete\n");
    return status;
}
