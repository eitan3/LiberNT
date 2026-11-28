/*
 * PROJECT:     LiberNT Intel BE200 Native 802.11 Miniport
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Firmware image and PNVM parsing
 * COPYRIGHT:   Copyright 2026 The LiberNT contributors
 */

#include "iwlbe.h"

static
ULONG
IwlReadLe32(
    _In_reads_bytes_(4) const UCHAR *Data)
{
    return Data[0] | ((ULONG)Data[1] << 8) | ((ULONG)Data[2] << 16) | ((ULONG)Data[3] << 24);
}

NTSTATUS
IwlParseFirmware(
    _Inout_ PIWL_FIRMWARE Fw)
{
    const UCHAR *Data = Fw->File;
    ULONG Offset = IWL_UCODE_TLV_START, Type, Length, Index;

    if (!Data || Fw->FileLength < IWL_UCODE_TLV_START ||
        IwlReadLe32(Data) != 0 || IwlReadLe32(Data + 4) != IWL_UCODE_MAGIC)
        return STATUS_INVALID_IMAGE_FORMAT;

    Fw->Version = IwlReadLe32(Data + 72);
    Fw->SectionCount = 0;
    Fw->Iml = NULL;
    Fw->Pnvm = NULL;

    while (Fw->FileLength - Offset >= 8)
    {
        Type = IwlReadLe32(Data + Offset);
        Length = IwlReadLe32(Data + Offset + 4);
        Offset += 8;
        if (Length > Fw->FileLength - Offset)
            return STATUS_INVALID_IMAGE_FORMAT;

        switch (Type)
        {
            case IWL_TLV_SEC_RT:
                if (Length < 4)
                    break;
                if (Fw->SectionCount >= RTL_NUMBER_OF(Fw->Sections))
                    return STATUS_INVALID_IMAGE_FORMAT;
                Fw->Sections[Fw->SectionCount].Offset = IwlReadLe32(Data + Offset);
                Fw->Sections[Fw->SectionCount].Data = Data + Offset + 4;
                Fw->Sections[Fw->SectionCount].Length = Length - 4;
                Fw->SectionCount++;
                break;

            case IWL_TLV_IML:
                Fw->Iml = Data + Offset;
                Fw->ImlLength = Length;
                break;

            case IWL_TLV_PNVM_DATA:
                Fw->Pnvm = Data + Offset;
                Fw->PnvmLength = Length;
                break;

            case IWL_TLV_PHY_SKU:
                if (Length >= 4)
                    Fw->PhyConfig = IwlReadLe32(Data + Offset);
                break;

            case IWL_TLV_DEF_CALIB:
                if (Length >= 12 && IwlReadLe32(Data + Offset) == 0)
                {
                    Fw->CalibFlow = IwlReadLe32(Data + Offset + 4);
                    Fw->CalibEvent = IwlReadLe32(Data + Offset + 8);
                }
                break;

            case IWL_TLV_ENABLED_CAPABILITIES:
                if (Length >= 8)
                {
                    Index = IwlReadLe32(Data + Offset);
                    if (Index < RTL_NUMBER_OF(Fw->Capabilities))
                        Fw->Capabilities[Index] = IwlReadLe32(Data + Offset + 4);
                }
                break;

            default:
                break;
        }

        Offset += (Length + 3) & ~3UL;
        if (Offset > Fw->FileLength)
            break;
    }

    if (!Fw->SectionCount || !Fw->Iml)
        return STATUS_INVALID_IMAGE_FORMAT;
    return STATUS_SUCCESS;
}

static
ULONG
IwlParsePnvmSection(
    _In_reads_bytes_(Length) const UCHAR *Data,
    _In_ ULONG Length,
    _In_ ULONG MacType,
    _In_ ULONG RfType,
    _Out_writes_(IWL_PNVM_MAX_CHUNKS) PIWL_SECTION Chunks)
{
    ULONG Offset = 0, Type, TlvLength, Count = 0, Size;
    BOOLEAN Match = FALSE;

    while (Length - Offset >= 8)
    {
        Type = IwlReadLe32(Data + Offset);
        TlvLength = IwlReadLe32(Data + Offset + 4);
        Offset += 8;
        if (TlvLength > Length - Offset)
            return 0;

        if (Type == IWL_TLV_PNVM_SKU)
            break;

        switch (Type)
        {
            case IWL_TLV_HW_TYPE:
                if (TlvLength >= 4 && !Match &&
                    (IwlReadLe32(Data + Offset) & 0xFFFF) == MacType &&
                    (IwlReadLe32(Data + Offset) >> 16) == RfType)
                {
                    Match = TRUE;
                }
                break;

            case IWL_TLV_SEC_RT:
                if (TlvLength < 4 || IwlReadLe32(Data + Offset) == IWL_PNVM_SKIP_SECTION)
                    break;
                if (Count >= IWL_PNVM_MAX_CHUNKS)
                    return 0;
                Chunks[Count].Offset = IwlReadLe32(Data + Offset);
                Chunks[Count].Data = Data + Offset + 4;
                Chunks[Count].Length = TlvLength - 4;
                Count++;
                break;

            case IWL_TLV_MEM_DESC:
                if (TlvLength < 8)
                    return 0;
                Size = IwlReadLe32(Data + Offset + 4);
                if (Size != TlvLength - 8)
                    return 0;
                if (Count >= IWL_PNVM_MAX_CHUNKS)
                    return 0;
                Chunks[Count].Offset = IwlReadLe32(Data + Offset);
                Chunks[Count].Data = Data + Offset + 8;
                Chunks[Count].Length = Size;
                Count++;
                break;

            default:
                break;
        }

        Offset += (TlvLength + 3) & ~3UL;
        if (Offset > Length)
            break;
    }

    return Match ? Count : 0;
}

ULONG
IwlParsePnvm(
    _In_reads_bytes_(Length) const UCHAR *Data,
    _In_ ULONG Length,
    _In_reads_(3) const ULONG *SkuId,
    _In_ ULONG MacType,
    _In_ ULONG RfType,
    _Out_writes_(IWL_PNVM_MAX_CHUNKS) PIWL_SECTION Chunks)
{
    ULONG Offset = 0, Type, TlvLength, Count;

    while (Data && Length - Offset >= 8)
    {
        Type = IwlReadLe32(Data + Offset);
        TlvLength = IwlReadLe32(Data + Offset + 4);
        Offset += 8;
        if (TlvLength > Length - Offset)
            return 0;

        if (Type == IWL_TLV_PNVM_SKU && TlvLength >= 12 &&
            IwlReadLe32(Data + Offset) == SkuId[0] &&
            IwlReadLe32(Data + Offset + 4) == SkuId[1] &&
            IwlReadLe32(Data + Offset + 8) == SkuId[2])
        {
            Offset += (TlvLength + 3) & ~3UL;
            if (Offset > Length)
                return 0;
            Count = IwlParsePnvmSection(Data + Offset, Length - Offset, MacType, RfType, Chunks);
            if (Count)
                return Count;
            continue;
        }

        Offset += (TlvLength + 3) & ~3UL;
        if (Offset > Length)
            break;
    }

    return 0;
}
