/*
 * PROJECT:     LiberNT Explorer
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Pieces shared by the Liber shell surfaces
 * COPYRIGHT:   Copyright 2026 Eitan Hadar <eitanhadar4@gmail.com>
 */

#include <windows.h>
#include <shlwapi.h>

#include "experience.h"
#include "libershell.h"

#define PERSONALIZE_KEY L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize"

VOID LiberShell_BuildTokens(LiberUI::ThemeTokens *pTokens)
{
    LiberUI::ThemeParams params = { LiberExperience_IsDark() ? LiberUI::ThemeMode::Dark : LiberUI::ThemeMode::Light,
                                    LiberUI::AccentPreset(LIBER_ACCENT_AZURE),
                                    (LiberUI::SurfaceTint)LiberExperience_SurfaceTint() };
    pTokens->Build(params);
}

VOID LiberShell_ApplyFont(LiberUI::Surface *pSurface)
{
    WCHAR family[LF_FACESIZE];

    if (!LiberExperience_UiFont(family, _countof(family)))
        family[0] = 0;
    pSurface->Fonts().SetFamily(family[0] ? family : LIBER_FONT_UI);
}

static DWORD WINAPI BroadcastColorSet(LPVOID pContext)
{
    DWORD_PTR result;

    UNREFERENCED_PARAMETER(pContext);
    SendMessageTimeoutW(HWND_BROADCAST, WM_SETTINGCHANGE, 0, (LPARAM)L"ImmersiveColorSet", SMTO_ABORTIFHUNG, 200,
                        &result);
    return 0;
}

BOOL LiberShell_SetDarkMode(BOOL bDark)
{
    HKEY key;
    DWORD light = bDark ? 0 : 1;
    LSTATUS status;

    if (RegCreateKeyExW(HKEY_CURRENT_USER, PERSONALIZE_KEY, 0, NULL, 0, KEY_SET_VALUE, NULL, &key, NULL) !=
        ERROR_SUCCESS)
    {
        return FALSE;
    }
    status = RegSetValueExW(key, L"AppsUseLightTheme", 0, REG_DWORD, (const BYTE *)&light, sizeof(light));
    if (status == ERROR_SUCCESS)
        status = RegSetValueExW(key, L"SystemUsesLightTheme", 0, REG_DWORD, (const BYTE *)&light, sizeof(light));
    RegCloseKey(key);
    if (status != ERROR_SUCCESS)
        return FALSE;
    if (!QueueUserWorkItem(BroadcastColorSet, NULL, WT_EXECUTELONGFUNCTION))
        BroadcastColorSet(NULL);
    return TRUE;
}

VOID LiberShell_PlaceAbove(const RECT *prcAnchor, int widthPx, int heightPx, POINT *pptTopLeft)
{
    HMONITOR monitor = MonitorFromRect(prcAnchor, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi = { sizeof(mi) };
    UINT dpi = 96;
    HDC screen = GetDC(NULL);

    if (screen)
    {
        dpi = (UINT)GetDeviceCaps(screen, LOGPIXELSX);
        ReleaseDC(NULL, screen);
    }
    if (!GetMonitorInfoW(monitor, &mi))
        SetRect(&mi.rcMonitor, 0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN));

    pptTopLeft->x = prcAnchor->right - widthPx;
    pptTopLeft->y = prcAnchor->top - LiberUI::ScaleToDpi(LIBER_FLYOUT_GAP, dpi) - heightPx;
    if (pptTopLeft->x + widthPx > mi.rcMonitor.right)
        pptTopLeft->x = mi.rcMonitor.right - widthPx;
    if (pptTopLeft->x < mi.rcMonitor.left)
        pptTopLeft->x = mi.rcMonitor.left;
    if (pptTopLeft->y < mi.rcMonitor.top)
        pptTopLeft->y = mi.rcMonitor.top;
}

BOOL LiberStart_Matches(LPCWSTR pszName, LPCWSTR pszQuery)
{
    if (!pszQuery || !*pszQuery)
        return TRUE;
    if (!pszName || !*pszName)
        return FALSE;
    return StrStrIW(pszName, pszQuery) != NULL;
}
