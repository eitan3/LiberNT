/*
 * PROJECT:     LiberNT Realtek RTL8125/RTL8126 Ethernet Driver
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Register layout, descriptor format and chip logic shared with the host tests
 * COPYRIGHT:   Copyright 2026 The LiberNT contributors
 */

#pragma once

/* No public datasheet: values follow the kdnet transport (kd_02_10ec) and observed r8169 behaviour. */

#define RT25_REG_IDR0               0x0000
#define RT25_REG_MAR0               0x0008
#define RT25_REG_COUNTER_LOW        0x0010
#define RT25_REG_COUNTER_HIGH       0x0014
#define RT25_REG_TNPDS              0x0020
#define RT25_REG_INT_CFG0           0x0034
#define RT25_REG_CR                 0x0037
#define RT25_REG_IMR                0x0038
#define RT25_REG_ISR                0x003C
#define RT25_REG_TCR                0x0040
#define RT25_REG_RCR                0x0044
#define RT25_REG_9346CR             0x0050
#define RT25_REG_CONFIG1            0x0052
#define RT25_REG_CONFIG2            0x0053
#define RT25_REG_CONFIG3            0x0054
#define RT25_REG_CONFIG5            0x0056
#define RT25_REG_PHYSTATUS          0x006C
#define RT25_REG_PMCH               0x006F
#define RT25_REG_INT_CFG1           0x007A
#define RT25_REG_TPPOLL             0x0090
#define RT25_REG_OCPDR              0x00B0
#define RT25_REG_GPHY_OCP           0x00B8
#define RT25_REG_MCU                0x00D3
#define RT25_REG_00D8               0x00D8
#define RT25_REG_RMS                0x00DA
#define RT25_REG_CPCR               0x00E0
#define RT25_REG_RDSAR              0x00E4
#define RT25_REG_MISC               0x00F0
#define RT25_REG_0382               0x0382
#define RT25_REG_COALESCE           0x0A00
#define RT25_REG_TCAM               0x1880
#define RT25_REG_MAC_BACKUP         0x19E0
#define RT25_REG_RSS_CTRL           0x4500
#define RT25_REG_QUEUES             0x4800

#define RT25_CR_TE                  0x04
#define RT25_CR_RE                  0x08
#define RT25_CR_RST                 0x10

#define RT25_TPPOLL_NPQ             0x0001

#define RT25_COUNTER_RESET          0x00000001
#define RT25_COUNTER_DUMP           0x00000008

#define RT25_TCR_DEFAULT            0x03000700
#define RT25_TCR_AUTO_FIFO          0x00000080
#define RT25_TCR_EMPTY              0x00000800
#define RT25_TCR_ID_SHIFT           20
#define RT25_TCR_ID_MASK            0x00000FCF

#define RT25_RCR_AAP                0x00000001
#define RT25_RCR_APM                0x00000002
#define RT25_RCR_AM                 0x00000004
#define RT25_RCR_AB                 0x00000008
#define RT25_RCR_ACCEPT_MASK        0x0000003F
#define RT25_RCR_MXDMA              0x00000700
#define RT25_RCR_PAUSE_SLOT         0x00000800
#define RT25_RCR_FETCH              0x40000000

#define RT25_9346CR_UNLOCK          0xC0
#define RT25_9346CR_LOCK            0x00

#define RT25_CONFIG1_PM_ENABLE      0x01
#define RT25_CONFIG2_PME_SIGNAL     0x20
#define RT25_CONFIG3_LINK_UP        0x10
#define RT25_CONFIG3_MAGIC          0x20
#define RT25_CONFIG5_PME_STATUS     0x01
#define RT25_CONFIG5_LAN_WAKE       0x02
#define RT25_CONFIG5_WAKE_FRAMES    0x70

#define RT25_INT_RX_OK              0x00000001
#define RT25_INT_RX_ERR             0x00000002
#define RT25_INT_TX_OK              0x00000004
#define RT25_INT_TX_ERR             0x00000008
#define RT25_INT_RX_OVERFLOW        0x00000010
#define RT25_INT_LINK_CHANGE        0x00000020
#define RT25_INT_RX_FIFO_OVER       0x00000040
#define RT25_INT_TX_DESC_UNAVAIL    0x00000080
#define RT25_INT_SYSTEM_ERROR       0x00008000
/* No TX_OK: sends complete at once from bounce buffers, and the next send reclaims the ring. */
#define RT25_INT_MASK               (RT25_INT_RX_OK | RT25_INT_RX_ERR | RT25_INT_TX_ERR | \
                                     RT25_INT_LINK_CHANGE)
#define RT25_INT_RX_EVENTS          (RT25_INT_RX_OK | RT25_INT_RX_ERR | RT25_INT_RX_OVERFLOW | \
                                     RT25_INT_RX_FIFO_OVER)
#define RT25_INT_TX_EVENTS          (RT25_INT_TX_OK | RT25_INT_TX_ERR | RT25_INT_TX_DESC_UNAVAIL)

#define RT25_PHYSTATUS_FULL_DUPLEX  0x0001
#define RT25_PHYSTATUS_LINK         0x0002
#define RT25_PHYSTATUS_10M          0x0004
#define RT25_PHYSTATUS_RX_PAUSE     0x0020
#define RT25_PHYSTATUS_TX_PAUSE     0x0040
#define RT25_PHYSTATUS_100M         0x0008
#define RT25_PHYSTATUS_2500M        0x0400

#define RT25_COALESCE_END           0x0A80
#define RT25_COALESCE_WIDE_END      0x0B00

#define RT25_PCI_GEN3_RELATED       0x0890
#define RT25_GEN3_ZRXDC_NONCOMPL    0x01

#define RT25_PMCH_PHY_POWER         0xC0
#define RT25_00D8_8126_CLEAR        0x02

#define RT25_CPCR_RXCHKSUM          0x0020
#define RT25_CPCR_RXVLAN            0x0040

#define RT25_MCU_LINK_LIST_READY    0x02
#define RT25_MCU_EMPTY              0x30
#define RT25_MCU_OUT_OF_BAND        0x80
#define RT25_MISC_RECEIVE_GATE      0x00080000

#define RT25_OCP_FLAG               0x80000000
#define RT25_OCP_SHIFT              15
#define RT25_OCP_PHY_BASE           0xA400

#define RT25_OCP_LINK_LIST          0xE8DE
#define RT25_OCP_LINK_LIST_HOLD     0x4000
#define RT25_OCP_REALWOW            0xC0BC
#define RT25_OCP_DESCRIPTOR         0xEB58
#define RT25_OCP_TCAM               0xEB54
#define RT25_OCP_TX_FIFO            0xE614
#define RT25_OCP_TX_QUEUES          0xE63E
#define RT25_OCP_BUSY               0xE00E
#define RT25_OCP_BUSY_BIT           0x2000
#define RT25_OCP_TALLY              0xEA84
#define RT25_OCP_WOL_MAGIC          0xC0B6
#define RT25_OCP_WOL_MAGIC_ENABLE   0x0001
#define RT25_OCP_EEE_MAC            0xE040
#define RT25_OCP_EEE_MAC_LPI        0x0003
#define RT25_OCP_EEE_TIMER          0xEB62
#define RT25_OCP_EEE_TIMER_ENABLE   0x0006

#define RT25_PHY_BMCR               0x00
#define RT25_PHY_BMSR               0x01
#define RT25_PHY_ADVERTISE          0x04
#define RT25_PHY_CTRL1000           0x09
#define RT25_PHY_PHYSR              0x1A
#define RT25_PHY_OCP_AN_10GBT_CTRL  0xA5D4
#define RT25_PHY_OCP_EEE_ADV        0xA5D0
#define RT25_PHY_OCP_EEE_ADV2       0xA6D4

#define RT25_BMCR_ANRESTART         0x0200
#define RT25_BMCR_ISOLATE           0x0400
#define RT25_BMCR_PDOWN             0x0800
#define RT25_BMCR_ANENABLE          0x1000

#define RT25_ADVERTISE_10_HALF      0x0020
#define RT25_ADVERTISE_10_FULL      0x0040
#define RT25_ADVERTISE_100_HALF     0x0080
#define RT25_ADVERTISE_100_FULL     0x0100
#define RT25_ADVERTISE_10_100       0x01E0
#define RT25_ADVERTISE_PAUSE        0x0400
#define RT25_ADVERTISE_ASYM_PAUSE   0x0800
#define RT25_CTRL1000_HALF          0x0100
#define RT25_CTRL1000_FULL          0x0200

#define RT25_AN_10GBT_ADV_2500      0x0080
#define RT25_AN_10GBT_ADV_5000      0x0100

/* Clause 45 EEE advertisement bits (7.60 and 7.62) as the PHY maps them into OCP space. */
#define RT25_EEE_ADV_100            0x0002
#define RT25_EEE_ADV_1000           0x0004
#define RT25_EEE_ADV2_2500          0x0001
#define RT25_EEE_ADV2_5000          0x0002

#define RT25_PHYSR_SPEED_MASK       0x0630
#define RT25_PHYSR_SPEED_10         0x0000
#define RT25_PHYSR_SPEED_100        0x0010
#define RT25_PHYSR_SPEED_1000       0x0020
#define RT25_PHYSR_SPEED_2500       0x0210
#define RT25_PHYSR_SPEED_5000       0x0220

#define RT25_DESC_OWN               0x80000000
#define RT25_DESC_EOR               0x40000000
#define RT25_DESC_FS                0x20000000
#define RT25_DESC_LS                0x10000000
#define RT25_DESC_LENGTH_MASK       0x00003FFF
#define RT25_RX_DESC_RES            0x00200000
#define RT25_RX_PROTO_TCP           0x00020000
#define RT25_RX_PROTO_UDP           0x00040000
#define RT25_RX_PROTO_MASK          0x00060000
#define RT25_RX_IP_FAIL             0x00010000
#define RT25_RX_UDP_FAIL            0x00008000
#define RT25_RX_TCP_FAIL            0x00004000

#define RT25_TX_IPV6_CS             0x10000000
#define RT25_TX_IPV4_CS             0x20000000
#define RT25_TX_TCP_CS              0x40000000
#define RT25_TX_UDP_CS              0x80000000
#define RT25_TX_L4_OFFSET_SHIFT     18
#define RT25_TX_L4_OFFSET_MAX       0x3FF

typedef struct _RT25_DESCRIPTOR
{
    ULONG Flags;
    ULONG Vlan;
    ULONG AddressLow;
    ULONG AddressHigh;
} RT25_DESCRIPTOR, *PRT25_DESCRIPTOR;

C_ASSERT(sizeof(RT25_DESCRIPTOR) == 16);

#define RT25_RING_SIZE              256
#define RT25_CRC_SIZE               4
#define RT25_VLAN_TAG_SIZE          4
#define RT25_ETH_HEADER             14
#define RT25_MTU                    1500
#define RT25_MAX_FRAME              1514
#define RT25_JUMBO_FRAME            9014
#define RT25_MIN_FRAME              60
#define RT25_MAX_BUFFER_SIZE        9216

/* *SpeedDuplex values; 2500 and 5000 are this driver's own for the multi-gigabit speeds. */
#define RT25_SPEED_AUTO             0
#define RT25_SPEED_10_HALF          1
#define RT25_SPEED_10_FULL          2
#define RT25_SPEED_100_HALF         3
#define RT25_SPEED_100_FULL         4
#define RT25_SPEED_1000_FULL        6
#define RT25_SPEED_2500_FULL        2500
#define RT25_SPEED_5000_FULL        5000

/* *FlowControl values. */
#define RT25_FLOW_OFF               0
#define RT25_FLOW_TX                1
#define RT25_FLOW_RX                2
#define RT25_FLOW_BOTH              3

/* Leading part of the tally block; RTL8125-class chips append more counters after it. */
typedef struct _RT25_TALLY
{
    ULONG64 TxPackets;
    ULONG64 RxPackets;
    ULONG64 TxErrors;
    ULONG RxErrors;
    USHORT RxMissed;
    USHORT AlignErrors;
    ULONG TxOneCollision;
    ULONG TxMultiCollision;
    ULONG64 RxUnicast;
    ULONG64 RxBroadcast;
    ULONG RxMulticast;
    USHORT TxAborted;
    USHORT TxUnderrun;
} RT25_TALLY, *PRT25_TALLY;

C_ASSERT(sizeof(RT25_TALLY) == 64);

typedef struct _RT25_TALLY_TOTALS
{
    ULONG64 TxErrors;
    ULONG64 RxErrors;
    ULONG64 RxMissed;
    ULONG64 AlignErrors;
    ULONG64 TxOneCollision;
    ULONG64 TxMultiCollision;
    ULONG64 TxAborted;
    ULONG64 TxUnderrun;
} RT25_TALLY_TOTALS, *PRT25_TALLY_TOTALS;

#define RT25_CHIP_8125A             0x01
#define RT25_CHIP_8125B             0x02
#define RT25_CHIP_8126              0x04
#define RT25_CHIP_WIDE_COALESCE     0x08

typedef struct _RT25_CHIP
{
    USHORT Mask;
    USHORT Value;
    UCHAR Flags;
    ULONG MaxSpeedMbps;
    const char *Name;
} RT25_CHIP;

FORCEINLINE
USHORT
Rt25ChipIdFromTcr(
    _In_ ULONG Tcr)
{
    return (USHORT)((Tcr >> RT25_TCR_ID_SHIFT) & RT25_TCR_ID_MASK);
}

FORCEINLINE
const RT25_CHIP *
Rt25LookupChip(
    _In_ USHORT ChipId)
{
    static const RT25_CHIP Chips[] =
    {
        { 0x7CF, 0x64A, RT25_CHIP_8126, 5000, "RTL8126A" },
        { 0x7CF, 0x649, RT25_CHIP_8126, 5000, "RTL8126A" },
        { 0x7CF, 0x708, RT25_CHIP_WIDE_COALESCE, 2500, "RTL8125CP" },
        { 0x7CF, 0x681, RT25_CHIP_WIDE_COALESCE, 2500, "RTL8125BP" },
        { 0x7CF, 0x68A, RT25_CHIP_WIDE_COALESCE, 2500, "RTL8125K" },
        { 0x7CF, 0x689, RT25_CHIP_WIDE_COALESCE, 2500, "RTL8125D" },
        { 0x7CF, 0x688, RT25_CHIP_WIDE_COALESCE, 2500, "RTL8125D" },
        { 0x7CF, 0x641, RT25_CHIP_8125B, 2500, "RTL8125B" },
        { 0x7CF, 0x609, RT25_CHIP_8125A | RT25_CHIP_WIDE_COALESCE, 2500, "RTL8125A" }
    };
    ULONG i;

    for (i = 0; i < sizeof(Chips) / sizeof(Chips[0]); i++)
    {
        if ((ChipId & Chips[i].Mask) == Chips[i].Value)
            return &Chips[i];
    }

    return NULL;
}

FORCEINLINE
ULONG
Rt25PhySpeedMbps(
    _In_ USHORT PhyStatus)
{
    switch (PhyStatus & RT25_PHYSR_SPEED_MASK)
    {
        case RT25_PHYSR_SPEED_10:
            return 10;

        case RT25_PHYSR_SPEED_100:
            return 100;

        case RT25_PHYSR_SPEED_1000:
            return 1000;

        case RT25_PHYSR_SPEED_2500:
            return 2500;

        case RT25_PHYSR_SPEED_5000:
            return 5000;
    }

    return 0;
}

FORCEINLINE
ULONG
Rt25MacSpeedMbps(
    _In_ USHORT MacStatus)
{
    if (MacStatus & RT25_PHYSTATUS_2500M)
        return 2500;
    if (MacStatus & RT25_PHYSTATUS_10M)
        return 10;
    if (MacStatus & RT25_PHYSTATUS_100M)
        return 100;
    return 1000;
}

FORCEINLINE
ULONG
Rt25NextIndex(
    _In_ ULONG Index)
{
    return (Index + 1) % RT25_RING_SIZE;
}

FORCEINLINE
ULONG
Rt25RxArmFlags(
    _In_ ULONG Index,
    _In_ ULONG BufferSize)
{
    ULONG Flags = RT25_DESC_OWN | (BufferSize & RT25_DESC_LENGTH_MASK);

    if (Index == RT25_RING_SIZE - 1)
        Flags |= RT25_DESC_EOR;
    return Flags;
}

FORCEINLINE
ULONG
Rt25TxFlags(
    _In_ ULONG Index,
    _In_ ULONG Length)
{
    ULONG Flags = RT25_DESC_OWN | RT25_DESC_FS | RT25_DESC_LS | (Length & RT25_DESC_LENGTH_MASK);

    if (Index == RT25_RING_SIZE - 1)
        Flags |= RT25_DESC_EOR;
    return Flags;
}

FORCEINLINE
ULONG
Rt25RxFrameLength(
    _In_ ULONG Flags,
    _In_ ULONG ReceiveSize)
{
    ULONG Length = Flags & RT25_DESC_LENGTH_MASK;

    if ((Flags & RT25_DESC_OWN) ||
        (Flags & (RT25_DESC_FS | RT25_DESC_LS)) != (RT25_DESC_FS | RT25_DESC_LS) ||
        (Flags & RT25_RX_DESC_RES) ||
        Length < RT25_ETH_HEADER + RT25_CRC_SIZE ||
        Length > ReceiveSize)
    {
        return 0;
    }

    return Length - RT25_CRC_SIZE;
}

FORCEINLINE
ULONG
Rt25ReceiveConfig(
    _In_ UCHAR ChipFlags,
    _In_ ULONG PacketFilter,
    _In_ ULONG Directed,
    _In_ ULONG Multicast,
    _In_ ULONG AllMulticast,
    _In_ ULONG Broadcast,
    _In_ ULONG Promiscuous)
{
    ULONG Config = RT25_RCR_FETCH | RT25_RCR_MXDMA;

    if (!(ChipFlags & RT25_CHIP_8125A))
        Config |= RT25_RCR_PAUSE_SLOT;
    if (PacketFilter & Directed)
        Config |= RT25_RCR_APM;
    if (PacketFilter & (Multicast | AllMulticast))
        Config |= RT25_RCR_AM;
    if (PacketFilter & Broadcast)
        Config |= RT25_RCR_AB;
    if (PacketFilter & Promiscuous)
        Config |= RT25_RCR_AAP | RT25_RCR_APM | RT25_RCR_AM | RT25_RCR_AB;
    return Config;
}

FORCEINLINE
ULONG
Rt25TxPaddedLength(
    _In_ ULONG Length)
{
    return Length < RT25_MIN_FRAME ? RT25_MIN_FRAME : Length;
}

#define RT25_CAST_UNICAST           0
#define RT25_CAST_MULTICAST         1
#define RT25_CAST_BROADCAST         2

FORCEINLINE
UCHAR
Rt25CastType(
    _In_reads_(6) const UCHAR *Destination)
{
    if (!(Destination[0] & 1))
        return RT25_CAST_UNICAST;
    if ((Destination[0] & Destination[1] & Destination[2] & Destination[3] & Destination[4] & Destination[5]) == 0xFF)
        return RT25_CAST_BROADCAST;
    return RT25_CAST_MULTICAST;
}

/* *JumboPacket gives the largest frame without the CRC; anything outside 1514-9014 means standard frames. */
FORCEINLINE
ULONG
Rt25MaxFrameFromJumbo(
    _In_ ULONG JumboPacket)
{
    if (JumboPacket < RT25_MAX_FRAME || JumboPacket > RT25_JUMBO_FRAME)
        return RT25_MAX_FRAME;
    return JumboPacket;
}

/* The RMS limit covers one VLAN tag and the CRC; 1536 for standard frames. */
FORCEINLINE
ULONG
Rt25ReceiveSize(
    _In_ ULONG MaxFrame)
{
    return (MaxFrame + RT25_VLAN_TAG_SIZE + RT25_CRC_SIZE + 31) & ~31u;
}

FORCEINLINE
ULONG
Rt25BufferSize(
    _In_ ULONG MaxFrame)
{
    return (Rt25ReceiveSize(MaxFrame) + 1023) & ~1023u;
}

typedef struct _RT25_ADVERTISEMENT
{
    USHORT Advertise;
    USHORT Ctrl1000;
    USHORT MultiGig;
} RT25_ADVERTISEMENT, *PRT25_ADVERTISEMENT;

/* Builds the autonegotiation words for a *SpeedDuplex choice; FALSE when the chip cannot do that speed. */
FORCEINLINE
BOOLEAN
Rt25LinkAdvertisement(
    _In_ ULONG SpeedDuplex,
    _In_ ULONG MaxSpeedMbps,
    _In_ ULONG FlowControl,
    _In_ const RT25_ADVERTISEMENT *Current,
    _Out_ PRT25_ADVERTISEMENT Result)
{
    USHORT Advertise = 0, Ctrl1000 = 0, MultiGig = 0;

    switch (SpeedDuplex)
    {
        case RT25_SPEED_AUTO:
            Advertise = RT25_ADVERTISE_10_100;
            Ctrl1000 = RT25_CTRL1000_FULL;
            if (MaxSpeedMbps >= 2500)
                MultiGig |= RT25_AN_10GBT_ADV_2500;
            if (MaxSpeedMbps >= 5000)
                MultiGig |= RT25_AN_10GBT_ADV_5000;
            break;

        case RT25_SPEED_10_HALF:
            Advertise = RT25_ADVERTISE_10_HALF;
            break;

        case RT25_SPEED_10_FULL:
            Advertise = RT25_ADVERTISE_10_FULL;
            break;

        case RT25_SPEED_100_HALF:
            Advertise = RT25_ADVERTISE_100_HALF;
            break;

        case RT25_SPEED_100_FULL:
            Advertise = RT25_ADVERTISE_100_FULL;
            break;

        case RT25_SPEED_1000_FULL:
            Ctrl1000 = RT25_CTRL1000_FULL;
            break;

        case RT25_SPEED_2500_FULL:
            if (MaxSpeedMbps < 2500)
                return FALSE;
            MultiGig = RT25_AN_10GBT_ADV_2500;
            break;

        case RT25_SPEED_5000_FULL:
            if (MaxSpeedMbps < 5000)
                return FALSE;
            MultiGig = RT25_AN_10GBT_ADV_5000;
            break;

        default:
            return FALSE;
    }

    /* 802.3 Annex 28B: symmetric pause, or asymmetric toward the side that only receives. */
    if (FlowControl == RT25_FLOW_BOTH)
        Advertise |= RT25_ADVERTISE_PAUSE;
    else if (FlowControl == RT25_FLOW_RX)
        Advertise |= RT25_ADVERTISE_PAUSE | RT25_ADVERTISE_ASYM_PAUSE;
    else if (FlowControl == RT25_FLOW_TX)
        Advertise |= RT25_ADVERTISE_ASYM_PAUSE;

    Result->Advertise = (USHORT)((Current->Advertise &
                                  ~(RT25_ADVERTISE_10_100 | RT25_ADVERTISE_PAUSE | RT25_ADVERTISE_ASYM_PAUSE)) |
                                 Advertise);
    Result->Ctrl1000 = (USHORT)((Current->Ctrl1000 & ~(RT25_CTRL1000_FULL | RT25_CTRL1000_HALF)) | Ctrl1000);
    Result->MultiGig = (USHORT)((Current->MultiGig & ~(RT25_AN_10GBT_ADV_2500 | RT25_AN_10GBT_ADV_5000)) |
                                MultiGig);
    return TRUE;
}

FORCEINLINE
ULONG
Rt25PauseFromStatus(
    _In_ USHORT MacStatus,
    _In_ ULONG SendOnly,
    _In_ ULONG ReceiveOnly,
    _In_ ULONG Both,
    _In_ ULONG None)
{
    BOOLEAN Tx = (MacStatus & RT25_PHYSTATUS_TX_PAUSE) != 0, Rx = (MacStatus & RT25_PHYSTATUS_RX_PAUSE) != 0;

    return Tx && Rx ? Both : Tx ? SendOnly : Rx ? ReceiveOnly : None;
}

/* Adds one tally snapshot; the controller counters are reset after every dump. */
FORCEINLINE
VOID
Rt25AccumulateTally(
    _Inout_ PRT25_TALLY_TOTALS Totals,
    _In_ const RT25_TALLY *Tally)
{
    Totals->TxErrors += Tally->TxErrors;
    Totals->RxErrors += Tally->RxErrors;
    Totals->RxMissed += Tally->RxMissed;
    Totals->AlignErrors += Tally->AlignErrors;
    Totals->TxOneCollision += Tally->TxOneCollision;
    Totals->TxMultiCollision += Tally->TxMultiCollision;
    Totals->TxAborted += Tally->TxAborted;
    Totals->TxUnderrun += Tally->TxUnderrun;
}

FORCEINLINE
ULONG
Rt25OcpCommand(
    _In_ ULONG Address,
    _In_ BOOLEAN Write,
    _In_ USHORT Value)
{
    ULONG Command = Address << RT25_OCP_SHIFT;

    if (Write)
        Command |= RT25_OCP_FLAG | Value;
    return Command;
}

FORCEINLINE
ULONG
Rt25EtherCrc(
    _In_reads_(6) const UCHAR *Address)
{
    ULONG Crc = 0xFFFFFFFF, i, Bit;
    UCHAR Byte;

    for (i = 0; i < 6; i++)
    {
        Byte = Address[i];
        for (Bit = 0; Bit < 8; Bit++, Byte >>= 1)
            Crc = (Crc << 1) ^ ((((Crc >> 31) ^ Byte) & 1) ? 0x04C11DB7 : 0);
    }

    return Crc;
}

FORCEINLINE
ULONG
Rt25Swap32(
    _In_ ULONG Value)
{
    return (Value >> 24) | ((Value >> 8) & 0xFF00) | ((Value << 8) & 0xFF0000) | (Value << 24);
}

/* The RTL8125 takes the 64-bit hash with its two words swapped and byte-reversed. */
FORCEINLINE
VOID
Rt25MulticastFilter(
    _In_reads_(Count * 6) const UCHAR *List,
    _In_ ULONG Count,
    _Out_ PULONG Mar0,
    _Out_ PULONG Mar4)
{
    ULONG Filter[2] = { 0, 0 }, Bit, i;

    for (i = 0; i < Count; i++)
    {
        Bit = Rt25EtherCrc(List + i * 6) >> 26;
        Filter[Bit >> 5] |= 1u << (Bit & 31);
    }

    *Mar0 = Rt25Swap32(Filter[1]);
    *Mar4 = Rt25Swap32(Filter[0]);
}

#define RT25_IP_NONE                0
#define RT25_IP_V4                  4
#define RT25_IP_V6                  6
#define RT25_PROTO_TCP              6
#define RT25_PROTO_UDP              17

typedef struct _RT25_FRAME_INFO
{
    UCHAR Ip;
    UCHAR Protocol;
    USHORT IpOffset;
    USHORT IpHeaderLength;
    USHORT L4Offset;
    USHORT L4Length;
} RT25_FRAME_INFO, *PRT25_FRAME_INFO;

FORCEINLINE
USHORT
Rt25Get16(
    _In_reads_(2) const UCHAR *Data)
{
    return (USHORT)((Data[0] << 8) | Data[1]);
}

/* Finds the IPv4/IPv6 and TCP/UDP headers of a frame; Protocol stays 0 for fragments and other payloads. */
FORCEINLINE
VOID
Rt25ParseFrame(
    _In_reads_(Length) const UCHAR *Frame,
    _In_ ULONG Length,
    _Out_ PRT25_FRAME_INFO Info)
{
    ULONG Offset = 12, Type, Header, Total, Minimum;

    Info->Ip = RT25_IP_NONE;
    Info->Protocol = 0;
    Info->IpOffset = Info->IpHeaderLength = Info->L4Offset = Info->L4Length = 0;

    if (Length < 14)
        return;

    Type = Rt25Get16(Frame + Offset);
    if (Type == 0x8100 || Type == 0x88A8)
    {
        Offset += 4;
        if (Length < Offset + 2)
            return;
        Type = Rt25Get16(Frame + Offset);
    }

    Offset += 2;
    if (Type == 0x0800 && Length >= Offset + 20 && (Frame[Offset] >> 4) == 4)
    {
        Header = (Frame[Offset] & 0x0F) * 4;
        Total = Rt25Get16(Frame + Offset + 2);
        if (Header < 20 || Total < Header || Length < Offset + Total)
            return;

        Info->Ip = RT25_IP_V4;
        Info->IpOffset = (USHORT)Offset;
        Info->IpHeaderLength = (USHORT)Header;
        if (Rt25Get16(Frame + Offset + 6) & 0x3FFF)
            return;

        Info->Protocol = Frame[Offset + 9];
        Info->L4Offset = (USHORT)(Offset + Header);
        Info->L4Length = (USHORT)(Total - Header);
    }
    else if (Type == 0x86DD && Length >= Offset + 40 && (Frame[Offset] >> 4) == 6)
    {
        Total = Rt25Get16(Frame + Offset + 4);
        if (Length < Offset + 40 + Total)
            return;

        Info->Ip = RT25_IP_V6;
        Info->IpOffset = (USHORT)Offset;
        Info->IpHeaderLength = 40;
        Info->Protocol = Frame[Offset + 6];
        Info->L4Offset = (USHORT)(Offset + 40);
        Info->L4Length = (USHORT)Total;
    }
    else
    {
        return;
    }

    Minimum = Info->Protocol == RT25_PROTO_TCP ? 20 : Info->Protocol == RT25_PROTO_UDP ? 8 : 0;
    if (!Minimum || Info->L4Length < Minimum)
    {
        Info->Protocol = 0;
        Info->L4Offset = Info->L4Length = 0;
    }
}

#define RT25_CSUM_IP                0x01
#define RT25_CSUM_TCP               0x02
#define RT25_CSUM_UDP               0x04

/* Descriptor second-word bits for the checksums asked for, or 0 when the frame does not allow them. */
FORCEINLINE
ULONG
Rt25TxChecksumFlags(
    _In_ const RT25_FRAME_INFO *Info,
    _In_ ULONG Requested)
{
    ULONG Flags = 0;

    if (Info->Ip == RT25_IP_V4)
        Flags |= RT25_TX_IPV4_CS;
    else if (Info->Ip == RT25_IP_V6)
        Flags |= RT25_TX_IPV6_CS;
    else
        return 0;

    if ((Requested & RT25_CSUM_TCP) && Info->Protocol == RT25_PROTO_TCP)
        Flags |= RT25_TX_TCP_CS;
    else if ((Requested & RT25_CSUM_UDP) && Info->Protocol == RT25_PROTO_UDP)
        Flags |= RT25_TX_UDP_CS;
    else if (!(Requested & RT25_CSUM_IP) || Info->Ip != RT25_IP_V4)
        return 0;

    if (Flags & (RT25_TX_TCP_CS | RT25_TX_UDP_CS))
    {
        if (Info->L4Offset > RT25_TX_L4_OFFSET_MAX)
            return 0;
        Flags |= (ULONG)Info->L4Offset << RT25_TX_L4_OFFSET_SHIFT;
    }

    return Flags;
}

FORCEINLINE
ULONG
Rt25SumWords(
    _In_reads_(Length) const UCHAR *Data,
    _In_ ULONG Length,
    _In_ ULONG Sum)
{
    ULONG i;

    for (i = 0; i + 1 < Length; i += 2)
        Sum += Rt25Get16(Data + i);
    if (Length & 1)
        Sum += (ULONG)Data[Length - 1] << 8;
    return Sum;
}

FORCEINLINE
USHORT
Rt25FoldSum(
    _In_ ULONG Sum)
{
    while (Sum >> 16)
        Sum = (Sum & 0xFFFF) + (Sum >> 16);
    return (USHORT)~Sum;
}

FORCEINLINE
VOID
Rt25Put16(
    _Out_writes_(2) UCHAR *Data,
    _In_ USHORT Value)
{
    Data[0] = (UCHAR)(Value >> 8);
    Data[1] = (UCHAR)Value;
}

/* Software fallback for frames the controller would checksum over its own padding. */
FORCEINLINE
VOID
Rt25SoftwareChecksum(
    _Inout_ UCHAR *Frame,
    _In_ const RT25_FRAME_INFO *Info,
    _In_ ULONG Flags)
{
    UCHAR *Ip = Frame + Info->IpOffset, *L4 = Frame + Info->L4Offset, *Field;
    ULONG Sum;
    USHORT Result;

    if ((Flags & RT25_TX_IPV4_CS) && Info->Ip == RT25_IP_V4)
    {
        Rt25Put16(Ip + 10, 0);
        Rt25Put16(Ip + 10, Rt25FoldSum(Rt25SumWords(Ip, Info->IpHeaderLength, 0)));
    }

    if (!(Flags & (RT25_TX_TCP_CS | RT25_TX_UDP_CS)))
        return;

    Field = L4 + ((Flags & RT25_TX_TCP_CS) ? 16 : 6);
    Rt25Put16(Field, 0);
    if (Info->Ip == RT25_IP_V4)
        Sum = Rt25SumWords(Ip + 12, 8, 0);
    else
        Sum = Rt25SumWords(Ip + 8, 32, 0);
    Sum += Info->Protocol + Info->L4Length;
    Result = Rt25FoldSum(Rt25SumWords(L4, Info->L4Length, Sum));
    if (Result == 0 && Info->Protocol == RT25_PROTO_UDP)
        Result = 0xFFFF;
    Rt25Put16(Field, Result);
}

#define RT25_RX_OK_IP               0x01
#define RT25_RX_OK_TCP              0x02
#define RT25_RX_OK_UDP              0x04
#define RT25_RX_BAD_IP              0x10
#define RT25_RX_BAD_TCP             0x20
#define RT25_RX_BAD_UDP             0x40

/* Turns the controller's receive checksum verdict into OK/BAD bits for the checks it actually made. */
FORCEINLINE
ULONG
Rt25RxChecksumResult(
    _In_ ULONG DescriptorFlags,
    _In_ const RT25_FRAME_INFO *Info)
{
    ULONG Proto = DescriptorFlags & RT25_RX_PROTO_MASK, Result = 0;

    if (Proto == 0 || Info->Ip == RT25_IP_NONE)
        return 0;

    if (Info->Ip == RT25_IP_V4)
        Result |= (DescriptorFlags & RT25_RX_IP_FAIL) ? RT25_RX_BAD_IP : RT25_RX_OK_IP;

    if (Proto == RT25_RX_PROTO_TCP && Info->Protocol == RT25_PROTO_TCP)
        Result |= (DescriptorFlags & RT25_RX_TCP_FAIL) ? RT25_RX_BAD_TCP : RT25_RX_OK_TCP;
    else if (Proto == RT25_RX_PROTO_UDP && Info->Protocol == RT25_PROTO_UDP)
        Result |= (DescriptorFlags & RT25_RX_UDP_FAIL) ? RT25_RX_BAD_UDP : RT25_RX_OK_UDP;

    return Result;
}

