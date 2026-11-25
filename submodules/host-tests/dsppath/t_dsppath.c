/*
 * PROJECT:     LiberNT host-native tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Runs the NHLT parser, the audio path decision, the topology file selection and the stream buffer's descriptor list of hdaudbus over the shared case tables
 * COPYRIGHT:   Copyright 2026 LiberNT contributors
 */

#include <stdio.h>
#include <string.h>

#include "shim.h"

#include <nhlt_parse.h>
#include <policy.h>
#include <hdac_bdl.h>

#include "dsp_cases.h"
#include "dmi_cases.h"

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
    CHECK_EQ(Summary.FormatsCount, Case->FormatsCount);
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
        fprintf(stderr, "%s:%d: FAIL: the table of endpoint case %u did not parse\n", __FILE__, __LINE__, Case->TableIndex);
        ++Checks;
        ++Failures;
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
    CHECK_EQ(Endpoint->DeviceConfigSize, Case->DeviceConfigSize);
    CHECK_EQ(Endpoint->FormatsCount, Case->FormatsCount);
    CHECK_EQ(Endpoint->DevicesCount, Case->DevicesCount);
}

static
void
check_view(const NHLT_VIEW_CASE* Case)
{
    const NHLT_TABLE_CASE* Table = &NhltTableCases[Case->TableIndex];
    NHLT_ENDPOINT_VIEW View;

    if (NhltEndpointView(Table->Table, Table->Size, (UINT8)Case->EndpointIndex, &View) != Case->Status)
        fprintf(stderr, "  in view case table %u endpoint %u\n", Case->TableIndex, Case->EndpointIndex);
    CHECK_EQ(NhltEndpointView(Table->Table, Table->Size, (UINT8)Case->EndpointIndex, &View), Case->Status);
    CHECK_EQ(View.DeviceConfigSize, Case->DeviceConfigSize);
    CHECK_EQ(View.FormatsCount, Case->FormatsCount);
    CHECK_EQ(View.DevicesCount, Case->DevicesCount);
    CHECK_EQ(View.HasDevicesInfo, Case->HasDevicesInfo);
    CHECK_EQ(View.Endpoint != NULL, Case->Located);
    if (View.Endpoint != NULL)
        CHECK_EQ(View.EndpointLength,
            (SIZE_T)View.Endpoint[0] | ((SIZE_T)View.Endpoint[1] << 8) | ((SIZE_T)View.Endpoint[2] << 16) |
            ((SIZE_T)View.Endpoint[3] << 24));
    if (View.DeviceConfig != NULL)
        CHECK(View.DeviceConfig == View.Endpoint + 23);
}

static
void
check_format(const NHLT_FORMAT_CASE* Case)
{
    const NHLT_TABLE_CASE* Table = &NhltTableCases[Case->TableIndex];
    NHLT_ENDPOINT_VIEW View;
    NHLT_FORMAT Format;
    static const UINT8 Pcm[16] =
    {
        0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x10, 0x00, 0x80, 0x00, 0x00, 0xAA, 0x00, 0x38, 0x9B, 0x71
    };
    unsigned int Byte;

    if (NhltEndpointView(Table->Table, Table->Size, (UINT8)Case->EndpointIndex, &View) == NhltParseNotFound)
    {
        fprintf(stderr, "%s:%d: FAIL: the endpoint of format case %u/%u was not found\n", __FILE__, __LINE__,
            Case->TableIndex, Case->EndpointIndex);
        ++Checks;
        ++Failures;
        return;
    }
    memset(&Format, 0xAA, sizeof(Format));
    if (NhltEndpointFormat(&View, (UINT8)Case->FormatIndex, &Format) != Case->Status)
        fprintf(stderr, "  in format case %u/%u/%u\n", Case->TableIndex, Case->EndpointIndex, Case->FormatIndex);
    CHECK_EQ(NhltEndpointFormat(&View, (UINT8)Case->FormatIndex, &Format), Case->Status);
    if (Case->Status != NhltParseOk)
        return;

    CHECK_EQ(Format.FormatTag, Case->FormatTag);
    CHECK_EQ(Format.ChannelCount, Case->ChannelCount);
    CHECK_EQ(Format.SamplesPerSec, Case->SamplesPerSec);
    CHECK_EQ(Format.AvgBytesPerSec, Case->AvgBytesPerSec);
    CHECK_EQ(Format.BlockAlign, Case->BlockAlign);
    CHECK_EQ(Format.BitsPerSample, Case->BitsPerSample);
    CHECK_EQ(Format.ExtraFormatSize, Case->ExtraFormatSize);
    CHECK_EQ(Format.ValidBitsPerSample, Case->ValidBitsPerSample);
    CHECK_EQ(Format.ChannelMask, Case->ChannelMask);
    CHECK_EQ(Format.ConfigSize, Case->ConfigSize);
    for (Byte = 0; Byte < 16; Byte++)
        CHECK_EQ(Format.Subformat[Byte] == Pcm[Byte], Case->IsPcm);
    if (Case->ConfigSize != 0)
        CHECK_EQ(Format.Config[0], Case->ConfigFirst);
}

static
void
check_device(const NHLT_DEVICE_CASE* Case)
{
    const NHLT_TABLE_CASE* Table = &NhltTableCases[Case->TableIndex];
    NHLT_ENDPOINT_VIEW View;
    NHLT_DEVICE Device;
    unsigned int Byte;

    if (NhltEndpointView(Table->Table, Table->Size, (UINT8)Case->EndpointIndex, &View) == NhltParseNotFound)
    {
        fprintf(stderr, "%s:%d: FAIL: the endpoint of device case %u/%u was not found\n", __FILE__, __LINE__,
            Case->TableIndex, Case->EndpointIndex);
        ++Checks;
        ++Failures;
        return;
    }
    memset(&Device, 0xAA, sizeof(Device));
    CHECK_EQ(NhltEndpointDevice(&View, (UINT8)Case->DeviceIndex, &Device), Case->Status);
    if (Case->Status != NhltParseOk)
        return;

    for (Byte = 0; Byte < 4; Byte++)
        CHECK_EQ(Device.Id[Byte], Case->Id[Byte]);
    for (Byte = 4; Byte < NHLT_DEVICE_ID_BYTES; Byte++)
        CHECK_EQ(Device.Id[Byte], 0);
    CHECK_EQ(Device.InstanceId, Case->InstanceId);
    CHECK_EQ(Device.PortId, Case->PortId);
}

static
void
check_link(const NHLT_LINK_CASE* Case)
{
    const NHLT_TABLE_CASE* Table = &NhltTableCases[Case->TableIndex];

    CHECK_EQ(NhltHasLinkType(Table->Table, Table->Size, Case->LinkType) ? 1 : 0, Case->Present);
    if (Case->LinkType == NHLT_LINKTYPE_SSP)
        CHECK_EQ(NhltSspEndpointMask(Table->Table, Table->Size), Case->SspMask);
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
    DSP_DMI_STRINGS Dmi;
    DSP_AUDIO_PATH Path;

    memset(&Dmi, 0, sizeof(Dmi));
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

    memset(&Evidence, 0, sizeof(Evidence));
    Evidence.CodecMask = Case->CodecMask;
    Evidence.NonHdaEndpoints = Case->NonHdaEndpoints;
    Evidence.DspPresent = Case->DspPresent;
    Evidence.NhltTableValid = Case->NhltTableValid;
    Evidence.Dmi = (Case->SysVendor != NULL || Case->SysProduct != NULL) ? &Dmi : NULL;
    Evidence.OverrideSet = Case->Override >= 0;
    Evidence.Override = Case->Override > 0 ? (DSP_AUDIO_PATH)Case->Override : DspAudioPathNone;

    Path = Case->TestTable ? DspAudioPathDecideWith(&Evidence, DspTestQuirks) : DspAudioPathDecide(&Evidence);
    CHECK_EQ(Path, Case->Expected);
    CHECK_STR(DspAudioPathName(Path), Case->ExpectedName);
}

static
void
check_dmi_tables(void)
{
    unsigned int Index, Field, FailuresBefore;

    for (Index = 0; Index < sizeof(DmiTableCases) / sizeof(DmiTableCases[0]); Index++)
    {
        const DMI_TABLE_CASE* Case = &DmiTableCases[Index];
        DSP_DMI_STRINGS Strings;

        FailuresBefore = Failures;
        memset(&Strings, 0xEE, sizeof(Strings));
        CHECK_EQ(DmiParseRawSMBiosTables(Case->Raw, Case->Size, &Strings), Case->Status);
        CHECK_EQ(Strings.Present, Case->Present);
        CHECK_EQ(Strings.Truncated, Case->Truncated);
        CHECK_EQ(Strings.SmbiosMajorVersion, Case->Size >= 8 ? 3 : 0);
        CHECK_EQ(Strings.SmbiosMinorVersion, Case->Size >= 8 ? 3 : 0);
        CHECK_EQ(Strings.DmiRevision, Case->Size >= 8 ? 2 : 0);
        for (Field = 0; Field < DSP_DMI_STRING_COUNT; Field++)
        {
            CHECK_EQ(Strings.Strings[Field][DSP_DMI_STRING_BYTES - 1], 0);
            if (!(Strings.Present & (1UL << Field)))
                CHECK_EQ(Strings.Strings[Field][0], 0);
        }
        if (Failures != FailuresBefore)
            fprintf(stderr, "  in DMI table case \"%s\"\n", Case->Name);
    }

    {
        DSP_DMI_STRINGS Strings;

        memset(&Strings, 0xEE, sizeof(Strings));
        CHECK_EQ(DmiParseRawSMBiosTables(DmiTestTable, sizeof(DmiTestTable), &Strings), DmiOk);
        for (Index = 0; Index < sizeof(DmiTestFields) / sizeof(DmiTestFields[0]); Index++)
            CHECK_STR(Strings.Strings[DmiTestFields[Index].Field], DmiTestFields[Index].Expected);
        CHECK_EQ(DspAudioPathDmiLookup(&Strings, DspTestQuirks), NULL);
        CHECK_EQ(DspAudioPathDmiLookup(&Strings, DspEmptyQuirks), NULL);
        CHECK_EQ(DspAudioPathDmiMatch(&Strings), NULL);

        memset(&Strings, 0xEE, sizeof(Strings));
        CHECK_EQ(DmiParseRawSMBiosTables(DmiSingleNulTable, sizeof(DmiSingleNulTable), &Strings), DmiOk);
        CHECK_STR(Strings.Strings[DSP_DMI_SYS_VENDOR], "VENDORX");
        CHECK_STR(Strings.Strings[DSP_DMI_SYS_PRODUCT], "PRODUCT 1");
        CHECK_STR(Strings.Strings[DSP_DMI_SYS_VERSION], "");

        memset(&Strings, 0xEE, sizeof(Strings));
        CHECK_EQ(DmiParseRawSMBiosTables(DmiLongVendorTable, sizeof(DmiLongVendorTable), &Strings), DmiOk);
        CHECK_EQ(strlen(Strings.Strings[DSP_DMI_SYS_VENDOR]), DSP_DMI_STRING_BYTES - 1);
        for (Index = 0; Index < DSP_DMI_STRING_BYTES - 1; Index++)
            CHECK_EQ(Strings.Strings[DSP_DMI_SYS_VENDOR][Index], 'X');
        CHECK_EQ(DspAudioPathDmiLookup(&Strings, DspTestQuirks), NULL);
    }
}

static
void
check_dmi_lookup(void)
{
    unsigned int Index, FailuresBefore;
    DSP_DMI_STRINGS Strings;
    const DSP_DMI_QUIRK* Match;

    for (Index = 0; Index < sizeof(DmiLookupCases) / sizeof(DmiLookupCases[0]); Index++)
    {
        const DMI_LOOKUP_CASE* Case = &DmiLookupCases[Index];

        FailuresBefore = Failures;
        memset(&Strings, 0, sizeof(Strings));
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
        CHECK_EQ(Match == NULL ? -1 : (int)(Match - DspTestQuirks), Case->Expected);
        CHECK_EQ(DspAudioPathDmiLookup(&Strings, DspEmptyQuirks), NULL);
        if (Failures != FailuresBefore)
            fprintf(stderr, "  in DMI lookup case %u\n", Index);
    }

    CHECK_EQ(DspAudioPathDmiLookup(NULL, DspTestQuirks), NULL);
    CHECK_STR(DmiStatusName(DmiOk), "ok");
    CHECK_STR(DmiStatusName(DmiNoData), "no data");
    CHECK_STR(DmiStatusName(DmiBadLength), "bad length");
    CHECK_STR(DmiStatusName(DmiTruncated), "truncated");
    CHECK_STR(DmiStatusName((DSP_DMI_STATUS)4), "unknown");

    {
        DSP_AUDIO_PATH Path;

        CHECK_EQ(DspAudioPathParseName("LegacyHdaOnly", &Path), 1);
        CHECK_EQ(Path, DspAudioPathLegacyHdaOnly);
        CHECK_EQ(DspAudioPathParseName("DspOptional", &Path), 1);
        CHECK_EQ(Path, DspAudioPathDspOptional);
        CHECK_EQ(DspAudioPathParseName("DspRequired", &Path), 1);
        CHECK_EQ(Path, DspAudioPathDspRequired);
        CHECK_EQ(DspAudioPathParseName("None", &Path), 1);
        CHECK_EQ(Path, DspAudioPathNone);
        CHECK_EQ(DspAudioPathParseName("legacyhdaonly", &Path), 0);
        CHECK_EQ(DspAudioPathParseName("LegacyHdaOnly ", &Path), 0);
        CHECK_EQ(DspAudioPathParseName("", &Path), 0);
        CHECK_EQ(DspAudioPathParseName(NULL, &Path), 0);
    }
}

static
int
TextIgnoreCaseEqual(
    const char* Left,
    const char* Right)
{
    unsigned int Index = 0;
    int L, R;

    do
    {
        L = (unsigned char)Left[Index];
        R = (unsigned char)Right[Index];
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
int
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
void
check_topology(const TOPOLOGY_CASE* Case)
{
    DSP_TOPOLOGY_EVIDENCE Evidence;
    DSP_TOPOLOGY_SELECTION Selection;
    DSP_DMI_STRINGS Dmi;
    const DSP_TOPOLOGY_QUIRK* Match;
    int ExpectedQuirk;
    unsigned int FailuresBefore = Failures;

    memset(&Dmi, 0, sizeof(Dmi));
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
    Dmi.Truncated = Case->Truncated ? 0xFF : 0;

    memset(&Evidence, 0, sizeof(Evidence));
    Evidence.Override = Case->Override;
    Evidence.Dmi = (Case->SysVendor != NULL || Case->SysProduct != NULL) ? &Dmi : NULL;
    Evidence.SpeakerTplg = Case->SpeakerTplg;
    Evidence.HpTplg = Case->HpTplg;
    Evidence.MicTplg = Case->MicTplg;
    Evidence.TestTable = Case->TestTable ? DspTestTopologyQuirks : NULL;

    /* an unselected name stays so whatever was in the selection before */
    Selection.Source = DspTopologySourceOverride;
    Selection.Name = "stale";
    Selection.Property = "stale";
    DspTopologySelect(&Evidence, &Selection);
    CHECK_EQ(Selection.Source, Case->Expected);
    CHECK_EQ(Selection.Name != NULL ? strcmp(Selection.Name, Case->ExpectedName != NULL ? Case->ExpectedName : "") : -1,
        Case->ExpectedName != NULL ? 0 : -1);
    CHECK_EQ(Selection.Property != NULL ? strcmp(Selection.Property, Case->ExpectedProperty != NULL ? Case->ExpectedProperty : "") : -1,
        Case->ExpectedProperty != NULL ? 0 : -1);
    if (Case->ExpectedName != NULL)
    {
        /* the name is the evidence's own text, used as it reads */
        if (Case->Expected == 1)
            CHECK(Selection.Name == Case->Override);
        if (Case->Expected == 3)
            CHECK(Selection.Name == Case->SpeakerTplg || Selection.Name == Case->HpTplg || Selection.Name == Case->MicTplg);
        CHECK_EQ(DspTopologyNameValid(Selection.Name), Case->NameValid);
    }
    /* the empty table matches no machine; the test table names three machines, the second by vendor only and the third by product only */
    Match = DspTopologyDmiLookup(Evidence.Dmi, DspEmptyTopologyQuirks);
    CHECK(Match == NULL);
    ExpectedQuirk = TestTableQuirk(Case);
    Match = DspTopologyDmiLookup(Evidence.Dmi, DspTestTopologyQuirks);
    CHECK_EQ(Match != NULL ? Match - DspTestTopologyQuirks : -1, ExpectedQuirk);
    if (Failures != FailuresBefore)
        fprintf(stderr, "  in topology case \"%s\"\n", Case->Name);
}

static
void
check_topology_names(void)
{
    unsigned int Index;
    unsigned int FailuresBefore;

    for (Index = 0; Index < sizeof(TopologyNameCases) / sizeof(TopologyNameCases[0]); Index++)
    {
        FailuresBefore = Failures;
        check_topology(&TopologyNameCases[Index]);
        CHECK_EQ(DspTopologyNameValid(TopologyNameCases[Index].Override), TopologyNameCases[Index].NameValid);
        if (Failures != FailuresBefore)
            fprintf(stderr, "  in topology name case \"%s\"\n", TopologyNameCases[Index].Name);
    }
    CHECK(!DspTopologyNameValid(NULL));
    CHECK(!DspTopologyNameValid(""));
    CHECK(DspTopologyNameValid("a"));
    CHECK(DspTopologyNameValid("A0._-"));
    CHECK_STR(DspTopologySourceName(DspTopologySourceNone), "none");
    CHECK_STR(DspTopologySourceName(DspTopologySourceOverride), "the registry override");
    CHECK_STR(DspTopologySourceName(DspTopologySourceQuirk), "the DMI quirk table");
    CHECK_STR(DspTopologySourceName(DspTopologySourceDsd), "the _DSD name");
    CHECK_STR(DspTopologySourceName((DSP_TOPOLOGY_SOURCE)4), "unknown");
}

static
UINT32
RandomStep(
    UINT32* State)
{
    UINT32 Value = *State;

    Value ^= Value << 13;
    Value ^= Value >> 17;
    Value ^= Value << 5;
    *State = Value;
    return Value;
}

static
void
exercise(
    const UINT8* Table,
    SIZE_T Size)
{
    NHLT_SUMMARY Summary;
    NHLT_ENDPOINT_VIEW View;
    NHLT_FORMAT Format;
    NHLT_DEVICE Device;
    UINT8 Link, Index;

    NhltParseTable(Table, Size, &Summary);
    NhltSspEndpointMask(Table, Size);
    for (Link = 0; Link < NHLT_LINK_TYPE_COUNT + 2; Link++)
        NhltHasLinkType(Table, Size, Link);
    for (Index = 0; Index < 3; Index++)
    {
        if (NhltEndpointView(Table, Size, Index, &View) != NhltParseNotFound && View.Endpoint != NULL)
        {
            NhltEndpointFormat(&View, 0, &Format);
            NhltEndpointFormat(&View, 1, &Format);
            NhltEndpointFormat(&View, 255, &Format);
            NhltEndpointDevice(&View, 0, &Device);
            NhltEndpointDevice(&View, 2, &Device);
            NhltEndpointDevice(&View, 255, &Device);
        }
    }
}

static
void
check_dmi_garbage(void)
{
    static const UINT32 Seeds[] = { 0x12345678, 0x9E3779B9, 0xDEADBEEF, 0xCAFEBABE };
    unsigned char Buffer[300];
    const DMI_TABLE_CASE* Case;
    DSP_DMI_STRINGS Strings;
    SIZE_T Length;
    unsigned int Seed, Iteration, T, Byte, Index;
    UINT32 State, Declared;
    int Status;

    for (T = 0; T < sizeof(DmiTableCases) / sizeof(DmiTableCases[0]); T++)
    {
        Case = &DmiTableCases[T];
        if (Case->Raw == NULL)
            continue;
        for (Length = 0; Length <= Case->Size; Length++)
        {
            Status = DmiParseRawSMBiosTables(Case->Raw, Length, &Strings);
            CHECK(Status >= DmiOk && Status <= DmiTruncated);
            CHECK(Strings.Present <= 0xFF && Strings.Truncated <= 0xFF);
            for (Index = 0; Index < DSP_DMI_STRING_COUNT; Index++)
                CHECK_EQ(Strings.Strings[Index][DSP_DMI_STRING_BYTES - 1], 0);
        }
        for (Byte = 0; Byte < Case->Size; Byte++)
        {
            memcpy(Buffer, Case->Raw, Case->Size);
            Buffer[Byte] ^= 0x5A;
            Status = DmiParseRawSMBiosTables(Buffer, Case->Size, &Strings);
            CHECK(Status >= DmiOk && Status <= DmiTruncated);
            CHECK(Strings.Present <= 0xFF && Strings.Truncated <= 0xFF);
        }
    }

    for (Seed = 0; Seed < sizeof(Seeds) / sizeof(Seeds[0]); Seed++)
    {
        State = Seeds[Seed];
        for (Iteration = 0; Iteration < 4000; Iteration++)
        {
            Length = RandomStep(&State) % (sizeof(Buffer) + 1);
            for (Index = 0; Index < Length; Index++)
                Buffer[Index] = (unsigned char)RandomStep(&State);
            if (Length >= 8)
            {
                Declared = RandomStep(&State) % 4096;
                Buffer[4] = (unsigned char)Declared;
                Buffer[5] = (unsigned char)(Declared >> 8);
                Buffer[6] = (unsigned char)(Declared >> 16);
                Buffer[7] = (unsigned char)(Declared >> 24);
                if (Iteration & 1)
                {
                    Buffer[8] = 1;
                    Buffer[9] = 0x1B;
                }
            }
            Status = DmiParseRawSMBiosTables(Buffer, Length, &Strings);
            CHECK(Status >= DmiOk && Status <= DmiTruncated);
            CHECK(Strings.Present <= 0xFF && Strings.Truncated <= 0xFF);
            for (Index = 0; Index < DSP_DMI_STRING_COUNT; Index++)
                CHECK_EQ(Strings.Strings[Index][DSP_DMI_STRING_BYTES - 1], 0);
        }
    }
}

static
void
check_short_and_garbage_inputs(void)
{
    static const UINT32 Seeds[] = { 0x12345678, 0x9E3779B9, 0xDEADBEEF, 0xCAFEBABE };
    unsigned char Buffer[512];
    const NHLT_TABLE_CASE* Table;
    SIZE_T Length;
    unsigned int Seed, Iteration, T, Index, Byte;
    UINT32 State, Value;
    NHLT_SUMMARY Summary;
    NHLT_ENDPOINT_VIEW View;

    for (T = 0; T < sizeof(NhltTableCases) / sizeof(NhltTableCases[0]); T++)
    {
        Table = &NhltTableCases[T];
        if (Table->Table == NULL)
            continue;
        for (Length = 0; Length <= Table->Size; Length++)
        {
            int Status = NhltParseTable(Table->Table, Length, &Summary);

            CHECK(Status == NhltParseNoTable || Status == NhltParseTruncated || Status == NhltParseBadSignature ||
                  Status == NhltParseBadLength || Status == NhltParseOk);
            CHECK(Summary.EndpointsParsed <= Summary.EndpointsCount);
            CHECK(Summary.ParsedLength <= Summary.HeaderLength);
            exercise(Table->Table, Length);
        }
        for (Byte = 0; Byte < Table->Size; Byte++)
        {
            memcpy(Buffer, Table->Table, Table->Size);
            Buffer[Byte] ^= 0x5A;
            exercise(Buffer, Table->Size);
        }
    }

    for (Seed = 0; Seed < sizeof(Seeds) / sizeof(Seeds[0]); Seed++)
    {
        State = Seeds[Seed];
        for (Iteration = 0; Iteration < 3000; Iteration++)
        {
            Length = 37 + (RandomStep(&State) % (sizeof(Buffer) - 37));
            Buffer[0] = 'N';
            Buffer[1] = 'H';
            Buffer[2] = 'L';
            Buffer[3] = 'T';
            for (Index = 4; Index < Length; Index++)
                Buffer[Index] = (unsigned char)RandomStep(&State);
            Value = 37 + RandomStep(&State) % (Length - 36);
            Buffer[4] = (unsigned char)Value;
            Buffer[5] = (unsigned char)(Value >> 8);
            Buffer[6] = (unsigned char)(Value >> 16);
            Buffer[7] = (unsigned char)(Value >> 24);
            exercise(Buffer, Length);
            exercise(Buffer, RandomStep(&State) % (Length + 1));

            Table = &NhltTableCases[Seed == 0 ? 1 : (Seed == 1 ? 13 : (Seed == 2 ? 10 : 2))];
            memcpy(Buffer, Table->Table, Table->Size);
            Length = Table->Size;
            for (Byte = 0; Byte < 3; Byte++)
                Buffer[RandomStep(&State) % Length] = (unsigned char)RandomStep(&State);
            exercise(Buffer, Length);
            NhltParseTable(Buffer, Length, &Summary);
            if (NhltEndpointView(Buffer, Length, 0, &View) == NhltParseOk)
                CHECK(View.EndpointLength <= Length && View.Endpoint >= Buffer && View.Endpoint + View.EndpointLength <= Buffer + Length);
        }
    }

    CHECK_EQ(NhltHasLinkType(NULL, 0, NHLT_LINKTYPE_PDM), 0);
    CHECK_EQ(NhltSspEndpointMask(NULL, 0), 0);
    CHECK_EQ(NhltEndpointView(NULL, 0, 0, &View), NhltParseNoTable);
    CHECK_EQ(NhltEndpointView(NhltEmptyTable, sizeof(NhltEmptyTable), 0, &View), NhltParseNotFound);
}

/* ---- The descriptor list of a stream's buffer (hdac_bdl.c) ---- */

/* One row: a request, and the layout and the list the driver must build; a descriptor is the page of the allocation it names, where in it, its bytes, and its interrupt on completion */
#define BDL_ROW_ENTRIES     8
#define BDL_ROW_PAGES       300
/* the frames the rows give the pages: their addresses cross the 4 GiB boundary, so the high word counts */
#define BDL_FIRST_FRAME     0x100000

typedef struct _BDL_ENTRY_CASE
{
    UINT32 Page;
    UINT32 Offset;
    UINT32 Bytes;
    UINT32 Ioc;
} BDL_ENTRY_CASE;

typedef struct _BDL_CASE
{
    const char* Name;
    ULONG NotificationCount;
    SIZE_T RequestedBytes;
    UINT32 FrameBytes;
    UINT32 BufferBytes;
    UINT32 HalfBytes;
    UINT32 OffsetBytes;
    UINT32 Pages;
    UINT32 Entries;
    BDL_ENTRY_CASE Entry[BDL_ROW_ENTRIES];
} BDL_CASE;

static const BDL_CASE BdlCases[] =
{
    /* the DSP child's self-test: 4096 frames of 16 bits in two channels, two notifications of 2048 frames, whole pages */
    { "the self-test at 16 bits", 2, 16384, 4, 16384, 8192, 0, 4, 4,
      { { 0, 0, 4096, 0 }, { 1, 0, 4096, 1 }, { 2, 0, 4096, 0 }, { 3, 0, 4096, 1 } } },
    /* 4096 frames of 24 or 32 valid bits in a 32-bit container: two notifications of four pages */
    { "the self-test at 24 and 32 bits", 2, 32768, 8, 32768, 16384, 0, 8, 8,
      { { 0, 0, 4096, 0 }, { 1, 0, 4096, 0 }, { 2, 0, 4096, 0 }, { 3, 0, 4096, 1 },
        { 4, 0, 4096, 0 }, { 5, 0, 4096, 0 }, { 6, 0, 4096, 0 }, { 7, 0, 4096, 1 } } },
    /* the codec path's: the audio engine's 8192 bytes at 48 kHz 16-bit stereo is two notifications of 1024 frames, a whole page each */
    { "the codec path, a half of whole pages", 2, 8192, 4, 8192, 4096, 0, 2, 2,
      { { 0, 0, 4096, 1 }, { 1, 0, 4096, 1 } } },
    /* the codec path's: the engine's 10 ms at 48 kHz 16-bit stereo, two notifications of 480 frames, padded to the page boundary as before */
    { "the codec path, a half that is not whole pages", 2, 3840, 4, 3840, 1920, 2176, 2, 2,
      { { 0, 2176, 1920, 1 }, { 1, 0, 1920, 1 } } },
    /* six channels of 16 bits: the frame is 12 bytes and the halves are multiples of 384 */
    { "six channels, a half between page boundaries", 2, 11520, 12, 11520, 5760, 2432, 4, 4,
      { { 0, 2432, 1664, 0 }, { 1, 0, 4096, 1 }, { 2, 0, 4096, 0 }, { 3, 0, 1664, 1 } } },
    /* 8000 bytes asked: the half of 4000 is rounded up to its 128-byte frame boundary, 4096, and so to whole pages */
    { "a half rounded up to a whole page", 2, 8000, 4, 8192, 4096, 0, 2, 2,
      { { 0, 0, 4096, 1 }, { 1, 0, 4096, 1 } } },
    /* the smallest half: two notifications of 128 bytes */
    { "the smallest half", 2, 256, 4, 256, 128, 3968, 2, 2,
      { { 0, 3968, 128, 1 }, { 1, 0, 128, 1 } } },
    /* the codec interface's AllocateDmaBuffer: one notification, the whole buffer one period, the interrupt at its end */
    { "one notification, whole pages", 1, 16384, 4, 16384, 0, 0, 4, 4,
      { { 0, 0, 4096, 0 }, { 1, 0, 4096, 0 }, { 2, 0, 4096, 0 }, { 3, 0, 4096, 1 } } },
    { "one notification, a size between pages", 1, 10000, 4, 10000, 0, 0, 3, 3,
      { { 0, 0, 4096, 0 }, { 1, 0, 4096, 0 }, { 2, 0, 1808, 1 } } },
    /* a count the DDI does not name is one notification */
    { "a count the DDI does not name", 3, 8192, 4, 8192, 0, 0, 2, 2,
      { { 0, 0, 4096, 0 }, { 1, 0, 4096, 1 } } },
    /* nothing asked: no buffer, no padding, no descriptor */
    { "nothing asked", 2, 0, 4, 0, 0, 0, 0, 0,
      { { 0, 0, 0, 0 } } },
};

/* A descriptor's address back to the page it names and the offset in it */
static
void
bdl_entry_place(
    const HDAC_BDLENTRY* Entry,
    UINT32* Page,
    UINT32* Offset)
{
    ULONGLONG Address = ((ULONGLONG)Entry->highAddr << 32) | Entry->lowAddr;

    *Page = (UINT32)((Address >> PAGE_SHIFT) - BDL_FIRST_FRAME);
    *Offset = (UINT32)(Address & (PAGE_SIZE - 1));
}

static
void
check_bdl(
    const BDL_CASE* Case)
{
    HDAC_BDL_LAYOUT Layout;
    HDAC_BDLENTRY Bdl[HDA_MAX_BDL_ENTRIES];
    PFN_NUMBER Frames[BDL_ROW_PAGES];
    UINT32 I, Page, Offset, Sum, Iocs;
    UINT16 Entries;
    int FailuresBefore = Failures;

    for (I = 0; I < BDL_ROW_PAGES; I++)
        Frames[I] = BDL_FIRST_FRAME + I;
    memset(Bdl, 0xAA, sizeof(Bdl));
    Entries = 0xFFFF;
    HdacBdlLayout(Case->NotificationCount, Case->RequestedBytes, Case->FrameBytes, &Layout);
    /* the layout: the buffer, its halves, the padding, and the pages the allocation takes */
    CHECK_EQ(Layout.BufferBytes, Case->BufferBytes);
    CHECK_EQ(Layout.HalfBytes, Case->HalfBytes);
    CHECK_EQ(Layout.OffsetBytes, Case->OffsetBytes);
    CHECK_EQ(Layout.AllocBytes, Case->BufferBytes + Case->OffsetBytes);
    CHECK_EQ((Layout.AllocBytes + PAGE_SIZE - 1) / PAGE_SIZE, Case->Pages);
    CHECK(Case->Pages <= BDL_ROW_PAGES);
    CHECK(HdacBdlBuild(Bdl, Frames, &Layout, &Entries));
    CHECK_EQ(Entries, Case->Entries);
    /* every descriptor as the row names it, inside the allocation and inside one page */
    for (I = 0; I < Case->Entries && I < Entries && I < HDA_MAX_BDL_ENTRIES; I++)
    {
        bdl_entry_place(&Bdl[I], &Page, &Offset);
        if (Page != Case->Entry[I].Page || Offset != Case->Entry[I].Offset || Bdl[I].len != Case->Entry[I].Bytes || Bdl[I].ioc != Case->Entry[I].Ioc)
            fprintf(stderr, "  %s: descriptor %u: page %u/%u, offset %u/%u, bytes %u/%u, interrupt %u/%u\n", Case->Name, I,
                    Page, Case->Entry[I].Page, Offset, Case->Entry[I].Offset, Bdl[I].len, Case->Entry[I].Bytes, Bdl[I].ioc, Case->Entry[I].Ioc);
        CHECK_EQ(Page, Case->Entry[I].Page);
        CHECK_EQ(Offset, Case->Entry[I].Offset);
        CHECK_EQ(Bdl[I].len, Case->Entry[I].Bytes);
        CHECK_EQ(Bdl[I].ioc, Case->Entry[I].Ioc);
        CHECK(Page < Case->Pages);
        CHECK(Offset + Bdl[I].len <= PAGE_SIZE);
    }
    if (Entries == 0)
        return;
    /* the list starts where the client fills the buffer from, offsetFromFirstPage into the pages: the first descriptor is the buffer's first byte (the live capture's shape) */
    bdl_entry_place(&Bdl[0], &Page, &Offset);
    CHECK_EQ(Page, Case->OffsetBytes / PAGE_SIZE);
    CHECK_EQ(Offset, Case->OffsetBytes % PAGE_SIZE);
    /* the descriptors cover the buffer exactly, in order, and the interrupts on completion end the halves and the buffer */
    Sum = Case->OffsetBytes;
    Iocs = 0;
    for (I = 0; I < Entries; I++)
    {
        Sum += Bdl[I].len;
        if (Bdl[I].ioc == 0)
            continue;
        Iocs++;
        CHECK_EQ(Sum, Iocs == 1 && Case->HalfBytes != 0 ? Case->OffsetBytes + Case->HalfBytes : Case->OffsetBytes + Case->BufferBytes);
    }
    CHECK_EQ(Sum, Case->OffsetBytes + Case->BufferBytes);
    CHECK_EQ(Iocs, Case->HalfBytes != 0 ? 2 : 1);
    if (Failures != FailuresBefore)
        fprintf(stderr, "  (in the row '%s')\n", Case->Name);
}

/* A request past what the list can describe: one notification is cut to the list's 256 pages, two to 128 pages a half; every page is a descriptor, and the interrupts end the halves and the buffer */
static
void
check_bdl_limits(void)
{
    HDAC_BDL_LAYOUT Layout;
    static HDAC_BDLENTRY Bdl[HDA_MAX_BDL_ENTRIES];
    PFN_NUMBER Frames[HDA_MAX_BDL_ENTRIES];
    UINT32 I, Page, Offset, Iocs;
    UINT16 Entries;

    for (I = 0; I < HDA_MAX_BDL_ENTRIES; I++)
        Frames[I] = BDL_FIRST_FRAME + I;

    HdacBdlLayout(1, 4 * 1024 * 1024, 4, &Layout);
    CHECK_EQ(Layout.BufferBytes, HDA_MAX_BDL_ENTRIES * PAGE_SIZE);
    CHECK_EQ(Layout.HalfBytes, 0);
    CHECK_EQ(Layout.OffsetBytes, 0);
    CHECK_EQ(Layout.AllocBytes, Layout.BufferBytes);
    CHECK(HdacBdlBuild(Bdl, Frames, &Layout, &Entries));
    CHECK_EQ(Entries, HDA_MAX_BDL_ENTRIES);
    Iocs = 0;
    for (I = 0; I < HDA_MAX_BDL_ENTRIES; I++)
    {
        bdl_entry_place(&Bdl[I], &Page, &Offset);
        CHECK_EQ(Page, I);
        CHECK_EQ(Offset, 0);
        CHECK_EQ(Bdl[I].len, PAGE_SIZE);
        CHECK_EQ(Bdl[I].ioc, I == HDA_MAX_BDL_ENTRIES - 1);
        Iocs += Bdl[I].ioc;
    }
    CHECK_EQ(Iocs, 1);

    HdacBdlLayout(2, 4 * 1024 * 1024, 4, &Layout);
    CHECK_EQ(Layout.HalfBytes, (HDA_MAX_BDL_ENTRIES / 2) * PAGE_SIZE);
    CHECK_EQ(Layout.BufferBytes, HDA_MAX_BDL_ENTRIES * PAGE_SIZE);
    CHECK_EQ(Layout.OffsetBytes, 0);
    CHECK_EQ(Layout.AllocBytes, Layout.BufferBytes);
    CHECK(HdacBdlBuild(Bdl, Frames, &Layout, &Entries));
    CHECK_EQ(Entries, HDA_MAX_BDL_ENTRIES);
    Iocs = 0;
    for (I = 0; I < HDA_MAX_BDL_ENTRIES; I++)
    {
        bdl_entry_place(&Bdl[I], &Page, &Offset);
        CHECK_EQ(Page, I);
        CHECK_EQ(Offset, 0);
        CHECK_EQ(Bdl[I].len, PAGE_SIZE);
        CHECK_EQ(Bdl[I].ioc, I == HDA_MAX_BDL_ENTRIES / 2 - 1 || I == HDA_MAX_BDL_ENTRIES - 1);
        Iocs += Bdl[I].ioc;
    }
    CHECK_EQ(Iocs, 2);
}

int main(void)
{
    unsigned int Index;

    for (Index = 0; Index < sizeof(NhltTableCases) / sizeof(NhltTableCases[0]); Index++)
        check_table(&NhltTableCases[Index]);

    for (Index = 0; Index < sizeof(NhltEndpointCases) / sizeof(NhltEndpointCases[0]); Index++)
        check_endpoint(&NhltEndpointCases[Index]);

    for (Index = 0; Index < sizeof(NhltViewCases) / sizeof(NhltViewCases[0]); Index++)
        check_view(&NhltViewCases[Index]);

    for (Index = 0; Index < sizeof(NhltFormatCases) / sizeof(NhltFormatCases[0]); Index++)
        check_format(&NhltFormatCases[Index]);

    for (Index = 0; Index < sizeof(NhltDeviceCases) / sizeof(NhltDeviceCases[0]); Index++)
        check_device(&NhltDeviceCases[Index]);

    for (Index = 0; Index < sizeof(NhltLinkCases) / sizeof(NhltLinkCases[0]); Index++)
        check_link(&NhltLinkCases[Index]);

    check_dmi_garbage();

    for (Index = 0; Index < sizeof(NhltResourceCases) / sizeof(NhltResourceCases[0]); Index++)
        check_resource(&NhltResourceCases[Index]);

    for (Index = 0; Index < sizeof(PolicyCases) / sizeof(PolicyCases[0]); Index++)
        check_policy(&PolicyCases[Index]);

    for (Index = 0; Index < sizeof(TopologyCases) / sizeof(TopologyCases[0]); Index++)
        check_topology(&TopologyCases[Index]);
    check_topology_names();

    check_dmi_tables();
    check_dmi_lookup();

    check_short_and_garbage_inputs();

    for (Index = 0; Index < sizeof(BdlCases) / sizeof(BdlCases[0]); Index++)
        check_bdl(&BdlCases[Index]);
    check_bdl_limits();

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
