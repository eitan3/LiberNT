/*
 * PROJECT:     LiberNT Intel BE200 Native 802.11 Miniport
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Firmware MAC, link, PHY and station contexts, scanning and frame transmit
 * COPYRIGHT:   Copyright 2026 The LiberNT contributors
 */

#include "iwlbe.h"

#define NDEBUG
#include <debug.h>

#define IWL_TX_RETRY_MS                 20

static const UCHAR IwlProbeRates2G[] = { DOT11_IE_RATES, 8, 0x82, 0x84, 0x8b, 0x96, 0x0c, 0x12, 0x18, 0x24,
                                         DOT11_IE_EXT_RATES, 4, 0x30, 0x48, 0x60, 0x6c };
static const UCHAR IwlProbeRates5G[] = { DOT11_IE_RATES, 8, 0x8c, 0x12, 0x98, 0x24, 0xb0, 0x48, 0x60, 0x6c };
static const UCHAR IwlTpcReport[] = { DOT11_IE_VENDOR, 7, 0x00, 0x50, 0xf2, 0x08, 0x00, 0x00, 0x00 };

/* Default EDCA parameters, by firmware access category BE, BK, VI, VO */
static const struct
{
    USHORT CwMin;
    USHORT CwMax;
    UCHAR Aifsn;
    UCHAR Fifo;
    USHORT TxopUs;
} IwlDefaultEdca[4] =
{
    { 15, 1023, 3, 1, 0 },
    { 15, 1023, 7, 0, 0 },
    { 7, 15, 2, 2, 3008 },
    { 3, 7, 2, 3, 1504 },
};

static
UCHAR
IwlBandOf(
    _In_ ULONG Channel)
{
    return Channel > 14 ? IWL_PHY_BAND_5 : IWL_PHY_BAND_24;
}

static
NTSTATUS
IwlSendPhyContext(
    _In_ PIWL_ADAPTER Adapter,
    _In_ ULONG Action,
    _In_ UCHAR Channel)
{
    UCHAR Cmd[IWL_PHY_CMD_SIZE];

    RtlZeroMemory(Cmd, sizeof(Cmd));
    IwlPutLe32(Cmd + IWL_PHY_ID_AND_COLOR, IWL_PHY_ID);
    IwlPutLe32(Cmd + IWL_PHY_ACTION, Action);
    IwlPutLe32(Cmd + IWL_PHY_CHANNEL, Channel);
    Cmd[IWL_PHY_BAND] = IwlBandOf(Channel);
    Cmd[IWL_PHY_WIDTH] = 0;
    Cmd[IWL_PHY_CTRL_POS] = 0;
    Cmd[IWL_PHY_SECONDARY_CTRL] = IWL_PHY_CTRL_POS_ABOVE;
    return IwlSendCommand(Adapter, IWL_GRP_LEGACY, IWL_CMD_PHY_CONTEXT, Cmd, sizeof(Cmd), NULL, 0, NULL);
}

static
NTSTATUS
IwlSendMac(
    _In_ PIWL_ADAPTER Adapter,
    _In_ ULONG Action)
{
    UCHAR Cmd[IWL_MAC_CMD_SIZE];
    BOOLEAN Associated = Adapter->Aid != 0;

    RtlZeroMemory(Cmd, sizeof(Cmd));
    IwlPutLe32(Cmd + IWL_MAC_ID_AND_COLOR, IWL_MAC_ID);
    IwlPutLe32(Cmd + IWL_MAC_ACTION, Action);
    IwlPutLe32(Cmd + IWL_MAC_TYPE, IWL_MAC_TYPE_BSS_STA);
    RtlCopyMemory(Cmd + IWL_MAC_ADDRESS, Adapter->CurrentAddress, IWL_ADDRESS_LENGTH);
    IwlPutLe32(Cmd + IWL_MAC_FILTER_FLAGS, IWL_MAC_FILTER_ACCEPT_GRP | IWL_MAC_FILTER_IN_BEACON);
    Cmd[IWL_MAC_CLIENT_IS_ASSOC] = Associated ? 1 : 0;
    IwlPutLe16(Cmd + IWL_MAC_CLIENT_ASSOC_ID, Adapter->Aid);
    return IwlSendCommand(Adapter, IWL_GRP_MAC_CONF, IWL_CMD_MAC_CONFIG, Cmd, sizeof(Cmd), NULL, 0, NULL);
}

static
NTSTATUS
IwlSendLink(
    _In_ PIWL_ADAPTER Adapter,
    _In_ ULONG Action,
    _In_ BOOLEAN Active,
    _In_ ULONG ModifyMask)
{
    UCHAR Cmd[IWL_LINK_CMD_SIZE];
    PUCHAR Ac;
    ULONG i;

    RtlZeroMemory(Cmd, sizeof(Cmd));
    IwlPutLe32(Cmd + IWL_LINK_ACTION, Action);
    IwlPutLe32(Cmd + IWL_LINK_ID, IWL_LINK_FW_ID);
    IwlPutLe32(Cmd + IWL_LINK_MAC_ID, IWL_MAC_ID);
    IwlPutLe32(Cmd + IWL_LINK_PHY_ID, Active ? IWL_PHY_ID : IWL_FW_CTXT_INVALID);
    RtlCopyMemory(Cmd + IWL_LINK_ADDRESS, Adapter->CurrentAddress, IWL_ADDRESS_LENGTH);
    Cmd[IWL_LINK_SPEC_ID] = 0;

    if (Action == IWL_FW_CTXT_ACTION_MODIFY)
    {
        IwlPutLe32(Cmd + IWL_LINK_MODIFY_MASK, ModifyMask);
        IwlPutLe32(Cmd + IWL_LINK_ACTIVE, Active ? 1 : 0);
        IwlPutLe32(Cmd + IWL_LINK_CCK_RATES, Adapter->CurrentChannel > 14 ? 0 : 0x0F);
        IwlPutLe32(Cmd + IWL_LINK_OFDM_RATES, 0x15);
        IwlPutLe32(Cmd + IWL_LINK_SHORT_SLOT, Adapter->CurrentChannel > 14 ? 1 : 0);
        for (i = 0; i < 4; i++)
        {
            Ac = Cmd + IWL_LINK_AC + i * IWL_LINK_AC_SIZE;
            IwlPutLe16(Ac + 0, IwlDefaultEdca[i].CwMin);
            IwlPutLe16(Ac + 2, IwlDefaultEdca[i].CwMax);
            Ac[4] = IwlDefaultEdca[i].Aifsn;
            Ac[5] = (UCHAR)IWL_BIT(IwlDefaultEdca[i].Fifo);
            IwlPutLe16(Ac + 6, IwlDefaultEdca[i].TxopUs);
        }
        IwlPutLe32(Cmd + IWL_LINK_BI, Adapter->BeaconInterval ? Adapter->BeaconInterval : 100);
        IwlPutLe32(Cmd + IWL_LINK_DTIM, Adapter->BeaconInterval ? Adapter->BeaconInterval : 100);
        IwlPutLe32(Cmd + IWL_LINK_FLAGS, IWL_LINK_FLAG_BSS_COLOR_DIS);
    }

    return IwlSendCommand(Adapter, IWL_GRP_MAC_CONF, IWL_CMD_LINK_CONFIG, Cmd, sizeof(Cmd), NULL, 0, NULL);
}

static
NTSTATUS
IwlSendStation(
    _In_ PIWL_ADAPTER Adapter)
{
    UCHAR Cmd[IWL_STA_CMD_SIZE];

    RtlZeroMemory(Cmd, sizeof(Cmd));
    IwlPutLe32(Cmd + IWL_STA_ID, IWL_STA_ID_AP);
    IwlPutLe32(Cmd + IWL_STA_LINK_MASK, IWL_BIT(IWL_LINK_FW_ID));
    RtlCopyMemory(Cmd + IWL_STA_PEER_MLD, Adapter->Bssid, IWL_ADDRESS_LENGTH);
    RtlCopyMemory(Cmd + IWL_STA_PEER_LINK, Adapter->Bssid, IWL_ADDRESS_LENGTH);
    IwlPutLe32(Cmd + IWL_STA_TYPE, IWL_STA_TYPE_PEER);
    IwlPutLe32(Cmd + IWL_STA_ASSOC_ID, Adapter->Aid);
    IwlPutLe32(Cmd + IWL_STA_MFP, (!Adapter->Aid || Adapter->PmfActive) ? 1 : 0);
    return IwlSendCommand(Adapter, IWL_GRP_MAC_CONF, IWL_CMD_STA_CONFIG, Cmd, sizeof(Cmd), NULL, 0, NULL);
}

static
VOID
IwlProtectSession(
    _In_ PIWL_ADAPTER Adapter)
{
    UCHAR Cmd[IWL_SESSION_CMD_SIZE];

    RtlZeroMemory(Cmd, sizeof(Cmd));
    IwlPutLe32(Cmd + 0, IWL_LINK_FW_ID);
    IwlPutLe32(Cmd + 4, IWL_FW_CTXT_ACTION_ADD);
    IwlPutLe32(Cmd + 8, IWL_SESSION_CONF_ASSOC);
    IwlPutLe32(Cmd + 12, IWL_SESSION_DURATION_TU);
    IwlSendCommand(Adapter, IWL_GRP_MAC_CONF, IWL_CMD_SESSION_PROTECTION, Cmd, sizeof(Cmd), NULL, 0, NULL);
}

NTSTATUS
IwlVifInit(
    _In_ PIWL_ADAPTER Adapter)
{
    NTSTATUS Status;

    Adapter->CurrentChannel = 1;
    Adapter->Aid = 0;
    Adapter->BeaconInterval = 0;
    Adapter->StationAdded = FALSE;
    Adapter->LinkActive = FALSE;

    Status = IwlSendPhyContext(Adapter, IWL_FW_CTXT_ACTION_ADD, Adapter->CurrentChannel);
    if (!NT_SUCCESS(Status))
        return Status;
    Adapter->PhyAdded = TRUE;

    Status = IwlSendMac(Adapter, IWL_FW_CTXT_ACTION_ADD);
    if (!NT_SUCCESS(Status))
        return Status;
    Adapter->MacAdded = TRUE;

    Status = IwlSendLink(Adapter, IWL_FW_CTXT_ACTION_ADD, FALSE, 0);
    if (!NT_SUCCESS(Status))
        return Status;

    DPRINT1("IWLBE: station interface ready\n");
    return STATUS_SUCCESS;
}

VOID
IwlSetChannel(
    _In_ PIWL_ADAPTER Adapter,
    _In_ UCHAR Channel)
{
    if (!Adapter->PhyAdded || Channel == 0)
        return;

    if (NT_SUCCESS(IwlSendPhyContext(Adapter, IWL_FW_CTXT_ACTION_MODIFY, Channel)))
        Adapter->CurrentChannel = Channel;
}

VOID
IwlSetRxFilter(
    _In_ PIWL_ADAPTER Adapter,
    _In_ BOOLEAN AllBeacons)
{
    /* Beacons stay enabled after association: the link watchdog in mlme.c counts them. */
    Adapter->AllBeacons = AllBeacons;
}

static
VOID
IwlTearDownStation(
    _In_ PIWL_ADAPTER Adapter)
{
    UCHAR Cmd[4];

    if (Adapter->StationAdded)
    {
        IwlReleaseTxQueue(Adapter, &Adapter->DataQueue, 0, TRUE);
        IwlReleaseTxQueue(Adapter, &Adapter->MgmtQueue, IWL_MGMT_TID, TRUE);
        IwlPutLe32(Cmd, IWL_STA_ID_AP);
        IwlSendCommand(Adapter, IWL_GRP_MAC_CONF, IWL_CMD_STA_REMOVE, Cmd, sizeof(Cmd), NULL, 0, NULL);
        Adapter->StationAdded = FALSE;
    }

    if (Adapter->LinkActive)
    {
        IwlSendLink(Adapter, IWL_FW_CTXT_ACTION_MODIFY, FALSE, IWL_LINK_MODIFY_ACTIVE);
        Adapter->LinkActive = FALSE;
    }
}

NTSTATUS
IwlVifSetLink(
    _In_ PIWL_ADAPTER Adapter,
    _In_reads_bytes_opt_(IWL_ADDRESS_LENGTH) const UCHAR *Bssid,
    _In_ BOOLEAN Associated,
    _In_ USHORT Aid,
    _In_ USHORT BeaconInterval)
{
    NTSTATUS Status;

    if (!Adapter->MacAdded)
        return STATUS_DEVICE_NOT_READY;

    if (!Bssid)
    {
        IwlTearDownStation(Adapter);
        if (Adapter->Aid)
        {
            Adapter->Aid = 0;
            IwlSendMac(Adapter, IWL_FW_CTXT_ACTION_MODIFY);
        }
        RtlZeroMemory(Adapter->Bssid, IWL_ADDRESS_LENGTH);
        return STATUS_SUCCESS;
    }

    if (!Associated)
    {
        IwlTearDownStation(Adapter);
        RtlCopyMemory(Adapter->Bssid, Bssid, IWL_ADDRESS_LENGTH);
        Adapter->Aid = 0;
        Adapter->BeaconInterval = BeaconInterval;

        Status = IwlSendLink(Adapter, IWL_FW_CTXT_ACTION_MODIFY, TRUE,
                             IWL_LINK_MODIFY_ACTIVE | IWL_LINK_MODIFY_RATES | IWL_LINK_MODIFY_PROTECT |
                             IWL_LINK_MODIFY_QOS | IWL_LINK_MODIFY_BEACON);
        if (!NT_SUCCESS(Status))
            return Status;
        Adapter->LinkActive = TRUE;

        Status = IwlSendStation(Adapter);
        if (!NT_SUCCESS(Status))
            return Status;
        Adapter->StationAdded = TRUE;

        Status = IwlAllocateTxQueue(Adapter, &Adapter->MgmtQueue, IWL_MGMT_TID);
        if (NT_SUCCESS(Status))
            Status = IwlAllocateTxQueue(Adapter, &Adapter->DataQueue, 0);
        if (!NT_SUCCESS(Status))
        {
            DPRINT1("IWLBE: transmit queue setup failed 0x%08lx\n", Status);
            return Status;
        }

        IwlProtectSession(Adapter);
        return STATUS_SUCCESS;
    }

    Adapter->Aid = Aid ? Aid : 1;
    Adapter->BeaconInterval = BeaconInterval;
    Status = IwlSendStation(Adapter);
    if (NT_SUCCESS(Status))
        Status = IwlSendMac(Adapter, IWL_FW_CTXT_ACTION_MODIFY);
    if (NT_SUCCESS(Status))
        Status = IwlSendLink(Adapter, IWL_FW_CTXT_ACTION_MODIFY, TRUE, IWL_LINK_MODIFY_BEACON);
    return Status;
}

NTSTATUS
IwlVifSetRates(
    _In_ PIWL_ADAPTER Adapter,
    _In_ ULONG RateMask)
{
    Adapter->TxRateMask = RateMask;
    return STATUS_SUCCESS;
}

static
VOID
IwlScanAddChannel(
    _Inout_ PUCHAR Cmd,
    _Inout_ PULONG Count,
    _In_ UCHAR Channel,
    _In_ ULONG SsidBitmap)
{
    PUCHAR Cfg;

    if (*Count >= IWL_SCAN_MAX_CHANNELS)
        return;

    Cfg = Cmd + IWL_SCAN_CHAN_CFG + *Count * IWL_SCAN_CHAN_CFG_SIZE;
    IwlPutLe32(Cfg, SsidBitmap | ((ULONG)IwlBandOf(Channel) << IWL_SCAN_CHAN_BAND_SHIFT));
    Cfg[4] = Channel;
    Cfg[5] = 0;
    Cfg[6] = 1;
    Cfg[7] = 0;
    (*Count)++;
}

static
VOID
IwlScanBuildProbe(
    _In_ PIWL_ADAPTER Adapter,
    _Inout_ PUCHAR Cmd)
{
    PUCHAR Buf = Cmd + IWL_SCAN_PREQ_BUF;
    ULONG Offset = 0;

    Buf[0] = DOT11_FC0_TYPE_MGMT | DOT11_SUBTYPE_PROBE_REQ;
    RtlFillMemory(Buf + 4, IWL_ADDRESS_LENGTH, 0xff);
    RtlCopyMemory(Buf + 10, Adapter->CurrentAddress, IWL_ADDRESS_LENGTH);
    RtlFillMemory(Buf + 16, IWL_ADDRESS_LENGTH, 0xff);
    Offset = DOT11_HEADER_LENGTH;
    Buf[Offset++] = DOT11_IE_SSID;
    Buf[Offset++] = 0;
    IwlPutLe16(Cmd + IWL_SCAN_PREQ_MAC_HEADER, 0);
    IwlPutLe16(Cmd + IWL_SCAN_PREQ_MAC_HEADER + 2, Offset);

    IwlPutLe16(Cmd + IWL_SCAN_PREQ_BAND_DATA, Offset);
    IwlPutLe16(Cmd + IWL_SCAN_PREQ_BAND_DATA + 2, sizeof(IwlProbeRates2G));
    RtlCopyMemory(Buf + Offset, IwlProbeRates2G, sizeof(IwlProbeRates2G));
    Offset += sizeof(IwlProbeRates2G);

    IwlPutLe16(Cmd + IWL_SCAN_PREQ_BAND_DATA + 4, Offset);
    IwlPutLe16(Cmd + IWL_SCAN_PREQ_BAND_DATA + 6, sizeof(IwlProbeRates5G));
    RtlCopyMemory(Buf + Offset, IwlProbeRates5G, sizeof(IwlProbeRates5G));
    Offset += sizeof(IwlProbeRates5G);

    IwlPutLe16(Cmd + IWL_SCAN_PREQ_BAND_DATA + 8, Offset);
    IwlPutLe16(Cmd + IWL_SCAN_PREQ_BAND_DATA + 10, 0);

    IwlPutLe16(Cmd + IWL_SCAN_PREQ_COMMON, Offset);
    IwlPutLe16(Cmd + IWL_SCAN_PREQ_COMMON + 2, sizeof(IwlTpcReport));
    RtlCopyMemory(Buf + Offset, IwlTpcReport, sizeof(IwlTpcReport));
}

NTSTATUS
IwlFirmwareScan(
    _In_ PIWL_ADAPTER Adapter,
    _In_opt_ PDOT11_SSID Ssid)
{
    BOOLEAN Directed = Ssid && Ssid->uSSIDLength && Ssid->uSSIDLength <= DOT11_SSID_MAX_LENGTH;
    ULONG Count = 0, i, Bitmap = Directed ? 1 : 0;
    NTSTATUS Status;
    PUCHAR Cmd;

    if (!Adapter->MacAdded || Adapter->FirmwareDead)
        return STATUS_DEVICE_NOT_READY;

    Cmd = IwlAllocate(IWL_SCAN_CMD_SIZE);
    if (!Cmd)
        return STATUS_INSUFFICIENT_RESOURCES;

    IwlPutLe32(Cmd + IWL_SCAN_UID, ++Adapter->ScanUid);
    IwlPutLe32(Cmd + IWL_SCAN_OOC_PRIORITY, IWL_SCAN_PRIORITY_EXT_6);

    IwlPutLe16(Cmd + IWL_SCAN_GEN_FLAGS, IWL_SCAN_GEN_FLAGS_PASS_ALL | IWL_SCAN_GEN_FLAGS_ADWELL |
                                         (Directed ? IWL_SCAN_GEN_FLAGS_ALL_RX : 0));
    Cmd[IWL_SCAN_GEN_START_LINK] = IWL_LINK_FW_ID;
    Cmd[IWL_SCAN_GEN_ACTIVE_DWELL] = IWL_SCAN_DWELL_ACTIVE;
    Cmd[IWL_SCAN_GEN_ACTIVE_DWELL + 1] = IWL_SCAN_DWELL_ACTIVE;
    Cmd[IWL_SCAN_GEN_ADWELL_2G] = 2;
    Cmd[IWL_SCAN_GEN_ADWELL_5G] = 8;
    Cmd[IWL_SCAN_GEN_ADWELL_SOCIAL] = 10;
    IwlPutLe16(Cmd + IWL_SCAN_GEN_MAX_BUDGET, Directed ? 100 : 300);
    IwlPutLe32(Cmd + IWL_SCAN_GEN_PRIORITY, IWL_SCAN_PRIORITY_EXT_6);
    Cmd[IWL_SCAN_GEN_PASSIVE_DWELL] = IWL_SCAN_DWELL_PASSIVE;
    Cmd[IWL_SCAN_GEN_PASSIVE_DWELL + 1] = IWL_SCAN_DWELL_PASSIVE;

    Cmd[IWL_SCAN_CHAN_FLAGS] = IWL_SCAN_CHAN_FLAG_ORDER;
    Cmd[IWL_SCAN_CHAN_N_APS] = 10;
    Cmd[IWL_SCAN_CHAN_N_APS + 1] = 2;
    for (i = 0; i < Adapter->Channels2GCount; i++)
        IwlScanAddChannel(Cmd, &Count, Adapter->Channels2G[i], Bitmap);
    for (i = 0; i < Adapter->Channels5GCount; i++)
        IwlScanAddChannel(Cmd, &Count, Adapter->Channels5G[i], Bitmap);
    Cmd[IWL_SCAN_CHAN_COUNT] = (UCHAR)Count;

    IwlPutLe16(Cmd + IWL_SCAN_PERIODIC, 0);
    Cmd[IWL_SCAN_PERIODIC + 2] = 1;

    IwlScanBuildProbe(Adapter, Cmd);
    if (Directed)
    {
        Cmd[IWL_SCAN_DIRECT] = DOT11_IE_SSID;
        Cmd[IWL_SCAN_DIRECT + 1] = (UCHAR)Ssid->uSSIDLength;
        RtlCopyMemory(Cmd + IWL_SCAN_DIRECT + 2, Ssid->ucSSID, Ssid->uSSIDLength);
    }

    IwlArmNotification(Adapter, IWL_GRP_LEGACY, IWL_CMD_SCAN_COMPLETE_UMAC);
    Status = IwlSendCommand(Adapter, IWL_GRP_LONG, IWL_CMD_SCAN_REQ_UMAC, Cmd, IWL_SCAN_CMD_SIZE, NULL, 0, NULL);
    IwlFree(Cmd);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("IWLBE: scan request failed 0x%08lx\n", Status);
        return Status;
    }

    Status = IwlWaitNotification(Adapter, IWL_SCAN_TIMEOUT_MS);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("IWLBE: scan did not complete 0x%08lx\n", Status);
        return Status;
    }

    DPRINT1("IWLBE: scan of %lu channels finished, status %u\n", Count,
            Adapter->NotifLength > IWL_SCAN_COMPLETE_STATUS ? Adapter->NotifData[IWL_SCAN_COMPLETE_STATUS] : 0);
    return STATUS_SUCCESS;
}

static
ULONG
IwlRateFlags(
    _In_ PIWL_ADAPTER Adapter,
    _In_ BOOLEAN Lowest)
{
    if (Lowest)
        return (Adapter->CurrentChannel > 14 ? IWL_RATE_MOD_OFDM : IWL_RATE_MOD_CCK) | IWL_RATE_ANT_A;

    if (Adapter->CurrentChannel > 14 || (Adapter->TxRateMask & IWL_RATE_MASK_OFDM) || !Adapter->TxRateMask)
        return IWL_RATE_MOD_OFDM | 4 | IWL_RATE_ANT_A;

    return IWL_RATE_MOD_CCK | 3 | IWL_RATE_ANT_A;
}

NTSTATUS
IwlTxFrame(
    _In_ PIWL_ADAPTER Adapter,
    _In_reads_bytes_(Length) const UCHAR *Frame,
    _In_ ULONG Length,
    _In_ BOOLEAN Management)
{
    UCHAR Header[40];
    ULONG HeaderLength, Waited;
    BOOLEAN Group;
    NTSTATUS Status;
    USHORT Sequence;

    if (Length < DOT11_HEADER_LENGTH || Length > IWL_MAX_80211_FRAME)
        return STATUS_INVALID_PARAMETER;

    HeaderLength = IwlFrameHeaderLength(Frame, Length);
    if (HeaderLength > sizeof(Header) || HeaderLength > Length)
        return STATUS_INVALID_PARAMETER;

    RtlCopyMemory(Header, Frame, HeaderLength);
    NdisAcquireSpinLock(&Adapter->TxLock);
    Sequence = Adapter->TxSequence++;
    NdisReleaseSpinLock(&Adapter->TxLock);
    IwlPutLe16(Header + 22, (ULONG)(Sequence & 0xFFF) << 4);

    Group = (Frame[4] & 0x01) != 0;
    for (Waited = 0;; Waited++)
    {
        Status = IwlQueueFrame(Adapter, Management ? &Adapter->MgmtQueue : &Adapter->DataQueue,
                               Header, HeaderLength, Frame + HeaderLength, Length - HeaderLength,
                               IwlRateFlags(Adapter, Management || Group),
                               IWL_TX_FLAGS_CMD_RATE | IWL_TX_FLAGS_ENCRYPT_DIS);
        if (Status != STATUS_DEVICE_BUSY || Waited >= IWL_TX_RETRY_MS || KeGetCurrentIrql() > PASSIVE_LEVEL)
            break;
        IwlSleep(1);
    }

    if (NT_SUCCESS(Status))
        Adapter->TxBytes += Length;
    return Status;
}
