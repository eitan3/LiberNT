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

static BOOL OrderContains(LPCWSTR const *pKeys, UINT cKeys, LPCWSTR pszKey)
{
    for (UINT i = 0; i < cKeys; i++)
    {
        if (!lstrcmpiW(pKeys[i], pszKey))
            return TRUE;
    }
    return FALSE;
}

UINT LiberDock_MergeOrder(LPCWSTR const *pFirst, UINT cFirst, LPCWSTR const *pThen, UINT cThen, LPCWSTR *pOut, UINT cMax)
{
    UINT count = 0;

    ATLASSERT((pFirst || !cFirst) && (pThen || !cThen) && pOut);
    for (UINT pass = 0; pass < 2; pass++)
    {
        LPCWSTR const *keys = pass ? pThen : pFirst;
        UINT cKeys = pass ? cThen : cFirst;
        for (UINT i = 0; i < cKeys && count < cMax; i++)
        {
            if (keys[i] && keys[i][0] && !OrderContains(pOut, count, keys[i]))
                pOut[count++] = keys[i];
        }
    }
    ATLASSERT(count <= cMax);
    return count;
}

static UINT OrderRank(LPCWSTR pszKey, LPCWSTR const *pOrder, UINT cOrder)
{
    if (pszKey && pszKey[0])
    {
        for (UINT i = 0; i < cOrder; i++)
        {
            if (!lstrcmpiW(pOrder[i], pszKey))
                return i;
        }
    }
    return cOrder;
}

VOID LiberDock_SortByOrder(LPCWSTR const *pKeys, UINT cKeys, LPCWSTR const *pOrder, UINT cOrder, UINT *pPermutation)
{
    ATLASSERT((pKeys || !cKeys) && (pOrder || !cOrder) && (pPermutation || !cKeys));
    for (UINT i = 0; i < cKeys; i++)
        pPermutation[i] = i;
    for (UINT i = 1; i < cKeys; i++)
    {
        UINT item = pPermutation[i], rank = OrderRank(pKeys[item], pOrder, cOrder), j = i;
        while (j > 0 && OrderRank(pKeys[pPermutation[j - 1]], pOrder, cOrder) > rank)
        {
            pPermutation[j] = pPermutation[j - 1];
            j--;
        }
        pPermutation[j] = item;
    }
}

UINT LiberDock_DropSlot(const RECT *pCells, UINT cCells, LONG x)
{
    ATLASSERT(pCells && cCells > 0);
    for (UINT i = 0; i < cCells; i++)
    {
        if (x < (pCells[i].left + pCells[i].right) / 2)
            return i;
    }
    return cCells - 1;
}

SIZE LiberDock_FitThumbnail(SIZE window, SIZE limit)
{
    SIZE size = { 0, 0 };

    if (window.cx <= 0 || window.cy <= 0 || limit.cx <= 0 || limit.cy <= 0)
        return size;
    if ((LONGLONG)window.cx * limit.cy >= (LONGLONG)window.cy * limit.cx)
    {
        size.cx = limit.cx;
        size.cy = max(1L, (LONG)((LONGLONG)window.cy * limit.cx / window.cx));
    }
    else
    {
        size.cy = limit.cy;
        size.cx = max(1L, (LONG)((LONGLONG)window.cx * limit.cy / window.cy));
    }
    return size;
}

VOID LiberDock_AppName(LPCWSTR pszPath, LPWSTR pszName, UINT cchName)
{
    ATLASSERT(pszName && cchName);
    pszName[0] = 0;
    if (!pszPath || !*pszPath)
        return;
    LPCWSTR name = pszPath;
    for (LPCWSTR p = pszPath; *p; p++)
    {
        if (*p == L'\\' || *p == L'/')
            name = p + 1;
    }
    lstrcpynW(pszName, name, (int)cchName);
    for (WCHAR *dot = pszName + lstrlenW(pszName); dot > pszName; dot--)
    {
        if (*dot == L'.')
        {
            *dot = 0;
            break;
        }
    }
}

VOID LiberDock_Metrics(UINT style, BOOL left, BOOL touched, LIBER_DOCK_METRICS *pMetrics)
{
    ATLASSERT(pMetrics);
    ZeroMemory(pMetrics, sizeof(*pMetrics));
    if (style > LIBER_TASKBAR_STYLE_CLASSIC)
        style = LIBER_TASKBAR_STYLE_CAPSULE;
    pMetrics->Style = style;
    pMetrics->Left = left;
    if (style == LIBER_TASKBAR_STYLE_CLASSIC)
    {
        pMetrics->Height = LIBER_TASKBAR_CLASSIC;
        pMetrics->Cell = LIBER_CLASSIC_CELL;
        pMetrics->Icon = LIBER_CLASSIC_ICON;
        pMetrics->PadX = LIBER_CLASSIC_PAD;
        pMetrics->PadY = LIBER_CLASSIC_PAD;
        pMetrics->Radius = 0;
        pMetrics->Reserve = LIBER_TASKBAR_CLASSIC;
        pMetrics->Gap = 0;
        return;
    }
    pMetrics->Touched = touched;
    pMetrics->Height = LIBER_DOCK_HEIGHT;
    pMetrics->Cell = LIBER_DOCK_CELL;
    pMetrics->Icon = LIBER_DOCK_ICON;
    pMetrics->PadX = LIBER_DOCK_PAD_X;
    pMetrics->PadY = LIBER_DOCK_PAD_Y;
    pMetrics->Radius = style == LIBER_TASKBAR_STYLE_ROUNDED ? LIBER_RADIUS_TASKBAR : LIBER_DOCK_HEIGHT / 2;
    pMetrics->Reserve = touched ? LIBER_TASKBAR_DOCK_TOUCHED : LIBER_TASKBAR_DOCK;
    pMetrics->Gap = (pMetrics->Reserve - LIBER_DOCK_HEIGHT) / 2;
}

VOID LiberDock_WorkArea(const RECT *prcMonitor, UINT dpi, const LIBER_DOCK_METRICS *pMetrics, RECT *prcWork)
{
    ATLASSERT(prcMonitor && prcWork && pMetrics && dpi != 0);
    *prcWork = *prcMonitor;
    prcWork->bottom = max(prcMonitor->top, prcMonitor->bottom - ScaleToDpi(pMetrics->Reserve, dpi));
}

VOID LiberDock_CellRect(const RECT *prcDock, UINT dpi, const LIBER_DOCK_METRICS *pMetrics, int cellsLeftDip, UINT cell,
                        RECT *prcCell)
{
    ATLASSERT(prcDock && prcCell && pMetrics && dpi != 0);
    int left = cellsLeftDip + (int)cell * (pMetrics->Cell + LIBER_DOCK_CELL_GAP);
    prcCell->left = prcDock->left + ScaleToDpi(left, dpi);
    prcCell->top = prcDock->top + ScaleToDpi(pMetrics->PadY, dpi);
    prcCell->right = prcDock->left + ScaleToDpi(left + pMetrics->Cell, dpi);
    prcCell->bottom = prcDock->top + ScaleToDpi(pMetrics->PadY + pMetrics->Cell, dpi);
}

VOID LiberDock_BlurBand(const RECT *prcMonitor, UINT dpi, const LIBER_DOCK_METRICS *pMetrics, UINT layer,
                        RECT *prcBand)
{
    ATLASSERT(prcMonitor && prcBand && pMetrics && dpi != 0 && layer < LIBER_BLUR_BAND_LAYERS);
    int height = ScaleToDpi(pMetrics->Reserve, dpi);
    *prcBand = *prcMonitor;
    prcBand->top = prcMonitor->bottom - height * (int)(LIBER_BLUR_BAND_LAYERS - layer) / LIBER_BLUR_BAND_LAYERS;
}

int LiberDock_Width(const LIBER_DOCK_METRICS *pMetrics, UINT cells)
{
    ATLASSERT(pMetrics && cells > 0);
    return (int)cells * pMetrics->Cell + ((int)cells - 1) * LIBER_DOCK_CELL_GAP + 2 * pMetrics->PadX;
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

VOID LiberDock_Layout(const RECT *prcMonitor, UINT dpi, UINT cells, int trayWidthDip, const LIBER_DOCK_METRICS *pMetrics,
                      LIBER_DOCK_LAYOUT *pLayout)
{
    ATLASSERT(prcMonitor && pMetrics && pLayout && dpi != 0);
    int cellsWidth = LiberDock_Width(pMetrics, cells);
    int dockHeight = ScaleToDpi(pMetrics->Height, dpi);
    int trayWidth = ScaleToDpi(trayWidthDip, dpi);
    int trayHeight = ScaleToDpi(LIBER_TRAY_HEIGHT, dpi);
    int inset = ScaleToDpi(LIBER_TRAY_INSET, dpi);
    LONG monitorWidth = prcMonitor->right - prcMonitor->left;
    int monitorWidthDip = MulDiv((int)monitorWidth, 96, (int)dpi);

    pLayout->Dock.bottom = prcMonitor->bottom - ScaleToDpi(pMetrics->Gap, dpi);
    pLayout->Dock.top = pLayout->Dock.bottom - dockHeight;
    if (pMetrics->Style == LIBER_TASKBAR_STYLE_CLASSIC)
    {
        pLayout->Dock.left = prcMonitor->left;
        pLayout->Dock.right = prcMonitor->right;
        pLayout->DockWidthDip = monitorWidthDip;
        pLayout->CellsLeft = pMetrics->Left ? pMetrics->PadX : max(pMetrics->PadX, (monitorWidthDip - cellsWidth) / 2 + pMetrics->PadX);
    }
    else
    {
        int dockWidth = ScaleToDpi(cellsWidth, dpi);
        pLayout->Dock.left = pMetrics->Left ? prcMonitor->left + inset : prcMonitor->left + (monitorWidth - dockWidth) / 2;
        pLayout->Dock.right = pLayout->Dock.left + dockWidth;
        pLayout->DockWidthDip = cellsWidth;
        pLayout->CellsLeft = pMetrics->PadX;
    }

    pLayout->Tray.right = prcMonitor->right - inset;
    pLayout->Tray.left = pLayout->Tray.right - trayWidth;
    pLayout->Tray.top = pLayout->Dock.top + (dockHeight - trayHeight) / 2;
    pLayout->Tray.bottom = pLayout->Tray.top + trayHeight;
}
