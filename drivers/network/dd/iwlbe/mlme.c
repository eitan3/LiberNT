/*
 * PROJECT:     LiberNT Intel BE200 Native 802.11 Miniport
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Station management: scanning, authentication, association and data receive
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 *              Copyright 2026 The LiberNT contributors
 */

#include "iwlbe.h"
#include <rsna_ccmp.h>

#define NDEBUG
#include <debug.h>

#define IWL_RX_CONTEXT_TAG       'XlwI'
#define IWL_BSS_EXPIRY_100NS     (60ULL * 10000000ULL)
#define IWL_RSN_CAP_MFPC         0x0080
#define IWL_RSN_CAP_MFPR         0x0040
#define IWL_AKM_SAE              8
#define IWL_AUTH_ALGORITHM_SAE   3
#define IWL_STATUS_SAE_H2E       126
#define IWL_EID_RSNXE            244
#define IWL_EID_EXTENSION        255
#define IWL_EXT_ACT_CONTAINER    93
#define IWL_RSNXE_SAE_H2E        0x20
#define IWL_ACTION_SA_QUERY      8
#define IWL_SA_QUERY_REQUEST     0
#define IWL_SA_QUERY_RESPONSE    1
#define IWL_STATUS_REFUSED_TEMPORARILY 30
#define IWL_TIMEOUT_COMEBACK     3
#define IWL_PROTECTED_MGMT_MAX   512
#define IWL_SA_QUERY_TIMEOUT_100NS (1000ULL * 10000ULL)

typedef struct _IWL_RX_PACKET
{
    DOT11_EXTSTA_RECV_CONTEXT Context;
    PMDL Mdl;
    ULONG Length;
    UCHAR Data[ANYSIZE_ARRAY];
} IWL_RX_PACKET, *PIWL_RX_PACKET;

static const UCHAR IwlBroadcast[IWL_ADDRESS_LENGTH] = { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff };


static const UCHAR IwlRates2G[] = { 0x82, 0x84, 0x8b, 0x96, 0x0c, 0x12, 0x18, 0x24 };
static const UCHAR IwlExtRates2G[] = { 0x30, 0x48, 0x60, 0x6c };
static const UCHAR IwlRates5G[] = { 0x8c, 0x12, 0x98, 0x24, 0xb0, 0x48, 0x60, 0x6c };

ULONG
IwlFrameHeaderLength(
    _In_reads_bytes_(Length) const UCHAR* Frame,
    _In_ ULONG Length)
{
    ULONG HeaderLength = DOT11_HEADER_LENGTH;

    if (Length < 2)
        return Length;

    if ((Frame[0] & DOT11_FC0_TYPE_MASK) == DOT11_FC0_TYPE_CTRL)
        return min(Length, 16);

    if ((Frame[0] & DOT11_FC0_TYPE_MASK) == DOT11_FC0_TYPE_DATA)
    {
        if ((Frame[1] & (DOT11_FC1_TODS | DOT11_FC1_FROMDS)) == (DOT11_FC1_TODS | DOT11_FC1_FROMDS))
            HeaderLength += IWL_ADDRESS_LENGTH;
        if (Frame[0] & DOT11_QOS_SUBTYPE)
        {
            HeaderLength += 2;
            if (Frame[1] & 0x80)
                HeaderLength += 4;
        }
    }
    else if (Frame[1] & 0x80)
    {
        HeaderLength += 4;
    }

    return min(HeaderLength, Length);
}

ULONG
IwlChannelToFrequency(
    _In_ ULONG Channel)
{
    if (Channel == 14)
        return 2484;
    if (Channel < 14)
        return 2407 + Channel * 5;
    return 5000 + Channel * 5;
}

VOID
IwlIndicateDot11Status(
    _In_ PIWL_ADAPTER Adapter,
    _In_ NDIS_STATUS StatusCode,
    _In_reads_bytes_opt_(BufferSize) PVOID Buffer,
    _In_ ULONG BufferSize)
{
    NDIS_STATUS_INDICATION Indication;

    RtlZeroMemory(&Indication, sizeof(Indication));
    Indication.Header.Type = NDIS_OBJECT_TYPE_STATUS_INDICATION;
    Indication.Header.Revision = NDIS_STATUS_INDICATION_REVISION_1;
    Indication.Header.Size = sizeof(NDIS_STATUS_INDICATION);
    Indication.SourceHandle = Adapter->MiniportAdapterHandle;
    Indication.StatusCode = StatusCode;
    Indication.StatusBuffer = Buffer;
    Indication.StatusBufferSize = BufferSize;
    NdisMIndicateStatusEx(Adapter->MiniportAdapterHandle, &Indication);
}

VOID
IwlIndicateLinkState(
    _In_ PIWL_ADAPTER Adapter,
    _In_ BOOLEAN Connected)
{
    NDIS_LINK_STATE LinkState;

    RtlZeroMemory(&LinkState, sizeof(LinkState));
    LinkState.Header.Type = NDIS_OBJECT_TYPE_DEFAULT;
    LinkState.Header.Revision = NDIS_LINK_STATE_REVISION_1;
    LinkState.Header.Size = sizeof(NDIS_LINK_STATE);
    LinkState.MediaConnectState = Connected ? MediaConnectStateConnected : MediaConnectStateDisconnected;
    LinkState.MediaDuplexState = MediaDuplexStateFull;
    LinkState.XmitLinkSpeed = Connected ? IWL_LINK_SPEED_BPS : NDIS_LINK_SPEED_UNKNOWN;
    LinkState.RcvLinkSpeed = Connected ? IWL_LINK_SPEED_BPS : NDIS_LINK_SPEED_UNKNOWN;
    IwlIndicateDot11Status(Adapter, NDIS_STATUS_LINK_STATE, &LinkState, sizeof(LinkState));
}

static
const UCHAR*
IwlFindIe(
    _In_reads_bytes_(Length) const UCHAR* Ies,
    _In_ ULONG Length,
    _In_ UCHAR Id)
{
    ULONG Offset = 0;

    while (Offset + 2 <= Length)
    {
        if (Offset + 2 + Ies[Offset + 1] > Length)
            return NULL;
        if (Ies[Offset] == Id)
            return Ies + Offset;
        Offset += 2 + Ies[Offset + 1];
    }

    return NULL;
}

static
ULONG
IwlBssRateMask(
    _In_ PIWL_BSS Bss)
{
    static const UCHAR Rates[] = { 2, 4, 11, 22, 12, 18, 24, 36, 48, 72, 96, 108 };
    static const UCHAR Ids[] = { DOT11_IE_RATES, DOT11_IE_EXT_RATES };
    const UCHAR* Ie;
    ULONG Mask = 0;
    ULONG i, j, k;

    for (k = 0; k < RTL_NUMBER_OF(Ids); k++)
    {
        Ie = IwlFindIe(Bss->Ies, Bss->IeLength, Ids[k]);
        if (!Ie)
            continue;
        for (i = 0; i < Ie[1]; i++)
        {
            for (j = 0; j < RTL_NUMBER_OF(Rates); j++)
            {
                if ((Ie[2 + i] & 0x7F) == Rates[j])
                    Mask |= 1UL << j;
            }
        }
    }

    if (Bss->Channel > 14)
        Mask &= IWL_RATE_MASK_OFDM;
    return Mask;
}

static
USHORT
IwlRsnCapabilities(
    _In_ PIWL_BSS Bss)
{
    const UCHAR* Rsn = IwlFindIe(Bss->Ies, Bss->IeLength, DOT11_IE_RSN);
    ULONG Length, Offset, Count;

    if (!Rsn)
        return 0;

    Length = Rsn[1];
    Rsn += 2;
    Offset = 2 + 4;
    if (Offset + 2 > Length)
        return 0;
    Count = Rsn[Offset] | (Rsn[Offset + 1] << 8);
    Offset += 2 + Count * 4;
    if (Offset + 2 > Length)
        return 0;
    Count = Rsn[Offset] | (Rsn[Offset + 1] << 8);
    Offset += 2 + Count * 4;
    if (Offset + 2 > Length)
        return 0;
    return (USHORT)(Rsn[Offset] | (Rsn[Offset + 1] << 8));
}

static
ULONG
IwlBuildHeader(
    _In_ PIWL_ADAPTER Adapter,
    _Out_writes_bytes_(DOT11_HEADER_LENGTH) PUCHAR Frame,
    _In_ UCHAR Subtype,
    _In_reads_bytes_(IWL_ADDRESS_LENGTH) const UCHAR* Destination,
    _In_reads_bytes_(IWL_ADDRESS_LENGTH) const UCHAR* Bssid);

static
NTSTATUS
IwlSendManagement(
    _In_ PIWL_ADAPTER Adapter,
    _In_reads_bytes_(Length) const UCHAR* Frame,
    _In_ ULONG Length)
{
    UCHAR Protected[IWL_PROTECTED_MGMT_MAX];
    UCHAR Tk[16];
    ULONGLONG Pn = 0;
    BOOLEAN Protect;

    NdisAcquireSpinLock(&Adapter->Lock);
    Protect = Adapter->PmfActive && Adapter->MgmtKeyValid && !(Frame[4] & 0x01) &&
              Length + RSNA_CCMP_HDR_LEN + RSNA_CCMP_MIC_LEN <= sizeof(Protected);
    if (Protect)
    {
        Pn = ++Adapter->MgmtTxPn;
        RtlCopyMemory(Tk, Adapter->MgmtTk, sizeof(Tk));
    }
    NdisReleaseSpinLock(&Adapter->Lock);

    if (!Protect)
        return IwlTxFrame(Adapter, Frame, Length, TRUE);

    RtlCopyMemory(Protected, Frame, DOT11_HEADER_LENGTH);
    Protected[1] |= DOT11_FC1_PROTECTED;
    RsnaCcmpEncrypt(Tk, Protected, DOT11_HEADER_LENGTH, Pn, 0,
                    Frame + DOT11_HEADER_LENGTH, Length - DOT11_HEADER_LENGTH,
                    Protected + DOT11_HEADER_LENGTH);
    RtlSecureZeroMemory(Tk, sizeof(Tk));
    return IwlTxFrame(Adapter, Protected, Length + RSNA_CCMP_HDR_LEN + RSNA_CCMP_MIC_LEN, TRUE);
}

static
VOID
IwlStartSaQuery(
    _In_ PIWL_ADAPTER Adapter)
{
    UCHAR Query[DOT11_HEADER_LENGTH + 4];
    ULONG QueryLength;
    USHORT Id;

    NdisAcquireSpinLock(&Adapter->Lock);
    if (Adapter->SaQueryPending)
    {
        NdisReleaseSpinLock(&Adapter->Lock);
        return;
    }
    Id = (USHORT)(KeQueryPerformanceCounter(NULL).QuadPart ^ (KeQueryInterruptTime() >> 4));
    Adapter->SaQueryPending = TRUE;
    Adapter->SaQueryId = Id;
    Adapter->SaQueryDeadline = KeQueryInterruptTime() + IWL_SA_QUERY_TIMEOUT_100NS;
    NdisReleaseSpinLock(&Adapter->Lock);

    QueryLength = IwlBuildHeader(Adapter, Query, DOT11_SUBTYPE_ACTION, Adapter->ConnectedBssid, Adapter->ConnectedBssid);
    Query[QueryLength++] = IWL_ACTION_SA_QUERY;
    Query[QueryLength++] = IWL_SA_QUERY_REQUEST;
    Query[QueryLength++] = (UCHAR)Id;
    Query[QueryLength++] = (UCHAR)(Id >> 8);
    IwlSendManagement(Adapter, Query, QueryLength);
}

static
BOOLEAN
IwlDecryptManagement(
    _In_ PIWL_ADAPTER Adapter,
    _In_reads_bytes_(Length) const UCHAR* Frame,
    _In_ ULONG Length,
    _Out_writes_bytes_(IWL_PROTECTED_MGMT_MAX) PUCHAR Plain,
    _Out_ PULONG PlainLength)
{
    UCHAR Tk[16];
    ULONGLONG Pn = 0;
    BOOLEAN Valid;

    if (Length < DOT11_HEADER_LENGTH + RSNA_CCMP_HDR_LEN + RSNA_CCMP_MIC_LEN ||
        Length > IWL_PROTECTED_MGMT_MAX)
    {
        return FALSE;
    }

    NdisAcquireSpinLock(&Adapter->Lock);
    Valid = Adapter->PmfActive && Adapter->MgmtKeyValid &&
            RtlEqualMemory(Frame + 4, Adapter->CurrentAddress, IWL_ADDRESS_LENGTH) &&
            RtlEqualMemory(Frame + 10, Adapter->ConnectedBssid, IWL_ADDRESS_LENGTH);
    if (Valid)
        RtlCopyMemory(Tk, Adapter->MgmtTk, sizeof(Tk));
    NdisReleaseSpinLock(&Adapter->Lock);
    if (!Valid)
        return FALSE;

    Valid = RsnaCcmpDecrypt(Tk, Frame, DOT11_HEADER_LENGTH,
                            Frame + DOT11_HEADER_LENGTH, Length - DOT11_HEADER_LENGTH,
                            Plain + DOT11_HEADER_LENGTH, &Pn) == 0;
    RtlSecureZeroMemory(Tk, sizeof(Tk));
    if (!Valid)
    {
        DPRINT1("IWLBE: protected management frame failed to decrypt\n");
        return FALSE;
    }

    NdisAcquireSpinLock(&Adapter->Lock);
    Valid = Pn > Adapter->MgmtRxPn;
    if (Valid)
        Adapter->MgmtRxPn = Pn;
    NdisReleaseSpinLock(&Adapter->Lock);
    if (!Valid)
        return FALSE;

    RtlCopyMemory(Plain, Frame, DOT11_HEADER_LENGTH);
    Plain[1] &= ~DOT11_FC1_PROTECTED;
    *PlainLength = Length - RSNA_CCMP_HDR_LEN - RSNA_CCMP_MIC_LEN;
    return TRUE;
}

static
BOOLEAN
IwlVerifyBip(
    _In_ PIWL_ADAPTER Adapter,
    _In_reads_bytes_(Length) const UCHAR* Frame,
    _In_ ULONG Length)
{
    UCHAR Igtk[16];
    ULONGLONG Ipn = 0;
    USHORT KeyId = 0;
    BOOLEAN Valid;

    NdisAcquireSpinLock(&Adapter->Lock);
    Valid = Adapter->IgtkValid;
    if (Valid)
        RtlCopyMemory(Igtk, Adapter->Igtk, sizeof(Igtk));
    NdisReleaseSpinLock(&Adapter->Lock);
    if (!Valid)
        return FALSE;

    Valid = RsnaBipVerify(Igtk, Frame, Length, &KeyId, &Ipn) == 0;
    RtlSecureZeroMemory(Igtk, sizeof(Igtk));
    if (!Valid)
        return FALSE;

    NdisAcquireSpinLock(&Adapter->Lock);
    Valid = KeyId == Adapter->IgtkKeyId && Ipn > Adapter->IgtkRxIpn;
    if (Valid)
        Adapter->IgtkRxIpn = Ipn;
    NdisReleaseSpinLock(&Adapter->Lock);
    return Valid;
}

static
VOID
IwlUpdateBss(
    _In_ PIWL_ADAPTER Adapter,
    _In_reads_bytes_(Length) const UCHAR* Frame,
    _In_ ULONG Length,
    _In_ LONG Rssi)
{
    const UCHAR *Ies, *Ssid, *Ds;
    ULONG IeLength, Channel, i, Slot = MAXULONG, Oldest = 0;
    ULONGLONG Now = KeQueryInterruptTime(), OldestTime = MAXULONGLONG;
    PIWL_BSS Bss;
    BOOLEAN New;

    if (Length < DOT11_HEADER_LENGTH + 12)
        return;

    Ies = Frame + DOT11_HEADER_LENGTH + 12;
    IeLength = Length - DOT11_HEADER_LENGTH - 12;
    Ssid = IwlFindIe(Ies, IeLength, DOT11_IE_SSID);
    Ds = IwlFindIe(Ies, IeLength, DOT11_IE_DS_PARAMS);
    Channel = (Ds && Ds[1] >= 1) ? Ds[2] : Adapter->RxChannel;

    if (Ds && Ds[1] >= 1 && Channel != Adapter->RxChannel && Adapter->RxChannel <= 14)
        return;

    NdisAcquireSpinLock(&Adapter->Lock);
    for (i = 0; i < Adapter->BssCount; i++)
    {
        if (RtlEqualMemory(Adapter->Bss[i].Bssid, Frame + 16, IWL_ADDRESS_LENGTH))
        {
            Slot = i;
            break;
        }
        if (Adapter->Bss[i].LastSeen < OldestTime)
        {
            OldestTime = Adapter->Bss[i].LastSeen;
            Oldest = i;
        }
    }

    New = Slot == MAXULONG;
    if (New)
        Slot = Adapter->BssCount < IWL_MAX_BSS ? Adapter->BssCount++ : Oldest;

    Bss = &Adapter->Bss[Slot];
    RtlCopyMemory(Bss->Bssid, Frame + 16, IWL_ADDRESS_LENGTH);
    if (Ssid && Ssid[1] <= DOT11_SSID_MAX_LENGTH && (Ssid[1] || !Bss->SsidLength))
    {
        Bss->SsidLength = Ssid[1];
        RtlCopyMemory(Bss->Ssid, Ssid + 2, Ssid[1]);
    }
    Bss->Channel = Channel;
    if (New)
        Bss->Rssi = Rssi;
    RtlCopyMemory(&Bss->Timestamp, Frame + DOT11_HEADER_LENGTH, sizeof(Bss->Timestamp));
    Bss->BeaconPeriod = Frame[DOT11_HEADER_LENGTH + 8] | (Frame[DOT11_HEADER_LENGTH + 9] << 8);
    Bss->Capability = Frame[DOT11_HEADER_LENGTH + 10] | (Frame[DOT11_HEADER_LENGTH + 11] << 8);
    Bss->IeLength = min(IeLength, IWL_MAX_IE_BYTES);
    RtlCopyMemory(Bss->Ies, Ies, Bss->IeLength);
    Bss->LastSeen = Now;

    if (Adapter->LinkState == IwlLinkAssociated &&
        RtlEqualMemory(Bss->Bssid, Adapter->ConnectedBssid, IWL_ADDRESS_LENGTH))
    {
        Adapter->LastBeaconTime = Now;
    }
    NdisReleaseSpinLock(&Adapter->Lock);
}

VOID
IwlMlmeUpdateRssi(
    _In_ PIWL_ADAPTER Adapter,
    _In_reads_bytes_(IWL_ADDRESS_LENGTH) const UCHAR* Transmitter,
    _In_ LONG Rssi)
{
    ULONG i;

    NdisAcquireSpinLock(&Adapter->Lock);
    for (i = 0; i < Adapter->BssCount; i++)
    {
        if (RtlEqualMemory(Adapter->Bss[i].Bssid, Transmitter, IWL_ADDRESS_LENGTH))
            Adapter->Bss[i].Rssi = Rssi;
    }
    if (Adapter->LinkState == IwlLinkAssociated &&
        RtlEqualMemory(Transmitter, Adapter->ConnectedBssid, IWL_ADDRESS_LENGTH))
    {
        Adapter->ConnectedRssi = (Adapter->ConnectedRssi * 3 + Rssi) / 4;
    }
    NdisReleaseSpinLock(&Adapter->Lock);
}

VOID
IwlMlmeReceive(
    _In_ PIWL_ADAPTER Adapter,
    _In_reads_bytes_(Length) const UCHAR* Frame,
    _In_ ULONG Length,
    _In_ LONG Rssi,
    _In_ BOOLEAN HwDecrypted)
{
    UCHAR Plain[IWL_PROTECTED_MGMT_MAX];
    UCHAR Subtype = Frame[0] & DOT11_FC0_SUBTYPE_MASK;
    const UCHAR* Body = Frame + DOT11_HEADER_LENGTH;
    BOOLEAN Protected = HwDecrypted;
    BOOLEAN Pmf;
    ULONG PlainLength;

    if (Length < DOT11_HEADER_LENGTH)
        return;

    if (Frame[1] & DOT11_FC1_PROTECTED)
    {
        if (!IwlDecryptManagement(Adapter, Frame, Length, Plain, &PlainLength))
            return;
        Frame = Plain;
        Length = PlainLength;
        Body = Frame + DOT11_HEADER_LENGTH;
        Protected = TRUE;
    }
    Pmf = Adapter->PmfActive && Adapter->MgmtKeyValid && Adapter->LinkState == IwlLinkAssociated;

    switch (Subtype)
    {
        case DOT11_SUBTYPE_BEACON:
        case DOT11_SUBTYPE_PROBE_RESP:
            IwlUpdateBss(Adapter, Frame, Length, Rssi);
            break;

        case DOT11_SUBTYPE_AUTH:
            if (Adapter->MlmeExpect == DOT11_SUBTYPE_AUTH &&
                Length >= DOT11_HEADER_LENGTH + 6 &&
                RtlEqualMemory(Frame + 10, Adapter->ConnectedBssid, IWL_ADDRESS_LENGTH) &&
                (Body[0] | (Body[1] << 8)) == IWL_AUTH_ALGORITHM_SAE &&
                Length - DOT11_HEADER_LENGTH <= sizeof(Adapter->SaeFrame))
            {
                Adapter->SaeSequence = (USHORT)(Body[2] | (Body[3] << 8));
                Adapter->SaeFrameLength = Length - DOT11_HEADER_LENGTH;
                RtlCopyMemory(Adapter->SaeFrame, Body, Adapter->SaeFrameLength);
                Adapter->MlmeStatus = Body[4] | (Body[5] << 8);
                Adapter->MlmeExpect = 0;
                KeSetEvent(&Adapter->MlmeEvent, IO_NETWORK_INCREMENT, FALSE);
            }
            else if (Adapter->MlmeExpect == DOT11_SUBTYPE_AUTH &&
                Length >= DOT11_HEADER_LENGTH + 6 &&
                RtlEqualMemory(Frame + 10, Adapter->ConnectedBssid, IWL_ADDRESS_LENGTH) &&
                (Body[2] | (Body[3] << 8)) == 2)
            {
                Adapter->MlmeStatus = Body[4] | (Body[5] << 8);
                Adapter->MlmeExpect = 0;
                KeSetEvent(&Adapter->MlmeEvent, IO_NETWORK_INCREMENT, FALSE);
            }
            break;

        case DOT11_SUBTYPE_ASSOC_RESP:
            if (Adapter->MlmeExpect == DOT11_SUBTYPE_ASSOC_RESP &&
                Length >= DOT11_HEADER_LENGTH + 6 &&
                RtlEqualMemory(Frame + 10, Adapter->ConnectedBssid, IWL_ADDRESS_LENGTH))
            {
                Adapter->MlmeStatus = Body[2] | (Body[3] << 8);
                Adapter->MlmeAid = (Body[4] | (Body[5] << 8)) & 0x3fff;
                Adapter->AssocResponseIeLength = min(Length - DOT11_HEADER_LENGTH, IWL_MAX_IE_BYTES);
                RtlCopyMemory(Adapter->AssocResponseIes, Body, Adapter->AssocResponseIeLength);
                Adapter->MlmeExpect = 0;
                KeSetEvent(&Adapter->MlmeEvent, IO_NETWORK_INCREMENT, FALSE);
            }
            break;

        case DOT11_SUBTYPE_DEAUTH:
        case DOT11_SUBTYPE_DISASSOC:
            if (Pmf &&
                (RtlEqualMemory(Frame + 4, IwlBroadcast, IWL_ADDRESS_LENGTH) ?
                 !IwlVerifyBip(Adapter, Frame, Length) : !Protected))
            {
                DPRINT1("IWLBE: unprotected %s, querying the AP\n",
                        Subtype == DOT11_SUBTYPE_DEAUTH ? "deauthentication" : "disassociation");
                if (RtlEqualMemory(Frame + 10, Adapter->ConnectedBssid, IWL_ADDRESS_LENGTH))
                    IwlStartSaQuery(Adapter);
                break;
            }
            if (Adapter->LinkState == IwlLinkAssociated &&
                RtlEqualMemory(Frame + 10, Adapter->ConnectedBssid, IWL_ADDRESS_LENGTH) &&
                (RtlEqualMemory(Frame + 4, Adapter->CurrentAddress, IWL_ADDRESS_LENGTH) ||
                 RtlEqualMemory(Frame + 4, IwlBroadcast, IWL_ADDRESS_LENGTH)))
            {
                DPRINT1("IWLBE: AP sent %s reason %u\n",
                        Subtype == DOT11_SUBTYPE_DEAUTH ? "deauthentication" : "disassociation",
                        Length >= DOT11_HEADER_LENGTH + 2 ? (Body[0] | (Body[1] << 8)) : 0);
                NdisAcquireSpinLock(&Adapter->Lock);
                Adapter->LastBeaconTime = 0;
                NdisReleaseSpinLock(&Adapter->Lock);
                IwlWakeCommand(Adapter);
            }
            break;

        case DOT11_SUBTYPE_ACTION:
            if (Length >= DOT11_HEADER_LENGTH + 4 &&
                Body[0] == IWL_ACTION_SA_QUERY &&
                Pmf && Protected &&
                RtlEqualMemory(Frame + 10, Adapter->ConnectedBssid, IWL_ADDRESS_LENGTH))
            {
                UCHAR Response[DOT11_HEADER_LENGTH + 4];
                ULONG ResponseLength;

                if (Body[1] == IWL_SA_QUERY_REQUEST)
                {
                    ResponseLength = IwlBuildHeader(Adapter, Response, DOT11_SUBTYPE_ACTION,
                                                   Adapter->ConnectedBssid, Adapter->ConnectedBssid);
                    Response[ResponseLength++] = IWL_ACTION_SA_QUERY;
                    Response[ResponseLength++] = IWL_SA_QUERY_RESPONSE;
                    Response[ResponseLength++] = Body[2];
                    Response[ResponseLength++] = Body[3];
                    IwlSendManagement(Adapter, Response, ResponseLength);
                    DPRINT1("IWLBE: answered SA Query %02x%02x\n", Body[2], Body[3]);
                }
                else if (Body[1] == IWL_SA_QUERY_RESPONSE)
                {
                    NdisAcquireSpinLock(&Adapter->Lock);
                    if (Adapter->SaQueryPending &&
                        Adapter->SaQueryId == (USHORT)(Body[2] | (Body[3] << 8)))
                    {
                        Adapter->SaQueryPending = FALSE;
                    }
                    NdisReleaseSpinLock(&Adapter->Lock);
                }
            }
            break;
    }
}

static
VOID
IwlFreeRxNbl(
    _In_ PNET_BUFFER_LIST Nbl)
{
    PIWL_RX_PACKET Packet = Nbl->MiniportReserved[0];

    NdisFreeNetBufferList(Nbl);
    if (Packet)
    {
        NdisFreeMdl(Packet->Mdl);
        ExFreePoolWithTag(Packet, IWL_RX_CONTEXT_TAG);
    }
}

static
VOID
IwlReleaseHeldReceives(
    _In_ PIWL_ADAPTER Adapter,
    _In_ BOOLEAN Indicate)
{
    PNET_BUFFER_LIST Head, Next;
    ULONG Count;

    NdisAcquireSpinLock(&Adapter->Lock);
    Adapter->HoldRx = FALSE;
    Head = Adapter->HeldRxHead;
    Count = Adapter->HeldRxCount;
    Adapter->HeldRxHead = NULL;
    Adapter->HeldRxTail = NULL;
    Adapter->HeldRxCount = 0;
    NdisReleaseSpinLock(&Adapter->Lock);

    if (!Head)
        return;

    if (Indicate && Adapter->DataPathRunning)
    {
        InterlockedExchangeAdd(&Adapter->RxIndicated, (LONG)Count);
        NdisMIndicateReceiveNetBufferLists(Adapter->MiniportAdapterHandle,
                                           Head,
                                           NDIS_DEFAULT_PORT_NUMBER,
                                           Count,
                                           0);
        return;
    }

    for (; Head; Head = Next)
    {
        Next = NET_BUFFER_LIST_NEXT_NBL(Head);
        IwlFreeRxNbl(Head);
    }
}

VOID
IwlReceiveData(
    _In_ PIWL_ADAPTER Adapter,
    _In_reads_bytes_(Length) const UCHAR* Frame,
    _In_ ULONG Length,
    _In_ LONG Rssi,
    _In_ ULONG Rate)
{
    PNET_BUFFER_LIST Nbl;
    PIWL_RX_PACKET Packet;
    UCHAR Subtype;

    UNREFERENCED_PARAMETER(Rate);

    if ((Adapter->LinkState != IwlLinkAssociated && Adapter->LinkState != IwlLinkAssociating) ||
        !Adapter->DataPathRunning || Length < DOT11_HEADER_LENGTH + 8)
        return;

    Subtype = Frame[0] & DOT11_FC0_SUBTYPE_MASK;
    if (Subtype & 0x40)
        return;

    if (!RtlEqualMemory(Frame + 10, Adapter->ConnectedBssid, IWL_ADDRESS_LENGTH))
        return;

    Packet = ExAllocatePoolWithTag(NonPagedPool, FIELD_OFFSET(IWL_RX_PACKET, Data) + Length, IWL_RX_CONTEXT_TAG);
    if (!Packet)
    {
        Adapter->RxError++;
        return;
    }

    RtlCopyMemory(Packet->Data, Frame, Length);
    Packet->Length = Length;
    Packet->Mdl = NdisAllocateMdl(Adapter->MiniportAdapterHandle, Packet->Data, Length);
    if (!Packet->Mdl)
    {
        ExFreePoolWithTag(Packet, IWL_RX_CONTEXT_TAG);
        Adapter->RxError++;
        return;
    }

    Nbl = NdisAllocateNetBufferAndNetBufferList(Adapter->RxNblPool, 0, 0, Packet->Mdl, 0, Length);
    if (!Nbl)
    {
        NdisFreeMdl(Packet->Mdl);
        ExFreePoolWithTag(Packet, IWL_RX_CONTEXT_TAG);
        Adapter->RxError++;
        return;
    }

    RtlZeroMemory(&Packet->Context, sizeof(Packet->Context));
    Packet->Context.Header.Type = NDIS_OBJECT_TYPE_DEFAULT;
    Packet->Context.Header.Revision = DOT11_EXTSTA_RECV_CONTEXT_REVISION_1;
    Packet->Context.Header.Size = sizeof(DOT11_EXTSTA_RECV_CONTEXT);
    Packet->Context.uPhyId = 0;
    Packet->Context.uChCenterFrequency = IwlChannelToFrequency(Adapter->RxChannel ? Adapter->RxChannel : Adapter->CurrentChannel);
    Packet->Context.usNumberOfMPDUsReceived = 1;
    Packet->Context.lRSSI = Rssi;
    Packet->Context.ucDataRate = 0;

    Nbl->MiniportReserved[0] = Packet;
    Nbl->SourceHandle = Adapter->MiniportAdapterHandle;
    NET_BUFFER_LIST_INFO(Nbl, MediaSpecificInformation) = &Packet->Context;
    NET_BUFFER_LIST_STATUS(Nbl) = NDIS_STATUS_SUCCESS;

    Adapter->RxOk++;
    Adapter->RxBytes += Length;

    NdisAcquireSpinLock(&Adapter->Lock);
    if (Adapter->HoldRx || Adapter->LinkState != IwlLinkAssociated)
    {
        if (Adapter->HoldRx && Adapter->HeldRxCount < IWL_HELD_RX_MAX)
        {
            if (Adapter->HeldRxTail)
                NET_BUFFER_LIST_NEXT_NBL(Adapter->HeldRxTail) = Nbl;
            else
                Adapter->HeldRxHead = Nbl;
            Adapter->HeldRxTail = Nbl;
            Adapter->HeldRxCount++;
            Nbl = NULL;
        }
        NdisReleaseSpinLock(&Adapter->Lock);
        if (Nbl)
            IwlFreeRxNbl(Nbl);
        return;
    }
    NdisReleaseSpinLock(&Adapter->Lock);

    InterlockedIncrement(&Adapter->RxIndicated);
    NdisMIndicateReceiveNetBufferLists(Adapter->MiniportAdapterHandle,
                                       Nbl,
                                       NDIS_DEFAULT_PORT_NUMBER,
                                       1,
                                       KeGetCurrentIrql() >= DISPATCH_LEVEL ? NDIS_RECEIVE_FLAGS_DISPATCH_LEVEL : 0);
}

VOID
NTAPI
IwlMiniportReturnNetBufferLists(
    _In_ NDIS_HANDLE MiniportAdapterContext,
    _In_ PNET_BUFFER_LIST NetBufferLists,
    _In_ ULONG ReturnFlags)
{
    PIWL_ADAPTER Adapter = MiniportAdapterContext;
    PNET_BUFFER_LIST Nbl, Next;

    UNREFERENCED_PARAMETER(ReturnFlags);

    for (Nbl = NetBufferLists; Nbl; Nbl = Next)
    {
        Next = NET_BUFFER_LIST_NEXT_NBL(Nbl);
        IwlFreeRxNbl(Nbl);
        InterlockedDecrement(&Adapter->RxIndicated);
    }
}

ULONG
IwlBuildBssList(
    _In_ PIWL_ADAPTER Adapter,
    _Out_writes_bytes_opt_(BufferLength) PUCHAR Buffer,
    _In_ ULONG BufferLength,
    _Out_ PULONG BytesNeeded)
{
    ULONGLONG Now = KeQueryInterruptTime();
    PDOT11_BYTE_ARRAY Array;
    PDOT11_BSS_ENTRY Entry;
    ULONG Payload = 0, Total, i;
    PUCHAR Cursor;

    NdisAcquireSpinLock(&Adapter->Lock);
    for (i = 0; i < Adapter->BssCount; i++)
    {
        if (Now - Adapter->Bss[i].LastSeen <= IWL_BSS_EXPIRY_100NS)
            Payload += FIELD_OFFSET(DOT11_BSS_ENTRY, ucBuffer) + Adapter->Bss[i].IeLength;
    }

    Total = FIELD_OFFSET(DOT11_BYTE_ARRAY, ucBuffer) + Payload;
    *BytesNeeded = Total;
    if (!Buffer || BufferLength < Total)
    {
        NdisReleaseSpinLock(&Adapter->Lock);
        return 0;
    }

    Array = (PDOT11_BYTE_ARRAY)Buffer;
    Array->Header.Type = NDIS_OBJECT_TYPE_DEFAULT;
    Array->Header.Revision = DOT11_BSS_ENTRY_BYTE_ARRAY_REVISION_1;
    Array->Header.Size = sizeof(DOT11_BYTE_ARRAY);
    Array->uNumOfBytes = Payload;
    Array->uTotalNumOfBytes = Payload;

    Cursor = Array->ucBuffer;
    for (i = 0; i < Adapter->BssCount; i++)
    {
        PIWL_BSS Bss = &Adapter->Bss[i];

        if (Now - Bss->LastSeen > IWL_BSS_EXPIRY_100NS)
            continue;

        Entry = (PDOT11_BSS_ENTRY)Cursor;
        RtlZeroMemory(Entry, FIELD_OFFSET(DOT11_BSS_ENTRY, ucBuffer));
        Entry->uPhyId = 0;
        Entry->PhySpecificInfo.uChCenterFrequency = IwlChannelToFrequency(Bss->Channel);
        RtlCopyMemory(Entry->dot11BSSID, Bss->Bssid, IWL_ADDRESS_LENGTH);
        Entry->dot11BSSType = (Bss->Capability & 0x0002) ? dot11_BSS_type_independent : dot11_BSS_type_infrastructure;
        Entry->lRSSI = Bss->Rssi;
        Entry->uLinkQuality = Bss->Rssi >= -50 ? 100 : Bss->Rssi <= -100 ? 0 : (ULONG)(2 * (Bss->Rssi + 100));
        Entry->bInRegDomain = TRUE;
        Entry->usBeaconPeriod = Bss->BeaconPeriod ? Bss->BeaconPeriod : 100;
        Entry->ullTimestamp = Bss->Timestamp;
        Entry->ullHostTimestamp = Bss->LastSeen;
        Entry->usCapabilityInformation = Bss->Capability;
        Entry->uBufferLength = Bss->IeLength;
        RtlCopyMemory(Entry->ucBuffer, Bss->Ies, Bss->IeLength);
        Cursor += FIELD_OFFSET(DOT11_BSS_ENTRY, ucBuffer) + Bss->IeLength;
    }
    NdisReleaseSpinLock(&Adapter->Lock);

    return Total;
}

static
ULONG
IwlBuildHeader(
    _In_ PIWL_ADAPTER Adapter,
    _Out_writes_bytes_(DOT11_HEADER_LENGTH) PUCHAR Frame,
    _In_ UCHAR Subtype,
    _In_reads_bytes_(IWL_ADDRESS_LENGTH) const UCHAR* Destination,
    _In_reads_bytes_(IWL_ADDRESS_LENGTH) const UCHAR* Bssid)
{
    RtlZeroMemory(Frame, DOT11_HEADER_LENGTH);
    Frame[0] = DOT11_FC0_TYPE_MGMT | Subtype;
    RtlCopyMemory(Frame + 4, Destination, IWL_ADDRESS_LENGTH);
    RtlCopyMemory(Frame + 10, Adapter->CurrentAddress, IWL_ADDRESS_LENGTH);
    RtlCopyMemory(Frame + 16, Bssid, IWL_ADDRESS_LENGTH);
    return DOT11_HEADER_LENGTH;
}

static
ULONG
IwlAppendRates(
    _In_ PIWL_ADAPTER Adapter,
    _Out_writes_bytes_(32) PUCHAR Out)
{
    ULONG Offset = 0;

    if (Adapter->CurrentChannel > 14)
    {
        Out[Offset++] = DOT11_IE_RATES;
        Out[Offset++] = sizeof(IwlRates5G);
        RtlCopyMemory(Out + Offset, IwlRates5G, sizeof(IwlRates5G));
        return Offset + sizeof(IwlRates5G);
    }

    Out[Offset++] = DOT11_IE_RATES;
    Out[Offset++] = sizeof(IwlRates2G);
    RtlCopyMemory(Out + Offset, IwlRates2G, sizeof(IwlRates2G));
    Offset += sizeof(IwlRates2G);
    Out[Offset++] = DOT11_IE_EXT_RATES;
    Out[Offset++] = sizeof(IwlExtRates2G);
    RtlCopyMemory(Out + Offset, IwlExtRates2G, sizeof(IwlExtRates2G));
    return Offset + sizeof(IwlExtRates2G);
}

static
VOID
IwlScanChannels(
    _In_ PIWL_ADAPTER Adapter,
    _In_opt_ PDOT11_SSID Ssid)
{
    NTSTATUS Status;

    if (Adapter->Halting)
        return;
    Status = IwlFirmwareScan(Adapter, Ssid);
    if (!NT_SUCCESS(Status))
        DPRINT1("IWLBE: firmware scan failed 0x%lx\n", Status);
}

static
VOID
IwlDoScan(
    _In_ PIWL_ADAPTER Adapter)
{
    NDIS_STATUS ScanStatus = NDIS_STATUS_SUCCESS;
    DOT11_SSID Ssid;
    BOOLEAN Associated;

    NdisAcquireSpinLock(&Adapter->Lock);
    Ssid = Adapter->ScanSsid;
    Associated = Adapter->LinkState == IwlLinkAssociated;
    Adapter->Scanning = TRUE;
    NdisReleaseSpinLock(&Adapter->Lock);

    if (!Associated)
        IwlScanChannels(Adapter, Ssid.uSSIDLength ? &Ssid : NULL);

    NdisAcquireSpinLock(&Adapter->Lock);
    Adapter->Scanning = FALSE;
    NdisReleaseSpinLock(&Adapter->Lock);

    DPRINT1("IWLBE: scan done, %lu networks\n", Adapter->BssCount);
    IwlIndicateDot11Status(Adapter, NDIS_STATUS_DOT11_SCAN_CONFIRM, &ScanStatus, sizeof(ScanStatus));
}

static
BOOLEAN
IwlRsnHasAkm(
    _In_ PIWL_BSS Bss,
    _In_ UCHAR Akm)
{
    const UCHAR* Rsn = IwlFindIe(Bss->Ies, Bss->IeLength, DOT11_IE_RSN);
    ULONG Length, Offset, Count, i;

    if (!Rsn)
        return FALSE;

    Length = Rsn[1];
    Rsn += 2;
    Offset = 2 + 4;
    if (Offset + 2 > Length)
        return FALSE;
    Count = Rsn[Offset] | (Rsn[Offset + 1] << 8);
    Offset += 2 + Count * 4;
    if (Offset + 2 > Length)
        return FALSE;
    Count = Rsn[Offset] | (Rsn[Offset + 1] << 8);
    Offset += 2;
    for (i = 0; i < Count && Offset + 4 <= Length; i++, Offset += 4)
    {
        if (Rsn[Offset] == 0x00 && Rsn[Offset + 1] == 0x0f && Rsn[Offset + 2] == 0xac && Rsn[Offset + 3] == Akm)
            return TRUE;
    }
    return FALSE;
}

static
PIWL_BSS
IwlSelectBss(
    _In_ PIWL_ADAPTER Adapter,
    _Out_ PIWL_BSS Copy)
{
    ULONGLONG Now = KeQueryInterruptTime();
    PIWL_BSS Best = NULL, Bss;
    ULONG i;

    NdisAcquireSpinLock(&Adapter->Lock);
    for (i = 0; i < Adapter->BssCount; i++)
    {
        Bss = &Adapter->Bss[i];
        if (Now - Bss->LastSeen > IWL_BSS_EXPIRY_100NS)
            continue;
        if (!(Bss->Capability & 0x0001))
            continue;
        if (Adapter->HaveDesiredSsid &&
            (Bss->SsidLength != Adapter->DesiredSsid.uSSIDLength ||
             !RtlEqualMemory(Bss->Ssid, Adapter->DesiredSsid.ucSSID, Bss->SsidLength)))
        {
            continue;
        }
        if (Adapter->HaveDesiredBssid && !RtlEqualMemory(Bss->Bssid, Adapter->DesiredBssid, IWL_ADDRESS_LENGTH))
            continue;
        if (Adapter->AuthAlgorithm == DOT11_AUTH_ALGO_WPA3_SAE && !IwlRsnHasAkm(Bss, IWL_AKM_SAE))
            continue;
        if (!Best || Bss->Rssi > Best->Rssi)
            Best = Bss;
    }

    if (Best)
        *Copy = *Best;
    NdisReleaseSpinLock(&Adapter->Lock);

    return Best ? Copy : NULL;
}

static
BOOLEAN
IwlWaitMlme(
    _In_ PIWL_ADAPTER Adapter,
    _In_ ULONG Milliseconds)
{
    LARGE_INTEGER Timeout;

    Timeout.QuadPart = -(LONGLONG)Milliseconds * 10000;
    return KeWaitForSingleObject(&Adapter->MlmeEvent, Executive, KernelMode, FALSE, &Timeout) == STATUS_SUCCESS;
}

static
BOOLEAN
IwlSaeRequestParams(
    _In_ PIWL_ADAPTER Adapter,
    _In_reads_bytes_(IWL_ADDRESS_LENGTH) const UCHAR* Bssid,
    _In_ ULONG IndicationType,
    _In_reads_bytes_opt_(FrameLength) const UCHAR* Frame,
    _In_ ULONG FrameLength)
{
    UCHAR Message[NWIFI_SAE_MAX_MESSAGE];
    LARGE_INTEGER Timeout;
    ULONG Length;
    BOOLEAN Valid;

    Length = NwifiSaeBuildIndication(Message, sizeof(Message), Bssid, IndicationType,
                                     Frame, FrameLength, FALSE, 0);
    if (Length == 0)
        return FALSE;

    NdisAcquireSpinLock(&Adapter->Lock);
    Adapter->SaeParamsValid = FALSE;
    NdisReleaseSpinLock(&Adapter->Lock);
    KeClearEvent(&Adapter->SaeEvent);
    IwlIndicateDot11Status(Adapter, NDIS_STATUS_WDI_INDICATION_SAE_AUTH_PARAMS_NEEDED, Message, Length);

    Timeout.QuadPart = -(LONGLONG)IWL_SAE_PARAMS_TIMEOUT_MS * 10000;
    if (KeWaitForSingleObject(&Adapter->SaeEvent, Executive, KernelMode, FALSE, &Timeout) != STATUS_SUCCESS)
        return FALSE;

    NdisAcquireSpinLock(&Adapter->Lock);
    Valid = Adapter->SaeParamsValid &&
            RtlEqualMemory(Adapter->SaeParams.Bssid, Bssid, IWL_ADDRESS_LENGTH);
    NdisReleaseSpinLock(&Adapter->Lock);
    return Valid;
}

static
VOID
IwlSaeIndicateError(
    _In_ PIWL_ADAPTER Adapter,
    _In_reads_bytes_(IWL_ADDRESS_LENGTH) const UCHAR* Bssid,
    _In_ ULONG Status)
{
    UCHAR Message[64];
    ULONG Length;

    Length = NwifiSaeBuildIndication(Message, sizeof(Message), Bssid, WDI_SAE_INDICATION_TYPE_ERROR,
                                     NULL, 0, TRUE, Status);
    if (Length)
        IwlIndicateDot11Status(Adapter, NDIS_STATUS_WDI_INDICATION_SAE_AUTH_PARAMS_NEEDED, Message, Length);
}

static
BOOLEAN
IwlSaeExchange(
    _In_ PIWL_ADAPTER Adapter,
    _In_ PIWL_BSS Bss,
    _In_ USHORT Sequence)
{
    UCHAR Frame[DOT11_HEADER_LENGTH + 8 + 3 + NWIFI_SAE_MAX_TOKEN + NWIFI_SAE_SCALAR_LENGTH + NWIFI_SAE_ELEMENT_LENGTH];
    NWIFI_SAE_PARAMS* Params = &Adapter->SaeParams;
    ULONG Length, Attempt;

    for (Attempt = 0; Attempt < IWL_MLME_RETRIES; Attempt++)
    {
        Length = IwlBuildHeader(Adapter, Frame, DOT11_SUBTYPE_AUTH, Bss->Bssid, Bss->Bssid);
        Frame[Length++] = (UCHAR)IWL_AUTH_ALGORITHM_SAE;
        Frame[Length++] = 0;
        Frame[Length++] = (UCHAR)Sequence;
        Frame[Length++] = 0;
        Frame[Length++] = Sequence == 1 && Adapter->SaeH2e ? IWL_STATUS_SAE_H2E : 0;
        Frame[Length++] = 0;
        if (Sequence == 1)
        {
            Frame[Length++] = (UCHAR)Params->Group;
            Frame[Length++] = (UCHAR)(Params->Group >> 8);
            if (Adapter->SaeH2e && Params->TokenLength)
            {
                Frame[Length++] = IWL_EID_EXTENSION;
                Frame[Length++] = (UCHAR)(Params->TokenLength + 1);
                Frame[Length++] = IWL_EXT_ACT_CONTAINER;
            }
            RtlCopyMemory(Frame + Length, Params->Token, Params->TokenLength);
            Length += Params->TokenLength;
            RtlCopyMemory(Frame + Length, Params->Scalar, NWIFI_SAE_SCALAR_LENGTH);
            Length += NWIFI_SAE_SCALAR_LENGTH;
            RtlCopyMemory(Frame + Length, Params->Element, NWIFI_SAE_ELEMENT_LENGTH);
            Length += NWIFI_SAE_ELEMENT_LENGTH;
        }
        else
        {
            Frame[Length++] = (UCHAR)Params->SendConfirm;
            Frame[Length++] = (UCHAR)(Params->SendConfirm >> 8);
            RtlCopyMemory(Frame + Length, Params->Confirm, NWIFI_SAE_CONFIRM_LENGTH);
            Length += NWIFI_SAE_CONFIRM_LENGTH;
        }

        KeClearEvent(&Adapter->MlmeEvent);
        Adapter->SaeSequence = 0;
        Adapter->MlmeExpect = DOT11_SUBTYPE_AUTH;
        IwlTxFrame(Adapter, Frame, Length, TRUE);
        if (IwlWaitMlme(Adapter, IWL_SAE_TIMEOUT_MS) && Adapter->SaeSequence == Sequence)
            return TRUE;
    }

    Adapter->MlmeExpect = 0;
    return FALSE;
}

static
BOOLEAN
IwlAuthenticateSae(
    _In_ PIWL_ADAPTER Adapter,
    _In_ PIWL_BSS Bss)
{
    ULONG Round, RequestType;

    Adapter->SaeH2e = FALSE;
    if (!IwlSaeRequestParams(Adapter, Bss->Bssid, WDI_SAE_INDICATION_TYPE_COMMIT_REQUEST_PARAMS_NEEDED, NULL, 0))
    {
        DPRINT1("IWLBE: no SAE commit parameters\n");
        return FALSE;
    }

    for (Round = 0; Round < IWL_SAE_ROUNDS; Round++)
    {
        RequestType = Adapter->SaeParams.RequestType;
        if (RequestType == WDI_SAE_REQUEST_TYPE_SUCCESS)
        {
            DPRINT1("IWLBE: SAE authentication succeeded\n");
            return TRUE;
        }
        if ((RequestType == WDI_SAE_REQUEST_TYPE_COMMIT_PARAMS ||
             RequestType == WDI_SAE_REQUEST_TYPE_COMMIT_H2E_PARAMS) && Adapter->SaeParams.HaveCommit)
        {
            Adapter->SaeH2e = RequestType == WDI_SAE_REQUEST_TYPE_COMMIT_H2E_PARAMS;
            if (!IwlSaeExchange(Adapter, Bss, 1))
            {
                IwlSaeIndicateError(Adapter, Bss->Bssid, WDI_SAE_STATUS_TIMEOUT_WAITING_FOR_COMMIT_REQUEST);
                break;
            }
            if (!IwlSaeRequestParams(Adapter, Bss->Bssid, WDI_SAE_INDICATION_TYPE_COMMIT_FRAME,
                                     Adapter->SaeFrame, Adapter->SaeFrameLength))
            {
                break;
            }
            continue;
        }
        if (RequestType == WDI_SAE_REQUEST_TYPE_CONFIRM_PARAMS && Adapter->SaeParams.HaveConfirm)
        {
            if (!IwlSaeExchange(Adapter, Bss, 2))
            {
                IwlSaeIndicateError(Adapter, Bss->Bssid, WDI_SAE_STATUS_TIMEOUT_WAITING_FOR_CONFIRM_REQUEST);
                break;
            }
            if (!IwlSaeRequestParams(Adapter, Bss->Bssid, WDI_SAE_INDICATION_TYPE_CONFIRM_FRAME,
                                     Adapter->SaeFrame, Adapter->SaeFrameLength))
            {
                break;
            }
            continue;
        }
        DPRINT1("IWLBE: SAE authentication failed, status %lu\n",
                Adapter->SaeParams.HaveStatus ? Adapter->SaeParams.Status : 0);
        break;
    }

    return FALSE;
}

static
BOOLEAN
IwlAuthenticate(
    _In_ PIWL_ADAPTER Adapter,
    _In_ PIWL_BSS Bss)
{
    UCHAR Frame[DOT11_HEADER_LENGTH + 6];
    ULONG Length, Attempt;

    if (Adapter->AuthAlgorithm == DOT11_AUTH_ALGO_WPA3_SAE)
        return IwlAuthenticateSae(Adapter, Bss);

    for (Attempt = 0; Attempt < IWL_MLME_RETRIES; Attempt++)
    {
        Length = IwlBuildHeader(Adapter, Frame, DOT11_SUBTYPE_AUTH, Bss->Bssid, Bss->Bssid);
        Frame[Length++] = 0;
        Frame[Length++] = 0;
        Frame[Length++] = 1;
        Frame[Length++] = 0;
        Frame[Length++] = 0;
        Frame[Length++] = 0;

        KeClearEvent(&Adapter->MlmeEvent);
        Adapter->MlmeExpect = DOT11_SUBTYPE_AUTH;
        IwlTxFrame(Adapter, Frame, Length, TRUE);
        if (IwlWaitMlme(Adapter, IWL_AUTH_TIMEOUT_MS))
        {
            DPRINT1("IWLBE: authentication status %u\n", Adapter->MlmeStatus);
            return Adapter->MlmeStatus == 0;
        }
    }

    Adapter->MlmeExpect = 0;
    DPRINT1("IWLBE: no authentication response\n");
    return FALSE;
}

static
ULONG
IwlBuildRsnIe(
    _In_ PIWL_ADAPTER Adapter,
    _Out_writes_bytes_(22) PUCHAR Out)
{
    static const UCHAR Template[22] =
    {
        0x30, 0x14, 0x01, 0x00,
        0x00, 0x0f, 0xac, 0x04,
        0x01, 0x00, 0x00, 0x0f, 0xac, 0x04,
        0x01, 0x00, 0x00, 0x0f, 0xac, 0x02,
        0x00, 0x00
    };

    RtlCopyMemory(Out, Template, sizeof(Template));
    Out[7] = Adapter->MulticastCipher == DOT11_CIPHER_ALGO_TKIP ? 2 : 4;
    Out[13] = Adapter->UnicastCipher == DOT11_CIPHER_ALGO_TKIP ? 2 : 4;
    if (Adapter->MulticastCipher == DOT11_CIPHER_ALGO_CCMP &&
        Adapter->UnicastCipher == DOT11_CIPHER_ALGO_CCMP)
    {
        Out[20] = (UCHAR)IWL_RSN_CAP_MFPC;
    }
    if (Adapter->AuthAlgorithm == DOT11_AUTH_ALGO_WPA3_SAE)
    {
        Out[19] = IWL_AKM_SAE;
        Out[20] = (UCHAR)(IWL_RSN_CAP_MFPC | IWL_RSN_CAP_MFPR);
    }
    return sizeof(Template);
}

static
BOOLEAN
IwlAssociate(
    _In_ PIWL_ADAPTER Adapter,
    _In_ PIWL_BSS Bss)
{
    UCHAR Frame[DOT11_HEADER_LENGTH + 4 + 2 + DOT11_SSID_MAX_LENGTH + 32 + 24 + 3];
    ULONG Length, BodyStart, Attempt, Comebacks = 0;
    USHORT Capability;
    BOOLEAN Secure = Adapter->AuthAlgorithm == DOT11_AUTH_ALGO_RSNA_PSK ||
                     Adapter->AuthAlgorithm == DOT11_AUTH_ALGO_WPA3_SAE;

    for (Attempt = 0; Attempt < IWL_MLME_RETRIES; Attempt++)
    {
        Length = IwlBuildHeader(Adapter, Frame, DOT11_SUBTYPE_ASSOC_REQ, Bss->Bssid, Bss->Bssid);
        BodyStart = Length;
        Capability = 0x0001 | (Bss->Capability & 0x0020);
        if (Secure)
            Capability |= 0x0010;
        if (Bss->Channel <= 14)
            Capability |= Bss->Capability & 0x0400;
        Frame[Length++] = (UCHAR)Capability;
        Frame[Length++] = (UCHAR)(Capability >> 8);
        Frame[Length++] = 10;
        Frame[Length++] = 0;

        Frame[Length++] = DOT11_IE_SSID;
        Frame[Length++] = (UCHAR)Bss->SsidLength;
        RtlCopyMemory(Frame + Length, Bss->Ssid, Bss->SsidLength);
        Length += Bss->SsidLength;
        Length += IwlAppendRates(Adapter, Frame + Length);
        if (Secure)
            Length += IwlBuildRsnIe(Adapter, Frame + Length);
        if (Adapter->AuthAlgorithm == DOT11_AUTH_ALGO_WPA3_SAE && Adapter->SaeH2e)
        {
            Frame[Length++] = IWL_EID_RSNXE;
            Frame[Length++] = 1;
            Frame[Length++] = IWL_RSNXE_SAE_H2E;
        }

        Adapter->AssocRequestIeLength = Length - BodyStart;
        RtlCopyMemory(Adapter->AssocRequestIes, Frame + BodyStart, Adapter->AssocRequestIeLength);

        KeClearEvent(&Adapter->MlmeEvent);
        Adapter->MlmeExpect = DOT11_SUBTYPE_ASSOC_RESP;
        IwlTxFrame(Adapter, Frame, Length, TRUE);
        if (IwlWaitMlme(Adapter, IWL_ASSOC_TIMEOUT_MS))
        {
            const UCHAR* Timeout;
            ULONG Comeback;

            DPRINT1("IWLBE: association status %u aid %u\n", Adapter->MlmeStatus, Adapter->MlmeAid);
            if (Adapter->MlmeStatus != IWL_STATUS_REFUSED_TEMPORARILY)
                return Adapter->MlmeStatus == 0;

            Timeout = IwlFindIe(Adapter->AssocResponseIes + 6, Adapter->AssocResponseIeLength - 6,
                               DOT11_IE_TIMEOUT_INTERVAL);
            Comeback = 1000;
            if (Timeout && Timeout[1] >= 5 && Timeout[2] == IWL_TIMEOUT_COMEBACK)
            {
                Comeback = Timeout[3] | (Timeout[4] << 8) | (Timeout[5] << 16) | ((ULONG)Timeout[6] << 24);
                Comeback = (ULONG)min((ULONGLONG)Comeback * 1024 / 1000, 10000);
            }
            if (Comebacks)
                Comeback = max(Comeback, IWL_SA_QUERY_MAX_TIMEOUT_MS);
            DPRINT1("IWLBE: association comeback in %lu ms\n", Comeback);
            IwlSleep(Comeback);
            if (++Comebacks < IWL_ASSOC_COMEBACK_MAX)
                Attempt--;
        }
    }

    Adapter->MlmeExpect = 0;
    DPRINT1("IWLBE: no association response\n");
    return FALSE;
}

static
VOID
IwlIndicateConnectionStart(
    _In_ PIWL_ADAPTER Adapter)
{
    DOT11_CONNECTION_START_PARAMETERS Params;

    RtlZeroMemory(&Params, sizeof(Params));
    Params.Header.Type = NDIS_OBJECT_TYPE_DEFAULT;
    Params.Header.Revision = DOT11_CONNECTION_START_PARAMETERS_REVISION_1;
    Params.Header.Size = sizeof(Params);
    Params.BSSType = dot11_BSS_type_infrastructure;
    IwlIndicateDot11Status(Adapter, NDIS_STATUS_DOT11_CONNECTION_START, &Params, sizeof(Params));
}

static
VOID
IwlIndicateAssociationStart(
    _In_ PIWL_ADAPTER Adapter,
    _In_ PIWL_BSS Bss)
{
    DOT11_ASSOCIATION_START_PARAMETERS Params;

    RtlZeroMemory(&Params, sizeof(Params));
    Params.Header.Type = NDIS_OBJECT_TYPE_DEFAULT;
    Params.Header.Revision = DOT11_ASSOCIATION_START_PARAMETERS_REVISION_1;
    Params.Header.Size = sizeof(Params);
    RtlCopyMemory(Params.MacAddr, Bss->Bssid, IWL_ADDRESS_LENGTH);
    Params.SSID.uSSIDLength = Bss->SsidLength;
    RtlCopyMemory(Params.SSID.ucSSID, Bss->Ssid, Bss->SsidLength);
    IwlIndicateDot11Status(Adapter, NDIS_STATUS_DOT11_ASSOCIATION_START, &Params, sizeof(Params));
}

static
VOID
IwlIndicateAssociationCompletion(
    _In_ PIWL_ADAPTER Adapter,
    _In_reads_bytes_(IWL_ADDRESS_LENGTH) const UCHAR* Bssid,
    _In_ DOT11_ASSOC_STATUS Status)
{
    PDOT11_ASSOCIATION_COMPLETION_PARAMETERS Params;
    ULONG Size;
    PUCHAR Tail;

    Size = sizeof(*Params) + Adapter->AssocRequestIeLength + Adapter->AssocResponseIeLength;
    Params = IwlAllocate(Size);
    if (!Params)
        return;

    Params->Header.Type = NDIS_OBJECT_TYPE_DEFAULT;
    Params->Header.Revision = DOT11_ASSOCIATION_COMPLETION_PARAMETERS_REVISION_1;
    Params->Header.Size = sizeof(*Params);
    RtlCopyMemory(Params->MacAddr, Bssid, IWL_ADDRESS_LENGTH);
    Params->uStatus = Status;

    if (Status == DOT11_ASSOC_STATUS_SUCCESS)
    {
        Params->bReAssocReq = FALSE;
        Params->bReAssocResp = FALSE;
        Params->AuthAlgo = Adapter->AuthAlgorithm;
        Params->UnicastCipher = Adapter->UnicastCipher;
        Params->MulticastCipher = Adapter->MulticastCipher;
        Params->bFourAddressSupported = FALSE;
        Params->bPortAuthorized = Adapter->AuthAlgorithm == DOT11_AUTH_ALGO_80211_OPEN;
        Params->DSInfo = DOT11_DS_UNKNOWN;
        Params->uEncapTableOffset = 0;
        Params->uEncapTableSize = 0;
        Tail = (PUCHAR)(Params + 1);
        Params->uAssocReqOffset = sizeof(*Params);
        Params->uAssocReqSize = Adapter->AssocRequestIeLength;
        RtlCopyMemory(Tail, Adapter->AssocRequestIes, Adapter->AssocRequestIeLength);
        Params->uAssocRespOffset = sizeof(*Params) + Adapter->AssocRequestIeLength;
        Params->uAssocRespSize = Adapter->AssocResponseIeLength;
        RtlCopyMemory(Tail + Adapter->AssocRequestIeLength, Adapter->AssocResponseIes, Adapter->AssocResponseIeLength);
    }
    else
    {
        Size = sizeof(*Params);
    }

    IwlIndicateDot11Status(Adapter, NDIS_STATUS_DOT11_ASSOCIATION_COMPLETION, Params, Size);
    IwlFree(Params);
}

static
VOID
IwlIndicateConnectionCompletion(
    _In_ PIWL_ADAPTER Adapter,
    _In_ DOT11_ASSOC_STATUS Status)
{
    DOT11_CONNECTION_COMPLETION_PARAMETERS Params;

    RtlZeroMemory(&Params, sizeof(Params));
    Params.Header.Type = NDIS_OBJECT_TYPE_DEFAULT;
    Params.Header.Revision = DOT11_CONNECTION_COMPLETION_PARAMETERS_REVISION_1;
    Params.Header.Size = sizeof(Params);
    Params.uStatus = Status;
    IwlIndicateDot11Status(Adapter, NDIS_STATUS_DOT11_CONNECTION_COMPLETION, &Params, sizeof(Params));
}

static
VOID
IwlIndicateDisassociation(
    _In_ PIWL_ADAPTER Adapter,
    _In_reads_bytes_(IWL_ADDRESS_LENGTH) const UCHAR* Bssid,
    _In_ ULONG Reason)
{
    DOT11_DISASSOCIATION_PARAMETERS Params;

    RtlZeroMemory(&Params, sizeof(Params));
    Params.Header.Type = NDIS_OBJECT_TYPE_DEFAULT;
    Params.Header.Revision = DOT11_DISASSOCIATION_PARAMETERS_REVISION_1;
    Params.Header.Size = sizeof(Params);
    RtlCopyMemory(Params.MacAddr, Bssid, IWL_ADDRESS_LENGTH);
    Params.uReason = Reason;
    IwlIndicateDot11Status(Adapter, NDIS_STATUS_DOT11_DISASSOCIATION, &Params, sizeof(Params));
}

static
VOID
IwlLeave(
    _In_ PIWL_ADAPTER Adapter,
    _In_ BOOLEAN SendDeauth,
    _In_ ULONG Reason)
{
    UCHAR Frame[DOT11_HEADER_LENGTH + 2];
    UCHAR Bssid[IWL_ADDRESS_LENGTH];
    ULONG Length;

    if (Adapter->LinkState != IwlLinkAssociated)
        return;

    RtlCopyMemory(Bssid, Adapter->ConnectedBssid, IWL_ADDRESS_LENGTH);

    if (SendDeauth)
    {
        Length = IwlBuildHeader(Adapter, Frame, DOT11_SUBTYPE_DEAUTH, Bssid, Bssid);
        Frame[Length++] = 3;
        Frame[Length++] = 0;
        IwlSendManagement(Adapter, Frame, Length);
        IwlSleep(20);
    }

    NdisAcquireSpinLock(&Adapter->Lock);
    Adapter->LinkState = IwlLinkIdle;
    Adapter->PmfActive = FALSE;
    Adapter->SoftwareCrypto = FALSE;
    Adapter->SaQueryPending = FALSE;
    NdisReleaseSpinLock(&Adapter->Lock);

    IwlClearKeys(Adapter);
    IwlVifSetLink(Adapter, NULL, FALSE, 0, 0);
    IwlSetRxFilter(Adapter, TRUE);

    IwlIndicateDisassociation(Adapter, Bssid, Reason);
    IwlIndicateLinkState(Adapter, FALSE);
    DPRINT1("IWLBE: left %02x:%02x:%02x:%02x:%02x:%02x reason %lx\n",
            Bssid[0], Bssid[1], Bssid[2], Bssid[3], Bssid[4], Bssid[5], Reason);
}

static
VOID
IwlDoConnect(
    _In_ PIWL_ADAPTER Adapter)
{
    DOT11_ASSOC_STATUS Status = DOT11_ASSOC_STATUS_FAILURE;
    IWL_BSS Candidate;
    PIWL_BSS Bss;
    ULONG Join;

    IwlLeave(Adapter, TRUE, DOT11_DISASSOC_REASON_OS);
    IwlIndicateConnectionStart(Adapter);

    Bss = IwlSelectBss(Adapter, &Candidate);
    if (!Bss)
    {
        NdisAcquireSpinLock(&Adapter->Lock);
        Adapter->ScanSsid = Adapter->DesiredSsid;
        NdisReleaseSpinLock(&Adapter->Lock);
        IwlScanChannels(Adapter, Adapter->HaveDesiredSsid ? &Adapter->DesiredSsid : NULL);
        Bss = IwlSelectBss(Adapter, &Candidate);
    }

    if (!Bss)
    {
        DPRINT1("IWLBE: desired network not found\n");
        IwlIndicateConnectionCompletion(Adapter, DOT11_ASSOC_STATUS_UNREACHABLE);
        return;
    }

    DPRINT1("IWLBE: joining %.*s on channel %lu rssi %ld\n", Bss->SsidLength, Bss->Ssid, Bss->Channel, Bss->Rssi);

    IwlSetChannel(Adapter, (UCHAR)Bss->Channel);
    IwlVifSetLink(Adapter, Bss->Bssid, FALSE, 0, Bss->BeaconPeriod);

    NdisAcquireSpinLock(&Adapter->Lock);
    RtlCopyMemory(Adapter->ConnectedBssid, Bss->Bssid, IWL_ADDRESS_LENGTH);
    Adapter->ConnectedChannel = Bss->Channel;
    Adapter->LinkState = IwlLinkAuthenticating;
    NdisReleaseSpinLock(&Adapter->Lock);

    IwlIndicateAssociationStart(Adapter, Bss);

    for (Join = 0; Join < IWL_JOIN_ATTEMPTS && Status != DOT11_ASSOC_STATUS_SUCCESS && !Adapter->Halting; Join++)
    {
        NdisAcquireSpinLock(&Adapter->Lock);
        Adapter->LinkState = IwlLinkAuthenticating;
        NdisReleaseSpinLock(&Adapter->Lock);
        if (!IwlAuthenticate(Adapter, Bss))
            continue;
        NdisAcquireSpinLock(&Adapter->Lock);
        Adapter->HoldRx = TRUE;
        Adapter->LinkState = IwlLinkAssociating;
        NdisReleaseSpinLock(&Adapter->Lock);
        if (IwlAssociate(Adapter, Bss))
            Status = DOT11_ASSOC_STATUS_SUCCESS;
    }

    if (Status == DOT11_ASSOC_STATUS_SUCCESS)
    {
        NdisAcquireSpinLock(&Adapter->Lock);
        Adapter->AssociationId = Adapter->MlmeAid;
        Adapter->ConnectedRssi = Bss->Rssi;
        Adapter->LastBeaconTime = KeQueryInterruptTime();
        Adapter->LinkState = IwlLinkAssociated;
        Adapter->PmfActive = (Adapter->AuthAlgorithm == DOT11_AUTH_ALGO_RSNA_PSK ||
                              Adapter->AuthAlgorithm == DOT11_AUTH_ALGO_WPA3_SAE) &&
                             Adapter->MulticastCipher == DOT11_CIPHER_ALGO_CCMP &&
                             Adapter->UnicastCipher == DOT11_CIPHER_ALGO_CCMP &&
                             (IwlRsnCapabilities(Bss) & IWL_RSN_CAP_MFPC) != 0;
        Adapter->SoftwareCrypto = Adapter->AuthAlgorithm == DOT11_AUTH_ALGO_RSNA_PSK ||
                                  Adapter->AuthAlgorithm == DOT11_AUTH_ALGO_WPA3_SAE;
        NdisReleaseSpinLock(&Adapter->Lock);
        DPRINT1("IWLBE: management frame protection %s\n", Adapter->PmfActive ? "active" : "off");

        IwlVifSetLink(Adapter, Bss->Bssid, TRUE, Adapter->AssociationId, Bss->BeaconPeriod);
        IwlVifSetRates(Adapter, IwlBssRateMask(Bss));
        IwlSetRxFilter(Adapter, FALSE);
        IwlIndicateAssociationCompletion(Adapter, Bss->Bssid, DOT11_ASSOC_STATUS_SUCCESS);
        IwlReleaseHeldReceives(Adapter, TRUE);
        IwlIndicateConnectionCompletion(Adapter, DOT11_ASSOC_STATUS_SUCCESS);
        IwlIndicateLinkState(Adapter, TRUE);
        return;
    }

    NdisAcquireSpinLock(&Adapter->Lock);
    Adapter->LinkState = IwlLinkIdle;
    NdisReleaseSpinLock(&Adapter->Lock);
    IwlReleaseHeldReceives(Adapter, FALSE);
    IwlVifSetLink(Adapter, NULL, FALSE, 0, 0);
    IwlIndicateAssociationCompletion(Adapter, Bss->Bssid, DOT11_ASSOC_STATUS_FAILURE);
    IwlIndicateConnectionCompletion(Adapter, DOT11_ASSOC_STATUS_FAILURE);
}

static
VOID
IwlCheckLink(
    _In_ PIWL_ADAPTER Adapter)
{
    ULONGLONG Now = KeQueryInterruptTime(), Last;
    BOOLEAN Lost;

    NdisAcquireSpinLock(&Adapter->Lock);
    Last = Adapter->LastBeaconTime;
    if (Adapter->SaQueryPending && Now > Adapter->SaQueryDeadline)
    {
        Adapter->SaQueryPending = FALSE;
        Last = 0;
    }
    Lost = Adapter->LinkState == IwlLinkAssociated &&
           (Last == 0 || Now - Last > (ULONGLONG)IWL_BEACON_LOSS_MS * 10000);
    NdisReleaseSpinLock(&Adapter->Lock);

    if (Lost)
        IwlLeave(Adapter, Last != 0, Last == 0 ? DOT11_ASSOC_STATUS_PEER_DEAUTHENTICATED : DOT11_ASSOC_STATUS_UNREACHABLE);

}

VOID
NTAPI
IwlCommandThread(
    _In_ PVOID Context)
{
    PIWL_ADAPTER Adapter = Context;
    LARGE_INTEGER Timeout;
    BOOLEAN Scan, Connect, Disconnect;

    Timeout.QuadPart = -10000000LL;

    while (!Adapter->CommandStop)
    {
        KeWaitForSingleObject(&Adapter->CommandEvent, Executive, KernelMode, FALSE, &Timeout);
        if (Adapter->CommandStop)
            break;

        NdisAcquireSpinLock(&Adapter->Lock);
        Scan = Adapter->ScanQueued;
        Connect = Adapter->ConnectQueued;
        Disconnect = Adapter->DisconnectQueued;
        Adapter->ScanQueued = FALSE;
        Adapter->ConnectQueued = FALSE;
        Adapter->DisconnectQueued = FALSE;
        NdisReleaseSpinLock(&Adapter->Lock);

        if (!Adapter->HardwareReady)
        {
            if (Scan)
            {
                NDIS_STATUS ScanStatus = NDIS_STATUS_FAILURE;
                IwlIndicateDot11Status(Adapter, NDIS_STATUS_DOT11_SCAN_CONFIRM, &ScanStatus, sizeof(ScanStatus));
            }
            if (Connect)
                IwlIndicateConnectionCompletion(Adapter, DOT11_ASSOC_STATUS_FAILURE);
            continue;
        }

        if (Disconnect)
            IwlLeave(Adapter, TRUE, DOT11_DISASSOC_REASON_OS);
        if (Connect)
            IwlDoConnect(Adapter);
        if (Scan)
            IwlDoScan(Adapter);

        IwlCheckLink(Adapter);
    }

    IwlLeave(Adapter, TRUE, DOT11_DISASSOC_REASON_OS);
    KeSetEvent(&Adapter->CommandStopped, IO_NO_INCREMENT, FALSE);
    PsTerminateSystemThread(STATUS_SUCCESS);
}
