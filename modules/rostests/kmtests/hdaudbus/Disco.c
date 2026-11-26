/*
 * PROJECT:     LiberNT kernel-mode tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     The DisCo walker finds the _DSD properties of a reply and never reads past a short or garbled one
 * COPYRIGHT:   Copyright 2026 LiberNT contributors
 */

#include <kmt_test.h>
#include <disco.h>

#define NDEBUG
#include <debug.h>

#include "disco_cases.h"

static
VOID
CheckBegin(VOID)
{
    ULONG Index;

    for (Index = 0; Index < ARRAYSIZE(DiscoBeginCases); Index++)
    {
        const DISCO_BEGIN_CASE* Case = &DiscoBeginCases[Index];
        DISCO_DSD Dsd;

        trace("%s\n", Case->Name);
        RtlFillMemory(&Dsd, sizeof(Dsd), 0xAA);
        ok_eq_int(DiscoDsdBegin(Case->Reply, Case->Size, &Dsd), Case->Status);
        ok_eq_uint(Dsd.PropertyCount, Case->PropertyCount);
        if (Case->Status == DiscoOk)
        {
            ok(Dsd.PackageData != NULL, "the package is located\n");
            ok(Dsd.PackageData >= Case->Reply && Dsd.PackageData + Dsd.PackageLength <= Case->Reply + Case->Size,
               "the package lies inside the reply\n");
        }
        else
        {
            ok(Dsd.PackageData == NULL, "nothing is located on failure\n");
        }
    }
}

static
VOID
CheckProperties(VOID)
{
    ULONG Index;
    UINT32 Number;
    DISCO_ARGUMENT Name, Value, Element;
    UCHAR Text[8];
    SIZE_T Length;

    for (Index = 0; Index < ARRAYSIZE(DiscoPropertyCases); Index++)
    {
        const DISCO_PROPERTY_CASE* Case = &DiscoPropertyCases[Index];
        const DISCO_BEGIN_CASE* Reply = &DiscoBeginCases[Case->ReplyIndex];
        DISCO_DSD Dsd;

        ok_eq_int(DiscoDsdBegin(Reply->Reply, Reply->Size, &Dsd), DiscoOk);
        RtlFillMemory(&Name, sizeof(Name), 0xAA);
        RtlFillMemory(&Value, sizeof(Value), 0xAA);
        ok_eq_int(DiscoDsdPropertyAt(&Dsd, Case->PropertyIndex, &Name, &Value), Case->Status);
        if (Case->Status != DiscoOk)
        {
            ok(Case->PropertyIndex >= Dsd.PropertyCount, "an index past the pairs is not found\n");
            continue;
        }

        ok_eq_int(DiscoArgumentIsName(&Name, Case->Name), Case->NameStatus);
        ok_eq_uint(Value.Type, Case->ValueType);
        ok_eq_uint(Value.DataLength, Case->ValueLength);
        ok_eq_int(DiscoArgumentU32(&Value, &Number), Value.Type == DISCO_ARGUMENT_INTEGER ? DiscoOk : DiscoWrongType);
        if (Value.Type == DISCO_ARGUMENT_INTEGER)
        {
            ok_eq_uint(Number, Case->ValueInteger);
        }
        if (Value.Type == DISCO_ARGUMENT_PACKAGE)
        {
            ok_eq_uint(DiscoPackageCount(&Value), Case->ElementCount);
            ok_eq_int(DiscoPackageElementAt(&Value, 0, &Element), DiscoOk);
            ok_eq_int(DiscoArgumentU32(&Element, &Number), DiscoOk);
            ok_eq_uint(Number, Case->Element0);
            ok_eq_int(DiscoPackageElementAt(&Value, 1, &Element), DiscoOk);
            ok_eq_int(DiscoArgumentU32(&Element, &Number), DiscoOk);
            ok_eq_uint(Number, Case->Element1);
            ok_eq_int(DiscoPackageElementAt(&Value, 2, &Element), DiscoNotFound);
        }
    }

    {
        const DISCO_BEGIN_CASE* Reply = &DiscoBeginCases[0];
        DISCO_DSD Dsd;

        ok_eq_int(DiscoDsdBegin(Reply->Reply, Reply->Size, &Dsd), DiscoOk);
        ok_eq_int(DiscoDsdPropertyAt(&Dsd, 2, &Name, &Value), DiscoOk);
        RtlFillMemory(Text, sizeof(Text), 0xEE);
        ok_eq_int(DiscoArgumentCopy(&Name, Text, sizeof(Text), &Length), DiscoOk);
        ok_eq_uint(Length, 5);
        ok_eq_uint(RtlCompareMemory(Text, "three", 5), 5);
        ok_eq_uint(Text[5], 0xEE);
        ok_eq_int(DiscoArgumentCopy(&Name, Text, 4, &Length), DiscoTruncated);
        ok_eq_uint(Length, 5);
        ok_eq_int(DiscoArgumentCopy(&Name, Text, 5, NULL), DiscoOk);
    }
}

static
VOID
CheckFind(VOID)
{
    ULONG Index;
    UINT32 Number;
    DISCO_DSD Dsd;
    DISCO_ARGUMENT Value;

    for (Index = 0; Index < ARRAYSIZE(DiscoFindCases); Index++)
    {
        const DISCO_FIND_CASE* Case = &DiscoFindCases[Index];
        const DISCO_BEGIN_CASE* Reply = &DiscoBeginCases[Case->ReplyIndex];

        RtlFillMemory(&Dsd, sizeof(Dsd), 0xAA);
        RtlFillMemory(&Value, sizeof(Value), 0xAA);
        ok_eq_int(DiscoDsdBegin(Reply->Reply, Reply->Size, &Dsd), Reply->Status);
        ok_eq_int(DiscoDsdFind(&Dsd, Case->Name, &Value), Case->Status);
        if (Case->Status != DiscoOk)
        {
            continue;
        }
        ok_eq_uint(Value.Type, Case->ValueType);
        if (Value.Type == DISCO_ARGUMENT_INTEGER)
        {
            ok_eq_int(DiscoArgumentU32(&Value, &Number), DiscoOk);
            ok_eq_uint(Number, Case->ValueInteger);
        }
    }
}

static
VOID
CheckShortInputs(VOID)
{
    UCHAR Buffer[256];
    SIZE_T Length;
    ULONG R, Byte;
    DISCO_DSD Dsd;
    DISCO_STATUS Status;
    DISCO_ARGUMENT Name, Value, Element;
    UINT32 Number;
    static const char* const Names[] = { "one", "two", "three", "" };
    ULONG Index, Inner;

    for (R = 0; R < ARRAYSIZE(DiscoBeginCases); R++)
    {
        const DISCO_BEGIN_CASE* Reply = &DiscoBeginCases[R];

        if (Reply->Reply == NULL)
            continue;
        for (Length = 0; Length <= Reply->Size; Length++)
        {
            Status = DiscoDsdBegin(Reply->Reply, Length, &Dsd);
            /* a prefix of the reply is never a well-formed _DSD: DiscoOk here would be the parser reading past what it was given */
            ok(Length == Reply->Size || Status != DiscoOk, "a prefix of %u bytes of a %u-byte reply parsed as well-formed\n", (ULONG)Length, Reply->Size);
            ok(Status <= DiscoWrongType, "a short reply stays within the status codes\n");
            for (Index = 0; Index < Dsd.PropertyCount + 1; Index++)
            {
                if (DiscoDsdPropertyAt(&Dsd, Index, &Name, &Value) == DiscoOk)
                {
                    DiscoPackageCount(&Value);
                    for (Inner = 0; Inner < 3; Inner++)
                    {
                        if (DiscoPackageElementAt(&Value, Inner, &Element) == DiscoOk)
                            DiscoArgumentU32(&Element, &Number);
                    }
                }
                DiscoDsdFind(&Dsd, Names[Index & 3], &Value);
            }
        }
        for (Byte = 0; Byte < Reply->Size; Byte++)
        {
            RtlCopyMemory(Buffer, Reply->Reply, Reply->Size);
            Buffer[Byte] ^= 0x5A;
            /* the whole reply with one byte flipped: within the status codes; which one depends on the byte */
            Status = DiscoDsdBegin(Buffer, Reply->Size, &Dsd);
            ok(Status <= DiscoWrongType, "a flipped byte stays within the status codes\n");
            for (Index = 0; Index < Dsd.PropertyCount + 1; Index++)
                DiscoDsdFind(&Dsd, Names[Index & 3], &Value);
        }
    }
}

START_TEST(Disco)
{
    static const char* const Names[] = { "ok", "no data", "bad signature", "bad length", "truncated", "not found", "wrong type" };
    DISCO_DSD Dsd;
    DISCO_ARGUMENT Argument;
    UINT32 Number;
    ULONG Index;

    CheckBegin();
    CheckProperties();
    CheckFind();
    CheckShortInputs();

    for (Index = 0; Index < ARRAYSIZE(Names); Index++)
    {
        ok_eq_str(DiscoStatusName((DISCO_STATUS)Index), Names[Index]);
    }
    ok_eq_str(DiscoStatusName((DISCO_STATUS)7), "unknown");
    ok_eq_uint(DiscoDsdUuid()[0], 0x14);
    ok_eq_uint(DiscoDsdUuid()[15], 0x01);

    RtlZeroMemory(&Dsd, sizeof(Dsd));
    ok_eq_int(DiscoDsdBegin(NULL, 100, &Dsd), DiscoNoData);
    ok_eq_int(DiscoDsdPropertyAt(&Dsd, 0, &Argument, &Argument), DiscoNoData);
    ok_eq_int(DiscoDsdFind(&Dsd, "one", &Argument), DiscoNoData);
    RtlZeroMemory(&Argument, sizeof(Argument));
    ok_eq_int(DiscoArgumentU32(&Argument, &Number), DiscoWrongType);
}
