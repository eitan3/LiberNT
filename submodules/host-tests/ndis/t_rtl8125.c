/*
 * PROJECT:     LiberNT host-native tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     RTL8125/RTL8126 chip table, descriptor and link decoding
 * COPYRIGHT:   Copyright 2026 The LiberNT contributors
 */

#undef NDEBUG
#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

typedef uint8_t UCHAR;
typedef uint16_t USHORT;
typedef uint32_t ULONG;
typedef UCHAR BOOLEAN;
#define TRUE 1
#define FALSE 0
#define FORCEINLINE static inline
#define C_ASSERT(e) _Static_assert(e, #e)
#define _In_

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
    printf("rtl8125: all tests passed\n");
    return 0;
}
