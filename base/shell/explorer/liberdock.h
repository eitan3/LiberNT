/*
 * PROJECT:     LiberNT Explorer
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Liber dock and tray islands
 * COPYRIGHT:   Copyright 2026 Eitan Hadar <eitanhadar4@gmail.com>
 */

#pragma once

#define LIBER_DOCK_MAX_ITEMS 32
#define LIBER_DOCK_PAD_X     8
#define LIBER_DOCK_PAD_Y     ((LIBER_DOCK_HEIGHT - LIBER_DOCK_CELL) / 2)
#define LIBER_DOCK_CELL_GAP  2
#define LIBER_TRAY_INSET     12
#define LIBER_DOCK_HOP       10
#define LIBER_DOCK_STACK_OFFSET 4
#define LIBER_LANGUAGE_WIDTH 40
#define LIBER_TRAY_BUTTON_PAD 10
#define LIBER_TRAY_ICON_GAP  10

typedef struct _LIBER_DOCK_LAYOUT
{
    RECT Dock;
    RECT Tray;
} LIBER_DOCK_LAYOUT;

BOOL LiberDock_IsTaskWindow(HWND hwnd);
VOID LiberDock_Layout(const RECT *prcMonitor, UINT dpi, UINT cells, int trayWidthDip, LIBER_DOCK_LAYOUT *pLayout);
BOOL LiberDock_SameApp(LPCWSTR pszTarget, LPCWSTR pszImage);
VOID LiberDock_AppName(LPCWSTR pszPath, LPWSTR pszName, UINT cchName);
#define LIBER_PREVIEW_THUMB_W 200
#define LIBER_PREVIEW_THUMB_H 120
#define LIBER_PREVIEW_MAX     8
#define LIBER_PREVIEW_DELAY   500
SIZE LiberDock_FitThumbnail(SIZE window, SIZE limit);
BOOL LiberPreview_Show(const HWND *pWindows, UINT cWindows, HICON hIcon, const RECT *prcAnchor);
VOID LiberPreview_Hide(VOID);
BOOL LiberPreview_IsOpen(VOID);
BOOL LiberPreview_Contains(POINT screen);
VOID LiberPreview_Destroy(VOID);
int LiberDock_Width(UINT cells);
double LiberDock_HopOffset(double progress);
BOOL LiberDock_LanguageLabel(LANGID langId, LPWSTR pszLabel, UINT cchLabel);
int LiberDock_SystemPillWidth(BOOL bBattery);
VOID LiberDock_WorkArea(const RECT *prcMonitor, UINT dpi, RECT *prcWork);
VOID LiberDock_CellRect(const RECT *prcDock, UINT dpi, UINT cell, RECT *prcCell);
#define LIBER_BLUR_BAND_LAYERS 4
VOID LiberDock_BlurBand(const RECT *prcMonitor, UINT dpi, UINT layer, RECT *prcBand);

struct ITrayWindow;
HRESULT LiberDock_Start(ITrayWindow *Tray);
VOID LiberDock_Stop(VOID);
VOID LiberDock_EndSession(VOID);
BOOL LiberDock_OpenStart(VOID);
BOOL LiberDock_ReservedWorkArea(HMONITOR hMonitor, RECT *prcWork);
