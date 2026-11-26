/*
 * PROJECT:     LiberNT Explorer
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Choose between the Classic and the Liber desktop experience
 * COPYRIGHT:   Copyright 2026 Eitan Hadar <eitanhadar4@gmail.com>
 */

#include <windows.h>

#include "experience.h"

static DWORD ReadDword(HKEY root, LPCWSTR key, LPCWSTR value, DWORD fallback)
{
    DWORD data = 0, size = sizeof(data), type = 0;

    if (RegGetValueW(root, key, value, RRF_RT_REG_DWORD, &type, &data, &size) != ERROR_SUCCESS)
        return fallback;
    return data;
}

DWORD LiberExperience_Get(VOID)
{
    DWORD value = ReadDword(HKEY_CURRENT_USER, LIBER_DESIGN_KEY, LIBER_EXPERIENCE_VALUE, LIBER_EXPERIENCE_CLASSIC);
    return value == LIBER_EXPERIENCE_LIBER ? LIBER_EXPERIENCE_LIBER : LIBER_EXPERIENCE_CLASSIC;
}

BOOL LiberExperience_IsLiber(VOID)
{
    return LiberExperience_Get() == LIBER_EXPERIENCE_LIBER;
}

BOOL LiberExperience_IsDark(VOID)
{
    return ReadDword(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
                     L"SystemUsesLightTheme", 1) == 0;
}
