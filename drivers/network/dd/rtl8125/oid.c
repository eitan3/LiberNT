/*
 * PROJECT:     LiberNT Realtek RTL8125/RTL8126 Ethernet Driver
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     OID requests
 * COPYRIGHT:   Copyright 2026 The LiberNT contributors
 */

#include "rtl8125.h"

const NDIS_OID Rt25SupportedOids[] =
{
    OID_GEN_SUPPORTED_LIST,
    OID_GEN_HARDWARE_STATUS,
    OID_GEN_MEDIA_SUPPORTED,
    OID_GEN_MEDIA_IN_USE,
    OID_GEN_PHYSICAL_MEDIUM,
    OID_GEN_MAXIMUM_LOOKAHEAD,
    OID_GEN_CURRENT_LOOKAHEAD,
    OID_GEN_MAXIMUM_FRAME_SIZE,
    OID_GEN_MAXIMUM_TOTAL_SIZE,
    OID_GEN_TRANSMIT_BLOCK_SIZE,
    OID_GEN_RECEIVE_BLOCK_SIZE,
    OID_GEN_TRANSMIT_BUFFER_SPACE,
    OID_GEN_RECEIVE_BUFFER_SPACE,
    OID_GEN_LINK_SPEED,
    OID_GEN_LINK_STATE,
    OID_GEN_MEDIA_CONNECT_STATUS,
    OID_GEN_VENDOR_ID,
    OID_GEN_VENDOR_DESCRIPTION,
    OID_GEN_VENDOR_DRIVER_VERSION,
    OID_GEN_DRIVER_VERSION,
    OID_GEN_CURRENT_PACKET_FILTER,
    OID_GEN_MAC_OPTIONS,
    OID_GEN_MAXIMUM_SEND_PACKETS,
    OID_GEN_XMIT_OK,
    OID_GEN_RCV_OK,
    OID_GEN_XMIT_ERROR,
    OID_GEN_RCV_ERROR,
    OID_GEN_RCV_NO_BUFFER,
    OID_GEN_STATISTICS,
    OID_802_3_PERMANENT_ADDRESS,
    OID_802_3_CURRENT_ADDRESS,
    OID_802_3_MULTICAST_LIST,
    OID_802_3_MAXIMUM_LIST_SIZE,
    OID_802_3_RCV_ERROR_ALIGNMENT,
    OID_802_3_XMIT_ONE_COLLISION,
    OID_802_3_XMIT_MORE_COLLISIONS
};

const ULONG Rt25SupportedOidsSize = sizeof(Rt25SupportedOids);

static
NDIS_STATUS
Rt25CopyQuery(
    _Inout_ PNDIS_OID_REQUEST Request,
    _In_reads_bytes_(Length) const VOID *Source,
    _In_ ULONG Length)
{
    Request->DATA.QUERY_INFORMATION.BytesWritten = 0;
    Request->DATA.QUERY_INFORMATION.BytesNeeded = Length;
    if (Request->DATA.QUERY_INFORMATION.InformationBufferLength < Length)
        return NDIS_STATUS_BUFFER_TOO_SHORT;

    NdisMoveMemory(Request->DATA.QUERY_INFORMATION.InformationBuffer, Source, Length);
    Request->DATA.QUERY_INFORMATION.BytesWritten = Length;
    return NDIS_STATUS_SUCCESS;
}

static
NDIS_STATUS
Rt25CopyCounter(
    _Inout_ PNDIS_OID_REQUEST Request,
    _In_ ULONG64 Value)
{
    ULONG Low = (ULONG)Value;

    if (Request->DATA.QUERY_INFORMATION.InformationBufferLength >= sizeof(ULONG64))
        return Rt25CopyQuery(Request, &Value, sizeof(Value));

    return Rt25CopyQuery(Request, &Low, sizeof(Low));
}

static
VOID
Rt25FillStatistics(
    _In_ PRT25_ADAPTER Adapter,
    _Out_ PNDIS_STATISTICS_INFO Statistics)
{
    NdisZeroMemory(Statistics, sizeof(*Statistics));
    Statistics->Header.Type = NDIS_OBJECT_TYPE_DEFAULT;
    Statistics->Header.Revision = NDIS_STATISTICS_INFO_REVISION_1;
    Statistics->Header.Size = NDIS_SIZEOF_STATISTICS_INFO_REVISION_1;
    Statistics->SupportedStatistics = NDIS_STATISTICS_FLAGS_VALID_DIRECTED_FRAMES_RCV |
                                      NDIS_STATISTICS_FLAGS_VALID_RCV_ERROR |
                                      NDIS_STATISTICS_FLAGS_VALID_RCV_DISCARDS |
                                      NDIS_STATISTICS_FLAGS_VALID_BYTES_RCV |
                                      NDIS_STATISTICS_FLAGS_VALID_DIRECTED_FRAMES_XMIT |
                                      NDIS_STATISTICS_FLAGS_VALID_XMIT_ERROR |
                                      NDIS_STATISTICS_FLAGS_VALID_BYTES_XMIT;
    Statistics->ifHCInUcastPkts = Adapter->RxPackets;
    Statistics->ifInErrors = Adapter->RxErrors;
    Statistics->ifInDiscards = Adapter->RxErrors + Adapter->RxNoBuffer;
    Statistics->ifHCInOctets = Adapter->RxBytes;
    Statistics->ifHCOutUcastPkts = Adapter->TxPackets;
    Statistics->ifOutErrors = Adapter->TxErrors;
    Statistics->ifHCOutOctets = Adapter->TxBytes;
}

static
NDIS_STATUS
Rt25QueryInformation(
    _In_ PRT25_ADAPTER Adapter,
    _Inout_ PNDIS_OID_REQUEST Request)
{
    union
    {
        ULONG Ulong;
        USHORT Ushort;
        NDIS_MEDIUM Medium;
        NDIS_PHYSICAL_MEDIUM PhysicalMedium;
        NDIS_HARDWARE_STATUS HardwareStatus;
        NDIS_MEDIA_STATE MediaState;
        NDIS_LINK_STATE LinkState;
        NDIS_STATISTICS_INFO Statistics;
    } Data;
    static const CHAR Description[] = "Realtek RTL8125/RTL8126 Ethernet";

    NdisZeroMemory(&Data, sizeof(Data));

    switch (Request->DATA.QUERY_INFORMATION.Oid)
    {
        case OID_GEN_SUPPORTED_LIST:
            return Rt25CopyQuery(Request, Rt25SupportedOids, Rt25SupportedOidsSize);

        case OID_GEN_HARDWARE_STATUS:
            Data.HardwareStatus = NdisHardwareStatusReady;
            return Rt25CopyQuery(Request, &Data.HardwareStatus, sizeof(Data.HardwareStatus));

        case OID_GEN_MEDIA_SUPPORTED:
        case OID_GEN_MEDIA_IN_USE:
            Data.Medium = NdisMedium802_3;
            return Rt25CopyQuery(Request, &Data.Medium, sizeof(Data.Medium));

        case OID_GEN_PHYSICAL_MEDIUM:
            Data.PhysicalMedium = NdisPhysicalMedium802_3;
            return Rt25CopyQuery(Request, &Data.PhysicalMedium, sizeof(Data.PhysicalMedium));

        case OID_GEN_MAXIMUM_LOOKAHEAD:
        case OID_GEN_MAXIMUM_FRAME_SIZE:
            Data.Ulong = RT25_MTU;
            return Rt25CopyQuery(Request, &Data.Ulong, sizeof(Data.Ulong));

        case OID_GEN_CURRENT_LOOKAHEAD:
            Data.Ulong = Adapter->Lookahead;
            return Rt25CopyQuery(Request, &Data.Ulong, sizeof(Data.Ulong));

        case OID_GEN_MAXIMUM_TOTAL_SIZE:
        case OID_GEN_TRANSMIT_BLOCK_SIZE:
        case OID_GEN_RECEIVE_BLOCK_SIZE:
            Data.Ulong = RT25_MAX_FRAME;
            return Rt25CopyQuery(Request, &Data.Ulong, sizeof(Data.Ulong));

        case OID_GEN_TRANSMIT_BUFFER_SPACE:
        case OID_GEN_RECEIVE_BUFFER_SPACE:
            Data.Ulong = RT25_RING_SIZE * RT25_MAX_FRAME;
            return Rt25CopyQuery(Request, &Data.Ulong, sizeof(Data.Ulong));

        case OID_GEN_MAXIMUM_SEND_PACKETS:
            Data.Ulong = RT25_RING_SIZE;
            return Rt25CopyQuery(Request, &Data.Ulong, sizeof(Data.Ulong));

        case OID_GEN_LINK_SPEED:
            Data.Ulong = Adapter->LinkSpeed == NDIS_LINK_SPEED_UNKNOWN ? 0 : (ULONG)(Adapter->LinkSpeed / 100);
            return Rt25CopyQuery(Request, &Data.Ulong, sizeof(Data.Ulong));

        case OID_GEN_LINK_STATE:
            Rt25FillLinkState(Adapter, &Data.LinkState);
            return Rt25CopyQuery(Request, &Data.LinkState, sizeof(Data.LinkState));

        case OID_GEN_MEDIA_CONNECT_STATUS:
            Data.MediaState = Adapter->MediaConnectState == MediaConnectStateConnected ?
                              NdisMediaStateConnected : NdisMediaStateDisconnected;
            return Rt25CopyQuery(Request, &Data.MediaState, sizeof(Data.MediaState));

        case OID_GEN_VENDOR_ID:
            Data.Ulong = ((ULONG)Adapter->PermanentMacAddress[0] << 16) |
                         ((ULONG)Adapter->PermanentMacAddress[1] << 8) |
                         Adapter->PermanentMacAddress[2];
            return Rt25CopyQuery(Request, &Data.Ulong, sizeof(Data.Ulong));

        case OID_GEN_VENDOR_DESCRIPTION:
            return Rt25CopyQuery(Request, Description, sizeof(Description));

        case OID_GEN_VENDOR_DRIVER_VERSION:
            Data.Ulong = RT25_DRIVER_VERSION;
            return Rt25CopyQuery(Request, &Data.Ulong, sizeof(Data.Ulong));

        case OID_GEN_DRIVER_VERSION:
            Data.Ushort = (NDIS_MINIPORT_MAJOR_VERSION << 8) | NDIS_MINIPORT_MINOR_VERSION;
            return Rt25CopyQuery(Request, &Data.Ushort, sizeof(Data.Ushort));

        case OID_GEN_CURRENT_PACKET_FILTER:
            Data.Ulong = Adapter->PacketFilter;
            return Rt25CopyQuery(Request, &Data.Ulong, sizeof(Data.Ulong));

        case OID_GEN_MAC_OPTIONS:
            Data.Ulong = RT25_MAC_OPTIONS;
            return Rt25CopyQuery(Request, &Data.Ulong, sizeof(Data.Ulong));

        case OID_GEN_XMIT_OK:
            return Rt25CopyCounter(Request, Adapter->TxPackets);

        case OID_GEN_RCV_OK:
            return Rt25CopyCounter(Request, Adapter->RxPackets);

        case OID_GEN_XMIT_ERROR:
            return Rt25CopyCounter(Request, Adapter->TxErrors);

        case OID_GEN_RCV_ERROR:
            return Rt25CopyCounter(Request, Adapter->RxErrors);

        case OID_GEN_RCV_NO_BUFFER:
            return Rt25CopyCounter(Request, Adapter->RxNoBuffer);

        case OID_GEN_STATISTICS:
            Rt25FillStatistics(Adapter, &Data.Statistics);
            return Rt25CopyQuery(Request, &Data.Statistics, sizeof(Data.Statistics));

        case OID_802_3_PERMANENT_ADDRESS:
            return Rt25CopyQuery(Request, Adapter->PermanentMacAddress, ETH_LENGTH_OF_ADDRESS);

        case OID_802_3_CURRENT_ADDRESS:
            return Rt25CopyQuery(Request, Adapter->CurrentMacAddress, ETH_LENGTH_OF_ADDRESS);

        case OID_802_3_MULTICAST_LIST:
            return Rt25CopyQuery(Request, Adapter->MulticastList, Adapter->MulticastCount * ETH_LENGTH_OF_ADDRESS);

        case OID_802_3_MAXIMUM_LIST_SIZE:
            Data.Ulong = RT25_MAX_MULTICAST;
            return Rt25CopyQuery(Request, &Data.Ulong, sizeof(Data.Ulong));

        case OID_802_3_RCV_ERROR_ALIGNMENT:
        case OID_802_3_XMIT_ONE_COLLISION:
        case OID_802_3_XMIT_MORE_COLLISIONS:
            Data.Ulong = 0;
            return Rt25CopyQuery(Request, &Data.Ulong, sizeof(Data.Ulong));
    }

    return NDIS_STATUS_NOT_SUPPORTED;
}

static
NDIS_STATUS
Rt25SetInformation(
    _In_ PRT25_ADAPTER Adapter,
    _Inout_ PNDIS_OID_REQUEST Request)
{
    PVOID Buffer = Request->DATA.SET_INFORMATION.InformationBuffer;
    ULONG Length = Request->DATA.SET_INFORMATION.InformationBufferLength;
    ULONG Value;

    Request->DATA.SET_INFORMATION.BytesRead = 0;
    Request->DATA.SET_INFORMATION.BytesNeeded = 0;

    switch (Request->DATA.SET_INFORMATION.Oid)
    {
        case OID_GEN_CURRENT_PACKET_FILTER:
            if (Length < sizeof(ULONG))
            {
                Request->DATA.SET_INFORMATION.BytesNeeded = sizeof(ULONG);
                return NDIS_STATUS_INVALID_LENGTH;
            }

            NdisMoveMemory(&Value, Buffer, sizeof(Value));
            if (Value & ~RT25_SUPPORTED_FILTERS)
                return NDIS_STATUS_NOT_SUPPORTED;

            Adapter->PacketFilter = Value;
            Rt25ApplyReceiveFilter(Adapter);
            Request->DATA.SET_INFORMATION.BytesRead = sizeof(ULONG);
            return NDIS_STATUS_SUCCESS;

        case OID_GEN_CURRENT_LOOKAHEAD:
            if (Length < sizeof(ULONG))
            {
                Request->DATA.SET_INFORMATION.BytesNeeded = sizeof(ULONG);
                return NDIS_STATUS_INVALID_LENGTH;
            }

            NdisMoveMemory(&Value, Buffer, sizeof(Value));
            if (Value > RT25_MTU)
                return NDIS_STATUS_INVALID_DATA;

            Adapter->Lookahead = Value;
            Request->DATA.SET_INFORMATION.BytesRead = sizeof(ULONG);
            return NDIS_STATUS_SUCCESS;

        case OID_802_3_MULTICAST_LIST:
            if (Length % ETH_LENGTH_OF_ADDRESS)
                return NDIS_STATUS_INVALID_LENGTH;

            if (Length > sizeof(Adapter->MulticastList))
            {
                Request->DATA.SET_INFORMATION.BytesNeeded = sizeof(Adapter->MulticastList);
                return NDIS_STATUS_MULTICAST_FULL;
            }

            NdisMoveMemory(Adapter->MulticastList, Buffer, Length);
            Adapter->MulticastCount = Length / ETH_LENGTH_OF_ADDRESS;
            Rt25ApplyReceiveFilter(Adapter);
            Request->DATA.SET_INFORMATION.BytesRead = Length;
            return NDIS_STATUS_SUCCESS;
    }

    return NDIS_STATUS_NOT_SUPPORTED;
}

NDIS_STATUS
NTAPI
Rt25OidRequest(
    _In_ NDIS_HANDLE MiniportAdapterContext,
    _Inout_ PNDIS_OID_REQUEST OidRequest)
{
    PRT25_ADAPTER Adapter = MiniportAdapterContext;

    switch (OidRequest->RequestType)
    {
        case NdisRequestQueryInformation:
        case NdisRequestQueryStatistics:
            return Rt25QueryInformation(Adapter, OidRequest);

        case NdisRequestSetInformation:
            return Rt25SetInformation(Adapter, OidRequest);

        default:
            return NDIS_STATUS_NOT_SUPPORTED;
    }
}

VOID
NTAPI
Rt25CancelOidRequest(
    _In_ NDIS_HANDLE MiniportAdapterContext,
    _In_ PVOID RequestId)
{
    UNREFERENCED_PARAMETER(MiniportAdapterContext);
    UNREFERENCED_PARAMETER(RequestId);
}
