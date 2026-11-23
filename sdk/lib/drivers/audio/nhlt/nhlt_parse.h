/*
 * PROJECT:     LiberNT audio driver libraries
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Summarises an NHLT table, walks the formats and devices of its endpoints and finds the memory resource of an NHLT _DSM reply, without I/O
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

#define NHLT_DEVICEID_DMIC      0xAE20
#define NHLT_DEVICEID_BT        0xAE30
#define NHLT_DEVICEID_I2S       0xAE34

#define NHLT_DEVICETYPE_BT      0
#define NHLT_DEVICETYPE_FM      1
#define NHLT_DEVICETYPE_MODEM   2
#define NHLT_DEVICETYPE_CODEC   4

#define NHLT_DIR_RENDER         0
#define NHLT_DIR_CAPTURE        1

#define NHLT_SUMMARY_ENDPOINTS  16

#define NHLT_DEVICE_ID_BYTES    16
#define NHLT_SUBFORMAT_BYTES    16

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
    UINT32 DeviceConfigSize;
    UINT8 LinkType;
    UINT8 InstanceId;
    UINT8 DeviceType;
    UINT8 Direction;
    UINT8 VirtualBusId;
    UINT8 FormatsCount;
    UINT8 DevicesCount;
} NHLT_ENDPOINT_SUMMARY, *PNHLT_ENDPOINT_SUMMARY;

typedef struct _NHLT_SUMMARY
{
    UINT32 HeaderLength;
    UINT32 ParsedLength;
    UINT32 OedConfigSize;
    UINT32 TrailingBytes;
    UINT16 NonHdaEndpoints;
    UINT16 FormatsCount;
    UINT8 EndpointsCount;
    UINT8 EndpointsParsed;
    UINT8 LinkCounts[NHLT_LINK_TYPE_COUNT];
    UINT8 OtherLinkCount;
    NHLT_ENDPOINT_SUMMARY Endpoints[NHLT_SUMMARY_ENDPOINTS];
} NHLT_SUMMARY, *PNHLT_SUMMARY;

/* acpi_nhlt_endpoint and the three parts that follow it inside its Length */
typedef struct _NHLT_ENDPOINT_VIEW
{
    const UINT8* Endpoint;
    SIZE_T EndpointLength;
    const UINT8* DeviceConfig;
    SIZE_T DeviceConfigSize;
    UINT8 FormatsCount;
    BOOLEAN HasDevicesInfo;
    UINT8 DevicesCount;
} NHLT_ENDPOINT_VIEW, *PNHLT_ENDPOINT_VIEW;

/* acpi_nhlt_wave_formatext, acpi_nhlt_format_config */
typedef struct _NHLT_FORMAT
{
    UINT16 FormatTag;
    UINT16 ChannelCount;
    UINT32 SamplesPerSec;
    UINT32 AvgBytesPerSec;
    UINT16 BlockAlign;
    UINT16 BitsPerSample;
    UINT16 ExtraFormatSize;
    UINT16 ValidBitsPerSample;
    UINT32 ChannelMask;
    UINT8 Subformat[NHLT_SUBFORMAT_BYTES];
    const UINT8* Config;
    SIZE_T ConfigSize;
} NHLT_FORMAT, *PNHLT_FORMAT;

/* acpi_nhlt_device_info */
typedef struct _NHLT_DEVICE
{
    UINT8 Id[NHLT_DEVICE_ID_BYTES];
    UINT8 InstanceId;
    UINT8 PortId;
} NHLT_DEVICE, *PNHLT_DEVICE;

NHLT_PARSE_STATUS
NhltParseTable(
    _In_reads_bytes_opt_(Size) const UINT8* Table,
    UINT64 Size,
    _Out_ NHLT_SUMMARY* Summary);

NHLT_PARSE_STATUS
NhltEndpointView(
    _In_reads_bytes_opt_(Size) const UINT8* Table,
    UINT64 Size,
    UINT8 Index,
    _Out_ NHLT_ENDPOINT_VIEW* View);

NHLT_PARSE_STATUS
NhltEndpointFormat(
    _In_ const NHLT_ENDPOINT_VIEW* View,
    UINT8 Index,
    _Out_ NHLT_FORMAT* Format);

NHLT_PARSE_STATUS
NhltEndpointDevice(
    _In_ const NHLT_ENDPOINT_VIEW* View,
    UINT8 Index,
    _Out_ NHLT_DEVICE* Device);

BOOLEAN
NhltHasLinkType(
    _In_reads_bytes_opt_(Size) const UINT8* Table,
    UINT64 Size,
    UINT8 LinkType);

UINT32
NhltSspEndpointMask(
    _In_reads_bytes_opt_(Size) const UINT8* Table,
    UINT64 Size);

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
