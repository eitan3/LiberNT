/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     The DSP endpoint loop API-test registration
 * COPYRIGHT:   Copyright 2026 LiberNT contributors
 */

#define STANDALONE
#include <apitest.h>

extern void func_dsploop(void);

const struct test winetest_testlist[] =
{
    { "dsploop", func_dsploop },
    { 0, 0 }
};
