/*
 * PROJECT:     LiberNT audio driver libraries
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Walks the reply of an ACPI _DSD evaluation into its properties and packages, without I/O
 * COPYRIGHT:   Copyright 2026 LiberNT contributors
 */

#include <ntddk.h>
#include "disco.h"

/* ACPI _DSD data extension UUID daffd814-6eba-4d8c-8a91-bc9bbf4aa301 as the GUID is laid out in memory */
static const UINT8 DiscoDsdUuidBytes[DISCO_GUID_BYTES] =
{
    0x14, 0xD8, 0xFF, 0xDA, 0xBA, 0x6E, 0x8C, 0x4D, 0x8A, 0x91, 0xBC, 0x9B, 0xBF, 0x4A, 0xA3, 0x01
};

#define DISCO_REPLY_SIGNATURE   0x41656F42UL
#define DISCO_MIN_ARGUMENT_BYTES (DISCO_ARGUMENT_HEADER_BYTES + DISCO_INTEGER_BYTES)

static
UINT16
DiscoReadU16(
    const UINT8* Data)
{
    return (UINT16)(Data[0] | ((UINT16)Data[1] << 8));
}

static
UINT32
DiscoReadU32(
    const UINT8* Data)
{
    return (UINT32)Data[0] | ((UINT32)Data[1] << 8) | ((UINT32)Data[2] << 16) |
        ((UINT32)Data[3] << 24);
}

static
BOOLEAN
DiscoBytesEqual(
    _In_reads_bytes_(Length) const UINT8* Left,
    _In_reads_bytes_(Length) const UINT8* Right,
    SIZE_T Length)
{
    SIZE_T Index;

    for (Index = 0; Index < Length; Index++)
    {
        if (Left[Index] != Right[Index])
            return FALSE;
    }
    return TRUE;
}

static
SIZE_T
DiscoTextLength(
    _In_z_ const char* Text)
{
    SIZE_T Length = 0;

    while (Text[Length] != 0)
        Length++;
    return Length;
}

/* Reads the argument at Offset; the next argument starts at *NextOffset, which may lie past the end of a short reply */
static
DISCO_STATUS
DiscoReadArgument(
    _In_reads_bytes_(Size) const UINT8* Base,
    SIZE_T Size,
    SIZE_T Offset,
    _Out_ DISCO_ARGUMENT* Argument,
    _Out_ SIZE_T* NextOffset)
{
    if (Offset > Size || Size - Offset < DISCO_ARGUMENT_HEADER_BYTES)
        return DiscoTruncated;

    Argument->Type = DiscoReadU16(Base + Offset);
    Argument->DataLength = DiscoReadU16(Base + Offset + 2);
    if (Argument->DataLength > Size - Offset - DISCO_ARGUMENT_HEADER_BYTES)
        return DiscoTruncated;

    Argument->Data = Base + Offset + DISCO_ARGUMENT_HEADER_BYTES;
    *NextOffset = Offset + (Argument->DataLength < DISCO_INTEGER_BYTES ? DISCO_MIN_ARGUMENT_BYTES :
        DISCO_ARGUMENT_HEADER_BYTES + Argument->DataLength);
    return DiscoOk;
}

/* Counts the complete (name, value) pairs of a property package; a pair cut short ends the count */
static
UINT32
DiscoCountProperties(
    _In_reads_bytes_(Size) const UINT8* Data,
    SIZE_T Size)
{
    DISCO_ARGUMENT Name, Value;
    SIZE_T Offset = 0;
    UINT32 Count = 0;

    while (DiscoReadArgument(Data, Size, Offset, &Name, &Offset) == DiscoOk)
    {
        if (DiscoReadArgument(Data, Size, Offset, &Value, &Offset) != DiscoOk)
            break;
        Count++;
    }
    return Count;
}

const UINT8* DiscoDsdUuid(VOID)
{
    return DiscoDsdUuidBytes;
}

DISCO_STATUS
DiscoDsdBegin(
    const UINT8* Reply,
    SIZE_T Size,
    DISCO_DSD* Dsd)
{
    static const DISCO_DSD Empty;
    DISCO_ARGUMENT Guid, Package;
    DISCO_STATUS Status;
    SIZE_T Offset, Length;
    UINT32 Count, Index;

    *Dsd = Empty;
    if (Reply == NULL || Size == 0)
        return DiscoNoData;
    if (Size < DISCO_REPLY_HEADER_BYTES)
        return DiscoTruncated;
    if (DiscoReadU32(Reply) != DISCO_REPLY_SIGNATURE)
        return DiscoBadSignature;

    Length = DiscoReadU32(Reply + 4);
    if (Length < DISCO_REPLY_HEADER_BYTES)
        return DiscoBadLength;
    if (Length > Size)
        return DiscoTruncated;

    Count = DiscoReadU32(Reply + 8);
    Offset = DISCO_REPLY_HEADER_BYTES;
    for (Index = 0; Index < Count; Index += 2)
    {
        Status = DiscoReadArgument(Reply, Length, Offset, &Guid, &Offset);
        if (Status != DiscoOk)
            return Status;
        if (Index + 1 >= Count)
            break;

        Status = DiscoReadArgument(Reply, Length, Offset, &Package, &Offset);
        if (Status != DiscoOk)
            return Status;
        if (Guid.Type != DISCO_ARGUMENT_BUFFER || Guid.DataLength != DISCO_GUID_BYTES ||
            !DiscoBytesEqual(Guid.Data, DiscoDsdUuidBytes, DISCO_GUID_BYTES) ||
            Package.Type != DISCO_ARGUMENT_PACKAGE)
        {
            continue;
        }

        Dsd->PackageData = Package.Data;
        Dsd->PackageLength = Package.DataLength;
        Dsd->PropertyCount = DiscoCountProperties(Package.Data, Package.DataLength);
        return DiscoOk;
    }
    return DiscoNotFound;
}

DISCO_STATUS
DiscoDsdPropertyAt(
    const DISCO_DSD* Dsd,
    UINT32 Index,
    DISCO_ARGUMENT* Name,
    DISCO_ARGUMENT* Value)
{
    DISCO_ARGUMENT SkipName, SkipValue;
    DISCO_STATUS Status;
    SIZE_T Offset = 0;
    UINT32 Pair;

    if (Dsd->PackageData == NULL)
        return DiscoNoData;
    if (Index >= Dsd->PropertyCount)
        return DiscoNotFound;
    for (Pair = 0; ; Pair++)
    {
        Status = DiscoReadArgument(Dsd->PackageData, Dsd->PackageLength, Offset, Pair == Index ? Name : &SkipName, &Offset);
        if (Status != DiscoOk)
            return DiscoTruncated;
        Status = DiscoReadArgument(Dsd->PackageData, Dsd->PackageLength, Offset, Pair == Index ? Value : &SkipValue, &Offset);
        if (Status != DiscoOk)
            return DiscoTruncated;
        if (Pair == Index)
            return DiscoOk;
    }
}

DISCO_STATUS
DiscoArgumentIsName(
    const DISCO_ARGUMENT* Argument,
    const char* Name)
{
    SIZE_T Length;

    if (Argument->Type != DISCO_ARGUMENT_STRING)
        return DiscoWrongType;

    Length = DiscoTextLength(Name);
    if ((Argument->DataLength != Length && Argument->DataLength != Length + 1) ||
        (Argument->DataLength == Length + 1 && Argument->Data[Length] != 0))
        return DiscoNotFound;
    return DiscoBytesEqual(Argument->Data, (const UINT8*)Name, Length) ? DiscoOk : DiscoNotFound;
}

DISCO_STATUS
DiscoDsdFind(
    const DISCO_DSD* Dsd,
    const char* Name,
    DISCO_ARGUMENT* Value)
{
    DISCO_ARGUMENT Candidate;
    DISCO_STATUS Status;
    UINT32 Index;

    for (Index = 0; Index < Dsd->PropertyCount; Index++)
    {
        Status = DiscoDsdPropertyAt(Dsd, Index, &Candidate, Value);
        if (Status != DiscoOk)
            return Status;
        if (DiscoArgumentIsName(&Candidate, Name) == DiscoOk)
            return DiscoOk;
    }
    return Dsd->PackageData == NULL ? DiscoNoData : DiscoNotFound;
}

UINT32
DiscoPackageCount(
    const DISCO_ARGUMENT* Package)
{
    DISCO_ARGUMENT Element;
    SIZE_T Offset = 0;
    UINT32 Count = 0;

    if (Package->Type != DISCO_ARGUMENT_PACKAGE || Package->Data == NULL)
        return 0;
    while (DiscoReadArgument(Package->Data, Package->DataLength, Offset, &Element, &Offset) == DiscoOk)
        Count++;
    return Count;
}

DISCO_STATUS
DiscoPackageElementAt(
    const DISCO_ARGUMENT* Package,
    UINT32 Index,
    DISCO_ARGUMENT* Element)
{
    DISCO_ARGUMENT Skip;
    SIZE_T Offset = 0;
    UINT32 Current;

    if (Package->Type != DISCO_ARGUMENT_PACKAGE || Package->Data == NULL)
        return DiscoWrongType;
    if (Index >= DiscoPackageCount(Package))
        return DiscoNotFound;
    for (Current = 0; ; Current++)
    {
        if (DiscoReadArgument(Package->Data, Package->DataLength, Offset, Current == Index ? Element : &Skip, &Offset) != DiscoOk)
            return DiscoTruncated;
        if (Current == Index)
            return DiscoOk;
    }
}

DISCO_STATUS
DiscoArgumentU32(
    const DISCO_ARGUMENT* Argument,
    UINT32* Value)
{
    *Value = 0;
    if (Argument->Type != DISCO_ARGUMENT_INTEGER || Argument->Data == NULL)
        return DiscoWrongType;
    if (Argument->DataLength < DISCO_INTEGER_BYTES)
        return DiscoTruncated;
    *Value = DiscoReadU32(Argument->Data);
    return DiscoOk;
}

DISCO_STATUS
DiscoArgumentCopy(
    const DISCO_ARGUMENT* Argument,
    UINT8* Buffer,
    SIZE_T Capacity,
    SIZE_T* Length)
{
    SIZE_T Index, Count;

    if (Length != NULL)
        *Length = Argument->DataLength;
    if (Argument->Data == NULL)
        return DiscoNoData;

    Count = Argument->DataLength < Capacity ? Argument->DataLength : Capacity;
    for (Index = 0; Index < Count; Index++)
        Buffer[Index] = Argument->Data[Index];
    return Argument->DataLength <= Capacity ? DiscoOk : DiscoTruncated;
}

const char* DiscoStatusName(
    DISCO_STATUS Status)
{
    static const char* const Names[] =
    {
        "ok", "no data", "bad signature", "bad length", "truncated", "not found", "wrong type"
    };

    if ((UINT32)Status >= sizeof(Names) / sizeof(Names[0]))
        return "unknown";
    return Names[Status];
}
