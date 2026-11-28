/*
 * PROJECT:     LiberNT Intel BE200 Native 802.11 Miniport
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     PCIe transport: registers, DMA rings, host commands and interrupts
 * COPYRIGHT:   Copyright 2026 The LiberNT contributors
 */

#include "iwlbe.h"

#define NDEBUG
#include <debug.h>

#define IWL_WORKER_POLL_MS              10

BOOLEAN
IwlAllocDma(
    _In_ PIWL_ADAPTER Adapter,
    _Out_ PIWL_DMA Dma,
    _In_ ULONG Length,
    _In_ BOOLEAN Cached)
{
    RtlZeroMemory(Dma, sizeof(*Dma));
    NdisMAllocateSharedMemory(Adapter->MiniportAdapterHandle, Length, Cached, &Dma->Va, &Dma->Pa);
    if (!Dma->Va)
        return FALSE;

    Dma->Length = Length;
    Dma->Cached = Cached;
    RtlZeroMemory(Dma->Va, Length);
    return TRUE;
}

VOID
IwlFreeDma(
    _In_ PIWL_ADAPTER Adapter,
    _Inout_ PIWL_DMA Dma)
{
    if (Dma->Va)
        NdisMFreeSharedMemory(Adapter->MiniportAdapterHandle, Dma->Length, Dma->Cached, Dma->Va, Dma->Pa);
    RtlZeroMemory(Dma, sizeof(*Dma));
}

static
NDIS_STATUS
IwlRegisterDma(
    _In_ PIWL_ADAPTER Adapter)
{
    NDIS_SG_DMA_DESCRIPTION Description;

    NdisZeroMemory(&Description, sizeof(Description));
    Description.Header.Type = NDIS_OBJECT_TYPE_SG_DMA_DESCRIPTION;
    Description.Header.Revision = NDIS_SG_DMA_DESCRIPTION_REVISION_1;
    Description.Header.Size = NDIS_SIZEOF_SG_DMA_DESCRIPTION_REVISION_1;
    Description.Flags = NDIS_SG_DMA_64_BIT_ADDRESS;
    Description.MaximumPhysicalMapping = 64 * 1024;

    return NdisMRegisterScatterGatherDma(Adapter->MiniportAdapterHandle, &Description, &Adapter->DmaHandle);
}

NDIS_STATUS
IwlMapResources(
    _In_ PIWL_ADAPTER Adapter,
    _In_opt_ PNDIS_RESOURCE_LIST Resources)
{
    PCM_PARTIAL_RESOURCE_DESCRIPTOR Descriptor;
    USHORT VendorId = 0, Command = 0;
    NDIS_STATUS Status;
    ULONG i;

    if (!Resources)
        return NDIS_STATUS_RESOURCES;

    for (i = 0; i < Resources->Count; i++)
    {
        Descriptor = &Resources->PartialDescriptors[i];
        if ((Descriptor->Type == CmResourceTypeMemory || Descriptor->Type == CmResourceTypeMemoryLarge) &&
            !Adapter->RegisterLength)
        {
            Adapter->RegisterPhysical = Descriptor->u.Memory.Start;
            Adapter->RegisterLength = min(Descriptor->u.Memory.Length, 0x4000);
            if (Descriptor->Type == CmResourceTypeMemoryLarge && !Adapter->RegisterLength)
                Adapter->RegisterLength = 0x4000;
        }
        else if (Descriptor->Type == CmResourceTypeInterrupt)
        {
            Adapter->HasInterrupt = TRUE;
        }
    }

    if (Adapter->RegisterLength < 0x1000)
    {
        DPRINT1("IWLBE: no register window (length %lu)\n", Adapter->RegisterLength);
        return NDIS_STATUS_RESOURCES;
    }

    if (NdisMGetBusData(Adapter->MiniportAdapterHandle, PCI_WHICHSPACE_CONFIG,
                        FIELD_OFFSET(PCI_COMMON_CONFIG, VendorID), &VendorId, sizeof(VendorId)) != sizeof(VendorId) ||
        NdisMGetBusData(Adapter->MiniportAdapterHandle, PCI_WHICHSPACE_CONFIG,
                        FIELD_OFFSET(PCI_COMMON_CONFIG, Command), &Command, sizeof(Command)) != sizeof(Command) ||
        VendorId != 0x8086)
    {
        DPRINT1("IWLBE: unexpected PCI function (vendor %04x)\n", VendorId);
        return NDIS_STATUS_ADAPTER_NOT_FOUND;
    }

    if ((Command & (PCI_ENABLE_MEMORY_SPACE | PCI_ENABLE_BUS_MASTER)) != (PCI_ENABLE_MEMORY_SPACE | PCI_ENABLE_BUS_MASTER))
    {
        Command |= PCI_ENABLE_MEMORY_SPACE | PCI_ENABLE_BUS_MASTER;
        NdisMSetBusData(Adapter->MiniportAdapterHandle, PCI_WHICHSPACE_CONFIG,
                        FIELD_OFFSET(PCI_COMMON_CONFIG, Command), &Command, sizeof(Command));
    }

    Status = NdisMMapIoSpace((PVOID *)&Adapter->Registers, Adapter->MiniportAdapterHandle,
                             Adapter->RegisterPhysical, Adapter->RegisterLength);
    if (Status != NDIS_STATUS_SUCCESS)
    {
        DPRINT1("IWLBE: NdisMMapIoSpace failed 0x%08lx\n", Status);
        Adapter->Registers = NULL;
        return Status;
    }

    Status = IwlRegisterDma(Adapter);
    if (Status != NDIS_STATUS_SUCCESS)
    {
        DPRINT1("IWLBE: DMA registration failed 0x%08lx\n", Status);
        return Status;
    }

    Adapter->HwRev = IwlRead32(Adapter, CSR_HW_REV);
    Adapter->HwRfId = IwlRead32(Adapter, CSR_HW_RF_ID);
    DPRINT1("IWLBE: hw_rev 0x%08lx (mac type 0x%03lx step %lu) rf_id 0x%08lx (type 0x%03lx)\n",
            Adapter->HwRev, CSR_HW_REV_TYPE(Adapter->HwRev), CSR_HW_REV_STEP(Adapter->HwRev),
            Adapter->HwRfId, CSR_HW_RFID_TYPE(Adapter->HwRfId));
    if (Adapter->HwRev == 0xFFFFFFFF)
        return NDIS_STATUS_ADAPTER_NOT_FOUND;

    return NDIS_STATUS_SUCCESS;
}

VOID
IwlUnmapResources(
    _In_ PIWL_ADAPTER Adapter)
{
    if (Adapter->DmaHandle)
    {
        NdisMDeregisterScatterGatherDma(Adapter->DmaHandle);
        Adapter->DmaHandle = NULL;
    }
    if (Adapter->Registers)
    {
        NdisMUnmapIoSpace(Adapter->MiniportAdapterHandle, Adapter->Registers, Adapter->RegisterLength);
        Adapter->Registers = NULL;
    }
}

static
BOOLEAN
IwlPoll(
    _In_ PIWL_ADAPTER Adapter,
    _In_ ULONG Offset,
    _In_ ULONG Mask,
    _In_ ULONG Value,
    _In_ ULONG TimeoutUs)
{
    ULONG Waited;

    for (Waited = 0; Waited <= TimeoutUs; Waited += 10)
    {
        if ((IwlRead32(Adapter, Offset) & Mask) == Value)
            return TRUE;
        KeStallExecutionProcessor(10);
    }
    return FALSE;
}

static
BOOLEAN
IwlGrabNicAccess(
    _In_ PIWL_ADAPTER Adapter)
{
    IwlSetBits(Adapter, CSR_GP_CNTRL, CSR_GP_CNTRL_BZ_MAC_ACCESS_REQ);
    KeStallExecutionProcessor(2);
    if (IwlPoll(Adapter, CSR_GP_CNTRL, CSR_GP_CNTRL_MAC_STATUS, CSR_GP_CNTRL_MAC_STATUS, 15000))
        return TRUE;

    IwlClearBits(Adapter, CSR_GP_CNTRL, CSR_GP_CNTRL_BZ_MAC_ACCESS_REQ);
    DPRINT1("IWLBE: NIC access timed out, GP_CNTRL 0x%08lx\n", IwlRead32(Adapter, CSR_GP_CNTRL));
    return FALSE;
}

static
VOID
IwlReleaseNicAccess(
    _In_ PIWL_ADAPTER Adapter)
{
    IwlClearBits(Adapter, CSR_GP_CNTRL, CSR_GP_CNTRL_BZ_MAC_ACCESS_REQ);
}

ULONG
IwlReadPrph(
    _In_ PIWL_ADAPTER Adapter,
    _In_ ULONG Address)
{
    ULONG Value = 0xA5A5A5A5;

    if (IwlGrabNicAccess(Adapter))
    {
        IwlWrite32(Adapter, HBUS_TARG_PRPH_RADDR, (Address & 0xFFFFFF) | (3UL << 24));
        Value = IwlRead32(Adapter, HBUS_TARG_PRPH_RDAT);
        IwlReleaseNicAccess(Adapter);
    }
    return Value;
}

VOID
IwlWritePrph(
    _In_ PIWL_ADAPTER Adapter,
    _In_ ULONG Address,
    _In_ ULONG Value)
{
    if (IwlGrabNicAccess(Adapter))
    {
        IwlWrite32(Adapter, HBUS_TARG_PRPH_WADDR, (Address & 0xFFFFFF) | (3UL << 24));
        IwlWrite32(Adapter, HBUS_TARG_PRPH_WDAT, Value);
        IwlReleaseNicAccess(Adapter);
    }
}

static
VOID
IwlDisableInterrupts(
    _In_ PIWL_ADAPTER Adapter)
{
    IwlWrite32(Adapter, CSR_INT_MASK, 0);
    IwlWrite32(Adapter, CSR_INT, 0xFFFFFFFF);
    IwlWrite32(Adapter, CSR_FH_INT_STATUS, 0xFFFFFFFF);
}

static
VOID
IwlEnableInterrupts(
    _In_ PIWL_ADAPTER Adapter,
    _In_ ULONG Mask)
{
    Adapter->IntMask = Mask;
    IwlWrite32(Adapter, CSR_INT_MASK, Mask);
}

static
BOOLEAN
IwlSetHwReady(
    _In_ PIWL_ADAPTER Adapter)
{
    IwlSetBits(Adapter, CSR_HW_IF_CONFIG, CSR_HW_IF_CONFIG_NIC_READY);
    if (!IwlPoll(Adapter, CSR_HW_IF_CONFIG, CSR_HW_IF_CONFIG_NIC_READY, CSR_HW_IF_CONFIG_NIC_READY, 50))
        return FALSE;

    IwlSetBits(Adapter, CSR_MBOX_SET, CSR_MBOX_SET_OS_ALIVE);
    return TRUE;
}

static
NTSTATUS
IwlPrepareCardHw(
    _In_ PIWL_ADAPTER Adapter)
{
    ULONG Attempt, Waited;

    if (IwlSetHwReady(Adapter))
        return STATUS_SUCCESS;

    IwlSetBits(Adapter, CSR_DBG_LINK_PWR_MGMT, CSR_DBG_LINK_PWR_MGMT_DISCON);
    IwlSleep(1);

    for (Attempt = 0; Attempt < 10; Attempt++)
    {
        IwlSetBits(Adapter, CSR_HW_IF_CONFIG, CSR_HW_IF_CONFIG_PREPARE);
        for (Waited = 0; Waited < 150; Waited += 1)
        {
            if (IwlSetHwReady(Adapter))
                return STATUS_SUCCESS;
            IwlSleep(1);
        }
        IwlSleep(25);
    }

    DPRINT1("IWLBE: the card did not become ready, HW_IF_CONFIG 0x%08lx\n", IwlRead32(Adapter, CSR_HW_IF_CONFIG));
    return STATUS_IO_DEVICE_ERROR;
}

static
VOID
IwlSoftwareReset(
    _In_ PIWL_ADAPTER Adapter)
{
    IwlSetBits(Adapter, CSR_GP_CNTRL, CSR_GP_CNTRL_BZ_SW_RESET);
    IwlSleep(15);
}

static
NTSTATUS
IwlApmInit(
    _In_ PIWL_ADAPTER Adapter)
{
    IwlSetBits(Adapter, CSR_GIO_CHICKEN_BITS, CSR_GIO_CHICKEN_DIS_L0S_TIMER);
    IwlSetBits(Adapter, CSR_DBG_HPET_MEM, CSR_DBG_HPET_MEM_VALUE);
    IwlSetBits(Adapter, CSR_HW_IF_CONFIG, CSR_HW_IF_CONFIG_HAP_WAKE);
    IwlSetBits(Adapter, CSR_GIO, CSR_GIO_L0S_DISABLED);

    IwlSetBits(Adapter, CSR_GP_CNTRL, CSR_GP_CNTRL_BZ_MAC_ACCESS_REQ | CSR_GP_CNTRL_BZ_MAC_INIT);
    if (!IwlPoll(Adapter, CSR_GP_CNTRL, CSR_GP_CNTRL_MAC_STATUS, CSR_GP_CNTRL_MAC_STATUS, 25000))
    {
        DPRINT1("IWLBE: MAC clock did not start, GP_CNTRL 0x%08lx\n", IwlRead32(Adapter, CSR_GP_CNTRL));
        return STATUS_IO_TIMEOUT;
    }

    IwlWritePrph(Adapter, UREG_CHICK + IWL_UMAC_PRPH_OFFSET, UREG_CHICK_MSI_ENABLE);
    Adapter->DeviceEnabled = TRUE;
    return STATUS_SUCCESS;
}

NTSTATUS
IwlStartHardware(
    _In_ PIWL_ADAPTER Adapter)
{
    NTSTATUS Status;

    Status = IwlPrepareCardHw(Adapter);
    if (!NT_SUCCESS(Status))
        return Status;

    IwlSoftwareReset(Adapter);
    Status = IwlPrepareCardHw(Adapter);
    if (!NT_SUCCESS(Status))
        return Status;

    Status = IwlApmInit(Adapter);
    if (!NT_SUCCESS(Status))
        return Status;

    IwlDisableInterrupts(Adapter);
    IwlEnableInterrupts(Adapter, CSR_INT_BIT_RF_KILL);
    DPRINT1("IWLBE: hardware started, radio switch %s\n",
            (IwlRead32(Adapter, CSR_GP_CNTRL) & CSR_GP_CNTRL_RFKILL) ? "on" : "off");
    return STATUS_SUCCESS;
}

static
VOID
IwlFreeFirmwareDram(
    _In_ PIWL_ADAPTER Adapter)
{
    ULONG i;

    for (i = 0; i < Adapter->FwDramCount; i++)
        IwlFreeDma(Adapter, &Adapter->FwDram[i]);
    Adapter->FwDramCount = 0;
    IwlFreeDma(Adapter, &Adapter->ImlDma);
}

static
VOID
IwlFreePnvmDram(
    _In_ PIWL_ADAPTER Adapter)
{
    ULONG i;

    for (i = 0; i < Adapter->PnvmDramCount; i++)
        IwlFreeDma(Adapter, &Adapter->PnvmDram[i]);
    Adapter->PnvmDramCount = 0;
}

VOID
IwlStopDevice(
    _In_ PIWL_ADAPTER Adapter)
{
    if (!Adapter->Registers)
        return;

    Adapter->FirmwareAlive = FALSE;
    Adapter->DeviceEnabled = FALSE;
    IwlDisableInterrupts(Adapter);
    Adapter->IntMask = 0;
    IwlClearBits(Adapter, CSR_GP_CNTRL, CSR_GP_CNTRL_BZ_MAC_INIT);
    IwlSoftwareReset(Adapter);
    IwlFreeFirmwareDram(Adapter);
    IwlFreePnvmDram(Adapter);
    Adapter->CmdQueue.Ready = FALSE;
    Adapter->MgmtQueue.Ready = FALSE;
    Adapter->DataQueue.Ready = FALSE;
}

static
BOOLEAN
IwlAllocTxRing(
    _In_ PIWL_ADAPTER Adapter,
    _Out_ PIWL_TXQ Queue,
    _In_ USHORT Size,
    _In_ BOOLEAN WithBuffers)
{
    RtlZeroMemory(Queue, sizeof(*Queue));
    Queue->Size = Size;
    if (!IwlAllocDma(Adapter, &Queue->Tfds, (ULONG)Size * IWL_TFD_SIZE, FALSE))
        return FALSE;
    if (!WithBuffers)
        return TRUE;
    return IwlAllocDma(Adapter, &Queue->ByteCounts, IWL_BC_TABLE_SIZE, FALSE) &&
           IwlAllocDma(Adapter, &Queue->Buffers, IWL_TXQ_BUFFERS * IWL_TXQ_BUFFER_SIZE, FALSE);
}

static
VOID
IwlFreeTxRing(
    _In_ PIWL_ADAPTER Adapter,
    _Inout_ PIWL_TXQ Queue)
{
    IwlFreeDma(Adapter, &Queue->Tfds);
    IwlFreeDma(Adapter, &Queue->ByteCounts);
    IwlFreeDma(Adapter, &Queue->Buffers);
}

NTSTATUS
IwlAllocateRings(
    _In_ PIWL_ADAPTER Adapter)
{
    ULONG i, Chunk, Index;

    if (!IwlAllocDma(Adapter, &Adapter->RxFreeBd, IWL_RX_RING_SIZE * IWL_RX_FREE_BD_SIZE, FALSE) ||
        !IwlAllocDma(Adapter, &Adapter->RxUsedBd, IWL_RX_RING_SIZE * IWL_RX_USED_BD_SIZE, FALSE) ||
        !IwlAllocDma(Adapter, &Adapter->RxStatus, 64, FALSE))
        return STATUS_INSUFFICIENT_RESOURCES;

    for (Chunk = 0; Chunk < IWL_RB_CHUNKS; Chunk++)
    {
        if (!IwlAllocDma(Adapter, &Adapter->RxChunks[Chunk], IWL_RB_PER_CHUNK * IWL_RB_SIZE, TRUE))
            return STATUS_INSUFFICIENT_RESOURCES;
        for (i = 0; i < IWL_RB_PER_CHUNK; i++)
        {
            Index = Chunk * IWL_RB_PER_CHUNK + i;
            if (Index >= IWL_RX_POSTED)
                break;
            Adapter->RbVa[Index] = (PUCHAR)Adapter->RxChunks[Chunk].Va + i * IWL_RB_SIZE;
            Adapter->RbPa[Index] = IwlDmaAddress(&Adapter->RxChunks[Chunk]) + i * IWL_RB_SIZE;
        }
    }

    if (!IwlAllocTxRing(Adapter, &Adapter->CmdQueue, IWL_CMD_QUEUE_SIZE, FALSE) ||
        !IwlAllocDma(Adapter, &Adapter->CmdBuffers, IWL_CMD_BUFFERS * IWL_CMD_BUFFER_SIZE, FALSE) ||
        !IwlAllocTxRing(Adapter, &Adapter->MgmtQueue, IWL_TXQ_SIZE, TRUE) ||
        !IwlAllocTxRing(Adapter, &Adapter->DataQueue, IWL_TXQ_SIZE, TRUE))
        return STATUS_INSUFFICIENT_RESOURCES;

    if (!IwlAllocDma(Adapter, &Adapter->ContextInfo, PAGE_SIZE, FALSE) ||
        !IwlAllocDma(Adapter, &Adapter->PrphInfo, PAGE_SIZE, FALSE) ||
        !IwlAllocDma(Adapter, &Adapter->PrphScratch, PAGE_SIZE, FALSE) ||
        !IwlAllocDma(Adapter, &Adapter->PnvmDesc, IWL_PNVM_DESC_SIZE, FALSE))
        return STATUS_INSUFFICIENT_RESOURCES;

    C_ASSERT(IWL_PS_SIZE <= PAGE_SIZE);
    C_ASSERT(IWL_CI2_SIZE <= PAGE_SIZE);
    return STATUS_SUCCESS;
}

VOID
IwlFreeRings(
    _In_ PIWL_ADAPTER Adapter)
{
    ULONG i;

    IwlFreeFirmwareDram(Adapter);
    IwlFreePnvmDram(Adapter);
    IwlFreeDma(Adapter, &Adapter->RxFreeBd);
    IwlFreeDma(Adapter, &Adapter->RxUsedBd);
    IwlFreeDma(Adapter, &Adapter->RxStatus);
    for (i = 0; i < IWL_RB_CHUNKS; i++)
        IwlFreeDma(Adapter, &Adapter->RxChunks[i]);
    IwlFreeTxRing(Adapter, &Adapter->CmdQueue);
    IwlFreeDma(Adapter, &Adapter->CmdBuffers);
    IwlFreeTxRing(Adapter, &Adapter->MgmtQueue);
    IwlFreeTxRing(Adapter, &Adapter->DataQueue);
    IwlFreeDma(Adapter, &Adapter->ContextInfo);
    IwlFreeDma(Adapter, &Adapter->PrphInfo);
    IwlFreeDma(Adapter, &Adapter->PrphScratch);
    IwlFreeDma(Adapter, &Adapter->PnvmDesc);
}

static
VOID
IwlResetRx(
    _In_ PIWL_ADAPTER Adapter)
{
    PUCHAR Bd = Adapter->RxFreeBd.Va;
    ULONG i;

    RtlZeroMemory(Adapter->RxFreeBd.Va, Adapter->RxFreeBd.Length);
    RtlZeroMemory(Adapter->RxUsedBd.Va, Adapter->RxUsedBd.Length);
    RtlZeroMemory(Adapter->RxStatus.Va, Adapter->RxStatus.Length);

    for (i = 0; i < IWL_RX_POSTED; i++)
    {
        IwlPutLe16(Bd + i * IWL_RX_FREE_BD_SIZE, i + 1);
        IwlPutLe64(Bd + i * IWL_RX_FREE_BD_SIZE + 8, Adapter->RbPa[i]);
    }
    Adapter->RxRead = 0;
    Adapter->RxWrite = IWL_RX_POSTED;
    Adapter->RxSkipFragment = FALSE;
}

static
VOID
IwlResetTxRing(
    _Inout_ PIWL_TXQ Queue)
{
    RtlZeroMemory(Queue->Tfds.Va, Queue->Tfds.Length);
    if (Queue->ByteCounts.Va)
        RtlZeroMemory(Queue->ByteCounts.Va, Queue->ByteCounts.Length);
    Queue->Read = 0;
    Queue->Write = 0;
}

static
VOID
IwlRestockRx(
    _In_ PIWL_ADAPTER Adapter)
{
    KeMemoryBarrier();
    IwlWrite32(Adapter, HBUS_TARG_WRPTR, (Adapter->RxWrite & ~7UL) | IWL_RX_QUEUE_WRPTR(0));
}

static
ULONG
IwlLog2(
    _In_ ULONG Value)
{
    ULONG Result = 0;

    while (Value > 1)
    {
        Value >>= 1;
        Result++;
    }
    return Result;
}

static
BOOLEAN
IwlCopySection(
    _In_ PIWL_ADAPTER Adapter,
    _In_ const IWL_SECTION *Section,
    _Out_ PULONGLONG Address)
{
    PIWL_DMA Dma;

    if (Adapter->FwDramCount >= RTL_NUMBER_OF(Adapter->FwDram))
        return FALSE;

    Dma = &Adapter->FwDram[Adapter->FwDramCount];
    if (!IwlAllocDma(Adapter, Dma, Section->Length, FALSE))
        return FALSE;
    RtlCopyMemory(Dma->Va, Section->Data, Section->Length);
    Adapter->FwDramCount++;
    *Address = IwlDmaAddress(Dma);
    return TRUE;
}

static
ULONG
IwlCountSections(
    _In_ const IWL_FIRMWARE *Fw,
    _In_ ULONG Start)
{
    ULONG i;

    for (i = Start; i < Fw->SectionCount; i++)
    {
        if (Fw->Sections[i].Offset == IWL_SEC_CPU_SEPARATOR || Fw->Sections[i].Offset == IWL_SEC_PAGING_SEPARATOR)
            break;
    }
    return i - Start;
}

static
NTSTATUS
IwlBuildContextInfo(
    _In_ PIWL_ADAPTER Adapter)
{
    const IWL_FIRMWARE *Fw = &Adapter->Fw;
    PUCHAR Scratch = Adapter->PrphScratch.Va;
    PUCHAR Info = Adapter->ContextInfo.Va;
    ULONG Lmac, Umac, Paging, i;
    ULONGLONG Address;

    Lmac = IwlCountSections(Fw, 0);
    Umac = IwlCountSections(Fw, Lmac + 1);
    Paging = IwlCountSections(Fw, Lmac + Umac + 2);
    if (Lmac > IWL_CI_DRAM_ENTRIES || Umac > IWL_CI_DRAM_ENTRIES || Paging > IWL_CI_DRAM_ENTRIES ||
        Lmac + Umac + 2 > Fw->SectionCount || !Fw->Iml)
    {
        DPRINT1("IWLBE: bad firmware layout (lmac %lu umac %lu paging %lu)\n", Lmac, Umac, Paging);
        return STATUS_INVALID_IMAGE_FORMAT;
    }

    RtlZeroMemory(Scratch, Adapter->PrphScratch.Length);
    IwlPutLe16(Scratch + IWL_PS_VERSION, Adapter->HwRev & 0xFFFF);
    IwlPutLe16(Scratch + IWL_PS_VERSION + 2, 0);
    IwlPutLe16(Scratch + IWL_PS_VERSION + 4, IWL_PS_SIZE / 4);
    IwlPutLe32(Scratch + IWL_PS_CONTROL, IWL_PS_RB_SIZE_4K | IWL_PS_MTR_MODE | IWL_PS_MTR_FORMAT_256B);
    IwlPutLe64(Scratch + IWL_PS_RBD, IwlDmaAddress(&Adapter->RxFreeBd));

    for (i = 0; i < Lmac; i++)
    {
        if (!IwlCopySection(Adapter, &Fw->Sections[i], &Address))
            return STATUS_INSUFFICIENT_RESOURCES;
        IwlPutLe64(Scratch + IWL_PS_DRAM_LMAC + 8 * i, Address);
    }
    for (i = 0; i < Umac; i++)
    {
        if (!IwlCopySection(Adapter, &Fw->Sections[Lmac + 1 + i], &Address))
            return STATUS_INSUFFICIENT_RESOURCES;
        IwlPutLe64(Scratch + IWL_PS_DRAM_UMAC + 8 * i, Address);
    }
    for (i = 0; i < Paging; i++)
    {
        if (!IwlCopySection(Adapter, &Fw->Sections[Lmac + Umac + 2 + i], &Address))
            return STATUS_INSUFFICIENT_RESOURCES;
        IwlPutLe64(Scratch + IWL_PS_DRAM_VIRTUAL + 8 * i, Address);
    }

    RtlZeroMemory(Adapter->PrphInfo.Va, Adapter->PrphInfo.Length);
    RtlZeroMemory(Info, Adapter->ContextInfo.Length);
    IwlPutLe64(Info + IWL_CI2_PRPH_INFO, IwlDmaAddress(&Adapter->PrphInfo));
    IwlPutLe64(Info + IWL_CI2_PRPH_SCRATCH, IwlDmaAddress(&Adapter->PrphScratch));
    IwlPutLe32(Info + IWL_CI2_PRPH_SCRATCH_SIZE, IWL_PS_SIZE_COMMON);
    IwlPutLe64(Info + IWL_CI2_CR_HEAD, IwlDmaAddress(&Adapter->RxStatus));
    IwlPutLe64(Info + IWL_CI2_TR_TAIL, IwlDmaAddress(&Adapter->PrphInfo) + PAGE_SIZE / 2);
    IwlPutLe64(Info + IWL_CI2_CR_TAIL, IwlDmaAddress(&Adapter->PrphInfo) + 3 * PAGE_SIZE / 4);
    IwlPutLe64(Info + IWL_CI2_MTR_BASE, IwlDmaAddress(&Adapter->CmdQueue.Tfds));
    IwlPutLe64(Info + IWL_CI2_MCR_BASE, IwlDmaAddress(&Adapter->RxUsedBd));
    IwlPutLe16(Info + IWL_CI2_MTR_SIZE, IwlLog2(IWL_CMD_QUEUE_SIZE) - 3);
    IwlPutLe16(Info + IWL_CI2_MCR_SIZE, IwlLog2(IWL_RX_RING_SIZE));

    if (!IwlAllocDma(Adapter, &Adapter->ImlDma, Fw->ImlLength, FALSE))
        return STATUS_INSUFFICIENT_RESOURCES;
    RtlCopyMemory(Adapter->ImlDma.Va, Fw->Iml, Fw->ImlLength);

    DPRINT1("IWLBE: firmware image lmac %lu umac %lu paging %lu sections\n", Lmac, Umac, Paging);
    return STATUS_SUCCESS;
}

NTSTATUS
IwlKickFirmware(
    _In_ PIWL_ADAPTER Adapter)
{
    NTSTATUS Status;

    Status = IwlPrepareCardHw(Adapter);
    if (!NT_SUCCESS(Status))
        return Status;

    IwlDisableInterrupts(Adapter);
    IwlWrite32(Adapter, CSR_UCODE_DRV_GP1_CLR, CSR_UCODE_SW_BIT_RFKILL | CSR_UCODE_DRV_GP1_CMD_BLOCKED);

    if (!Adapter->DeviceEnabled)
    {
        Status = IwlApmInit(Adapter);
        if (!NT_SUCCESS(Status))
            return Status;
    }

    IwlWrite32(Adapter, CSR_INT_COALESCING, IWL_HOST_INT_TIMEOUT_DEF);
    IwlResetRx(Adapter);
    IwlResetTxRing(&Adapter->CmdQueue);
    Adapter->CmdQueue.Id = IWL_CMD_QUEUE;
    Adapter->CmdQueue.Ready = TRUE;
    IwlSetBits(Adapter, CSR_MAC_SHADOW_REG_CTRL, CSR_MAC_SHADOW_ALL);

    IwlFreeFirmwareDram(Adapter);
    IwlFreePnvmDram(Adapter);
    Status = IwlBuildContextInfo(Adapter);
    if (!NT_SUCCESS(Status))
        return Status;

    KeMemoryBarrier();
    Adapter->FirmwareAlive = FALSE;
    Adapter->FirmwareDead = FALSE;
    IwlEnableInterrupts(Adapter, CSR_INT_LOAD_MASK);
    IwlWrite32(Adapter, CSR_CTXT_INFO_ADDR, (ULONG)IwlDmaAddress(&Adapter->ContextInfo));
    IwlWrite32(Adapter, CSR_CTXT_INFO_ADDR + 4, (ULONG)(IwlDmaAddress(&Adapter->ContextInfo) >> 32));
    IwlWrite32(Adapter, CSR_IML_DATA_ADDR, (ULONG)IwlDmaAddress(&Adapter->ImlDma));
    IwlWrite32(Adapter, CSR_IML_DATA_ADDR + 4, (ULONG)(IwlDmaAddress(&Adapter->ImlDma) >> 32));
    IwlWrite32(Adapter, CSR_IML_SIZE_ADDR, Adapter->Fw.ImlLength);
    IwlSetBits(Adapter, CSR_CTXT_INFO_BOOT_CTRL, CSR_AUTO_FUNC_BOOT_ENA);

    IwlWrite32(Adapter, CSR_FUNC_SCRATCH, CSR_FUNC_SCRATCH_INIT);
    IwlSetBits(Adapter, CSR_GP_CNTRL, CSR_GP_CNTRL_BZ_ROM_START);
    return STATUS_SUCCESS;
}

VOID
IwlFirmwareAlive(
    _In_ PIWL_ADAPTER Adapter)
{
    IwlFreeFirmwareDram(Adapter);
    Adapter->FirmwareAlive = TRUE;
    IwlEnableInterrupts(Adapter, CSR_INT_RUNTIME_MASK);
}

NTSTATUS
IwlLoadPnvmDram(
    _In_ PIWL_ADAPTER Adapter,
    _In_reads_(Count) const IWL_SECTION *Chunks,
    _In_ ULONG Count)
{
    PUCHAR Scratch = Adapter->PrphScratch.Va;
    PUCHAR Desc = Adapter->PnvmDesc.Va;
    ULONG i, Total = 0;

    IwlFreePnvmDram(Adapter);
    RtlZeroMemory(Desc, Adapter->PnvmDesc.Length);
    for (i = 0; i < Count && i < IWL_PNVM_MAX_CHUNKS; i++)
    {
        if (!IwlAllocDma(Adapter, &Adapter->PnvmDram[i], Chunks[i].Length, FALSE))
        {
            IwlFreePnvmDram(Adapter);
            return STATUS_INSUFFICIENT_RESOURCES;
        }
        Adapter->PnvmDramCount++;
        RtlCopyMemory(Adapter->PnvmDram[i].Va, Chunks[i].Data, Chunks[i].Length);
        IwlPutLe64(Desc + 8 * i, IwlDmaAddress(&Adapter->PnvmDram[i]));
        Total += Chunks[i].Length;
    }

    IwlPutLe64(Scratch + IWL_PS_PNVM, IwlDmaAddress(&Adapter->PnvmDesc));
    IwlPutLe32(Scratch + IWL_PS_PNVM + 8, Total);
    KeMemoryBarrier();
    DPRINT1("IWLBE: PNVM %lu chunks, %lu bytes\n", Adapter->PnvmDramCount, Total);
    return STATUS_SUCCESS;
}

VOID
IwlArmNotification(
    _In_ PIWL_ADAPTER Adapter,
    _In_ UCHAR Group,
    _In_ UCHAR Command)
{
    Adapter->NotifGroup = Group;
    Adapter->NotifCommand = Command;
    Adapter->NotifLength = 0;
    KeClearEvent(&Adapter->NotifEvent);
    InterlockedExchange(&Adapter->NotifArmed, 1);
}

NTSTATUS
IwlWaitNotification(
    _In_ PIWL_ADAPTER Adapter,
    _In_ ULONG TimeoutMs)
{
    LARGE_INTEGER Timeout;
    NTSTATUS Status;

    Timeout.QuadPart = -(LONGLONG)TimeoutMs * 10000;
    Status = KeWaitForSingleObject(&Adapter->NotifEvent, Executive, KernelMode, FALSE, &Timeout);
    InterlockedExchange(&Adapter->NotifArmed, 0);
    if (Status == STATUS_TIMEOUT)
        return STATUS_IO_TIMEOUT;
    return Adapter->FirmwareDead ? STATUS_DEVICE_HARDWARE_ERROR : STATUS_SUCCESS;
}

static
VOID
IwlSetTb(
    _Inout_ PUCHAR Tfd,
    _In_ ULONG Index,
    _In_ ULONGLONG Address,
    _In_ ULONG Length)
{
    PUCHAR Tb = Tfd + 2 + Index * IWL_TFD_TB_SIZE;

    IwlPutLe16(Tb, Length);
    IwlPutLe64(Tb + 2, Address);
}

NTSTATUS
IwlSendCommand(
    _In_ PIWL_ADAPTER Adapter,
    _In_ UCHAR Group,
    _In_ UCHAR Command,
    _In_reads_bytes_opt_(Length) const VOID *Data,
    _In_ ULONG Length,
    _Out_writes_bytes_opt_(ResponseLength) PVOID Response,
    _In_ ULONG ResponseLength,
    _Out_opt_ PULONG Returned)
{
    PIWL_TXQ Queue = &Adapter->CmdQueue;
    LARGE_INTEGER Timeout;
    ULONG Slot, Total;
    ULONGLONG Address;
    PUCHAR Buffer, Tfd;
    NTSTATUS Status;

    if (Returned)
        *Returned = 0;
    if (IWL_CMD_HEADER_WIDE + Length > IWL_CMD_BUFFER_SIZE)
        return STATUS_INVALID_PARAMETER;

    KeWaitForSingleObject(&Adapter->CommandLock, Executive, KernelMode, FALSE, NULL);
    if (!Queue->Ready || Adapter->FirmwareDead || !Adapter->Registers)
    {
        KeReleaseMutex(&Adapter->CommandLock, FALSE);
        return STATUS_DEVICE_NOT_READY;
    }

    Slot = Queue->Write & (IWL_CMD_QUEUE_SIZE - 1);
    Buffer = (PUCHAR)Adapter->CmdBuffers.Va + (Queue->Write % IWL_CMD_BUFFERS) * IWL_CMD_BUFFER_SIZE;
    Address = IwlDmaAddress(&Adapter->CmdBuffers) + (Queue->Write % IWL_CMD_BUFFERS) * IWL_CMD_BUFFER_SIZE;
    Total = IWL_CMD_HEADER_WIDE + Length;

    Buffer[0] = Command;
    Buffer[1] = Group;
    IwlPutLe16(Buffer + 2, IWL_CMD_SEQ(IWL_CMD_QUEUE, Slot));
    IwlPutLe16(Buffer + 4, Length);
    Buffer[6] = 0;
    Buffer[7] = 0;
    if (Length)
        RtlCopyMemory(Buffer + IWL_CMD_HEADER_WIDE, Data, Length);

    Tfd = (PUCHAR)Queue->Tfds.Va + Slot * IWL_TFD_SIZE;
    RtlZeroMemory(Tfd, IWL_TFD_SIZE);
    if (Total > IWL_FIRST_TB_SIZE)
    {
        IwlSetTb(Tfd, 0, Address, IWL_FIRST_TB_SIZE);
        IwlSetTb(Tfd, 1, Address + IWL_FIRST_TB_SIZE, Total - IWL_FIRST_TB_SIZE);
        IwlPutLe16(Tfd, 2);
    }
    else
    {
        IwlSetTb(Tfd, 0, Address, Total);
        IwlPutLe16(Tfd, 1);
    }

    Adapter->CommandSequence = IWL_CMD_SEQ(IWL_CMD_QUEUE, Slot);
    Adapter->CommandResponse = Response;
    Adapter->CommandResponseLength = Response ? ResponseLength : 0;
    Adapter->CommandReturned = 0;
    KeClearEvent(&Adapter->CommandDone);
    InterlockedExchange(&Adapter->CommandWaiting, 1);

    Queue->Write++;
    KeMemoryBarrier();
    IwlWrite32(Adapter, HBUS_TARG_WRPTR, Queue->Write | ((ULONG)IWL_CMD_QUEUE << 16));

    Timeout.QuadPart = -(LONGLONG)IWL_CMD_TIMEOUT_MS * 10000;
    Status = KeWaitForSingleObject(&Adapter->CommandDone, Executive, KernelMode, FALSE, &Timeout);
    if (Adapter->FirmwareDead)
    {
        InterlockedExchange(&Adapter->CommandWaiting, 0);
        Status = STATUS_DEVICE_HARDWARE_ERROR;
    }
    else if (InterlockedExchange(&Adapter->CommandWaiting, 0) != 0 || Status == STATUS_TIMEOUT)
    {
        DPRINT1("IWLBE: command %02x:%02x timed out\n", Group, Command);
        Status = STATUS_IO_TIMEOUT;
    }
    else
    {
        Status = STATUS_SUCCESS;
        if (Returned)
            *Returned = Adapter->CommandReturned;
    }
    Adapter->CommandResponse = NULL;
    KeReleaseMutex(&Adapter->CommandLock, FALSE);
    return Status;
}

static
VOID
IwlCompleteCommand(
    _In_ PIWL_ADAPTER Adapter,
    _In_ USHORT Sequence,
    _In_reads_bytes_(Length) const UCHAR *Payload,
    _In_ ULONG Length)
{
    PIWL_TXQ Queue = &Adapter->CmdQueue;

    if (!Adapter->CommandWaiting || Sequence != Adapter->CommandSequence)
        return;

    if (Adapter->CommandResponse)
    {
        Adapter->CommandReturned = min(Length, Adapter->CommandResponseLength);
        RtlCopyMemory(Adapter->CommandResponse, Payload, Adapter->CommandReturned);
    }
    Queue->Read = Queue->Write;
    InterlockedExchange(&Adapter->CommandWaiting, 0);
    KeSetEvent(&Adapter->CommandDone, IO_NETWORK_INCREMENT, FALSE);
}

NTSTATUS
IwlAllocateTxQueue(
    _In_ PIWL_ADAPTER Adapter,
    _Inout_ PIWL_TXQ Queue,
    _In_ UCHAR Tid)
{
    UCHAR Cmd[IWL_SCD_CMD_SIZE], Resp[IWL_SCD_RESP_SIZE];
    ULONG Returned;
    NTSTATUS Status;

    NdisAcquireSpinLock(&Adapter->TxLock);
    Queue->Ready = FALSE;
    IwlResetTxRing(Queue);
    NdisReleaseSpinLock(&Adapter->TxLock);

    RtlZeroMemory(Cmd, sizeof(Cmd));
    IwlPutLe32(Cmd + 0, IWL_SCD_ADD);
    IwlPutLe32(Cmd + 4, IWL_BIT(IWL_STA_ID_AP));
    Cmd[8] = Tid;
    IwlPutLe32(Cmd + 12, 0);
    IwlPutLe32(Cmd + 16, IwlLog2(Queue->Size) - 3);
    IwlPutLe64(Cmd + 20, IwlDmaAddress(&Queue->ByteCounts));
    IwlPutLe64(Cmd + 28, IwlDmaAddress(&Queue->Tfds));

    Status = IwlSendCommand(Adapter, IWL_GRP_DATA_PATH, IWL_CMD_SCD_QUEUE_CONFIG, Cmd, sizeof(Cmd),
                            Resp, sizeof(Resp), &Returned);
    if (!NT_SUCCESS(Status))
        return Status;
    if (Returned < sizeof(Resp))
        return STATUS_DEVICE_PROTOCOL_ERROR;

    NdisAcquireSpinLock(&Adapter->TxLock);
    Queue->Id = IwlLe16(Resp);
    Queue->Write = IwlLe16(Resp + 4);
    Queue->Read = Queue->Write;
    Queue->Ready = TRUE;
    NdisReleaseSpinLock(&Adapter->TxLock);
    DPRINT1("IWLBE: TID %u uses queue %u\n", Tid, Queue->Id);
    return STATUS_SUCCESS;
}

VOID
IwlReleaseTxQueue(
    _In_ PIWL_ADAPTER Adapter,
    _Inout_ PIWL_TXQ Queue,
    _In_ UCHAR Tid,
    _In_ BOOLEAN TellFirmware)
{
    UCHAR Cmd[IWL_SCD_CMD_SIZE];
    BOOLEAN WasReady;

    NdisAcquireSpinLock(&Adapter->TxLock);
    WasReady = Queue->Ready;
    Queue->Ready = FALSE;
    NdisReleaseSpinLock(&Adapter->TxLock);

    if (!WasReady || !TellFirmware)
        return;

    RtlZeroMemory(Cmd, sizeof(Cmd));
    IwlPutLe32(Cmd + 0, IWL_SCD_REMOVE);
    IwlPutLe32(Cmd + 4, IWL_BIT(IWL_STA_ID_AP));
    Cmd[8] = Tid;
    IwlSendCommand(Adapter, IWL_GRP_DATA_PATH, IWL_CMD_SCD_QUEUE_CONFIG, Cmd, sizeof(Cmd), NULL, 0, NULL);
}

NTSTATUS
IwlQueueFrame(
    _In_ PIWL_ADAPTER Adapter,
    _Inout_ PIWL_TXQ Queue,
    _In_reads_bytes_(HeaderLength) const UCHAR *Header,
    _In_ ULONG HeaderLength,
    _In_reads_bytes_(PayloadLength) const UCHAR *Payload,
    _In_ ULONG PayloadLength,
    _In_ ULONG RateFlags,
    _In_ ULONG TxFlags)
{
    ULONG Slot, Index, PayloadOffset, Tbs, Filled, Length = HeaderLength + PayloadLength;
    ULONGLONG Address;
    PUCHAR Buffer, Tfd, Cmd;

    if (IWL_TX_CMD_HEADER + IWL_TX_CMD_SIZE + 4 + Length > IWL_TXQ_BUFFER_SIZE)
        return STATUS_INVALID_PARAMETER;

    NdisAcquireSpinLock(&Adapter->TxLock);
    if (!Queue->Ready || !Adapter->FirmwareAlive)
    {
        NdisReleaseSpinLock(&Adapter->TxLock);
        return STATUS_DEVICE_NOT_READY;
    }
    if ((USHORT)(Queue->Write - Queue->Read) >= IWL_TXQ_BUFFERS)
    {
        NdisReleaseSpinLock(&Adapter->TxLock);
        return STATUS_DEVICE_BUSY;
    }

    Slot = Queue->Write & (Queue->Size - 1);
    Index = Queue->Write % IWL_TXQ_BUFFERS;
    Buffer = (PUCHAR)Queue->Buffers.Va + Index * IWL_TXQ_BUFFER_SIZE;
    Address = IwlDmaAddress(&Queue->Buffers) + Index * IWL_TXQ_BUFFER_SIZE;

    RtlZeroMemory(Buffer, IWL_TX_CMD_HEADER + IWL_TX_CMD_SIZE);
    Buffer[0] = IWL_CMD_TX;
    Buffer[1] = 0;
    IwlPutLe16(Buffer + 2, IWL_CMD_SEQ(Queue->Id, Slot));
    Cmd = Buffer + IWL_TX_CMD_HEADER;
    IwlPutLe16(Cmd + 0, Length);
    IwlPutLe16(Cmd + 2, TxFlags);
    IwlPutLe32(Cmd + 4, ((HeaderLength / 2) << IWL_TX_OFFLOAD_MH_SHIFT) |
                        ((HeaderLength & 3) ? IWL_TX_OFFLOAD_PAD : 0));
    IwlPutLe32(Cmd + 16, RateFlags);

    RtlCopyMemory(Buffer + IWL_TX_CMD_HEADER + IWL_TX_CMD_SIZE, Header, HeaderLength);
    PayloadOffset = (IWL_TX_CMD_HEADER + IWL_TX_CMD_SIZE + HeaderLength + 3) & ~3UL;
    if (PayloadLength)
        RtlCopyMemory(Buffer + PayloadOffset, Payload, PayloadLength);

    Tfd = (PUCHAR)Queue->Tfds.Va + Slot * IWL_TFD_SIZE;
    RtlZeroMemory(Tfd, IWL_TFD_SIZE);
    IwlSetTb(Tfd, 0, Address, IWL_FIRST_TB_SIZE);
    IwlSetTb(Tfd, 1, Address + IWL_FIRST_TB_SIZE, PayloadOffset - IWL_FIRST_TB_SIZE);
    Tbs = 2;
    if (PayloadLength)
        IwlSetTb(Tfd, Tbs++, Address + PayloadOffset, PayloadLength);
    IwlPutLe16(Tfd, Tbs);

    Filled = 2 + Tbs * IWL_TFD_TB_SIZE;
    IwlPutLe16((PUCHAR)Queue->ByteCounts.Va + 2 * Slot, Length | ((((Filled + 63) / 64) - 1) << 14));

    Queue->Write++;
    KeMemoryBarrier();
    IwlWrite32(Adapter, HBUS_TARG_WRPTR, Queue->Write | ((ULONG)Queue->Id << 16));
    NdisReleaseSpinLock(&Adapter->TxLock);
    return STATUS_SUCCESS;
}

VOID
IwlReclaimTx(
    _In_ PIWL_ADAPTER Adapter,
    _In_ USHORT QueueId,
    _In_ USHORT Ssn)
{
    PIWL_TXQ Queues[2] = { &Adapter->MgmtQueue, &Adapter->DataQueue };
    PIWL_TXQ Queue;
    ULONG i;

    NdisAcquireSpinLock(&Adapter->TxLock);
    for (i = 0; i < RTL_NUMBER_OF(Queues); i++)
    {
        Queue = Queues[i];
        if (!Queue->Ready || Queue->Id != QueueId)
            continue;
        while ((Queue->Read & (Queue->Size - 1)) != (Ssn & (Queue->Size - 1)) && Queue->Read != Queue->Write)
            Queue->Read++;
    }
    NdisReleaseSpinLock(&Adapter->TxLock);
}

static
VOID
IwlDispatchPacket(
    _In_ PIWL_ADAPTER Adapter,
    _In_ PUCHAR Packet,
    _In_ ULONG Available)
{
    ULONG Length, PayloadLength;
    UCHAR Command, Group;
    USHORT Sequence;
    PUCHAR Payload;

    if (Available < 8)
        return;

    Length = IwlLe32(Packet) & IWL_RX_FRAME_SIZE_MASK;
    if (IwlLe32(Packet) == IWL_RX_FRAME_INVALID || Length < 4 || Length + 4 > Available)
        return;

    Command = Packet[4];
    Group = Packet[5];
    Sequence = IwlLe16(Packet + 6);
    Payload = Packet + 8;
    PayloadLength = Length - 4;

    if (InterlockedCompareExchange(&Adapter->NotifArmed, 0, 0) &&
        Group == Adapter->NotifGroup && Command == Adapter->NotifCommand)
    {
        Adapter->NotifLength = min(PayloadLength, IWL_NOTIF_MAX);
        RtlCopyMemory(Adapter->NotifData, Payload, Adapter->NotifLength);
        InterlockedExchange(&Adapter->NotifArmed, 0);
        KeSetEvent(&Adapter->NotifEvent, IO_NETWORK_INCREMENT, FALSE);
    }

    if (!(Sequence & IWL_SEQ_RX_FRAME) && ((Sequence >> 8) & 0x1F) == IWL_CMD_QUEUE &&
        !(Group == IWL_GRP_LEGACY && Command == IWL_CMD_TX))
    {
        IwlCompleteCommand(Adapter, Sequence, Payload, PayloadLength);
        return;
    }

    IwlHandlePacket(Adapter, Packet, Length + 4);
}

static
VOID
IwlServiceRx(
    _In_ PIWL_ADAPTER Adapter)
{
    PUCHAR Used = Adapter->RxUsedBd.Va, Free = Adapter->RxFreeBd.Va;
    ULONG Closed, Vid, Posted = 0;
    BOOLEAN Fragment;

    Closed = (IwlLe16(Adapter->RxStatus.Va) & 0xFFF) & (IWL_RX_RING_SIZE - 1);
    KeMemoryBarrier();

    while (Adapter->RxRead != Closed)
    {
        Vid = IwlLe16(Used + Adapter->RxRead * IWL_RX_USED_BD_SIZE);
        Fragment = (Used[Adapter->RxRead * IWL_RX_USED_BD_SIZE + 2] & IWL_RX_CD_FRAGMENTED) != 0;
        Adapter->RxRead = (Adapter->RxRead + 1) & (IWL_RX_RING_SIZE - 1);

        if (Vid == 0 || Vid > IWL_RX_POSTED)
        {
            DPRINT1("IWLBE: invalid receive buffer id %lu\n", Vid);
            continue;
        }

        if (Fragment || Adapter->RxSkipFragment)
            Adapter->RxSkipFragment = Fragment;
        else
            IwlDispatchPacket(Adapter, Adapter->RbVa[Vid - 1], IWL_RB_SIZE);

        IwlPutLe16(Free + Adapter->RxWrite * IWL_RX_FREE_BD_SIZE, Vid);
        IwlPutLe64(Free + Adapter->RxWrite * IWL_RX_FREE_BD_SIZE + 8, Adapter->RbPa[Vid - 1]);
        Adapter->RxWrite = (Adapter->RxWrite + 1) & (IWL_RX_RING_SIZE - 1);
        Posted++;
    }

    if (Posted && Adapter->FirmwareAlive)
        IwlRestockRx(Adapter);
}

static
BOOLEAN
NTAPI
IwlInterrupt(
    _In_ NDIS_HANDLE MiniportInterruptContext,
    _Out_ PBOOLEAN QueueDefaultInterruptDpc,
    _Out_ PULONG TargetProcessors)
{
    PIWL_ADAPTER Adapter = MiniportInterruptContext;
    ULONG Causes, Fh;

    *QueueDefaultInterruptDpc = FALSE;
    *TargetProcessors = 0;
    if (!Adapter->Registers || !Adapter->IntMask)
        return FALSE;

    Causes = IwlRead32(Adapter, CSR_INT);
    if (Causes == 0 || Causes == 0xFFFFFFFF)
        return FALSE;

    Fh = IwlRead32(Adapter, CSR_FH_INT_STATUS);
    IwlWrite32(Adapter, CSR_INT_MASK, 0);
    IwlWrite32(Adapter, CSR_INT, Causes);
    IwlWrite32(Adapter, CSR_FH_INT_STATUS, Fh);
    InterlockedOr(&Adapter->PendingCauses, (LONG)Causes);
    *QueueDefaultInterruptDpc = TRUE;
    return TRUE;
}

static
VOID
NTAPI
IwlInterruptDpc(
    _In_ NDIS_HANDLE MiniportInterruptContext,
    _In_ PVOID MiniportDpcContext,
    _In_ PVOID ReceiveThrottleParameters,
    _In_ PVOID NdisReserved2)
{
    PIWL_ADAPTER Adapter = MiniportInterruptContext;

    UNREFERENCED_PARAMETER(MiniportDpcContext);
    UNREFERENCED_PARAMETER(ReceiveThrottleParameters);
    UNREFERENCED_PARAMETER(NdisReserved2);

    KeSetEvent(&Adapter->WorkerEvent, IO_NETWORK_INCREMENT, FALSE);
}

static
VOID
NTAPI
IwlDisableInterrupt(
    _In_ NDIS_HANDLE MiniportInterruptContext)
{
    PIWL_ADAPTER Adapter = MiniportInterruptContext;

    if (Adapter->Registers)
        IwlWrite32(Adapter, CSR_INT_MASK, 0);
}

static
VOID
NTAPI
IwlEnableInterrupt(
    _In_ NDIS_HANDLE MiniportInterruptContext)
{
    PIWL_ADAPTER Adapter = MiniportInterruptContext;

    if (Adapter->Registers)
        IwlWrite32(Adapter, CSR_INT_MASK, Adapter->IntMask);
}

NDIS_STATUS
IwlRegisterInterrupt(
    _In_ PIWL_ADAPTER Adapter)
{
    NDIS_MINIPORT_INTERRUPT_CHARACTERISTICS Characteristics;
    NDIS_STATUS Status;

    if (!Adapter->HasInterrupt)
    {
        DPRINT1("IWLBE: no interrupt resource, polling only\n");
        return NDIS_STATUS_SUCCESS;
    }

    NdisZeroMemory(&Characteristics, sizeof(Characteristics));
    Characteristics.Header.Type = NDIS_OBJECT_TYPE_MINIPORT_INTERRUPT;
    Characteristics.Header.Revision = NDIS_MINIPORT_INTERRUPT_REVISION_1;
    Characteristics.Header.Size = NDIS_SIZEOF_MINIPORT_INTERRUPT_CHARACTERISTICS_REVISION_1;
    Characteristics.InterruptHandler = IwlInterrupt;
    Characteristics.InterruptDpcHandler = IwlInterruptDpc;
    Characteristics.DisableInterruptHandler = IwlDisableInterrupt;
    Characteristics.EnableInterruptHandler = IwlEnableInterrupt;

    Status = NdisMRegisterInterruptEx(Adapter->MiniportAdapterHandle, Adapter, &Characteristics,
                                      &Adapter->InterruptHandle);
    if (Status != NDIS_STATUS_SUCCESS)
    {
        DPRINT1("IWLBE: interrupt registration failed 0x%08lx, polling only\n", Status);
        Adapter->InterruptHandle = NULL;
    }
    return NDIS_STATUS_SUCCESS;
}

VOID
IwlDeregisterInterrupt(
    _In_ PIWL_ADAPTER Adapter)
{
    if (Adapter->InterruptHandle)
    {
        if (Adapter->Registers)
            IwlWrite32(Adapter, CSR_INT_MASK, 0);
        NdisMDeregisterInterruptEx(Adapter->InterruptHandle);
        Adapter->InterruptHandle = NULL;
    }
}

static
VOID
IwlReportError(
    _In_ PIWL_ADAPTER Adapter,
    _In_ ULONG Causes)
{
    if (Adapter->FirmwareDead)
        return;

    Adapter->FirmwareDead = TRUE;
    DPRINT1("IWLBE: firmware error, causes 0x%08lx GP_CNTRL 0x%08lx CPU1 0x%08lx CPU2 0x%08lx\n",
            Causes, IwlRead32(Adapter, CSR_GP_CNTRL),
            IwlReadPrph(Adapter, UMAG_SB_CPU_1_STATUS), IwlReadPrph(Adapter, UMAG_SB_CPU_2_STATUS));
    KeSetEvent(&Adapter->CommandDone, IO_NETWORK_INCREMENT, FALSE);
    KeSetEvent(&Adapter->NotifEvent, IO_NETWORK_INCREMENT, FALSE);
}

static
VOID
NTAPI
IwlWorkerThread(
    _In_ PVOID Context)
{
    PIWL_ADAPTER Adapter = Context;
    LARGE_INTEGER Timeout;
    ULONG Causes, Polled, Fh;

    Timeout.QuadPart = -(LONGLONG)IWL_WORKER_POLL_MS * 10000;
    while (!Adapter->WorkerStop)
    {
        KeWaitForSingleObject(&Adapter->WorkerEvent, Executive, KernelMode, FALSE, &Timeout);
        if (Adapter->WorkerStop)
            break;

        Causes = (ULONG)InterlockedExchange(&Adapter->PendingCauses, 0);
        if (Adapter->Registers && Adapter->DeviceEnabled)
        {
            Polled = IwlRead32(Adapter, CSR_INT);
            if (Polled && Polled != 0xFFFFFFFF)
            {
                Fh = IwlRead32(Adapter, CSR_FH_INT_STATUS);
                IwlWrite32(Adapter, CSR_INT, Polled);
                IwlWrite32(Adapter, CSR_FH_INT_STATUS, Fh);
                Causes |= Polled;
            }
            else if (Polled == 0xFFFFFFFF)
            {
                Causes |= CSR_INT_BIT_HW_ERR;
            }

            if (Causes & CSR_INT_BIT_ALIVE)
                IwlRestockRx(Adapter);
            if (Causes & CSR_INT_BIT_RF_KILL)
                DPRINT1("IWLBE: radio switch %s\n",
                        (IwlRead32(Adapter, CSR_GP_CNTRL) & CSR_GP_CNTRL_RFKILL) ? "on" : "off");
            if (Causes & CSR_INT_BIT_CT_KILL)
                DPRINT1("IWLBE: critical temperature shutdown\n");

            IwlServiceRx(Adapter);

            if (Causes & (CSR_INT_BIT_HW_ERR | CSR_INT_BIT_SW_ERR))
                IwlReportError(Adapter, Causes);

            if (Adapter->IntMask)
                IwlWrite32(Adapter, CSR_INT_MASK, Adapter->IntMask);
        }

        IwlTxPump(Adapter);
    }

    KeSetEvent(&Adapter->WorkerStopped, IO_NO_INCREMENT, FALSE);
    PsTerminateSystemThread(STATUS_SUCCESS);
}

NTSTATUS
IwlStartWorker(
    _In_ PIWL_ADAPTER Adapter)
{
    HANDLE ThreadHandle;
    NTSTATUS Status;

    Status = PsCreateSystemThread(&ThreadHandle, THREAD_ALL_ACCESS, NULL, NULL, NULL, IwlWorkerThread, Adapter);
    if (!NT_SUCCESS(Status))
        return Status;

    ObReferenceObjectByHandle(ThreadHandle, THREAD_ALL_ACCESS, NULL, KernelMode, (PVOID *)&Adapter->WorkerThread, NULL);
    ZwClose(ThreadHandle);
    return STATUS_SUCCESS;
}

VOID
IwlStopWorker(
    _In_ PIWL_ADAPTER Adapter)
{
    if (!Adapter->WorkerThread)
        return;

    Adapter->WorkerStop = TRUE;
    KeSetEvent(&Adapter->WorkerEvent, IO_NETWORK_INCREMENT, FALSE);
    KeWaitForSingleObject(&Adapter->WorkerStopped, Executive, KernelMode, FALSE, NULL);
    ObDereferenceObject(Adapter->WorkerThread);
    Adapter->WorkerThread = NULL;
}
