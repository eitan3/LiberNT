/*
 * PROJECT:     LiberNT kernel-mode tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     The NHLT parser summarises a table and finds the address of a _DSM reply without reading past either
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

    for (Index = 0; Index < sizeof(NhltTableCases) / sizeof(NhltTableCases[0]); Index++)
    {
        trace("%s\n", NhltTableCases[Index].Name);
        CheckTable(&NhltTableCases[Index]);
    }

    for (Index = 0; Index < sizeof(NhltEndpointCases) / sizeof(NhltEndpointCases[0]); Index++)
    {
        CheckEndpoint(&NhltEndpointCases[Index]);
    }

    for (Index = 0; Index < sizeof(NhltResourceCases) / sizeof(NhltResourceCases[0]); Index++)
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

    /* TODO: add a real machine's NHLT table (acpidump -b -n NHLT, plan Step-0 L5) as a fixture once its vendor blobs are checked */
}
