/*
 * PROJECT:     LiberNT Realtek RTL8125/RTL8126 Ethernet Driver
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Controller bring-up, PHY access and link state
 * COPYRIGHT:   Copyright 2026 The LiberNT contributors
 */

#include "rtl8125.h"

static const UCHAR Rt25FallbackAddress[ETH_LENGTH_OF_ADDRESS] = { 0x02, 0xDE, 0xAD, 0x81, 0x25, 0x00 };

typedef struct _RT25_CONTROLLER_UPDATE
{
    USHORT Register;
    USHORT Clear;
    USHORT Set;
} RT25_CONTROLLER_UPDATE;

/* Same MAC OCP setup the kdnet Realtek transport applies before it starts an RTL8125. */
static const RT25_CONTROLLER_UPDATE Rt25Setup[] =
{
    { 0xD40A, 0x0010, 0x0000 },
    { 0xC140, 0xFFFF, 0xFFFF },
    { 0xC142, 0xFFFF, 0xFFFF },
    { 0xD3E2, 0x0FFF, 0x03A9 },
    { 0xD3E4, 0x00FF, 0x0000 },
    { 0xE860, 0x0000, 0x0080 },
    { RT25_OCP_DESCRIPTOR, 0x0001, 0x0000 },
    { 0xC0B4, 0x0000, 0x000C },
    { 0xEB6A, 0x00FF, 0x0033 },
    { 0xEB50, 0x03E0, 0x0040 },
    { 0xE056, 0x00F0, 0x0000 },
    { 0xE040, 0x1000, 0x0000 },
    { 0xEA1C, 0x0003, 0x0001 },
    { 0xE0C0, 0x4F0F, 0x4403 },
    { 0xE052, 0x0080, 0x0068 },
    { 0xD430, 0x0FFF, 0x047F },
    { 0xEA1C, 0x0004, 0x0000 }
};

static
VOID
Rt25Stall(
    _In_ ULONG Microseconds)
{
    NdisStallExecution(Microseconds);
}

static
BOOLEAN
Rt25WaitOcp(
    _In_ PRT25_ADAPTER Adapter,
    _In_ ULONG Register,
    _In_ BOOLEAN Set)
{
    ULONG i;

    for (i = 0; i < 100; i++)
    {
        if (!(Rt25Read32(Adapter, Register) & RT25_OCP_FLAG) == !Set)
            return TRUE;

        Rt25Stall(10);
    }

    return FALSE;
}

static
VOID
Rt25WaitMcu(
    _In_ PRT25_ADAPTER Adapter,
    _In_ UCHAR Bits)
{
    ULONG i;

    for (i = 0; i < 100 && (Rt25Read8(Adapter, RT25_REG_MCU) & Bits) != Bits; i++)
        Rt25Stall(100);
}

static
USHORT
Rt25ReadMacOcp(
    _In_ PRT25_ADAPTER Adapter,
    _In_ ULONG Address)
{
    Rt25Write32(Adapter, RT25_REG_OCPDR, Rt25OcpCommand(Address, FALSE, 0));
    return (USHORT)Rt25Read32(Adapter, RT25_REG_OCPDR);
}

static
VOID
Rt25WriteMacOcp(
    _In_ PRT25_ADAPTER Adapter,
    _In_ ULONG Address,
    _In_ USHORT Value)
{
    Rt25Write32(Adapter, RT25_REG_OCPDR, Rt25OcpCommand(Address, TRUE, Value));
}

static
VOID
Rt25ModifyMacOcp(
    _In_ PRT25_ADAPTER Adapter,
    _In_ ULONG Address,
    _In_ USHORT Clear,
    _In_ USHORT Set)
{
    Rt25WriteMacOcp(Adapter, Address, (USHORT)((Rt25ReadMacOcp(Adapter, Address) & ~Clear) | Set));
}

static
BOOLEAN
Rt25ReadPhyOcp(
    _In_ PRT25_ADAPTER Adapter,
    _In_ ULONG Address,
    _Out_ PUSHORT Value)
{
    *Value = 0;
    Rt25Write32(Adapter, RT25_REG_GPHY_OCP, Rt25OcpCommand(Address, FALSE, 0));
    if (!Rt25WaitOcp(Adapter, RT25_REG_GPHY_OCP, TRUE))
        return FALSE;

    *Value = (USHORT)Rt25Read32(Adapter, RT25_REG_GPHY_OCP);
    return TRUE;
}

static
BOOLEAN
Rt25WritePhyOcp(
    _In_ PRT25_ADAPTER Adapter,
    _In_ ULONG Address,
    _In_ USHORT Value)
{
    Rt25Write32(Adapter, RT25_REG_GPHY_OCP, Rt25OcpCommand(Address, TRUE, Value));
    return Rt25WaitOcp(Adapter, RT25_REG_GPHY_OCP, FALSE);
}

static
BOOLEAN
Rt25ReadPhy(
    _In_ PRT25_ADAPTER Adapter,
    _In_ ULONG Register,
    _Out_ PUSHORT Value)
{
    return Rt25ReadPhyOcp(Adapter, RT25_OCP_PHY_BASE + Register * 2, Value);
}

static
BOOLEAN
Rt25WritePhy(
    _In_ PRT25_ADAPTER Adapter,
    _In_ ULONG Register,
    _In_ USHORT Value)
{
    return Rt25WritePhyOcp(Adapter, RT25_OCP_PHY_BASE + Register * 2, Value);
}

static
BOOLEAN
Rt25ValidAddress(
    _In_reads_(ETH_LENGTH_OF_ADDRESS) const UCHAR *Address)
{
    UCHAR Any = 0, All = 0xFF;
    ULONG i;

    for (i = 0; i < ETH_LENGTH_OF_ADDRESS; i++)
    {
        Any |= Address[i];
        All &= Address[i];
    }

    return Any && All != 0xFF && !(Address[0] & 1);
}

VOID
Rt25DisableInterrupts(
    _In_ PRT25_ADAPTER Adapter)
{
    Rt25Write32(Adapter, RT25_REG_IMR, 0);
}

VOID
Rt25EnableInterrupts(
    _In_ PRT25_ADAPTER Adapter)
{
    if (Adapter->HardwareRunning)
        Rt25Write32(Adapter, RT25_REG_IMR, RT25_INT_MASK);
}

VOID
Rt25ResumeInterrupts(
    _In_ PRT25_ADAPTER Adapter)
{
    NdisAcquireSpinLock(&Adapter->StateLock);
    Rt25EnableInterrupts(Adapter);
    NdisReleaseSpinLock(&Adapter->StateLock);
}

/* Once this returns, no DPC can unmask the controller again until Rt25StartHardware. */
VOID
Rt25QuiesceHardware(
    _In_ PRT25_ADAPTER Adapter)
{
    NdisAcquireSpinLock(&Adapter->StateLock);
    Adapter->HardwareRunning = FALSE;
    if (Adapter->RegisterBase && !Adapter->SurpriseRemoved)
        Rt25DisableInterrupts(Adapter);
    NdisReleaseSpinLock(&Adapter->StateLock);
}

NDIS_STATUS
Rt25IdentifyChip(
    _In_ PRT25_ADAPTER Adapter)
{
    const RT25_CHIP *Chip;
    ULONG Tcr;

    Tcr = Rt25Read32(Adapter, RT25_REG_TCR);
    if (Tcr == 0xFFFFFFFF)
    {
        DPRINT1("RTL8125: register window reads all ones\n");
        return NDIS_STATUS_ADAPTER_NOT_FOUND;
    }

    Adapter->ChipId = Rt25ChipIdFromTcr(Tcr);
    Chip = Rt25LookupChip(Adapter->ChipId);
    if (!Chip)
    {
        DPRINT1("RTL8125: unsupported chip id 0x%03x (device 0x%04x)\n", Adapter->ChipId, Adapter->DeviceId);
        return NDIS_STATUS_ADAPTER_NOT_FOUND;
    }

    Adapter->ChipFlags = Chip->Flags;
    Adapter->MaxSpeedMbps = Chip->MaxSpeedMbps;
    Adapter->ChipName = Chip->Name;
    DPRINT1("RTL8125: %s, chip id 0x%03x, up to %lu Mbps\n", Chip->Name, Adapter->ChipId, Chip->MaxSpeedMbps);
    return NDIS_STATUS_SUCCESS;
}

VOID
Rt25ReadMacAddress(
    _In_ PRT25_ADAPTER Adapter)
{
    UCHAR Address[ETH_LENGTH_OF_ADDRESS];
    ULONG i;

    for (i = 0; i < ETH_LENGTH_OF_ADDRESS; i++)
        Address[i] = Rt25Read8(Adapter, RT25_REG_IDR0 + i);

    if (!Rt25ValidAddress(Address))
    {
        for (i = 0; i < ETH_LENGTH_OF_ADDRESS; i++)
            Address[i] = Rt25Read8(Adapter, RT25_REG_MAC_BACKUP + i);
    }

    if (!Rt25ValidAddress(Address))
    {
        DPRINT1("RTL8125: no valid station address in the controller, using a local one\n");
        NdisMoveMemory(Address, Rt25FallbackAddress, ETH_LENGTH_OF_ADDRESS);
    }

    NdisMoveMemory(Adapter->PermanentMacAddress, Address, ETH_LENGTH_OF_ADDRESS);
}

static
VOID
Rt25WriteMacAddress(
    _In_ PRT25_ADAPTER Adapter)
{
    const UCHAR *Address = Adapter->CurrentMacAddress;
    ULONG Low, High;

    Low = Address[0] | (Address[1] << 8) | (Address[2] << 16) | ((ULONG)Address[3] << 24);
    High = Address[4] | (Address[5] << 8);
    Rt25Write8(Adapter, RT25_REG_9346CR, RT25_9346CR_UNLOCK);
    Rt25Write32(Adapter, RT25_REG_IDR0 + 4, High);
    Rt25Read8(Adapter, RT25_REG_CR);
    Rt25Write32(Adapter, RT25_REG_IDR0, Low);
    Rt25Read8(Adapter, RT25_REG_CR);
    Rt25Write8(Adapter, RT25_REG_9346CR, RT25_9346CR_LOCK);
}

static
VOID
Rt25LeaveOutOfBand(
    _In_ PRT25_ADAPTER Adapter)
{
    Rt25WriteMacOcp(Adapter, RT25_OCP_REALWOW, 0x00FF);
    Rt25Write32(Adapter, RT25_REG_MISC, Rt25Read32(Adapter, RT25_REG_MISC) | RT25_MISC_RECEIVE_GATE);
    Rt25Stall(2000);
    Rt25WaitMcu(Adapter, RT25_MCU_EMPTY);
    Rt25Write8(Adapter, RT25_REG_CR, Rt25Read8(Adapter, RT25_REG_CR) & ~(RT25_CR_TE | RT25_CR_RE));
    Rt25Stall(1000);
    Rt25Write8(Adapter, RT25_REG_MCU, Rt25Read8(Adapter, RT25_REG_MCU) & ~RT25_MCU_OUT_OF_BAND);
    Rt25ModifyMacOcp(Adapter, RT25_OCP_LINK_LIST, RT25_OCP_LINK_LIST_HOLD, 0);
    Rt25WaitMcu(Adapter, RT25_MCU_LINK_LIST_READY);
    Rt25WriteMacOcp(Adapter, 0xC0AA, 0x07D0);
    Rt25WriteMacOcp(Adapter, 0xC0A6, 0x0150);
    Rt25WriteMacOcp(Adapter, 0xC01E, 0x5555);
    Rt25WaitMcu(Adapter, RT25_MCU_LINK_LIST_READY);
}

static
VOID
Rt25DisableZeroRxDcTimeout(
    _In_ PRT25_ADAPTER Adapter)
{
    UCHAR Value;

    if (NdisMGetBusData(Adapter->MiniportHandle, PCI_WHICHSPACE_CONFIG, RT25_PCI_GEN3_RELATED,
                        &Value, sizeof(Value)) != sizeof(Value))
    {
        DPRINT1("RTL8125: PCIe extended configuration space is not reachable\n");
        return;
    }

    if (!(Value & RT25_GEN3_ZRXDC_NONCOMPL))
        return;

    Value &= ~RT25_GEN3_ZRXDC_NONCOMPL;
    if (NdisMSetBusData(Adapter->MiniportHandle, PCI_WHICHSPACE_CONFIG, RT25_PCI_GEN3_RELATED,
                        &Value, sizeof(Value)) != sizeof(Value))
    {
        DPRINT1("RTL8125: could not clear the Gen3 ZRXDC bit\n");
    }
}

static
VOID
Rt25StartMac(
    _In_ PRT25_ADAPTER Adapter)
{
    ULONG i, End;

    Rt25Write16(Adapter, RT25_REG_0382, 0x221B);
    Rt25Write32(Adapter, RT25_REG_RSS_CTRL, 0);
    Rt25Write16(Adapter, RT25_REG_QUEUES, 0);
    if (Adapter->ChipFlags & RT25_CHIP_8126)
    {
        Rt25DisableZeroRxDcTimeout(Adapter);
        Rt25Write8(Adapter, RT25_REG_00D8, Rt25Read8(Adapter, RT25_REG_00D8) & ~RT25_00D8_8126_CLEAR);
    }

    for (i = 0; i < sizeof(Rt25Setup) / sizeof(Rt25Setup[0]); i++)
        Rt25ModifyMacOcp(Adapter, Rt25Setup[i].Register, Rt25Setup[i].Clear, Rt25Setup[i].Set);

    if (Adapter->ChipFlags & RT25_CHIP_8126)
        Rt25ModifyMacOcp(Adapter, 0xEA1C, 0x0300, 0x0000);

    Rt25ModifyMacOcp(Adapter, RT25_OCP_TX_FIFO, 0x0700,
                     (Adapter->ChipFlags & RT25_CHIP_8125B) ? 0x0200 :
                     (Adapter->ChipFlags & RT25_CHIP_8126) ? 0x0400 : 0x0300);
    Rt25ModifyMacOcp(Adapter, RT25_OCP_TX_QUEUES, 0x0C30,
                     (Adapter->ChipFlags & RT25_CHIP_8125B) ? 0x0000 : 0x0020);
    Rt25ModifyMacOcp(Adapter, RT25_OCP_TCAM, 0, 0x0001);
    Rt25Stall(1);
    Rt25ModifyMacOcp(Adapter, RT25_OCP_TCAM, 0x0001, 0);
    Rt25Write16(Adapter, RT25_REG_TCAM, Rt25Read16(Adapter, RT25_REG_TCAM) & ~0x0030);
    Rt25WriteMacOcp(Adapter, 0xE098, 0xC302);
    for (i = 0; i < 1000 && (Rt25ReadMacOcp(Adapter, RT25_OCP_BUSY) & RT25_OCP_BUSY_BIT); i++)
        Rt25Stall(10);

    End = (Adapter->ChipFlags & RT25_CHIP_WIDE_COALESCE) ? RT25_COALESCE_WIDE_END : RT25_COALESCE_END;
    for (i = RT25_REG_COALESCE; i < End; i += 4)
        Rt25Write32(Adapter, i, 0);
    if (!(Adapter->ChipFlags & RT25_CHIP_WIDE_COALESCE))
        Rt25Write16(Adapter, RT25_REG_INT_CFG1, 0);
    Rt25Write32(Adapter, RT25_REG_INT_CFG0, 0);

    Rt25Write32(Adapter, RT25_REG_MISC, Rt25Read32(Adapter, RT25_REG_MISC) & ~RT25_MISC_RECEIVE_GATE);
}

/* FIXME: the MAC-side EEE bits follow observed r8169 behaviour and are untested on RTL8125/RTL8126 hardware */
static
VOID
Rt25ConfigureEee(
    _In_ PRT25_ADAPTER Adapter)
{
    USHORT Value, Advertise = 0, Advertise2 = 0;

    /* Turning EEE off only withdraws the advertisement, so no low-power idle is ever negotiated. */
    if (Adapter->EeeEnabled)
    {
        Advertise = RT25_EEE_ADV_100 | RT25_EEE_ADV_1000;
        if (Adapter->MaxSpeedMbps >= 2500)
            Advertise2 |= RT25_EEE_ADV2_2500;
        if (Adapter->MaxSpeedMbps >= 5000)
            Advertise2 |= RT25_EEE_ADV2_5000;
        Rt25ModifyMacOcp(Adapter, RT25_OCP_EEE_MAC, 0, RT25_OCP_EEE_MAC_LPI);
        Rt25ModifyMacOcp(Adapter, RT25_OCP_EEE_TIMER, 0, RT25_OCP_EEE_TIMER_ENABLE);
    }

    if (Rt25ReadPhyOcp(Adapter, RT25_PHY_OCP_EEE_ADV, &Value))
    {
        Rt25WritePhyOcp(Adapter, RT25_PHY_OCP_EEE_ADV,
                        (USHORT)((Value & ~(RT25_EEE_ADV_100 | RT25_EEE_ADV_1000)) | Advertise));
    }

    if (Rt25ReadPhyOcp(Adapter, RT25_PHY_OCP_EEE_ADV2, &Value))
    {
        Rt25WritePhyOcp(Adapter, RT25_PHY_OCP_EEE_ADV2,
                        (USHORT)((Value & ~(RT25_EEE_ADV2_2500 | RT25_EEE_ADV2_5000)) | Advertise2));
    }
}

static
VOID
Rt25StartPhy(
    _In_ PRT25_ADAPTER Adapter)
{
    RT25_ADVERTISEMENT Current, Wanted;
    ULONG FlowControl = Adapter->FlowControl;

    /* These controllers do not support pause frames together with jumbo frames. */
    if (Adapter->MaxFrame > RT25_MAX_FRAME)
        FlowControl = RT25_FLOW_OFF;

    /* TODO: apply Realtek's PHY parameter patches; only the advertisement is set here. */
    NdisAcquireSpinLock(&Adapter->PhyLock);

    if (Rt25ReadPhy(Adapter, RT25_PHY_ADVERTISE, &Current.Advertise) &&
        Rt25ReadPhy(Adapter, RT25_PHY_CTRL1000, &Current.Ctrl1000) &&
        Rt25ReadPhyOcp(Adapter, RT25_PHY_OCP_AN_10GBT_CTRL, &Current.MultiGig))
    {
        if (!Rt25LinkAdvertisement(Adapter->SpeedDuplex, Adapter->MaxSpeedMbps, FlowControl, &Current, &Wanted))
        {
            DPRINT1("RTL8125: speed setting %lu is not available, negotiating all speeds\n", Adapter->SpeedDuplex);
            Rt25LinkAdvertisement(RT25_SPEED_AUTO, Adapter->MaxSpeedMbps, FlowControl, &Current, &Wanted);
        }

        Rt25WritePhy(Adapter, RT25_PHY_ADVERTISE, Wanted.Advertise);
        Rt25WritePhy(Adapter, RT25_PHY_CTRL1000, Wanted.Ctrl1000);
        Rt25WritePhyOcp(Adapter, RT25_PHY_OCP_AN_10GBT_CTRL, Wanted.MultiGig);
    }
    else
    {
        DPRINT1("RTL8125: PHY advertisement registers not readable, keeping the PHY defaults\n");
    }

    Rt25ConfigureEee(Adapter);
    Rt25WritePhy(Adapter, RT25_PHY_BMCR, RT25_BMCR_ANENABLE | RT25_BMCR_ANRESTART);

    NdisReleaseSpinLock(&Adapter->PhyLock);
}

static
BOOLEAN
Rt25TallyCommand(
    _In_ PRT25_ADAPTER Adapter,
    _In_ ULONG Command)
{
    ULONG i;

    Rt25Write32(Adapter, RT25_REG_COUNTER_HIGH, (ULONG)((ULONG64)Adapter->TallyPhysical.QuadPart >> 32));
    Rt25Write32(Adapter, RT25_REG_COUNTER_LOW, Adapter->TallyPhysical.LowPart | Command);
    for (i = 0; i < 1000; i++)
    {
        if (!(Rt25Read32(Adapter, RT25_REG_COUNTER_LOW) & (RT25_COUNTER_RESET | RT25_COUNTER_DUMP)))
            return TRUE;
        Rt25Stall(10);
    }

    return FALSE;
}

/* Folds the controller's error counters into the running totals and clears them. */
VOID
Rt25UpdateTally(
    _In_ PRT25_ADAPTER Adapter)
{
    RT25_TALLY Snapshot;

    if (!Adapter->TallyVirtual)
        return;

    NdisAcquireSpinLock(&Adapter->TallyLock);
    if (Adapter->HardwareRunning && !Adapter->SurpriseRemoved && Rt25TallyCommand(Adapter, RT25_COUNTER_DUMP))
    {
        KeMemoryBarrier();
        NdisMoveMemory(&Snapshot, Adapter->TallyVirtual, sizeof(Snapshot));
        Rt25AccumulateTally(&Adapter->Tally, &Snapshot);
        Rt25TallyCommand(Adapter, RT25_COUNTER_RESET);
    }
    NdisReleaseSpinLock(&Adapter->TallyLock);
}

VOID
Rt25ApplyReceiveFilter(
    _In_ PRT25_ADAPTER Adapter)
{
    ULONG Config, Mar0 = 0, Mar4 = 0;

    if (!Adapter->HardwareRunning)
        return;

    Config = Rt25ReceiveConfig(Adapter->ChipFlags,
                               Adapter->PacketFilter,
                               NDIS_PACKET_TYPE_DIRECTED,
                               NDIS_PACKET_TYPE_MULTICAST,
                               NDIS_PACKET_TYPE_ALL_MULTICAST,
                               NDIS_PACKET_TYPE_BROADCAST,
                               NDIS_PACKET_TYPE_PROMISCUOUS);

    if (Adapter->PacketFilter & (NDIS_PACKET_TYPE_ALL_MULTICAST | NDIS_PACKET_TYPE_PROMISCUOUS))
    {
        Mar0 = Mar4 = 0xFFFFFFFF;
    }
    else if (Adapter->PacketFilter & NDIS_PACKET_TYPE_MULTICAST)
    {
        Rt25MulticastFilter(&Adapter->MulticastList[0][0], Adapter->MulticastCount, &Mar0, &Mar4);
    }

    Rt25Write32(Adapter, RT25_REG_MAR0 + 4, Mar4);
    Rt25Write32(Adapter, RT25_REG_MAR0, Mar0);
    Rt25Write32(Adapter, RT25_REG_RCR, Config);
}

NDIS_STATUS
Rt25StartHardware(
    _In_ PRT25_ADAPTER Adapter)
{
    USHORT Command;
    ULONG i;

    Rt25DisableInterrupts(Adapter);
    Rt25Write32(Adapter, RT25_REG_ISR, 0xFFFFFFFF);
    Rt25LeaveOutOfBand(Adapter);

    Rt25Write8(Adapter, RT25_REG_CR, RT25_CR_RST);
    for (i = 0; i < 1000; i++)
    {
        Rt25Stall(100);
        if (!(Rt25Read8(Adapter, RT25_REG_CR) & RT25_CR_RST))
            break;
    }

    if (i == 1000)
    {
        DPRINT1("RTL8125: controller reset timed out\n");
        return NDIS_STATUS_HARD_ERRORS;
    }

    Rt25DisableInterrupts(Adapter);
    Rt25Write32(Adapter, RT25_REG_ISR, 0xFFFFFFFF);

    Rt25Write32(Adapter, RT25_REG_RCR,
                Rt25ReceiveConfig(Adapter->ChipFlags, 0,
                                  NDIS_PACKET_TYPE_DIRECTED,
                                  NDIS_PACKET_TYPE_MULTICAST,
                                  NDIS_PACKET_TYPE_ALL_MULTICAST,
                                  NDIS_PACKET_TYPE_BROADCAST,
                                  NDIS_PACKET_TYPE_PROMISCUOUS));
    Rt25WriteMacAddress(Adapter);

    Rt25Write8(Adapter, RT25_REG_9346CR, RT25_9346CR_UNLOCK);
    Command = Rt25Read16(Adapter, RT25_REG_CPCR);
    Command &= ~(RT25_CPCR_RXCHKSUM | RT25_CPCR_RXVLAN);
    if (Adapter->Offload & RT25_OFFLOAD_RX_ANY)
        Command |= RT25_CPCR_RXCHKSUM;
    Rt25Write16(Adapter, RT25_REG_CPCR, Command);
    Rt25Write16(Adapter, RT25_REG_RMS, (USHORT)Adapter->ReceiveSize);
    Rt25StartMac(Adapter);

    Rt25Write32(Adapter, RT25_REG_TNPDS + 4, Adapter->TxRingPhysical.HighPart);
    Rt25Write32(Adapter, RT25_REG_TNPDS, Adapter->TxRingPhysical.LowPart);
    Rt25Write32(Adapter, RT25_REG_RDSAR + 4, Adapter->RxRingPhysical.HighPart);
    Rt25Write32(Adapter, RT25_REG_RDSAR, Adapter->RxRingPhysical.LowPart);

    Rt25Write8(Adapter, RT25_REG_CR, RT25_CR_RE | RT25_CR_TE);
    Rt25Write32(Adapter, RT25_REG_TCR, RT25_TCR_DEFAULT | RT25_TCR_AUTO_FIFO);
    Rt25Write8(Adapter, RT25_REG_9346CR, RT25_9346CR_LOCK);

    NdisAcquireSpinLock(&Adapter->StateLock);
    Adapter->HardwareRunning = TRUE;
    NdisReleaseSpinLock(&Adapter->StateLock);
    Rt25ApplyReceiveFilter(Adapter);

    /* Counts left from firmware or a wake period are not ours. */
    if (Adapter->TallyVirtual)
    {
        NdisAcquireSpinLock(&Adapter->TallyLock);
        Rt25TallyCommand(Adapter, RT25_COUNTER_RESET);
        NdisReleaseSpinLock(&Adapter->TallyLock);
    }

    Rt25Write8(Adapter, RT25_REG_PMCH, Rt25Read8(Adapter, RT25_REG_PMCH) | RT25_PMCH_PHY_POWER);
    Rt25StartPhy(Adapter);
    Rt25Write32(Adapter, RT25_REG_ISR, 0xFFFFFFFF);
    return NDIS_STATUS_SUCCESS;
}

VOID
Rt25StopHardware(
    _In_ PRT25_ADAPTER Adapter)
{
    ULONG i;

    Rt25UpdateTally(Adapter);
    Rt25QuiesceHardware(Adapter);
    if (!Adapter->RegisterBase || Adapter->SurpriseRemoved)
        return;

    Rt25Write32(Adapter, RT25_REG_RCR, Rt25Read32(Adapter, RT25_REG_RCR) & ~RT25_RCR_ACCEPT_MASK);
    Rt25Write32(Adapter, RT25_REG_MISC, Rt25Read32(Adapter, RT25_REG_MISC) | RT25_MISC_RECEIVE_GATE);
    for (i = 0; i < 100 && !(Rt25Read32(Adapter, RT25_REG_TCR) & RT25_TCR_EMPTY); i++)
        Rt25Stall(100);

    Rt25Write8(Adapter, RT25_REG_CR, 0);
    Rt25Write8(Adapter, RT25_REG_CR, RT25_CR_RST);
    for (i = 0; i < 1000 && (Rt25Read8(Adapter, RT25_REG_CR) & RT25_CR_RST); i++)
        Rt25Stall(100);

    Rt25Write32(Adapter, RT25_REG_ISR, 0xFFFFFFFF);
}

VOID
Rt25ApplyChecksumConfig(
    _In_ PRT25_ADAPTER Adapter)
{
    USHORT Command;

    if (!Adapter->HardwareRunning)
        return;

    Rt25Write8(Adapter, RT25_REG_9346CR, RT25_9346CR_UNLOCK);
    Command = Rt25Read16(Adapter, RT25_REG_CPCR) & ~RT25_CPCR_RXCHKSUM;
    if (Adapter->Offload & RT25_OFFLOAD_RX_ANY)
        Command |= RT25_CPCR_RXCHKSUM;
    Rt25Write16(Adapter, RT25_REG_CPCR, Command);
    Rt25Write8(Adapter, RT25_REG_9346CR, RT25_9346CR_LOCK);
}

/* FIXME: the wake register use follows observed r8169 behaviour and has not been tried on RTL8125/RTL8126 hardware */
VOID
Rt25ArmWake(
    _In_ PRT25_ADAPTER Adapter,
    _In_ BOOLEAN Magic,
    _In_ BOOLEAN LinkChange)
{
    UCHAR Value;

    Rt25ModifyMacOcp(Adapter, RT25_OCP_WOL_MAGIC, RT25_OCP_WOL_MAGIC_ENABLE,
                     Magic ? RT25_OCP_WOL_MAGIC_ENABLE : 0);

    Rt25Write8(Adapter, RT25_REG_9346CR, RT25_9346CR_UNLOCK);
    Value = Rt25Read8(Adapter, RT25_REG_CONFIG3) & ~(RT25_CONFIG3_MAGIC | RT25_CONFIG3_LINK_UP);
    if (LinkChange)
        Value |= RT25_CONFIG3_LINK_UP;
    Rt25Write8(Adapter, RT25_REG_CONFIG3, Value);

    Value = Rt25Read8(Adapter, RT25_REG_CONFIG5) & ~(RT25_CONFIG5_WAKE_FRAMES | RT25_CONFIG5_LAN_WAKE);
    if (Magic || LinkChange)
        Value |= RT25_CONFIG5_LAN_WAKE;
    Rt25Write8(Adapter, RT25_REG_CONFIG5, Value);

    Value = Rt25Read8(Adapter, RT25_REG_CONFIG2) & ~RT25_CONFIG2_PME_SIGNAL;
    if (Magic || LinkChange)
        Value |= RT25_CONFIG2_PME_SIGNAL;
    Rt25Write8(Adapter, RT25_REG_CONFIG2, Value);
    Rt25Write8(Adapter, RT25_REG_9346CR, RT25_9346CR_LOCK);

    /* The wake detector sits behind the receive filter, so it must still accept our frames. */
    if (Magic)
    {
        Rt25Write32(Adapter, RT25_REG_RCR,
                    Rt25Read32(Adapter, RT25_REG_RCR) | RT25_RCR_APM | RT25_RCR_AM | RT25_RCR_AB);
        Rt25Write32(Adapter, RT25_REG_MISC, Rt25Read32(Adapter, RT25_REG_MISC) & ~RT25_MISC_RECEIVE_GATE);
        Rt25Write8(Adapter, RT25_REG_CR, Rt25Read8(Adapter, RT25_REG_CR) | RT25_CR_RE);
    }
}

VOID
Rt25DisarmWake(
    _In_ PRT25_ADAPTER Adapter)
{
    Rt25ArmWake(Adapter, FALSE, FALSE);
}

BOOLEAN
Rt25RefreshLink(
    _In_ PRT25_ADAPTER Adapter)
{
    NDIS_MEDIA_CONNECT_STATE Connect = MediaConnectStateDisconnected;
    NDIS_MEDIA_DUPLEX_STATE Duplex = MediaDuplexStateUnknown;
    ULONG64 Speed = NDIS_LINK_SPEED_UNKNOWN;
    NDIS_SUPPORTED_PAUSE_FUNCTIONS Pause = NdisPauseFunctionsUnknown;
    ULONG SpeedMbps;
    USHORT Status, PhyStatus;
    BOOLEAN Changed;

    if (!Adapter->HardwareRunning)
        return FALSE;

    NdisAcquireSpinLock(&Adapter->PhyLock);

    Status = Rt25Read16(Adapter, RT25_REG_PHYSTATUS);
    if (Status != 0xFFFF && (Status & RT25_PHYSTATUS_LINK))
    {
        Connect = MediaConnectStateConnected;
        Duplex = (Status & RT25_PHYSTATUS_FULL_DUPLEX) ? MediaDuplexStateFull : MediaDuplexStateHalf;

        /* The MAC status register has no 5 Gbps bit; the PHY reports the resolved speed. */
        SpeedMbps = 0;
        if (Rt25ReadPhy(Adapter, RT25_PHY_PHYSR, &PhyStatus))
            SpeedMbps = Rt25PhySpeedMbps(PhyStatus);
        if (SpeedMbps == 0)
            SpeedMbps = Rt25MacSpeedMbps(Status);
        Speed = (ULONG64)SpeedMbps * 1000000;
        Pause = (NDIS_SUPPORTED_PAUSE_FUNCTIONS)Rt25PauseFromStatus(Status,
                                                                    NdisPauseFunctionsSendOnly,
                                                                    NdisPauseFunctionsReceiveOnly,
                                                                    NdisPauseFunctionsSendAndReceive,
                                                                    NdisPauseFunctionsUnsupported);
    }

    Changed = Connect != Adapter->MediaConnectState ||
              Duplex != Adapter->MediaDuplexState ||
              Speed != Adapter->LinkSpeed ||
              Pause != Adapter->PauseFunctions;
    Adapter->MediaConnectState = Connect;
    Adapter->MediaDuplexState = Duplex;
    Adapter->LinkSpeed = Speed;
    Adapter->PauseFunctions = Pause;
    if (Changed)
        Adapter->LinkSequence++;

    NdisReleaseSpinLock(&Adapter->PhyLock);

    if (Changed)
    {
        DPRINT1("RTL8125: link %s, %I64u Mbps\n",
                Connect == MediaConnectStateConnected ? "up" : "down",
                Speed == NDIS_LINK_SPEED_UNKNOWN ? 0 : Speed / 1000000);
    }

    return Changed;
}

VOID
Rt25FillLinkState(
    _In_ PRT25_ADAPTER Adapter,
    _Out_ PNDIS_LINK_STATE LinkState)
{
    NdisZeroMemory(LinkState, sizeof(*LinkState));
    LinkState->Header.Type = NDIS_OBJECT_TYPE_DEFAULT;
    LinkState->Header.Revision = NDIS_LINK_STATE_REVISION_1;
    LinkState->Header.Size = NDIS_SIZEOF_LINK_STATE_REVISION_1;
    LinkState->MediaConnectState = Adapter->MediaConnectState;
    LinkState->MediaDuplexState = Adapter->MediaDuplexState;
    LinkState->XmitLinkSpeed = Adapter->LinkSpeed;
    LinkState->RcvLinkSpeed = Adapter->LinkSpeed;
    LinkState->PauseFunctions = Adapter->PauseFunctions;
    LinkState->AutoNegotiationFlags = NDIS_LINK_STATE_XMIT_LINK_SPEED_AUTO_NEGOTIATED |
                                      NDIS_LINK_STATE_RCV_LINK_SPEED_AUTO_NEGOTIATED |
                                      NDIS_LINK_STATE_DUPLEX_AUTO_NEGOTIATED;
}

VOID
Rt25UpdateLinkState(
    _In_ PRT25_ADAPTER Adapter)
{
    NDIS_STATUS_INDICATION Indication;
    NDIS_LINK_STATE LinkState;
    ULONG Sequence;
    BOOLEAN Current;

    if (!Rt25RefreshLink(Adapter))
        return;

    /* The DPC and CheckForHang can both indicate; whoever sees a newer state indicates again. */
    do
    {
        NdisAcquireSpinLock(&Adapter->PhyLock);
        Rt25FillLinkState(Adapter, &LinkState);
        Sequence = Adapter->LinkSequence;
        NdisReleaseSpinLock(&Adapter->PhyLock);

        NdisZeroMemory(&Indication, sizeof(Indication));
        Indication.Header.Type = NDIS_OBJECT_TYPE_STATUS_INDICATION;
        Indication.Header.Revision = NDIS_STATUS_INDICATION_REVISION_1;
        Indication.Header.Size = NDIS_SIZEOF_STATUS_INDICATION_REVISION_1;
        Indication.SourceHandle = Adapter->MiniportHandle;
        Indication.StatusCode = NDIS_STATUS_LINK_STATE;
        Indication.StatusBuffer = &LinkState;
        Indication.StatusBufferSize = sizeof(LinkState);
        NdisMIndicateStatusEx(Adapter->MiniportHandle, &Indication);

        NdisAcquireSpinLock(&Adapter->PhyLock);
        Current = Sequence == Adapter->LinkSequence;
        NdisReleaseSpinLock(&Adapter->PhyLock);
    } while (!Current);
}
