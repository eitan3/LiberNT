/*
 * PROJECT:     LiberNT kernel-mode tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Synthetic NHLT tables, _DSM replies and audio-path evidence, shared by the kmtests and the host test
 * COPYRIGHT:   Copyright 2026 LiberNT contributors
 */

#ifndef _DSP_CASES_H_
#define _DSP_CASES_H_

#define DSP_U16(Value)   (unsigned char)((Value) & 0xff), (unsigned char)(((Value) >> 8) & 0xff)
#define DSP_U32(Value)   DSP_U16((unsigned long)(Value) & 0xffff), DSP_U16(((unsigned long)(Value) >> 16) & 0xffff)
#define DSP_U64(Value)   DSP_U32((unsigned long long)(Value) & 0xffffffffULL), DSP_U32(((unsigned long long)(Value) >> 32) & 0xffffffffULL)

#define NHLT_HEADER_SIG(S0, S1, S2, S3, Length) \
    S0, S1, S2, S3, DSP_U32(Length), 0x00, 0x00, \
    'O', 'E', 'M', 'I', 'D', '0', 'T', 'B', 'L', 'I', 'D', '0', '0', '0', DSP_U32(1), 'A', 'S', 'L', ' ', DSP_U32(0)
#define NHLT_HEADER(Length) NHLT_HEADER_SIG('N', 'H', 'L', 'T', Length)

#define NHLT_ENDPOINT(Length, Link, Instance, Vendor, Device, Revision, Subsystem, DevType, Direction, Bus) \
    DSP_U32(Length), (Link), (Instance), DSP_U16(Vendor), DSP_U16(Device), DSP_U16(Revision), DSP_U32(Subsystem), \
    (DevType), (Direction), (Bus)

#define QWORD_ADDRESS(Minimum, Length) \
    0x8A, 0x2B, 0x00, 0x00, 0x00, 0x00, DSP_U64(0), DSP_U64(Minimum), DSP_U64(Minimum), DSP_U64(0), DSP_U64(Length)

#define NHLT_SSP_CODEC_ENDPOINT \
    NHLT_ENDPOINT(23, 3, 0, 0x8086, 0xAE34, 0x0100, 0x12345678, 4, 0, 0), DSP_U32(0)
#define NHLT_DMIC_ENDPOINT \
    NHLT_ENDPOINT(19, 2, 0, 0x8086, 0xAE20, 0x0100, 0x12345678, 0, 1, 0)

typedef struct _NHLT_TABLE_CASE
{
    const char* Name;
    const unsigned char* Table;
    unsigned int Size;
    int Status;
    unsigned int HeaderLength;
    unsigned int ParsedLength;
    unsigned int OedConfigSize;
    unsigned int TrailingBytes;
    unsigned short NonHdaEndpoints;
    unsigned char EndpointsCount;
    unsigned char EndpointsParsed;
    unsigned char LinkCounts[7];
    unsigned char OtherLinkCount;
} NHLT_TABLE_CASE;

typedef struct _NHLT_ENDPOINT_CASE
{
    unsigned int TableIndex;
    unsigned int EndpointIndex;
    unsigned int Length;
    unsigned int SubsystemId;
    unsigned short VendorId;
    unsigned short DeviceId;
    unsigned short RevisionId;
    unsigned char LinkType;
    unsigned char InstanceId;
    unsigned char DeviceType;
    unsigned char Direction;
    unsigned char VirtualBusId;
} NHLT_ENDPOINT_CASE;

typedef struct _NHLT_RESOURCE_CASE
{
    const char* Name;
    const unsigned char* Template;
    unsigned int Size;
    int Status;
    unsigned long long Address;
    unsigned long long Length;
} NHLT_RESOURCE_CASE;

typedef struct _POLICY_CASE
{
    unsigned short CodecMask;
    unsigned short NonHdaEndpoints;
    int DspPresent;
    int NhltTableValid;
    int Expected;
    const char* ExpectedName;
} POLICY_CASE;

static const unsigned char NhltEmptyTable[] =
{
    NHLT_HEADER(37), 0x00
};

static const unsigned char NhltTwoEndpointTable[] =
{
    NHLT_HEADER(85), 0x02,
    NHLT_SSP_CODEC_ENDPOINT,
    NHLT_DMIC_ENDPOINT,
    DSP_U32(2), 0xAA, 0x55
};

static const unsigned char NhltHdaOnlyTable[] =
{
    NHLT_HEADER(56), 0x01,
    NHLT_ENDPOINT(19, 0, 0, 0x8086, 0x280D, 0x0100, 0x12345678, 0, 0, 0)
};

static const unsigned char NhltBadSignatureTable[] =
{
    NHLT_HEADER_SIG('N', 'H', 'L', 'E', 37), 0x00
};

static const unsigned char NhltLengthPastTable[] =
{
    NHLT_HEADER(1000), 0x00
};

static const unsigned char NhltLengthBelowFixedTable[] =
{
    NHLT_HEADER(20), 0x00
};

static const unsigned char NhltSecondEndpointTruncatedTable[] =
{
    NHLT_HEADER(70), 0x02,
    NHLT_ENDPOINT(19, 3, 0, 0x8086, 0xAE34, 0x0100, 0x12345678, 4, 0, 0),
    DSP_U32(30), 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};

static const unsigned char NhltNoCountByteTable[] =
{
    NHLT_HEADER(37)
};

static const unsigned char NhltUnknownLinkTable[] =
{
    NHLT_HEADER(56), 0x01,
    NHLT_ENDPOINT(19, 7, 0, 0x10EC, 0x0287, 0x1000, 0x12345678, 0, 0, 0)
};

static const unsigned char NhltEighteenEndpointTable[] =
{
    NHLT_HEADER(379), 18,
    NHLT_ENDPOINT(19, 3, 0, 0x8086, 0xAE34, 0x0100, 0x12345678, 4, 0, 0),
    NHLT_ENDPOINT(19, 3, 1, 0x8086, 0xAE34, 0x0100, 0x12345678, 4, 0, 0),
    NHLT_ENDPOINT(19, 3, 2, 0x8086, 0xAE34, 0x0100, 0x12345678, 4, 0, 0),
    NHLT_ENDPOINT(19, 3, 3, 0x8086, 0xAE34, 0x0100, 0x12345678, 4, 0, 0),
    NHLT_ENDPOINT(19, 3, 4, 0x8086, 0xAE34, 0x0100, 0x12345678, 4, 0, 0),
    NHLT_ENDPOINT(19, 3, 5, 0x8086, 0xAE34, 0x0100, 0x12345678, 4, 0, 0),
    NHLT_ENDPOINT(19, 3, 6, 0x8086, 0xAE34, 0x0100, 0x12345678, 4, 0, 0),
    NHLT_ENDPOINT(19, 3, 7, 0x8086, 0xAE34, 0x0100, 0x12345678, 4, 0, 0),
    NHLT_ENDPOINT(19, 3, 8, 0x8086, 0xAE34, 0x0100, 0x12345678, 4, 0, 0),
    NHLT_ENDPOINT(19, 2, 0, 0x8086, 0xAE20, 0x0100, 0x12345678, 0, 1, 0),
    NHLT_ENDPOINT(19, 2, 1, 0x8086, 0xAE20, 0x0100, 0x12345678, 0, 1, 0),
    NHLT_ENDPOINT(19, 2, 2, 0x8086, 0xAE20, 0x0100, 0x12345678, 0, 1, 0),
    NHLT_ENDPOINT(19, 2, 3, 0x8086, 0xAE20, 0x0100, 0x12345678, 0, 1, 0),
    NHLT_ENDPOINT(19, 2, 4, 0x8086, 0xAE20, 0x0100, 0x12345678, 0, 1, 0),
    NHLT_ENDPOINT(19, 2, 5, 0x8086, 0xAE20, 0x0100, 0x12345678, 0, 1, 0),
    NHLT_ENDPOINT(19, 5, 0, 0x025D, 0x0714, 0x0100, 0x12345678, 0, 0, 0),
    NHLT_ENDPOINT(19, 5, 1, 0x025D, 0x0714, 0x0100, 0x12345678, 0, 0, 0),
    NHLT_ENDPOINT(19, 1, 0, 0x8086, 0x0AE30, 0x0100, 0x12345678, 0, 0, 0)
};

static const unsigned char NhltEndpointBelowFixedTable[] =
{
    NHLT_HEADER(56), 0x01,
    DSP_U32(10), 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};

static const unsigned char NhltCountPastTable[] =
{
    NHLT_HEADER(56), 0x03,
    NHLT_ENDPOINT(19, 3, 0, 0x8086, 0xAE34, 0x0100, 0x12345678, 4, 0, 0)
};

static const NHLT_TABLE_CASE NhltTableCases[] =
{
    { "empty table", NhltEmptyTable, sizeof(NhltEmptyTable), 0, 37, 37, 0, 0, 0, 0, 0, { 0, 0, 0, 0, 0, 0, 0 }, 0 },
    { "SSP codec and DMIC with OED config", NhltTwoEndpointTable, sizeof(NhltTwoEndpointTable), 0, 85, 79, 2, 6, 2, 2, 2,
        { 0, 0, 1, 1, 0, 0, 0 }, 0 },
    { "HDA link endpoint only", NhltHdaOnlyTable, sizeof(NhltHdaOnlyTable), 0, 56, 56, 0, 0, 0, 1, 1, { 1, 0, 0, 0, 0, 0, 0 }, 0 },
    { "bad signature", NhltBadSignatureTable, sizeof(NhltBadSignatureTable), 2, 0, 0, 0, 0, 0, 0, 0, { 0, 0, 0, 0, 0, 0, 0 }, 0 },
    { "header length past the table", NhltLengthPastTable, sizeof(NhltLengthPastTable), 3, 1000, 0, 0, 0, 0, 0, 0, { 0, 0, 0, 0, 0, 0, 0 }, 0 },
    { "header length below the fixed part", NhltLengthBelowFixedTable, sizeof(NhltLengthBelowFixedTable), 3, 20, 0, 0, 0, 0, 0, 0, { 0, 0, 0, 0, 0, 0, 0 }, 0 },
    { "second endpoint runs past the table", NhltSecondEndpointTruncatedTable, sizeof(NhltSecondEndpointTruncatedTable), 4, 70, 56, 0, 0, 1, 2, 1, { 0, 0, 0, 1, 0, 0, 0 }, 0 },
    { "table without its count byte", NhltNoCountByteTable, sizeof(NhltNoCountByteTable), 4, 0, 0, 0, 0, 0, 0, 0, { 0, 0, 0, 0, 0, 0, 0 }, 0 },
    { "no table", NULL, 0, 1, 0, 0, 0, 0, 0, 0, 0, { 0, 0, 0, 0, 0, 0, 0 }, 0 },
    { "unknown link type", NhltUnknownLinkTable, sizeof(NhltUnknownLinkTable), 0, 56, 56, 0, 0, 1, 1, 1, { 0, 0, 0, 0, 0, 0, 0 }, 1 },
    { "eighteen endpoints, sixteen kept", NhltEighteenEndpointTable, sizeof(NhltEighteenEndpointTable), 0, 379, 379, 0, 0, 18, 18, 18,
        { 0, 1, 6, 9, 0, 2, 0 }, 0 },
    { "endpoint length below the fixed part", NhltEndpointBelowFixedTable, sizeof(NhltEndpointBelowFixedTable), 4, 56, 37, 0, 0, 0, 1, 0, { 0, 0, 0, 0, 0, 0, 0 }, 0 },
    { "count names more endpoints than the table holds", NhltCountPastTable, sizeof(NhltCountPastTable), 4, 56, 56, 0, 0, 1, 3, 1, { 0, 0, 0, 1, 0, 0, 0 }, 0 }
};

static const NHLT_ENDPOINT_CASE NhltEndpointCases[] =
{
    { 1, 0, 23, 0x12345678, 0x8086, 0xAE34, 0x0100, 3, 0, 4, 0, 0 },
    { 1, 1, 19, 0x12345678, 0x8086, 0xAE20, 0x0100, 2, 0, 0, 1, 0 },
    { 9, 0, 19, 0x12345678, 0x10EC, 0x0287, 0x1000, 7, 0, 0, 0, 0 },
    { 10, 16, 19, 0x12345678, 0x025D, 0x0714, 0x0100, 5, 1, 0, 0, 0 },
    { 10, 17, 19, 0x12345678, 0x8086, 0x0AE30, 0x0100, 1, 0, 0, 0, 0 }
};

static const unsigned char NhltAddressReply[] =
{
    QWORD_ADDRESS(0x9F000000, 0x10000), 0x79, 0x00
};

static const unsigned char NhltIrqThenAddressReply[] =
{
    0x22, 0x0B, 0x00,
    QWORD_ADDRESS(0x9F000000, 0x10000), 0x79, 0x00
};

static const unsigned char NhltShortAddressDescriptor[] =
{
    0x8A, 0x20, 0x00, 0x00, 0x00, 0x00,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};

static const unsigned char NhltCutAddressReply[] =
{
    0x8A, 0x2B, 0x00, 0x00, 0x00, 0x00,
    DSP_U64(0), DSP_U64(0x1000)
};

static const unsigned char NhltEndTagOnly[] =
{
    0x79, 0x00
};

static const unsigned char NhltLargeHeaderCut[] =
{
    0x8A, 0x2B
};

static const unsigned char NhltIrqOnly[] =
{
    0x22, 0x0B, 0x00
};

static const NHLT_RESOURCE_CASE NhltResourceCases[] =
{
    { "address descriptor and end tag", NhltAddressReply, sizeof(NhltAddressReply), 0, 0x9F000000ULL, 0x10000ULL },
    { "small descriptor before the address descriptor", NhltIrqThenAddressReply, sizeof(NhltIrqThenAddressReply), 0, 0x9F000000ULL, 0x10000ULL },
    { "address descriptor shorter than its structure", NhltShortAddressDescriptor, sizeof(NhltShortAddressDescriptor), 4, 0, 0 },
    { "address descriptor cut after 22 bytes", NhltCutAddressReply, sizeof(NhltCutAddressReply), 4, 0, 0 },
    { "end tag only", NhltEndTagOnly, sizeof(NhltEndTagOnly), 5, 0, 0 },
    { "no reply", NULL, 0, 5, 0, 0 },
    { "large header cut after two bytes", NhltLargeHeaderCut, sizeof(NhltLargeHeaderCut), 4, 0, 0 },
    { "small descriptor only", NhltIrqOnly, sizeof(NhltIrqOnly), 5, 0, 0 }
};

static const POLICY_CASE PolicyCases[] =
{
    { 0x0000, 0, 0, 0, 0, "None" },
    { 0x0000, 1, 0, 0, 0, "None" },
    { 0x0000, 0, 0, 1, 0, "None" },
    { 0x0000, 1, 0, 1, 0, "None" },
    { 0x0000, 0, 1, 0, 0, "None" },
    { 0x0000, 1, 1, 0, 0, "None" },
    { 0x0000, 0, 1, 1, 0, "None" },
    { 0x0000, 1, 1, 1, 3, "DspRequired" },
    { 0x0002, 0, 0, 0, 1, "LegacyHdaOnly" },
    { 0x0002, 1, 0, 0, 1, "LegacyHdaOnly" },
    { 0x0002, 0, 0, 1, 1, "LegacyHdaOnly" },
    { 0x0002, 1, 0, 1, 1, "LegacyHdaOnly" },
    { 0x0002, 0, 1, 0, 2, "DspOptional" },
    { 0x0002, 1, 1, 0, 2, "DspOptional" },
    { 0x8002, 0, 1, 1, 2, "DspOptional" },
    { 0x8002, 1, 1, 1, 2, "DspOptional" }
};

#endif
