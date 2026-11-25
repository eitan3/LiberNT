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

/* A machine whose topology file is named here instead of by the controller's ACPI _DSD; a NULL string matches any value. The driver's table is empty until a LiberNT run shows a machine that needs a row */
typedef struct _DSP_TOPOLOGY_QUIRK
{
    const char* SysVendor;
    const char* SysProduct;
    const char* Topology;
} DSP_TOPOLOGY_QUIRK, *PDSP_TOPOLOGY_QUIRK;

/* Where the topology file name comes from, in the order the DSP driver asks */
typedef enum _DSP_TOPOLOGY_SOURCE
{
    DspTopologySourceNone = 0,
    /* the Topology value under the driver's Settings key */
    DspTopologySourceOverride,
    /* the DMI quirk table */
    DspTopologySourceQuirk,
    /* a topology name in the controller's _DSD */
    DspTopologySourceDsd
} DSP_TOPOLOGY_SOURCE;

#define DSP_TOPOLOGY_NAME_BYTES    64

typedef struct _DSP_TOPOLOGY_EVIDENCE
{
    /* the registry value as it reads, NULL when it is absent or empty */
    const char* Override;
    /* the machine's strings, NULL when they could not be read */
    const DSP_DMI_STRINGS* Dmi;
    /* the three names the controller's _DSD carries, NULL or empty when it carries none */
    const char* SpeakerTplg;
    const char* HpTplg;
    const char* MicTplg;
    /* a test's quirk table instead of the driver's */
    const DSP_TOPOLOGY_QUIRK* TestTable;
} DSP_TOPOLOGY_EVIDENCE, *PDSP_TOPOLOGY_EVIDENCE;

typedef struct _DSP_TOPOLOGY_SELECTION
{
    DSP_TOPOLOGY_SOURCE Source;
    /* the file name as it was given, NULL when none was selected */
    const char* Name;
    /* the _DSD property that named it, NULL for every other source */
    const char* Property;
} DSP_TOPOLOGY_SELECTION, *PDSP_TOPOLOGY_SELECTION;

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

const DSP_TOPOLOGY_QUIRK*
DspTopologyDmiLookup(
    _In_opt_ const DSP_DMI_STRINGS* Strings,
    _In_ const DSP_TOPOLOGY_QUIRK* Table);

/* The topology file the evidence names: the override, then the machine's quirk, then the first _DSD name in the order speaker, headphone, microphone, then none */
VOID
DspTopologySelect(
    _In_ const DSP_TOPOLOGY_EVIDENCE* Evidence,
    _Out_ DSP_TOPOLOGY_SELECTION* Selection);

VOID
DspTopologySelectWith(
    _In_ const DSP_TOPOLOGY_EVIDENCE* Evidence,
    _In_ const DSP_TOPOLOGY_QUIRK* Table,
    _Out_ DSP_TOPOLOGY_SELECTION* Selection);

/* Whether a selected name is a plain file name the driver may open under its topology directory: letters, digits, dot, underline and dash only */
BOOLEAN
DspTopologyNameValid(
    _In_opt_z_ const char* Name);

const char* DspTopologySourceName(DSP_TOPOLOGY_SOURCE Source);

#ifdef __cplusplus
}
#endif

#endif
