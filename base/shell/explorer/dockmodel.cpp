/*
 * PROJECT:     LiberNT Explorer
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Liber dock placement and task window rules
 * COPYRIGHT:   Copyright 2026 Eitan Hadar <eitanhadar4@gmail.com>
 */

#include <windows.h>
#include <atlbase.h>
#include <math.h>
#include <liberui.h>

#include "liberdock.h"

using namespace LiberUI;

#define LIBER_DOCK_GAP       ((LIBER_TASKBAR_DOCK - LIBER_DOCK_HEIGHT) / 2)

BOOL LiberDock_IsTaskWindow(HWND hwnd)
{
    LONG_PTR exStyle;

    if (!IsWindow(hwnd) || !IsWindowVisible(hwnd))
        return FALSE;
    if (GetWindow(hwnd, GW_OWNER) != NULL)
        return FALSE;
    exStyle = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
    if (exStyle & WS_EX_TOOLWINDOW)
        return FALSE;
    if ((GetWindowLongPtrW(hwnd, GWL_STYLE) & WS_CHILD) != 0)
        return FALSE;
    return GetWindowTextLengthW(hwnd) > 0;
}

BOOL LiberDock_SameApp(LPCWSTR pszTarget, LPCWSTR pszImage)
{
    return pszTarget && pszImage && *pszTarget && *pszImage && !lstrcmpiW(pszTarget, pszImage);
}

VOID LiberDock_WorkArea(const RECT *prcMonitor, UINT dpi, RECT *prcWork)
{
    ATLASSERT(prcMonitor && prcWork && dpi != 0);
    *prcWork = *prcMonitor;
    prcWork->bottom = max(prcMonitor->top, prcMonitor->bottom - ScaleToDpi(LIBER_TASKBAR_DOCK, dpi));
}

VOID LiberDock_CellRect(const RECT *prcDock, UINT dpi, UINT cell, RECT *prcCell)
{
    ATLASSERT(prcDock && prcCell && dpi != 0);
    prcCell->left = prcDock->left + ScaleToDpi(LIBER_DOCK_PAD_X + (int)cell * (LIBER_DOCK_CELL + LIBER_DOCK_CELL_GAP), dpi);
    prcCell->top = prcDock->top + ScaleToDpi(LIBER_DOCK_PAD_Y, dpi);
    prcCell->right = prcDock->left +
                     ScaleToDpi(LIBER_DOCK_PAD_X + (int)cell * (LIBER_DOCK_CELL + LIBER_DOCK_CELL_GAP) + LIBER_DOCK_CELL, dpi);
    prcCell->bottom = prcDock->top + ScaleToDpi(LIBER_DOCK_PAD_Y + LIBER_DOCK_CELL, dpi);
}

VOID LiberDock_BlurBand(const RECT *prcMonitor, UINT dpi, UINT layer, RECT *prcBand)
{
    ATLASSERT(prcMonitor && prcBand && dpi != 0 && layer < LIBER_BLUR_BAND_LAYERS);
    int height = ScaleToDpi(LIBER_TASKBAR_DOCK, dpi);
    *prcBand = *prcMonitor;
    prcBand->top = prcMonitor->bottom - height * (int)(LIBER_BLUR_BAND_LAYERS - layer) / LIBER_BLUR_BAND_LAYERS;
}

int LiberDock_Width(UINT cells)
{
    ATLASSERT(cells > 0);
    return (int)cells * LIBER_DOCK_CELL + ((int)cells - 1) * LIBER_DOCK_CELL_GAP + 2 * LIBER_DOCK_PAD_X;
}

BOOL LiberDock_LanguageLabel(LANGID langId, LPWSTR pszLabel, UINT cchLabel)
{
    WCHAR szIso[9];

    if (!pszLabel || cchLabel < 4)
        return FALSE;
    pszLabel[0] = 0;
    if (!GetLocaleInfoW(MAKELCID(langId, SORT_DEFAULT), LOCALE_SISO639LANGNAME2, szIso, _countof(szIso)) || !szIso[0])
        return FALSE;
    lstrcpynW(pszLabel, szIso, cchLabel);
    CharUpperW(pszLabel);
    return TRUE;
}

double LiberDock_HopOffset(double progress)
{
    if (progress <= 0.0 || progress >= 1.0)
        return 0.0;
    return LIBER_DOCK_HOP * sin(3.14159265358979 * progress);
}

int LiberDock_SystemPillWidth(BOOL bBattery)
{
    int icons = bBattery ? 3 : 2;
    return 2 * LIBER_TRAY_BUTTON_PAD + icons * 20 + (icons - 1) * LIBER_TRAY_ICON_GAP;
}

VOID LiberDock_Layout(const RECT *prcMonitor, UINT dpi, UINT cells, int trayWidthDip, LIBER_DOCK_LAYOUT *pLayout)
{
    int dockWidth = ScaleToDpi(LiberDock_Width(cells), dpi);
    int dockHeight = ScaleToDpi(LIBER_DOCK_HEIGHT, dpi);
    int trayWidth = ScaleToDpi(trayWidthDip, dpi);
    int trayHeight = ScaleToDpi(LIBER_TRAY_HEIGHT, dpi);
    int gap = ScaleToDpi(LIBER_DOCK_GAP, dpi);
    LONG monitorWidth = prcMonitor->right - prcMonitor->left;

    ATLASSERT(prcMonitor && pLayout && dpi != 0);
    pLayout->Dock.left = prcMonitor->left + (monitorWidth - dockWidth) / 2;
    pLayout->Dock.bottom = prcMonitor->bottom - gap;
    pLayout->Dock.top = pLayout->Dock.bottom - dockHeight;
    pLayout->Dock.right = pLayout->Dock.left + dockWidth;

    pLayout->Tray.right = prcMonitor->right - ScaleToDpi(LIBER_TRAY_INSET, dpi);
    pLayout->Tray.left = pLayout->Tray.right - trayWidth;
    pLayout->Tray.top = pLayout->Dock.top + (dockHeight - trayHeight) / 2;
    pLayout->Tray.bottom = pLayout->Tray.top + trayHeight;
}
