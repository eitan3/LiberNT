/*
 * PROJECT:     LiberNT host-native tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     RTL8125/RTL8126 chip table, descriptors, link, multicast hash and checksums
 * COPYRIGHT:   Copyright 2026 The LiberNT contributors
 */

#undef NDEBUG
#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

typedef uint8_t UCHAR;
typedef uint16_t USHORT;
typedef uint32_t ULONG;
typedef UCHAR BOOLEAN;
typedef ULONG *PULONG;
typedef void VOID;
#define TRUE 1
#define FALSE 0
#define FORCEINLINE static inline
#define C_ASSERT(e) _Static_assert(e, #e)
#define _In_
#define _In_reads_(n)
#define _Out_
#define _Out_writes_(n)
#define _Inout_

#include "rtl8125hw.h"

#define FILTER_DIRECTED     0x01
#define FILTER_MULTICAST    0x02
#define FILTER_ALLMULTI     0x04
#define FILTER_BROADCAST    0x08
#define FILTER_PROMISCUOUS  0x20

static ULONG
ReceiveConfig(UCHAR ChipFlags, ULONG Filter)
{
    return Rt25ReceiveConfig(ChipFlags, Filter, FILTER_DIRECTED, FILTER_MULTICAST, FILTER_ALLMULTI,
                             FILTER_BROADCAST, FILTER_PROMISCUOUS);
}

static void
TestChipTable(void)
{
    const RT25_CHIP *Chip;

    /* The id sits in TCR bits 20-31, with bits 4 and 5 not part of it. */
    assert(Rt25ChipIdFromTcr(0x64900000) == 0x649);
    assert(Rt25ChipIdFromTcr(0x67900F00) == 0x649);
    assert(Rt25ChipIdFromTcr(0x64100000 | RT25_TCR_DEFAULT) == 0x641);

    Chip = Rt25LookupChip(0x649);
    assert(Chip && (Chip->Flags & RT25_CHIP_8126) && Chip->MaxSpeedMbps == 5000);
    Chip = Rt25LookupChip(0x64A);
    assert(Chip && (Chip->Flags & RT25_CHIP_8126));
    Chip = Rt25LookupChip(0x641);
    assert(Chip && Chip->Flags == RT25_CHIP_8125B && Chip->MaxSpeedMbps == 2500);
    Chip = Rt25LookupChip(0x609);
    assert(Chip && (Chip->Flags & RT25_CHIP_8125A) && (Chip->Flags & RT25_CHIP_WIDE_COALESCE));
    Chip = Rt25LookupChip(0x688);
    assert(Chip && Chip->Flags == RT25_CHIP_WIDE_COALESCE);
    Chip = Rt25LookupChip(0x708);
    assert(Chip && Chip->MaxSpeedMbps == 2500);

    /* Bit 11 is outside the 0x7CF mask. */
    Chip = Rt25LookupChip(0x800 | 0x641);
    assert(Chip && Chip->Flags == RT25_CHIP_8125B);

    /* RTL8168 family ids are not driven by this miniport. */
    assert(Rt25LookupChip(0x541) == NULL);
    assert(Rt25LookupChip(0x000) == NULL);
}

static void
TestLinkSpeed(void)
{
    assert(Rt25PhySpeedMbps(0x0000) == 10);
    assert(Rt25PhySpeedMbps(0x0010) == 100);
    assert(Rt25PhySpeedMbps(0x0020) == 1000);
    assert(Rt25PhySpeedMbps(0x0210) == 2500);
    assert(Rt25PhySpeedMbps(0x0220) == 5000);
    assert(Rt25PhySpeedMbps(0x0224 | 0x0008) == 5000);
    assert(Rt25PhySpeedMbps(0x0030) == 0);
    assert(Rt25PhySpeedMbps(0x0400) == 0);

    assert(Rt25MacSpeedMbps(RT25_PHYSTATUS_LINK | RT25_PHYSTATUS_2500M) == 2500);
    assert(Rt25MacSpeedMbps(RT25_PHYSTATUS_LINK | RT25_PHYSTATUS_100M) == 100);
    assert(Rt25MacSpeedMbps(RT25_PHYSTATUS_LINK | RT25_PHYSTATUS_10M) == 10);
    assert(Rt25MacSpeedMbps(RT25_PHYSTATUS_LINK) == 1000);
}

static void
TestAdvertise(void)
{
    assert(Rt25MultiGigAdvertise(0x0000, 1000) == 0x0000);
    assert(Rt25MultiGigAdvertise(0x0000, 2500) == RT25_AN_10GBT_ADV_2500);
    assert(Rt25MultiGigAdvertise(0x0000, 5000) == (RT25_AN_10GBT_ADV_2500 | RT25_AN_10GBT_ADV_5000));
    assert(Rt25MultiGigAdvertise(0x1001 | RT25_AN_10GBT_ADV_5000, 2500) == (0x1001 | RT25_AN_10GBT_ADV_2500));
}

static void
TestDescriptors(void)
{
    ULONG Last = RT25_RING_SIZE - 1;

    assert(sizeof(RT25_DESCRIPTOR) == 16);
    assert(offsetof(RT25_DESCRIPTOR, AddressLow) == 8);

    assert(Rt25NextIndex(0) == 1);
    assert(Rt25NextIndex(Last) == 0);

    assert(Rt25RxArmFlags(0) == (RT25_DESC_OWN | RT25_BUFFER_SIZE));
    assert(Rt25RxArmFlags(Last) == (RT25_DESC_OWN | RT25_DESC_EOR | RT25_BUFFER_SIZE));

    assert(Rt25TxFlags(3, 60) == (RT25_DESC_OWN | RT25_DESC_FS | RT25_DESC_LS | 60));
    assert(Rt25TxFlags(Last, 1514) == (RT25_DESC_OWN | RT25_DESC_EOR | RT25_DESC_FS | RT25_DESC_LS | 1514));
    assert(Rt25TxPaddedLength(14) == RT25_MIN_FRAME);
    assert(Rt25TxPaddedLength(60) == 60);
    assert(Rt25TxPaddedLength(1514) == 1514);
}

static void
TestReceiveLength(void)
{
    ULONG Whole = RT25_DESC_FS | RT25_DESC_LS;

    assert(Rt25RxFrameLength(Whole | 64) == 60);
    assert(Rt25RxFrameLength(Whole | RT25_DESC_EOR | 1518) == 1514);
    assert(Rt25RxFrameLength(Whole | RT25_DESC_OWN | 64) == 0);
    assert(Rt25RxFrameLength(RT25_DESC_FS | 64) == 0);
    assert(Rt25RxFrameLength(RT25_DESC_LS | 64) == 0);
    assert(Rt25RxFrameLength(Whole | RT25_RX_DESC_RES | 64) == 0);
    assert(Rt25RxFrameLength(Whole | 17) == 0);
    assert(Rt25RxFrameLength(Whole | 18) == 14);
    assert(Rt25RxFrameLength(Whole | (RT25_RECEIVE_SIZE + 1)) == 0);
}

static void
TestReceiveConfig(void)
{
    ULONG Base = RT25_RCR_FETCH | RT25_RCR_MXDMA;

    /* Only the first RTL8125 revision lacks the pause slot bit. */
    assert(ReceiveConfig(RT25_CHIP_8125A | RT25_CHIP_WIDE_COALESCE, 0) == Base);
    assert(ReceiveConfig(RT25_CHIP_8125B, 0) == (Base | RT25_RCR_PAUSE_SLOT));
    assert(ReceiveConfig(RT25_CHIP_8126, 0) == (Base | RT25_RCR_PAUSE_SLOT));

    assert((ReceiveConfig(RT25_CHIP_8126, FILTER_DIRECTED | FILTER_BROADCAST) & RT25_RCR_ACCEPT_MASK) ==
           (RT25_RCR_APM | RT25_RCR_AB));
    assert((ReceiveConfig(RT25_CHIP_8126, FILTER_MULTICAST) & RT25_RCR_ACCEPT_MASK) == RT25_RCR_AM);
    assert((ReceiveConfig(RT25_CHIP_8126, FILTER_ALLMULTI) & RT25_RCR_ACCEPT_MASK) == RT25_RCR_AM);
    assert((ReceiveConfig(RT25_CHIP_8126, FILTER_PROMISCUOUS) & RT25_RCR_ACCEPT_MASK) ==
           (RT25_RCR_AAP | RT25_RCR_APM | RT25_RCR_AM | RT25_RCR_AB));
}

static void
TestOcpCommand(void)
{
    /* MAC and PHY OCP windows take the register address in bits 16-30 and data in 0-15. */
    assert(Rt25OcpCommand(0xA434, FALSE, 0) == (0xA434u << 15));
    assert(Rt25OcpCommand(0xA5D4, TRUE, 0x0180) == (RT25_OCP_FLAG | (0xA5D4u << 15) | 0x0180));
    assert(Rt25OcpCommand(RT25_OCP_PHY_BASE + RT25_PHY_PHYSR * 2, FALSE, 0) == (0xA434u << 15));
}

static void
TestMulticastHash(void)
{
    static const UCHAR AllHosts[6] = { 0x01, 0x00, 0x5E, 0x00, 0x00, 0x01 };
    static const UCHAR Pair[12] = { 0x33, 0x33, 0x00, 0x00, 0x00, 0x01, 0x01, 0x00, 0x5E, 0x7F, 0xFF, 0xFA };
    ULONG Mar0, Mar4;

    assert(Rt25EtherCrc(AllHosts) == 0x7FA32D9B);
    assert(Rt25Swap32(0x11223344) == 0x44332211);

    /* Hash bit 31 lands in the low word, which goes to MAR4. */
    Rt25MulticastFilter(AllHosts, 1, &Mar0, &Mar4);
    assert(Mar0 == 0 && Mar4 == 0x00000080);

    /* Bits 62 and 43 are both in the high word. */
    Rt25MulticastFilter(Pair, 2, &Mar0, &Mar4);
    assert(Mar0 == 0x00080040 && Mar4 == 0);

    Rt25MulticastFilter(Pair, 0, &Mar0, &Mar4);
    assert(Mar0 == 0 && Mar4 == 0);
}

/* 10.0.0.1:1234 -> 10.0.0.2:53, payload "abc", checksums zeroed. */
static const UCHAR Udp4Frame[45] = {
    0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x02, 0x00, 0x00, 0x00, 0x00, 0x01, 0x08, 0x00,
    0x45, 0x00, 0x00, 0x1F, 0x00, 0x01, 0x00, 0x00, 0x40, 0x11, 0x00, 0x00,
    0x0A, 0x00, 0x00, 0x01, 0x0A, 0x00, 0x00, 0x02,
    0x04, 0xD2, 0x00, 0x35, 0x00, 0x0B, 0x00, 0x00, 'a', 'b', 'c'
};

/* ::1 port 80 -> ::2 port 1000, bare SYN. */
static void
BuildTcp6Frame(UCHAR *Frame, BOOLEAN Vlan)
{
    UCHAR *p = Frame;

    memset(Frame, 0, 18 + 40 + 20);
    p += 12;
    if (Vlan)
    {
        p[0] = 0x81; p[1] = 0x00; p[2] = 0x00; p[3] = 0x05;
        p += 4;
    }
    p[0] = 0x86; p[1] = 0xDD;
    p += 2;
    p[0] = 0x60; p[5] = 20; p[6] = RT25_PROTO_TCP; p[7] = 64;
    p[23] = 1;
    p[39] = 2;
    p += 40;
    p[1] = 80; p[2] = 0x03; p[3] = 0xE8; p[7] = 1; p[12] = 0x50; p[13] = 0x02; p[14] = 0x04;
}

static void
TestParseFrame(void)
{
    UCHAR Frame[80];
    RT25_FRAME_INFO Info;

    Rt25ParseFrame(Udp4Frame, sizeof(Udp4Frame), &Info);
    assert(Info.Ip == RT25_IP_V4 && Info.Protocol == RT25_PROTO_UDP);
    assert(Info.IpOffset == 14 && Info.IpHeaderLength == 20 && Info.L4Offset == 34 && Info.L4Length == 11);

    /* Truncated: the IP total length runs past the frame. */
    Rt25ParseFrame(Udp4Frame, sizeof(Udp4Frame) - 1, &Info);
    assert(Info.Ip == RT25_IP_NONE);

    /* Non-first fragment keeps the IP header but no L4. */
    memcpy(Frame, Udp4Frame, sizeof(Udp4Frame));
    Frame[20] = 0x00; Frame[21] = 0x10;
    Rt25ParseFrame(Frame, sizeof(Udp4Frame), &Info);
    assert(Info.Ip == RT25_IP_V4 && Info.Protocol == 0 && Info.L4Offset == 0);

    /* More-fragments flag alone is a fragment too. */
    Frame[20] = 0x20; Frame[21] = 0x00;
    Rt25ParseFrame(Frame, sizeof(Udp4Frame), &Info);
    assert(Info.Ip == RT25_IP_V4 && Info.Protocol == 0);

    /* Don't-fragment is not. */
    Frame[20] = 0x40;
    Rt25ParseFrame(Frame, sizeof(Udp4Frame), &Info);
    assert(Info.Protocol == RT25_PROTO_UDP);

    BuildTcp6Frame(Frame, FALSE);
    Rt25ParseFrame(Frame, 14 + 40 + 20, &Info);
    assert(Info.Ip == RT25_IP_V6 && Info.Protocol == RT25_PROTO_TCP);
    assert(Info.IpOffset == 14 && Info.L4Offset == 54 && Info.L4Length == 20);

    BuildTcp6Frame(Frame, TRUE);
    Rt25ParseFrame(Frame, 18 + 40 + 20, &Info);
    assert(Info.Ip == RT25_IP_V6 && Info.IpOffset == 18 && Info.L4Offset == 58);

    /* A TCP header shorter than 20 bytes is not offloaded. */
    BuildTcp6Frame(Frame, FALSE);
    Frame[19] = 12;
    Rt25ParseFrame(Frame, 14 + 40 + 12, &Info);
    assert(Info.Ip == RT25_IP_V6 && Info.Protocol == 0);

    /* ARP and runts. */
    memset(Frame, 0, sizeof(Frame));
    Frame[12] = 0x08; Frame[13] = 0x06;
    Rt25ParseFrame(Frame, 60, &Info);
    assert(Info.Ip == RT25_IP_NONE);
    Rt25ParseFrame(Frame, 10, &Info);
    assert(Info.Ip == RT25_IP_NONE);
}

static void
TestTxChecksumFlags(void)
{
    RT25_FRAME_INFO Info;
    UCHAR Frame[80];

    Rt25ParseFrame(Udp4Frame, sizeof(Udp4Frame), &Info);
    assert(Rt25TxChecksumFlags(&Info, RT25_CSUM_IP | RT25_CSUM_UDP) ==
           (RT25_TX_IPV4_CS | RT25_TX_UDP_CS | (34u << RT25_TX_L4_OFFSET_SHIFT)));
    assert(Rt25TxChecksumFlags(&Info, RT25_CSUM_IP) == RT25_TX_IPV4_CS);
    /* TCP asked for a UDP frame. */
    assert(Rt25TxChecksumFlags(&Info, RT25_CSUM_TCP) == 0);
    assert(Rt25TxChecksumFlags(&Info, 0) == 0);

    BuildTcp6Frame(Frame, TRUE);
    Rt25ParseFrame(Frame, 18 + 40 + 20, &Info);
    assert(Rt25TxChecksumFlags(&Info, RT25_CSUM_TCP) ==
           (RT25_TX_IPV6_CS | RT25_TX_TCP_CS | (58u << RT25_TX_L4_OFFSET_SHIFT)));
    /* IPv6 has no header checksum. */
    assert(Rt25TxChecksumFlags(&Info, RT25_CSUM_IP) == 0);

    Info.L4Offset = RT25_TX_L4_OFFSET_MAX + 1;
    assert(Rt25TxChecksumFlags(&Info, RT25_CSUM_TCP) == 0);

    Info.Ip = RT25_IP_NONE;
    assert(Rt25TxChecksumFlags(&Info, RT25_CSUM_IP | RT25_CSUM_TCP | RT25_CSUM_UDP) == 0);
}

static void
TestSoftwareChecksum(void)
{
    /* Classic IPv4 header example; its checksum is 0xB861. */
    UCHAR Classic[34] = {
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x08, 0x00,
        0x45, 0x00, 0x00, 0x14, 0x00, 0x00, 0x40, 0x00, 0x40, 0x11, 0x00, 0x00,
        0xC0, 0xA8, 0x00, 0x01, 0xC0, 0xA8, 0x00, 0xC7
    };
    UCHAR Frame[80];
    RT25_FRAME_INFO Info;

    /* Parsed with a total length of 20 so the bare header fits, then set back to 0x73. */
    Rt25ParseFrame(Classic, sizeof(Classic), &Info);
    assert(Info.Ip == RT25_IP_V4);
    Classic[16] = 0x00; Classic[17] = 0x73;
    Rt25SoftwareChecksum(Classic, &Info, RT25_TX_IPV4_CS);
    assert(Classic[24] == 0xB8 && Classic[25] == 0x61);

    memcpy(Frame, Udp4Frame, sizeof(Udp4Frame));
    Rt25ParseFrame(Frame, sizeof(Udp4Frame), &Info);
    Rt25SoftwareChecksum(Frame, &Info, Rt25TxChecksumFlags(&Info, RT25_CSUM_IP | RT25_CSUM_UDP));
    assert(Rt25Get16(Frame + 24) == 0x66CB);
    assert(Rt25Get16(Frame + 40) == 0x226C);

    /* Running it again over the filled-in fields gives the same answer. */
    Rt25SoftwareChecksum(Frame, &Info, RT25_TX_IPV4_CS | RT25_TX_UDP_CS);
    assert(Rt25Get16(Frame + 24) == 0x66CB && Rt25Get16(Frame + 40) == 0x226C);

    /* IP only leaves the UDP field alone. */
    memcpy(Frame, Udp4Frame, sizeof(Udp4Frame));
    Rt25SoftwareChecksum(Frame, &Info, RT25_TX_IPV4_CS);
    assert(Rt25Get16(Frame + 24) == 0x66CB && Rt25Get16(Frame + 40) == 0);

    BuildTcp6Frame(Frame, FALSE);
    Rt25ParseFrame(Frame, 14 + 40 + 20, &Info);
    Rt25SoftwareChecksum(Frame, &Info, Rt25TxChecksumFlags(&Info, RT25_CSUM_TCP));
    assert(Rt25Get16(Frame + 54 + 16) == 0xA7A7);

    assert(Rt25FoldSum(0x1FFFE) == 0);
    assert(Rt25SumWords((const UCHAR *)"\x12\x34\x56", 3, 0) == 0x1234 + 0x5600);
}

static void
TestRxChecksumResult(void)
{
    RT25_FRAME_INFO Udp4, Tcp6, None;
    UCHAR Frame[80];

    Rt25ParseFrame(Udp4Frame, sizeof(Udp4Frame), &Udp4);
    BuildTcp6Frame(Frame, FALSE);
    Rt25ParseFrame(Frame, 14 + 40 + 20, &Tcp6);
    memset(&None, 0, sizeof(None));

    assert(Rt25RxChecksumResult(RT25_RX_PROTO_UDP, &Udp4) == (RT25_RX_OK_IP | RT25_RX_OK_UDP));
    assert(Rt25RxChecksumResult(RT25_RX_PROTO_UDP | RT25_RX_UDP_FAIL, &Udp4) == (RT25_RX_OK_IP | RT25_RX_BAD_UDP));
    assert(Rt25RxChecksumResult(RT25_RX_PROTO_UDP | RT25_RX_IP_FAIL, &Udp4) == (RT25_RX_BAD_IP | RT25_RX_OK_UDP));
    /* Plain IP verdict (both protocol bits) for a frame whose L4 was not checked. */
    assert(Rt25RxChecksumResult(RT25_RX_PROTO_MASK, &Udp4) == RT25_RX_OK_IP);
    /* The hardware says TCP but the frame is UDP: report only what both agree on. */
    assert(Rt25RxChecksumResult(RT25_RX_PROTO_TCP, &Udp4) == RT25_RX_OK_IP);

    assert(Rt25RxChecksumResult(RT25_RX_PROTO_TCP, &Tcp6) == RT25_RX_OK_TCP);
    assert(Rt25RxChecksumResult(RT25_RX_PROTO_TCP | RT25_RX_TCP_FAIL, &Tcp6) == RT25_RX_BAD_TCP);

    assert(Rt25RxChecksumResult(0, &Udp4) == 0);
    assert(Rt25RxChecksumResult(RT25_RX_PROTO_TCP, &None) == 0);
}

int
main(void)
{
    TestChipTable();
    TestLinkSpeed();
    TestAdvertise();
    TestDescriptors();
    TestReceiveLength();
    TestReceiveConfig();
    TestOcpCommand();
    TestMulticastHash();
    TestParseFrame();
    TestTxChecksumFlags();
    TestSoftwareChecksum();
    TestRxChecksumResult();
    printf("rtl8125: all tests passed\n");
    return 0;
}
