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
#define RT25_REG_TNPDS              0x0020
#define RT25_REG_INT_CFG0           0x0034
#define RT25_REG_CR                 0x0037
#define RT25_REG_IMR                0x0038
#define RT25_REG_ISR                0x003C
#define RT25_REG_TCR                0x0040
#define RT25_REG_RCR                0x0044
#define RT25_REG_9346CR             0x0050
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

#define RT25_INT_RX_OK              0x00000001
#define RT25_INT_RX_ERR             0x00000002
#define RT25_INT_TX_OK              0x00000004
#define RT25_INT_TX_ERR             0x00000008
#define RT25_INT_RX_OVERFLOW        0x00000010
#define RT25_INT_LINK_CHANGE        0x00000020
#define RT25_INT_RX_FIFO_OVER       0x00000040
#define RT25_INT_TX_DESC_UNAVAIL    0x00000080
#define RT25_INT_SYSTEM_ERROR       0x00008000
#define RT25_INT_MASK               (RT25_INT_RX_OK | RT25_INT_RX_ERR | RT25_INT_TX_OK | \
                                     RT25_INT_TX_ERR | RT25_INT_LINK_CHANGE)
#define RT25_INT_RX_EVENTS          (RT25_INT_RX_OK | RT25_INT_RX_ERR | RT25_INT_RX_OVERFLOW | \
                                     RT25_INT_RX_FIFO_OVER)
#define RT25_INT_TX_EVENTS          (RT25_INT_TX_OK | RT25_INT_TX_ERR | RT25_INT_TX_DESC_UNAVAIL)

#define RT25_PHYSTATUS_FULL_DUPLEX  0x0001
#define RT25_PHYSTATUS_LINK         0x0002
#define RT25_PHYSTATUS_10M          0x0004
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

#define RT25_PHY_BMCR               0x00
#define RT25_PHY_ADVERTISE          0x04
#define RT25_PHY_CTRL1000           0x09
#define RT25_PHY_PHYSR              0x1A
#define RT25_PHY_OCP_AN_10GBT_CTRL  0xA5D4

#define RT25_BMCR_ANRESTART         0x0200
#define RT25_BMCR_ISOLATE           0x0400
#define RT25_BMCR_PDOWN             0x0800
#define RT25_BMCR_ANENABLE          0x1000

#define RT25_ADVERTISE_10_100       0x01E0
#define RT25_CTRL1000_FULL          0x0200

#define RT25_AN_10GBT_ADV_2500      0x0080
#define RT25_AN_10GBT_ADV_5000      0x0100

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

typedef struct _RT25_DESCRIPTOR
{
    ULONG Flags;
    ULONG Vlan;
    ULONG AddressLow;
    ULONG AddressHigh;
} RT25_DESCRIPTOR, *PRT25_DESCRIPTOR;

C_ASSERT(sizeof(RT25_DESCRIPTOR) == 16);

#define RT25_RING_SIZE              256
#define RT25_BUFFER_SIZE            2048
#define RT25_RECEIVE_SIZE           1536
#define RT25_CRC_SIZE               4
#define RT25_ETH_HEADER             14
#define RT25_MTU                    1500
#define RT25_MAX_FRAME              1514
#define RT25_MIN_FRAME              60

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
    _In_ ULONG Index)
{
    ULONG Flags = RT25_DESC_OWN | RT25_BUFFER_SIZE;

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
    _In_ ULONG Flags)
{
    ULONG Length = Flags & RT25_DESC_LENGTH_MASK;

    if ((Flags & RT25_DESC_OWN) ||
        (Flags & (RT25_DESC_FS | RT25_DESC_LS)) != (RT25_DESC_FS | RT25_DESC_LS) ||
        (Flags & RT25_RX_DESC_RES) ||
        Length < RT25_ETH_HEADER + RT25_CRC_SIZE ||
        Length > RT25_RECEIVE_SIZE)
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
USHORT
Rt25MultiGigAdvertise(
    _In_ USHORT Current,
    _In_ ULONG MaxSpeedMbps)
{
    Current &= ~(RT25_AN_10GBT_ADV_2500 | RT25_AN_10GBT_ADV_5000);
    if (MaxSpeedMbps >= 2500)
        Current |= RT25_AN_10GBT_ADV_2500;
    if (MaxSpeedMbps >= 5000)
        Current |= RT25_AN_10GBT_ADV_5000;
    return Current;
}

FORCEINLINE
ULONG
Rt25TxPaddedLength(
    _In_ ULONG Length)
{
    return Length < RT25_MIN_FRAME ? RT25_MIN_FRAME : Length;
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
