/*
 * PROJECT:     LiberNT Explorer
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Liber dock placement and task window rules
 * COPYRIGHT:   Copyright 2026 Eitan Hadar <eitanhadar4@gmail.com>
 */

#include <windows.h>
#include <atlbase.h>
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

VOID LiberDock_Layout(const RECT *prcMonitor, UINT dpi, UINT cells, int trayWidthDip, LIBER_DOCK_LAYOUT *pLayout)
{
    int dockWidth = ScaleToDpi((int)cells * LIBER_DOCK_CELL + 2 * LIBER_DOCK_PAD, dpi);
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

    pLayout->Tray.right = prcMonitor->right - gap;
    pLayout->Tray.left = pLayout->Tray.right - trayWidth;
    pLayout->Tray.top = pLayout->Dock.top + (dockHeight - trayHeight) / 2;
    pLayout->Tray.bottom = pLayout->Tray.top + trayHeight;
}
