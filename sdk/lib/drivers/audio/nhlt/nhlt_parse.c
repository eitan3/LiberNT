/*
 * PROJECT:     LiberNT audio driver libraries
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Summarises an NHLT table and finds the memory resource of an NHLT _DSM reply, without I/O
 * COPYRIGHT:   Copyright 2026 LiberNT contributors
 */

#include <ntddk.h>
#include "nhlt_parse.h"

/* Byte offsets of acpi_table_header, acpi_table_nhlt and acpi_nhlt_endpoint as laid out in ACPICA's actbl.h and actbl2.h. */
#define ACPI_HEADER_LENGTH_OFFSET       4
#define NHLT_ENDPOINTS_COUNT_OFFSET     36
#define NHLT_TABLE_FIXED_SIZE           37
#define NHLT_ENDPOINT_FIXED_SIZE        19
#define NHLT_ENDPOINT_LINKTYPE_OFFSET   4
#define NHLT_ENDPOINT_INSTANCE_OFFSET   5
#define NHLT_ENDPOINT_VENDOR_OFFSET     6
#define NHLT_ENDPOINT_DEVICE_OFFSET     8
#define NHLT_ENDPOINT_REVISION_OFFSET   10
#define NHLT_ENDPOINT_SUBSYSTEM_OFFSET  12
#define NHLT_ENDPOINT_DEVICETYPE_OFFSET 16
#define NHLT_ENDPOINT_DIRECTION_OFFSET  17
#define NHLT_ENDPOINT_BUSID_OFFSET      18
#define NHLT_CONFIG_SIZE_FIELD          4

#define AML_RESOURCE_LARGE              0x80
#define AML_RESOURCE_SMALL_LENGTH_MASK  0x07
#define AML_RESOURCE_END_TAG            0x78
#define AML_RESOURCE_ADDRESS64          0x8A
#define AML_ADDRESS64_SIZE              46
#define AML_ADDRESS64_MINIMUM_OFFSET    14
#define AML_ADDRESS64_LENGTH_OFFSET     38

static
UINT16
NhltReadU16(
    const UINT8* Data)
{
    return (UINT16)(Data[0] | ((UINT16)Data[1] << 8));
}

static
UINT32
NhltReadU32(
    const UINT8* Data)
{
    return (UINT32)Data[0] | ((UINT32)Data[1] << 8) | ((UINT32)Data[2] << 16) |
        ((UINT32)Data[3] << 24);
}

static
UINT64
NhltReadU64(
    const UINT8* Data)
{
    return (UINT64)NhltReadU32(Data) | ((UINT64)NhltReadU32(Data + 4) << 32);
}

static
void
NhltSummarizeEndpoint(
    const UINT8* Endpoint,
    NHLT_SUMMARY* Summary,
    UINT8 Index)
{
    UINT8 LinkType = Endpoint[NHLT_ENDPOINT_LINKTYPE_OFFSET];
    NHLT_ENDPOINT_SUMMARY* Entry;

    if (LinkType < NHLT_LINK_TYPE_COUNT)
        Summary->LinkCounts[LinkType]++;
    else
        Summary->OtherLinkCount++;
    if (LinkType != NHLT_LINKTYPE_HDA)
        Summary->NonHdaEndpoints++;

    if (Index >= NHLT_SUMMARY_ENDPOINTS)
        return;

    Entry = &Summary->Endpoints[Index];
    Entry->Length = NhltReadU32(Endpoint);
    Entry->LinkType = LinkType;
    Entry->InstanceId = Endpoint[NHLT_ENDPOINT_INSTANCE_OFFSET];
    Entry->VendorId = NhltReadU16(Endpoint + NHLT_ENDPOINT_VENDOR_OFFSET);
    Entry->DeviceId = NhltReadU16(Endpoint + NHLT_ENDPOINT_DEVICE_OFFSET);
    Entry->RevisionId = NhltReadU16(Endpoint + NHLT_ENDPOINT_REVISION_OFFSET);
    Entry->SubsystemId = NhltReadU32(Endpoint + NHLT_ENDPOINT_SUBSYSTEM_OFFSET);
    Entry->DeviceType = Endpoint[NHLT_ENDPOINT_DEVICETYPE_OFFSET];
    Entry->Direction = Endpoint[NHLT_ENDPOINT_DIRECTION_OFFSET];
    Entry->VirtualBusId = Endpoint[NHLT_ENDPOINT_BUSID_OFFSET];
}

NHLT_PARSE_STATUS
NhltParseTable(
    const UINT8* Table,
    UINT64 Size,
    NHLT_SUMMARY* Summary)
{
    static const NHLT_SUMMARY EmptySummary;
    NHLT_PARSE_STATUS Status;
    UINT64 Length, Offset, Remaining;
    UINT32 EndpointLength;
    UINT8 Count, Index;

    *Summary = EmptySummary;
    if (Table == NULL || Size == 0)
        return NhltParseNoTable;
    if (Size < NHLT_TABLE_FIXED_SIZE)
        return NhltParseTruncated;
    if (Table[0] != 'N' || Table[1] != 'H' || Table[2] != 'L' || Table[3] != 'T')
        return NhltParseBadSignature;

    Length = NhltReadU32(Table + ACPI_HEADER_LENGTH_OFFSET);
    Summary->HeaderLength = (UINT32)Length;
    if (Length < NHLT_TABLE_FIXED_SIZE || Length > Size)
        return NhltParseBadLength;

    Count = Table[NHLT_ENDPOINTS_COUNT_OFFSET];
    Summary->EndpointsCount = Count;
    Offset = NHLT_TABLE_FIXED_SIZE;
    Status = NhltParseOk;

    for (Index = 0; Index < Count; Index++)
    {
        if (Length - Offset < NHLT_CONFIG_SIZE_FIELD)
        {
            Status = NhltParseTruncated;
            break;
        }
        EndpointLength = NhltReadU32(Table + Offset);
        if (EndpointLength < NHLT_ENDPOINT_FIXED_SIZE || EndpointLength > Length - Offset)
        {
            Status = NhltParseTruncated;
            break;
        }

        NhltSummarizeEndpoint(Table + Offset, Summary, Index);
        Offset += EndpointLength;
        Summary->EndpointsParsed++;
    }

    Summary->ParsedLength = (UINT32)Offset;
    Remaining = Length - Offset;
    Summary->TrailingBytes = (UINT32)Remaining;
    if (Remaining >= NHLT_CONFIG_SIZE_FIELD)
        Summary->OedConfigSize = NhltReadU32(Table + Offset);
    return Status;
}

NHLT_PARSE_STATUS
NhltFindMemoryAddress64(
    const UINT8* ResourceTemplate,
    SIZE_T Size,
    UINT64* Address,
    UINT64* Length)
{
    SIZE_T Offset = 0, DescriptorSize;
    UINT8 Type;

    *Address = 0;
    *Length = 0;
    if (ResourceTemplate == NULL || Size == 0)
        return NhltParseNotFound;

    while (Offset < Size)
    {
        Type = ResourceTemplate[Offset];
        if ((Type & AML_RESOURCE_LARGE) == 0)
        {
            if ((Type & ~AML_RESOURCE_SMALL_LENGTH_MASK) == AML_RESOURCE_END_TAG)
                break;
            DescriptorSize = 1 + (Type & AML_RESOURCE_SMALL_LENGTH_MASK);
        }
        else
        {
            if (Size - Offset < 3)
                return NhltParseTruncated;
            DescriptorSize = 3 + NhltReadU16(ResourceTemplate + Offset + 1);
            if (Type == AML_RESOURCE_ADDRESS64 && DescriptorSize < AML_ADDRESS64_SIZE)
                return NhltParseTruncated;
        }
        if (DescriptorSize > Size - Offset)
            return NhltParseTruncated;

        if (Type == AML_RESOURCE_ADDRESS64)
        {
            *Address = NhltReadU64(ResourceTemplate + Offset + AML_ADDRESS64_MINIMUM_OFFSET);
            *Length = NhltReadU64(ResourceTemplate + Offset + AML_ADDRESS64_LENGTH_OFFSET);
            return NhltParseOk;
        }
        Offset += DescriptorSize;
    }
    return NhltParseNotFound;
}

const char* NhltParseStatusName(
    NHLT_PARSE_STATUS Status)
{
    static const char* const Names[] =
    {
        "ok", "no table", "bad signature", "bad length", "truncated", "not found"
    };

    if ((UINT32)Status >= sizeof(Names) / sizeof(Names[0]))
        return "unknown";
    return Names[Status];
}

const char* NhltLinkTypeName(
    UINT8 LinkType)
{
    static const char* const Names[NHLT_LINK_TYPE_COUNT] =
    {
        "HDA", "DSP", "PDM", "SSP", "SLIMBUS", "SDW", "UAOL"
    };

    if (LinkType >= NHLT_LINK_TYPE_COUNT)
        return "UNKNOWN";
    return Names[LinkType];
}
