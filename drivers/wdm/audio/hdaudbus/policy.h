/*
 * PROJECT:     LiberNT HD Audio Bus Driver
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Chooses the audio path a controller offers from the evidence found on it
 * COPYRIGHT:   Copyright 2026 LiberNT contributors
 */

#ifndef _SKLHDAUDBUS_POLICY_H_
#define _SKLHDAUDBUS_POLICY_H_

#ifdef __cplusplus
extern "C" {
#endif

typedef enum _DSP_AUDIO_PATH
{
    DspAudioPathNone = 0,
    DspAudioPathLegacyHdaOnly,
    DspAudioPathDspOptional,
    DspAudioPathDspRequired
} DSP_AUDIO_PATH;

#define DSP_DMI_STRING_BYTES    64
#define DSP_DMI_STRING_COUNT    8

#define DSP_DMI_SYS_VENDOR      0
#define DSP_DMI_SYS_PRODUCT     1
#define DSP_DMI_SYS_VERSION     2
#define DSP_DMI_SYS_SKU         3
#define DSP_DMI_BOARD_VENDOR    4
#define DSP_DMI_BOARD_NAME      5
#define DSP_DMI_BOARD_VERSION   6
#define DSP_DMI_BIOS_VERSION    7

#define DSP_DMI_RAW_HEADER_BYTES 8

typedef struct _DSP_DMI_STRINGS
{
    CHAR Strings[DSP_DMI_STRING_COUNT][DSP_DMI_STRING_BYTES];
    UINT32 Present;
    UINT32 Truncated;
    UINT8 SmbiosMajorVersion;
    UINT8 SmbiosMinorVersion;
    UINT8 DmiRevision;
} DSP_DMI_STRINGS, *PDSP_DMI_STRINGS;

typedef enum _DSP_DMI_STATUS
{
    DmiOk = 0,
    DmiNoData,
    DmiBadLength,
    DmiTruncated
} DSP_DMI_STATUS;

/* A machine whose audio path is set here instead of from the evidence; a NULL string matches any value */
typedef struct _DSP_DMI_QUIRK
{
    const char* SysVendor;
    const char* SysProduct;
    DSP_AUDIO_PATH Path;
} DSP_DMI_QUIRK, *PDSP_DMI_QUIRK;

typedef struct _DSP_AUDIO_PATH_EVIDENCE
{
    UINT16 CodecMask;
    UINT16 NonHdaEndpoints;
    BOOLEAN DspPresent;
    BOOLEAN NhltTableValid;
    const DSP_DMI_STRINGS* Dmi;
    BOOLEAN OverrideSet;
    DSP_AUDIO_PATH Override;
} DSP_AUDIO_PATH_EVIDENCE, *PDSP_AUDIO_PATH_EVIDENCE;

DSP_DMI_STATUS
DmiParseRawSMBiosTables(
    _In_reads_bytes_opt_(Size) const UINT8* Raw,
    SIZE_T Size,
    _Out_ DSP_DMI_STRINGS* Strings);

BOOLEAN
DspAudioPathParseName(
    _In_z_ const char* Name,
    _Out_ DSP_AUDIO_PATH* Path);

const DSP_DMI_QUIRK*
DspAudioPathDmiLookup(
    _In_opt_ const DSP_DMI_STRINGS* Strings,
    _In_ const DSP_DMI_QUIRK* Table);

const DSP_DMI_QUIRK*
DspAudioPathDmiMatch(
    _In_opt_ const DSP_DMI_STRINGS* Strings);

DSP_AUDIO_PATH DspAudioPathDecide(_In_ const DSP_AUDIO_PATH_EVIDENCE* Evidence);

DSP_AUDIO_PATH
DspAudioPathDecideWith(
    _In_ const DSP_AUDIO_PATH_EVIDENCE* Evidence,
    _In_ const DSP_DMI_QUIRK* Table);
const char* DspAudioPathName(DSP_AUDIO_PATH Path);
const char* DmiStatusName(DSP_DMI_STATUS Status);

#ifdef __cplusplus
}
#endif

#endif
