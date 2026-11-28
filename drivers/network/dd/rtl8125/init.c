/*
 * PROJECT:     LiberNT Realtek RTL8125/RTL8126 Ethernet Driver
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Driver entry, adapter initialization, halt, pause and restart
 * COPYRIGHT:   Copyright 2026 The LiberNT contributors
 */

#include "rtl8125.h"

static NDIS_HANDLE Rt25DriverHandle;

static
NDIS_STATUS
Rt25SetRegistrationAttributes(
    _In_ PRT25_ADAPTER Adapter)
{
    NDIS_MINIPORT_ADAPTER_REGISTRATION_ATTRIBUTES Attributes;

    NdisZeroMemory(&Attributes, sizeof(Attributes));
    Attributes.Header.Type = NDIS_OBJECT_TYPE_MINIPORT_ADAPTER_REGISTRATION_ATTRIBUTES;
    Attributes.Header.Revision = NDIS_MINIPORT_ADAPTER_REGISTRATION_ATTRIBUTES_REVISION_1;
    Attributes.Header.Size = NDIS_SIZEOF_MINIPORT_ADAPTER_REGISTRATION_ATTRIBUTES_REVISION_1;
    Attributes.MiniportAdapterContext = Adapter;
    Attributes.AttributeFlags = NDIS_MINIPORT_ATTRIBUTES_HARDWARE_DEVICE |
                                NDIS_MINIPORT_ATTRIBUTES_BUS_MASTER |
                                NDIS_MINIPORT_ATTRIBUTES_SURPRISE_REMOVE_OK;
    Attributes.CheckForHangTimeInSeconds = 2;
    Attributes.InterfaceType = NdisInterfacePci;

    return NdisMSetMiniportAttributes(Adapter->MiniportHandle, (PNDIS_MINIPORT_ADAPTER_ATTRIBUTES)&Attributes);
}

static
NDIS_STATUS
Rt25SetGeneralAttributes(
    _In_ PRT25_ADAPTER Adapter)
{
    NDIS_MINIPORT_ADAPTER_GENERAL_ATTRIBUTES Attributes;
    ULONG64 MaxSpeed = (ULONG64)Adapter->MaxSpeedMbps * 1000000;

    NdisZeroMemory(&Attributes, sizeof(Attributes));
    Attributes.Header.Type = NDIS_OBJECT_TYPE_MINIPORT_ADAPTER_GENERAL_ATTRIBUTES;
    Attributes.Header.Revision = NDIS_MINIPORT_ADAPTER_GENERAL_ATTRIBUTES_REVISION_1;
    Attributes.Header.Size = NDIS_SIZEOF_MINIPORT_ADAPTER_GENERAL_ATTRIBUTES_REVISION_1;
    Attributes.MediaType = NdisMedium802_3;
    Attributes.PhysicalMediumType = NdisPhysicalMedium802_3;
    Attributes.MtuSize = RT25_MTU;
    Attributes.MaxXmitLinkSpeed = MaxSpeed;
    Attributes.MaxRcvLinkSpeed = MaxSpeed;
    Attributes.XmitLinkSpeed = Adapter->LinkSpeed;
    Attributes.RcvLinkSpeed = Adapter->LinkSpeed;
    Attributes.MediaConnectState = Adapter->MediaConnectState;
    Attributes.MediaDuplexState = Adapter->MediaDuplexState;
    Attributes.LookaheadSize = RT25_MTU;
    Attributes.MacOptions = RT25_MAC_OPTIONS;
    Attributes.SupportedPacketFilters = RT25_SUPPORTED_FILTERS;
    Attributes.MaxMulticastListSize = RT25_MAX_MULTICAST;
    Attributes.MacAddressLength = ETH_LENGTH_OF_ADDRESS;
    NdisMoveMemory(Attributes.PermanentMacAddress, Adapter->PermanentMacAddress, ETH_LENGTH_OF_ADDRESS);
    NdisMoveMemory(Attributes.CurrentMacAddress, Adapter->CurrentMacAddress, ETH_LENGTH_OF_ADDRESS);
    Attributes.AccessType = NET_IF_ACCESS_BROADCAST;
    Attributes.DirectionType = NET_IF_DIRECTION_SENDRECEIVE;
    Attributes.ConnectionType = NET_IF_CONNECTION_DEDICATED;
    Attributes.IfType = IF_TYPE_ETHERNET_CSMACD;
    Attributes.IfConnectorPresent = TRUE;
    Attributes.SupportedPauseFunctions = NdisPauseFunctionsUnsupported;
    Attributes.AutoNegotiationFlags = NDIS_LINK_STATE_XMIT_LINK_SPEED_AUTO_NEGOTIATED |
                                      NDIS_LINK_STATE_RCV_LINK_SPEED_AUTO_NEGOTIATED |
                                      NDIS_LINK_STATE_DUPLEX_AUTO_NEGOTIATED;
    Attributes.SupportedStatistics = NDIS_STATISTICS_XMIT_OK_SUPPORTED |
                                     NDIS_STATISTICS_RCV_OK_SUPPORTED |
                                     NDIS_STATISTICS_XMIT_ERROR_SUPPORTED |
                                     NDIS_STATISTICS_RCV_ERROR_SUPPORTED |
                                     NDIS_STATISTICS_RCV_NO_BUFFER_SUPPORTED |
                                     NDIS_STATISTICS_BYTES_RCV_SUPPORTED |
                                     NDIS_STATISTICS_BYTES_XMIT_SUPPORTED;
    Attributes.SupportedOidList = (PNDIS_OID)Rt25SupportedOids;
    Attributes.SupportedOidListLength = Rt25SupportedOidsSize;
    /* TODO: wake-on-LAN; without PM capabilities NDIS halts the adapter across sleep. */
    Attributes.PowerManagementCapabilities = NULL;

    return NdisMSetMiniportAttributes(Adapter->MiniportHandle, (PNDIS_MINIPORT_ADAPTER_ATTRIBUTES)&Attributes);
}

static
NDIS_STATUS
Rt25RegisterDma(
    _In_ PRT25_ADAPTER Adapter)
{
    NDIS_SG_DMA_DESCRIPTION Description;

    /* NdisMAllocateSharedMemory needs a DMA registration; no scatter-gather lists are built. */
    NdisZeroMemory(&Description, sizeof(Description));
    Description.Header.Type = NDIS_OBJECT_TYPE_SG_DMA_DESCRIPTION;
    Description.Header.Revision = NDIS_SG_DMA_DESCRIPTION_REVISION_1;
    Description.Header.Size = NDIS_SIZEOF_SG_DMA_DESCRIPTION_REVISION_1;
    Description.Flags = NDIS_SG_DMA_64_BIT_ADDRESS;
    Description.MaximumPhysicalMapping = RT25_BUFFER_SIZE;
    Description.ProcessSGListHandler = NULL;
    Description.SharedMemAllocateCompleteHandler = NULL;

    return NdisMRegisterScatterGatherDma(Adapter->MiniportHandle, &Description, &Adapter->DmaHandle);
}

static
ULONG
Rt25MemoryLength(
    _In_ PCM_PARTIAL_RESOURCE_DESCRIPTOR Descriptor)
{
    ULONG64 Length;

    if (Descriptor->Type != CmResourceTypeMemoryLarge)
        return Descriptor->u.Memory.Length;

    if (Descriptor->Flags & CM_RESOURCE_MEMORY_LARGE_40)
        Length = (ULONG64)Descriptor->u.Memory40.Length40 << 8;
    else if (Descriptor->Flags & CM_RESOURCE_MEMORY_LARGE_48)
        Length = (ULONG64)Descriptor->u.Memory48.Length48 << 16;
    else if (Descriptor->Flags & CM_RESOURCE_MEMORY_LARGE_64)
        Length = (ULONG64)Descriptor->u.Memory64.Length64 << 32;
    else
        Length = Descriptor->u.Memory.Length;

    return (ULONG)min(Length, 0x10000);
}

static
NDIS_STATUS
Rt25MapResources(
    _In_ PRT25_ADAPTER Adapter,
    _In_opt_ PNDIS_RESOURCE_LIST Resources)
{
    PCM_PARTIAL_RESOURCE_DESCRIPTOR Descriptor;
    NDIS_STATUS Status;
    ULONG i, Length;

    if (!Resources)
        return NDIS_STATUS_RESOURCES;

    for (i = 0; i < Resources->Count; i++)
    {
        Descriptor = &Resources->PartialDescriptors[i];
        switch (Descriptor->Type)
        {
            case CmResourceTypeMemory:
            case CmResourceTypeMemoryLarge:
                /* BAR0 is I/O space; the first memory BAR is the 64 KiB register window. */
                Length = Rt25MemoryLength(Descriptor);
                if (!Adapter->RegisterLength && Length >= 0x100)
                {
                    Adapter->RegisterPhysical = Descriptor->u.Memory.Start;
                    Adapter->RegisterLength = Length;
                }
                break;

            case CmResourceTypeInterrupt:
                Adapter->HasInterrupt = TRUE;
                break;

            default:
                break;
        }
    }

    if (!Adapter->RegisterLength || !Adapter->HasInterrupt)
    {
        DPRINT1("RTL8125: missing resources (memory %lu, interrupt %u)\n",
                Adapter->RegisterLength, Adapter->HasInterrupt);
        return NDIS_STATUS_RESOURCES;
    }

    Status = NdisMMapIoSpace((PVOID *)&Adapter->RegisterBase, Adapter->MiniportHandle,
                             Adapter->RegisterPhysical, Adapter->RegisterLength);
    if (Status != NDIS_STATUS_SUCCESS)
    {
        DPRINT1("RTL8125: NdisMMapIoSpace failed 0x%08lx\n", Status);
        Adapter->RegisterBase = NULL;
    }

    return Status;
}

static
NDIS_STATUS
Rt25PrepareBus(
    _In_ PRT25_ADAPTER Adapter)
{
    USHORT VendorId = 0, Command = 0;

    if (NdisMGetBusData(Adapter->MiniportHandle, PCI_WHICHSPACE_CONFIG,
                        FIELD_OFFSET(PCI_COMMON_CONFIG, VendorID), &VendorId, sizeof(VendorId)) != sizeof(VendorId) ||
        NdisMGetBusData(Adapter->MiniportHandle, PCI_WHICHSPACE_CONFIG,
                        FIELD_OFFSET(PCI_COMMON_CONFIG, DeviceID), &Adapter->DeviceId,
                        sizeof(Adapter->DeviceId)) != sizeof(Adapter->DeviceId) ||
        NdisMGetBusData(Adapter->MiniportHandle, PCI_WHICHSPACE_CONFIG,
                        FIELD_OFFSET(PCI_COMMON_CONFIG, Command), &Command, sizeof(Command)) != sizeof(Command))
    {
        DPRINT1("RTL8125: PCI configuration space is not readable\n");
        return NDIS_STATUS_ADAPTER_NOT_FOUND;
    }

    if (VendorId != 0x10EC)
        return NDIS_STATUS_ADAPTER_NOT_FOUND;

    if ((Command & (PCI_ENABLE_MEMORY_SPACE | PCI_ENABLE_BUS_MASTER)) !=
        (PCI_ENABLE_MEMORY_SPACE | PCI_ENABLE_BUS_MASTER))
    {
        Command |= PCI_ENABLE_MEMORY_SPACE | PCI_ENABLE_BUS_MASTER;
        NdisMSetBusData(Adapter->MiniportHandle, PCI_WHICHSPACE_CONFIG,
                        FIELD_OFFSET(PCI_COMMON_CONFIG, Command), &Command, sizeof(Command));
    }

    return NDIS_STATUS_SUCCESS;
}

static
VOID
Rt25ReadConfiguration(
    _In_ PRT25_ADAPTER Adapter)
{
    NDIS_CONFIGURATION_OBJECT Object;
    NDIS_HANDLE Configuration = NULL;
    NDIS_STATUS Status;
    PUCHAR Address = NULL;
    UINT Length = 0;

    NdisMoveMemory(Adapter->CurrentMacAddress, Adapter->PermanentMacAddress, ETH_LENGTH_OF_ADDRESS);

    NdisZeroMemory(&Object, sizeof(Object));
    Object.Header.Type = NDIS_OBJECT_TYPE_CONFIGURATION_OBJECT;
    Object.Header.Revision = NDIS_CONFIGURATION_OBJECT_REVISION_1;
    Object.Header.Size = NDIS_SIZEOF_CONFIGURATION_OBJECT_REVISION_1;
    Object.NdisHandle = Adapter->MiniportHandle;
    Object.Flags = 0;
    if (NdisOpenConfigurationEx(&Object, &Configuration) != NDIS_STATUS_SUCCESS)
        return;

    NdisReadNetworkAddress(&Status, (PVOID *)&Address, &Length, Configuration);
    if (Status == NDIS_STATUS_SUCCESS && Length == ETH_LENGTH_OF_ADDRESS && Address &&
        !(Address[0] & 1) && (Address[0] | Address[1] | Address[2] | Address[3] | Address[4] | Address[5]))
    {
        NdisMoveMemory(Adapter->CurrentMacAddress, Address, ETH_LENGTH_OF_ADDRESS);
    }

    NdisCloseConfiguration(Configuration);
}

static
VOID
Rt25ReleaseAdapter(
    _In_ PRT25_ADAPTER Adapter)
{
    Adapter->HardwareRunning = FALSE;
    if (Adapter->RegisterBase && !Adapter->SurpriseRemoved)
        Rt25DisableInterrupts(Adapter);
    Rt25DeregisterInterrupt(Adapter);
    Rt25StopHardware(Adapter);
    Rt25FreeDatapath(Adapter);

    if (Adapter->DmaHandle)
    {
        NdisMDeregisterScatterGatherDma(Adapter->DmaHandle);
        Adapter->DmaHandle = NULL;
    }

    if (Adapter->RegisterBase)
    {
        NdisMUnmapIoSpace(Adapter->MiniportHandle, Adapter->RegisterBase, Adapter->RegisterLength);
        Adapter->RegisterBase = NULL;
    }

    NdisFreeSpinLock(&Adapter->TxLock);
    NdisFreeSpinLock(&Adapter->RxLock);
    NdisFreeSpinLock(&Adapter->PhyLock);
    NdisFreeMemory(Adapter, sizeof(*Adapter), 0);
}

static
NDIS_STATUS
NTAPI
Rt25InitializeEx(
    _In_ NDIS_HANDLE MiniportAdapterHandle,
    _In_ NDIS_HANDLE MiniportDriverContext,
    _In_ PNDIS_MINIPORT_INIT_PARAMETERS InitParameters)
{
    PRT25_ADAPTER Adapter;
    NDIS_STATUS Status;

    UNREFERENCED_PARAMETER(MiniportDriverContext);

    Adapter = NdisAllocateMemoryWithTagPriority(MiniportAdapterHandle, sizeof(*Adapter), RT25_TAG,
                                                NormalPoolPriority);
    if (!Adapter)
        return NDIS_STATUS_RESOURCES;

    NdisZeroMemory(Adapter, sizeof(*Adapter));
    Adapter->MiniportHandle = MiniportAdapterHandle;
    Adapter->Lookahead = RT25_MTU;
    Adapter->State = Rt25StatePaused;
    Adapter->LinkSpeed = NDIS_LINK_SPEED_UNKNOWN;
    Adapter->MediaConnectState = MediaConnectStateUnknown;
    Adapter->MediaDuplexState = MediaDuplexStateUnknown;
    NdisAllocateSpinLock(&Adapter->PhyLock);
    NdisAllocateSpinLock(&Adapter->RxLock);
    NdisAllocateSpinLock(&Adapter->TxLock);

    Status = Rt25SetRegistrationAttributes(Adapter);
    if (Status != NDIS_STATUS_SUCCESS)
        goto Fail;

    Status = Rt25PrepareBus(Adapter);
    if (Status != NDIS_STATUS_SUCCESS)
        goto Fail;

    Status = Rt25MapResources(Adapter, InitParameters->AllocatedResources);
    if (Status != NDIS_STATUS_SUCCESS)
        goto Fail;

    Status = Rt25IdentifyChip(Adapter);
    if (Status != NDIS_STATUS_SUCCESS)
        goto Fail;

    Rt25ReadMacAddress(Adapter);
    Rt25ReadConfiguration(Adapter);

    Status = Rt25RegisterDma(Adapter);
    if (Status != NDIS_STATUS_SUCCESS)
    {
        DPRINT1("RTL8125: DMA registration failed 0x%08lx\n", Status);
        goto Fail;
    }

    Status = Rt25AllocateDatapath(Adapter);
    if (Status != NDIS_STATUS_SUCCESS)
    {
        DPRINT1("RTL8125: could not allocate the rings 0x%08lx\n", Status);
        goto Fail;
    }

    Status = Rt25RegisterInterrupt(Adapter);
    if (Status != NDIS_STATUS_SUCCESS)
        goto Fail;

    Status = Rt25StartHardware(Adapter);
    if (Status != NDIS_STATUS_SUCCESS)
        goto Fail;

    Rt25RefreshLink(Adapter);
    Status = Rt25SetGeneralAttributes(Adapter);
    if (Status != NDIS_STATUS_SUCCESS)
        goto Fail;

    Rt25EnableInterrupts(Adapter);
    return NDIS_STATUS_SUCCESS;

Fail:
    DPRINT1("RTL8125: initialization failed 0x%08lx\n", Status);
    Rt25ReleaseAdapter(Adapter);
    return Status;
}

static
VOID
NTAPI
Rt25HaltEx(
    _In_ NDIS_HANDLE MiniportAdapterContext,
    _In_ NDIS_HALT_ACTION HaltAction)
{
    UNREFERENCED_PARAMETER(HaltAction);

    Rt25ReleaseAdapter(MiniportAdapterContext);
}

static
NDIS_STATUS
NTAPI
Rt25Pause(
    _In_ NDIS_HANDLE MiniportAdapterContext,
    _In_ PNDIS_MINIPORT_PAUSE_PARAMETERS PauseParameters)
{
    PRT25_ADAPTER Adapter = MiniportAdapterContext;
    LONG Outstanding;

    UNREFERENCED_PARAMETER(PauseParameters);

    NdisAcquireSpinLock(&Adapter->RxLock);
    InterlockedExchange(&Adapter->State, Rt25StatePausing);
    Outstanding = Adapter->RxOutstanding;
    NdisReleaseSpinLock(&Adapter->RxLock);

    if (Outstanding == 0 &&
        InterlockedCompareExchange(&Adapter->State, Rt25StatePaused, Rt25StatePausing) == Rt25StatePausing)
    {
        return NDIS_STATUS_SUCCESS;
    }

    return NDIS_STATUS_PENDING;
}

static
NDIS_STATUS
NTAPI
Rt25Restart(
    _In_ NDIS_HANDLE MiniportAdapterContext,
    _In_ PNDIS_MINIPORT_RESTART_PARAMETERS RestartParameters)
{
    PRT25_ADAPTER Adapter = MiniportAdapterContext;

    UNREFERENCED_PARAMETER(RestartParameters);

    InterlockedExchange(&Adapter->State, Rt25StateRunning);
    return NDIS_STATUS_SUCCESS;
}

static
BOOLEAN
NTAPI
Rt25CheckForHang(
    _In_ NDIS_HANDLE MiniportAdapterContext)
{
    PRT25_ADAPTER Adapter = MiniportAdapterContext;

    /* Backs up the link-change interrupt, which some boards do not raise for every transition. */
    Rt25UpdateLinkState(Adapter);

    Rt25ReclaimTransmit(Adapter);
    return FALSE;
}

static
NDIS_STATUS
NTAPI
Rt25ResetEx(
    _In_ NDIS_HANDLE MiniportAdapterContext,
    _Out_ PBOOLEAN AddressingReset)
{
    UNREFERENCED_PARAMETER(MiniportAdapterContext);

    *AddressingReset = FALSE;
    return NDIS_STATUS_SUCCESS;
}

static
VOID
NTAPI
Rt25DevicePnPEventNotify(
    _In_ NDIS_HANDLE MiniportAdapterContext,
    _In_ PNET_DEVICE_PNP_EVENT NetDevicePnPEvent)
{
    PRT25_ADAPTER Adapter = MiniportAdapterContext;

    if (NetDevicePnPEvent->DevicePnPEvent == NdisDevicePnPEventSurpriseRemoved)
    {
        Adapter->SurpriseRemoved = TRUE;
        Adapter->HardwareRunning = FALSE;
    }
}

static
VOID
NTAPI
Rt25ShutdownEx(
    _In_ NDIS_HANDLE MiniportAdapterContext,
    _In_ NDIS_SHUTDOWN_ACTION ShutdownAction)
{
    PRT25_ADAPTER Adapter = MiniportAdapterContext;

    /* May run at high IRQL after a bugcheck: registers only, no locks or waits on NDIS. */
    if (ShutdownAction != NdisShutdownBugCheck && Adapter->RegisterBase)
    {
        Rt25DisableInterrupts(Adapter);
        Rt25Write8(Adapter, RT25_REG_CR, 0);
    }
}

static
VOID
NTAPI
Rt25Unload(
    _In_ PDRIVER_OBJECT DriverObject)
{
    UNREFERENCED_PARAMETER(DriverObject);

    if (Rt25DriverHandle)
    {
        NdisMDeregisterMiniportDriver(Rt25DriverHandle);
        Rt25DriverHandle = NULL;
    }
}

NTSTATUS
NTAPI
DriverEntry(
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ PUNICODE_STRING RegistryPath)
{
    NDIS_MINIPORT_DRIVER_CHARACTERISTICS Characteristics;
    NDIS_STATUS Status;

    NdisZeroMemory(&Characteristics, sizeof(Characteristics));
    Characteristics.Header.Type = NDIS_OBJECT_TYPE_MINIPORT_DRIVER_CHARACTERISTICS;
    Characteristics.Header.Revision = NDIS_MINIPORT_DRIVER_CHARACTERISTICS_REVISION_1;
    Characteristics.Header.Size = NDIS_SIZEOF_MINIPORT_DRIVER_CHARACTERISTICS_REVISION_1;
    Characteristics.MajorNdisVersion = NDIS_MINIPORT_MAJOR_VERSION;
    Characteristics.MinorNdisVersion = NDIS_MINIPORT_MINOR_VERSION;
    Characteristics.MajorDriverVersion = RT25_DRIVER_VERSION >> 8;
    Characteristics.MinorDriverVersion = RT25_DRIVER_VERSION & 0xFF;
    Characteristics.InitializeHandlerEx = Rt25InitializeEx;
    Characteristics.HaltHandlerEx = Rt25HaltEx;
    Characteristics.UnloadHandler = Rt25Unload;
    Characteristics.PauseHandler = Rt25Pause;
    Characteristics.RestartHandler = Rt25Restart;
    Characteristics.OidRequestHandler = Rt25OidRequest;
    Characteristics.SendNetBufferListsHandler = Rt25SendNetBufferLists;
    Characteristics.ReturnNetBufferListsHandler = Rt25ReturnNetBufferLists;
    Characteristics.CancelSendHandler = Rt25CancelSend;
    Characteristics.CheckForHangHandlerEx = Rt25CheckForHang;
    Characteristics.ResetHandlerEx = Rt25ResetEx;
    Characteristics.DevicePnPEventNotifyHandler = Rt25DevicePnPEventNotify;
    Characteristics.ShutdownHandlerEx = Rt25ShutdownEx;
    Characteristics.CancelOidRequestHandler = Rt25CancelOidRequest;

    Status = NdisMRegisterMiniportDriver(DriverObject, RegistryPath, NULL, &Characteristics, &Rt25DriverHandle);
    if (Status != NDIS_STATUS_SUCCESS)
        DPRINT1("RTL8125: NdisMRegisterMiniportDriver failed 0x%08lx\n", Status);

    return Status;
}
