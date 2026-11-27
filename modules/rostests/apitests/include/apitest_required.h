/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     The decision of the expected switch LIBERNT_APITEST_REQUIRED on the value the environment gave: without an include, so that the host test runs the very decision the apitests run (N7-07); the reading of the environment and the summary's text stay in apitest_expected.h
 * COPYRIGHT:   Copyright 2026 LiberNT contributors
 */

#ifndef _APITEST_REQUIRED_H_
#define _APITEST_REQUIRED_H_

/* The bytes the switch's value may take, its terminating 0 included */
#define APITEST_REQUIRED_BYTES   8

/* What the switch's value is */
enum APITEST_REQUIRED_KIND
{
    ApitestRequiredUnset,     /* the variable is absent, or empty: the skip stays */
    ApitestRequiredOne,       /* 1 alone: a precondition that is missing fails */
    ApitestRequiredZero,      /* 0 alone: the skip stays */
    ApitestRequiredOther,     /* anything else within the bytes: a run script's mistake, fails closed */
    ApitestRequiredTooLong    /* 8 characters or more: the same */
};

/* Whether the value is Digit alone, as the switch reads 0 and 1 */
static __inline int
ApitestRequiredDigit(unsigned long Length, const char *Value, char Digit)
{
    return Length == 1 && Value[0] == Digit;
}

/*
 * The decision on a value as GetEnvironmentVariableA gave it: Length its
 * length, 0 for the variable absent (as apitest_expected.h reads it), and
 * Value its characters, terminated by the caller when Length is within the
 * bytes. 0 and 1 alone keep or lift the skip; any other value, and one of
 * APITEST_REQUIRED_BYTES characters or more, is required and fails closed
 * (R8-09).
 */
static __inline enum APITEST_REQUIRED_KIND
ApitestRequiredKind(unsigned long Length, const char *Value)
{
    if (Length == 0)
        return ApitestRequiredUnset;
    if (Length >= APITEST_REQUIRED_BYTES)
        return ApitestRequiredTooLong;
    if (ApitestRequiredDigit(Length, Value, '1'))
        return ApitestRequiredOne;
    if (ApitestRequiredDigit(Length, Value, '0'))
        return ApitestRequiredZero;
    return ApitestRequiredOther;
}

/* Whether the kind makes a precondition that is missing fail: every one but the unset and the 0 */
static __inline int
ApitestRequiredFails(enum APITEST_REQUIRED_KIND Kind)
{
    return Kind != ApitestRequiredUnset && Kind != ApitestRequiredZero;
}

#endif /* _APITEST_REQUIRED_H_ */
