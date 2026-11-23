/*
 * PROJECT:     LiberNT audio driver libraries
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Summarises an NHLT table, walks the formats and devices of its endpoints and finds the memory resource of an NHLT _DSM reply, without I/O
 * COPYRIGHT:   Copyright 2026 LiberNT contributors
 */

#include <ntddk.h>
#include "nhlt_parse.h"

/* Byte offsets of acpi_table_header, acpi_table_nhlt, acpi_nhlt_endpoint and the parts that follow it, as laid out in ACPICA's actbl.h and actbl2.h. */
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
#define NHLT_ENDPOINT_DEVICE_CONFIG_OFFSET (NHLT_ENDPOINT_FIXED_SIZE + NHLT_CONFIG_SIZE_FIELD)
#define NHLT_WAVE_FORMATEXT_SIZE        40
#define NHLT_WAVE_TAG_OFFSET            0
#define NHLT_WAVE_CHANNELS_OFFSET       2
#define NHLT_WAVE_RATE_OFFSET           4
#define NHLT_WAVE_BYTES_PER_SEC_OFFSET  8
#define NHLT_WAVE_BLOCK_ALIGN_OFFSET    12
#define NHLT_WAVE_BITS_OFFSET           14
#define NHLT_WAVE_EXTRA_SIZE_OFFSET     16
#define NHLT_WAVE_VALID_BITS_OFFSET     18
#define NHLT_WAVE_CHANNEL_MASK_OFFSET   20
#define NHLT_WAVE_SUBFORMAT_OFFSET      24
#define NHLT_DEVICE_INFO_SIZE           18
#define NHLT_DEVICE_INFO_INSTANCE_OFFSET 16
#define NHLT_DEVICE_INFO_PORT_OFFSET    17

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
    const NHLT_ENDPOINT_VIEW* View,
    NHLT_SUMMARY* Summary,
    UINT8 Index)
{
    const UINT8* Endpoint = View->Endpoint;
    UINT8 LinkType = Endpoint[NHLT_ENDPOINT_LINKTYPE_OFFSET];
    NHLT_ENDPOINT_SUMMARY* Entry;

    if (LinkType < NHLT_LINK_TYPE_COUNT)
        Summary->LinkCounts[LinkType]++;
    else
        Summary->OtherLinkCount++;
    if (LinkType != NHLT_LINKTYPE_HDA)
        Summary->NonHdaEndpoints++;
    Summary->FormatsCount = (UINT16)(Summary->FormatsCount + View->FormatsCount);

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
    Entry->DeviceConfigSize = (UINT32)View->DeviceConfigSize;
    Entry->FormatsCount = View->FormatsCount;
    Entry->DevicesCount = View->DevicesCount;
}

/* Checks that the device config, the formats and the devices of one endpoint record fit in its own length */
static
NHLT_PARSE_STATUS
NhltViewEndpoint(
    _In_reads_bytes_(Length) const UINT8* Endpoint,
    SIZE_T Length,
    _Out_ NHLT_ENDPOINT_VIEW* View)
{
    static const NHLT_ENDPOINT_VIEW Empty;
    SIZE_T Offset, Size;
    UINT32 Count, Index;

    *View = Empty;
    View->Endpoint = Endpoint;
    View->EndpointLength = Length;
    if (Length < NHLT_ENDPOINT_DEVICE_CONFIG_OFFSET)
        return NhltParseTruncated;

    Size = NhltReadU32(Endpoint + NHLT_ENDPOINT_FIXED_SIZE);
    if (Size > Length - NHLT_ENDPOINT_DEVICE_CONFIG_OFFSET)
        return NhltParseTruncated;
    View->DeviceConfig = Endpoint + NHLT_ENDPOINT_DEVICE_CONFIG_OFFSET;
    View->DeviceConfigSize = Size;

    Offset = NHLT_ENDPOINT_DEVICE_CONFIG_OFFSET + Size;
    if (Offset >= Length)
        return NhltParseTruncated;
    Count = Endpoint[Offset];
    View->FormatsCount = (UINT8)Count;
    Offset++;
    for (Index = 0; Index < Count; Index++)
    {
        if (Length - Offset < NHLT_WAVE_FORMATEXT_SIZE + NHLT_CONFIG_SIZE_FIELD)
            return NhltParseTruncated;
        Size = NhltReadU32(Endpoint + Offset + NHLT_WAVE_FORMATEXT_SIZE);
        if (Size > Length - Offset - NHLT_WAVE_FORMATEXT_SIZE - NHLT_CONFIG_SIZE_FIELD)
            return NhltParseTruncated;
        Offset += NHLT_WAVE_FORMATEXT_SIZE + NHLT_CONFIG_SIZE_FIELD + Size;
    }

    if (Offset < Length)
    {
        Count = Endpoint[Offset];
        View->DevicesCount = (UINT8)Count;
        View->HasDevicesInfo = TRUE;
        Offset++;
        if (Count > (Length - Offset) / NHLT_DEVICE_INFO_SIZE)
            return NhltParseTruncated;
    }
    return NhltParseOk;
}

static
NHLT_PARSE_STATUS
NhltCheckTable(
    _In_reads_bytes_opt_(Size) const UINT8* Table,
    UINT64 Size,
    _Out_ UINT64* Length)
{
    if (Table == NULL || Size == 0)
        return NhltParseNoTable;
    if (Size < NHLT_TABLE_FIXED_SIZE)
        return NhltParseTruncated;
    if (Table[0] != 'N' || Table[1] != 'H' || Table[2] != 'L' || Table[3] != 'T')
        return NhltParseBadSignature;

    *Length = NhltReadU32(Table + ACPI_HEADER_LENGTH_OFFSET);
    if (*Length < NHLT_TABLE_FIXED_SIZE || *Length > Size)
        return NhltParseBadLength;
    return NhltParseOk;
}

/* Steps to the record of endpoint Index; the record is at least the fixed part and lies inside the table */
static
NHLT_PARSE_STATUS
NhltLocateEndpoint(
    _In_reads_bytes_(Size) const UINT8* Table,
    UINT64 Size,
    UINT8 Index,
    _Out_ const UINT8** Endpoint,
    _Out_ UINT32* EndpointLength)
{
    NHLT_PARSE_STATUS Status;
    UINT64 Length, Offset = NHLT_TABLE_FIXED_SIZE;
    UINT8 Current;

    Status = NhltCheckTable(Table, Size, &Length);
    if (Status != NhltParseOk)
        return Status;
    if (Index >= Table[NHLT_ENDPOINTS_COUNT_OFFSET])
        return NhltParseNotFound;

    for (Current = 0; ; Current++)
    {
        if (Length - Offset < NHLT_CONFIG_SIZE_FIELD)
            return NhltParseTruncated;
        *EndpointLength = NhltReadU32(Table + Offset);
        if (*EndpointLength < NHLT_ENDPOINT_FIXED_SIZE || *EndpointLength > Length - Offset)
            return NhltParseTruncated;
        if (Current == Index)
            break;
        Offset += *EndpointLength;
    }
    *Endpoint = Table + Offset;
    return NhltParseOk;
}

NHLT_PARSE_STATUS
NhltParseTable(
    const UINT8* Table,
    UINT64 Size,
    NHLT_SUMMARY* Summary)
{
    static const NHLT_SUMMARY EmptySummary;
    NHLT_PARSE_STATUS Status;
    NHLT_ENDPOINT_VIEW View;
    UINT64 Length, Offset, Remaining;
    UINT32 EndpointLength;
    UINT8 Count, Index;

    *Summary = EmptySummary;
    Status = NhltCheckTable(Table, Size, &Length);
    if (Status != NhltParseOk)
    {
        if (Status == NhltParseBadLength)
            Summary->HeaderLength = (UINT32)NhltReadU32(Table + ACPI_HEADER_LENGTH_OFFSET);
        return Status;
    }
    Summary->HeaderLength = (UINT32)Length;

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
        if (NhltViewEndpoint(Table + Offset, EndpointLength, &View) != NhltParseOk)
        {
            Status = NhltParseTruncated;
            break;
        }

        NhltSummarizeEndpoint(&View, Summary, Index);
        Offset += EndpointLength;
        Summary->EndpointsParsed++;
    }

    Summary->ParsedLength = (UINT32)Offset;
    if (Status == NhltParseOk)
    {
        Remaining = Length - Offset;
        Summary->TrailingBytes = (UINT32)Remaining;
        if (Remaining >= NHLT_CONFIG_SIZE_FIELD)
            Summary->OedConfigSize = NhltReadU32(Table + Offset);
    }
    return Status;
}

NHLT_PARSE_STATUS
NhltEndpointView(
    const UINT8* Table,
    UINT64 Size,
    UINT8 Index,
    NHLT_ENDPOINT_VIEW* View)
{
    static const NHLT_ENDPOINT_VIEW Empty;
    NHLT_PARSE_STATUS Status;
    const UINT8* Endpoint;
    UINT32 EndpointLength;

    *View = Empty;
    Status = NhltLocateEndpoint(Table, Size, Index, &Endpoint, &EndpointLength);
    if (Status != NhltParseOk)
        return Status;
    return NhltViewEndpoint(Endpoint, EndpointLength, View);
}

NHLT_PARSE_STATUS
NhltEndpointFormat(
    const NHLT_ENDPOINT_VIEW* View,
    UINT8 Index,
    NHLT_FORMAT* Format)
{
    static const NHLT_FORMAT Empty;
    const UINT8* Record;
    SIZE_T Offset, Size;
    UINT8 Current;

    *Format = Empty;
    if (View->Endpoint == NULL || View->DeviceConfig == NULL || Index >= View->FormatsCount)
        return NhltParseNotFound;

    Offset = NHLT_ENDPOINT_DEVICE_CONFIG_OFFSET + View->DeviceConfigSize + 1;
    for (Current = 0; ; Current++)
    {
        if (View->EndpointLength - Offset < NHLT_WAVE_FORMATEXT_SIZE + NHLT_CONFIG_SIZE_FIELD)
            return NhltParseTruncated;
        Record = View->Endpoint + Offset;
        Size = NhltReadU32(Record + NHLT_WAVE_FORMATEXT_SIZE);
        if (Size > View->EndpointLength - Offset - NHLT_WAVE_FORMATEXT_SIZE - NHLT_CONFIG_SIZE_FIELD)
            return NhltParseTruncated;
        if (Current == Index)
            break;
        Offset += NHLT_WAVE_FORMATEXT_SIZE + NHLT_CONFIG_SIZE_FIELD + Size;
    }

    Format->FormatTag = NhltReadU16(Record + NHLT_WAVE_TAG_OFFSET);
    Format->ChannelCount = NhltReadU16(Record + NHLT_WAVE_CHANNELS_OFFSET);
    Format->SamplesPerSec = NhltReadU32(Record + NHLT_WAVE_RATE_OFFSET);
    Format->AvgBytesPerSec = NhltReadU32(Record + NHLT_WAVE_BYTES_PER_SEC_OFFSET);
    Format->BlockAlign = NhltReadU16(Record + NHLT_WAVE_BLOCK_ALIGN_OFFSET);
    Format->BitsPerSample = NhltReadU16(Record + NHLT_WAVE_BITS_OFFSET);
    Format->ExtraFormatSize = NhltReadU16(Record + NHLT_WAVE_EXTRA_SIZE_OFFSET);
    Format->ValidBitsPerSample = NhltReadU16(Record + NHLT_WAVE_VALID_BITS_OFFSET);
    Format->ChannelMask = NhltReadU32(Record + NHLT_WAVE_CHANNEL_MASK_OFFSET);
    for (Offset = 0; Offset < NHLT_SUBFORMAT_BYTES; Offset++)
        Format->Subformat[Offset] = Record[NHLT_WAVE_SUBFORMAT_OFFSET + Offset];
    Format->Config = Record + NHLT_WAVE_FORMATEXT_SIZE + NHLT_CONFIG_SIZE_FIELD;
    Format->ConfigSize = Size;
    return NhltParseOk;
}

NHLT_PARSE_STATUS
NhltEndpointDevice(
    const NHLT_ENDPOINT_VIEW* View,
    UINT8 Index,
    NHLT_DEVICE* Device)
{
    static const NHLT_DEVICE Empty;
    SIZE_T Offset, Size;
    const UINT8* Record;
    UINT8 Current;

    *Device = Empty;
    if (View->Endpoint == NULL || !View->HasDevicesInfo || Index >= View->DevicesCount)
        return NhltParseNotFound;

    Offset = NHLT_ENDPOINT_DEVICE_CONFIG_OFFSET + View->DeviceConfigSize + 1;
    for (Current = 0; Current < View->FormatsCount; Current++)
    {
        if (View->EndpointLength - Offset < NHLT_WAVE_FORMATEXT_SIZE + NHLT_CONFIG_SIZE_FIELD)
            return NhltParseTruncated;
        Size = NhltReadU32(View->Endpoint + Offset + NHLT_WAVE_FORMATEXT_SIZE);
        if (Size > View->EndpointLength - Offset - NHLT_WAVE_FORMATEXT_SIZE - NHLT_CONFIG_SIZE_FIELD)
            return NhltParseTruncated;
        Offset += NHLT_WAVE_FORMATEXT_SIZE + NHLT_CONFIG_SIZE_FIELD + Size;
    }
    if (View->EndpointLength - Offset < 1 + (SIZE_T)(Index + 1) * NHLT_DEVICE_INFO_SIZE)
        return NhltParseTruncated;

    Record = View->Endpoint + Offset + 1 + (SIZE_T)Index * NHLT_DEVICE_INFO_SIZE;
    for (Offset = 0; Offset < NHLT_DEVICE_ID_BYTES; Offset++)
        Device->Id[Offset] = Record[Offset];
    Device->InstanceId = Record[NHLT_DEVICE_INFO_INSTANCE_OFFSET];
    Device->PortId = Record[NHLT_DEVICE_INFO_PORT_OFFSET];
    return NhltParseOk;
}

static
NHLT_PARSE_STATUS
NhltWalkEndpoints(
    _In_reads_bytes_(Size) const UINT8* Table,
    UINT64 Size,
    UINT8 LinkType,
    _Out_ UINT32* Mask)
{
    NHLT_PARSE_STATUS Status;
    UINT64 Length, Offset = NHLT_TABLE_FIXED_SIZE;
    UINT8 Count, Index;
    UINT32 EndpointLength;

    *Mask = 0;
    Status = NhltCheckTable(Table, Size, &Length);
    if (Status != NhltParseOk)
        return Status;

    Count = Table[NHLT_ENDPOINTS_COUNT_OFFSET];
    for (Index = 0; Index < Count; Index++)
    {
        if (Length - Offset < NHLT_CONFIG_SIZE_FIELD)
            return NhltParseTruncated;
        EndpointLength = NhltReadU32(Table + Offset);
        if (EndpointLength < NHLT_ENDPOINT_FIXED_SIZE || EndpointLength > Length - Offset)
            return NhltParseTruncated;
        if (Table[Offset + NHLT_ENDPOINT_LINKTYPE_OFFSET] == LinkType)
            *Mask |= 1UL << (Table[Offset + NHLT_ENDPOINT_INSTANCE_OFFSET] & 0x1F);
        Offset += EndpointLength;
    }
    return NhltParseOk;
}

/* The walk keeps the endpoints it stepped over before a malformed one, so a query answers for them as well */
static
BOOLEAN
NhltQueryEndpoints(
    const UINT8* Table,
    UINT64 Size,
    UINT8 LinkType,
    _Out_ UINT32* Mask)
{
    NHLT_PARSE_STATUS Status = NhltWalkEndpoints(Table, Size, LinkType, Mask);

    return Status == NhltParseOk || Status == NhltParseTruncated;
}

BOOLEAN
NhltHasLinkType(
    const UINT8* Table,
    UINT64 Size,
    UINT8 LinkType)
{
    UINT32 Mask = 0;

    if (!NhltQueryEndpoints(Table, Size, LinkType, &Mask))
        return FALSE;
    return Mask != 0;
}

UINT32
NhltSspEndpointMask(
    const UINT8* Table,
    UINT64 Size)
{
    UINT32 Mask = 0;

    if (!NhltQueryEndpoints(Table, Size, NHLT_LINKTYPE_SSP, &Mask))
        return 0;
    return Mask;
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
