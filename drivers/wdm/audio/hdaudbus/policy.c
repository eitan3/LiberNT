/*
 * PROJECT:     LiberNT HD Audio Bus Driver
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Chooses the audio path a controller offers from the evidence found on it
 * COPYRIGHT:   Copyright 2026 LiberNT contributors
 */

#include <ntddk.h>
#include "policy.h"

/* TODO: name machines here once a LiberNT run shows that their evidence leads to the wrong path; the table ends with a row of two NULL strings */
static const DSP_DMI_QUIRK DspDmiQuirks[] =
{
    { NULL, NULL, DspAudioPathNone }
};

static
BOOLEAN
DspTextEqual(
    _In_z_ const char* Left,
    _In_z_ const char* Right)
{
    SIZE_T Index = 0;

    while (Left[Index] != 0 && Left[Index] == Right[Index])
        Index++;
    return Left[Index] == 0 && Right[Index] == 0;
}

static
BOOLEAN
DspTextEqualIgnoreCase(
    _In_z_ const char* Left,
    _In_z_ const char* Right)
{
    SIZE_T Index = 0;
    char L, R;

    do
    {
        L = Left[Index];
        R = Right[Index];
        if (L >= 'A' && L <= 'Z')
            L = (char)(L + ('a' - 'A'));
        if (R >= 'A' && R <= 'Z')
            R = (char)(R + ('a' - 'A'));
        Index++;
    } while (L != 0 && L == R);
    return L == R;
}

static
BOOLEAN
DspDmiFieldMatches(
    _In_ const DSP_DMI_STRINGS* Strings,
    UINT32 Field,
    _In_z_ const char* Pattern)
{
    if (!(Strings->Present & (1UL << Field)) || (Strings->Truncated & (1UL << Field)))
        return FALSE;
    return DspTextEqualIgnoreCase(Strings->Strings[Field], Pattern);
}

BOOLEAN
DspAudioPathParseName(
    const char* Name,
    DSP_AUDIO_PATH* Path)
{
    *Path = DspAudioPathNone;
    if (Name == NULL)
        return FALSE;
    if (DspTextEqual(Name, "LegacyHdaOnly"))
        *Path = DspAudioPathLegacyHdaOnly;
    else if (DspTextEqual(Name, "DspOptional"))
        *Path = DspAudioPathDspOptional;
    else if (DspTextEqual(Name, "DspRequired"))
        *Path = DspAudioPathDspRequired;
    else if (DspTextEqual(Name, "None"))
        *Path = DspAudioPathNone;
    else
        return FALSE;
    return TRUE;
}

const DSP_DMI_QUIRK*
DspAudioPathDmiLookup(
    const DSP_DMI_STRINGS* Strings,
    const DSP_DMI_QUIRK* Table)
{
    const DSP_DMI_QUIRK* Quirk;

    if (Strings == NULL)
        return NULL;
    for (Quirk = Table; Quirk->SysVendor != NULL || Quirk->SysProduct != NULL; Quirk++)
    {
        if ((Quirk->SysVendor == NULL || DspDmiFieldMatches(Strings, DSP_DMI_SYS_VENDOR, Quirk->SysVendor)) &&
            (Quirk->SysProduct == NULL || DspDmiFieldMatches(Strings, DSP_DMI_SYS_PRODUCT, Quirk->SysProduct)))
        {
            return Quirk;
        }
    }
    return NULL;
}

const DSP_DMI_QUIRK*
DspAudioPathDmiMatch(
    const DSP_DMI_STRINGS* Strings)
{
    return DspAudioPathDmiLookup(Strings, DspDmiQuirks);
}

DSP_AUDIO_PATH
DspAudioPathDecideWith(
    const DSP_AUDIO_PATH_EVIDENCE* Evidence,
    const DSP_DMI_QUIRK* Table)
{
    const DSP_DMI_QUIRK* Quirk;
    BOOLEAN DspEvidence, DspPresent;

    if (Evidence->OverrideSet)
        return Evidence->Override;

    Quirk = DspAudioPathDmiLookup(Evidence->Dmi, Table);
    if (Quirk != NULL)
        return Quirk->Path;

    DspPresent = Evidence->DspPresent;
    if (Evidence->CodecMask != 0)
    {
        if (DspPresent)
            return DspAudioPathDspOptional;
        return DspAudioPathLegacyHdaOnly;
    }

    DspEvidence = DspPresent && Evidence->NhltTableValid && Evidence->NonHdaEndpoints != 0;
    if (DspEvidence)
        return DspAudioPathDspRequired;
    return DspAudioPathNone;
}

DSP_AUDIO_PATH
DspAudioPathDecide(
    const DSP_AUDIO_PATH_EVIDENCE* Evidence)
{
    return DspAudioPathDecideWith(Evidence, DspDmiQuirks);
}

const char*
DspAudioPathName(
    DSP_AUDIO_PATH Path)
{
    static const char* const Names[] =
    {
        "None", "LegacyHdaOnly", "DspOptional", "DspRequired"
    };

    if ((UINT32)Path >= sizeof(Names) / sizeof(Names[0]))
        return "Unknown";
    return Names[Path];
}

const char*
DmiStatusName(
    DSP_DMI_STATUS Status)
{
    static const char* const Names[] =
    {
        "ok", "no data", "bad length", "truncated"
    };

    if ((UINT32)Status >= sizeof(Names) / sizeof(Names[0]))
        return "unknown";
    return Names[Status];
}
