/*
 * PROJECT:     LiberNT HD Audio Bus Driver
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Reads the vendor and model strings of a raw SMBIOS table for the audio path policy, without I/O
 * COPYRIGHT:   Copyright 2026 LiberNT contributors
 */

#include <ntddk.h>
#include "policy.h"

/* DMTF DSP0134: structure header (type, length, handle), then the formatted area and its string set of index-based NUL-terminated strings */
#define DMI_TYPE_BIOS             0
#define DMI_TYPE_SYSTEM           1
#define DMI_TYPE_BASEBOARD        2
#define DMI_TYPE_END_OF_TABLE     127
#define DMI_HEADER_BYTES          4

/* MSSmBios_RawSMBiosTables: calling method, versions, Size, then Size bytes of structure table */
#define DMI_RAW_SIZE_OFFSET       4
#define DMI_RAW_DATA_OFFSET       8

typedef struct _DMI_FIELD
{
    UINT8 Type;
    UINT8 Offset;
    UINT32 Field;
} DMI_FIELD;

/* String indexes inside the formatted areas of the BIOS Information, System Information and Base Board structures (DSP0134 sections 7.1 to 7.3) */
static const DMI_FIELD DmiFields[] =
{
    { DMI_TYPE_BIOS, 0x05, DSP_DMI_BIOS_VERSION },
    { DMI_TYPE_SYSTEM, 0x04, DSP_DMI_SYS_VENDOR },
    { DMI_TYPE_SYSTEM, 0x05, DSP_DMI_SYS_PRODUCT },
    { DMI_TYPE_SYSTEM, 0x06, DSP_DMI_SYS_VERSION },
    { DMI_TYPE_SYSTEM, 0x19, DSP_DMI_SYS_SKU },
    { DMI_TYPE_BASEBOARD, 0x04, DSP_DMI_BOARD_VENDOR },
    { DMI_TYPE_BASEBOARD, 0x05, DSP_DMI_BOARD_NAME },
    { DMI_TYPE_BASEBOARD, 0x06, DSP_DMI_BOARD_VERSION }
};

static
UINT32
DmiReadU32(
    const UINT8* Data)
{
    return (UINT32)Data[0] | ((UINT32)Data[1] << 8) | ((UINT32)Data[2] << 16) |
        ((UINT32)Data[3] << 24);
}

static
SIZE_T
DmiTextLength(
    _In_reads_bytes_(Limit) const UINT8* Text,
    SIZE_T Limit)
{
    SIZE_T Length = 0;

    while (Length < Limit && Text[Length] != 0)
        Length++;
    return Length;
}

/* Where the string set of a structure ends: after two NUL bytes, or after one NUL that is the last byte of the table */
static
SIZE_T
DmiStringSetEnd(
    _In_reads_bytes_(Remaining) const UINT8* Structure,
    SIZE_T Remaining,
    SIZE_T FormattedLength)
{
    SIZE_T Position;

    for (Position = FormattedLength; Position < Remaining; Position++)
    {
        if (Structure[Position] != 0)
            continue;
        if (Position + 1 >= Remaining || Structure[Position + 1] == 0)
            return Position + 1 < Remaining ? Position + 2 : Remaining;
    }
    return 0;
}

static
void
DmiStoreString(
    _Inout_ DSP_DMI_STRINGS* Strings,
    UINT32 Field,
    _In_reads_bytes_(Length) const UINT8* Text,
    SIZE_T Length)
{
    SIZE_T Copy = Length < DSP_DMI_STRING_BYTES - 1 ? Length : DSP_DMI_STRING_BYTES - 1;
    SIZE_T Index;

    Strings->Present |= 1UL << Field;
    for (Index = 0; Index < Copy; Index++)
        Strings->Strings[Field][Index] = (CHAR)Text[Index];
    Strings->Strings[Field][Copy] = 0;
    if (Length >= DSP_DMI_STRING_BYTES)
        Strings->Truncated |= 1UL << Field;
}

static
void
DmiReadField(
    _In_reads_bytes_(Remaining) const UINT8* Structure,
    SIZE_T Remaining,
    SIZE_T FormattedLength,
    SIZE_T SetEnd,
    UINT8 Index,
    _Inout_ DSP_DMI_STRINGS* Strings,
    UINT32 Field)
{
    SIZE_T Position = FormattedLength, Length;
    UINT8 Current = 1;

    if (Index == 0 || SetEnd == 0)
        return;
    while (Position < SetEnd)
    {
        Length = DmiTextLength(Structure + Position, SetEnd - Position);
        if (Current == Index)
        {
            DmiStoreString(Strings, Field, Structure + Position, Length);
            return;
        }
        Position += Length + 1;
        Current++;
    }
}

DSP_DMI_STATUS
DmiParseRawSMBiosTables(
    const UINT8* Raw,
    SIZE_T Size,
    DSP_DMI_STRINGS* Strings)
{
    static const DSP_DMI_STRINGS Empty;
    DSP_DMI_STATUS Status = DmiOk;
    const UINT8* Data;
    SIZE_T Remaining, Offset = 0, Declared, FormattedLength, SetEnd;
    UINT32 Field;
    UINT8 Type;

    *Strings = Empty;
    if (Raw == NULL || Size == 0)
        return DmiNoData;
    if (Size < DMI_RAW_DATA_OFFSET)
        return DmiTruncated;

    Strings->SmbiosMajorVersion = Raw[1];
    Strings->SmbiosMinorVersion = Raw[2];
    Strings->DmiRevision = Raw[3];
    Declared = DmiReadU32(Raw + DMI_RAW_SIZE_OFFSET);
    Remaining = Size - DMI_RAW_DATA_OFFSET;
    if (Declared > Remaining)
        Status = DmiTruncated;
    else
        Remaining = Declared;

    Data = Raw + DMI_RAW_DATA_OFFSET;
    while (Remaining - Offset >= DMI_HEADER_BYTES)
    {
        Type = Data[Offset];
        FormattedLength = Data[Offset + 1];
        if (Type == DMI_TYPE_END_OF_TABLE)
            break;
        if (FormattedLength < DMI_HEADER_BYTES)
        {
            Status = DmiBadLength;
            break;
        }
        if (FormattedLength > Remaining - Offset)
        {
            Status = DmiTruncated;
            break;
        }

        SetEnd = DmiStringSetEnd(Data + Offset, Remaining - Offset, FormattedLength);
        for (Field = 0; Field < sizeof(DmiFields) / sizeof(DmiFields[0]); Field++)
        {
            if (DmiFields[Field].Type == Type && DmiFields[Field].Offset < FormattedLength)
            {
                DmiReadField(Data + Offset, Remaining - Offset, FormattedLength, SetEnd,
                    Data[Offset + DmiFields[Field].Offset], Strings, DmiFields[Field].Field);
            }
        }

        if (SetEnd == 0)
        {
            Status = DmiTruncated;
            break;
        }
        Offset += SetEnd;
    }

    if (Status == DmiOk && Remaining > Offset && Remaining - Offset < DMI_HEADER_BYTES)
        Status = DmiTruncated;
    return Status;
}
