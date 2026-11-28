/*
 * PROJECT:     LiberNT Intel BE200 Native 802.11 Miniport
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Firmware notifications, receive path and transmit encryption
 * COPYRIGHT:   Copyright 2026 The LiberNT contributors
 */

#include "iwlbe.h"
#include <rsna_ccmp.h>

#define NDEBUG
#include <debug.h>

#define IWL_RX_MIC_CRC_LEN(Flags1)      ((((Flags1) & 0xF0) >> 4) << 1)
#define IWL_RX_BAND(Value)              (((Value) & 0xC0) >> 6)
#define IWL_MIN_FRAME_LENGTH            10

static
BOOLEAN
IwlDecryptData(
    _In_ PIWL_ADAPTER Adapter,
    _Inout_updates_bytes_(*Length) PUCHAR Frame,
    _Inout_ PULONG Length)
{
    ULONG HeaderLength = IwlFrameHeaderLength(Frame, *Length);
    PUCHAR Body = Frame + HeaderLength;
    BOOLEAN Group = (Frame[4] & 0x01) != 0;
    ULONGLONG Pn = 0, *LastPn;
    UCHAR KeyId, Tk[16];
    BOOLEAN Valid;

    if (!HeaderLength || *Length < HeaderLength + RSNA_CCMP_HDR_LEN + RSNA_CCMP_MIC_LEN)
        return FALSE;
    KeyId = Body[3] >> 6;

    NdisAcquireSpinLock(&Adapter->Lock);
    Valid = Adapter->SoftwareCrypto && (Group ? Adapter->GroupKeyValid[KeyId] : Adapter->MgmtKeyValid);
    if (Valid)
        RtlCopyMemory(Tk, Group ? Adapter->GroupKey[KeyId] : Adapter->MgmtTk, sizeof(Tk));
    NdisReleaseSpinLock(&Adapter->Lock);
    if (!Valid)
        return FALSE;

    Valid = RsnaCcmpDecrypt(Tk, Frame, HeaderLength, Body, *Length - HeaderLength, Body, &Pn) == 0;
    RtlSecureZeroMemory(Tk, sizeof(Tk));
    if (!Valid)
        return FALSE;

    NdisAcquireSpinLock(&Adapter->Lock);
    LastPn = Group ? &Adapter->GroupRxPn[KeyId] : &Adapter->DataRxPn;
    Valid = Pn > *LastPn;
    if (Valid)
        *LastPn = Pn;
    NdisReleaseSpinLock(&Adapter->Lock);
    if (!Valid)
        return FALSE;

    Frame[1] &= ~DOT11_FC1_PROTECTED;
    *Length -= RSNA_CCMP_HDR_LEN + RSNA_CCMP_MIC_LEN;
    return TRUE;
}

static
VOID
IwlHandleRxMpdu(
    _In_ PIWL_ADAPTER Adapter,
    _In_reads_bytes_(Length) PUCHAR Payload,
    _In_ ULONG Length)
{
    PUCHAR Frame;
    ULONG FrameLength, Status, MicCrc;
    UCHAR Band, Channel;
    LONG Rssi;

    if (Length < IWL_RX_MPDU_DESC_SIZE)
        return;

    FrameLength = IwlLe16(Payload + IWL_RX_MPDU_LEN);
    if (FrameLength > Length - IWL_RX_MPDU_DESC_SIZE)
    {
        Adapter->RxError++;
        return;
    }

    Status = IwlLe32(Payload + IWL_RX_MPDU_STATUS);
    if (!(Status & IWL_RX_MPDU_STATUS_CRC_OK) || !(Status & IWL_RX_MPDU_STATUS_OVERRUN_OK))
    {
        Adapter->RxError++;
        return;
    }

    Band = (UCHAR)IWL_RX_BAND(Payload[IWL_RX_MPDU_PHY_BAND]);
    Channel = Payload[IWL_RX_MPDU_CHANNEL];
    if (Band != IWL_PHY_BAND_24 && Band != IWL_PHY_BAND_5)
        return;

    Frame = Payload + IWL_RX_MPDU_DESC_SIZE;
    if (FrameLength < IWL_MIN_FRAME_LENGTH)
        return;

    /* The device inserts 2 bytes of padding after a header that is not a multiple of 4 */
    if (Payload[IWL_RX_MPDU_FLAGS2] & IWL_RX_MPDU_FLAGS2_PAD)
    {
        ULONG HeaderLength = IwlFrameHeaderLength(Frame, FrameLength);

        if (!HeaderLength || FrameLength < HeaderLength + 2)
            return;
        RtlMoveMemory(Frame + 2, Frame, HeaderLength);
        Frame += 2;
        FrameLength -= 2;
    }

    MicCrc = IWL_RX_MIC_CRC_LEN(Payload[IWL_RX_MPDU_FLAGS1]);
    if (FrameLength > MicCrc)
        FrameLength -= MicCrc;
    if (FrameLength < IWL_MIN_FRAME_LENGTH)
        return;

    Rssi = -(LONG)max(Payload[IWL_RX_MPDU_ENERGY_A], Payload[IWL_RX_MPDU_ENERGY_B]);
    if (Rssi == 0)
        Rssi = -100;
    Adapter->RxChannel = Channel;

    switch (Frame[0] & DOT11_FC0_TYPE_MASK)
    {
        case DOT11_FC0_TYPE_MGMT:
            IwlMlmeReceive(Adapter, Frame, FrameLength, Rssi, FALSE);
            break;

        case DOT11_FC0_TYPE_DATA:
            if ((Frame[1] & DOT11_FC1_PROTECTED) && !IwlDecryptData(Adapter, Frame, &FrameLength))
            {
                Adapter->RxError++;
                break;
            }
            IwlReceiveData(Adapter, Frame, FrameLength, Rssi, 0);
            break;

        default:
            break;
    }
}

static
VOID
IwlHandleTxResponse(
    _In_ PIWL_ADAPTER Adapter,
    _In_reads_bytes_(Length) const UCHAR *Payload,
    _In_ ULONG Length)
{
    ULONG FrameCount, Status;
    USHORT Queue, Ssn;

    if (Length < IWL_TX_RESP_STATUS + 4)
        return;

    FrameCount = Payload[IWL_TX_RESP_FRAME_COUNT];
    Queue = IwlLe16(Payload + IWL_TX_RESP_QUEUE);
    Status = IwlLe32(Payload + IWL_TX_RESP_STATUS) & IWL_TX_STATUS_MASK;
    if (Status != IWL_TX_STATUS_SUCCESS)
        Adapter->TxError++;

    if (Length < IWL_TX_RESP_STATUS + 4 * FrameCount + 4)
        return;
    Ssn = (USHORT)(IwlLe32(Payload + IWL_TX_RESP_STATUS + 4 * FrameCount) & 0xFFFF);
    IwlReclaimTx(Adapter, Queue, Ssn);
}

VOID
IwlHandlePacket(
    _In_ PIWL_ADAPTER Adapter,
    _In_reads_bytes_(Length) PUCHAR Packet,
    _In_ ULONG Length)
{
    UCHAR Command = Packet[4], Group = Packet[5];
    PUCHAR Payload = Packet + 8;
    ULONG PayloadLength = Length - 8;

    if (Length < 8)
        return;

    if (Group == IWL_GRP_LEGACY || Group == IWL_GRP_LONG)
    {
        switch (Command)
        {
            case IWL_CMD_RX_MPDU:
                IwlHandleRxMpdu(Adapter, Payload, PayloadLength);
                return;

            case IWL_CMD_TX:
                IwlHandleTxResponse(Adapter, Payload, PayloadLength);
                return;

            case IWL_CMD_SCAN_ITERATION_COMPLETE:
            case IWL_CMD_SCAN_COMPLETE_UMAC:
                return;

            default:
                break;
        }
    }
    else if (Group == IWL_GRP_MAC_CONF)
    {
        switch (Command)
        {
            case IWL_CMD_MISSED_BEACONS:
                DPRINT1("IWLBE: firmware reports missed beacons\n");
                return;

            case IWL_CMD_SESSION_PROTECTION_NOTIF:
                return;

            default:
                break;
        }
    }

    DPRINT("IWLBE: unhandled notification group %u command 0x%02x length %lu\n",
           Group, Command, PayloadLength);
}

static
VOID
IwlEncryptData(
    _In_ PIWL_ADAPTER Adapter,
    _Inout_updates_bytes_(*Length + RSNA_CCMP_HDR_LEN + RSNA_CCMP_MIC_LEN) PUCHAR Frame,
    _Inout_ PULONG Length)
{
    ULONG HeaderLength, BodyLength;
    ULONGLONG Pn = 0;
    UCHAR Tk[16];
    BOOLEAN Valid;

    if ((Frame[0] & DOT11_FC0_TYPE_MASK) != DOT11_FC0_TYPE_DATA || (Frame[4] & 0x01))
        return;

    HeaderLength = IwlFrameHeaderLength(Frame, *Length);
    if (!HeaderLength)
        return;

    NdisAcquireSpinLock(&Adapter->Lock);
    Valid = Adapter->SoftwareCrypto && Adapter->MgmtKeyValid;
    if (Valid)
    {
        Pn = ++Adapter->MgmtTxPn;
        RtlCopyMemory(Tk, Adapter->MgmtTk, sizeof(Tk));
    }
    NdisReleaseSpinLock(&Adapter->Lock);
    if (!Valid)
        return;

    BodyLength = *Length - HeaderLength;
    RtlMoveMemory(Frame + HeaderLength + RSNA_CCMP_HDR_LEN, Frame + HeaderLength, BodyLength);
    Frame[1] |= DOT11_FC1_PROTECTED;
    RsnaCcmpEncrypt(Tk, Frame, HeaderLength, Pn, 0,
                    Frame + HeaderLength + RSNA_CCMP_HDR_LEN, BodyLength,
                    Frame + HeaderLength);
    RtlSecureZeroMemory(Tk, sizeof(Tk));
    *Length += RSNA_CCMP_HDR_LEN + RSNA_CCMP_MIC_LEN;
}

NTSTATUS
IwlSendNetBuffer(
    _In_ PIWL_ADAPTER Adapter,
    _In_ PNET_BUFFER NetBuffer,
    _In_ BOOLEAN Exempt)
{
    ULONG Length = NET_BUFFER_DATA_LENGTH(NetBuffer);
    PUCHAR Storage = Adapter->TxFrame, Data;

    if (Length < DOT11_HEADER_LENGTH || Length > IWL_MAX_80211_FRAME)
        return STATUS_INVALID_PARAMETER;

    Data = NdisGetDataBuffer(NetBuffer, Length, Storage, 1, 0);
    if (!Data)
        return STATUS_INVALID_PARAMETER;
    if (Data != Storage)
        RtlCopyMemory(Storage, Data, Length);

    if (!Exempt)
        IwlEncryptData(Adapter, Storage, &Length);

    Adapter->TxBytes += Length;
    return IwlTxFrame(Adapter, Storage, Length, FALSE);
}
