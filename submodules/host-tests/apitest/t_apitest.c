/*
 * PROJECT:     LiberNT host-native tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     The expected switch of the apitests (modules/rostests/apitests/include/apitest_required.h, the decision the apitests run): the five values of R8-09 and the boundary of the bytes, each with the kind it is read as and whether a missing precondition fails
 * COPYRIGHT:   Copyright 2026 LiberNT contributors
 */

/* The decision first, before any include of its own: the header has none */
#include <apitest_required.h>

#include <stdio.h>
#include <string.h>

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
        long long _a = (long long)(a), _b = (long long)(b); \
        ++Checks; \
        if (_a != _b) { \
            fprintf(stderr, "%s:%d: FAIL: %s == %s: %lld != %lld\n", __FILE__, __LINE__, #a, #b, _a, _b); \
            ++Failures; \
        } \
    } while (0)

/* One value of the switch as the environment gives it: its length and its characters; a length of 0 is the variable absent */
static
void
check_value(
    const char* name,
    unsigned long length,
    const char* value,
    enum APITEST_REQUIRED_KIND kind,
    int fails)
{
    enum APITEST_REQUIRED_KIND got = ApitestRequiredKind(length, value);

    if (got != kind || ApitestRequiredFails(got) != fails)
        fprintf(stderr, "%s: read as kind %d (expected %d), fails %d (expected %d)\n", name, (int)got, (int)kind,
                ApitestRequiredFails(got), fails);
    CHECK_EQ(got, kind);
    CHECK_EQ(ApitestRequiredFails(got), fails);
}

/* (a1) The five values of R8-09 and the boundary of the bytes */
static
void
check_switch(
    void)
{
    /* unset: not required, a skip */
    check_value("unset", 0, "", ApitestRequiredUnset, 0);
    /* 0 alone: not required, a skip */
    check_value("0", 1, "0", ApitestRequiredZero, 0);
    /* 1 alone: required, a failure */
    check_value("1", 1, "1", ApitestRequiredOne, 1);
    /* yes: not a 0 or a 1, required, a failure */
    check_value("yes", 3, "yes", ApitestRequiredOther, 1);
    /* an 8-character value: too long, required, a failure */
    check_value("11111111", 8, "11111111", ApitestRequiredTooLong, 1);
    /* the boundary: seven characters are within the bytes and read as a value, so not a 1 either */
    check_value("1111111", 7, "1111111", ApitestRequiredOther, 1);
    check_value("0000000", 7, "0000000", ApitestRequiredOther, 1);
    check_value("00000000", 8, "00000000", ApitestRequiredTooLong, 1);
    CHECK_EQ(APITEST_REQUIRED_BYTES, 8);
    /* 0 and 1 alone: beside them, with anything, they are another value */
    check_value("10", 2, "10", ApitestRequiredOther, 1);
    check_value("01", 2, "01", ApitestRequiredOther, 1);
    check_value("2", 1, "2", ApitestRequiredOther, 1);
    check_value("true", 4, "true", ApitestRequiredOther, 1);
    check_value("space", 1, " ", ApitestRequiredOther, 1);
    /* the digit test reads a character alone, and nothing past it */
    CHECK_EQ(ApitestRequiredDigit(1, "1x", '1'), 1);
    CHECK_EQ(ApitestRequiredDigit(2, "1x", '1'), 0);
    CHECK_EQ(ApitestRequiredDigit(0, "1", '1'), 0);
    CHECK_EQ(ApitestRequiredDigit(1, "0", '1'), 0);
    /* every kind fails or not as the decision says: the unset and the 0 alone keep the skip */
    CHECK_EQ(ApitestRequiredFails(ApitestRequiredUnset), 0);
    CHECK_EQ(ApitestRequiredFails(ApitestRequiredZero), 0);
    CHECK_EQ(ApitestRequiredFails(ApitestRequiredOne), 1);
    CHECK_EQ(ApitestRequiredFails(ApitestRequiredOther), 1);
    CHECK_EQ(ApitestRequiredFails(ApitestRequiredTooLong), 1);
}

int
main(
    void)
{
    check_switch();

    printf("apitest: %d checks, %d failures\n", Checks, Failures);
    return Failures != 0;
}
