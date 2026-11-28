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
#define LIBER_TIMER_BACKDROP 3
#define LIBER_TIMER_PREVIEW  4
#define LIBER_TIMER_UNHOVER  5
#define LIBER_UNHOVER_DELAY  300
#define LIBER_BACKDROP_DELAY 100
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
    UINT cWindows;
    WCHAR szTitle[128];
};

class CLiberDock;

class CDockCell : public Widget, public IAnimationClient
{
public:
    CDockCell(CLiberDock *dock, int index, const LIBER_DOCK_METRICS *metrics) : m_Dock(dock), m_Index(index),
        m_Icon(NULL), m_Running(FALSE), m_Active(FALSE), m_Windows(0), m_Metrics(metrics)
    {
        ATLASSERT(metrics);
        SetSize(metrics->Cell, metrics->Cell);
        SetHitTestable(TRUE);
        SetFocusable(TRUE);
    }

    ~CDockCell()
    {
        AnimationClock::Instance().Remove(this);
    }

    void Hop()
    {
        if (!AnimationsEnabled())
            return;
        m_Hop.Start(0.0, 1.0, LIBER_DUR_SPRING, Easing::Out, AnimationClock::Now());
        AnimationClock::Instance().Add(this);
    }

    BOOL OnAnimationFrame(ULONGLONG now);
    BOOL OnContextMenu(POINT screen);

    void Set(HICON icon, BOOL running, BOOL active, UINT windows = 0, LPCWSTR title = NULL)
    {
        m_Icon = icon;
        m_Running = running;
        m_Active = active;
        m_Windows = windows;
        if (title)
            SetTooltip(running ? L"" : title);
        if (Parent())
            Parent()->Invalidate();
        else
            Invalidate();
    }

    UINT Windows() const { return m_Windows; }
    void OnPointerEnter();
    void OnPointerLeave();

    void OnClick();

protected:
    void PaintOverride(PaintContext &context)
    {
        RectF rc = ToRectF(Bounds());
        const ThemeTokens &tokens = context.Tokens;

        BOOL capsule = m_Metrics->Style == LIBER_TASKBAR_STYLE_CAPSULE;
        if (capsule && m_Windows >= 2 && (m_Active || HasState(StateHover)))
        {
            RectF behind = { rc.X + LIBER_DOCK_STACK_OFFSET, rc.Y, rc.Width, rc.Height };
            context.Painter.FillEllipse(behind, tokens.Color(m_Active ? LIBER_COLOR_FILL_PLATE_HOVER
                                                                      : LIBER_COLOR_FILL_SUBTLE_HOVER));
        }
        if (capsule && m_Active)
            context.Painter.FillEllipse(rc, tokens.Color(LIBER_COLOR_FILL_PLATE));
        else if (capsule && (HasState(StateHover) || HasState(StatePressed)))
            context.Painter.FillEllipse(rc, tokens.Color(LIBER_COLOR_FILL_PLATE_HOVER));
        else if (!capsule && (HasState(StateHover) || HasState(StatePressed)))
            context.Painter.FillEllipse(rc, tokens.Color(HasState(StatePressed) ? LIBER_COLOR_FILL_SUBTLE_PRESSED
                                                                                : LIBER_COLOR_FILL_SUBTLE_HOVER));

        float scale = HasState(StatePressed) ? 0.84f : 1.0f;
        float lift = (float)LiberDock_HopOffset(m_Hop.Value(context.Now));
        if (m_Index == LIBER_CELL_START)
            PaintLogo(context, rc, (capsule ? 30.0f : (float)m_Metrics->Icon * 0.84f) * scale);
        else
        {
            float size = (m_Index == LIBER_CELL_SEARCH ? (capsule ? 24.0f : 20.0f) : (float)m_Metrics->Icon) * scale;
            RectF icon = { rc.X + (rc.Width - size) / 2, rc.Y + (rc.Height - size) / 2 - lift, size, size };
            if (m_Index == LIBER_CELL_SEARCH)
                context.Painter.Icon(L"search_24_regular", icon, tokens.Color(LIBER_COLOR_TEXT1));
            else if (m_Icon)
                context.Painter.Image(m_Icon, icon);
        }

        if (capsule && m_Running)
        {
            float dot = (float)LIBER_DOCK_RUNNING_DOT;
            RectF rcDot = { rc.X + (rc.Width - dot) / 2, rc.Y + rc.Height - dot - 2.0f, dot, dot };
            context.Painter.FillEllipse(rcDot, tokens.Color(LIBER_COLOR_TEXT2));
        }
        else if (!capsule && (m_Running || m_Active))
        {
            float width = m_Active ? (float)m_Metrics->Icon : (float)LIBER_TONGUE_RUNNING;
            float edge = rc.Y + rc.Height + (float)m_Metrics->PadY;
            RectF tongue = { rc.X + (rc.Width - width) / 2, edge - LIBER_TONGUE_HEIGHT, width,
                             LIBER_TONGUE_HEIGHT * 2.0f };
            context.Painter.FillRoundRect(tongue, LIBER_TONGUE_HEIGHT / 2.0f,
                                          tokens.Color(m_Active ? LIBER_COLOR_ACCENT : LIBER_COLOR_TEXT2));
        }
    }

    static void PaintLogo(PaintContext &context, const RectF &rc, float size)
    {
        static const float Bars[4][4] = { { 0.0f, 5.8333f, 5.8333f, 26.25f },
                                          { 0.0f, 20.4167f, 20.4167f, 26.25f },
                                          { 7.5833f, 1.75f, 28.0f, 7.5833f },
                                          { 22.1667f, 1.75f, 28.0f, 22.1667f } };
        float x = rc.X + (rc.Width - size) / 2, y = rc.Y + (rc.Height - size) / 2, unit = size / 28.0f;
        PointF from = { x, y + 1.75f * unit }, to = { x + 28.0f * unit, y + 26.25f * unit };
        Argb fromColor = context.Tokens.Color(LIBER_COLOR_LOGO_FROM), toColor = context.Tokens.Color(LIBER_COLOR_LOGO_TO);

        for (UINT i = 0; i < _countof(Bars); i++)
        {
            RectF bar = { x + Bars[i][0] * unit, y + Bars[i][1] * unit, (Bars[i][2] - Bars[i][0]) * unit,
                          (Bars[i][3] - Bars[i][1]) * unit };
            context.Painter.FillGradient(bar, 0.0f, from, to, fromColor, toColor);
        }
    }

private:
    Animation m_Hop;
    CLiberDock *m_Dock;
    int m_Index;
    HICON m_Icon;
    BOOL m_Running;
    BOOL m_Active;
    UINT m_Windows;
    const LIBER_DOCK_METRICS *m_Metrics;
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

class CTrayLanguage : public CTrayButton
{
public:
    CTrayLanguage(CLiberDock *dock) : CTrayButton(dock, LIBER_LANGUAGE_WIDTH)
    {
        m_szLabel[0] = 0;
    }

    BOOL Update()
    {
        WCHAR szLabel[8] = L"";
        HWND foreground = GetForegroundWindow();
        HKL layout = GetKeyboardLayout(foreground ? GetWindowThreadProcessId(foreground, NULL) : 0);

        LiberDock_LanguageLabel(LOWORD((ULONG_PTR)layout), szLabel, _countof(szLabel));
        if (!wcscmp(szLabel, m_szLabel))
            return FALSE;
        StringCchCopyW(m_szLabel, _countof(m_szLabel), szLabel);
        Invalidate();
        return TRUE;
    }

    void OnClick()
    {
        HWND foreground = GetForegroundWindow();
        if (foreground)
            PostMessageW(foreground, WM_INPUTLANGCHANGEREQUEST, 0, (LPARAM)HKL_NEXT);
    }

protected:
    void PaintContent(PaintContext &context, const RectF &rc)
    {
        ShadowedText(context, m_szLabel, rc, TextCenter | TextMiddle);
    }

private:
    WCHAR m_szLabel[8];
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
        WCHAR szTime[32] = L"", szDate[32] = L"";

        GetLocalTime(&st);
        if (!GetTimeFormatW(LOCALE_USER_DEFAULT, TIME_NOSECONDS, &st, NULL, szTime, _countof(szTime)))
            szTime[0] = 0;
        if (!GetDateFormatW(LOCALE_USER_DEFAULT, DATE_SHORTDATE, &st, NULL, szDate, _countof(szDate)))
            szDate[0] = 0;
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
    CLiberDock() : m_Tray(NULL), m_hwndSink(NULL), m_hMoveHook(NULL), m_hMinimizeHook(NULL), m_BackdropPending(FALSE), m_HoverCell(-1), m_uShellHook(0), m_Count(0), m_DockRoot(NULL), m_TrayRoot(NULL),
        m_Pill(NULL), m_Chevron(NULL), m_Language(NULL), m_TrayWidth(0), m_Clock(NULL), m_Dpi(96),
        m_RestoreTray(TRUE)
    {
        ZeroMemory(m_Entries, sizeof(m_Entries));
        ZeroMemory(m_Cells, sizeof(m_Cells));
        ZeroMemory(&m_Layout, sizeof(m_Layout));
        m_PlateRoot = NULL;
        LiberDock_Metrics(LIBER_TASKBAR_STYLE_CAPSULE, FALSE, FALSE, &m_Metrics);
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
    void RenderDock(ULONGLONG now) { m_DockSurface.Render(now); }
    BOOL CellMenu(int index, POINT screen);
    BOOL MinimizeRect(HWND hwnd, RECT *prc);
    UINT Dpi() const { return m_Dpi; }
    const LIBER_DOCK_METRICS &Metrics() const { return m_Metrics; }
    void OnChevronClick();
    void CellHover(int index, BOOL inside);

private:
    static LRESULT CALLBACK SinkProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    static VOID CALLBACK WindowMoved(HWINEVENTHOOK hook, DWORD event, HWND hwnd, LONG idObject, LONG idChild,
                                     DWORD thread, DWORD time);
    static BOOL CALLBACK PinProc(PCWSTR pszShortcut, PCWSTR pszTarget, PCWSTR pszAppId, const FILETIME *pCreationTime,
                                 LPARAM lParam);
    static BOOL CALLBACK WindowProc(HWND hwnd, LPARAM lParam);

    void BuildTheme();
    void ClearEntries();
    void Refresh();
    void Place();
    void PlaceBands(const RECT *prcMonitor);
    void PlacePlate(const RECT *prcMonitor, BOOL show);
    BOOL WindowTouches(const RECT *prcMonitor);
    BOOL UpdateMetrics(const RECT *prcMonitor);
    static BOOL IsForeground(const LIBER_DOCK_ENTRY &entry, HWND foreground);
    void ShowPreview();
    UINT AppWindows(UINT index, HWND *windows, UINT max);
    RECT ScreenRect(Surface &surface, const RECT &dip);

    ITrayWindow *m_Tray;
    HWND m_hwndSink;
    HWINEVENTHOOK m_hMoveHook;
    HWINEVENTHOOK m_hMinimizeHook;
    BOOL m_BackdropPending;
    int m_HoverCell;
    UINT m_uShellHook;
    ThemeTokens m_Tokens;
    Surface m_DockSurface;
    Surface m_TraySurface;
    Surface m_Bands[LIBER_BLUR_BAND_LAYERS];
    Surface m_PlateSurface;
    CLiberPanel *m_PlateRoot;
    LIBER_DOCK_METRICS m_Metrics;
    LIBER_DOCK_ENTRY m_Entries[LIBER_DOCK_MAX_ITEMS];
    UINT m_Count;
    CLiberPanel *m_DockRoot;
    StackPanel *m_TrayRoot;
    CSystemPill *m_Pill;
    CTrayChevron *m_Chevron;
    CTrayLanguage *m_Language;
    ThemeTokens m_TrayTokens;
    int m_TrayWidth;
    CDockCell *m_Cells[LIBER_DOCK_MAX_ITEMS + LIBER_DOCK_FIXED_CELLS];
    CTrayClock *m_Clock;
    UINT m_Dpi;
    LIBER_DOCK_LAYOUT m_Layout;

public:
    BOOL m_RestoreTray;
};

static CLiberDock *g_pLiberDock;

void CDockCell::OnPointerEnter()
{
    m_Dock->CellHover(m_Index, TRUE);
}

void CDockCell::OnPointerLeave()
{
    m_Dock->CellHover(m_Index, FALSE);
}

void CDockCell::OnClick()
{
    m_Dock->OnCellClick(m_Index, Bounds());
}

BOOL CDockCell::OnContextMenu(POINT screen)
{
    return m_Dock->CellMenu(m_Index, screen);
}

BOOL CDockCell::OnAnimationFrame(ULONGLONG now)
{
    BOOL running = m_Hop.IsRunning(now);

    Invalidate();
    m_Dock->RenderDock(now);
    return running;
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
    LiberShell_BuildWallpaperInkTokens(&m_TrayTokens, IsRectEmpty(&m_Layout.Tray) ? NULL : &m_Layout.Tray);
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
    LiberDock_AppName(entry.szSource, entry.szTitle, _countof(entry.szTitle));
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
            self->m_Entries[i].cWindows = 1;
            GetWindowTextW(hwnd, self->m_Entries[i].szTitle, _countof(self->m_Entries[i].szTitle));
            return TRUE;
        }
        if (self->m_Entries[i].hwnd != NULL && LiberDock_SameApp(self->m_Entries[i].szTarget, szImage))
        {
            self->m_Entries[i].cWindows++;
            return TRUE;
        }
    }
    if (self->m_Count >= LIBER_DOCK_MAX_ITEMS)
        return FALSE;

    LIBER_DOCK_ENTRY &entry = self->m_Entries[self->m_Count++];
    ZeroMemory(&entry, sizeof(entry));
    StringCchCopyW(entry.szTarget, _countof(entry.szTarget), szImage);
    entry.hwnd = hwnd;
    entry.cWindows = 1;
    GetWindowTextW(hwnd, entry.szTitle, _countof(entry.szTitle));
    DWORD_PTR result = 0;
    if (SendMessageTimeoutW(hwnd, WM_GETICON, ICON_BIG, 0, SMTO_ABORTIFHUNG, 100, &result) && result)
        entry.hIcon = (HICON)result;
    if (!entry.hIcon)
        entry.hIcon = (HICON)GetClassLongPtrW(hwnd, GCLP_HICON);
    if (!entry.hIcon)
        entry.hIcon = LoadIconW(NULL, (LPCWSTR)IDI_APPLICATION);
    return TRUE;
}

static HRESULT LaunchSource(LPCWSTR pszSource)
{
    HANDLE process = NULL;
    HRESULT hr;

    if (!pszSource || !*pszSource)
        return E_INVALIDARG;
    AllowSetForegroundWindow(ASFW_ANY);
    hr = TaskbarPin_Launch(pszSource, &process);
    if (FAILED(hr) && hr != HRESULT_FROM_WIN32(ERROR_CANCELLED))
    {
        SHELLEXECUTEINFOW execute = { sizeof(execute) };

        ERR("Dock launch failed for %S: %08lx\n", pszSource, hr);
        execute.fMask = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_NOASYNC | SEE_MASK_FLAG_NO_UI;
        execute.lpFile = pszSource;
        execute.nShow = SW_SHOWNORMAL;
        if (ShellExecuteExW(&execute))
        {
            hr = S_OK;
            process = execute.hProcess;
        }
        else
        {
            DWORD error = GetLastError();
            hr = error == ERROR_CANCELLED ? HRESULT_FROM_WIN32(error) : hr;
        }
    }
    if (process)
        CloseHandle(process);
    return hr;
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

typedef struct _LIBER_TOUCH
{
    LONG Edge;
    RECT Monitor;
    BOOL Touches;
} LIBER_TOUCH;

static BOOL CALLBACK FindTouchingWindow(HWND hwnd, LPARAM lParam)
{
    LIBER_TOUCH *touch = (LIBER_TOUCH *)lParam;
    RECT rc;

    if (!LiberDock_IsTaskWindow(hwnd) || IsIconic(hwnd) || !GetWindowRect(hwnd, &rc))
        return TRUE;
    if (rc.bottom >= touch->Edge && rc.right > touch->Monitor.left && rc.left < touch->Monitor.right &&
        rc.top < touch->Monitor.bottom)
    {
        touch->Touches = TRUE;
        return FALSE;
    }
    return TRUE;
}

BOOL CLiberDock::WindowTouches(const RECT *prcMonitor)
{
    LIBER_TOUCH touch = { prcMonitor->bottom - ScaleToDpi(LIBER_TASKBAR_DOCK, m_Dpi), *prcMonitor, FALSE };

    EnumWindows(FindTouchingWindow, (LPARAM)&touch);
    return touch.Touches;
}

BOOL CLiberDock::UpdateMetrics(const RECT *prcMonitor)
{
    LIBER_DOCK_METRICS metrics;
    UINT style = LiberExperience_TaskbarStyle();

    LiberDock_Metrics(style, LiberExperience_TaskbarLeft(),
                      style != LIBER_TASKBAR_STYLE_CLASSIC && WindowTouches(prcMonitor), &metrics);
    if (!memcmp(&metrics, &m_Metrics, sizeof(metrics)))
        return FALSE;
    m_Metrics = metrics;
    return TRUE;
}

void CLiberDock::PlaceBands(const RECT *prcMonitor)
{
    BOOL glass = LiberShell_GlassEnabled() && m_Metrics.Style != LIBER_TASKBAR_STYLE_CLASSIC && !m_Metrics.Touched;

    for (UINT i = 0; i < LIBER_BLUR_BAND_LAYERS; i++)
    {
        RECT band;
        if (!m_Bands[i].Window())
            continue;
        LiberDock_BlurBand(prcMonitor, m_Dpi, &m_Metrics, i, &band);
        m_Bands[i].SetPosition(band.left, band.top, MulDiv(band.right - band.left, 96, (int)m_Dpi),
                               MulDiv(band.bottom - band.top, 96, (int)m_Dpi));
        m_Bands[i].SetMaterial(glass ? (int)i : -1, 0, 0, 0.0f);
        m_Bands[i].Render(AnimationClock::Now());
        ShowWindow(m_Bands[i].Window(), glass ? SW_SHOWNOACTIVATE : SW_HIDE);
    }
}

void CLiberDock::PlacePlate(const RECT *prcMonitor, BOOL show)
{
    if (!m_PlateSurface.Window())
        return;
    if (!show)
    {
        ShowWindow(m_PlateSurface.Window(), SW_HIDE);
        return;
    }
    int height = ScaleToDpi(m_Metrics.Reserve, m_Dpi);
    m_PlateSurface.SetTokens(&m_Tokens);
    m_PlateSurface.SetPosition(prcMonitor->left, prcMonitor->bottom - height,
                               MulDiv(prcMonitor->right - prcMonitor->left, 96, (int)m_Dpi), m_Metrics.Reserve);
    LiberShell_ApplyMaterial(&m_PlateSurface, &m_Tokens, LIBER_MATERIAL_REGULAR, 0.0f);
    m_PlateSurface.RequestLayout();
    m_PlateSurface.Render(AnimationClock::Now());
    ShowWindow(m_PlateSurface.Window(), SW_SHOWNOACTIVATE);
    SetWindowPos(m_DockSurface.Window(), HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    SetWindowPos(m_TraySurface.Window(), HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
}

void CLiberDock::Place()
{
    HMONITOR monitor = MonitorFromPoint(POINT(), MONITOR_DEFAULTTOPRIMARY);
    MONITORINFO mi = { sizeof(mi) };
    UINT cells = m_Count + LIBER_DOCK_FIXED_CELLS;

    if (!GetMonitorInfoW(monitor, &mi))
        SetRect(&mi.rcMonitor, 0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN));
    if (UpdateMetrics(&mi.rcMonitor))
        LiberPreview_Hide();
    LiberDock_Layout(&mi.rcMonitor, m_Dpi, cells, m_TrayWidth, &m_Metrics, &m_Layout);
    BOOL floating = m_Metrics.Style != LIBER_TASKBAR_STYLE_CLASSIC;
    BOOL island = floating && !m_Metrics.Touched;
    Thickness pad = { m_Layout.CellsLeft, m_Metrics.PadY, m_Metrics.PadX, m_Metrics.PadY };
    m_DockRoot->SetPadding(pad);
    m_DockRoot->SetShape((float)m_Metrics.Radius, floating && m_Metrics.Touched);
    m_DockSurface.SetInputShape(island ? (float)m_Metrics.Radius : 0.0f);
    m_DockSurface.SetElevation(island ? LIBER_ELEVATION_E16 : -1, (float)m_Metrics.Radius);
    m_DockSurface.SetPosition(m_Layout.Dock.left, m_Layout.Dock.top, m_Layout.DockWidthDip, m_Metrics.Height);
    m_DockSurface.RequestLayout();
    m_TraySurface.SetPosition(m_Layout.Tray.left, m_Layout.Tray.top, m_TrayWidth, LIBER_TRAY_HEIGHT);
    LiberShell_ApplyMaterial(&m_DockSurface, &m_Tokens, LIBER_MATERIAL_REGULAR, (float)m_Metrics.Radius);
    if (floating && m_Metrics.Touched)
        m_DockSurface.SetMaterial(-1, 0, 0, 0.0f);
    LiberShell_SampleBackdrop(&m_DockSurface, &m_Tokens, FALSE);
    PlacePlate(&mi.rcMonitor, floating && m_Metrics.Touched);
    PlaceBands(&mi.rcMonitor);
    LiberShell_SetTaskbarReserve(m_Metrics.Reserve);

    RECT work;
    LiberDock_WorkArea(&mi.rcMonitor, m_Dpi, &m_Metrics, &work);
    if (!EqualRect(&work, &mi.rcWork))
        SystemParametersInfoW(SPI_SETWORKAREA, 0, &work, SPIF_SENDCHANGE);
}

BOOL CLiberDock::IsForeground(const LIBER_DOCK_ENTRY &entry, HWND foreground)
{
    DWORD entryPid = 0, foregroundPid = 0;

    if (!entry.hwnd || !foreground)
        return FALSE;
    if (entry.hwnd == foreground)
        return TRUE;
    return entry.cWindows > 1 && GetWindowThreadProcessId(entry.hwnd, &entryPid) &&
           GetWindowThreadProcessId(foreground, &foregroundPid) && entryPid == foregroundPid;
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

    LIBER_DOCK_METRICS built = m_Metrics;
    HMONITOR monitor = MonitorFromPoint(POINT(), MONITOR_DEFAULTTOPRIMARY);
    MONITORINFO mi = { sizeof(mi) };
    if (GetMonitorInfoW(monitor, &mi))
        UpdateMetrics(&mi.rcMonitor);
    same = m_Cells[0] != NULL && m_Count == previousCount && built.Cell == m_Metrics.Cell;
    for (UINT i = 0; same && i < m_Count; i++)
        same = previous[i].hwnd == m_Entries[i].hwnd && previous[i].bPinned == m_Entries[i].bPinned &&
               !lstrcmpiW(previous[i].szTarget, m_Entries[i].szTarget) &&
               !lstrcmpiW(previous[i].szSource, m_Entries[i].szSource);

    if (same)
    {
        for (UINT i = 0; i < m_Count; i++)
        {
            if (!previous[i].bOwnsIcon && !m_Entries[i].bOwnsIcon)
                previous[i].hIcon = m_Entries[i].hIcon;
            previous[i].cWindows = m_Entries[i].cWindows;
            StringCchCopyW(previous[i].szTitle, _countof(previous[i].szTitle), m_Entries[i].szTitle);
        }
        ClearEntries();
        CopyMemory(m_Entries, previous, sizeof(m_Entries));
        m_Count = previousCount;
        LocalFree(previous);
        m_Cells[0]->Set(NULL, FALSE, LiberStart_IsOpen());
        for (UINT i = 0; i < m_Count; i++)
        {
            m_Cells[i + LIBER_DOCK_FIXED_CELLS]->Set(m_Entries[i].hIcon, m_Entries[i].hwnd != NULL,
                                                     IsForeground(m_Entries[i], foreground) && !LiberStart_IsOpen(),
                                                     m_Entries[i].cWindows, m_Entries[i].szTitle);
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

    m_Cells[0] = new CDockCell(this, LIBER_CELL_START, &m_Metrics);
    m_Cells[0]->Set(NULL, FALSE, LiberStart_IsOpen(), 0, L"Start");
    m_DockRoot->AddChild(m_Cells[0]);
    m_Cells[1] = new CDockCell(this, LIBER_CELL_SEARCH, &m_Metrics);
    m_Cells[1]->Set(NULL, FALSE, FALSE, 0, L"Search");
    m_DockRoot->AddChild(m_Cells[1]);
    for (UINT i = 0; i < m_Count; i++)
    {
        CDockCell *cell = new CDockCell(this, (int)i, &m_Metrics);
        cell->Set(m_Entries[i].hIcon, m_Entries[i].hwnd != NULL,
                  IsForeground(m_Entries[i], foreground) && !LiberStart_IsOpen(), m_Entries[i].cWindows,
                  m_Entries[i].szTitle);
        m_Cells[i + LIBER_DOCK_FIXED_CELLS] = cell;
        m_DockRoot->AddChild(cell);
    }

    Place();
    m_DockSurface.Render(AnimationClock::Now());
    m_TraySurface.Render(AnimationClock::Now());
}

typedef struct _LIBER_APP_WINDOWS
{
    DWORD Pid;
    HWND *Windows;
    UINT Count;
    UINT Max;
} LIBER_APP_WINDOWS;

static BOOL CALLBACK CollectAppWindow(HWND hwnd, LPARAM lParam)
{
    LIBER_APP_WINDOWS *app = (LIBER_APP_WINDOWS *)lParam;
    DWORD pid = 0;

    if (app->Count >= app->Max)
        return FALSE;
    if (LiberDock_IsTaskWindow(hwnd) && GetWindowThreadProcessId(hwnd, &pid) && pid == app->Pid)
        app->Windows[app->Count++] = hwnd;
    return TRUE;
}

UINT CLiberDock::AppWindows(UINT index, HWND *windows, UINT max)
{
    LIBER_APP_WINDOWS app = { 0, windows, 0, max };

    if (index >= m_Count || !m_Entries[index].hwnd || !IsWindow(m_Entries[index].hwnd) ||
        !GetWindowThreadProcessId(m_Entries[index].hwnd, &app.Pid))
    {
        return 0;
    }
    EnumWindows(CollectAppWindow, (LPARAM)&app);
    return app.Count;
}

void CLiberDock::CellHover(int index, BOOL inside)
{
    if (!inside)
    {
        if (index == m_HoverCell)
            SetTimer(m_hwndSink, LIBER_TIMER_UNHOVER, LIBER_UNHOVER_DELAY, NULL);
        return;
    }
    KillTimer(m_hwndSink, LIBER_TIMER_UNHOVER);
    m_HoverCell = index;
    if (index < 0 || (UINT)index >= m_Count || !m_Entries[index].hwnd)
    {
        KillTimer(m_hwndSink, LIBER_TIMER_PREVIEW);
        LiberPreview_Hide();
        return;
    }
    if (LiberPreview_IsOpen())
        ShowPreview();
    else
        SetTimer(m_hwndSink, LIBER_TIMER_PREVIEW, LIBER_PREVIEW_DELAY, NULL);
}

void CLiberDock::ShowPreview()
{
    HWND windows[LIBER_PREVIEW_MAX];
    int index = m_HoverCell;

    KillTimer(m_hwndSink, LIBER_TIMER_PREVIEW);
    if (index < 0 || (UINT)index >= m_Count || !m_Cells[index + LIBER_DOCK_FIXED_CELLS] ||
        !m_Cells[index + LIBER_DOCK_FIXED_CELLS]->HasState(StateHover))
    {
        return;
    }
    UINT count = AppWindows((UINT)index, windows, _countof(windows));
    RECT anchor = ScreenRect(m_DockSurface, m_Cells[index + LIBER_DOCK_FIXED_CELLS]->Bounds());
    if (!count || !LiberPreview_Show(windows, count, m_Entries[index].hIcon, &anchor))
        LiberPreview_Hide();
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
        if ((UINT)index + LIBER_DOCK_FIXED_CELLS < _countof(m_Cells) && m_Cells[index + LIBER_DOCK_FIXED_CELLS])
            m_Cells[index + LIBER_DOCK_FIXED_CELLS]->Hop();
        HRESULT hr = LaunchSource(entry.szSource[0] ? entry.szSource : entry.szTarget);
        if (FAILED(hr) && hr != HRESULT_FROM_WIN32(ERROR_CANCELLED))
            MessageBeep(MB_ICONERROR);
    }
}

BOOL CLiberDock::MinimizeRect(HWND hwnd, RECT *prc)
{
    DWORD pid = 0;
    RECT dock;
    UINT index = m_Count;

    if (!hwnd || !prc || !m_DockSurface.Window() || !GetWindowRect(m_DockSurface.Window(), &dock))
        return FALSE;
    for (UINT i = 0; i < m_Count && index == m_Count; i++)
    {
        if (m_Entries[i].hwnd == hwnd)
            index = i;
    }
    if (index == m_Count && GetWindowThreadProcessId(hwnd, &pid) && pid)
    {
        for (UINT i = 0; i < m_Count && index == m_Count; i++)
        {
            DWORD entryPid = 0;
            if (m_Entries[i].hwnd && GetWindowThreadProcessId(m_Entries[i].hwnd, &entryPid) && entryPid == pid)
                index = i;
        }
    }
    if (index == m_Count)
        return FALSE;
    LiberDock_CellRect(&dock, m_Dpi, &m_Metrics, m_Layout.CellsLeft, index + LIBER_DOCK_FIXED_CELLS, prc);
    CDockCell *cell = m_Cells[index + LIBER_DOCK_FIXED_CELLS];
    RECT laidOut = cell && !IsRectEmpty(&cell->Bounds()) ? ScreenRect(m_DockSurface, cell->Bounds()) : *prc;
    ATLASSERT(EqualRect(prc, &laidOut));
    UNREFERENCED_PARAMETER(laidOut);
    return TRUE;
}

BOOL CLiberDock::CellMenu(int index, POINT screen)
{
    enum { DOCK_MENU_OPEN = 1, DOCK_MENU_PIN, DOCK_MENU_UNPIN, DOCK_MENU_CLOSE };

    if (index < 0 || (UINT)index >= m_Count)
        return FALSE;
    LIBER_DOCK_ENTRY entry = m_Entries[index];
    BOOL canPin = !TaskbarPin_IsDisabled() && entry.szTarget[0] && TaskbarPin_IsPinnable(entry.szTarget);
    LIBER_MENU_ITEM menu[] = {
        { DOCK_MENU_OPEN, L"Open new window", L"open_16_regular", NULL, FALSE, !entry.szSource[0] && !entry.szTarget[0] },
        { 0, NULL, NULL, NULL, TRUE, FALSE },
        { entry.bPinned ? (UINT)DOCK_MENU_UNPIN : (UINT)DOCK_MENU_PIN,
          entry.bPinned ? L"Unpin from taskbar" : L"Pin to taskbar", L"pin_16_regular", NULL, FALSE,
          !entry.bPinned && !canPin },
        { DOCK_MENU_CLOSE, L"Close window", L"dismiss_16_regular", NULL, FALSE, entry.hwnd == NULL },
    };

    switch (LiberMenu_Track(menu, _countof(menu), screen))
    {
        case DOCK_MENU_OPEN:
            if (FAILED(LaunchSource(entry.szSource[0] ? entry.szSource : entry.szTarget)))
                MessageBeep(MB_ICONERROR);
            break;
        case DOCK_MENU_PIN:
            TaskbarPin_Create(entry.szTarget);
            SetTimer(m_hwndSink, LIBER_TIMER_REFRESH, LIBER_REFRESH_DELAY, NULL);
            break;
        case DOCK_MENU_UNPIN:
            TaskbarPin_Remove(entry.szTarget);
            SetTimer(m_hwndSink, LIBER_TIMER_REFRESH, LIBER_REFRESH_DELAY, NULL);
            break;
        case DOCK_MENU_CLOSE:
            if (entry.hwnd && IsWindow(entry.hwnd))
                PostMessageW(entry.hwnd, WM_SYSCOMMAND, SC_CLOSE, 0);
            break;
    }
    return TRUE;
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

    rc = ScreenRect(m_TraySurface, m_Chevron->Bounds());
    LiberOverflow_Toggle(&rc);
}

void CLiberDock::OnClockClick(const RECT &bounds)
{
    RECT rc = ScreenRect(m_TraySurface, bounds);
    LiberCalendar_Toggle(&rc);
}

VOID CALLBACK CLiberDock::WindowMoved(HWINEVENTHOOK hook, DWORD event, HWND hwnd, LONG idObject, LONG idChild,
                                      DWORD thread, DWORD time)
{
    CLiberDock *self = g_pLiberDock;

    UNREFERENCED_PARAMETER(hook);
    UNREFERENCED_PARAMETER(event);
    UNREFERENCED_PARAMETER(idChild);
    UNREFERENCED_PARAMETER(thread);
    UNREFERENCED_PARAMETER(time);
    if (!self || !self->m_hwndSink || idObject != OBJID_WINDOW || !hwnd || GetAncestor(hwnd, GA_ROOT) != hwnd)
        return;
    if (!self->m_BackdropPending)
        self->m_BackdropPending = SetTimer(self->m_hwndSink, LIBER_TIMER_BACKDROP, LIBER_BACKDROP_DELAY, NULL) != 0;
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
    if (msg == WM_KLUDGEMINRECT)
        return self && self->MinimizeRect((HWND)wParam, (RECT *)lParam);
    if (self && msg == WM_TIMER)
    {
        if (wParam == LIBER_TIMER_PREVIEW)
        {
            self->ShowPreview();
        }
        else if (wParam == LIBER_TIMER_UNHOVER)
        {
            POINT cursor;
            KillTimer(hwnd, LIBER_TIMER_UNHOVER);
            if (GetCursorPos(&cursor) && LiberPreview_Contains(cursor))
                SetTimer(hwnd, LIBER_TIMER_UNHOVER, LIBER_UNHOVER_DELAY, NULL);
            else if (self->m_HoverCell < 0 || (UINT)self->m_HoverCell >= self->m_Count ||
                     !self->m_Cells[self->m_HoverCell + LIBER_DOCK_FIXED_CELLS] ||
                     !self->m_Cells[self->m_HoverCell + LIBER_DOCK_FIXED_CELLS]->HasState(StateHover))
            {
                self->m_HoverCell = -1;
                KillTimer(hwnd, LIBER_TIMER_PREVIEW);
                LiberPreview_Hide();
            }
        }
        else if (wParam == LIBER_TIMER_BACKDROP)
        {
            KillTimer(hwnd, LIBER_TIMER_BACKDROP);
            self->m_BackdropPending = FALSE;
            HMONITOR monitor = MonitorFromPoint(POINT(), MONITOR_DEFAULTTOPRIMARY);
            MONITORINFO mi = { sizeof(mi) };
            if (GetMonitorInfoW(monitor, &mi) &&
                self->WindowTouches(&mi.rcMonitor) != self->m_Metrics.Touched &&
                self->m_Metrics.Style != LIBER_TASKBAR_STYLE_CLASSIC)
            {
                self->Place();
                self->RenderDock(AnimationClock::Now());
            }
            LiberShell_SampleBackdrop(&self->m_DockSurface, &self->m_Tokens, FALSE);
        }
        else if (wParam == LIBER_TIMER_REFRESH)
        {
            KillTimer(hwnd, LIBER_TIMER_REFRESH);
            self->Refresh();
        }
        else if (wParam == LIBER_TIMER_CLOCK && self->m_Clock &&
                 (self->m_Clock->Update() | (self->m_Language && self->m_Language->Update())))
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
        LiberShell_ApplyMaterial(&self->m_DockSurface, &self->m_Tokens, LIBER_MATERIAL_REGULAR,
                                 (float)self->m_Metrics.Radius);
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
    for (UINT i = 0; i < LIBER_BLUR_BAND_LAYERS; i++)
    {
        if (m_Bands[i].Create(NULL, L"Liber Scrim", exStyle | WS_EX_LAYERED | WS_EX_TRANSPARENT))
        {
            m_Bands[i].SetDpi(m_Dpi);
            m_Bands[i].SetMaterialFade(TRUE);
        }
    }
    if (!m_DockSurface.Create(NULL, L"Liber Dock", exStyle) || !m_TraySurface.Create(NULL, L"Liber Tray", exStyle))
        return E_FAIL;
    m_TraySurface.SetInputShape((float)LIBER_TRAY_HEIGHT / 2.0f);
    if (m_PlateSurface.Create(NULL, L"Liber Taskbar Plate", exStyle))
    {
        m_PlateSurface.SetDpi(m_Dpi);
        m_PlateRoot = new CLiberPanel(Orientation::Horizontal, 0, 0.0f, LIBER_MATERIAL_REGULAR);
        m_PlateSurface.SetRoot(m_PlateRoot);
    }

    m_DockRoot = new CLiberPanel(Orientation::Horizontal, LIBER_DOCK_CELL_GAP, (float)m_Metrics.Radius,
                                 LIBER_MATERIAL_REGULAR);
    Thickness pad = { m_Metrics.PadX, m_Metrics.PadY, m_Metrics.PadX, m_Metrics.PadY };
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
    m_TrayWidth = 2 * LIBER_TRAY_PAD_X + LIBER_CHEVRON_WIDTH + LiberDock_SystemPillWidth(battery) + LIBER_CLOCK_WIDTH +
                  2 * LIBER_TRAY_GAP;
    if (GetKeyboardLayoutList(0, NULL) > 1)
    {
        m_Language = new CTrayLanguage(this);
        m_Language->Update();
        m_TrayRoot->AddChild(m_Language);
        m_TrayWidth += LIBER_LANGUAGE_WIDTH + LIBER_TRAY_GAP;
    }
    m_TrayRoot->AddChild(m_Pill);
    m_TrayRoot->AddChild(m_Clock);
    m_Clock->Update();

    LiberShell_ApplyFont(&m_DockSurface);
    LiberShell_ApplyFont(&m_TraySurface);
    m_DockSurface.SetDpi(m_Dpi);
    m_TraySurface.SetDpi(m_Dpi);
    m_DockSurface.SetTokens(&m_Tokens);
    LiberShell_ApplyMaterial(&m_DockSurface, &m_Tokens, LIBER_MATERIAL_REGULAR, (float)m_Metrics.Radius);
    m_TraySurface.SetTokens(&m_TrayTokens);
    m_DockSurface.SetRoot(m_DockRoot);
    m_TraySurface.SetRoot(m_TrayRoot);

    g_pLiberDock = this;
    Refresh();
    BuildTheme();
    m_TraySurface.SetTokens(&m_TrayTokens);
    m_TraySurface.Render(AnimationClock::Now());
    ShowWindow(m_DockSurface.Window(), SW_SHOWNOACTIVATE);
    ShowWindow(m_TraySurface.Window(), SW_SHOWNOACTIVATE);

    m_uShellHook = RegisterWindowMessageW(L"SHELLHOOK");
    RegisterShellHookWindow(m_hwndSink);
    m_hMoveHook = SetWinEventHook(EVENT_OBJECT_LOCATIONCHANGE, EVENT_OBJECT_LOCATIONCHANGE, NULL, WindowMoved, 0, 0,
                                  WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS);
    m_hMinimizeHook = SetWinEventHook(EVENT_SYSTEM_MINIMIZESTART, EVENT_SYSTEM_MINIMIZEEND, NULL, WindowMoved, 0, 0,
                                      WINEVENT_OUTOFCONTEXT);
    SetTimer(m_hwndSink, LIBER_TIMER_CLOCK, 1000, NULL);

    if (m_Tray && m_Tray->GetHWND())
        ShowWindow(m_Tray->GetHWND(), SW_HIDE);
    return S_OK;
}

void CLiberDock::Stop()
{
    LiberControlCenter_Close();
    LiberCalendar_Destroy();
    LiberOverflow_Destroy();
    LiberStart_Destroy();
    LiberPreview_Destroy();
    if (m_hMoveHook)
        UnhookWinEvent(m_hMoveHook);
    if (m_hMinimizeHook)
        UnhookWinEvent(m_hMinimizeHook);
    m_hMoveHook = m_hMinimizeHook = NULL;
    if (m_hwndSink)
    {
        DeregisterShellHookWindow(m_hwndSink);
        KillTimer(m_hwndSink, LIBER_TIMER_CLOCK);
        KillTimer(m_hwndSink, LIBER_TIMER_REFRESH);
        KillTimer(m_hwndSink, LIBER_TIMER_BACKDROP);
        KillTimer(m_hwndSink, LIBER_TIMER_PREVIEW);
        KillTimer(m_hwndSink, LIBER_TIMER_UNHOVER);
        DestroyWindow(m_hwndSink);
        m_hwndSink = NULL;
    }
    m_DockSurface.SetRoot(NULL);
    m_TraySurface.SetRoot(NULL);
    m_DockSurface.Destroy();
    m_TraySurface.Destroy();
    for (UINT i = 0; i < LIBER_BLUR_BAND_LAYERS; i++)
        m_Bands[i].Destroy();
    m_PlateSurface.SetRoot(NULL);
    m_PlateSurface.Destroy();
    delete m_PlateRoot;
    m_PlateRoot = NULL;
    delete m_DockRoot;
    delete m_TrayRoot;
    m_DockRoot = NULL;
    m_TrayRoot = NULL;
    m_Pill = NULL;
    m_Chevron = NULL;
    m_Language = NULL;
    m_Clock = NULL;
    ZeroMemory(m_Cells, sizeof(m_Cells));
    ClearEntries();
    if (g_pLiberDock == this)
        g_pLiberDock = NULL;
    if (m_Tray && m_Tray->GetHWND() && m_RestoreTray)
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
    LiberDock_WorkArea(&mi.rcMonitor, g_pLiberDock->Dpi(), &g_pLiberDock->Metrics(), prcWork);
    return TRUE;
}

VOID LiberDock_Stop(VOID)
{
    delete g_pLiberDock;
}

VOID LiberDock_EndSession(VOID)
{
    LiberWidgets_Stop();
    if (g_pLiberDock)
        g_pLiberDock->m_RestoreTray = FALSE;
    LiberDock_Stop();
    ATLASSERT(g_pLiberDock == NULL);
}
