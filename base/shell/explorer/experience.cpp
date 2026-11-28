/*
 * PROJECT:     LiberNT Explorer
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Choose between the Classic and the Liber desktop experience
 * COPYRIGHT:   Copyright 2026 Eitan Hadar <eitanhadar4@gmail.com>
 */

#include <windows.h>
#include <math.h>

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

DWORD LiberExperience_SurfaceTint(VOID)
{
    DWORD tint = ReadDword(HKEY_CURRENT_USER, LIBER_DESIGN_KEY, L"SurfaceTint", 1);
    return tint <= 2 ? tint : 1;
}

VOID LiberExperience_SetDimShade(COLORREF shade)
{
    DWORD value = (DWORD)shade & 0xFFFFFF;

    if (ReadDword(HKEY_CURRENT_USER, LIBER_DESIGN_KEY, L"DimShade", 0xFFFFFFFF) != value)
        RegSetKeyValueW(HKEY_CURRENT_USER, LIBER_DESIGN_KEY, L"DimShade", REG_DWORD, &value, sizeof(value));
}

static VOID WriteDword(LPCWSTR value, DWORD data)
{
    if (ReadDword(HKEY_CURRENT_USER, LIBER_DESIGN_KEY, value, ~data) != data)
        RegSetKeyValueW(HKEY_CURRENT_USER, LIBER_DESIGN_KEY, value, REG_DWORD, &data, sizeof(data));
}

VOID LiberExperience_SetGlassTint(double hue, double tint)
{
    double wrapped = fmod(hue, 360.0);

    if (wrapped < 0.0)
        wrapped += 360.0;
    tint = tint < 0.0 ? 0.0 : tint > 2.5 ? 2.5 : tint;
    WriteDword(L"GlassHue", (DWORD)(wrapped * 100.0 + 0.5) % 36000);
    WriteDword(L"GlassTint", (DWORD)(tint * 1000.0 + 0.5));
}

DWORD LiberExperience_TaskbarStyle(VOID)
{
    DWORD style = ReadDword(HKEY_CURRENT_USER, LIBER_DESIGN_KEY, L"TaskbarStyle", 0);
    return style <= 2 ? style : 0;
}

BOOL LiberExperience_TaskbarLeft(VOID)
{
    return ReadDword(HKEY_CURRENT_USER, LIBER_DESIGN_KEY, L"TaskbarAlignment", 0) == 1;
}

DWORD LiberExperience_Accent(VOID)
{
    return ReadDword(HKEY_CURRENT_USER, LIBER_DESIGN_KEY, L"Accent", LIBER_ACCENT_FOLLOW);
}

BOOL LiberExperience_WidgetsEnabled(VOID)
{
    return ReadDword(HKEY_CURRENT_USER, LIBER_DESIGN_KEY, L"Widgets", 1) != 0;
}

BOOL LiberExperience_UiFont(LPWSTR pszFamily, DWORD cchFamily)
{
    DWORD size = cchFamily * sizeof(WCHAR);

    if (!pszFamily || cchFamily == 0)
        return FALSE;
    pszFamily[0] = 0;
    if (RegGetValueW(HKEY_CURRENT_USER, LIBER_DESIGN_KEY, L"UiFont", RRF_RT_REG_SZ, NULL, pszFamily, &size) !=
        ERROR_SUCCESS)
    {
        pszFamily[0] = 0;
        return FALSE;
    }
    return pszFamily[0] != 0;
}

BOOL LiberExperience_IsDark(VOID)
{
    return ReadDword(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
                     L"SystemUsesLightTheme", 1) == 0;
}
