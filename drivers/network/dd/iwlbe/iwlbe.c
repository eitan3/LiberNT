/*
 * PROJECT:     LiberNT Intel BE200 Native 802.11 Miniport
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Driver entry, miniport lifecycle and transmit queue
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 *              Copyright 2026 The LiberNT contributors
 */

#include "iwlbe.h"

#define NDEBUG
#include <debug.h>

static NDIS_HANDLE IwlMiniportDriverHandle;

PVOID
IwlAllocate(
    _In_ SIZE_T Size)
{
    PVOID Buffer = ExAllocatePoolWithTag(NonPagedPool, Size, IWL_TAG);

    if (Buffer)
        RtlZeroMemory(Buffer, Size);
    return Buffer;
}

VOID
IwlFree(
    _In_opt_ PVOID Buffer)
{
    if (Buffer)
        ExFreePoolWithTag(Buffer, IWL_TAG);
}

VOID
IwlSleep(
    _In_ ULONG Milliseconds)
{
    LARGE_INTEGER Interval;

    Interval.QuadPart = -(LONGLONG)Milliseconds * 10000;
    KeDelayExecutionThread(KernelMode, FALSE, &Interval);
}

VOID
IwlWakeCommand(
    _In_ PIWL_ADAPTER Adapter)
{
    KeSetEvent(&Adapter->CommandEvent, IO_NETWORK_INCREMENT, FALSE);
}

static
VOID
IwlCompleteNbls(
    _In_ PIWL_ADAPTER Adapter,
    _In_opt_ PNET_BUFFER_LIST Nbls,
    _In_ NDIS_STATUS Status,
    _In_ BOOLEAN AtDispatch)
{
    PNET_BUFFER_LIST Nbl;

    if (!Nbls)
        return;

    for (Nbl = Nbls; Nbl; Nbl = NET_BUFFER_LIST_NEXT_NBL(Nbl))
        NET_BUFFER_LIST_STATUS(Nbl) = Status;

    NdisMSendNetBufferListsComplete(Adapter->MiniportAdapterHandle,
                                    Nbls,
                                    AtDispatch ? NDIS_SEND_COMPLETE_FLAGS_DISPATCH_LEVEL : 0);
}

static
PNET_BUFFER_LIST
IwlDequeueNbl(
    _In_ PIWL_ADAPTER Adapter)
{
    PNET_BUFFER_LIST Nbl;

    NdisAcquireSpinLock(&Adapter->TxQueueLock);
    Nbl = Adapter->TxQueueHead;
    if (Nbl)
    {
        Adapter->TxQueueHead = NET_BUFFER_LIST_NEXT_NBL(Nbl);
        if (!Adapter->TxQueueHead)
            Adapter->TxQueueTail = NULL;
        NET_BUFFER_LIST_NEXT_NBL(Nbl) = NULL;
    }
    NdisReleaseSpinLock(&Adapter->TxQueueLock);

    return Nbl;
}

VOID
IwlTxPump(
    _In_ PIWL_ADAPTER Adapter)
{
    PDOT11_EXTSTA_SEND_CONTEXT SendContext;
    PNET_BUFFER_LIST Nbl;
    PNET_BUFFER NetBuffer;
    NDIS_STATUS Status;
    BOOLEAN Exempt;

    while ((Nbl = IwlDequeueNbl(Adapter)) != NULL)
    {
        Status = NDIS_STATUS_SUCCESS;
        if (Adapter->LinkState != IwlLinkAssociated || !Adapter->DataPathRunning || Adapter->Halting)
        {
            Status = NDIS_STATUS_MEDIA_DISCONNECTED;
        }
        else
        {
            SendContext = NET_BUFFER_LIST_INFO(Nbl, MediaSpecificInformation);
            Exempt = SendContext && SendContext->usExemptionActionType == DOT11_EXEMPT_ALWAYS;

            for (NetBuffer = NET_BUFFER_LIST_FIRST_NB(Nbl); NetBuffer; NetBuffer = NET_BUFFER_NEXT_NB(NetBuffer))
            {
                if (NT_SUCCESS(IwlSendNetBuffer(Adapter, NetBuffer, Exempt)))
                {
                    Adapter->TxOk++;
                }
                else
                {
                    Adapter->TxError++;
                    Status = NDIS_STATUS_FAILURE;
                }
            }
        }

        IwlCompleteNbls(Adapter, Nbl, Status, FALSE);
        InterlockedDecrement(&Adapter->TxPending);
    }
}

static
VOID
NTAPI
IwlMiniportSendNetBufferLists(
    _In_ NDIS_HANDLE MiniportAdapterContext,
    _In_ PNET_BUFFER_LIST NetBufferLists,
    _In_ NDIS_PORT_NUMBER PortNumber,
    _In_ ULONG SendFlags)
{
    PIWL_ADAPTER Adapter = MiniportAdapterContext;
    BOOLEAN AtDispatch = NDIS_TEST_SEND_AT_DISPATCH_LEVEL(SendFlags) ? TRUE : FALSE;
    PNET_BUFFER_LIST Tail;
    LONG Count = 1;

    UNREFERENCED_PARAMETER(PortNumber);

    if (!Adapter->DataPathRunning || Adapter->Halting)
    {
        IwlCompleteNbls(Adapter, NetBufferLists, NDIS_STATUS_PAUSED, AtDispatch);
        return;
    }

    if (Adapter->LinkState != IwlLinkAssociated)
    {
        IwlCompleteNbls(Adapter, NetBufferLists, NDIS_STATUS_MEDIA_DISCONNECTED, AtDispatch);
        return;
    }

    for (Tail = NetBufferLists; NET_BUFFER_LIST_NEXT_NBL(Tail); Tail = NET_BUFFER_LIST_NEXT_NBL(Tail))
        Count++;

    InterlockedExchangeAdd(&Adapter->TxPending, Count);
    NdisAcquireSpinLock(&Adapter->TxQueueLock);
    if (Adapter->TxQueueTail)
        NET_BUFFER_LIST_NEXT_NBL(Adapter->TxQueueTail) = NetBufferLists;
    else
        Adapter->TxQueueHead = NetBufferLists;
    Adapter->TxQueueTail = Tail;
    NdisReleaseSpinLock(&Adapter->TxQueueLock);

    KeSetEvent(&Adapter->WorkerEvent, IO_NETWORK_INCREMENT, FALSE);
}

static
VOID
NTAPI
IwlMiniportCancelSend(
    _In_ NDIS_HANDLE MiniportAdapterContext,
    _In_ PVOID CancelId)
{
    UNREFERENCED_PARAMETER(MiniportAdapterContext);
    UNREFERENCED_PARAMETER(CancelId);
}

static
VOID
IwlFlushTxQueue(
    _In_ PIWL_ADAPTER Adapter,
    _In_ NDIS_STATUS Status)
{
    PNET_BUFFER_LIST Nbl;

    while ((Nbl = IwlDequeueNbl(Adapter)) != NULL)
    {
        IwlCompleteNbls(Adapter, Nbl, Status, FALSE);
        InterlockedDecrement(&Adapter->TxPending);
    }
}

static
NDIS_STATUS
IwlSetRegistrationAttributes(
    _In_ PIWL_ADAPTER Adapter)
{
    NDIS_MINIPORT_ADAPTER_REGISTRATION_ATTRIBUTES Registration;

    RtlZeroMemory(&Registration, sizeof(Registration));
    Registration.Header.Type = NDIS_OBJECT_TYPE_MINIPORT_ADAPTER_REGISTRATION_ATTRIBUTES;
    Registration.Header.Revision = NDIS_MINIPORT_ADAPTER_REGISTRATION_ATTRIBUTES_REVISION_1;
    Registration.Header.Size = sizeof(Registration);
    Registration.MiniportAdapterContext = Adapter;
    Registration.AttributeFlags = NDIS_MINIPORT_ATTRIBUTES_HARDWARE_DEVICE |
                                  NDIS_MINIPORT_ATTRIBUTES_BUS_MASTER |
                                  NDIS_MINIPORT_ATTRIBUTES_NO_HALT_ON_SUSPEND |
                                  NDIS_MINIPORT_ATTRIBUTES_SURPRISE_REMOVE_OK;
    Registration.InterfaceType = NdisInterfacePci;

    return NdisMSetMiniportAttributes(Adapter->MiniportAdapterHandle,
                                      (PNDIS_MINIPORT_ADAPTER_ATTRIBUTES)&Registration);
}

static
NDIS_STATUS
IwlSetGeneralAttributes(
    _In_ PIWL_ADAPTER Adapter)
{
    NDIS_MINIPORT_ADAPTER_GENERAL_ATTRIBUTES General;

    RtlZeroMemory(&General, sizeof(General));
    General.Header.Type = NDIS_OBJECT_TYPE_MINIPORT_ADAPTER_GENERAL_ATTRIBUTES;
    General.Header.Revision = NDIS_MINIPORT_ADAPTER_GENERAL_ATTRIBUTES_REVISION_1;
    General.Header.Size = sizeof(General);
    General.MediaType = NdisMediumNative802_11;
    General.PhysicalMediumType = NdisPhysicalMediumNative802_11;
    General.MtuSize = IWL_MTU_SIZE;
    General.MaxXmitLinkSpeed = IWL_LINK_SPEED_BPS;
    General.MaxRcvLinkSpeed = IWL_LINK_SPEED_BPS;
    General.XmitLinkSpeed = NDIS_LINK_SPEED_UNKNOWN;
    General.RcvLinkSpeed = NDIS_LINK_SPEED_UNKNOWN;
    General.MediaConnectState = MediaConnectStateDisconnected;
    General.MediaDuplexState = MediaDuplexStateFull;
    General.LookaheadSize = IWL_MAX_80211_FRAME;
    General.MacOptions = NDIS_MAC_OPTION_COPY_LOOKAHEAD_DATA |
                         NDIS_MAC_OPTION_TRANSFERS_NOT_PEND |
                         NDIS_MAC_OPTION_NO_LOOPBACK;
    General.SupportedPacketFilters = NDIS_PACKET_TYPE_DIRECTED |
                                     NDIS_PACKET_TYPE_MULTICAST |
                                     NDIS_PACKET_TYPE_ALL_MULTICAST |
                                     NDIS_PACKET_TYPE_BROADCAST;
    General.MaxMulticastListSize = 32;
    General.MacAddressLength = IWL_ADDRESS_LENGTH;
    RtlCopyMemory(General.PermanentMacAddress, Adapter->PermanentAddress, IWL_ADDRESS_LENGTH);
    RtlCopyMemory(General.CurrentMacAddress, Adapter->CurrentAddress, IWL_ADDRESS_LENGTH);
    General.AccessType = NET_IF_ACCESS_BROADCAST;
    General.DirectionType = NET_IF_DIRECTION_SENDRECEIVE;
    General.ConnectionType = NET_IF_CONNECTION_DEDICATED;
    General.IfType = IF_TYPE_IEEE80211;
    General.IfConnectorPresent = TRUE;
    General.SupportedOidList = IwlSupportedOids;
    General.SupportedOidListLength = IwlSupportedOidCount * sizeof(NDIS_OID);

    return NdisMSetMiniportAttributes(Adapter->MiniportAdapterHandle,
                                      (PNDIS_MINIPORT_ADAPTER_ATTRIBUTES)&General);
}

static
VOID
IwlReleaseAdapter(
    _In_ PIWL_ADAPTER Adapter)
{
    if (Adapter->CommandThread)
    {
        Adapter->CommandStop = TRUE;
        IwlWakeCommand(Adapter);
        KeWaitForSingleObject(&Adapter->CommandStopped, Executive, KernelMode, FALSE, NULL);
        ObDereferenceObject(Adapter->CommandThread);
        Adapter->CommandThread = NULL;
    }

    Adapter->Halting = TRUE;
    Adapter->DataPathRunning = FALSE;
    Adapter->HardwareReady = FALSE;
    IwlStopDevice(Adapter);
    IwlDeregisterInterrupt(Adapter);
    IwlStopWorker(Adapter);
    IwlFlushTxQueue(Adapter, NDIS_STATUS_FAILURE);
    IwlFreeRings(Adapter);
    IwlUnmapResources(Adapter);

    if (Adapter->RxNblPool)
        NdisFreeNetBufferListPool(Adapter->RxNblPool);

    IwlClearKeys(Adapter);
    if (Adapter->Fw.File)
        ExFreePoolWithTag((PVOID)Adapter->Fw.File, IWL_TAG);
    NdisFreeSpinLock(&Adapter->Lock);
    NdisFreeSpinLock(&Adapter->TxQueueLock);
    NdisFreeSpinLock(&Adapter->TxLock);
    IwlFree(Adapter);
}

static
NTSTATUS
IwlHardwareInitialize(
    _In_ PIWL_ADAPTER Adapter)
{
    NTSTATUS Status;

    Status = IwlLoadFirmwareFile(Adapter);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("IWLBE: firmware image not usable 0x%08lx\n", Status);
        return Status;
    }

    Status = IwlStartHardware(Adapter);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("IWLBE: hardware start failed 0x%08lx\n", Status);
        return Status;
    }

    IwlReadMacAddress(Adapter);

    Status = IwlBootFirmware(Adapter);
    if (!NT_SUCCESS(Status))
        return Status;

    IwlSetRxFilter(Adapter, TRUE);
    return STATUS_SUCCESS;
}

static
NDIS_STATUS
NTAPI
IwlMiniportInitializeEx(
    _In_ NDIS_HANDLE MiniportAdapterHandle,
    _In_ NDIS_HANDLE MiniportDriverContext,
    _In_ PNDIS_MINIPORT_INIT_PARAMETERS InitParameters)
{
    NET_BUFFER_LIST_POOL_PARAMETERS PoolParameters;
    HANDLE ThreadHandle;
    PIWL_ADAPTER Adapter;
    NDIS_STATUS Status;
    NTSTATUS NtStatus;
    BOOLEAN Ready;

    UNREFERENCED_PARAMETER(MiniportDriverContext);

    Adapter = IwlAllocate(sizeof(*Adapter));
    if (!Adapter)
        return NDIS_STATUS_RESOURCES;

    Adapter->MiniportAdapterHandle = MiniportAdapterHandle;
    NdisAllocateSpinLock(&Adapter->Lock);
    NdisAllocateSpinLock(&Adapter->TxQueueLock);
    NdisAllocateSpinLock(&Adapter->TxLock);
    KeInitializeMutex(&Adapter->CommandLock, 0);
    KeInitializeEvent(&Adapter->CommandDone, SynchronizationEvent, FALSE);
    KeInitializeEvent(&Adapter->NotifEvent, SynchronizationEvent, FALSE);
    KeInitializeEvent(&Adapter->WorkerEvent, SynchronizationEvent, FALSE);
    KeInitializeEvent(&Adapter->WorkerStopped, NotificationEvent, FALSE);
    KeInitializeEvent(&Adapter->CommandEvent, SynchronizationEvent, FALSE);
    KeInitializeEvent(&Adapter->CommandStopped, NotificationEvent, FALSE);
    KeInitializeEvent(&Adapter->MlmeEvent, NotificationEvent, FALSE);
    KeInitializeEvent(&Adapter->SaeEvent, SynchronizationEvent, FALSE);

    Adapter->OperationMode = DOT11_OPERATION_MODE_EXTENSIBLE_STATION;
    Adapter->CurrentPhyType = dot11_phy_type_erp;
    Adapter->RadioOn = TRUE;
    Adapter->AutoConfigEnabled = TRUE;
    Adapter->RtsThreshold = 2347;
    Adapter->FragmentationThreshold = 2346;
    Adapter->DesiredBssType = dot11_BSS_type_infrastructure;
    Adapter->AuthAlgorithm = DOT11_AUTH_ALGO_80211_OPEN;
    Adapter->UnicastCipher = DOT11_CIPHER_ALGO_NONE;
    Adapter->MulticastCipher = DOT11_CIPHER_ALGO_NONE;
    Adapter->LinkState = IwlLinkIdle;
    Adapter->CurrentChannel = 1;

    Status = IwlSetRegistrationAttributes(Adapter);
    if (Status != NDIS_STATUS_SUCCESS)
        goto Fail;

    Status = IwlMapResources(Adapter, InitParameters->AllocatedResources);
    if (Status != NDIS_STATUS_SUCCESS)
        goto Fail;

    if (!NT_SUCCESS(IwlAllocateRings(Adapter)))
    {
        Status = NDIS_STATUS_RESOURCES;
        goto Fail;
    }

    Status = IwlRegisterInterrupt(Adapter);
    if (Status != NDIS_STATUS_SUCCESS)
        goto Fail;

    if (!NT_SUCCESS(IwlStartWorker(Adapter)))
    {
        Status = NDIS_STATUS_RESOURCES;
        goto Fail;
    }

    /* A firmware failure keeps the adapter visible so the log can be read */
    Ready = NT_SUCCESS(IwlHardwareInitialize(Adapter));
    if (!Ready)
        IwlReadMacAddress(Adapter);

    RtlZeroMemory(&PoolParameters, sizeof(PoolParameters));
    PoolParameters.Header.Type = NDIS_OBJECT_TYPE_DEFAULT;
    PoolParameters.Header.Revision = NET_BUFFER_LIST_POOL_PARAMETERS_REVISION_1;
    PoolParameters.Header.Size = NDIS_SIZEOF_NET_BUFFER_LIST_POOL_PARAMETERS_REVISION_1;
    PoolParameters.ProtocolId = NDIS_PROTOCOL_ID_DEFAULT;
    PoolParameters.fAllocateNetBuffer = TRUE;
    PoolParameters.PoolTag = IWL_TAG;
    Adapter->RxNblPool = NdisAllocateNetBufferListPool(MiniportAdapterHandle, &PoolParameters);
    if (!Adapter->RxNblPool)
    {
        Status = NDIS_STATUS_RESOURCES;
        goto Fail;
    }

    Status = IwlSetGeneralAttributes(Adapter);
    if (Status != NDIS_STATUS_SUCCESS)
        goto Fail;

    NtStatus = PsCreateSystemThread(&ThreadHandle, THREAD_ALL_ACCESS, NULL, NULL, NULL, IwlCommandThread, Adapter);
    if (!NT_SUCCESS(NtStatus))
    {
        Status = NDIS_STATUS_RESOURCES;
        goto Fail;
    }

    ObReferenceObjectByHandle(ThreadHandle, THREAD_ALL_ACCESS, NULL, KernelMode, (PVOID*)&Adapter->CommandThread, NULL);
    ZwClose(ThreadHandle);

    Adapter->HardwareReady = Ready;
    DPRINT1("IWLBE: adapter %s\n", Ready ? "ready" : "present, firmware not running");
    return NDIS_STATUS_SUCCESS;

Fail:
    DPRINT1("IWLBE: initialization failed 0x%08lx\n", Status);
    IwlReleaseAdapter(Adapter);
    return Status;
}

static
VOID
NTAPI
IwlMiniportHaltEx(
    _In_ NDIS_HANDLE MiniportAdapterContext,
    _In_ NDIS_HALT_ACTION HaltAction)
{
    PIWL_ADAPTER Adapter = MiniportAdapterContext;

    UNREFERENCED_PARAMETER(HaltAction);

    Adapter->DataPathRunning = FALSE;
    IwlReleaseAdapter(Adapter);
}

static
NDIS_STATUS
NTAPI
IwlMiniportPauseEx(
    _In_ NDIS_HANDLE MiniportAdapterContext,
    _In_ PNDIS_MINIPORT_PAUSE_PARAMETERS PauseParameters)
{
    PIWL_ADAPTER Adapter = MiniportAdapterContext;
    ULONG Wait;

    UNREFERENCED_PARAMETER(PauseParameters);

    Adapter->DataPathRunning = FALSE;
    KeSetEvent(&Adapter->WorkerEvent, IO_NETWORK_INCREMENT, FALSE);
    for (Wait = 0; Wait < 500 && (Adapter->RxIndicated || Adapter->TxPending); Wait++)
        IwlSleep(2);
    IwlFlushTxQueue(Adapter, NDIS_STATUS_PAUSED);

    return NDIS_STATUS_SUCCESS;
}

static
NDIS_STATUS
NTAPI
IwlMiniportRestartEx(
    _In_ NDIS_HANDLE MiniportAdapterContext,
    _In_ PNDIS_MINIPORT_RESTART_PARAMETERS RestartParameters)
{
    PIWL_ADAPTER Adapter = MiniportAdapterContext;

    UNREFERENCED_PARAMETER(RestartParameters);

    Adapter->DataPathRunning = TRUE;
    IwlIndicateLinkState(Adapter, Adapter->LinkState == IwlLinkAssociated);
    return NDIS_STATUS_SUCCESS;
}

static
VOID
NTAPI
IwlMiniportShutdownEx(
    _In_ NDIS_HANDLE MiniportAdapterContext,
    _In_ NDIS_SHUTDOWN_ACTION ShutdownAction)
{
    PIWL_ADAPTER Adapter = MiniportAdapterContext;

    UNREFERENCED_PARAMETER(ShutdownAction);

    Adapter->DataPathRunning = FALSE;
}

static
VOID
NTAPI
IwlMiniportDevicePnpEventNotify(
    _In_ NDIS_HANDLE MiniportAdapterContext,
    _In_ PNET_DEVICE_PNP_EVENT NetDevicePnPEvent)
{
    PIWL_ADAPTER Adapter = MiniportAdapterContext;

    if (NetDevicePnPEvent->DevicePnPEvent == NdisDevicePnPEventSurpriseRemoved)
    {
        Adapter->Halting = TRUE;
        Adapter->DataPathRunning = FALSE;
    }
}

static
VOID
NTAPI
IwlMiniportUnload(
    _In_ PDRIVER_OBJECT DriverObject)
{
    UNREFERENCED_PARAMETER(DriverObject);

    if (IwlMiniportDriverHandle)
    {
        NdisMDeregisterMiniportDriver(IwlMiniportDriverHandle);
        IwlMiniportDriverHandle = NULL;
    }
}

NTSTATUS
NTAPI
DriverEntry(
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ PUNICODE_STRING RegistryPath)
{
    NDIS_MINIPORT_DRIVER_CHARACTERISTICS Characteristics;

    RtlZeroMemory(&Characteristics, sizeof(Characteristics));
    Characteristics.Header.Type = NDIS_OBJECT_TYPE_MINIPORT_DRIVER_CHARACTERISTICS;
    Characteristics.Header.Revision = NDIS_MINIPORT_DRIVER_CHARACTERISTICS_REVISION_2;
    Characteristics.Header.Size = sizeof(Characteristics);
    Characteristics.MajorNdisVersion = 6;
    Characteristics.MinorNdisVersion = 20;
    Characteristics.MajorDriverVersion = 1;
    Characteristics.MinorDriverVersion = 0;
    Characteristics.InitializeHandlerEx = IwlMiniportInitializeEx;
    Characteristics.HaltHandlerEx = IwlMiniportHaltEx;
    Characteristics.UnloadHandler = IwlMiniportUnload;
    Characteristics.PauseHandler = IwlMiniportPauseEx;
    Characteristics.RestartHandler = IwlMiniportRestartEx;
    Characteristics.OidRequestHandler = IwlMiniportOidRequest;
    Characteristics.SendNetBufferListsHandler = IwlMiniportSendNetBufferLists;
    Characteristics.ReturnNetBufferListsHandler = IwlMiniportReturnNetBufferLists;
    Characteristics.CancelSendHandler = IwlMiniportCancelSend;
    Characteristics.DevicePnPEventNotifyHandler = IwlMiniportDevicePnpEventNotify;
    Characteristics.ShutdownHandlerEx = IwlMiniportShutdownEx;
    Characteristics.CancelOidRequestHandler = IwlMiniportCancelOidRequest;

    return NdisMRegisterMiniportDriver(DriverObject, RegistryPath, NULL, &Characteristics, &IwlMiniportDriverHandle);
}
