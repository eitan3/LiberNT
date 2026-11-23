/*
 * PROJECT:     LiberNT host-native tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Runs the NHLT parser and the audio path decision of hdaudbus over the shared case tables
 * COPYRIGHT:   Copyright 2026 LiberNT contributors
 */

#include <stdio.h>
#include <string.h>

#include "shim.h"

#include <nhlt_parse.h>
#include <policy.h>

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

    check_dmi_tables();
    check_dmi_lookup();

    check_short_and_garbage_inputs();

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
