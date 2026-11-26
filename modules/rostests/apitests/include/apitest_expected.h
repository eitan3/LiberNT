/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     The expected switch of the apitests: with LIBERNT_APITEST_REQUIRED=1 a precondition the image is expected to provide is a failure when it is missing, not a skip; without it the skip stays, counted, and the program's last line says how much was exercised
 * COPYRIGHT:   Copyright 2026 LiberNT contributors
 */

#ifndef _APITEST_EXPECTED_H_
#define _APITEST_EXPECTED_H_

#include <apitest.h>
#include <stdlib.h>

/* The preconditions that were not met in this program, skipped or failed */
static LONG ApitestMissing = 0;

/*
 * Whether the image is expected to provide what the preconditions ask for:
 * LIBERNT_APITEST_REQUIRED=1, set by the gate's run scripts. The harness's
 * exit code is its failure count alone (wine/test.h, run_test), so a skip
 * leaves it 0; under the switch a precondition that is missing fails instead.
 */
static __inline BOOL
ApitestRequired(VOID)
{
    char Value[8];
    DWORD Length = GetEnvironmentVariableA("LIBERNT_APITEST_REQUIRED", Value, sizeof(Value));

    return Length > 0 && Length < sizeof(Value) && atoi(Value) != 0;
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
    winetest_printf("%s: ", Program);
    if (Planned)
        winetest_printf("%u of %u ", Exercised, Planned);
    else
        winetest_printf("%u ", Exercised);
    winetest_printf("%s, %ld precondition%s missing (%s)\n", What, ApitestMissing, ApitestMissing == 1 ? "" : "s",
                    ApitestRequired() ? "LIBERNT_APITEST_REQUIRED=1, a failure" : "not required, a skip");
}

#endif /* _APITEST_EXPECTED_H_ */
