/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     The expected switch of the apitests: with LIBERNT_APITEST_REQUIRED=1 a precondition the image is expected to provide is a failure when it is missing, not a skip; without it the skip stays, counted, and the program's last line says how much was exercised
 * COPYRIGHT:   Copyright 2026 LiberNT contributors
 */

#ifndef _APITEST_EXPECTED_H_
#define _APITEST_EXPECTED_H_

#include <apitest.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <apitest_required.h>

/* The preconditions that were not met in this program, skipped or failed */
static LONG ApitestMissing = 0;

/* What the switch read, as the summary line names it (R8-09); set by ApitestRequired */
static char ApitestRequiredText[APITEST_REQUIRED_BYTES + 64];

/* The summary's name of the switch's kind of value, the value itself quoted when it is none of the others */
static __inline VOID
ApitestRequiredSay(
    enum APITEST_REQUIRED_KIND Kind,
    const char *Value)
{
    switch (Kind)
    {
        case ApitestRequiredUnset:
            strcpy(ApitestRequiredText, "unset: not required, a skip");
            break;
        case ApitestRequiredOne:
            strcpy(ApitestRequiredText, "1: required, a failure");
            break;
        case ApitestRequiredZero:
            strcpy(ApitestRequiredText, "0: not required, a skip");
            break;
        case ApitestRequiredTooLong:
            strcpy(ApitestRequiredText, "set to 8 characters or more: not a 0 or a 1, required, a failure");
            break;
        default:
            snprintf(ApitestRequiredText, sizeof(ApitestRequiredText), "'%s': not a 0 or a 1, required, a failure", Value);
    }
}

/*
 * Whether the image is expected to provide what the preconditions ask for:
 * LIBERNT_APITEST_REQUIRED=1, set by the gate's run scripts. The harness's
 * exit code is its failure count alone (wine/test.h, run_test), so a skip
 * leaves it 0; under the switch a precondition that is missing fails instead.
 * The decision on the value is apitest_required.h's, where the host test runs
 * it (N7-07): 0, or the variable absent, keeps the skip; any other value, or
 * one of 8 characters or more, is a run script's mistake and fails closed as
 * required (R8-09), and the summary names the value.
 */
static __inline BOOL
ApitestRequired(VOID)
{
    char Value[APITEST_REQUIRED_BYTES];
    DWORD Length = GetEnvironmentVariableA("LIBERNT_APITEST_REQUIRED", Value, sizeof(Value));
    enum APITEST_REQUIRED_KIND Kind;

    if (Length != 0 && Length < sizeof(Value))
        Value[Length] = 0;
    Kind = ApitestRequiredKind(Length, Value);
    ApitestRequiredSay(Kind, Value);
    return ApitestRequiredFails(Kind) ? TRUE : FALSE;
}

/*
 * A precondition of what follows is not met; the message says what is
 * missing. The caller returns, or goes on to what the precondition does not
 * gate.
 */
#define apitest_missing(...)                     \
    do                                           \
    {                                            \
        ApitestMissing++;                        \
        if (ApitestRequired())                   \
            ok(0, "required: " __VA_ARGS__);     \
        else                                     \
            skip(__VA_ARGS__);                   \
    } while (0)

/*
 * The program's last line, before the harness's summary: how much of what it
 * planned was exercised (Planned 0 when it has no planned count) and how many
 * preconditions were missing. Printed whatever WINETEST_DEBUG is, as the
 * summary is, and unlike trace.
 */
static __inline VOID
ApitestSummary(
    const char *Program,
    UINT Exercised,
    UINT Planned,
    const char *What)
{
    ApitestRequired();
    winetest_printf("%s: ", Program);
    if (Planned)
        winetest_printf("%u of %u ", Exercised, Planned);
    else
        winetest_printf("%u ", Exercised);
    winetest_printf("%s, %ld precondition%s missing (LIBERNT_APITEST_REQUIRED %s)\n", What, ApitestMissing, ApitestMissing == 1 ? "" : "s",
                    ApitestRequiredText);
}

#endif /* _APITEST_EXPECTED_H_ */
