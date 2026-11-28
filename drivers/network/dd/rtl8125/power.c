/*
 * PROJECT:     LiberNT Realtek RTL8125/RTL8126 Ethernet Driver
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Device power states and wake-on-LAN
 * COPYRIGHT:   Copyright 2026 The LiberNT contributors
 */

#include "rtl8125.h"

VOID
Rt25FillPmCapabilities(
    _Out_ PNDIS_PM_CAPABILITIES Capabilities)
{
    NdisZeroMemory(Capabilities, sizeof(*Capabilities));
    Capabilities->Header.Type = NDIS_OBJECT_TYPE_DEFAULT;
    Capabilities->Header.Revision = NDIS_PM_CAPABILITIES_REVISION_1;
    Capabilities->Header.Size = NDIS_SIZEOF_NDIS_PM_CAPABILITIES_REVISION_1;
    Capabilities->SupportedWoLPacketPatterns = NDIS_PM_WOL_MAGIC_PACKET_SUPPORTED;
    Capabilities->NumTotalWoLPatterns = 1;
    Capabilities->MinMagicPacketWakeUp = NdisDeviceStateD3;
    Capabilities->MinPatternWakeUp = NdisDeviceStateUnspecified;
    Capabilities->MinLinkChangeWakeUp = NdisDeviceStateD3;
}

static
NDIS_STATUS
Rt25PowerUp(
    _In_ PRT25_ADAPTER Adapter)
{
    NDIS_STATUS Status;

    Status = Rt25PrepareBus(Adapter);
    if (Status != NDIS_STATUS_SUCCESS)
        return Status;

    /* A wake-armed controller still receives into the ring, so stop it before the ring is rewritten. */
    Rt25DisarmWake(Adapter);
    Rt25StopHardware(Adapter);
    Rt25ResetRings(Adapter);
    Status = Rt25StartHardware(Adapter);
    if (Status != NDIS_STATUS_SUCCESS)
        return Status;

    Rt25UpdateLinkState(Adapter);
    Rt25ResumeInterrupts(Adapter);
    return NDIS_STATUS_SUCCESS;
}

static
VOID
Rt25PowerDown(
    _In_ PRT25_ADAPTER Adapter)
{
    BOOLEAN Magic, LinkChange;

    /* An added magic-packet pattern only takes effect once OID_PM_PARAMETERS enables that type. */
    Magic = (Adapter->EnabledWakePatterns & NDIS_PM_WOL_MAGIC_PACKET_ENABLED) != 0;
    LinkChange = (Adapter->WakeUpFlags & NDIS_PM_WAKE_ON_LINK_CHANGE_ENABLED) != 0;

    Rt25UpdateTally(Adapter);
    Rt25QuiesceHardware(Adapter);
    Rt25ReclaimTransmit(Adapter);
    if (Magic || LinkChange)
    {
        Rt25ArmWake(Adapter, Magic, LinkChange);
        Rt25Write32(Adapter, RT25_REG_ISR, 0xFFFFFFFF);
    }
    else
    {
        Rt25StopHardware(Adapter);
    }

    NdisAcquireSpinLock(&Adapter->PhyLock);
    Adapter->MediaConnectState = MediaConnectStateUnknown;
    Adapter->MediaDuplexState = MediaDuplexStateUnknown;
    Adapter->LinkSpeed = NDIS_LINK_SPEED_UNKNOWN;
    Adapter->PauseFunctions = NdisPauseFunctionsUnknown;
    NdisReleaseSpinLock(&Adapter->PhyLock);
}

/* NDIS pauses the adapter before any change of device power state, so no receive NBL is out. */
NDIS_STATUS
Rt25SetPower(
    _In_ PRT25_ADAPTER Adapter,
    _In_ NDIS_DEVICE_POWER_STATE PowerState)
{
    NDIS_STATUS Status = NDIS_STATUS_SUCCESS;

    if (PowerState < NdisDeviceStateD0 || PowerState > NdisDeviceStateD3)
        return NDIS_STATUS_INVALID_DATA;

    if (PowerState == Adapter->PowerState)
        return NDIS_STATUS_SUCCESS;

    if (Adapter->SurpriseRemoved)
    {
        Adapter->PowerState = PowerState;
        return NDIS_STATUS_SUCCESS;
    }

    if (PowerState == NdisDeviceStateD0)
    {
        Status = Rt25PowerUp(Adapter);
        if (Status != NDIS_STATUS_SUCCESS)
            DPRINT1("RTL8125: resume to D0 failed 0x%08lx\n", Status);
    }
    else if (Adapter->PowerState == NdisDeviceStateD0)
    {
        Rt25PowerDown(Adapter);
    }

    Adapter->PowerState = PowerState;
    return Status;
}
