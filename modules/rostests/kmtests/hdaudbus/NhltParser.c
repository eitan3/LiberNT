/*
 * PROJECT:     LiberNT kernel-mode tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     The NHLT parser summarises a table, walks its endpoints and finds the address of a _DSM reply without reading past any of them
 * COPYRIGHT:   Copyright 2026 LiberNT contributors
 */

#include <kmt_test.h>
#include <nhlt_parse.h>

#define NDEBUG
#include <debug.h>

#include "dsp_cases.h"

static
VOID
CheckTable(
    _In_ const NHLT_TABLE_CASE* Case)
{
    NHLT_SUMMARY Summary;
    INT Status;
    UINT8 Link;

    RtlFillMemory(&Summary, sizeof(Summary), 0xAA);
    Status = NhltParseTable(Case->Table, Case->Size, &Summary);
    ok_eq_int(Status, Case->Status);
    ok_eq_uint(Summary.HeaderLength, Case->HeaderLength);
    ok_eq_uint(Summary.ParsedLength, Case->ParsedLength);
    ok_eq_uint(Summary.OedConfigSize, Case->OedConfigSize);
    ok_eq_uint(Summary.TrailingBytes, Case->TrailingBytes);
    ok_eq_uint(Summary.NonHdaEndpoints, Case->NonHdaEndpoints);
    ok_eq_uint(Summary.FormatsCount, Case->FormatsCount);
    ok_eq_uint(Summary.EndpointsCount, Case->EndpointsCount);
    ok_eq_uint(Summary.EndpointsParsed, Case->EndpointsParsed);
    ok_eq_uint(Summary.OtherLinkCount, Case->OtherLinkCount);
    for (Link = 0; Link < NHLT_LINK_TYPE_COUNT; Link++)
    {
        ok_eq_uint(Summary.LinkCounts[Link], Case->LinkCounts[Link]);
    }

    if (Status == NhltParseOk)
    {
        ok_eq_uint(Case->Size, Case->HeaderLength);
    }
}

static
VOID
CheckEndpoint(
    _In_ const NHLT_ENDPOINT_CASE* Case)
{
    const NHLT_TABLE_CASE* Table = &NhltTableCases[Case->TableIndex];
    NHLT_SUMMARY Summary;
    const NHLT_ENDPOINT_SUMMARY* Endpoint;

    ok_eq_int(NhltParseTable(Table->Table, Table->Size, &Summary), NhltParseOk);
    ok(Case->EndpointIndex < Summary.EndpointsParsed && Case->EndpointIndex < NHLT_SUMMARY_ENDPOINTS,
       "endpoint %u is kept, parsed %u\n", Case->EndpointIndex, Summary.EndpointsParsed);
    if (Case->EndpointIndex >= Summary.EndpointsParsed || Case->EndpointIndex >= NHLT_SUMMARY_ENDPOINTS)
    {
        return;
    }

    Endpoint = &Summary.Endpoints[Case->EndpointIndex];
    ok_eq_uint(Endpoint->Length, Case->Length);
    ok_eq_uint(Endpoint->SubsystemId, Case->SubsystemId);
    ok_eq_uint(Endpoint->VendorId, Case->VendorId);
    ok_eq_uint(Endpoint->DeviceId, Case->DeviceId);
    ok_eq_uint(Endpoint->RevisionId, Case->RevisionId);
    ok_eq_uint(Endpoint->LinkType, Case->LinkType);
    ok_eq_uint(Endpoint->InstanceId, Case->InstanceId);
    ok_eq_uint(Endpoint->DeviceType, Case->DeviceType);
    ok_eq_uint(Endpoint->Direction, Case->Direction);
    ok_eq_uint(Endpoint->VirtualBusId, Case->VirtualBusId);
    ok_eq_uint(Endpoint->DeviceConfigSize, Case->DeviceConfigSize);
    ok_eq_uint(Endpoint->FormatsCount, Case->FormatsCount);
    ok_eq_uint(Endpoint->DevicesCount, Case->DevicesCount);
}

static
VOID
CheckView(
    _In_ const NHLT_VIEW_CASE* Case)
{
    const NHLT_TABLE_CASE* Table = &NhltTableCases[Case->TableIndex];
    NHLT_ENDPOINT_VIEW View;

    RtlFillMemory(&View, sizeof(View), 0xAA);
    ok_eq_int(NhltEndpointView(Table->Table, Table->Size, (UINT8)Case->EndpointIndex, &View), Case->Status);
    ok_eq_uint(View.DeviceConfigSize, Case->DeviceConfigSize);
    ok_eq_uint(View.FormatsCount, Case->FormatsCount);
    ok_eq_uint(View.DevicesCount, Case->DevicesCount);
    ok_eq_uint(View.HasDevicesInfo, Case->HasDevicesInfo);
    ok_eq_uint(View.Endpoint != NULL, Case->Located);
    if (View.Endpoint != NULL)
    {
        ok_eq_uint(View.EndpointLength,
            (UINT32)View.Endpoint[0] | ((UINT32)View.Endpoint[1] << 8) | ((UINT32)View.Endpoint[2] << 16) |
            ((UINT32)View.Endpoint[3] << 24));
    }
}

static
VOID
CheckFormat(
    _In_ const NHLT_FORMAT_CASE* Case)
{
    const NHLT_TABLE_CASE* Table = &NhltTableCases[Case->TableIndex];
    static const UINT8 Pcm[16] =
    {
        0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x10, 0x00, 0x80, 0x00, 0x00, 0xAA, 0x00, 0x38, 0x9B, 0x71
    };
    NHLT_ENDPOINT_VIEW View;
    NHLT_FORMAT Format;
    UINT32 Byte;

    ok(NhltEndpointView(Table->Table, Table->Size, (UINT8)Case->EndpointIndex, &View) != NhltParseNotFound,
       "the endpoint of format case %u/%u was found\n", Case->TableIndex, Case->EndpointIndex);
    RtlFillMemory(&Format, sizeof(Format), 0xAA);
    ok_eq_int(NhltEndpointFormat(&View, (UINT8)Case->FormatIndex, &Format), Case->Status);
    if (Case->Status != NhltParseOk)
    {
        return;
    }

    ok_eq_uint(Format.FormatTag, Case->FormatTag);
    ok_eq_uint(Format.ChannelCount, Case->ChannelCount);
    ok_eq_uint(Format.SamplesPerSec, Case->SamplesPerSec);
    ok_eq_uint(Format.AvgBytesPerSec, Case->AvgBytesPerSec);
    ok_eq_uint(Format.BlockAlign, Case->BlockAlign);
    ok_eq_uint(Format.BitsPerSample, Case->BitsPerSample);
    ok_eq_uint(Format.ExtraFormatSize, Case->ExtraFormatSize);
    ok_eq_uint(Format.ValidBitsPerSample, Case->ValidBitsPerSample);
    ok_eq_uint(Format.ChannelMask, Case->ChannelMask);
    ok_eq_uint(Format.ConfigSize, Case->ConfigSize);
    for (Byte = 0; Byte < NHLT_SUBFORMAT_BYTES; Byte++)
    {
        ok_eq_uint(Format.Subformat[Byte] == Pcm[Byte], Case->IsPcm);
    }
    if (Case->ConfigSize != 0)
    {
        ok_eq_uint(Format.Config[0], Case->ConfigFirst);
    }
}

static
VOID
CheckDevice(
    _In_ const NHLT_DEVICE_CASE* Case)
{
    const NHLT_TABLE_CASE* Table = &NhltTableCases[Case->TableIndex];
    NHLT_ENDPOINT_VIEW View;
    NHLT_DEVICE Device;
    UINT32 Byte;

    ok(NhltEndpointView(Table->Table, Table->Size, (UINT8)Case->EndpointIndex, &View) != NhltParseNotFound,
       "the endpoint of device case %u/%u was found\n", Case->TableIndex, Case->EndpointIndex);
    RtlFillMemory(&Device, sizeof(Device), 0xAA);
    ok_eq_int(NhltEndpointDevice(&View, (UINT8)Case->DeviceIndex, &Device), Case->Status);
    if (Case->Status != NhltParseOk)
    {
        return;
    }

    for (Byte = 0; Byte < 4; Byte++)
    {
        ok_eq_uint(Device.Id[Byte], Case->Id[Byte]);
    }
    for (Byte = 4; Byte < NHLT_DEVICE_ID_BYTES; Byte++)
    {
        ok_eq_uint(Device.Id[Byte], 0);
    }
    ok_eq_uint(Device.InstanceId, Case->InstanceId);
    ok_eq_uint(Device.PortId, Case->PortId);
}

static
VOID
CheckLink(
    _In_ const NHLT_LINK_CASE* Case)
{
    const NHLT_TABLE_CASE* Table = &NhltTableCases[Case->TableIndex];

    ok_eq_uint(NhltHasLinkType(Table->Table, Table->Size, Case->LinkType) ? 1 : 0, Case->Present);
    if (Case->LinkType == NHLT_LINKTYPE_SSP)
    {
        ok_eq_uint(NhltSspEndpointMask(Table->Table, Table->Size), Case->SspMask);
    }
}

static
VOID
CheckResource(
    _In_ const NHLT_RESOURCE_CASE* Case)
{
    UINT64 Address = 0x5555555555555555ULL;
    UINT64 Length = 0x5555555555555555ULL;
    INT Status;

    Status = NhltFindMemoryAddress64(Case->Template, Case->Size, &Address, &Length);
    ok_eq_int(Status, Case->Status);
    ok_eq_ulonglong(Address, Case->Address);
    ok_eq_ulonglong(Length, Case->Length);
}

START_TEST(NhltParser)
{
    ULONG Index;

    for (Index = 0; Index < ARRAYSIZE(NhltTableCases); Index++)
    {
        trace("%s\n", NhltTableCases[Index].Name);
        CheckTable(&NhltTableCases[Index]);
    }

    for (Index = 0; Index < ARRAYSIZE(NhltEndpointCases); Index++)
    {
        CheckEndpoint(&NhltEndpointCases[Index]);
    }

    for (Index = 0; Index < ARRAYSIZE(NhltViewCases); Index++)
    {
        CheckView(&NhltViewCases[Index]);
    }

    for (Index = 0; Index < ARRAYSIZE(NhltFormatCases); Index++)
    {
        CheckFormat(&NhltFormatCases[Index]);
    }

    for (Index = 0; Index < ARRAYSIZE(NhltDeviceCases); Index++)
    {
        CheckDevice(&NhltDeviceCases[Index]);
    }

    for (Index = 0; Index < ARRAYSIZE(NhltLinkCases); Index++)
    {
        CheckLink(&NhltLinkCases[Index]);
    }

    for (Index = 0; Index < ARRAYSIZE(NhltResourceCases); Index++)
    {
        trace("%s\n", NhltResourceCases[Index].Name);
        CheckResource(&NhltResourceCases[Index]);
    }

    ok_eq_str(NhltParseStatusName(NhltParseOk), "ok");
    ok_eq_str(NhltParseStatusName(NhltParseNoTable), "no table");
    ok_eq_str(NhltParseStatusName(NhltParseBadSignature), "bad signature");
    ok_eq_str(NhltParseStatusName(NhltParseBadLength), "bad length");
    ok_eq_str(NhltParseStatusName(NhltParseTruncated), "truncated");
    ok_eq_str(NhltParseStatusName(NhltParseNotFound), "not found");
    ok_eq_str(NhltParseStatusName((NHLT_PARSE_STATUS)0x7f), "unknown");

    ok_eq_str(NhltLinkTypeName(NHLT_LINKTYPE_HDA), "HDA");
    ok_eq_str(NhltLinkTypeName(NHLT_LINKTYPE_DSP), "DSP");
    ok_eq_str(NhltLinkTypeName(NHLT_LINKTYPE_PDM), "PDM");
    ok_eq_str(NhltLinkTypeName(NHLT_LINKTYPE_SSP), "SSP");
    ok_eq_str(NhltLinkTypeName(NHLT_LINKTYPE_SLIMBUS), "SLIMBUS");
    ok_eq_str(NhltLinkTypeName(NHLT_LINKTYPE_SDW), "SDW");
    ok_eq_str(NhltLinkTypeName(NHLT_LINKTYPE_UAOL), "UAOL");
    ok_eq_str(NhltLinkTypeName(0xff), "UNKNOWN");

    /* TODO: add the endpoint table the firmware _DSM of a real machine returns at run time (the static table above is a stub) once checked for vendor blobs */
}
