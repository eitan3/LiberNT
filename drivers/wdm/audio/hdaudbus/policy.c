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

/* TODO: name machines here once a LiberNT run shows that their _DSD names no topology the image carries; the table ends with a row of two NULL strings */
static const DSP_TOPOLOGY_QUIRK DspTopologyQuirks[] =
{
    { NULL, NULL, NULL }
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

static
BOOLEAN
DspTopologyNamePresent(
    _In_opt_z_ const char* Name)
{
    return Name != NULL && Name[0] != 0;
}

const DSP_TOPOLOGY_QUIRK*
DspTopologyDmiLookup(
    const DSP_DMI_STRINGS* Strings,
    const DSP_TOPOLOGY_QUIRK* Table)
{
    const DSP_TOPOLOGY_QUIRK* Quirk;

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

VOID
DspTopologySelectWith(
    const DSP_TOPOLOGY_EVIDENCE* Evidence,
    const DSP_TOPOLOGY_QUIRK* Table,
    DSP_TOPOLOGY_SELECTION* Selection)
{
    const DSP_TOPOLOGY_QUIRK* Quirk;
    static const char* const Properties[3] = { "speaker-tplg", "hp-tplg", "mic-tplg" };
    const char* Names[3];
    UINT32 Index;

    Selection->Source = DspTopologySourceNone;
    Selection->Name = NULL;
    Selection->Property = NULL;
    /* the override is used as it reads, whether or not the file exists: a missing file is the driver's error line, never a fall-through */
    if (DspTopologyNamePresent(Evidence->Override))
    {
        Selection->Source = DspTopologySourceOverride;
        Selection->Name = Evidence->Override;
        return;
    }
    Quirk = DspTopologyDmiLookup(Evidence->Dmi, Table);
    if (Quirk != NULL && DspTopologyNamePresent(Quirk->Topology))
    {
        Selection->Source = DspTopologySourceQuirk;
        Selection->Name = Quirk->Topology;
        return;
    }
    /* the driver loads one topology file: of several _DSD names it takes the first, speaker before headphone before microphone */
    Names[0] = Evidence->SpeakerTplg;
    Names[1] = Evidence->HpTplg;
    Names[2] = Evidence->MicTplg;
    for (Index = 0; Index < 3; Index++)
    {
        if (!DspTopologyNamePresent(Names[Index]))
            continue;
        Selection->Source = DspTopologySourceDsd;
        Selection->Name = Names[Index];
        Selection->Property = Properties[Index];
        return;
    }
}

VOID
DspTopologySelect(
    const DSP_TOPOLOGY_EVIDENCE* Evidence,
    DSP_TOPOLOGY_SELECTION* Selection)
{
    DspTopologySelectWith(Evidence, Evidence->TestTable != NULL ? Evidence->TestTable : DspTopologyQuirks, Selection);
}

BOOLEAN
DspTopologyNameValid(
    const char* Name)
{
    SIZE_T Index;

    if (!DspTopologyNamePresent(Name))
        return FALSE;
    for (Index = 0; Name[Index] != 0; Index++)
    {
        if (Index + 1 >= DSP_TOPOLOGY_NAME_BYTES)
            return FALSE;
        if (!((Name[Index] >= 'A' && Name[Index] <= 'Z') || (Name[Index] >= 'a' && Name[Index] <= 'z') ||
              (Name[Index] >= '0' && Name[Index] <= '9') || Name[Index] == '.' || Name[Index] == '_' || Name[Index] == '-'))
            return FALSE;
    }
    /* "." and ".." name directories */
    return Name[0] != '.' || (Name[1] != 0 && !(Name[1] == '.' && Name[2] == 0));
}

const char*
DspTopologySourceName(
    DSP_TOPOLOGY_SOURCE Source)
{
    static const char* const Names[] =
    {
        "none", "the registry override", "the DMI quirk table", "the _DSD name"
    };

    if ((UINT32)Source >= sizeof(Names) / sizeof(Names[0]))
        return "unknown";
    return Names[Source];
}
