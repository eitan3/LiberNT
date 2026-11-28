/*
 * PROJECT:     LiberNT Intel BE200 Native 802.11 Miniport
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     NDIS and Native 802.11 OID handling
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 *              Copyright 2026 The LiberNT contributors
 */

#include "iwlbe.h"

#define NDEBUG
#include <debug.h>

NDIS_OID IwlSupportedOids[] =
{
    OID_GEN_SUPPORTED_LIST,
    OID_GEN_HARDWARE_STATUS,
    OID_GEN_MEDIA_SUPPORTED,
    OID_GEN_MEDIA_IN_USE,
    OID_GEN_MAXIMUM_FRAME_SIZE,
    OID_GEN_MAXIMUM_TOTAL_SIZE,
    OID_GEN_TRANSMIT_BLOCK_SIZE,
    OID_GEN_RECEIVE_BLOCK_SIZE,
    OID_GEN_TRANSMIT_BUFFER_SPACE,
    OID_GEN_RECEIVE_BUFFER_SPACE,
    OID_GEN_VENDOR_ID,
    OID_GEN_VENDOR_DESCRIPTION,
    OID_GEN_VENDOR_DRIVER_VERSION,
    OID_GEN_DRIVER_VERSION,
    OID_GEN_PHYSICAL_MEDIUM,
    OID_GEN_CURRENT_PACKET_FILTER,
    OID_GEN_CURRENT_LOOKAHEAD,
    OID_GEN_MAXIMUM_LOOKAHEAD,
    OID_GEN_LINK_SPEED,
    OID_GEN_MEDIA_CONNECT_STATUS,
    OID_GEN_MAXIMUM_SEND_PACKETS,
    OID_GEN_MAC_OPTIONS,
    OID_GEN_XMIT_OK,
    OID_GEN_RCV_OK,
    OID_GEN_XMIT_ERROR,
    OID_GEN_RCV_ERROR,
    OID_GEN_RCV_NO_BUFFER,
    OID_GEN_STATISTICS,
    OID_DOT11_MPDU_MAX_LENGTH,
    OID_DOT11_OPERATION_MODE_CAPABILITY,
    OID_DOT11_CURRENT_OPERATION_MODE,
    OID_DOT11_CURRENT_PACKET_FILTER,
    OID_DOT11_CURRENT_PHY_TYPE,
    OID_DOT11_SUPPORTED_PHY_TYPES,
    OID_DOT11_NIC_POWER_STATE,
    OID_DOT11_HARDWARE_PHY_STATE,
    OID_DOT11_MAC_ADDRESS,
    OID_DOT11_PERMANENT_ADDRESS,
    OID_DOT11_CURRENT_ADDRESS,
    OID_DOT11_OPERATIONAL_RATE_SET,
    OID_DOT11_BEACON_PERIOD,
    OID_DOT11_CURRENT_CHANNEL_NUMBER,
    OID_DOT11_RTS_THRESHOLD,
    OID_DOT11_FRAGMENTATION_THRESHOLD,
    OID_DOT11_AUTO_CONFIG_ENABLED,
    OID_DOT11_SCAN_REQUEST,
    OID_DOT11_ENUM_BSS_LIST,
    OID_DOT11_FLUSH_BSS_LIST,
    OID_DOT11_DESIRED_SSID_LIST,
    OID_DOT11_DESIRED_BSSID_LIST,
    OID_DOT11_DESIRED_BSS_TYPE,
    OID_DOT11_CONNECT_REQUEST,
    OID_DOT11_DISCONNECT_REQUEST,
    OID_DOT11_RESET_REQUEST,
    OID_DOT11_ENABLED_AUTHENTICATION_ALGORITHM,
    OID_DOT11_ENABLED_UNICAST_CIPHER_ALGORITHM,
    OID_DOT11_ENABLED_MULTICAST_CIPHER_ALGORITHM,
    OID_DOT11_CIPHER_DEFAULT_KEY,
    OID_DOT11_CIPHER_DEFAULT_KEY_ID,
    OID_DOT11_CIPHER_KEY_MAPPING_KEY,
    OID_WDI_SET_SAE_AUTH_PARAMS,
};

ULONG IwlSupportedOidCount = RTL_NUMBER_OF(IwlSupportedOids);

static
NDIS_STATUS
IwlQueryBuffer(
    _In_ PNDIS_OID_REQUEST Request,
    _In_reads_bytes_(Length) const VOID* Data,
    _In_ ULONG Length)
{
    if (Request->DATA.QUERY_INFORMATION.InformationBufferLength < Length)
    {
        Request->DATA.QUERY_INFORMATION.BytesNeeded = Length;
        return NDIS_STATUS_BUFFER_TOO_SHORT;
    }

    RtlCopyMemory(Request->DATA.QUERY_INFORMATION.InformationBuffer, Data, Length);
    Request->DATA.QUERY_INFORMATION.BytesWritten = Length;
    return NDIS_STATUS_SUCCESS;
}

static
NDIS_STATUS
IwlQueryUlong(
    _In_ PNDIS_OID_REQUEST Request,
    _In_ ULONG Value)
{
    return IwlQueryBuffer(Request, &Value, sizeof(Value));
}

static
NDIS_STATUS
IwlQueryCounter(
    _In_ PNDIS_OID_REQUEST Request,
    _In_ ULONG64 Value)
{
    if (Request->DATA.QUERY_INFORMATION.InformationBufferLength >= sizeof(ULONG64))
        return IwlQueryBuffer(Request, &Value, sizeof(ULONG64));

    return IwlQueryUlong(Request, (ULONG)Value);
}

static
NDIS_STATUS
IwlQueryStatistics(
    _In_ PIWL_ADAPTER Adapter,
    _In_ PNDIS_OID_REQUEST Request)
{
    NDIS_STATISTICS_INFO Stats;

    RtlZeroMemory(&Stats, sizeof(Stats));
    Stats.Header.Type = NDIS_OBJECT_TYPE_DEFAULT;
    Stats.Header.Revision = NDIS_STATISTICS_INFO_REVISION_1;
    Stats.Header.Size = sizeof(Stats);
    Stats.SupportedStatistics = NDIS_STATISTICS_FLAGS_VALID_BYTES_RCV |
                                NDIS_STATISTICS_FLAGS_VALID_BYTES_XMIT |
                                NDIS_STATISTICS_FLAGS_VALID_RCV_ERROR |
                                NDIS_STATISTICS_FLAGS_VALID_XMIT_ERROR |
                                NDIS_STATISTICS_FLAGS_VALID_DIRECTED_FRAMES_RCV |
                                NDIS_STATISTICS_FLAGS_VALID_DIRECTED_FRAMES_XMIT;
    Stats.ifHCInOctets = Adapter->RxBytes;
    Stats.ifHCInUcastPkts = Adapter->RxOk;
    Stats.ifInErrors = Adapter->RxError;
    Stats.ifHCOutOctets = Adapter->TxBytes;
    Stats.ifHCOutUcastPkts = Adapter->TxOk;
    Stats.ifOutErrors = Adapter->TxError;
    return IwlQueryBuffer(Request, &Stats, sizeof(Stats));
}

static
NDIS_STATUS
IwlQueryEnumBssList(
    _In_ PIWL_ADAPTER Adapter,
    _In_reads_bytes_opt_(Length) PUCHAR Buffer,
    _In_ ULONG Length,
    _Out_ PULONG Written,
    _Out_ PULONG Needed)
{
    *Written = IwlBuildBssList(Adapter, Buffer, Length, Needed);
    return *Written ? NDIS_STATUS_SUCCESS : NDIS_STATUS_BUFFER_TOO_SHORT;
}

static
NDIS_STATUS
IwlDot11Query(
    _In_ PIWL_ADAPTER Adapter,
    _In_ PNDIS_OID_REQUEST Request)
{
    switch (Request->DATA.QUERY_INFORMATION.Oid)
    {
        case OID_DOT11_MPDU_MAX_LENGTH:
            return IwlQueryUlong(Request, IWL_MAX_80211_FRAME);

        case OID_DOT11_OPERATION_MODE_CAPABILITY:
        {
            DOT11_OPERATION_MODE_CAPABILITY Capability;

            RtlZeroMemory(&Capability, sizeof(Capability));
            Capability.uMajorVersion = 2;
            Capability.uNumOfTXBuffers = IWL_TX_QUEUE_DEPTH;
            Capability.uNumOfRXBuffers = IWL_RX_QUEUE_DEPTH;
            Capability.uOpModeCapability = DOT11_OPERATION_MODE_EXTENSIBLE_STATION;
            return IwlQueryBuffer(Request, &Capability, sizeof(Capability));
        }

        case OID_DOT11_CURRENT_OPERATION_MODE:
        {
            DOT11_CURRENT_OPERATION_MODE Mode;

            RtlZeroMemory(&Mode, sizeof(Mode));
            Mode.uCurrentOpMode = Adapter->OperationMode;
            return IwlQueryBuffer(Request, &Mode, sizeof(Mode));
        }

        case OID_DOT11_CURRENT_PHY_TYPE:
            return IwlQueryUlong(Request, (ULONG)Adapter->CurrentPhyType);

        case OID_DOT11_SUPPORTED_PHY_TYPES:
        {
            /* FIXME: the OID's payload is DOT11_SUPPORTED_PHY_TYPES, which has no NDIS header */
            UCHAR Buffer[FIELD_OFFSET(DOT11_PHY_TYPE_LIST, dot11PhyType) + 2 * sizeof(DOT11_PHY_TYPE)];
            PDOT11_PHY_TYPE_LIST List = (PDOT11_PHY_TYPE_LIST)Buffer;

            RtlZeroMemory(Buffer, sizeof(Buffer));
            List->Header.Type = NDIS_OBJECT_TYPE_DEFAULT;
            List->Header.Revision = DOT11_PHY_TYPE_LIST_REVISION_1;
            List->Header.Size = sizeof(DOT11_PHY_TYPE_LIST);
            List->uNumOfEntries = 2;
            List->uTotalNumOfEntries = List->uNumOfEntries;
            List->dot11PhyType[0] = dot11_phy_type_erp;
            List->dot11PhyType[1] = dot11_phy_type_ofdm;
            return IwlQueryBuffer(Request,
                                 Buffer,
                                 FIELD_OFFSET(DOT11_PHY_TYPE_LIST, dot11PhyType) + List->uNumOfEntries * sizeof(DOT11_PHY_TYPE));
        }

        case OID_DOT11_CURRENT_PACKET_FILTER:
            return IwlQueryUlong(Request, Adapter->PacketFilter);

        case OID_DOT11_CURRENT_CHANNEL_NUMBER:
            return IwlQueryUlong(Request, Adapter->CurrentChannel);

        case OID_DOT11_RTS_THRESHOLD:
            return IwlQueryUlong(Request, Adapter->RtsThreshold);

        case OID_DOT11_FRAGMENTATION_THRESHOLD:
            return IwlQueryUlong(Request, Adapter->FragmentationThreshold);

        case OID_DOT11_BEACON_PERIOD:
            return IwlQueryUlong(Request, 100);

        case OID_DOT11_NIC_POWER_STATE:
        case OID_DOT11_HARDWARE_PHY_STATE:
        {
            BOOLEAN On = Adapter->RadioOn;
            return IwlQueryBuffer(Request, &On, sizeof(On));
        }

        case OID_DOT11_AUTO_CONFIG_ENABLED:
            return IwlQueryUlong(Request, Adapter->AutoConfigEnabled ? 1 : 0);

        case OID_DOT11_MAC_ADDRESS:
        case OID_DOT11_CURRENT_ADDRESS:
            return IwlQueryBuffer(Request, Adapter->CurrentAddress, IWL_ADDRESS_LENGTH);

        case OID_DOT11_PERMANENT_ADDRESS:
            return IwlQueryBuffer(Request, Adapter->PermanentAddress, IWL_ADDRESS_LENGTH);

        case OID_DOT11_OPERATIONAL_RATE_SET:
        {
            static const UCHAR Rates[] = { 2, 4, 11, 22, 12, 18, 24, 36, 48, 72, 96, 108 };
            DOT11_RATE_SET RateSet;

            RtlZeroMemory(&RateSet, sizeof(RateSet));
            RateSet.uRateSetLength = sizeof(Rates);
            RtlCopyMemory(RateSet.ucRateSet, Rates, sizeof(Rates));
            return IwlQueryBuffer(Request, &RateSet, sizeof(RateSet));
        }

        case OID_DOT11_ENABLED_AUTHENTICATION_ALGORITHM:
            return IwlQueryUlong(Request, (ULONG)Adapter->AuthAlgorithm);

        case OID_DOT11_ENABLED_UNICAST_CIPHER_ALGORITHM:
            return IwlQueryUlong(Request, (ULONG)Adapter->UnicastCipher);

        case OID_DOT11_ENABLED_MULTICAST_CIPHER_ALGORITHM:
            return IwlQueryUlong(Request, (ULONG)Adapter->MulticastCipher);

        case OID_DOT11_DESIRED_BSS_TYPE:
            return IwlQueryUlong(Request, (ULONG)Adapter->DesiredBssType);

        case OID_DOT11_ENUM_BSS_LIST:
        {
            ULONG Written, Needed;
            NDIS_STATUS Status;

            Status = IwlQueryEnumBssList(Adapter,
                                        Request->DATA.QUERY_INFORMATION.InformationBuffer,
                                        Request->DATA.QUERY_INFORMATION.InformationBufferLength,
                                        &Written,
                                        &Needed);
            Request->DATA.QUERY_INFORMATION.BytesWritten = Written;
            Request->DATA.QUERY_INFORMATION.BytesNeeded = Needed;
            return Status;
        }

        default:
            return NDIS_STATUS_NOT_SUPPORTED;
    }
}

static
NDIS_STATUS
IwlOidQuery(
    _In_ PIWL_ADAPTER Adapter,
    _In_ PNDIS_OID_REQUEST Request)
{
    switch (Request->DATA.QUERY_INFORMATION.Oid)
    {
        case OID_GEN_SUPPORTED_LIST:
            return IwlQueryBuffer(Request, IwlSupportedOids, IwlSupportedOidCount * sizeof(NDIS_OID));

        case OID_GEN_HARDWARE_STATUS:
            return IwlQueryUlong(Request, Adapter->HardwareReady ? NdisHardwareStatusReady : NdisHardwareStatusInitializing);

        case OID_GEN_MEDIA_SUPPORTED:
        case OID_GEN_MEDIA_IN_USE:
            return IwlQueryUlong(Request, NdisMediumNative802_11);

        case OID_GEN_PHYSICAL_MEDIUM:
            return IwlQueryUlong(Request, NdisPhysicalMediumNative802_11);

        case OID_GEN_MAXIMUM_FRAME_SIZE:
            return IwlQueryUlong(Request, IWL_MTU_SIZE);

        case OID_GEN_MAXIMUM_TOTAL_SIZE:
        case OID_GEN_TRANSMIT_BLOCK_SIZE:
        case OID_GEN_RECEIVE_BLOCK_SIZE:
        case OID_GEN_CURRENT_LOOKAHEAD:
        case OID_GEN_MAXIMUM_LOOKAHEAD:
            return IwlQueryUlong(Request, IWL_MAX_80211_FRAME);

        case OID_GEN_TRANSMIT_BUFFER_SPACE:
            return IwlQueryUlong(Request, IWL_MAX_80211_FRAME * IWL_TX_QUEUE_DEPTH);

        case OID_GEN_RECEIVE_BUFFER_SPACE:
            return IwlQueryUlong(Request, IWL_RX_BUFFER_SIZE);

        case OID_GEN_VENDOR_ID:
            return IwlQueryUlong(Request,
                                ((ULONG)Adapter->PermanentAddress[0] << 16) |
                                    ((ULONG)Adapter->PermanentAddress[1] << 8) |
                                    Adapter->PermanentAddress[2]);

        case OID_GEN_VENDOR_DESCRIPTION:
        {
            static const CHAR Description[] = "Intel Wi-Fi 7 BE200 Wireless Network Adapter";
            return IwlQueryBuffer(Request, Description, sizeof(Description));
        }

        case OID_GEN_VENDOR_DRIVER_VERSION:
            return IwlQueryUlong(Request, 0x00010000);

        case OID_GEN_DRIVER_VERSION:
        {
            USHORT Version = (6 << 8) | 20;
            return IwlQueryBuffer(Request, &Version, sizeof(Version));
        }

        case OID_GEN_CURRENT_PACKET_FILTER:
            return IwlQueryUlong(Request, Adapter->PacketFilter);

        case OID_GEN_LINK_SPEED:
            return IwlQueryUlong(Request, Adapter->LinkState == IwlLinkAssociated ? (ULONG)(IWL_LINK_SPEED_BPS / 100) : 0);

        case OID_GEN_MEDIA_CONNECT_STATUS:
            return IwlQueryUlong(Request, Adapter->LinkState == IwlLinkAssociated ? NdisMediaStateConnected : NdisMediaStateDisconnected);

        case OID_GEN_MAXIMUM_SEND_PACKETS:
            return IwlQueryUlong(Request, IWL_TX_QUEUE_DEPTH);

        case OID_GEN_MAC_OPTIONS:
            return IwlQueryUlong(Request,
                                NDIS_MAC_OPTION_COPY_LOOKAHEAD_DATA |
                                    NDIS_MAC_OPTION_TRANSFERS_NOT_PEND |
                                    NDIS_MAC_OPTION_NO_LOOPBACK);

        case OID_GEN_XMIT_OK:
            return IwlQueryCounter(Request, Adapter->TxOk);
        case OID_GEN_RCV_OK:
            return IwlQueryCounter(Request, Adapter->RxOk);
        case OID_GEN_XMIT_ERROR:
            return IwlQueryCounter(Request, Adapter->TxError);
        case OID_GEN_RCV_ERROR:
            return IwlQueryCounter(Request, Adapter->RxError);
        case OID_GEN_RCV_NO_BUFFER:
            return IwlQueryCounter(Request, 0);
        case OID_GEN_STATISTICS:
            return IwlQueryStatistics(Adapter, Request);

        default:
            return IwlDot11Query(Adapter, Request);
    }
}

static
BOOLEAN
IwlDecodeCcmpKey(
    _In_reads_bytes_(Length) const UCHAR* Encoded,
    _In_ ULONG Length,
    _Out_writes_bytes_(16) PUCHAR Key)
{
    PDOT11_KEY_ALGO_CCMP Ccmp = (PDOT11_KEY_ALGO_CCMP)Encoded;

    if (Length < FIELD_OFFSET(DOT11_KEY_ALGO_CCMP, ucCCMPKey) + 16 || Ccmp->ulCCMPKeyLength != 16)
        return FALSE;

    RtlCopyMemory(Key, Ccmp->ucCCMPKey, 16);
    return TRUE;
}

VOID
IwlClearKeys(
    _In_ PIWL_ADAPTER Adapter)
{
    NdisAcquireSpinLock(&Adapter->Lock);
    Adapter->MgmtKeyValid = FALSE;
    Adapter->IgtkValid = FALSE;
    RtlZeroMemory(Adapter->GroupKeyValid, sizeof(Adapter->GroupKeyValid));
    RtlSecureZeroMemory(Adapter->GroupKey, sizeof(Adapter->GroupKey));
    RtlSecureZeroMemory(Adapter->MgmtTk, sizeof(Adapter->MgmtTk));
    RtlSecureZeroMemory(Adapter->Igtk, sizeof(Adapter->Igtk));
    NdisReleaseSpinLock(&Adapter->Lock);
}

static
NDIS_STATUS
IwlSetDefaultKey(
    _In_ PIWL_ADAPTER Adapter,
    _In_reads_bytes_(Length) PVOID Buffer,
    _In_ ULONG Length)
{
    PDOT11_CIPHER_DEFAULT_KEY_VALUE Value = Buffer;
    UCHAR Key[16];

    if (Length < FIELD_OFFSET(DOT11_CIPHER_DEFAULT_KEY_VALUE, ucKey) ||
        Length < FIELD_OFFSET(DOT11_CIPHER_DEFAULT_KEY_VALUE, ucKey) + Value->usKeyLength)
    {
        return NDIS_STATUS_INVALID_LENGTH;
    }

    if (Value->AlgorithmId == DOT11_CIPHER_ALGO_NONE && !Value->bDelete)
        return NDIS_STATUS_NOT_SUPPORTED;

    if (Value->bDelete)
        return NDIS_STATUS_SUCCESS;

    if (Value->AlgorithmId == DOT11_CIPHER_ALGO_BIP)
    {
        PDOT11_KEY_ALGO_BIP Bip = (PDOT11_KEY_ALGO_BIP)Value->ucKey;
        ULONG i;

        if ((Value->uKeyIndex != 4 && Value->uKeyIndex != 5) ||
            Value->usKeyLength < FIELD_OFFSET(DOT11_KEY_ALGO_BIP, ucBIPKey) + 16 ||
            Bip->ulBIPKeyLength != 16)
        {
            return NDIS_STATUS_INVALID_DATA;
        }

        NdisAcquireSpinLock(&Adapter->Lock);
        RtlCopyMemory(Adapter->Igtk, Bip->ucBIPKey, 16);
        Adapter->IgtkKeyId = (USHORT)Value->uKeyIndex;
        Adapter->IgtkRxIpn = 0;
        for (i = 0; i < 6; i++)
            Adapter->IgtkRxIpn |= (ULONGLONG)Bip->ucIPN[i] << (8 * i);
        Adapter->IgtkValid = TRUE;
        NdisReleaseSpinLock(&Adapter->Lock);
        DPRINT1("IWLBE: IGTK %lu installed\n", Value->uKeyIndex);
        return NDIS_STATUS_SUCCESS;
    }

    if (Value->AlgorithmId != DOT11_CIPHER_ALGO_CCMP || Value->uKeyIndex >= 4 ||
        !IwlDecodeCcmpKey(Value->ucKey, Value->usKeyLength, Key))
    {
        return NDIS_STATUS_NOT_SUPPORTED;
    }

    NdisAcquireSpinLock(&Adapter->Lock);
    RtlCopyMemory(Adapter->GroupKey[Value->uKeyIndex], Key, 16);
    Adapter->GroupRxPn[Value->uKeyIndex] = 0;
    Adapter->GroupKeyValid[Value->uKeyIndex] = TRUE;
    NdisReleaseSpinLock(&Adapter->Lock);
    RtlSecureZeroMemory(Key, sizeof(Key));
    DPRINT1("IWLBE: group key %lu installed\n", Value->uKeyIndex);
    return NDIS_STATUS_SUCCESS;
}

static
NDIS_STATUS
IwlSetKeyMappingKey(
    _In_ PIWL_ADAPTER Adapter,
    _In_reads_bytes_(Length) PVOID Buffer,
    _In_ ULONG Length)
{
    PDOT11_BYTE_ARRAY Array = Buffer;
    PDOT11_CIPHER_KEY_MAPPING_KEY_VALUE Value;
    UCHAR Key[16];

    if (Length < FIELD_OFFSET(DOT11_BYTE_ARRAY, ucBuffer) ||
        Length < FIELD_OFFSET(DOT11_BYTE_ARRAY, ucBuffer) + Array->uNumOfBytes ||
        Array->uNumOfBytes < FIELD_OFFSET(DOT11_CIPHER_KEY_MAPPING_KEY_VALUE, ucKey))
    {
        return NDIS_STATUS_INVALID_LENGTH;
    }

    Value = (PDOT11_CIPHER_KEY_MAPPING_KEY_VALUE)Array->ucBuffer;
    if (Array->uNumOfBytes < FIELD_OFFSET(DOT11_CIPHER_KEY_MAPPING_KEY_VALUE, ucKey) + Value->usKeyLength)
        return NDIS_STATUS_INVALID_LENGTH;

    if (Value->bDelete)
    {
        NdisAcquireSpinLock(&Adapter->Lock);
        Adapter->MgmtKeyValid = FALSE;
        RtlSecureZeroMemory(Adapter->MgmtTk, sizeof(Adapter->MgmtTk));
        NdisReleaseSpinLock(&Adapter->Lock);
        return NDIS_STATUS_SUCCESS;
    }

    if (Value->AlgorithmId != DOT11_CIPHER_ALGO_CCMP ||
        !IwlDecodeCcmpKey(Value->ucKey, Value->usKeyLength, Key))
    {
        return NDIS_STATUS_NOT_SUPPORTED;
    }

    NdisAcquireSpinLock(&Adapter->Lock);
    RtlCopyMemory(Adapter->MgmtTk, Key, 16);
    Adapter->MgmtTxPn = 0;
    Adapter->MgmtRxPn = 0;
    Adapter->DataRxPn = 0;
    Adapter->MgmtKeyValid = TRUE;
    NdisReleaseSpinLock(&Adapter->Lock);
    RtlSecureZeroMemory(Key, sizeof(Key));
    DPRINT1("IWLBE: pairwise key installed\n");
    return NDIS_STATUS_SUCCESS;
}

static
VOID
IwlQueueCommand(
    _In_ PIWL_ADAPTER Adapter,
    _In_ IWL_COMMAND Command)
{
    NdisAcquireSpinLock(&Adapter->Lock);
    switch (Command)
    {
        case IwlCommandScan:
            Adapter->ScanQueued = TRUE;
            break;
        case IwlCommandConnect:
            Adapter->ConnectQueued = TRUE;
            Adapter->DisconnectQueued = FALSE;
            break;
        case IwlCommandDisconnect:
            Adapter->DisconnectQueued = TRUE;
            Adapter->ConnectQueued = FALSE;
            break;
        default:
            break;
    }
    NdisReleaseSpinLock(&Adapter->Lock);
    IwlWakeCommand(Adapter);
}

static
NDIS_STATUS
IwlSetScanRequest(
    _In_ PIWL_ADAPTER Adapter,
    _In_reads_bytes_(Length) PVOID Buffer,
    _In_ ULONG Length)
{
    PDOT11_SCAN_REQUEST_V2 Scan = Buffer;
    PDOT11_SSID Ssid;

    if (Length < FIELD_OFFSET(DOT11_SCAN_REQUEST_V2, ucBuffer))
        return NDIS_STATUS_INVALID_LENGTH;

    if (!Adapter->RadioOn)
        return NDIS_STATUS_DOT11_POWER_STATE_INVALID;

    Ssid = NULL;
    if (Scan->uNumOfdot11SSIDs >= 1)
    {
        if (Scan->udot11SSIDsOffset <= Length - FIELD_OFFSET(DOT11_SCAN_REQUEST_V2, ucBuffer) &&
            Length - FIELD_OFFSET(DOT11_SCAN_REQUEST_V2, ucBuffer) - Scan->udot11SSIDsOffset >= sizeof(DOT11_SSID))
        {
            Ssid = (PDOT11_SSID)(Scan->ucBuffer + Scan->udot11SSIDsOffset);
        }
        else if (Scan->udot11SSIDsOffset >= FIELD_OFFSET(DOT11_SCAN_REQUEST_V2, ucBuffer) &&
                 Scan->udot11SSIDsOffset <= Length &&
                 Length - Scan->udot11SSIDsOffset >= sizeof(DOT11_SSID))
        {
            Ssid = (PDOT11_SSID)((PUCHAR)Scan + Scan->udot11SSIDsOffset);
        }
    }

    NdisAcquireSpinLock(&Adapter->Lock);
    RtlZeroMemory(&Adapter->ScanSsid, sizeof(Adapter->ScanSsid));
    if (Ssid && Ssid->uSSIDLength <= DOT11_SSID_MAX_LENGTH)
        Adapter->ScanSsid = *Ssid;
    NdisReleaseSpinLock(&Adapter->Lock);

    IwlQueueCommand(Adapter, IwlCommandScan);
    return NDIS_STATUS_SUCCESS;
}

static
NDIS_STATUS
IwlDot11Set(
    _In_ PIWL_ADAPTER Adapter,
    _In_ PNDIS_OID_REQUEST Request)
{
    PVOID Buffer = Request->DATA.SET_INFORMATION.InformationBuffer;
    ULONG Length = Request->DATA.SET_INFORMATION.InformationBufferLength;
    NDIS_STATUS Status = NDIS_STATUS_SUCCESS;

    switch (Request->DATA.SET_INFORMATION.Oid)
    {
        case OID_DOT11_CURRENT_PHY_TYPE:
            if (Length < sizeof(ULONG))
                return NDIS_STATUS_INVALID_LENGTH;
            Adapter->CurrentPhyType = (DOT11_PHY_TYPE)*(PULONG)Buffer;
            break;

        case OID_DOT11_CURRENT_PACKET_FILTER:
            if (Length < sizeof(ULONG))
                return NDIS_STATUS_INVALID_LENGTH;
            Adapter->PacketFilter = *(PULONG)Buffer;
            break;

        case OID_DOT11_CURRENT_OPERATION_MODE:
            if (Length < sizeof(DOT11_CURRENT_OPERATION_MODE))
                return NDIS_STATUS_INVALID_LENGTH;
            if (((PDOT11_CURRENT_OPERATION_MODE)Buffer)->uCurrentOpMode != DOT11_OPERATION_MODE_EXTENSIBLE_STATION)
                return NDIS_STATUS_INVALID_DATA;
            Adapter->OperationMode = DOT11_OPERATION_MODE_EXTENSIBLE_STATION;
            break;

        case OID_DOT11_NIC_POWER_STATE:
            if (Length < sizeof(BOOLEAN))
                return NDIS_STATUS_INVALID_LENGTH;
            Adapter->RadioOn = *(PBOOLEAN)Buffer;
            if (!Adapter->RadioOn)
                IwlQueueCommand(Adapter, IwlCommandDisconnect);
            break;

        case OID_DOT11_RTS_THRESHOLD:
            if (Length < sizeof(ULONG))
                return NDIS_STATUS_INVALID_LENGTH;
            Adapter->RtsThreshold = *(PULONG)Buffer;
            break;

        case OID_DOT11_FRAGMENTATION_THRESHOLD:
            if (Length < sizeof(ULONG))
                return NDIS_STATUS_INVALID_LENGTH;
            Adapter->FragmentationThreshold = *(PULONG)Buffer;
            break;

        case OID_DOT11_AUTO_CONFIG_ENABLED:
            if (Length < sizeof(ULONG))
                return NDIS_STATUS_INVALID_LENGTH;
            Adapter->AutoConfigEnabled = *(PULONG)Buffer != 0;
            break;

        case OID_DOT11_BEACON_PERIOD:
        case OID_DOT11_CURRENT_CHANNEL_NUMBER:
        case OID_DOT11_OPERATIONAL_RATE_SET:
        case OID_DOT11_FLUSH_BSS_LIST:
            break;

        case OID_DOT11_ENABLED_AUTHENTICATION_ALGORITHM:
        {
            PDOT11_AUTH_ALGORITHM_LIST List = Buffer;

            if (Length < FIELD_OFFSET(DOT11_AUTH_ALGORITHM_LIST, AlgorithmIds) + sizeof(DOT11_AUTH_ALGORITHM) ||
                List->uNumOfEntries < 1)
            {
                return NDIS_STATUS_INVALID_LENGTH;
            }
            if (List->AlgorithmIds[0] != DOT11_AUTH_ALGO_80211_OPEN &&
                List->AlgorithmIds[0] != DOT11_AUTH_ALGO_RSNA_PSK &&
                List->AlgorithmIds[0] != DOT11_AUTH_ALGO_WPA3_SAE)
            {
                return NDIS_STATUS_NOT_SUPPORTED;
            }
            Adapter->AuthAlgorithm = List->AlgorithmIds[0];
            break;
        }

        case OID_DOT11_ENABLED_UNICAST_CIPHER_ALGORITHM:
        case OID_DOT11_ENABLED_MULTICAST_CIPHER_ALGORITHM:
        {
            PDOT11_CIPHER_ALGORITHM_LIST List = Buffer;

            if (Length < FIELD_OFFSET(DOT11_CIPHER_ALGORITHM_LIST, AlgorithmIds) + sizeof(DOT11_CIPHER_ALGORITHM) ||
                List->uNumOfEntries < 1)
            {
                return NDIS_STATUS_INVALID_LENGTH;
            }
            if (Request->DATA.SET_INFORMATION.Oid == OID_DOT11_ENABLED_UNICAST_CIPHER_ALGORITHM)
                Adapter->UnicastCipher = List->AlgorithmIds[0];
            else
                Adapter->MulticastCipher = List->AlgorithmIds[0];
            break;
        }

        case OID_DOT11_DESIRED_BSS_TYPE:
            if (Length < sizeof(ULONG))
                return NDIS_STATUS_INVALID_LENGTH;
            if (*(PULONG)Buffer != dot11_BSS_type_infrastructure && *(PULONG)Buffer != dot11_BSS_type_any)
                return NDIS_STATUS_NOT_SUPPORTED;
            Adapter->DesiredBssType = (DOT11_BSS_TYPE)*(PULONG)Buffer;
            break;

        case OID_DOT11_DESIRED_SSID_LIST:
        {
            PDOT11_SSID_LIST List = Buffer;

            if (Length < FIELD_OFFSET(DOT11_SSID_LIST, SSIDs))
                return NDIS_STATUS_INVALID_LENGTH;
            NdisAcquireSpinLock(&Adapter->Lock);
            if (List->uNumOfEntries >= 1 &&
                Length >= FIELD_OFFSET(DOT11_SSID_LIST, SSIDs) + sizeof(DOT11_SSID) &&
                List->SSIDs[0].uSSIDLength <= DOT11_SSID_MAX_LENGTH)
            {
                Adapter->DesiredSsid = List->SSIDs[0];
                Adapter->HaveDesiredSsid = List->SSIDs[0].uSSIDLength != 0;
            }
            else
            {
                RtlZeroMemory(&Adapter->DesiredSsid, sizeof(Adapter->DesiredSsid));
                Adapter->HaveDesiredSsid = FALSE;
            }
            NdisReleaseSpinLock(&Adapter->Lock);
            break;
        }

        case OID_DOT11_DESIRED_BSSID_LIST:
        {
            PDOT11_BSSID_LIST List = Buffer;
            static const UCHAR Any[IWL_ADDRESS_LENGTH] = { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff };

            if (Length < FIELD_OFFSET(DOT11_BSSID_LIST, BSSIDs))
                return NDIS_STATUS_INVALID_LENGTH;
            NdisAcquireSpinLock(&Adapter->Lock);
            if (List->uNumOfEntries >= 1 &&
                Length >= FIELD_OFFSET(DOT11_BSSID_LIST, BSSIDs) + sizeof(DOT11_MAC_ADDRESS) &&
                !RtlEqualMemory(List->BSSIDs[0], Any, IWL_ADDRESS_LENGTH))
            {
                RtlCopyMemory(Adapter->DesiredBssid, List->BSSIDs[0], IWL_ADDRESS_LENGTH);
                Adapter->HaveDesiredBssid = TRUE;
            }
            else
            {
                Adapter->HaveDesiredBssid = FALSE;
            }
            NdisReleaseSpinLock(&Adapter->Lock);
            break;
        }

        case OID_DOT11_SCAN_REQUEST:
            Status = IwlSetScanRequest(Adapter, Buffer, Length);
            break;

        case OID_DOT11_CONNECT_REQUEST:
            if (!Adapter->RadioOn)
                return NDIS_STATUS_DOT11_POWER_STATE_INVALID;
            IwlQueueCommand(Adapter, IwlCommandConnect);
            break;

        case OID_DOT11_DISCONNECT_REQUEST:
            IwlQueueCommand(Adapter, IwlCommandDisconnect);
            break;

        case OID_DOT11_RESET_REQUEST:
            NdisAcquireSpinLock(&Adapter->Lock);
            Adapter->HaveDesiredSsid = FALSE;
            Adapter->HaveDesiredBssid = FALSE;
            NdisReleaseSpinLock(&Adapter->Lock);
            IwlQueueCommand(Adapter, IwlCommandDisconnect);
            break;

        case OID_DOT11_CIPHER_DEFAULT_KEY:
            Status = IwlSetDefaultKey(Adapter, Buffer, Length);
            break;

        case OID_DOT11_CIPHER_DEFAULT_KEY_ID:
            if (Length < sizeof(ULONG))
                return NDIS_STATUS_INVALID_LENGTH;
            break;

        case OID_DOT11_CIPHER_KEY_MAPPING_KEY:
            Status = IwlSetKeyMappingKey(Adapter, Buffer, Length);
            break;

        default:
            return NDIS_STATUS_NOT_SUPPORTED;
    }

    if (Status == NDIS_STATUS_SUCCESS)
        Request->DATA.SET_INFORMATION.BytesRead = Length;

    return Status;
}

static
NDIS_STATUS
IwlOidSet(
    _In_ PIWL_ADAPTER Adapter,
    _In_ PNDIS_OID_REQUEST Request)
{
    PVOID Buffer = Request->DATA.SET_INFORMATION.InformationBuffer;
    ULONG Length = Request->DATA.SET_INFORMATION.InformationBufferLength;

    switch (Request->DATA.SET_INFORMATION.Oid)
    {
        case OID_GEN_CURRENT_PACKET_FILTER:
            if (Length < sizeof(ULONG))
            {
                Request->DATA.SET_INFORMATION.BytesNeeded = sizeof(ULONG);
                return NDIS_STATUS_INVALID_LENGTH;
            }
            Adapter->PacketFilter = *(PULONG)Buffer;
            Request->DATA.SET_INFORMATION.BytesRead = sizeof(ULONG);
            return NDIS_STATUS_SUCCESS;

        case OID_GEN_CURRENT_LOOKAHEAD:
            if (Length < sizeof(ULONG))
            {
                Request->DATA.SET_INFORMATION.BytesNeeded = sizeof(ULONG);
                return NDIS_STATUS_INVALID_LENGTH;
            }
            Request->DATA.SET_INFORMATION.BytesRead = sizeof(ULONG);
            return NDIS_STATUS_SUCCESS;

        case OID_WDI_SET_SAE_AUTH_PARAMS:
        {
            NWIFI_SAE_PARAMS Params;

            if (!NwifiSaeParseParams(Buffer, Length, &Params))
                return NDIS_STATUS_INVALID_DATA;
            NdisAcquireSpinLock(&Adapter->Lock);
            Adapter->SaeParams = Params;
            Adapter->SaeParamsValid = TRUE;
            NdisReleaseSpinLock(&Adapter->Lock);
            RtlSecureZeroMemory(&Params, sizeof(Params));
            KeSetEvent(&Adapter->SaeEvent, IO_NETWORK_INCREMENT, FALSE);
            Request->DATA.SET_INFORMATION.BytesRead = Length;
            return NDIS_STATUS_SUCCESS;
        }

        case OID_GEN_NETWORK_LAYER_ADDRESSES:
        case OID_PNP_SET_POWER:
        case OID_PNP_ADD_WAKE_UP_PATTERN:
        case OID_PNP_REMOVE_WAKE_UP_PATTERN:
            Request->DATA.SET_INFORMATION.BytesRead = Length;
            return NDIS_STATUS_SUCCESS;

        default:
            return IwlDot11Set(Adapter, Request);
    }
}

static
NDIS_STATUS
IwlOidMethod(
    _In_ PIWL_ADAPTER Adapter,
    _In_ PNDIS_OID_REQUEST Request)
{
    switch (Request->DATA.METHOD_INFORMATION.Oid)
    {
        case OID_DOT11_ENUM_BSS_LIST:
        {
            ULONG Written, Needed;
            NDIS_STATUS Status;

            Status = IwlQueryEnumBssList(Adapter,
                                        Request->DATA.METHOD_INFORMATION.InformationBuffer,
                                        Request->DATA.METHOD_INFORMATION.OutputBufferLength,
                                        &Written,
                                        &Needed);
            Request->DATA.METHOD_INFORMATION.BytesWritten = Written;
            Request->DATA.METHOD_INFORMATION.BytesNeeded = Needed;
            return Status;
        }

        case OID_DOT11_SCAN_REQUEST:
            Request->DATA.METHOD_INFORMATION.BytesWritten = 0;
            return IwlSetScanRequest(Adapter,
                                    Request->DATA.METHOD_INFORMATION.InformationBuffer,
                                    Request->DATA.METHOD_INFORMATION.InputBufferLength);

        case OID_DOT11_CONNECT_REQUEST:
            Request->DATA.METHOD_INFORMATION.BytesWritten = 0;
            IwlQueueCommand(Adapter, IwlCommandConnect);
            return NDIS_STATUS_SUCCESS;

        case OID_DOT11_DISCONNECT_REQUEST:
        case OID_DOT11_RESET_REQUEST:
            Request->DATA.METHOD_INFORMATION.BytesWritten = 0;
            IwlQueueCommand(Adapter, IwlCommandDisconnect);
            return NDIS_STATUS_SUCCESS;

        default:
            return NDIS_STATUS_NOT_SUPPORTED;
    }
}

NDIS_STATUS
NTAPI
IwlMiniportOidRequest(
    _In_ NDIS_HANDLE MiniportAdapterContext,
    _In_ PNDIS_OID_REQUEST OidRequest)
{
    PIWL_ADAPTER Adapter = MiniportAdapterContext;

    switch (OidRequest->RequestType)
    {
        case NdisRequestQueryInformation:
        case NdisRequestQueryStatistics:
            return IwlOidQuery(Adapter, OidRequest);

        case NdisRequestSetInformation:
            return IwlOidSet(Adapter, OidRequest);

        case NdisRequestMethod:
            return IwlOidMethod(Adapter, OidRequest);

        default:
            return NDIS_STATUS_NOT_SUPPORTED;
    }
}

VOID
NTAPI
IwlMiniportCancelOidRequest(
    _In_ NDIS_HANDLE MiniportAdapterContext,
    _In_ PVOID RequestId)
{
    UNREFERENCED_PARAMETER(MiniportAdapterContext);
    UNREFERENCED_PARAMETER(RequestId);
}
