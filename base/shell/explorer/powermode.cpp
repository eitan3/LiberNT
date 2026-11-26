/*
 * PROJECT:     LiberNT Explorer
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Power mode schemes shared by the power flyouts
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <windows.h>

#include "powermode.h"

HMODULE g_hPowrProf = NULL;
PFN_POWERGETACTIVESCHEME g_pfnPowerGetActiveScheme = NULL;
PFN_POWERSETACTIVESCHEME g_pfnPowerSetActiveScheme = NULL;

const TFYPOWERMODE g_TfyPowerModes[TFY_POWER_MODES] =
{
    { { 0xA1841308, 0x3541, 0x4FAB, { 0xBC, 0x81, 0xF7, 0x15, 0x56, 0xF2, 0x0B, 0x4A } }, L"Best power efficiency" },
    { { 0x381B4222, 0xF694, 0x41F0, { 0x96, 0x85, 0xFF, 0x5B, 0xB2, 0x60, 0xDF, 0x2E } }, L"Balanced" },
    { { 0x8C5E7FDA, 0xE8BF, 0x4A96, { 0x9A, 0x85, 0xA6, 0xE2, 0x3A, 0x8C, 0x63, 0x5C } }, L"Best performance" }
};

BOOL TfyLoadPowrProf()
{
    if (g_hPowrProf)
        return g_pfnPowerGetActiveScheme && g_pfnPowerSetActiveScheme;
    g_hPowrProf = LoadLibraryW(L"powrprof.dll");
    if (!g_hPowrProf)
        return FALSE;
    g_pfnPowerGetActiveScheme = (PFN_POWERGETACTIVESCHEME)GetProcAddress(g_hPowrProf, "PowerGetActiveScheme");
    g_pfnPowerSetActiveScheme = (PFN_POWERSETACTIVESCHEME)GetProcAddress(g_hPowrProf, "PowerSetActiveScheme");
    return g_pfnPowerGetActiveScheme && g_pfnPowerSetActiveScheme;
}

int
TfyActivePowerMode(VOID)
{
    GUID *pScheme = NULL;
    int iMode = -1;

    if (!TfyLoadPowrProf() || g_pfnPowerGetActiveScheme(NULL, &pScheme) != ERROR_SUCCESS || !pScheme)
        return -1;
    for (int i = 0; i < (int)_countof(g_TfyPowerModes); i++)
    {
        if (IsEqualGUID(*pScheme, g_TfyPowerModes[i].Scheme))
            iMode = i;
    }
    LocalFree(pScheme);
    return iMode;
}
