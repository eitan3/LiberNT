/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test list of the performance data helper tests
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#define STANDALONE
#include <apitest.h>

extern void func_ProcessorFrequency(void);

const struct test winetest_testlist[] =
{
    { "ProcessorFrequency", func_ProcessorFrequency },
    { 0, 0 }
};
