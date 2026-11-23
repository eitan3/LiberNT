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

/* acpi_nhlt_config without capabilities, and a three-byte PDM mic array entry */
#define NHLT_NO_DEVICE_CONFIG            DSP_U32(0)
#define NHLT_MIC_ARRAY_CONFIG(Slot, Type, Array) DSP_U32(3), (Slot), (Type), (Array)

/* acpi_nhlt_wave_formatext with the PCM subformat, and a format config of CapSize capabilities */
#define NHLT_SUBFORMAT_PCM \
    0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x10, 0x00, 0x80, 0x00, 0x00, 0xAA, 0x00, 0x38, 0x9B, 0x71
#define NHLT_WAVE_EXT(Tag, Channels, Rate, BytesPerSec, BlockAlign, Bits, Extra, ValidBits, Mask) \
    DSP_U16(Tag), DSP_U16(Channels), DSP_U32(Rate), DSP_U32(BytesPerSec), DSP_U16(BlockAlign), DSP_U16(Bits), \
    DSP_U16(Extra), DSP_U16(ValidBits), DSP_U32(Mask), NHLT_SUBFORMAT_PCM
#define NHLT_FORMAT_CONFIG(CapSize) DSP_U32(CapSize)

#define NHLT_WAVE_48K_4CH_24B NHLT_WAVE_EXT(0xFFFE, 4, 48000, 768000, 16, 32, 22, 24, 0x33)
#define NHLT_WAVE_48K_2CH_16B NHLT_WAVE_EXT(0x0001, 2, 48000, 192000, 4, 16, 0, 0, 0x03)
#define NHLT_WAVE_96K_2CH_32B NHLT_WAVE_EXT(0xFFFE, 2, 96000, 384000, 8, 32, 22, 32, 0x03)

/* acpi_nhlt_devices_info: the count, then records of 16 id bytes, instance and port */
#define NHLT_DEVICE_INFO(Id0, Id1, Id2, Id3, Instance, Port) \
    (Id0), (Id1), (Id2), (Id3), 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, (Instance), (Port)

/* Endpoint records: the fixed part, the device config, the formats count, the formats, then the devices info */
#define NHLT_SSP_CODEC_ENDPOINT \
    NHLT_ENDPOINT(24, 3, 0, 0x8086, 0xAE34, 0x0100, 0x12345678, 4, 0, 0), NHLT_NO_DEVICE_CONFIG, 0x00
#define NHLT_DMIC_ENDPOINT \
    NHLT_ENDPOINT(71, 2, 0, 0x8086, 0xAE20, 0x0100, 0x12345678, 0, 1, 0), \
    NHLT_MIC_ARRAY_CONFIG(0, 1, 0x0A), 0x01, NHLT_WAVE_48K_4CH_24B, NHLT_FORMAT_CONFIG(0)
#define NHLT_HDA_ENDPOINT \
    NHLT_ENDPOINT(24, 0, 0, 0x8086, 0x280D, 0x0100, 0x12345678, 0, 0, 0), NHLT_NO_DEVICE_CONFIG, 0x00
#define NHLT_SDW_ENDPOINT \
    NHLT_ENDPOINT(157, 5, 0, 0x025D, 0x0714, 0x0100, 0x12345678, 0, 0, 0), NHLT_NO_DEVICE_CONFIG, 0x02, \
    NHLT_WAVE_48K_2CH_16B, NHLT_FORMAT_CONFIG(8), 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88, \
    NHLT_WAVE_96K_2CH_32B, NHLT_FORMAT_CONFIG(0), \
    0x02, NHLT_DEVICE_INFO('a', 'm', 'p', '0', 1, 0), NHLT_DEVICE_INFO('j', 'a', 'c', 'k', 2, 1)

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
    unsigned short FormatsCount;
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
    unsigned int DeviceConfigSize;
    unsigned char FormatsCount;
    unsigned char DevicesCount;
} NHLT_ENDPOINT_CASE;

typedef struct _NHLT_VIEW_CASE
{
    unsigned int TableIndex;
    unsigned int EndpointIndex;
    int Status;
    unsigned int DeviceConfigSize;
    unsigned char FormatsCount;
    unsigned char DevicesCount;
    int HasDevicesInfo;
    int Located;
} NHLT_VIEW_CASE;

typedef struct _NHLT_FORMAT_CASE
{
    unsigned int TableIndex;
    unsigned int EndpointIndex;
    unsigned int FormatIndex;
    int Status;
    unsigned short FormatTag;
    unsigned short ChannelCount;
    unsigned int SamplesPerSec;
    unsigned int AvgBytesPerSec;
    unsigned short BlockAlign;
    unsigned short BitsPerSample;
    unsigned short ExtraFormatSize;
    unsigned short ValidBitsPerSample;
    unsigned int ChannelMask;
    unsigned int ConfigSize;
    unsigned char ConfigFirst;
    int IsPcm;
} NHLT_FORMAT_CASE;

typedef struct _NHLT_DEVICE_CASE
{
    unsigned int TableIndex;
    unsigned int EndpointIndex;
    unsigned int DeviceIndex;
    int Status;
    unsigned char Id[4];
    unsigned char InstanceId;
    unsigned char PortId;
} NHLT_DEVICE_CASE;

typedef struct _NHLT_LINK_CASE
{
    unsigned int TableIndex;
    unsigned char LinkType;
    int Present;
    unsigned int SspMask;
} NHLT_LINK_CASE;
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
    const char* SysVendor;
    const char* SysProduct;
    int Override;
    int TestTable;
    int Expected;
    const char* ExpectedName;
} POLICY_CASE;

static const unsigned char NhltEmptyTable[] =
{
    NHLT_HEADER(37), 0x00
};

static const unsigned char NhltTwoEndpointTable[] =
{
    NHLT_HEADER(138), 0x02,
    NHLT_SSP_CODEC_ENDPOINT,
    NHLT_DMIC_ENDPOINT,
    DSP_U32(2), 0xAA, 0x55
};

static const unsigned char NhltHdaOnlyTable[] =
{
    NHLT_HEADER(61), 0x01,
    NHLT_HDA_ENDPOINT
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
    NHLT_SSP_CODEC_ENDPOINT,
    DSP_U32(30), 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};

static const unsigned char NhltNoCountByteTable[] =
{
    NHLT_HEADER(37)
};

static const unsigned char NhltUnknownLinkTable[] =
{
    NHLT_HEADER(61), 0x01,
    NHLT_ENDPOINT(24, 7, 0, 0x10EC, 0x0287, 0x1000, 0x12345678, 0, 0, 0), NHLT_NO_DEVICE_CONFIG, 0x00
};

#define NHLT_NINE_SSP_ENDPOINTS \
    NHLT_ENDPOINT(24, 3, 0, 0x8086, 0xAE34, 0x0100, 0x12345678, 4, 0, 0), NHLT_NO_DEVICE_CONFIG, 0x00, \
    NHLT_ENDPOINT(24, 3, 1, 0x8086, 0xAE34, 0x0100, 0x12345678, 4, 0, 0), NHLT_NO_DEVICE_CONFIG, 0x00, \
    NHLT_ENDPOINT(24, 3, 2, 0x8086, 0xAE34, 0x0100, 0x12345678, 4, 0, 0), NHLT_NO_DEVICE_CONFIG, 0x00, \
    NHLT_ENDPOINT(24, 3, 3, 0x8086, 0xAE34, 0x0100, 0x12345678, 4, 0, 0), NHLT_NO_DEVICE_CONFIG, 0x00, \
    NHLT_ENDPOINT(24, 3, 4, 0x8086, 0xAE34, 0x0100, 0x12345678, 4, 0, 0), NHLT_NO_DEVICE_CONFIG, 0x00, \
    NHLT_ENDPOINT(24, 3, 5, 0x8086, 0xAE34, 0x0100, 0x12345678, 4, 0, 0), NHLT_NO_DEVICE_CONFIG, 0x00, \
    NHLT_ENDPOINT(24, 3, 6, 0x8086, 0xAE34, 0x0100, 0x12345678, 4, 0, 0), NHLT_NO_DEVICE_CONFIG, 0x00, \
    NHLT_ENDPOINT(24, 3, 7, 0x8086, 0xAE34, 0x0100, 0x12345678, 4, 0, 0), NHLT_NO_DEVICE_CONFIG, 0x00, \
    NHLT_ENDPOINT(24, 3, 8, 0x8086, 0xAE34, 0x0100, 0x12345678, 4, 0, 0), NHLT_NO_DEVICE_CONFIG, 0x00
#define NHLT_SIX_DMIC_ENDPOINTS \
    NHLT_ENDPOINT(24, 2, 0, 0x8086, 0xAE20, 0x0100, 0x12345678, 0, 1, 0), NHLT_NO_DEVICE_CONFIG, 0x00, \
    NHLT_ENDPOINT(24, 2, 1, 0x8086, 0xAE20, 0x0100, 0x12345678, 0, 1, 0), NHLT_NO_DEVICE_CONFIG, 0x00, \
    NHLT_ENDPOINT(24, 2, 2, 0x8086, 0xAE20, 0x0100, 0x12345678, 0, 1, 0), NHLT_NO_DEVICE_CONFIG, 0x00, \
    NHLT_ENDPOINT(24, 2, 3, 0x8086, 0xAE20, 0x0100, 0x12345678, 0, 1, 0), NHLT_NO_DEVICE_CONFIG, 0x00, \
    NHLT_ENDPOINT(24, 2, 4, 0x8086, 0xAE20, 0x0100, 0x12345678, 0, 1, 0), NHLT_NO_DEVICE_CONFIG, 0x00, \
    NHLT_ENDPOINT(24, 2, 5, 0x8086, 0xAE20, 0x0100, 0x12345678, 0, 1, 0), NHLT_NO_DEVICE_CONFIG, 0x00

static const unsigned char NhltEighteenEndpointTable[] =
{
    NHLT_HEADER(469), 18,
    NHLT_NINE_SSP_ENDPOINTS,
    NHLT_SIX_DMIC_ENDPOINTS,
    NHLT_ENDPOINT(24, 5, 0, 0x025D, 0x0714, 0x0100, 0x12345678, 0, 0, 0), NHLT_NO_DEVICE_CONFIG, 0x00,
    NHLT_ENDPOINT(24, 5, 1, 0x025D, 0x0714, 0x0100, 0x12345678, 0, 0, 0), NHLT_NO_DEVICE_CONFIG, 0x00,
    NHLT_ENDPOINT(24, 1, 0, 0x8086, 0x0AE30, 0x0100, 0x12345678, 0, 0, 0), NHLT_NO_DEVICE_CONFIG, 0x00
};

static const unsigned char NhltEndpointBelowFixedTable[] =
{
    NHLT_HEADER(56), 0x01,
    DSP_U32(10), 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};

static const unsigned char NhltCountPastTable[] =
{
    NHLT_HEADER(61), 0x03,
    NHLT_SSP_CODEC_ENDPOINT
};

static const unsigned char NhltSdwDevicesTable[] =
{
    NHLT_HEADER(194), 0x01,
    NHLT_SDW_ENDPOINT
};

static const unsigned char NhltDeviceConfigPastEndpointTable[] =
{
    NHLT_HEADER(61), 0x01,
    NHLT_ENDPOINT(24, 3, 0, 0x8086, 0xAE34, 0x0100, 0x12345678, 4, 0, 0), DSP_U32(10), 0x00
};

static const unsigned char NhltFormatConfigPastEndpointTable[] =
{
    NHLT_HEADER(105), 0x01,
    NHLT_ENDPOINT(68, 3, 1, 0x8086, 0xAE34, 0x0100, 0x12345678, 4, 0, 0), NHLT_NO_DEVICE_CONFIG, 0x01,
    NHLT_WAVE_48K_2CH_16B, NHLT_FORMAT_CONFIG(20)
};

static const unsigned char NhltDevicesPastEndpointTable[] =
{
    NHLT_HEADER(80), 0x01,
    NHLT_ENDPOINT(43, 5, 0, 0x025D, 0x0714, 0x0100, 0x12345678, 0, 0, 0), NHLT_NO_DEVICE_CONFIG, 0x00,
    0x03, NHLT_DEVICE_INFO('a', 'm', 'p', '0', 1, 0)
};

/* one device record and a count of two */
static const unsigned char NhltDevicesOnePastEndpointTable[] =
{
    NHLT_HEADER(80), 0x01,
    NHLT_ENDPOINT(43, 5, 0, 0x025D, 0x0714, 0x0100, 0x12345678, 0, 0, 0), NHLT_NO_DEVICE_CONFIG, 0x00,
    0x02, NHLT_DEVICE_INFO('a', 'm', 'p', '0', 1, 0)
};

static const unsigned char NhltFormatsCountPastEndpointTable[] =
{
    NHLT_HEADER(115), 0x01,
    NHLT_ENDPOINT(78, 3, 0, 0x8086, 0xAE34, 0x0100, 0x12345678, 4, 0, 0), NHLT_NO_DEVICE_CONFIG, 0x02,
    NHLT_WAVE_48K_2CH_16B, NHLT_FORMAT_CONFIG(0), 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};

static const unsigned char NhltNoFormatsCountTable[] =
{
    NHLT_HEADER(60), 0x01,
    NHLT_ENDPOINT(23, 3, 0, 0x8086, 0xAE34, 0x0100, 0x12345678, 4, 0, 0), NHLT_NO_DEVICE_CONFIG
};

static const unsigned char NhltBytesAfterDevicesTable[] =
{
    NHLT_HEADER(82), 0x01,
    NHLT_ENDPOINT(45, 5, 0, 0x025D, 0x0714, 0x0100, 0x12345678, 0, 0, 0), NHLT_NO_DEVICE_CONFIG, 0x00,
    0x01, NHLT_DEVICE_INFO('a', 'm', 'p', '0', 1, 0), 0xDE, 0xAD
};

/* A real machine's static table, structure reproduced with fictional strings: Windows publishes a stub with no endpoint and a 4-byte placeholder OED config; the descriptors come from the firmware _DSM at run time. The checksum byte is computed over the fictional OEM strings */
static const unsigned char NhltStubTable[] =
{
    'N', 'H', 'L', 'T', DSP_U32(45), 0x01, 0x73, 'O', 'E', 'M', 'X', 'Y', 'Z', 'S', 'T', 'U', 'B', '0', '1', ' ', ' ',
    DSP_U32(1), 'A', 'C', 'P', 'I', DSP_U32(0x00040000),
    0x00, DSP_U32(4), 0xDE, 0xAD, 0xBE, 0xEF
};

static const NHLT_TABLE_CASE NhltTableCases[] =
{
    { "empty table", NhltEmptyTable, sizeof(NhltEmptyTable), 0, 37, 37, 0, 0, 0, 0, 0, 0, { 0, 0, 0, 0, 0, 0, 0 }, 0 },
    { "SSP codec and DMIC with OED config", NhltTwoEndpointTable, sizeof(NhltTwoEndpointTable), 0, 138, 132, 2, 6, 2, 1, 2, 2,
        { 0, 0, 1, 1, 0, 0, 0 }, 0 },
    { "HDA link endpoint only", NhltHdaOnlyTable, sizeof(NhltHdaOnlyTable), 0, 61, 61, 0, 0, 0, 0, 1, 1, { 1, 0, 0, 0, 0, 0, 0 }, 0 },
    { "bad signature", NhltBadSignatureTable, sizeof(NhltBadSignatureTable), 2, 0, 0, 0, 0, 0, 0, 0, 0, { 0, 0, 0, 0, 0, 0, 0 }, 0 },
    { "header length past the table", NhltLengthPastTable, sizeof(NhltLengthPastTable), 3, 1000, 0, 0, 0, 0, 0, 0, 0, { 0, 0, 0, 0, 0, 0, 0 }, 0 },
    { "header length below the fixed part", NhltLengthBelowFixedTable, sizeof(NhltLengthBelowFixedTable), 3, 20, 0, 0, 0, 0, 0, 0, 0, { 0, 0, 0, 0, 0, 0, 0 }, 0 },
    { "second endpoint runs past the table", NhltSecondEndpointTruncatedTable, sizeof(NhltSecondEndpointTruncatedTable), 4, 70, 61, 0, 0, 1, 0, 2, 1, { 0, 0, 0, 1, 0, 0, 0 }, 0 },
    { "table without its count byte", NhltNoCountByteTable, sizeof(NhltNoCountByteTable), 4, 0, 0, 0, 0, 0, 0, 0, 0, { 0, 0, 0, 0, 0, 0, 0 }, 0 },
    { "no table", NULL, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, { 0, 0, 0, 0, 0, 0, 0 }, 0 },
    { "unknown link type", NhltUnknownLinkTable, sizeof(NhltUnknownLinkTable), 0, 61, 61, 0, 0, 1, 0, 1, 1, { 0, 0, 0, 0, 0, 0, 0 }, 1 },
    { "eighteen endpoints, sixteen kept", NhltEighteenEndpointTable, sizeof(NhltEighteenEndpointTable), 0, 469, 469, 0, 0, 18, 0, 18, 18,
        { 0, 1, 6, 9, 0, 2, 0 }, 0 },
    { "endpoint length below the fixed part", NhltEndpointBelowFixedTable, sizeof(NhltEndpointBelowFixedTable), 4, 56, 37, 0, 0, 0, 0, 1, 0, { 0, 0, 0, 0, 0, 0, 0 }, 0 },
    { "count names more endpoints than the table holds", NhltCountPastTable, sizeof(NhltCountPastTable), 4, 61, 61, 0, 0, 1, 0, 3, 1, { 0, 0, 0, 1, 0, 0, 0 }, 0 },
    { "SoundWire endpoint with two formats and two devices", NhltSdwDevicesTable, sizeof(NhltSdwDevicesTable), 0, 194, 194, 0, 0, 1, 2, 1, 1, { 0, 0, 0, 0, 0, 1, 0 }, 0 },
    { "device config runs past its endpoint", NhltDeviceConfigPastEndpointTable, sizeof(NhltDeviceConfigPastEndpointTable), 4, 61, 37, 0, 0, 0, 0, 1, 0, { 0, 0, 0, 0, 0, 0, 0 }, 0 },
    { "format config runs past its endpoint", NhltFormatConfigPastEndpointTable, sizeof(NhltFormatConfigPastEndpointTable), 4, 105, 37, 0, 0, 0, 0, 1, 0, { 0, 0, 0, 0, 0, 0, 0 }, 0 },
    { "devices info runs past its endpoint", NhltDevicesPastEndpointTable, sizeof(NhltDevicesPastEndpointTable), 4, 80, 37, 0, 0, 0, 0, 1, 0, { 0, 0, 0, 0, 0, 0, 0 }, 0 },
    { "formats count names more formats than the endpoint holds", NhltFormatsCountPastEndpointTable, sizeof(NhltFormatsCountPastEndpointTable), 4, 115, 37, 0, 0, 0, 0, 1, 0, { 0, 0, 0, 0, 0, 0, 0 }, 0 },
    { "endpoint without its formats count", NhltNoFormatsCountTable, sizeof(NhltNoFormatsCountTable), 4, 60, 37, 0, 0, 0, 0, 1, 0, { 0, 0, 0, 0, 0, 0, 0 }, 0 },
    { "endpoint keeps bytes after its devices", NhltBytesAfterDevicesTable, sizeof(NhltBytesAfterDevicesTable), 0, 82, 82, 0, 0, 1, 0, 1, 1, { 0, 0, 0, 0, 0, 1, 0 }, 0 },
    { "devices count one past its endpoint", NhltDevicesOnePastEndpointTable, sizeof(NhltDevicesOnePastEndpointTable), 4, 80, 37, 0, 0, 0, 0, 1, 0, { 0, 0, 0, 0, 0, 0, 0 }, 0 },
    { "a real machine's static table: a stub without endpoints", NhltStubTable, sizeof(NhltStubTable), 0, 45, 37, 4, 8, 0, 0, 0, 0, { 0, 0, 0, 0, 0, 0, 0 }, 0 }
};

static const NHLT_ENDPOINT_CASE NhltEndpointCases[] =
{
    { 1, 0, 24, 0x12345678, 0x8086, 0xAE34, 0x0100, 3, 0, 4, 0, 0, 0, 0, 0 },
    { 1, 1, 71, 0x12345678, 0x8086, 0xAE20, 0x0100, 2, 0, 0, 1, 0, 3, 1, 0 },
    { 9, 0, 24, 0x12345678, 0x10EC, 0x0287, 0x1000, 7, 0, 0, 0, 0, 0, 0, 0 },
    { 10, 16, 24, 0x12345678, 0x025D, 0x0714, 0x0100, 5, 1, 0, 0, 0, 0, 0, 0 },
    { 10, 17, 24, 0x12345678, 0x8086, 0x0AE30, 0x0100, 1, 0, 0, 0, 0, 0, 0, 0 },
    { 13, 0, 157, 0x12345678, 0x025D, 0x0714, 0x0100, 5, 0, 0, 0, 0, 0, 2, 2 },
    { 19, 0, 45, 0x12345678, 0x025D, 0x0714, 0x0100, 5, 0, 0, 0, 0, 0, 0, 1 }
};

static const NHLT_VIEW_CASE NhltViewCases[] =
{
    { 1, 0, 0, 0, 0, 0, 0, 1 },
    { 1, 1, 0, 3, 1, 0, 0, 1 },
    { 1, 2, 5, 0, 0, 0, 0, 0 },
    { 6, 0, 0, 0, 0, 0, 0, 1 },
    { 6, 1, 4, 0, 0, 0, 0, 0 },
    { 13, 0, 0, 0, 2, 2, 1, 1 },
    { 14, 0, 4, 0, 0, 0, 0, 1 },
    { 15, 0, 4, 0, 1, 0, 0, 1 },
    { 16, 0, 4, 0, 0, 3, 1, 1 },
    { 17, 0, 4, 0, 2, 0, 0, 1 },
    { 18, 0, 4, 0, 0, 0, 0, 1 },
    { 20, 0, 4, 0, 0, 2, 1, 1 },
    { 19, 0, 0, 0, 0, 1, 1, 1 },
    { 3, 0, 2, 0, 0, 0, 0, 0 },
    { 8, 0, 1, 0, 0, 0, 0, 0 }
};

static const NHLT_FORMAT_CASE NhltFormatCases[] =
{
    { 1, 1, 0, 0, 0xFFFE, 4, 48000, 768000, 16, 32, 22, 24, 0x33, 0, 0, 1 },
    { 13, 0, 0, 0, 0x0001, 2, 48000, 192000, 4, 16, 0, 0, 0x03, 8, 0x11, 1 },
    { 13, 0, 1, 0, 0xFFFE, 2, 96000, 384000, 8, 32, 22, 32, 0x03, 0, 0, 1 },
    { 1, 0, 0, 5, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
    { 1, 1, 1, 5, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
    { 17, 0, 0, 0, 0x0001, 2, 48000, 192000, 4, 16, 0, 0, 0x03, 0, 0, 1 },
    { 17, 0, 1, 4, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 }
};

static const NHLT_DEVICE_CASE NhltDeviceCases[] =
{
    { 13, 0, 0, 0, { 'a', 'm', 'p', '0' }, 1, 0 },
    { 13, 0, 1, 0, { 'j', 'a', 'c', 'k' }, 2, 1 },
    { 13, 0, 2, 5, { 0, 0, 0, 0 }, 0, 0 },
    { 19, 0, 0, 0, { 'a', 'm', 'p', '0' }, 1, 0 },
    { 1, 1, 0, 5, { 0, 0, 0, 0 }, 0, 0 },
    { 16, 0, 0, 0, { 'a', 'm', 'p', '0' }, 1, 0 },
    { 16, 0, 1, 4, { 0, 0, 0, 0 }, 0, 0 },
    { 20, 0, 0, 0, { 'a', 'm', 'p', '0' }, 1, 0 },
    { 20, 0, 1, 4, { 0, 0, 0, 0 }, 0, 0 },
    { 16, 0, 3, 5, { 0, 0, 0, 0 }, 0, 0 }
};

static const NHLT_LINK_CASE NhltLinkCases[] =
{
    { 1, 2, 1, 0x1 },
    { 1, 3, 1, 0x1 },
    { 1, 5, 0, 0x1 },
    { 1, 0, 0, 0x1 },
    { 2, 0, 1, 0x0 },
    { 2, 2, 0, 0x0 },
    { 3, 2, 0, 0x0 },
    { 8, 2, 0, 0x0 },
    { 10, 3, 1, 0x1FF },
    { 10, 5, 1, 0x1FF },
    { 10, 0, 0, 0x1FF },
    { 12, 3, 1, 0x1 },
    { 13, 5, 1, 0x0 },
    { 14, 3, 1, 0x1 },
    { 15, 3, 1, 0x2 },
    { 9, 7, 1, 0x1 },
    { 21, 2, 0, 0x0 },
    { 21, 5, 0, 0x0 }
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

/* Override -1 means no value under Settings\DspAudioPathOverride; TestTable 1 decides with DspTestQuirks of dmi_cases.h instead of the empty table of the driver */
static const POLICY_CASE PolicyCases[] =
{
    { 0x0000, 0, 0, 0, NULL, NULL, -1, 0, 0, "None" },
    { 0x0000, 1, 0, 0, NULL, NULL, -1, 0, 0, "None" },
    { 0x0000, 0, 0, 1, NULL, NULL, -1, 0, 0, "None" },
    { 0x0000, 1, 0, 1, NULL, NULL, -1, 0, 0, "None" },
    { 0x0000, 0, 1, 0, NULL, NULL, -1, 0, 0, "None" },
    { 0x0000, 1, 1, 0, NULL, NULL, -1, 0, 0, "None" },
    { 0x0000, 0, 1, 1, NULL, NULL, -1, 0, 0, "None" },
    { 0x0000, 1, 1, 1, NULL, NULL, -1, 0, 3, "DspRequired" },
    { 0x0002, 0, 0, 0, NULL, NULL, -1, 0, 1, "LegacyHdaOnly" },
    { 0x0002, 1, 0, 0, NULL, NULL, -1, 0, 1, "LegacyHdaOnly" },
    { 0x0002, 0, 0, 1, NULL, NULL, -1, 0, 1, "LegacyHdaOnly" },
    { 0x0002, 1, 0, 1, NULL, NULL, -1, 0, 1, "LegacyHdaOnly" },
    { 0x0002, 0, 1, 0, NULL, NULL, -1, 0, 2, "DspOptional" },
    { 0x0002, 1, 1, 0, NULL, NULL, -1, 0, 2, "DspOptional" },
    { 0x8002, 0, 1, 1, NULL, NULL, -1, 0, 2, "DspOptional" },
    { 0x8002, 1, 1, 1, NULL, NULL, -1, 0, 2, "DspOptional" },
    { 0x0002, 0, 1, 1, "Vendor A", "Product A", -1, 0, 2, "DspOptional" },
    { 0x0002, 0, 1, 1, "Vendor A", "Product A", -1, 1, 3, "DspRequired" },
    { 0x0002, 0, 1, 1, "vendor a", "PRODUCT A", -1, 1, 3, "DspRequired" },
    { 0x0000, 1, 1, 1, "vendor b", "anything", -1, 1, 2, "DspOptional" },
    { 0x0000, 0, 0, 0, "anyone", "product c", -1, 1, 1, "LegacyHdaOnly" },
    { 0x0000, 1, 1, 1, "Vendor X", "Product A", -1, 1, 3, "DspRequired" },
    { 0x0002, 0, 1, 1, "Vendor A", "Product A", 1, 1, 1, "LegacyHdaOnly" },
    { 0x0002, 0, 1, 1, NULL, NULL, 1, 0, 1, "LegacyHdaOnly" },
    { 0x0002, 0, 1, 1, NULL, NULL, 0, 0, 0, "None" },
    { 0x0002, 0, 1, 1, NULL, NULL, 2, 0, 2, "DspOptional" },
    { 0x0002, 0, 1, 1, NULL, NULL, 3, 0, 3, "DspRequired" }
};

#endif
