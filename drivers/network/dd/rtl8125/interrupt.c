/*
 * PROJECT:     LiberNT Realtek RTL8125/RTL8126 Ethernet Driver
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Interrupt service routine and DPC (one MSI message, or a line)
 * COPYRIGHT:   Copyright 2026 The LiberNT contributors
 */

#include "rtl8125.h"

static
BOOLEAN
Rt25Isr(
    _In_ PRT25_ADAPTER Adapter,
    _Out_ PBOOLEAN QueueDpc,
    _Out_ PULONG TargetProcessors)
{
    ULONG Status, Mask;

    *QueueDpc = FALSE;
    *TargetProcessors = 0;

    /* Bits latched while the DPC runs with IMR clear are not a new interrupt from this chip. */
    Mask = Rt25Read32(Adapter, RT25_REG_IMR);
    if (Mask == 0 || Mask == 0xFFFFFFFF)
        return FALSE;

    Status = Rt25Read32(Adapter, RT25_REG_ISR);
    if (Status == 0xFFFFFFFF || !(Status & Mask))
        return FALSE;

    Rt25DisableInterrupts(Adapter);
    Rt25Write32(Adapter, RT25_REG_ISR, Status);
    InterlockedOr(&Adapter->InterruptPending, (LONG)Status);
    *QueueDpc = TRUE;
    return TRUE;
}

static
VOID
Rt25Dpc(
    _In_ PRT25_ADAPTER Adapter,
    _In_opt_ PVOID ReceiveThrottleParameters)
{
    PNDIS_RECEIVE_THROTTLE_PARAMETERS Throttle = ReceiveThrottleParameters;
    ULONG Pending, Budget = RT25_RING_SIZE;
    BOOLEAN Throttled = FALSE;

    Pending = (ULONG)InterlockedExchange(&Adapter->InterruptPending, 0);

    if (Throttle)
    {
        Throttle->MoreNblsPending = FALSE;
        if (Throttle->MaxNblsToIndicate != NDIS_INDICATE_ALL_NBLS)
        {
            Throttled = TRUE;
            Budget = max(1, min(Budget, Throttle->MaxNblsToIndicate));
        }
    }

    if (Pending & RT25_INT_LINK_CHANGE)
        Rt25UpdateLinkState(Adapter);

    if (Pending & RT25_INT_TX_EVENTS)
        Rt25ReclaimTransmit(Adapter);

    if (Pending & RT25_INT_TX_ERR)
        Adapter->TxErrors++;

    Rt25PollReceive(Adapter, Budget, TRUE);

    if (Throttled && Rt25ReceivePending(Adapter))
    {
        /* NDIS calls the DPC again with interrupts still masked. */
        Throttle->MoreNblsPending = TRUE;
        return;
    }

    Rt25EnableInterrupts(Adapter);
}

static
BOOLEAN
NTAPI
Rt25Interrupt(
    _In_ NDIS_HANDLE MiniportInterruptContext,
    _Out_ PBOOLEAN QueueDefaultInterruptDpc,
    _Out_ PULONG TargetProcessors)
{
    return Rt25Isr(MiniportInterruptContext, QueueDefaultInterruptDpc, TargetProcessors);
}

static
VOID
NTAPI
Rt25InterruptDpc(
    _In_ NDIS_HANDLE MiniportInterruptContext,
    _In_ PVOID MiniportDpcContext,
    _In_ PVOID ReceiveThrottleParameters,
    _In_ PVOID NdisReserved2)
{
    UNREFERENCED_PARAMETER(MiniportDpcContext);
    UNREFERENCED_PARAMETER(NdisReserved2);

    Rt25Dpc(MiniportInterruptContext, ReceiveThrottleParameters);
}

static
VOID
NTAPI
Rt25DisableInterrupt(
    _In_ NDIS_HANDLE MiniportInterruptContext)
{
    Rt25DisableInterrupts(MiniportInterruptContext);
}

static
VOID
NTAPI
Rt25EnableInterrupt(
    _In_ NDIS_HANDLE MiniportInterruptContext)
{
    Rt25EnableInterrupts(MiniportInterruptContext);
}

static
BOOLEAN
NTAPI
Rt25MessageInterrupt(
    _In_ NDIS_HANDLE MiniportInterruptContext,
    _In_ ULONG MessageId,
    _Out_ PBOOLEAN QueueDefaultInterruptDpc,
    _Out_ PULONG TargetProcessors)
{
    UNREFERENCED_PARAMETER(MessageId);

    return Rt25Isr(MiniportInterruptContext, QueueDefaultInterruptDpc, TargetProcessors);
}

static
VOID
NTAPI
Rt25MessageInterruptDpc(
    _In_ NDIS_HANDLE MiniportInterruptContext,
    _In_ ULONG MessageId,
    _In_ PVOID MiniportDpcContext,
    _In_ PVOID ReceiveThrottleParameters,
    _In_ PVOID NdisReserved2)
{
    UNREFERENCED_PARAMETER(MessageId);
    UNREFERENCED_PARAMETER(MiniportDpcContext);
    UNREFERENCED_PARAMETER(NdisReserved2);

    Rt25Dpc(MiniportInterruptContext, ReceiveThrottleParameters);
}

static
VOID
NTAPI
Rt25DisableMessageInterrupt(
    _In_ NDIS_HANDLE MiniportInterruptContext,
    _In_ ULONG MessageId)
{
    UNREFERENCED_PARAMETER(MessageId);

    Rt25DisableInterrupts(MiniportInterruptContext);
}

static
VOID
NTAPI
Rt25EnableMessageInterrupt(
    _In_ NDIS_HANDLE MiniportInterruptContext,
    _In_ ULONG MessageId)
{
    UNREFERENCED_PARAMETER(MessageId);

    Rt25EnableInterrupts(MiniportInterruptContext);
}

NDIS_STATUS
Rt25RegisterInterrupt(
    _In_ PRT25_ADAPTER Adapter)
{
    NDIS_MINIPORT_INTERRUPT_CHARACTERISTICS Characteristics;
    NDIS_STATUS Status;

    NdisZeroMemory(&Characteristics, sizeof(Characteristics));
    Characteristics.Header.Type = NDIS_OBJECT_TYPE_MINIPORT_INTERRUPT;
    Characteristics.Header.Revision = NDIS_MINIPORT_INTERRUPT_REVISION_1;
    Characteristics.Header.Size = NDIS_SIZEOF_MINIPORT_INTERRUPT_CHARACTERISTICS_REVISION_1;
    Characteristics.InterruptHandler = Rt25Interrupt;
    Characteristics.InterruptDpcHandler = Rt25InterruptDpc;
    Characteristics.DisableInterruptHandler = Rt25DisableInterrupt;
    Characteristics.EnableInterruptHandler = Rt25EnableInterrupt;
    Characteristics.MsiSupported = TRUE;
    Characteristics.MsiSyncWithAllMessages = TRUE;
    Characteristics.MessageInterruptHandler = Rt25MessageInterrupt;
    Characteristics.MessageInterruptDpcHandler = Rt25MessageInterruptDpc;
    Characteristics.DisableMessageInterruptHandler = Rt25DisableMessageInterrupt;
    Characteristics.EnableMessageInterruptHandler = Rt25EnableMessageInterrupt;

    Status = NdisMRegisterInterruptEx(Adapter->MiniportHandle, Adapter, &Characteristics,
                                      &Adapter->InterruptHandle);
    if (Status != NDIS_STATUS_SUCCESS)
    {
        DPRINT1("RTL8125: MSI registration failed 0x%08lx, trying the line interrupt\n", Status);
        Characteristics.MsiSupported = FALSE;
        Characteristics.MsiSyncWithAllMessages = FALSE;
        Characteristics.MessageInterruptHandler = NULL;
        Characteristics.MessageInterruptDpcHandler = NULL;
        Characteristics.DisableMessageInterruptHandler = NULL;
        Characteristics.EnableMessageInterruptHandler = NULL;
        Status = NdisMRegisterInterruptEx(Adapter->MiniportHandle, Adapter, &Characteristics,
                                          &Adapter->InterruptHandle);
    }

    if (Status != NDIS_STATUS_SUCCESS)
    {
        DPRINT1("RTL8125: interrupt registration failed 0x%08lx\n", Status);
        Adapter->InterruptHandle = NULL;
        return Status;
    }

    Adapter->MessageInterrupt = Characteristics.InterruptType == NDIS_CONNECT_MESSAGE_BASED;
    DPRINT1("RTL8125: %s interrupt\n", Adapter->MessageInterrupt ? "MSI" : "line");
    return NDIS_STATUS_SUCCESS;
}

VOID
Rt25DeregisterInterrupt(
    _In_ PRT25_ADAPTER Adapter)
{
    if (Adapter->InterruptHandle)
    {
        Rt25DisableInterrupts(Adapter);
        NdisMDeregisterInterruptEx(Adapter->InterruptHandle);
        Adapter->InterruptHandle = NULL;
    }
}
