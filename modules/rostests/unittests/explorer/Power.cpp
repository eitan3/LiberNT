/*
 * PROJECT:     LiberNT Explorer - Unit-tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Pin the power modes shared by the power flyouts to the Windows power schemes
 * COPYRIGHT:   Copyright 2026 Eitan Hadar <eitanhadar4@gmail.com>
 */

#include <apitest.h>

#include <windows.h>
#include <initguid.h>

#include "powermode.h"

DEFINE_GUID(TEST_GUID_MAX_POWER_SAVINGS, 0xA1841308, 0x3541, 0x4FAB, 0xBC, 0x81, 0xF7, 0x15, 0x56, 0xF2, 0x0B, 0x4A);
DEFINE_GUID(TEST_GUID_TYPICAL_POWER_SAVINGS, 0x381B4222, 0xF694, 0x41F0, 0x96, 0x85, 0xFF, 0x5B, 0xB2, 0x60, 0xDF, 0x2E);
DEFINE_GUID(TEST_GUID_MIN_POWER_SAVINGS, 0x8C5E7FDA, 0xE8BF, 0x4A96, 0x9A, 0x85, 0xA6, 0xE2, 0x3A, 0x8C, 0x63, 0x5C);

START_TEST(Power)
{
    GUID *active = NULL;
    int mode, expected = -1;

    ok(IsEqualGUID(g_TfyPowerModes[0].Scheme, TEST_GUID_MAX_POWER_SAVINGS), "mode 0 is the power saver scheme\n");
    ok(IsEqualGUID(g_TfyPowerModes[1].Scheme, TEST_GUID_TYPICAL_POWER_SAVINGS), "mode 1 is the balanced scheme\n");
    ok(IsEqualGUID(g_TfyPowerModes[2].Scheme, TEST_GUID_MIN_POWER_SAVINGS), "mode 2 is the high performance scheme\n");
    ok(!wcscmp(g_TfyPowerModes[1].pszName, L"Balanced"), "name %ls\n", g_TfyPowerModes[1].pszName);

    if (!TfyLoadPowrProf())
    {
        skip("powrprof.dll has no PowerGetActiveScheme\n");
        return;
    }
    ok(TfyLoadPowrProf(), "loading twice\n");

    mode = TfyActivePowerMode();
    ok(mode >= -1 && mode < TFY_POWER_MODES, "mode %d\n", mode);
    if (g_pfnPowerGetActiveScheme(NULL, &active) == ERROR_SUCCESS && active)
    {
        for (int i = 0; i < TFY_POWER_MODES; i++)
        {
            if (IsEqualGUID(*active, g_TfyPowerModes[i].Scheme))
                expected = i;
        }
        LocalFree(active);
    }
    ok(mode == expected, "active mode %d, expected %d\n", mode, expected);
}
