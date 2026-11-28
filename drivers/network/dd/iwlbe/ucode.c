/*
 * PROJECT:     LiberNT Intel BE200 Native 802.11 Miniport
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Firmware loading, boot sequence and runtime configuration
 * COPYRIGHT:   Copyright 2026 The LiberNT contributors
 */

#include "iwlbe.h"

#define NDEBUG
#include <debug.h>

#define IWL_PNVM_TIMEOUT_MS             1000
#define IWL_MCC_WORLD                   (('Z' << 8) | 'Z')

static const UCHAR IwlDefaultChannels5G[] =
{
    36, 40, 44, 48, 52, 56, 60, 64, 100, 104, 108, 112, 116, 120, 124, 128, 132, 136, 140, 144,
    149, 153, 157, 161, 165
};

NTSTATUS
IwlLoadFirmwareFile(
    _In_ PIWL_ADAPTER Adapter)
{
    FILE_STANDARD_INFORMATION FileInfo;
    OBJECT_ATTRIBUTES Attributes;
    IO_STATUS_BLOCK IoStatus;
    UNICODE_STRING Path;
    HANDLE Handle;
    NTSTATUS Status;
    PUCHAR Buffer;
    ULONG Length;

    if (Adapter->Fw.File)
        return STATUS_SUCCESS;

    RtlInitUnicodeString(&Path, IWL_FIRMWARE_PATH);
    InitializeObjectAttributes(&Attributes, &Path, OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE, NULL, NULL);
    Status = ZwCreateFile(&Handle, GENERIC_READ | SYNCHRONIZE, &Attributes, &IoStatus, NULL,
                          FILE_ATTRIBUTE_NORMAL, FILE_SHARE_READ, FILE_OPEN,
                          FILE_SYNCHRONOUS_IO_NONALERT | FILE_NON_DIRECTORY_FILE, NULL, 0);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("IWLBE: firmware %ws unavailable 0x%08lx\n", IWL_FIRMWARE_PATH, Status);
        return Status;
    }

    Status = ZwQueryInformationFile(Handle, &IoStatus, &FileInfo, sizeof(FileInfo), FileStandardInformation);
    if (!NT_SUCCESS(Status) ||
        FileInfo.EndOfFile.QuadPart <= IWL_UCODE_TLV_START ||
        FileInfo.EndOfFile.QuadPart > IWL_FIRMWARE_MAX_SIZE)
    {
        ZwClose(Handle);
        return NT_SUCCESS(Status) ? STATUS_INVALID_IMAGE_FORMAT : Status;
    }

    Length = (ULONG)FileInfo.EndOfFile.QuadPart;
    Buffer = ExAllocatePoolWithTag(NonPagedPool, Length, IWL_TAG);
    if (!Buffer)
    {
        ZwClose(Handle);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    Status = ZwReadFile(Handle, NULL, NULL, NULL, &IoStatus, Buffer, Length, NULL, NULL);
    ZwClose(Handle);
    if (!NT_SUCCESS(Status) || IoStatus.Information != Length)
    {
        ExFreePoolWithTag(Buffer, IWL_TAG);
        return NT_SUCCESS(Status) ? STATUS_END_OF_FILE : Status;
    }

    Adapter->Fw.File = Buffer;
    Adapter->Fw.FileLength = Length;
    Status = IwlParseFirmware(&Adapter->Fw);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("IWLBE: firmware file is not a valid image\n");
        ExFreePoolWithTag(Buffer, IWL_TAG);
        RtlZeroMemory(&Adapter->Fw, sizeof(Adapter->Fw));
        return Status;
    }

    Adapter->ValidTxChains = (UCHAR)((Adapter->Fw.PhyConfig >> IWL_FW_PHY_CFG_TX_CHAIN_POS) & 0xF);
    Adapter->ValidRxChains = (UCHAR)((Adapter->Fw.PhyConfig >> IWL_FW_PHY_CFG_RX_CHAIN_POS) & 0xF);
    if (!Adapter->ValidTxChains)
        Adapter->ValidTxChains = 1;
    if (!Adapter->ValidRxChains)
        Adapter->ValidRxChains = 1;

    DPRINT1("IWLBE: firmware version %lu, %lu sections, IML %lu bytes, PNVM %lu bytes, phy 0x%08lx\n",
            Adapter->Fw.Version, Adapter->Fw.SectionCount, Adapter->Fw.ImlLength,
            Adapter->Fw.PnvmLength, Adapter->Fw.PhyConfig);
    return STATUS_SUCCESS;
}

static
BOOLEAN
IwlValidAddress(
    _In_reads_bytes_(IWL_ADDRESS_LENGTH) const UCHAR *Address)
{
    static const UCHAR Zero[IWL_ADDRESS_LENGTH] = { 0 };
    static const UCHAR Ones[IWL_ADDRESS_LENGTH] = { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff };

    return !(Address[0] & 0x01) &&
           !RtlEqualMemory(Address, Zero, IWL_ADDRESS_LENGTH) &&
           !RtlEqualMemory(Address, Ones, IWL_ADDRESS_LENGTH);
}

static
VOID
IwlFlipAddress(
    _In_ ULONG Low,
    _In_ ULONG High,
    _Out_writes_bytes_(IWL_ADDRESS_LENGTH) PUCHAR Address)
{
    Address[0] = (UCHAR)(Low >> 24);
    Address[1] = (UCHAR)(Low >> 16);
    Address[2] = (UCHAR)(Low >> 8);
    Address[3] = (UCHAR)Low;
    Address[4] = (UCHAR)(High >> 8);
    Address[5] = (UCHAR)High;
}

VOID
IwlReadMacAddress(
    _In_ PIWL_ADAPTER Adapter)
{
    UCHAR Address[IWL_ADDRESS_LENGTH];

    IwlFlipAddress(IwlRead32(Adapter, CSR_MAC_ADDR0_STRAP), IwlRead32(Adapter, CSR_MAC_ADDR1_STRAP), Address);
    if (!IwlValidAddress(Address))
        IwlFlipAddress(IwlRead32(Adapter, CSR_MAC_ADDR0_OTP), IwlRead32(Adapter, CSR_MAC_ADDR1_OTP), Address);

    if (!IwlValidAddress(Address))
    {
        DPRINT1("IWLBE: no valid MAC address in the device, using a local one\n");
        Address[0] = 0x02;
        Address[1] = 0x00;
        Address[2] = 0x86;
        Address[3] = (UCHAR)(Adapter->HwRev >> 8);
        Address[4] = (UCHAR)Adapter->HwRev;
        Address[5] = 0x01;
    }

    RtlCopyMemory(Adapter->PermanentAddress, Address, IWL_ADDRESS_LENGTH);
    RtlCopyMemory(Adapter->CurrentAddress, Address, IWL_ADDRESS_LENGTH);
}

static
NTSTATUS
IwlWaitAlive(
    _In_ PIWL_ADAPTER Adapter)
{
    NTSTATUS Status;
    USHORT AliveStatus;

    IwlArmNotification(Adapter, IWL_GRP_LEGACY, IWL_CMD_ALIVE);
    Status = IwlKickFirmware(Adapter);
    if (!NT_SUCCESS(Status))
        return Status;

    Status = IwlWaitNotification(Adapter, IWL_ALIVE_TIMEOUT_MS);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("IWLBE: no ALIVE from firmware (0x%08lx), CPU1 0x%08lx CPU2 0x%08lx\n", Status,
                IwlReadPrph(Adapter, UMAG_SB_CPU_1_STATUS), IwlReadPrph(Adapter, UMAG_SB_CPU_2_STATUS));
        return Status;
    }

    AliveStatus = IwlLe16(Adapter->NotifData);
    if (Adapter->NotifLength < IWL_ALIVE_SKU_ID + 12 || AliveStatus != IWL_ALIVE_STATUS_OK)
    {
        DPRINT1("IWLBE: firmware reported ALIVE status 0x%04x (%lu bytes)\n", AliveStatus, Adapter->NotifLength);
        return STATUS_DEVICE_HARDWARE_ERROR;
    }

    Adapter->SkuId[0] = IwlLe32(Adapter->NotifData + IWL_ALIVE_SKU_ID);
    Adapter->SkuId[1] = IwlLe32(Adapter->NotifData + IWL_ALIVE_SKU_ID + 4);
    Adapter->SkuId[2] = IwlLe32(Adapter->NotifData + IWL_ALIVE_SKU_ID + 8);
    IwlFirmwareAlive(Adapter);
    DPRINT1("IWLBE: firmware alive, sku %08lx %08lx %08lx\n", Adapter->SkuId[0], Adapter->SkuId[1], Adapter->SkuId[2]);
    return STATUS_SUCCESS;
}

static
NTSTATUS
IwlLoadPnvm(
    _In_ PIWL_ADAPTER Adapter)
{
    IWL_SECTION *Chunks;
    NTSTATUS Status;
    ULONG Count;

    if (!Adapter->SkuId[0] && !Adapter->SkuId[1] && !Adapter->SkuId[2])
        return STATUS_SUCCESS;

    Chunks = IwlAllocate(sizeof(IWL_SECTION) * IWL_PNVM_MAX_CHUNKS);
    if (!Chunks)
        return STATUS_INSUFFICIENT_RESOURCES;

    Count = IwlParsePnvm(Adapter->Fw.Pnvm, Adapter->Fw.PnvmLength, Adapter->SkuId,
                         CSR_HW_REV_TYPE(Adapter->HwRev), CSR_HW_RFID_TYPE(Adapter->HwRfId), Chunks);
    if (Count)
    {
        Status = IwlLoadPnvmDram(Adapter, Chunks, Count);
        if (!NT_SUCCESS(Status))
        {
            IwlFree(Chunks);
            return Status;
        }
    }
    else
    {
        DPRINT1("IWLBE: no PNVM section for this SKU and radio\n");
    }
    IwlFree(Chunks);

    IwlArmNotification(Adapter, IWL_GRP_REGULATORY, IWL_CMD_PNVM_INIT_COMPLETE);
    IwlWritePrph(Adapter, UREG_DOORBELL_TO_ISR6 + IWL_UMAC_PRPH_OFFSET, UREG_DOORBELL_TO_ISR6_PNVM);
    Status = IwlWaitNotification(Adapter, IWL_PNVM_TIMEOUT_MS);
    if (!NT_SUCCESS(Status))
        DPRINT1("IWLBE: PNVM was not accepted 0x%08lx\n", Status);
    return Status;
}

static
NTSTATUS
IwlRunInitSequence(
    _In_ PIWL_ADAPTER Adapter)
{
    UCHAR Init[4], Phy[IWL_PHY_CFG_CMD_SIZE];
    NTSTATUS Status;

    IwlArmNotification(Adapter, IWL_GRP_LEGACY, IWL_CMD_INIT_COMPLETE);

    IwlPutLe32(Init, IWL_INIT_PHY);
    Status = IwlSendCommand(Adapter, IWL_GRP_SYSTEM, IWL_CMD_INIT_EXTENDED_CFG, Init, sizeof(Init), NULL, 0, NULL);
    if (!NT_SUCCESS(Status))
        return Status;

    RtlZeroMemory(Phy, sizeof(Phy));
    IwlPutLe32(Phy + 0, Adapter->Fw.PhyConfig);
    IwlPutLe32(Phy + 4, Adapter->Fw.CalibFlow);
    IwlPutLe32(Phy + 8, Adapter->Fw.CalibEvent);
    Status = IwlSendCommand(Adapter, IWL_GRP_LEGACY, IWL_CMD_PHY_CONFIGURATION, Phy, sizeof(Phy), NULL, 0, NULL);
    if (!NT_SUCCESS(Status))
        return Status;

    Status = IwlWaitNotification(Adapter, IWL_INIT_TIMEOUT_MS);
    if (!NT_SUCCESS(Status))
        DPRINT1("IWLBE: no INIT_COMPLETE 0x%08lx\n", Status);
    return Status;
}

static
VOID
IwlUpdateChannels(
    _In_ PIWL_ADAPTER Adapter,
    _In_reads_bytes_(Length) const UCHAR *Response,
    _In_ ULONG Length)
{
    ULONG Count, i, Flags, Channel;

    Adapter->Channels2GCount = 0;
    Adapter->Channels5GCount = 0;

    if (Length >= IWL_MCC_RESP_HEADER)
    {
        Count = IwlLe32(Response + IWL_MCC_RESP_N_CHANNELS);
        Count = min(Count, (Length - IWL_MCC_RESP_HEADER) / 4);
        for (i = 0; i < Count; i++)
        {
            Flags = IwlLe32(Response + IWL_MCC_RESP_HEADER + 4 * i);
            if (!(Flags & IWL_NVM_CHANNEL_VALID))
                continue;
            if (i < 14)
            {
                if (i < 13 && Adapter->Channels2GCount < RTL_NUMBER_OF(Adapter->Channels2G))
                    Adapter->Channels2G[Adapter->Channels2GCount++] = (UCHAR)(i + 1);
                continue;
            }
            if (i < 14 + 28)
                Channel = 36 + 4 * (i - 14);
            else if (i < 14 + 37)
                Channel = 149 + 4 * (i - 14 - 28);
            else
                break;
            if (Channel <= 165 && Adapter->Channels5GCount < RTL_NUMBER_OF(Adapter->Channels5G))
                Adapter->Channels5G[Adapter->Channels5GCount++] = (UCHAR)Channel;
        }
    }

    if (!Adapter->Channels2GCount)
    {
        for (i = 0; i < 11; i++)
            Adapter->Channels2G[i] = (UCHAR)(i + 1);
        Adapter->Channels2GCount = 11;
    }
    if (!Adapter->Channels5GCount)
    {
        RtlCopyMemory(Adapter->Channels5G, IwlDefaultChannels5G, sizeof(IwlDefaultChannels5G));
        Adapter->Channels5GCount = sizeof(IwlDefaultChannels5G);
    }
    DPRINT1("IWLBE: %lu 2.4 GHz and %lu 5 GHz channels allowed\n", Adapter->Channels2GCount, Adapter->Channels5GCount);
}

static
NTSTATUS
IwlConfigureFirmware(
    _In_ PIWL_ADAPTER Adapter)
{
    UCHAR Buffer[IWL_SCAN_CONFIG_SIZE], Mcc[IWL_MCC_CMD_SIZE];
    PUCHAR Response;
    ULONG Returned = 0;
    NTSTATUS Status;

    IwlPutLe32(Buffer, Adapter->ValidTxChains);
    Status = IwlSendCommand(Adapter, IWL_GRP_LEGACY, IWL_CMD_TX_ANT_CONFIGURATION, Buffer, 4, NULL, 0, NULL);
    if (!NT_SUCCESS(Status))
        return Status;

    IwlPutLe32(Buffer, IWL_BT_COEX_NW);
    IwlPutLe32(Buffer + 4, IWL_BT_COEX_MODULES);
    Status = IwlSendCommand(Adapter, IWL_GRP_LEGACY, IWL_CMD_BT_CONFIG, Buffer, 8, NULL, 0, NULL);
    if (!NT_SUCCESS(Status))
        return Status;

    IwlPutLe32(Buffer, IWL_SOC_CONFIG_DISCRETE | IWL_SOC_CONFIG_LOW_LATENCY);
    IwlPutLe32(Buffer + 4, IWL_SOC_LATENCY_US);
    Status = IwlSendCommand(Adapter, IWL_GRP_SYSTEM, IWL_CMD_SOC_CONFIGURATION, Buffer, 8, NULL, 0, NULL);
    if (!NT_SUCCESS(Status))
        return Status;

    RtlZeroMemory(Buffer, sizeof(Buffer));
    IwlPutLe32(Buffer + 4, Adapter->ValidTxChains);
    IwlPutLe32(Buffer + 8, Adapter->ValidRxChains);
    Status = IwlSendCommand(Adapter, IWL_GRP_LONG, IWL_CMD_SCAN_CFG, Buffer, IWL_SCAN_CONFIG_SIZE, NULL, 0, NULL);
    if (!NT_SUCCESS(Status))
        return Status;

    RtlZeroMemory(Buffer, sizeof(Buffer));
    Status = IwlSendCommand(Adapter, IWL_GRP_LEGACY, IWL_CMD_POWER_TABLE, Buffer, IWL_POWER_CMD_SIZE, NULL, 0, NULL);
    if (!NT_SUCCESS(Status))
        return Status;

    Response = IwlAllocate(IWL_RESP_MAX);
    if (!Response)
        return STATUS_INSUFFICIENT_RESOURCES;

    RtlZeroMemory(Mcc, sizeof(Mcc));
    IwlPutLe16(Mcc, IWL_MCC_WORLD);
    Status = IwlSendCommand(Adapter, IWL_GRP_LEGACY, IWL_CMD_MCC_UPDATE, Mcc, sizeof(Mcc),
                            Response, IWL_RESP_MAX, &Returned);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("IWLBE: regulatory update failed 0x%08lx, using default channels\n", Status);
        Returned = 0;
    }
    IwlUpdateChannels(Adapter, Response, Returned);
    IwlFree(Response);
    return Adapter->FirmwareDead ? STATUS_DEVICE_HARDWARE_ERROR : STATUS_SUCCESS;
}

NTSTATUS
IwlBootFirmware(
    _In_ PIWL_ADAPTER Adapter)
{
    NTSTATUS Status;

    Status = IwlWaitAlive(Adapter);
    if (NT_SUCCESS(Status))
        Status = IwlLoadPnvm(Adapter);
    if (NT_SUCCESS(Status))
        Status = IwlRunInitSequence(Adapter);
    if (NT_SUCCESS(Status))
        Status = IwlConfigureFirmware(Adapter);
    if (NT_SUCCESS(Status))
        Status = IwlVifInit(Adapter);

    if (!NT_SUCCESS(Status))
    {
        DPRINT1("IWLBE: firmware bring-up failed 0x%08lx\n", Status);
        return Status;
    }

    DPRINT1("IWLBE: firmware running\n");
    return STATUS_SUCCESS;
}
