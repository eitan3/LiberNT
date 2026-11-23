/*
 * PROJECT:     LiberNT host-native tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Runs the NHLT parser and the audio path decision of hdaudbus over the shared case tables
 * COPYRIGHT:   Copyright 2026 LiberNT contributors
 */

#include <stdio.h>
#include <string.h>

#include "shim.h"

#include <nhlt_parse.h>
#include <policy.h>

#include "dsp_cases.h"

static int Checks;
static int Failures;

#define CHECK(cond) \
    do { \
        ++Checks; \
        if (!(cond)) { \
            fprintf(stderr, "%s:%d: FAIL: %s\n", __FILE__, __LINE__, #cond); \
            ++Failures; \
        } \
    } while (0)

#define CHECK_EQ(a, b) \
    do { \
        unsigned long long _a = (unsigned long long)(a), _b = (unsigned long long)(b); \
        ++Checks; \
        if (_a != _b) { \
            fprintf(stderr, "%s:%d: FAIL: %s == %s: %llx != %llx\n", __FILE__, __LINE__, #a, #b, _a, _b); \
            ++Failures; \
        } \
    } while (0)

#define CHECK_STR(a, b) \
    do { \
        ++Checks; \
        if (strcmp((a), (b)) != 0) { \
            fprintf(stderr, "%s:%d: FAIL: %s == %s: \"%s\" != \"%s\"\n", __FILE__, __LINE__, #a, #b, (a), (b)); \
            ++Failures; \
        } \
    } while (0)

static
void
check_table(const NHLT_TABLE_CASE* Case)
{
    NHLT_SUMMARY Summary;
    unsigned int Link;
    int Status;
    int FailuresBefore = Failures;

    memset(&Summary, 0xAA, sizeof(Summary));
    Status = NhltParseTable(Case->Table, Case->Size, &Summary);
    CHECK_EQ(Status, Case->Status);
    CHECK_EQ(Summary.HeaderLength, Case->HeaderLength);
    CHECK_EQ(Summary.ParsedLength, Case->ParsedLength);
    CHECK_EQ(Summary.OedConfigSize, Case->OedConfigSize);
    CHECK_EQ(Summary.TrailingBytes, Case->TrailingBytes);
    CHECK_EQ(Summary.NonHdaEndpoints, Case->NonHdaEndpoints);
    CHECK_EQ(Summary.EndpointsCount, Case->EndpointsCount);
    CHECK_EQ(Summary.EndpointsParsed, Case->EndpointsParsed);
    CHECK_EQ(Summary.OtherLinkCount, Case->OtherLinkCount);
    for (Link = 0; Link < NHLT_LINK_TYPE_COUNT; Link++)
        CHECK_EQ(Summary.LinkCounts[Link], Case->LinkCounts[Link]);

    if (Status == NhltParseOk)
        CHECK_EQ(Case->Size, Case->HeaderLength);

    if (Failures != FailuresBefore)
        fprintf(stderr, "  in table case \"%s\"\n", Case->Name);
}

static
void
check_endpoint(const NHLT_ENDPOINT_CASE* Case)
{
    const NHLT_TABLE_CASE* Table = &NhltTableCases[Case->TableIndex];
    NHLT_SUMMARY Summary;
    const NHLT_ENDPOINT_SUMMARY* Endpoint;

    if (NhltParseTable(Table->Table, Table->Size, &Summary) != NhltParseOk)
    {
        CHECK(!"the table of an endpoint case did not parse");
        return;
    }
    if (Case->EndpointIndex >= Summary.EndpointsParsed || Case->EndpointIndex >= NHLT_SUMMARY_ENDPOINTS)
    {
        CHECK(Case->EndpointIndex < Summary.EndpointsParsed);
        return;
    }

    Endpoint = &Summary.Endpoints[Case->EndpointIndex];
    CHECK_EQ(Endpoint->Length, Case->Length);
    CHECK_EQ(Endpoint->SubsystemId, Case->SubsystemId);
    CHECK_EQ(Endpoint->VendorId, Case->VendorId);
    CHECK_EQ(Endpoint->DeviceId, Case->DeviceId);
    CHECK_EQ(Endpoint->RevisionId, Case->RevisionId);
    CHECK_EQ(Endpoint->LinkType, Case->LinkType);
    CHECK_EQ(Endpoint->InstanceId, Case->InstanceId);
    CHECK_EQ(Endpoint->DeviceType, Case->DeviceType);
    CHECK_EQ(Endpoint->Direction, Case->Direction);
    CHECK_EQ(Endpoint->VirtualBusId, Case->VirtualBusId);
}

static
void
check_resource(const NHLT_RESOURCE_CASE* Case)
{
    UINT64 Address = 0x5555555555555555ULL;
    UINT64 Length = 0x5555555555555555ULL;

    CHECK_EQ(NhltFindMemoryAddress64(Case->Template, Case->Size, &Address, &Length), Case->Status);
    CHECK_EQ(Address, Case->Address);
    CHECK_EQ(Length, Case->Length);
}

static
void
check_policy(const POLICY_CASE* Case)
{
    DSP_AUDIO_PATH_EVIDENCE Evidence;
    DSP_AUDIO_PATH Path;

    memset(&Evidence, 0, sizeof(Evidence));
    Evidence.CodecMask = Case->CodecMask;
    Evidence.NonHdaEndpoints = Case->NonHdaEndpoints;
    Evidence.DspPresent = Case->DspPresent;
    Evidence.NhltTableValid = Case->NhltTableValid;

    Path = DspAudioPathDecide(&Evidence);
    CHECK_EQ(Path, Case->Expected);
    CHECK_STR(DspAudioPathName(Path), Case->ExpectedName);
}

int main(void)
{
    unsigned int Index;

    for (Index = 0; Index < sizeof(NhltTableCases) / sizeof(NhltTableCases[0]); Index++)
        check_table(&NhltTableCases[Index]);

    for (Index = 0; Index < sizeof(NhltEndpointCases) / sizeof(NhltEndpointCases[0]); Index++)
        check_endpoint(&NhltEndpointCases[Index]);

    for (Index = 0; Index < sizeof(NhltResourceCases) / sizeof(NhltResourceCases[0]); Index++)
        check_resource(&NhltResourceCases[Index]);

    for (Index = 0; Index < sizeof(PolicyCases) / sizeof(PolicyCases[0]); Index++)
        check_policy(&PolicyCases[Index]);

    CHECK_STR(NhltParseStatusName(NhltParseOk), "ok");
    CHECK_STR(NhltParseStatusName(NhltParseNoTable), "no table");
    CHECK_STR(NhltParseStatusName(NhltParseBadSignature), "bad signature");
    CHECK_STR(NhltParseStatusName(NhltParseBadLength), "bad length");
    CHECK_STR(NhltParseStatusName(NhltParseTruncated), "truncated");
    CHECK_STR(NhltParseStatusName(NhltParseNotFound), "not found");
    CHECK_STR(NhltParseStatusName((NHLT_PARSE_STATUS)0x7f), "unknown");
    CHECK_STR(NhltLinkTypeName(NHLT_LINKTYPE_HDA), "HDA");
    CHECK_STR(NhltLinkTypeName(NHLT_LINKTYPE_SDW), "SDW");
    CHECK_STR(NhltLinkTypeName(0xff), "UNKNOWN");
    CHECK_EQ(DspAudioPathNone, 0);
    CHECK_STR(DspAudioPathName(DspAudioPathNone), "None");
    CHECK_STR(DspAudioPathName(DspAudioPathLegacyHdaOnly), "LegacyHdaOnly");
    CHECK_STR(DspAudioPathName(DspAudioPathDspOptional), "DspOptional");
    CHECK_STR(DspAudioPathName(DspAudioPathDspRequired), "DspRequired");
    CHECK_STR(DspAudioPathName((DSP_AUDIO_PATH)4), "Unknown");

    printf("dsppath: %d checks, %d failures\n", Checks, Failures);
    return Failures ? 1 : 0;
}
