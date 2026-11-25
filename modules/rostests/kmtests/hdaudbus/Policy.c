/*
 * PROJECT:     LiberNT kernel-mode tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     The audio path follows the override, the DMI quirk table, the codecs, the DSP capability and the NHLT endpoints; the topology file follows the override, the quirk table and the _DSD names
 * COPYRIGHT:   Copyright 2026 LiberNT contributors
 */

#include <kmt_test.h>
#include <policy.h>

#define NDEBUG
#include <debug.h>

#include "dsp_cases.h"
#include "dmi_cases.h"

static
VOID
CheckPolicy(VOID)
{
    ULONG Index;

    for (Index = 0; Index < ARRAYSIZE(PolicyCases); Index++)
    {
        const POLICY_CASE* Case = &PolicyCases[Index];
        DSP_AUDIO_PATH_EVIDENCE Evidence;
        DSP_DMI_STRINGS Dmi;
        DSP_AUDIO_PATH Path;

        RtlZeroMemory(&Dmi, sizeof(Dmi));
        if (Case->SysVendor != NULL)
        {
            strcpy(Dmi.Strings[DSP_DMI_SYS_VENDOR], Case->SysVendor);
            Dmi.Present |= 1UL << DSP_DMI_SYS_VENDOR;
        }
        if (Case->SysProduct != NULL)
        {
            strcpy(Dmi.Strings[DSP_DMI_SYS_PRODUCT], Case->SysProduct);
            Dmi.Present |= 1UL << DSP_DMI_SYS_PRODUCT;
        }

        RtlZeroMemory(&Evidence, sizeof(Evidence));
        Evidence.CodecMask = Case->CodecMask;
        Evidence.NonHdaEndpoints = Case->NonHdaEndpoints;
        Evidence.DspPresent = Case->DspPresent;
        Evidence.NhltTableValid = Case->NhltTableValid;
        Evidence.Dmi = (Case->SysVendor != NULL || Case->SysProduct != NULL) ? &Dmi : NULL;
        Evidence.OverrideSet = Case->Override >= 0;
        Evidence.Override = Case->Override > 0 ? (DSP_AUDIO_PATH)Case->Override : DspAudioPathNone;

        Path = Case->TestTable ? DspAudioPathDecideWith(&Evidence, DspTestQuirks) : DspAudioPathDecide(&Evidence);
        ok_eq_int(Path, Case->Expected);
        ok_eq_str(DspAudioPathName(Path), Case->ExpectedName);
    }
}

static
VOID
CheckDmiTables(VOID)
{
    ULONG Index, Field;
    DSP_DMI_STRINGS Strings;

    for (Index = 0; Index < ARRAYSIZE(DmiTableCases); Index++)
    {
        const DMI_TABLE_CASE* Case = &DmiTableCases[Index];

        trace("%s\n", Case->Name);
        RtlFillMemory(&Strings, sizeof(Strings), 0xEE);
        ok_eq_int(DmiParseRawSMBiosTables(Case->Raw, Case->Size, &Strings), Case->Status);
        ok_eq_uint(Strings.Present, Case->Present);
        ok_eq_uint(Strings.Truncated, Case->Truncated);
        ok_eq_uint(Strings.SmbiosMajorVersion, Case->Size >= 8 ? 3 : 0);
        ok_eq_uint(Strings.DmiRevision, Case->Size >= 8 ? 2 : 0);
        for (Field = 0; Field < DSP_DMI_STRING_COUNT; Field++)
        {
            ok_eq_uint(Strings.Strings[Field][DSP_DMI_STRING_BYTES - 1], 0);
        }
    }

    RtlFillMemory(&Strings, sizeof(Strings), 0xEE);
    ok_eq_int(DmiParseRawSMBiosTables(DmiTestTable, sizeof(DmiTestTable), &Strings), DmiOk);
    for (Index = 0; Index < ARRAYSIZE(DmiTestFields); Index++)
    {
        ok_eq_str(Strings.Strings[DmiTestFields[Index].Field], DmiTestFields[Index].Expected);
    }
    ok(DspAudioPathDmiMatch(&Strings) == NULL, "the driver's quirk table names no machine yet\n");
    ok(DspAudioPathDmiLookup(&Strings, DspTestQuirks) == NULL, "the test table does not name the quirk\n");
}

static
VOID
CheckDmiLookup(VOID)
{
    ULONG Index;
    DSP_DMI_STRINGS Strings;
    const DSP_DMI_QUIRK* Match;
    DSP_AUDIO_PATH Path;

    for (Index = 0; Index < ARRAYSIZE(DmiLookupCases); Index++)
    {
        const DMI_LOOKUP_CASE* Case = &DmiLookupCases[Index];

        RtlZeroMemory(&Strings, sizeof(Strings));
        if (Case->SysVendor != NULL)
        {
            strcpy(Strings.Strings[DSP_DMI_SYS_VENDOR], Case->SysVendor);
            Strings.Present |= 1UL << DSP_DMI_SYS_VENDOR;
        }
        if (Case->SysProduct != NULL)
        {
            strcpy(Strings.Strings[DSP_DMI_SYS_PRODUCT], Case->SysProduct);
            Strings.Present |= 1UL << DSP_DMI_SYS_PRODUCT;
        }
        Strings.Truncated = Case->Truncated ? 0xFF : 0;
        Match = DspAudioPathDmiLookup(&Strings, DspTestQuirks);
        ok(Match == NULL ? Case->Expected < 0 : Match - DspTestQuirks == Case->Expected,
           "lookup case %lu: quirk %ld\n", Index, Match == NULL ? -1 : (LONG)(Match - DspTestQuirks));
        ok(DspAudioPathDmiLookup(&Strings, DspEmptyQuirks) == NULL, "an empty table matches nothing\n");
    }

    ok(DspAudioPathDmiLookup(NULL, DspTestQuirks) == NULL, "no strings match nothing\n");
    ok_eq_str(DmiStatusName(DmiOk), "ok");
    ok_eq_str(DmiStatusName(DmiNoData), "no data");
    ok_eq_str(DmiStatusName(DmiBadLength), "bad length");
    ok_eq_str(DmiStatusName(DmiTruncated), "truncated");
    ok_eq_str(DmiStatusName((DSP_DMI_STATUS)4), "unknown");

    ok(DspAudioPathParseName("LegacyHdaOnly", &Path), "LegacyHdaOnly parses\n");
    ok_eq_int(Path, DspAudioPathLegacyHdaOnly);
    ok(DspAudioPathParseName("DspOptional", &Path), "DspOptional parses\n");
    ok_eq_int(Path, DspAudioPathDspOptional);
    ok(DspAudioPathParseName("DspRequired", &Path), "DspRequired parses\n");
    ok_eq_int(Path, DspAudioPathDspRequired);
    ok(DspAudioPathParseName("None", &Path), "None parses\n");
    ok_eq_int(Path, DspAudioPathNone);
    ok(!DspAudioPathParseName("legacyhdaonly", &Path), "the names compare exactly\n");
    ok(!DspAudioPathParseName("", &Path), "an empty value is no override\n");
    ok(!DspAudioPathParseName(NULL, &Path), "no value is no override\n");
}

static
BOOLEAN
TextIgnoreCaseEqual(
    const char* Left,
    const char* Right)
{
    ULONG Index = 0;
    UCHAR L, R;

    do
    {
        L = (UCHAR)Left[Index];
        R = (UCHAR)Right[Index];
        if (L >= 'A' && L <= 'Z')
            L += 'a' - 'A';
        if (R >= 'A' && R <= 'Z')
            R += 'a' - 'A';
        Index++;
    } while (L != 0 && L == R);
    return L == R;
}

/* The row of the test table a case's strings match, by the table's own rules: -1 for none */
static
LONG
TestTableQuirk(
    const TOPOLOGY_CASE* Case)
{
    if (Case->Truncated)
        return -1;
    if (Case->SysVendor != NULL && TextIgnoreCaseEqual(Case->SysVendor, "vendor a") &&
        Case->SysProduct != NULL && TextIgnoreCaseEqual(Case->SysProduct, "product a"))
        return 0;
    if (Case->SysVendor != NULL && TextIgnoreCaseEqual(Case->SysVendor, "vendor b"))
        return 1;
    if (Case->SysProduct != NULL && TextIgnoreCaseEqual(Case->SysProduct, "product c"))
        return 2;
    return -1;
}

static
VOID
CheckTopology(VOID)
{
    ULONG Index;
    DSP_DMI_STRINGS Strings;
    const DSP_TOPOLOGY_QUIRK* Match;

    for (Index = 0; Index < ARRAYSIZE(TopologyCases); Index++)
    {
        const TOPOLOGY_CASE* Case = &TopologyCases[Index];
        DSP_TOPOLOGY_EVIDENCE Evidence;
        DSP_TOPOLOGY_SELECTION Selection;
        LONG ExpectedQuirk;

        RtlZeroMemory(&Strings, sizeof(Strings));
        if (Case->SysVendor != NULL)
        {
            strcpy(Strings.Strings[DSP_DMI_SYS_VENDOR], Case->SysVendor);
            Strings.Present |= 1UL << DSP_DMI_SYS_VENDOR;
        }
        if (Case->SysProduct != NULL)
        {
            strcpy(Strings.Strings[DSP_DMI_SYS_PRODUCT], Case->SysProduct);
            Strings.Present |= 1UL << DSP_DMI_SYS_PRODUCT;
        }
        Strings.Truncated = Case->Truncated ? 0xFF : 0;

        RtlZeroMemory(&Evidence, sizeof(Evidence));
        Evidence.Override = Case->Override;
        Evidence.Dmi = (Case->SysVendor != NULL || Case->SysProduct != NULL) ? &Strings : NULL;
        Evidence.SpeakerTplg = Case->SpeakerTplg;
        Evidence.HpTplg = Case->HpTplg;
        Evidence.MicTplg = Case->MicTplg;
        Evidence.TestTable = Case->TestTable ? DspTestTopologyQuirks : NULL;

        Selection.Source = DspTopologySourceOverride;
        Selection.Name = "stale";
        Selection.Property = "stale";
        DspTopologySelect(&Evidence, &Selection);
        ok_eq_int(Selection.Source, Case->Expected);
        ok(Selection.Name != NULL ? (Case->ExpectedName != NULL && strcmp(Selection.Name, Case->ExpectedName) == 0) : Case->ExpectedName == NULL,
           "topology case %lu \"%s\": name %s\n", Index, Case->Name, Selection.Name != NULL ? Selection.Name : "(none)");
        ok(Selection.Property != NULL ? (Case->ExpectedProperty != NULL && strcmp(Selection.Property, Case->ExpectedProperty) == 0) : Case->ExpectedProperty == NULL,
           "topology case %lu \"%s\": property %s\n", Index, Case->Name, Selection.Property != NULL ? Selection.Property : "(none)");
        if (Case->ExpectedName != NULL)
            ok_eq_int(DspTopologyNameValid(Selection.Name) ? 1 : 0, Case->NameValid);

        Match = DspTopologyDmiLookup(Evidence.Dmi, DspEmptyTopologyQuirks);
        ok(Match == NULL, "topology case %lu \"%s\": the empty table matched\n", Index, Case->Name);
        ExpectedQuirk = TestTableQuirk(Case);
        Match = DspTopologyDmiLookup(Evidence.Dmi, DspTestTopologyQuirks);
        ok(Match != NULL ? (LONG)(Match - DspTestTopologyQuirks) == ExpectedQuirk : ExpectedQuirk == -1,
           "topology case %lu \"%s\": quirk %ld\n", Index, Case->Name, Match != NULL ? (LONG)(Match - DspTestTopologyQuirks) : -1);
    }

    for (Index = 0; Index < ARRAYSIZE(TopologyNameCases); Index++)
        ok_eq_int(DspTopologyNameValid(TopologyNameCases[Index].Override) ? 1 : 0, TopologyNameCases[Index].NameValid);
    ok(DspTopologyDmiLookup(NULL, DspTestTopologyQuirks) == NULL, "no strings match no machine\n");
    ok(!DspTopologyNameValid(NULL), "no name is not a file name\n");
    ok_eq_str(DspTopologySourceName(DspTopologySourceNone), "none");
    ok_eq_str(DspTopologySourceName(DspTopologySourceOverride), "the registry override");
    ok_eq_str(DspTopologySourceName(DspTopologySourceQuirk), "the DMI quirk table");
    ok_eq_str(DspTopologySourceName(DspTopologySourceDsd), "the _DSD name");
    ok_eq_str(DspTopologySourceName((DSP_TOPOLOGY_SOURCE)4), "unknown");
}

START_TEST(Policy)
{
    CheckPolicy();
    CheckDmiTables();
    CheckDmiLookup();
    CheckTopology();

    ok_eq_uint(DspAudioPathNone, 0);
    ok_eq_str(DspAudioPathName(DspAudioPathNone), "None");
    ok_eq_str(DspAudioPathName(DspAudioPathLegacyHdaOnly), "LegacyHdaOnly");
    ok_eq_str(DspAudioPathName(DspAudioPathDspOptional), "DspOptional");
    ok_eq_str(DspAudioPathName(DspAudioPathDspRequired), "DspRequired");
    ok_eq_str(DspAudioPathName((DSP_AUDIO_PATH)4), "Unknown");
}
