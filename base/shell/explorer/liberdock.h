/*
 * PROJECT:     LiberNT Explorer
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Liber dock and tray islands
 * COPYRIGHT:   Copyright 2026 Eitan Hadar <eitanhadar4@gmail.com>
 */

#pragma once

#define LIBER_DOCK_MAX_ITEMS 32
#define LIBER_DOCK_PAD       6

typedef struct _LIBER_DOCK_LAYOUT
{
    RECT Dock;
    RECT Tray;
} LIBER_DOCK_LAYOUT;

BOOL LiberDock_IsTaskWindow(HWND hwnd);
VOID LiberDock_Layout(const RECT *prcMonitor, UINT dpi, UINT cells, int trayWidthDip, LIBER_DOCK_LAYOUT *pLayout);
BOOL LiberDock_SameApp(LPCWSTR pszTarget, LPCWSTR pszImage);
VOID LiberDock_WorkArea(const RECT *prcMonitor, UINT dpi, RECT *prcWork);

struct ITrayWindow;
HRESULT LiberDock_Start(ITrayWindow *Tray);
VOID LiberDock_Stop(VOID);
BOOL LiberDock_OpenStart(VOID);
BOOL LiberDock_ReservedWorkArea(HMONITOR hMonitor, RECT *prcWork);
