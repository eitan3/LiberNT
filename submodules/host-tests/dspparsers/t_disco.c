/*
 * PROJECT:     LiberNT host-native tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     The DisCo walker finds the _DSD properties of a reply and never reads past a short or garbled one
 * COPYRIGHT:   Copyright 2026 LiberNT contributors
 */

#include <stdio.h>
#include <string.h>

#include "shim.h"

#include <disco.h>

#include "disco_cases.h"

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
check_begin(void)
{
    unsigned int Index, FailuresBefore;

    for (Index = 0; Index < sizeof(DiscoBeginCases) / sizeof(DiscoBeginCases[0]); Index++)
    {
        const DISCO_BEGIN_CASE* Case = &DiscoBeginCases[Index];
        DISCO_DSD Dsd;

        FailuresBefore = Failures;
        memset(&Dsd, 0xA5, sizeof(Dsd));
        CHECK_EQ(DiscoDsdBegin(Case->Reply, Case->Size, &Dsd), Case->Status);
        CHECK_EQ(Dsd.PropertyCount, Case->PropertyCount);
        if (Case->Status == DiscoOk)
        {
            CHECK_EQ(Case->Reply[4] | (Case->Reply[5] << 8) | (Case->Reply[6] << 16) | ((unsigned)Case->Reply[7] << 24), Case->Size);
            CHECK(Dsd.PackageData != NULL);
            CHECK(Dsd.PackageData >= Case->Reply && Dsd.PackageData + Dsd.PackageLength <= Case->Reply + Case->Size);
        }
        else
        {
            CHECK(Dsd.PackageData == NULL);
        }
        if (Failures != FailuresBefore)
            fprintf(stderr, "  in begin case \"%s\"\n", Case->Name);
    }
}

static
void
check_properties(void)
{
    unsigned int Index, FailuresBefore;

    for (Index = 0; Index < sizeof(DiscoPropertyCases) / sizeof(DiscoPropertyCases[0]); Index++)
    {
        const DISCO_PROPERTY_CASE* Case = &DiscoPropertyCases[Index];
        const DISCO_BEGIN_CASE* Reply = &DiscoBeginCases[Case->ReplyIndex];
        DISCO_DSD Dsd;
        DISCO_ARGUMENT Name, Value, Element;
        UINT32 Number;

        FailuresBefore = Failures;
        CHECK_EQ(DiscoDsdBegin(Reply->Reply, Reply->Size, &Dsd), DiscoOk);
        memset(&Name, 0xA5, sizeof(Name));
        memset(&Value, 0xA5, sizeof(Value));
        CHECK_EQ(DiscoDsdPropertyAt(&Dsd, Case->PropertyIndex, &Name, &Value), Case->Status);
        if (Case->Status == DiscoOk)
        {
            CHECK_EQ(DiscoArgumentIsName(&Name, Case->Name), Case->NameStatus);
            CHECK_EQ(Value.Type, Case->ValueType);
            CHECK_EQ(Value.DataLength, Case->ValueLength);
            if (Value.Type == DISCO_ARGUMENT_INTEGER)
            {
                CHECK_EQ(DiscoArgumentU32(&Value, &Number), DiscoOk);
                CHECK_EQ(Number, Case->ValueInteger);
            }
            else
            {
                CHECK_EQ(DiscoArgumentU32(&Value, &Number), DiscoWrongType);
            }
            if (Value.Type == DISCO_ARGUMENT_PACKAGE)
            {
                CHECK_EQ(DiscoPackageCount(&Value), Case->ElementCount);
                CHECK_EQ(DiscoPackageElementAt(&Value, 0, &Element), DiscoOk);
                CHECK_EQ(DiscoArgumentU32(&Element, &Number), DiscoOk);
                CHECK_EQ(Number, Case->Element0);
                CHECK_EQ(DiscoPackageElementAt(&Value, 1, &Element), DiscoOk);
                CHECK_EQ(DiscoArgumentU32(&Element, &Number), DiscoOk);
                CHECK_EQ(Number, Case->Element1);
                CHECK_EQ(DiscoPackageElementAt(&Value, 2, &Element), DiscoNotFound);
            }
            else
            {
                CHECK_EQ(DiscoPackageCount(&Value), 0);
                CHECK_EQ(DiscoPackageElementAt(&Value, 0, &Element), DiscoWrongType);
            }
        }
        else
        {
            CHECK_EQ(Case->PropertyIndex >= Dsd.PropertyCount, 1);
        }
        if (Failures != FailuresBefore)
            fprintf(stderr, "  in property case %u\n", Index);
    }

    {
        const DISCO_BEGIN_CASE* Reply = &DiscoBeginCases[0];
        DISCO_DSD Dsd;
        DISCO_ARGUMENT Name, Value;
        UINT8 Text[8];
        SIZE_T Length;

        CHECK_EQ(DiscoDsdBegin(Reply->Reply, Reply->Size, &Dsd), DiscoOk);
        CHECK_EQ(DiscoDsdPropertyAt(&Dsd, 2, &Name, &Value), DiscoOk);
        memset(Text, 0xEE, sizeof(Text));
        CHECK_EQ(DiscoArgumentCopy(&Name, Text, sizeof(Text), &Length), DiscoOk);
        CHECK_EQ(Length, 5);
        CHECK_EQ(memcmp(Text, "three", 5), 0);
        CHECK_EQ(Text[5], 0xEE);
        CHECK_EQ(DiscoArgumentCopy(&Name, Text, 4, &Length), DiscoTruncated);
        CHECK_EQ(Length, 5);
        CHECK_EQ(DiscoArgumentCopy(&Name, Text, 5, NULL), DiscoOk);
        Value.Data = NULL;
        CHECK_EQ(DiscoArgumentCopy(&Value, Text, sizeof(Text), &Length), DiscoNoData);
    }
}

static
void
check_find(void)
{
    unsigned int Index, FailuresBefore;

    for (Index = 0; Index < sizeof(DiscoFindCases) / sizeof(DiscoFindCases[0]); Index++)
    {
        const DISCO_FIND_CASE* Case = &DiscoFindCases[Index];
        const DISCO_BEGIN_CASE* Reply = &DiscoBeginCases[Case->ReplyIndex];
        DISCO_DSD Dsd;
        DISCO_ARGUMENT Value;
        UINT32 Number;

        FailuresBefore = Failures;
        memset(&Dsd, 0xA5, sizeof(Dsd));
        DiscoDsdBegin(Reply->Reply, Reply->Size, &Dsd);
        memset(&Value, 0xA5, sizeof(Value));
        CHECK_EQ(DiscoDsdFind(&Dsd, Case->Name, &Value), Case->Status);
        if (Case->Status == DiscoOk)
        {
            CHECK_EQ(Value.Type, Case->ValueType);
            if (Value.Type == DISCO_ARGUMENT_INTEGER)
            {
                CHECK_EQ(DiscoArgumentU32(&Value, &Number), DiscoOk);
                CHECK_EQ(Number, Case->ValueInteger);
            }
        }
        if (Failures != FailuresBefore)
            fprintf(stderr, "  in find case %u (\"%s\")\n", Index, Case->Name);
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
    const UINT8* Reply,
    SIZE_T Size)
{
    static const char* const Names[] = { "one", "two", "three", "" };
    DISCO_DSD Dsd;
    DISCO_ARGUMENT Name, Value, Element;
    UINT32 Number, Index, Inner;
    UINT8 Text[32];
    SIZE_T Length;

    if (DiscoDsdBegin(Reply, Size, &Dsd) > DiscoWrongType)
        ++Failures;
    for (Index = 0; Index < Dsd.PropertyCount + 2; Index++)
    {
        if (DiscoDsdPropertyAt(&Dsd, Index, &Name, &Value) == DiscoOk)
        {
            DiscoArgumentIsName(&Name, Names[Index & 3]);
            DiscoArgumentU32(&Value, &Number);
            DiscoPackageCount(&Value);
            for (Inner = 0; Inner < 4; Inner++)
            {
                if (DiscoPackageElementAt(&Value, Inner, &Element) == DiscoOk)
                {
                    DiscoArgumentU32(&Element, &Number);
                    DiscoArgumentCopy(&Element, Text, sizeof(Text), &Length);
                }
            }
            DiscoArgumentCopy(&Value, Text, sizeof(Text), &Length);
        }
    }
    for (Index = 0; Index < 4; Index++)
        DiscoDsdFind(&Dsd, Names[Index], &Value);
}

static
void
check_short_and_garbage_inputs(void)
{
    static const UINT32 Seeds[] = { 0x12345678, 0x9E3779B9, 0xDEADBEEF, 0xCAFEBABE };
    /* a well-formed header and _DSD GUID followed by a package header that claims 64 bytes */
    static const unsigned char Template[] =
    {
        DISCO_REPLY(112, 2), DISCO_ARG_BUFFER(16), DISCO_DSD_GUID, DISCO_ARG_PACKAGE(64)
    };
    unsigned char Buffer[256];
    const DISCO_BEGIN_CASE* Reply;
    SIZE_T Length;
    unsigned int Seed, Iteration, R, Index, Byte;
    UINT32 State;

    for (R = 0; R < sizeof(DiscoBeginCases) / sizeof(DiscoBeginCases[0]); R++)
    {
        Reply = &DiscoBeginCases[R];
        if (Reply->Reply == NULL)
            continue;
        for (Length = 0; Length <= Reply->Size; Length++)
            exercise(Reply->Reply, Length);
        for (Byte = 0; Byte < Reply->Size; Byte++)
        {
            memcpy(Buffer, Reply->Reply, Reply->Size);
            Buffer[Byte] ^= 0x5A;
            exercise(Buffer, Reply->Size);
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
            if (Length >= sizeof(Template) && (Iteration & 1))
            {
                memcpy(Buffer, Template, sizeof(Template));
            }
            exercise(Buffer, Length);

            Reply = &DiscoBeginCases[(Seed * 3 + 5) % 14];
            memcpy(Buffer, Reply->Reply, Reply->Size);
            Length = Reply->Size;
            for (Byte = 0; Byte < 2; Byte++)
                Buffer[RandomStep(&State) % Length] = (unsigned char)RandomStep(&State);
            exercise(Buffer, Length);
            exercise(Buffer, RandomStep(&State) % (Length + 1));
        }
    }

    CHECK_EQ(Failures, 0);
}

static
void
check_names(void)
{
    DISCO_DSD Dsd;
    DISCO_ARGUMENT Argument;
    UINT32 Number;

    CHECK_STR(DiscoStatusName(DiscoOk), "ok");
    CHECK_STR(DiscoStatusName(DiscoNoData), "no data");
    CHECK_STR(DiscoStatusName(DiscoBadSignature), "bad signature");
    CHECK_STR(DiscoStatusName(DiscoBadLength), "bad length");
    CHECK_STR(DiscoStatusName(DiscoTruncated), "truncated");
    CHECK_STR(DiscoStatusName(DiscoNotFound), "not found");
    CHECK_STR(DiscoStatusName(DiscoWrongType), "wrong type");
    CHECK_STR(DiscoStatusName((DISCO_STATUS)7), "unknown");
    CHECK_EQ(DiscoDsdUuid()[0], 0x14);
    CHECK_EQ(DiscoDsdUuid()[15], 0x01);

    memset(&Dsd, 0, sizeof(Dsd));
    CHECK_EQ(DiscoDsdBegin(NULL, 100, &Dsd), DiscoNoData);
    CHECK_EQ(DiscoDsdPropertyAt(&Dsd, 0, &Argument, &Argument), DiscoNoData);
    CHECK_EQ(DiscoDsdFind(&Dsd, "one", &Argument), DiscoNoData);
    memset(&Argument, 0, sizeof(Argument));
    CHECK_EQ(DiscoArgumentU32(&Argument, &Number), DiscoWrongType);
}

int main(void)
{
    check_begin();
    check_properties();
    check_find();
    check_short_and_garbage_inputs();
    check_names();

    printf("disco: %d checks, %d failures\n", Checks, Failures);
    return Failures ? 1 : 0;
}
