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

using namespace LiberUI;

#define LIBER_TRAY_WIDTH     112
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
        float inset = HasState(StatePressed) ? 4.0f : 0.0f;

        if (m_Active || HasState(StateHover) || HasState(StatePressed))
        {
            RectF plate = { rc.X + inset, rc.Y + inset, rc.Width - 2 * inset, rc.Height - 2 * inset };
            context.Painter.FillEllipse(plate, tokens.Color(m_Active ? LIBER_COLOR_FILL_PLATE : LIBER_COLOR_FILL_PLATE_HOVER));
        }

        float size = m_Index < 0 ? 24.0f : (float)LIBER_DOCK_ICON;
        if (HasState(StatePressed))
            size *= 0.84f;
        RectF icon = { rc.X + (rc.Width - size) / 2, rc.Y + (rc.Height - size) / 2, size, size };
        if (m_Index < 0)
            context.Painter.Icon(L"apps_20_regular", icon, tokens.Color(LIBER_COLOR_ACCENT));
        else if (m_Icon)
            context.Painter.Image(m_Icon, icon);

        if (m_Running)
        {
            float dot = (float)LIBER_DOCK_RUNNING_DOT;
            RectF rcDot = { rc.X + (rc.Width - dot) / 2, rc.Y + rc.Height - dot - 1.0f, dot, dot };
            context.Painter.FillEllipse(rcDot, tokens.Color(m_Active ? LIBER_COLOR_ACCENT : LIBER_COLOR_TEXT3));
        }
    }

private:
    CLiberDock *m_Dock;
    int m_Index;
    HICON m_Icon;
    BOOL m_Running;
    BOOL m_Active;
};

class CIsland : public StackPanel
{
public:
    CIsland(float radius) : StackPanel(Orientation::Horizontal, 0), m_Radius(radius) {}

protected:
    void PaintOverride(PaintContext &context)
    {
        RectF rc = ToRectF(Bounds());
        GlassTint tint = context.Tokens.Material(LIBER_MATERIAL_REGULAR, 0.5);

        context.Painter.FillRoundRect(rc, m_Radius, OklchToArgb(tint.Color, 1.0));
        context.Painter.StrokeRoundRect(rc, m_Radius, context.Tokens.Color(LIBER_COLOR_STROKE_CARD), 1.0f);
    }

private:
    float m_Radius;
};

class CTrayClock : public Widget
{
public:
    CTrayClock(CLiberDock *dock) : m_Dock(dock)
    {
        m_szTime[0] = m_szDate[0] = 0;
        SetSize(LIBER_TRAY_WIDTH, LIBER_TRAY_HEIGHT);
        SetHitTestable(TRUE);
        SetFocusable(TRUE);
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
    void PaintOverride(PaintContext &context)
    {
        RectF rc = ToRectF(Bounds());
        const ThemeTokens &tokens = context.Tokens;

        if (HasState(StateHover) || HasState(StatePressed))
            context.Painter.FillRoundRect(rc, rc.Height / 2, tokens.Color(LIBER_COLOR_FILL_SUBTLE_HOVER));
        RectF time = { rc.X, rc.Y + 6.0f, rc.Width, 18.0f };
        RectF date = { rc.X, rc.Y + 24.0f, rc.Width, 16.0f };
        context.Painter.Text(m_szTime, time, LIBER_TYPE_CAPTION, tokens.Color(LIBER_COLOR_TEXT1), TextCenter | TextMiddle);
        context.Painter.Text(m_szDate, date, LIBER_TYPE_CAPTION, tokens.Color(LIBER_COLOR_TEXT2), TextCenter | TextMiddle);
    }

private:
    CLiberDock *m_Dock;
    WCHAR m_szTime[32];
    WCHAR m_szDate[32];
};

class CLiberDock
{
public:
    CLiberDock() : m_Tray(NULL), m_hwndSink(NULL), m_uShellHook(0), m_Count(0), m_DockRoot(NULL), m_TrayRoot(NULL),
        m_Clock(NULL), m_Dpi(96)
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
    CIsland *m_DockRoot;
    CIsland *m_TrayRoot;
    CDockCell *m_Cells[LIBER_DOCK_MAX_ITEMS + 1];
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

void CLiberDock::BuildTheme()
{
    ThemeParams params = { LiberExperience_IsDark() ? ThemeMode::Dark : ThemeMode::Light,
                           AccentPreset(LIBER_ACCENT_AZURE), SurfaceTint::Soft };
    m_Tokens.Build(params);
}

BOOL CALLBACK CLiberDock::PinProc(PCWSTR pszShortcut, PCWSTR pszTarget, PCWSTR pszAppId, const FILETIME *pCreationTime,
                                  LPARAM lParam)
{
    CLiberDock *self = (CLiberDock *)lParam;
    SHFILEINFOW sfi;

    UNREFERENCED_PARAMETER(pszShortcut);
    UNREFERENCED_PARAMETER(pszAppId);
    UNREFERENCED_PARAMETER(pCreationTime);
    if (self->m_Count >= LIBER_DOCK_MAX_ITEMS || !pszTarget || !*pszTarget)
        return self->m_Count < LIBER_DOCK_MAX_ITEMS;

    LIBER_DOCK_ENTRY &entry = self->m_Entries[self->m_Count++];
    ZeroMemory(&entry, sizeof(entry));
    StringCchCopyW(entry.szTarget, _countof(entry.szTarget), pszTarget);
    entry.bPinned = TRUE;
    ZeroMemory(&sfi, sizeof(sfi));
    if (SHGetFileInfoW(pszShortcut && *pszShortcut ? pszShortcut : pszTarget, 0, &sfi, sizeof(sfi),
                       SHGFI_ICON | SHGFI_LARGEICON) && sfi.hIcon)
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
    UINT cells = m_Count + 1;

    if (!GetMonitorInfoW(monitor, &mi))
        SetRect(&mi.rcMonitor, 0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN));
    LiberDock_Layout(&mi.rcMonitor, m_Dpi, cells, LIBER_TRAY_WIDTH, &m_Layout);
    m_DockSurface.SetPosition(m_Layout.Dock.left, m_Layout.Dock.top, (int)cells * LIBER_DOCK_CELL + 2 * LIBER_DOCK_PAD,
                              LIBER_DOCK_HEIGHT);
    m_TraySurface.SetPosition(m_Layout.Tray.left, m_Layout.Tray.top, LIBER_TRAY_WIDTH, LIBER_TRAY_HEIGHT);
}

void CLiberDock::Refresh()
{
    HWND foreground = GetForegroundWindow();

    for (UINT i = 0; i <= LIBER_DOCK_MAX_ITEMS; i++)
    {
        if (m_Cells[i])
        {
            m_DockRoot->RemoveChild(m_Cells[i]);
            delete m_Cells[i];
            m_Cells[i] = NULL;
        }
    }
    ClearEntries();
    if (!TaskbarPin_IsDisabled())
        TaskbarPin_Enum(PinProc, (LPARAM)this);
    EnumWindows(WindowProc, (LPARAM)this);

    m_Cells[0] = new CDockCell(this, -1);
    m_Cells[0]->Set(NULL, FALSE, StartMenu2_IsVisible());
    m_DockRoot->AddChild(m_Cells[0]);
    for (UINT i = 0; i < m_Count; i++)
    {
        m_Cells[i + 1] = new CDockCell(this, (int)i);
        m_Cells[i + 1]->Set(m_Entries[i].hIcon, m_Entries[i].hwnd != NULL,
                            m_Entries[i].hwnd != NULL && m_Entries[i].hwnd == foreground);
        m_DockRoot->AddChild(m_Cells[i + 1]);
    }

    Place();
    m_DockSurface.Render(AnimationClock::Now());
    m_TraySurface.Render(AnimationClock::Now());
}

void CLiberDock::OnCellClick(int index, const RECT &bounds)
{
    if (index < 0)
    {
        if (StartMenu2_IsVisible())
        {
            StartMenu2_Hide();
            return;
        }
        RECT rc = ScreenRect(m_DockSurface, bounds);
        StartMenu2_Popup(&rc, ABE_BOTTOM);
        return;
    }
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
        TaskbarPin_Launch(entry.szTarget, NULL);
}

void CLiberDock::OnClockClick(const RECT &bounds)
{
    RECT rc = ScreenRect(m_TraySurface, bounds);
    TrayCalendar_Toggle(m_hwndSink, &rc);
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
        self->m_DockSurface.SetTokens(&self->m_Tokens);
        self->m_TraySurface.SetTokens(&self->m_Tokens);
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

    m_DockRoot = new CIsland((float)LIBER_DOCK_HEIGHT / 2.0f);
    Thickness pad = { LIBER_DOCK_PAD, LIBER_DOCK_PAD, LIBER_DOCK_PAD, LIBER_DOCK_PAD };
    m_DockRoot->SetPadding(pad);
    m_TrayRoot = new CIsland((float)LIBER_TRAY_HEIGHT / 2.0f);
    m_Clock = new CTrayClock(this);
    m_TrayRoot->AddChild(m_Clock);
    m_Clock->Update();

    m_DockSurface.SetDpi(m_Dpi);
    m_TraySurface.SetDpi(m_Dpi);
    m_DockSurface.SetTokens(&m_Tokens);
    m_TraySurface.SetTokens(&m_Tokens);
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
    m_DockRoot = m_TrayRoot = NULL;
    m_Clock = NULL;
    ZeroMemory(m_Cells, sizeof(m_Cells));
    ClearEntries();
    if (g_pLiberDock == this)
        g_pLiberDock = NULL;
    if (m_Tray && m_Tray->GetHWND())
        ShowWindow(m_Tray->GetHWND(), SW_SHOW);
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

VOID LiberDock_Stop(VOID)
{
    delete g_pLiberDock;
}
