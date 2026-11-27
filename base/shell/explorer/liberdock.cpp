/*
 * PROJECT:     LiberNT Explorer
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Liber dock and tray islands
 * COPYRIGHT:   Copyright 2026 Eitan Hadar <eitanhadar4@gmail.com>
 */

#include "precomp.h"

#include <liberui.h>

#include "experience.h"
#include "liberdock.h"
#include "libershell.h"

using namespace LiberUI;

#define LIBER_CLOCK_WIDTH    84
#define LIBER_CHEVRON_WIDTH  32
#define LIBER_TRAY_BUTTON    36
#define LIBER_TRAY_PAD_X     8
#define LIBER_TRAY_GAP       2
#define LIBER_TRAY_SHADOW    0x59000000
#define LIBER_DOCK_FIXED_CELLS 2
#define LIBER_CELL_START     (-1)
#define LIBER_CELL_SEARCH    (-2)
#define LIBER_TIMER_CLOCK    1
#define LIBER_TIMER_REFRESH  2
#define LIBER_REFRESH_DELAY  60

static BOOL ImagePathOf(HWND hwnd, LPWSTR pszPath, DWORD cchPath)
{
    DWORD pid = 0, cch = cchPath;
    HANDLE process;
    BOOL bOk;

    GetWindowThreadProcessId(hwnd, &pid);
    if (!pid)
        return FALSE;
    process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!process)
        return FALSE;
    bOk = QueryFullProcessImageNameW(process, 0, pszPath, &cch);
    CloseHandle(process);
    return bOk && cch > 0;
}

struct LIBER_DOCK_ENTRY
{
    WCHAR szTarget[MAX_PATH];
    WCHAR szSource[MAX_PATH];
    HWND hwnd;
    HICON hIcon;
    BOOL bOwnsIcon;
    BOOL bPinned;
};

class CLiberDock;

class CDockCell : public Widget
{
public:
    CDockCell(CLiberDock *dock, int index) : m_Dock(dock), m_Index(index), m_Icon(NULL), m_Running(FALSE),
        m_Active(FALSE)
    {
        SetSize(LIBER_DOCK_CELL, LIBER_DOCK_CELL);
        SetHitTestable(TRUE);
        SetFocusable(TRUE);
    }

    void Set(HICON icon, BOOL running, BOOL active)
    {
        m_Icon = icon;
        m_Running = running;
        m_Active = active;
        Invalidate();
    }

    void OnClick();

protected:
    void PaintOverride(PaintContext &context)
    {
        RectF rc = ToRectF(Bounds());
        const ThemeTokens &tokens = context.Tokens;

        if (m_Active)
            context.Painter.FillEllipse(rc, tokens.Color(LIBER_COLOR_FILL_PLATE));
        else if (HasState(StateHover) || HasState(StatePressed))
            context.Painter.FillEllipse(rc, tokens.Color(LIBER_COLOR_FILL_PLATE_HOVER));

        float scale = HasState(StatePressed) ? 0.84f : 1.0f;
        if (m_Index == LIBER_CELL_START)
            PaintLogo(context, rc, 30.0f * scale);
        else
        {
            float size = (m_Index == LIBER_CELL_SEARCH ? 24.0f : (float)LIBER_DOCK_ICON) * scale;
            RectF icon = { rc.X + (rc.Width - size) / 2, rc.Y + (rc.Height - size) / 2, size, size };
            if (m_Index == LIBER_CELL_SEARCH)
                context.Painter.Icon(L"search_24_regular", icon, tokens.Color(LIBER_COLOR_TEXT1));
            else if (m_Icon)
                context.Painter.Image(m_Icon, icon);
        }

        if (m_Running)
        {
            float dot = (float)LIBER_DOCK_RUNNING_DOT;
            RectF rcDot = { rc.X + (rc.Width - dot) / 2, rc.Y + rc.Height - dot - 2.0f, dot, dot };
            context.Painter.FillEllipse(rcDot, tokens.Color(LIBER_COLOR_TEXT2));
        }
    }

    static void PaintLogo(PaintContext &context, const RectF &rc, float size)
    {
        static const float Bars[4][4] = { { 0.0f, 5.8333f, 5.8333f, 26.25f },
                                          { 0.0f, 20.4167f, 20.4167f, 26.25f },
                                          { 7.5833f, 1.75f, 28.0f, 7.5833f },
                                          { 22.1667f, 1.75f, 28.0f, 22.1667f } };
        float x = rc.X + (rc.Width - size) / 2, y = rc.Y + (rc.Height - size) / 2, unit = size / 28.0f;
        /* FIXME: the spec fills the mark with a --logo-from to --logo-to gradient; it is drawn in the middle colour */
        Argb color = context.Tokens.IsDark() ? 0xFF67C5FA : 0xFF126FCE;

        for (UINT i = 0; i < _countof(Bars); i++)
        {
            RectF bar = { x + Bars[i][0] * unit, y + Bars[i][1] * unit, (Bars[i][2] - Bars[i][0]) * unit,
                          (Bars[i][3] - Bars[i][1]) * unit };
            context.Painter.FillRect(bar, color);
        }
    }

private:
    CLiberDock *m_Dock;
    int m_Index;
    HICON m_Icon;
    BOOL m_Running;
    BOOL m_Active;
};

class CTrayButton : public Widget
{
public:
    CTrayButton(CLiberDock *dock, int width) : m_Dock(dock), m_Open(FALSE)
    {
        SetSize(width, LIBER_TRAY_BUTTON);
        SetHitTestable(TRUE);
        SetFocusable(TRUE);
    }

    void SetOpen(BOOL open)
    {
        if (open != m_Open)
        {
            m_Open = open;
            Invalidate();
        }
    }

protected:
    virtual void PaintContent(PaintContext &context, const RectF &rc) = 0;

    void PaintOverride(PaintContext &context)
    {
        RectF rc = ToRectF(Bounds());
        const ThemeTokens &tokens = context.Tokens;

        if (m_Open)
            context.Painter.FillRoundRect(rc, rc.Height / 2, tokens.Color(LIBER_COLOR_FILL_PLATE));
        else if (HasState(StateHover) || HasState(StatePressed))
            context.Painter.FillRoundRect(rc, rc.Height / 2, tokens.Color(LIBER_COLOR_FILL_PLATE_HOVER));
        PaintContent(context, rc);
    }

    static void ShadowedIcon(PaintContext &context, LPCWSTR name, const RectF &rc)
    {
        RectF shadow = { rc.X, rc.Y + 1.0f, rc.Width, rc.Height };
        context.Painter.Icon(name, shadow, LIBER_TRAY_SHADOW);
        context.Painter.Icon(name, rc, context.Tokens.Color(LIBER_COLOR_TEXT1));
    }

    static void ShadowedText(PaintContext &context, LPCWSTR text, const RectF &rc, UINT flags)
    {
        RectF shadow = { rc.X, rc.Y + 1.0f, rc.Width, rc.Height };
        context.Painter.Text(text, shadow, LIBER_TYPE_CAPTION, LIBER_TRAY_SHADOW, flags);
        context.Painter.Text(text, rc, LIBER_TYPE_CAPTION, context.Tokens.Color(LIBER_COLOR_TEXT1), flags);
    }

    CLiberDock *m_Dock;
    BOOL m_Open;
};

class CTrayChevron : public CTrayButton
{
public:
    CTrayChevron(CLiberDock *dock) : CTrayButton(dock, LIBER_CHEVRON_WIDTH) {}
    void OnClick();

protected:
    void PaintContent(PaintContext &context, const RectF &rc)
    {
        RectF icon = { rc.X + (rc.Width - 12.0f) / 2, rc.Y + (rc.Height - 12.0f) / 2, 12.0f, 12.0f };
        ShadowedIcon(context, L"chevron_up_12_regular", icon);
    }
};

class CSystemPill : public CTrayButton
{
public:
    CSystemPill(CLiberDock *dock, BOOL battery)
        : CTrayButton(dock, LiberDock_SystemPillWidth(battery)), m_Battery(battery)
    {
    }

    void OnClick();

protected:
    void PaintContent(PaintContext &context, const RectF &rc)
    {
        static LPCWSTR const Icons[] = { L"wifi_1_20_regular", L"speaker_2_20_regular", L"battery_10_20_regular" };
        UINT count = m_Battery ? 3 : 2;

        for (UINT i = 0; i < count; i++)
        {
            RectF icon = { rc.X + (float)(LIBER_TRAY_BUTTON_PAD + i * (20 + LIBER_TRAY_ICON_GAP)),
                           rc.Y + (rc.Height - 20.0f) / 2, 20.0f, 20.0f };
            ShadowedIcon(context, Icons[i], icon);
        }
    }

private:
    BOOL m_Battery;
};

class CTrayClock : public CTrayButton
{
public:
    CTrayClock(CLiberDock *dock) : CTrayButton(dock, LIBER_CLOCK_WIDTH)
    {
        m_szTime[0] = m_szDate[0] = 0;
    }

    BOOL Update()
    {
        SYSTEMTIME st;
        WCHAR szTime[32], szDate[32];

        GetLocalTime(&st);
        GetTimeFormatW(LOCALE_USER_DEFAULT, TIME_NOSECONDS, &st, NULL, szTime, _countof(szTime));
        GetDateFormatW(LOCALE_USER_DEFAULT, DATE_SHORTDATE, &st, NULL, szDate, _countof(szDate));
        if (!wcscmp(szTime, m_szTime) && !wcscmp(szDate, m_szDate))
            return FALSE;
        StringCchCopyW(m_szTime, _countof(m_szTime), szTime);
        StringCchCopyW(m_szDate, _countof(m_szDate), szDate);
        Invalidate();
        return TRUE;
    }

    void OnClick();

protected:
    void PaintContent(PaintContext &context, const RectF &rc)
    {
        float pad = (float)LIBER_TRAY_BUTTON_PAD;
        RectF time = { rc.X + pad, rc.Y + 2.0f, rc.Width - 2 * pad, 16.0f };
        RectF date = { rc.X + pad, rc.Y + 18.0f, rc.Width - 2 * pad, 16.0f };
        ShadowedText(context, m_szTime, time, TextRight | TextMiddle);
        ShadowedText(context, m_szDate, date, TextRight | TextMiddle);
    }

private:
    WCHAR m_szTime[32];
    WCHAR m_szDate[32];
};

class CLiberDock
{
public:
    CLiberDock() : m_Tray(NULL), m_hwndSink(NULL), m_uShellHook(0), m_Count(0), m_DockRoot(NULL), m_TrayRoot(NULL),
        m_Pill(NULL), m_Chevron(NULL), m_TrayWidth(0), m_Clock(NULL), m_Dpi(96)
    {
        ZeroMemory(m_Entries, sizeof(m_Entries));
        ZeroMemory(m_Cells, sizeof(m_Cells));
    }

    ~CLiberDock()
    {
        Stop();
    }

    HRESULT Start(ITrayWindow *tray);
    void Stop();
    void OnCellClick(int index, const RECT &bounds);
    void OnClockClick(const RECT &bounds);
    void OnPillClick();
    void OpenStart();
    UINT Dpi() const { return m_Dpi; }
    void OnChevronClick();

private:
    static LRESULT CALLBACK SinkProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    static BOOL CALLBACK PinProc(PCWSTR pszShortcut, PCWSTR pszTarget, PCWSTR pszAppId, const FILETIME *pCreationTime,
                                 LPARAM lParam);
    static BOOL CALLBACK WindowProc(HWND hwnd, LPARAM lParam);

    void BuildTheme();
    void ClearEntries();
    void Refresh();
    void Place();
    RECT ScreenRect(Surface &surface, const RECT &dip);

    ITrayWindow *m_Tray;
    HWND m_hwndSink;
    UINT m_uShellHook;
    ThemeTokens m_Tokens;
    Surface m_DockSurface;
    Surface m_TraySurface;
    LIBER_DOCK_ENTRY m_Entries[LIBER_DOCK_MAX_ITEMS];
    UINT m_Count;
    CLiberPanel *m_DockRoot;
    StackPanel *m_TrayRoot;
    CSystemPill *m_Pill;
    CTrayChevron *m_Chevron;
    ThemeTokens m_TrayTokens;
    int m_TrayWidth;
    CDockCell *m_Cells[LIBER_DOCK_MAX_ITEMS + LIBER_DOCK_FIXED_CELLS];
    CTrayClock *m_Clock;
    UINT m_Dpi;
    LIBER_DOCK_LAYOUT m_Layout;
};

static CLiberDock *g_pLiberDock;

void CDockCell::OnClick()
{
    m_Dock->OnCellClick(m_Index, Bounds());
}

void CTrayClock::OnClick()
{
    m_Dock->OnClockClick(Bounds());
}

void CSystemPill::OnClick()
{
    m_Dock->OnPillClick();
}

void CTrayChevron::OnClick()
{
    m_Dock->OnChevronClick();
}

void CLiberDock::BuildTheme()
{
    LiberShell_BuildTokens(&m_Tokens);
    LiberShell_BuildWallpaperInkTokens(&m_TrayTokens);
}

BOOL CALLBACK CLiberDock::PinProc(PCWSTR pszShortcut, PCWSTR pszTarget, PCWSTR pszAppId, const FILETIME *pCreationTime,
                                  LPARAM lParam)
{
    CLiberDock *self = (CLiberDock *)lParam;
    SHFILEINFOW sfi;

    UNREFERENCED_PARAMETER(pszAppId);
    UNREFERENCED_PARAMETER(pCreationTime);
    if (self->m_Count >= LIBER_DOCK_MAX_ITEMS || !pszTarget || !*pszTarget)
        return self->m_Count < LIBER_DOCK_MAX_ITEMS;

    LIBER_DOCK_ENTRY &entry = self->m_Entries[self->m_Count++];
    ZeroMemory(&entry, sizeof(entry));
    StringCchCopyW(entry.szTarget, _countof(entry.szTarget), pszTarget);
    StringCchCopyW(entry.szSource, _countof(entry.szSource), pszShortcut && *pszShortcut ? pszShortcut : pszTarget);
    entry.bPinned = TRUE;
    ZeroMemory(&sfi, sizeof(sfi));
    if ((SHGetFileInfoW(pszTarget, 0, &sfi, sizeof(sfi), SHGFI_ICON | SHGFI_LARGEICON) && sfi.hIcon) ||
        (pszShortcut && *pszShortcut &&
         SHGetFileInfoW(pszShortcut, 0, &sfi, sizeof(sfi), SHGFI_ICON | SHGFI_LARGEICON) && sfi.hIcon))
    {
        entry.hIcon = sfi.hIcon;
        entry.bOwnsIcon = TRUE;
    }
    return TRUE;
}

BOOL CALLBACK CLiberDock::WindowProc(HWND hwnd, LPARAM lParam)
{
    CLiberDock *self = (CLiberDock *)lParam;
    WCHAR szImage[MAX_PATH];
    UINT i;

    if (hwnd == self->m_DockSurface.Window() || hwnd == self->m_TraySurface.Window() ||
        (self->m_Tray && hwnd == self->m_Tray->GetHWND()) || !LiberDock_IsTaskWindow(hwnd))
    {
        return TRUE;
    }
    if (!ImagePathOf(hwnd, szImage, _countof(szImage)))
        szImage[0] = 0;

    for (i = 0; i < self->m_Count; i++)
    {
        if (self->m_Entries[i].hwnd == NULL && LiberDock_SameApp(self->m_Entries[i].szTarget, szImage))
        {
            self->m_Entries[i].hwnd = hwnd;
            return TRUE;
        }
        if (self->m_Entries[i].hwnd != NULL && LiberDock_SameApp(self->m_Entries[i].szTarget, szImage))
            return TRUE;
    }
    if (self->m_Count >= LIBER_DOCK_MAX_ITEMS)
        return FALSE;

    LIBER_DOCK_ENTRY &entry = self->m_Entries[self->m_Count++];
    ZeroMemory(&entry, sizeof(entry));
    StringCchCopyW(entry.szTarget, _countof(entry.szTarget), szImage);
    entry.hwnd = hwnd;
    DWORD_PTR result = 0;
    if (SendMessageTimeoutW(hwnd, WM_GETICON, ICON_BIG, 0, SMTO_ABORTIFHUNG, 100, &result) && result)
        entry.hIcon = (HICON)result;
    if (!entry.hIcon)
        entry.hIcon = (HICON)GetClassLongPtrW(hwnd, GCLP_HICON);
    if (!entry.hIcon)
        entry.hIcon = LoadIconW(NULL, (LPCWSTR)IDI_APPLICATION);
    return TRUE;
}

void CLiberDock::ClearEntries()
{
    for (UINT i = 0; i < m_Count; i++)
    {
        if (m_Entries[i].bOwnsIcon && m_Entries[i].hIcon)
            DestroyIcon(m_Entries[i].hIcon);
    }
    ZeroMemory(m_Entries, sizeof(m_Entries));
    m_Count = 0;
}

RECT CLiberDock::ScreenRect(Surface &surface, const RECT &dip)
{
    RECT window = { 0 }, rc;

    GetWindowRect(surface.Window(), &window);
    rc.left = window.left + ScaleToDpi(dip.left, m_Dpi);
    rc.top = window.top + ScaleToDpi(dip.top, m_Dpi);
    rc.right = window.left + ScaleToDpi(dip.right, m_Dpi);
    rc.bottom = window.top + ScaleToDpi(dip.bottom, m_Dpi);
    return rc;
}

void CLiberDock::Place()
{
    HMONITOR monitor = MonitorFromPoint(POINT(), MONITOR_DEFAULTTOPRIMARY);
    MONITORINFO mi = { sizeof(mi) };
    UINT cells = m_Count + LIBER_DOCK_FIXED_CELLS;

    if (!GetMonitorInfoW(monitor, &mi))
        SetRect(&mi.rcMonitor, 0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN));
    LiberDock_Layout(&mi.rcMonitor, m_Dpi, cells, m_TrayWidth, &m_Layout);
    m_DockSurface.SetPosition(m_Layout.Dock.left, m_Layout.Dock.top, LiberDock_Width(cells),
                              LIBER_DOCK_HEIGHT);
    m_TraySurface.SetPosition(m_Layout.Tray.left, m_Layout.Tray.top, m_TrayWidth, LIBER_TRAY_HEIGHT);

    RECT work;
    LiberDock_WorkArea(&mi.rcMonitor, m_Dpi, &work);
    if (!EqualRect(&work, &mi.rcWork))
        SystemParametersInfoW(SPI_SETWORKAREA, 0, &work, SPIF_SENDCHANGE);
}

void CLiberDock::Refresh()
{
    HWND foreground = GetForegroundWindow();
    LIBER_DOCK_ENTRY *previous = (LIBER_DOCK_ENTRY *)LocalAlloc(LPTR, sizeof(m_Entries));
    UINT previousCount = m_Count;
    BOOL same;

    if (!previous)
        return;
    CopyMemory(previous, m_Entries, sizeof(m_Entries));
    ZeroMemory(m_Entries, sizeof(m_Entries));
    m_Count = 0;
    if (!TaskbarPin_IsDisabled())
        TaskbarPin_Enum(PinProc, (LPARAM)this);
    EnumWindows(WindowProc, (LPARAM)this);

    same = m_Cells[0] != NULL && m_Count == previousCount;
    for (UINT i = 0; same && i < m_Count; i++)
        same = previous[i].hwnd == m_Entries[i].hwnd && !lstrcmpiW(previous[i].szTarget, m_Entries[i].szTarget);

    if (same)
    {
        for (UINT i = 0; i < m_Count; i++)
        {
            if (!previous[i].bOwnsIcon && !m_Entries[i].bOwnsIcon)
                previous[i].hIcon = m_Entries[i].hIcon;
        }
        ClearEntries();
        CopyMemory(m_Entries, previous, sizeof(m_Entries));
        m_Count = previousCount;
        LocalFree(previous);
        m_Cells[0]->Set(NULL, FALSE, LiberStart_IsOpen());
        for (UINT i = 0; i < m_Count; i++)
        {
            m_Cells[i + LIBER_DOCK_FIXED_CELLS]->Set(m_Entries[i].hIcon, m_Entries[i].hwnd != NULL,
                                                     m_Entries[i].hwnd != NULL && m_Entries[i].hwnd == foreground &&
                                                         !LiberStart_IsOpen());
        }
        Place();
        m_DockSurface.Render(AnimationClock::Now());
        m_TraySurface.Render(AnimationClock::Now());
        return;
    }

    for (UINT i = 0; i < previousCount; i++)
    {
        if (previous[i].bOwnsIcon && previous[i].hIcon)
            DestroyIcon(previous[i].hIcon);
    }
    LocalFree(previous);
    for (UINT i = 0; i < _countof(m_Cells); i++)
    {
        if (m_Cells[i])
        {
            m_DockRoot->RemoveChild(m_Cells[i]);
            delete m_Cells[i];
            m_Cells[i] = NULL;
        }
    }

    m_Cells[0] = new CDockCell(this, LIBER_CELL_START);
    m_Cells[0]->Set(NULL, FALSE, LiberStart_IsOpen());
    m_DockRoot->AddChild(m_Cells[0]);
    m_Cells[1] = new CDockCell(this, LIBER_CELL_SEARCH);
    m_DockRoot->AddChild(m_Cells[1]);
    for (UINT i = 0; i < m_Count; i++)
    {
        CDockCell *cell = new CDockCell(this, (int)i);
        cell->Set(m_Entries[i].hIcon, m_Entries[i].hwnd != NULL,
                  m_Entries[i].hwnd != NULL && m_Entries[i].hwnd == foreground && !LiberStart_IsOpen());
        m_Cells[i + LIBER_DOCK_FIXED_CELLS] = cell;
        m_DockRoot->AddChild(cell);
    }

    Place();
    m_DockSurface.Render(AnimationClock::Now());
    m_TraySurface.Render(AnimationClock::Now());
}

void CLiberDock::OnCellClick(int index, const RECT &bounds)
{
    if (index == LIBER_CELL_START || index == LIBER_CELL_SEARCH)
    {
        RECT rc = ScreenRect(m_DockSurface, bounds);
        LiberStart_Toggle(&rc);
        return;
    }
    if (index < 0)
        return;
    if ((UINT)index >= m_Count)
        return;

    LIBER_DOCK_ENTRY &entry = m_Entries[index];
    if (entry.hwnd && IsWindow(entry.hwnd))
    {
        if (entry.hwnd == GetForegroundWindow() && !IsIconic(entry.hwnd))
            ShowWindowAsync(entry.hwnd, SW_MINIMIZE);
        else
            SwitchToThisWindow(entry.hwnd, TRUE);
        return;
    }
    if (entry.bPinned)
    {
        HANDLE process = NULL;
        HRESULT hr = TaskbarPin_Launch(entry.szSource, &process);
        if (process)
            CloseHandle(process);
        if (FAILED(hr) && hr != HRESULT_FROM_WIN32(ERROR_CANCELLED))
            MessageBeep(MB_ICONERROR);
    }
}

void CLiberDock::OpenStart()
{
    if (m_Cells[0])
        OnCellClick(LIBER_CELL_START, m_Cells[0]->Bounds());
}

void CLiberDock::OnPillClick()
{
    RECT rc;

    GetWindowRect(m_TraySurface.Window(), &rc);
    LiberControlCenter_Toggle(&rc);
}

void CLiberDock::OnChevronClick()
{
    RECT rc;

    GetWindowRect(m_TraySurface.Window(), &rc);
    TrayOverflow_ToggleAll(m_hwndSink, &rc);
}

void CLiberDock::OnClockClick(const RECT &bounds)
{
    RECT rc = ScreenRect(m_TraySurface, bounds);
    LiberCalendar_Toggle(&rc);
}

LRESULT CALLBACK CLiberDock::SinkProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    CLiberDock *self = g_pLiberDock;

    if (self && msg == self->m_uShellHook && self->m_uShellHook != 0)
    {
        switch (wParam & 0x7FFF)
        {
            case HSHELL_WINDOWCREATED:
            case HSHELL_WINDOWDESTROYED:
            case HSHELL_WINDOWACTIVATED:
            case HSHELL_REDRAW:
                SetTimer(hwnd, LIBER_TIMER_REFRESH, LIBER_REFRESH_DELAY, NULL);
                break;
        }
        return 0;
    }
    if (self && msg == WM_TIMER)
    {
        if (wParam == LIBER_TIMER_REFRESH)
        {
            KillTimer(hwnd, LIBER_TIMER_REFRESH);
            self->Refresh();
        }
        else if (wParam == LIBER_TIMER_CLOCK && self->m_Clock && self->m_Clock->Update())
        {
            self->m_TraySurface.Render(AnimationClock::Now());
        }
        return 0;
    }
    if (self && (msg == WM_SETTINGCHANGE || msg == WM_DISPLAYCHANGE))
    {
        self->BuildTheme();
        LiberShell_ApplyFont(&self->m_DockSurface);
        LiberShell_ApplyFont(&self->m_TraySurface);
        self->m_DockSurface.SetTokens(&self->m_Tokens);
        self->m_TraySurface.SetTokens(&self->m_TrayTokens);
        LiberWidgets_Sync(GetShellWindow());
        SetTimer(hwnd, LIBER_TIMER_REFRESH, LIBER_REFRESH_DELAY, NULL);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

HRESULT CLiberDock::Start(ITrayWindow *tray)
{
    WNDCLASSEXW wc = { sizeof(wc) };
    HDC screen;

    ATLASSERT(m_hwndSink == NULL);
    if (!LiberUI::Startup())
        return E_FAIL;

    m_Tray = tray;
    screen = GetDC(NULL);
    if (screen)
    {
        m_Dpi = (UINT)GetDeviceCaps(screen, LOGPIXELSX);
        ReleaseDC(NULL, screen);
    }
    BuildTheme();

    wc.lpfnWndProc = SinkProc;
    wc.hInstance = hExplorerInstance;
    wc.lpszClassName = L"LiberDockSink";
    RegisterClassExW(&wc);
    m_hwndSink = CreateWindowExW(WS_EX_TOOLWINDOW, wc.lpszClassName, NULL, WS_POPUP, 0, 0, 0, 0, NULL, NULL,
                                 hExplorerInstance, NULL);
    if (!m_hwndSink)
        return HRESULT_FROM_WIN32(GetLastError());

    DWORD exStyle = WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE;
    if (!m_DockSurface.Create(NULL, L"Liber Dock", exStyle) || !m_TraySurface.Create(NULL, L"Liber Tray", exStyle))
        return E_FAIL;
    m_DockSurface.SetInputShape((float)LIBER_DOCK_HEIGHT / 2.0f);
    m_TraySurface.SetInputShape((float)LIBER_TRAY_HEIGHT / 2.0f);

    m_DockRoot = new CLiberPanel(Orientation::Horizontal, LIBER_DOCK_CELL_GAP, (float)LIBER_DOCK_HEIGHT / 2.0f,
                                 LIBER_MATERIAL_REGULAR);
    Thickness pad = { LIBER_DOCK_PAD_X, LIBER_DOCK_PAD_Y, LIBER_DOCK_PAD_X, LIBER_DOCK_PAD_Y };
    m_DockRoot->SetPadding(pad);
    SYSTEM_POWER_STATUS power;
    BOOL battery = GetSystemPowerStatus(&power) && power.BatteryFlag != 128 && power.BatteryFlag != 255;
    m_TrayRoot = new StackPanel(Orientation::Horizontal, LIBER_TRAY_GAP);
    Thickness trayPad = { LIBER_TRAY_PAD_X, (LIBER_TRAY_HEIGHT - LIBER_TRAY_BUTTON) / 2, LIBER_TRAY_PAD_X,
                          (LIBER_TRAY_HEIGHT - LIBER_TRAY_BUTTON) / 2 };
    m_TrayRoot->SetPadding(trayPad);
    m_Clock = new CTrayClock(this);
    m_Chevron = new CTrayChevron(this);
    m_Pill = new CSystemPill(this, battery);
    m_TrayRoot->AddChild(m_Chevron);
    m_TrayRoot->AddChild(m_Pill);
    m_TrayRoot->AddChild(m_Clock);
    m_TrayWidth = 2 * LIBER_TRAY_PAD_X + LIBER_CHEVRON_WIDTH + LiberDock_SystemPillWidth(battery) + LIBER_CLOCK_WIDTH +
                  2 * LIBER_TRAY_GAP;
    m_Clock->Update();

    LiberShell_ApplyFont(&m_DockSurface);
    LiberShell_ApplyFont(&m_TraySurface);
    m_DockSurface.SetDpi(m_Dpi);
    m_TraySurface.SetDpi(m_Dpi);
    m_DockSurface.SetTokens(&m_Tokens);
    m_TraySurface.SetTokens(&m_TrayTokens);
    m_DockSurface.SetRoot(m_DockRoot);
    m_TraySurface.SetRoot(m_TrayRoot);

    g_pLiberDock = this;
    Refresh();
    ShowWindow(m_DockSurface.Window(), SW_SHOWNOACTIVATE);
    ShowWindow(m_TraySurface.Window(), SW_SHOWNOACTIVATE);

    m_uShellHook = RegisterWindowMessageW(L"SHELLHOOK");
    RegisterShellHookWindow(m_hwndSink);
    SetTimer(m_hwndSink, LIBER_TIMER_CLOCK, 1000, NULL);

    if (m_Tray && m_Tray->GetHWND())
        ShowWindow(m_Tray->GetHWND(), SW_HIDE);
    return S_OK;
}

void CLiberDock::Stop()
{
    LiberControlCenter_Close();
    LiberCalendar_Destroy();
    LiberStart_Destroy();
    if (m_hwndSink)
    {
        DeregisterShellHookWindow(m_hwndSink);
        KillTimer(m_hwndSink, LIBER_TIMER_CLOCK);
        KillTimer(m_hwndSink, LIBER_TIMER_REFRESH);
        DestroyWindow(m_hwndSink);
        m_hwndSink = NULL;
    }
    m_DockSurface.SetRoot(NULL);
    m_TraySurface.SetRoot(NULL);
    m_DockSurface.Destroy();
    m_TraySurface.Destroy();
    delete m_DockRoot;
    delete m_TrayRoot;
    m_DockRoot = NULL;
    m_TrayRoot = NULL;
    m_Pill = NULL;
    m_Chevron = NULL;
    m_Clock = NULL;
    ZeroMemory(m_Cells, sizeof(m_Cells));
    ClearEntries();
    if (g_pLiberDock == this)
        g_pLiberDock = NULL;
    if (m_Tray && m_Tray->GetHWND())
    {
        ShowWindow(m_Tray->GetHWND(), SW_SHOW);
        SendMessageW(m_Tray->GetHWND(), WM_SIZE, SIZE_RESTORED, 0);
    }
    if (m_Tray)
        LiberUI::Shutdown();
    m_Tray = NULL;
}

HRESULT LiberDock_Start(ITrayWindow *Tray)
{
    if (g_pLiberDock)
        return S_FALSE;

    CLiberDock *dock = new CLiberDock();
    if (!dock)
        return E_OUTOFMEMORY;
    HRESULT hr = dock->Start(Tray);
    if (FAILED(hr))
        delete dock;
    return hr;
}

BOOL LiberDock_OpenStart(VOID)
{
    if (!g_pLiberDock)
        return FALSE;
    g_pLiberDock->OpenStart();
    return TRUE;
}

BOOL LiberDock_ReservedWorkArea(HMONITOR hMonitor, RECT *prcWork)
{
    MONITORINFO mi = { sizeof(mi) };

    if (!g_pLiberDock || hMonitor != MonitorFromPoint(POINT(), MONITOR_DEFAULTTOPRIMARY) ||
        !GetMonitorInfoW(hMonitor, &mi))
    {
        return FALSE;
    }
    LiberDock_WorkArea(&mi.rcMonitor, g_pLiberDock->Dpi(), prcWork);
    return TRUE;
}

VOID LiberDock_Stop(VOID)
{
    delete g_pLiberDock;
}
