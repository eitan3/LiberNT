/*
 * PROJECT:     LiberNT audio driver libraries
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Summarises an NHLT table and finds the memory resource of an NHLT _DSM reply, without I/O
 * COPYRIGHT:   Copyright 2026 LiberNT contributors
 */

#ifndef _AUDIO_NHLT_PARSE_H_
#define _AUDIO_NHLT_PARSE_H_

#ifdef __cplusplus
extern "C" {
#endif

#define NHLT_LINK_TYPE_COUNT    7

#define NHLT_LINKTYPE_HDA       0
#define NHLT_LINKTYPE_DSP       1
#define NHLT_LINKTYPE_PDM       2
#define NHLT_LINKTYPE_SSP       3
#define NHLT_LINKTYPE_SLIMBUS   4
#define NHLT_LINKTYPE_SDW       5
#define NHLT_LINKTYPE_UAOL      6

#define NHLT_SUMMARY_ENDPOINTS  16

typedef enum _NHLT_PARSE_STATUS
{
    NhltParseOk = 0,
    NhltParseNoTable,
    NhltParseBadSignature,
    NhltParseBadLength,
    NhltParseTruncated,
    NhltParseNotFound
} NHLT_PARSE_STATUS;

typedef struct _NHLT_ENDPOINT_SUMMARY
{
    UINT32 Length;
    UINT16 VendorId;
    UINT16 DeviceId;
    UINT16 RevisionId;
    UINT32 SubsystemId;
    UINT8 LinkType;
    UINT8 InstanceId;
    UINT8 DeviceType;
    UINT8 Direction;
    UINT8 VirtualBusId;
} NHLT_ENDPOINT_SUMMARY, *PNHLT_ENDPOINT_SUMMARY;

typedef struct _NHLT_SUMMARY
{
    UINT32 HeaderLength;
    UINT32 ParsedLength;
    UINT32 OedConfigSize;
    UINT32 TrailingBytes;
    UINT16 NonHdaEndpoints;
    UINT8 EndpointsCount;
    UINT8 EndpointsParsed;
    UINT8 LinkCounts[NHLT_LINK_TYPE_COUNT];
    UINT8 OtherLinkCount;
    NHLT_ENDPOINT_SUMMARY Endpoints[NHLT_SUMMARY_ENDPOINTS];
} NHLT_SUMMARY, *PNHLT_SUMMARY;

NHLT_PARSE_STATUS
NhltParseTable(
    _In_reads_bytes_opt_(Size) const UINT8* Table,
    UINT64 Size,
    _Out_ NHLT_SUMMARY* Summary);

NHLT_PARSE_STATUS
NhltFindMemoryAddress64(
    _In_reads_bytes_opt_(Size) const UINT8* ResourceTemplate,
    SIZE_T Size,
    _Out_ UINT64* Address,
    _Out_ UINT64* Length);

const char* NhltParseStatusName(NHLT_PARSE_STATUS Status);
const char* NhltLinkTypeName(UINT8 LinkType);

#ifdef __cplusplus
}
#endif

#endif
