/*
 * PROJECT:     LiberNT Explorer
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Power mode schemes shared by the power flyouts
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

#define TFY_POWER_MODES 3

typedef DWORD (WINAPI *PFN_POWERGETACTIVESCHEME)(HKEY, GUID **);
typedef DWORD (WINAPI *PFN_POWERSETACTIVESCHEME)(HKEY, const GUID *);

struct TFYPOWERMODE
{
    GUID Scheme;
    LPCWSTR pszName;
};

extern HMODULE g_hPowrProf;
extern PFN_POWERGETACTIVESCHEME g_pfnPowerGetActiveScheme;
extern PFN_POWERSETACTIVESCHEME g_pfnPowerSetActiveScheme;
extern const TFYPOWERMODE g_TfyPowerModes[TFY_POWER_MODES];

BOOL TfyLoadPowrProf();
int TfyActivePowerMode(VOID);
