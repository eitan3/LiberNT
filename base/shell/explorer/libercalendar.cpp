/*
 * PROJECT:     LiberNT Explorer
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Liber clock flyout: notifications and calendar panels
 * COPYRIGHT:   Copyright 2026 Eitan Hadar <eitanhadar4@gmail.com>
 */

#include "precomp.h"

#include "libershell.h"

using namespace LiberUI;

#define LIBER_CAL_WIDTH     348
#define LIBER_CAL_GAP       12
#define LIBER_CAL_CELL      40
#define LIBER_CAL_COLUMN    45
#define LIBER_CAL_NOTIFS    3
#define LIBER_CAL_CARD_H    98

enum
{
    LCA_CLEAR = 1,
    LCA_PREV,
    LCA_NEXT,
    LCA_DISMISS = 10,
    LCA_DAY = 100
};

class CGlassPanel : public StackPanel
{
public:
    CGlassPanel(int gap) : StackPanel(Orientation::Vertical, gap) {}

protected:
    void PaintOverride(PaintContext &context)
    {
        LiberShell_PaintGlass(context, ToRectF(Bounds()), (float)LIBER_RADIUS_FLYOUT, LIBER_MATERIAL_THICK);
    }
};

class CTextButton : public Control
{
public:
    CTextButton(UINT id, LPCWSTR text)
    {
        SetId(id);
        StringCchCopyW(m_Text, _countof(m_Text), text);
        SetSize(-1, 24);
    }

    void OnClick() { Raise(ControlClicked); }

protected:
    SIZE MeasureOverride(SIZE available)
    {
        SIZE size = { min(available.cx, (LONG)(16 + lstrlenW(m_Text) * 6)), 24 };
        return size;
    }

    void PaintOverride(PaintContext &context)
    {
        RectF rc = ToRectF(Bounds());
        const ThemeTokens &tokens = context.Tokens;

        if (HasState(StateHover) || HasState(StatePressed))
            context.Painter.FillRoundRect(rc, (float)LIBER_RADIUS_CONTROL,
                                          tokens.Color(HasState(StatePressed) ? LIBER_COLOR_FILL_SUBTLE_PRESSED
                                                                              : LIBER_COLOR_FILL_SUBTLE_HOVER));
        context.Painter.Text(m_Text, rc, LIBER_TYPE_CAPTION, tokens.Color(LIBER_COLOR_TEXT1), TextCenter | TextMiddle);
    }

private:
    WCHAR m_Text[32];
};

class CRoundIconButton : public Control
{
public:
    CRoundIconButton(UINT id, LPCWSTR icon, int size, float glyph) : m_Glyph(glyph)
    {
        SetId(id);
        StringCchCopyW(m_Icon, _countof(m_Icon), icon);
        SetSize(size, size);
    }

    void OnClick() { Raise(ControlClicked); }

protected:
    void PaintOverride(PaintContext &context)
    {
        RectF rc = ToRectF(Bounds());
        const ThemeTokens &tokens = context.Tokens;

        if (HasState(StateHover) || HasState(StatePressed))
            context.Painter.FillEllipse(rc, tokens.Color(HasState(StatePressed) ? LIBER_COLOR_FILL_SUBTLE_PRESSED
                                                                                : LIBER_COLOR_FILL_SUBTLE_HOVER));
        RectF icon = { rc.X + (rc.Width - m_Glyph) / 2, rc.Y + (rc.Height - m_Glyph) / 2, m_Glyph, m_Glyph };
        context.Painter.Icon(m_Icon, icon, tokens.Color(LIBER_COLOR_TEXT1));
    }

private:
    WCHAR m_Icon[64];
    float m_Glyph;
};

class CNotificationCard : public Panel
{
public:
    CNotificationCard(UINT index, IControlListener *listener) : m_First(FALSE), m_Last(FALSE)
    {
        ZeroMemory(&m_Info, sizeof(m_Info));
        SetSize(-1, LIBER_CAL_CARD_H);
        m_Dismiss = new CRoundIconButton(LCA_DISMISS + index, L"dismiss_12_regular", 24, 12.0f);
        m_Dismiss->SetListener(listener);
        m_Dismiss->SetAlign(Align::End, Align::Start);
        Thickness margin = { 0, 6, 6, 0 };
        m_Dismiss->SetMargin(margin);
        AddChild(m_Dismiss);
    }

    void Set(const TRAY_NOTIFICATION_INFO *info, BOOL first, BOOL last)
    {
        if (info)
            m_Info = *info;
        m_First = first;
        m_Last = last;
        SetVisible(info != NULL);
        Invalidate();
    }

protected:
    void PaintOverride(PaintContext &context)
    {
        RectF rc = ToRectF(Bounds());
        const ThemeTokens &tokens = context.Tokens;
        float outer = (float)LIBER_RADIUS_TILE, inner = (float)LIBER_RADIUS_GROUP_INNER;
        WCHAR szTime[32];

        context.Painter.PushClip(rc, inner);
        context.Painter.FillRect(rc, tokens.Color(LIBER_COLOR_FILL_TILE));
        context.Painter.PopClip();
        if (m_First || m_Last)
        {
            RectF cap = { rc.X, m_First ? rc.Y : rc.Y + rc.Height / 2, rc.Width, rc.Height / 2 };
            context.Painter.PushClip(cap, 0.0f);
            context.Painter.FillRoundRect(rc, outer, tokens.Color(LIBER_COLOR_FILL_TILE));
            context.Painter.PopClip();
        }
        RectF icon = { rc.X + 12.0f, rc.Y + 10.0f, 16.0f, 16.0f };
        if (m_Info.hIcon)
            context.Painter.Image(m_Info.hIcon, icon);
        RectF app = { rc.X + 36.0f, rc.Y + 10.0f, rc.Width - 36.0f - 76.0f, 16.0f };
        context.Painter.Text(m_Info.szApp, app, LIBER_TYPE_CAPTION, tokens.Color(LIBER_COLOR_TEXT2),
                             TextMiddle | TextEllipsis);
        if (!GetTimeFormatW(LOCALE_USER_DEFAULT, TIME_NOSECONDS, &m_Info.stTime, NULL, szTime, _countof(szTime)))
            szTime[0] = 0;
        RectF time = { rc.X + rc.Width - 40.0f - 72.0f, rc.Y + 10.0f, 72.0f, 16.0f };
        context.Painter.Text(szTime, time, LIBER_TYPE_CAPTION, tokens.Color(LIBER_COLOR_TEXT2), TextRight | TextMiddle);
        RectF title = { rc.X + 12.0f, rc.Y + 32.0f, rc.Width - 24.0f, 20.0f };
        context.Painter.Text(m_Info.szTitle, title, LIBER_TYPE_BODY_STRONG, tokens.Color(LIBER_COLOR_TEXT1),
                             TextMiddle | TextEllipsis);
        RectF body = { rc.X + 12.0f, rc.Y + 54.0f, rc.Width - 24.0f, rc.Height - 66.0f };
        context.Painter.Text(m_Info.szText, body, LIBER_TYPE_CAPTION, tokens.Color(LIBER_COLOR_TEXT2), TextWrap);
    }

private:
    BOOL m_First;
    BOOL m_Last;
    TRAY_NOTIFICATION_INFO m_Info;
    CRoundIconButton *m_Dismiss;
};

class CDayCell : public Control
{
public:
    CDayCell(UINT index) : m_Today(FALSE)
    {
        ZeroMemory(&m_Day, sizeof(m_Day));
        SetId(LCA_DAY + index);
        SetSize(LIBER_CAL_COLUMN, LIBER_CAL_CELL + 2);
    }

    void Set(const LIBER_CALENDAR_DAY &day, BOOL today)
    {
        m_Day = day;
        m_Today = today;
        Invalidate();
    }

protected:
    void PaintOverride(PaintContext &context)
    {
        RectF rc = ToRectF(Bounds());
        const ThemeTokens &tokens = context.Tokens;
        RectF circle = { rc.X + (rc.Width - LIBER_CAL_CELL) / 2, rc.Y + 1.0f, (float)LIBER_CAL_CELL,
                         (float)LIBER_CAL_CELL };
        WCHAR szDay[4];

        if (m_Today)
            context.Painter.FillEllipse(circle, tokens.Color(LIBER_COLOR_ACCENT));
        else if (HasState(StateHover) || HasState(StatePressed))
            context.Painter.FillEllipse(circle, tokens.Color(LIBER_COLOR_FILL_SUBTLE_HOVER));
        StringCchPrintfW(szDay, _countof(szDay), L"%u", m_Day.wDay);
        context.Painter.Text(szDay, circle, m_Today ? LIBER_TYPE_BODY_STRONG : LIBER_TYPE_BODY,
                             tokens.Color(m_Today ? LIBER_COLOR_ON_ACCENT
                                                  : m_Day.bInMonth ? LIBER_COLOR_TEXT1 : LIBER_COLOR_TEXT3),
                             TextCenter | TextMiddle);
    }

private:
    LIBER_CALENDAR_DAY m_Day;
    BOOL m_Today;
};

class CCalendarSurface : public Surface
{
public:
    CCalendarSurface() : m_HiddenAt(0), m_Peer(NULL) {}
    ULONGLONG m_HiddenAt;
    CCalendarSurface *m_Peer;
    void Hide()
    {
        if (Window() && IsWindowVisible(Window()) && !IsClosing())
        {
            TransitionOut(24.0f, 150);
            m_HiddenAt = GetTickCount64();
        }
    }

protected:
    LRESULT HandleMessage(UINT msg, WPARAM wParam, LPARAM lParam)
    {
        if ((msg == WM_ACTIVATE && LOWORD(wParam) == WA_INACTIVE) || (msg == WM_KEYDOWN && wParam == VK_ESCAPE))
        {
            BOOL toPeer = msg == WM_ACTIVATE && m_Peer && (HWND)lParam == m_Peer->Window();
            if (!toPeer)
            {
                Hide();
                if (m_Peer)
                    m_Peer->Hide();
            }
            return 0;
        }
        return Surface::HandleMessage(msg, wParam, lParam);
    }
};

class CLiberCalendar final : public IControlListener
{
public:
    CLiberCalendar();
    ~CLiberCalendar();

    BOOL Open(const RECT *prcAnchor);
    void Close()
    {
        m_Surface.Hide();
        m_NotifSurface.Hide();
    }
    BOOL IsOpen() const { return m_Surface.Window() && IsWindowVisible(m_Surface.Window()) && !m_Surface.IsClosing(); }
    BOOL JustClosed() const { return m_Surface.m_HiddenAt && GetTickCount64() - m_Surface.m_HiddenAt < 250; }
    void OnControlEvent(Widget *source, ControlEvent event);

private:
    void Build();
    void SyncNotifications();
    void SyncMonth();
    void Place();

    CCalendarSurface m_Surface;
    CCalendarSurface m_NotifSurface;
    ThemeTokens m_Tokens;
    CGlassPanel *m_Root;
    CGlassPanel *m_Notifs;
    Label *m_Empty;
    CTextButton *m_Clear;
    CNotificationCard *m_Cards[LIBER_CAL_NOTIFS];
    Label *m_Today;
    Label *m_Month;
    Label *m_Weekdays[7];
    CDayCell *m_Days[LIBER_CALENDAR_CELLS];
    RECT m_Anchor;
    UINT m_Year;
    UINT m_MonthIndex;
    UINT m_FirstDay;
};

static CLiberCalendar *g_pLiberCalendar;

CLiberCalendar::CLiberCalendar()
    : m_Root(NULL), m_Notifs(NULL), m_Empty(NULL), m_Clear(NULL), m_Today(NULL), m_Month(NULL), m_Year(0),
      m_MonthIndex(0), m_FirstDay(0)
{
    ZeroMemory(m_Cards, sizeof(m_Cards));
    ZeroMemory(m_Weekdays, sizeof(m_Weekdays));
    ZeroMemory(m_Days, sizeof(m_Days));
    SetRectEmpty(&m_Anchor);
}

CLiberCalendar::~CLiberCalendar()
{
    m_Surface.SetRoot(NULL);
    m_NotifSurface.SetRoot(NULL);
    m_Surface.Destroy();
    m_NotifSurface.Destroy();
    delete m_Root;
    delete m_Notifs;
}

void CLiberCalendar::Build()
{
    Thickness notifPad = { 12, 12, 12, 12 };
    Thickness calPad = { 16, 16, 16, 16 };
    CGlassPanel *calendar = new CGlassPanel(0);
    m_Root = calendar;
    Panel *notifHead = new Panel();
    Panel *monthHead = new Panel();
    StackPanel *arrows = new StackPanel(Orientation::Horizontal, 4);
    GridPanel *weekdays = new GridPanel(7, 0, 0);
    GridPanel *grid = new GridPanel(7, 0, 0);
    CRoundIconButton *prev = new CRoundIconButton(LCA_PREV, L"chevron_up_16_regular", 24, 16.0f);
    CRoundIconButton *next = new CRoundIconButton(LCA_NEXT, L"chevron_down_16_regular", 24, 16.0f);

    m_Notifs = new CGlassPanel(2);
    m_Notifs->SetPadding(notifPad);
    Label *notifTitle = new Label(L"Notifications", LIBER_TYPE_BODY_STRONG, LIBER_COLOR_TEXT1, TextLeft | TextMiddle);
    notifTitle->SetAlign(Align::Start, Align::Center);
    Thickness titleMargin = { 6, 0, 0, 0 };
    notifTitle->SetMargin(titleMargin);
    m_Clear = new CTextButton(LCA_CLEAR, L"Clear all");
    m_Clear->SetAlign(Align::End, Align::Center);
    m_Clear->SetListener(this);
    notifHead->SetSize(-1, 26);
    notifHead->AddChild(notifTitle);
    notifHead->AddChild(m_Clear);
    m_Notifs->AddChild(notifHead);
    m_Empty = new Label(L"No new notifications", LIBER_TYPE_CAPTION, LIBER_COLOR_TEXT2, TextCenter | TextMiddle);
    m_Empty->SetSize(-1, 48);
    m_Notifs->AddChild(m_Empty);
    for (UINT i = 0; i < LIBER_CAL_NOTIFS; i++)
    {
        m_Cards[i] = new CNotificationCard(i, this);
        m_Notifs->AddChild(m_Cards[i]);
    }

    calendar->SetPadding(calPad);
    m_Today = new Label(L"", LIBER_TYPE_BODY_STRONG, LIBER_COLOR_TEXT1, TextLeft | TextMiddle);
    m_Today->SetSize(-1, 20);
    Thickness todayMargin = { 6, 0, 6, 12 };
    m_Today->SetMargin(todayMargin);
    m_Month = new Label(L"", LIBER_TYPE_BODY_STRONG, LIBER_COLOR_TEXT1, TextLeft | TextMiddle);
    m_Month->SetAlign(Align::Start, Align::Center);
    Thickness monthMargin = { 6, 0, 0, 0 };
    m_Month->SetMargin(monthMargin);
    prev->SetListener(this);
    next->SetListener(this);
    arrows->AddChild(prev);
    arrows->AddChild(next);
    arrows->SetAlign(Align::End, Align::Start);
    monthHead->SetSize(-1, 32);
    monthHead->AddChild(m_Month);
    monthHead->AddChild(arrows);
    for (UINT i = 0; i < 7; i++)
    {
        m_Weekdays[i] = new Label(L"", LIBER_TYPE_CAPTION, LIBER_COLOR_TEXT2, TextCenter | TextMiddle);
        m_Weekdays[i]->SetSize(LIBER_CAL_COLUMN, 28);
        weekdays->AddChild(m_Weekdays[i]);
    }
    for (UINT i = 0; i < LIBER_CALENDAR_CELLS; i++)
    {
        m_Days[i] = new CDayCell(i);
        grid->AddChild(m_Days[i]);
    }
    calendar->AddChild(m_Today);
    calendar->AddChild(monthHead);
    calendar->AddChild(weekdays);
    calendar->AddChild(grid);
}

void CLiberCalendar::SyncNotifications()
{
    UINT count = min(TrayNotifications_GetCount(), (UINT)LIBER_CAL_NOTIFS);

    for (UINT i = 0; i < LIBER_CAL_NOTIFS; i++)
    {
        TRAY_NOTIFICATION_INFO info;
        BOOL have = i < count && TrayNotifications_Get(i, &info);
        m_Cards[i]->Set(have ? &info : NULL, i == 0, i + 1 == count);
    }
    m_Empty->SetVisible(count == 0);
    m_Clear->SetVisible(count != 0);
}

void CLiberCalendar::SyncMonth()
{
    LIBER_CALENDAR_DAY days[LIBER_CALENDAR_CELLS];
    SYSTEMTIME now, first = { 0 };
    WCHAR szText[64];

    GetLocalTime(&now);
    if (!GetDateFormatW(LOCALE_USER_DEFAULT, 0, &now, L"dddd, MMMM d", szText, _countof(szText)))
        szText[0] = 0;
    m_Today->SetText(szText);
    first.wYear = (WORD)m_Year;
    first.wMonth = (WORD)m_MonthIndex;
    first.wDay = 1;
    if (!GetDateFormatW(LOCALE_USER_DEFAULT, 0, &first, L"MMMM yyyy", szText, _countof(szText)))
        szText[0] = 0;
    m_Month->SetText(szText);
    for (UINT i = 0; i < 7; i++)
    {
        WCHAR szName[16];
        if (!GetLocaleInfoW(LOCALE_USER_DEFAULT, LOCALE_SSHORTESTDAYNAME1 + (m_FirstDay + i + 6) % 7, szName,
                            _countof(szName)))
            szName[0] = 0;
        m_Weekdays[i]->SetText(szName);
    }
    LiberCalendar_MonthGrid(m_Year, m_MonthIndex, m_FirstDay, days);
    for (UINT i = 0; i < LIBER_CALENDAR_CELLS; i++)
    {
        BOOL today = days[i].bInMonth && days[i].wYear == now.wYear && days[i].wMonth == now.wMonth &&
                     days[i].wDay == now.wDay;
        m_Days[i]->Set(days[i], today);
    }
}

void CLiberCalendar::Place()
{
    SIZE available = { LIBER_CAL_WIDTH, LayoutInfinite };
    SIZE size = m_Root->Measure(available);
    SIZE notifSize = m_Notifs->Measure(available);
    UINT dpi = m_Surface.Dpi();
    POINT pt;

    LiberShell_PlaceFlyout(&m_Anchor, ScaleToDpi(LIBER_CAL_WIDTH, dpi), ScaleToDpi(size.cy, dpi), LIBER_FLYOUT_RIGHT, &pt);
    m_Surface.SetPosition(pt.x, pt.y, LIBER_CAL_WIDTH, size.cy);
    m_Surface.RequestLayout();
    m_NotifSurface.SetPosition(pt.x, pt.y - ScaleToDpi(LIBER_CAL_GAP + notifSize.cy, dpi), LIBER_CAL_WIDTH,
                               notifSize.cy);
    m_NotifSurface.RequestLayout();
}

BOOL CLiberCalendar::Open(const RECT *prcAnchor)
{
    SYSTEMTIME now;
    WCHAR szFirst[4];

    if (!m_Root)
    {
        if (!m_Surface.Create(NULL, L"Liber Calendar", WS_EX_TOPMOST | WS_EX_TOOLWINDOW) ||
            !m_NotifSurface.Create(NULL, L"Liber Notifications", WS_EX_TOPMOST | WS_EX_TOOLWINDOW))
        {
            m_Surface.Destroy();
            m_NotifSurface.Destroy();
            return FALSE;
        }
        m_Surface.m_Peer = &m_NotifSurface;
        m_NotifSurface.m_Peer = &m_Surface;
        Surface *surfaces[] = { &m_Surface, &m_NotifSurface };
        for (UINT i = 0; i < _countof(surfaces); i++)
        {
            surfaces[i]->SetInputShape((float)LIBER_RADIUS_FLYOUT);
            surfaces[i]->SetElevation(LIBER_ELEVATION_E32, (float)LIBER_RADIUS_FLYOUT);
        }
        Build();
        m_Surface.SetRoot(m_Root);
        m_NotifSurface.SetRoot(m_Notifs);
    }
    m_Anchor = *prcAnchor;
    GetLocalTime(&now);
    m_Year = now.wYear;
    m_MonthIndex = now.wMonth;
    m_FirstDay = 0;
    if (GetLocaleInfoW(LOCALE_USER_DEFAULT, LOCALE_IFIRSTDAYOFWEEK, szFirst, _countof(szFirst)) && szFirst[0] >= L'0' &&
        szFirst[0] <= L'6')
    {
        m_FirstDay = (UINT)(szFirst[0] - L'0' + 1) % 7;
    }

    LiberShell_BuildTokens(&m_Tokens);
    Surface *surfaces[] = { &m_NotifSurface, &m_Surface };
    for (UINT i = 0; i < _countof(surfaces); i++)
    {
        LiberShell_ApplyFont(surfaces[i]);
        surfaces[i]->SetTokens(&m_Tokens);
        LiberShell_ApplyMaterial(surfaces[i], &m_Tokens, LIBER_MATERIAL_THICK, (float)LIBER_RADIUS_FLYOUT);
    }
    SyncNotifications();
    SyncMonth();
    Place();
    for (UINT i = 0; i < _countof(surfaces); i++)
    {
        LiberShell_SampleBackdrop(surfaces[i], &m_Tokens, FALSE);
        surfaces[i]->Render(AnimationClock::Now());
        ShowWindow(surfaces[i]->Window(), SW_SHOW);
        surfaces[i]->TransitionIn(32.0f, 300);
    }
    SetForegroundWindow(m_Surface.Window());
    return TRUE;
}

void CLiberCalendar::OnControlEvent(Widget *source, ControlEvent event)
{
    UINT id = static_cast<Control *>(source)->Id();

    UNREFERENCED_PARAMETER(event);
    if (id == LCA_PREV || id == LCA_NEXT)
    {
        if (id == LCA_PREV)
        {
            m_MonthIndex = m_MonthIndex == 1 ? 12 : m_MonthIndex - 1;
            m_Year -= m_MonthIndex == 12 ? 1 : 0;
        }
        else
        {
            m_MonthIndex = m_MonthIndex == 12 ? 1 : m_MonthIndex + 1;
            m_Year += m_MonthIndex == 1 ? 1 : 0;
        }
        SyncMonth();
    }
    else if (id == LCA_CLEAR)
    {
        TrayNotifications_Clear();
        SyncNotifications();
        Place();
    }
    else if (id >= LCA_DISMISS && id < LCA_DISMISS + LIBER_CAL_NOTIFS)
    {
        TrayNotifications_Remove(id - LCA_DISMISS);
        SyncNotifications();
        Place();
    }
    m_Surface.InvalidateRect(m_Root->Bounds());
    m_NotifSurface.InvalidateRect(m_Notifs->Bounds());
    m_Surface.Render(AnimationClock::Now());
    m_NotifSurface.Render(AnimationClock::Now());
}

VOID LiberCalendar_Toggle(IN const RECT *prcAnchor)
{
    if (!g_pLiberCalendar)
        g_pLiberCalendar = new CLiberCalendar();
    if (!g_pLiberCalendar)
        return;
    if (g_pLiberCalendar->IsOpen())
        g_pLiberCalendar->Close();
    else if (!g_pLiberCalendar->JustClosed())
        g_pLiberCalendar->Open(prcAnchor);
}

BOOL LiberCalendar_IsOpen(VOID)
{
    return g_pLiberCalendar && g_pLiberCalendar->IsOpen();
}

VOID LiberCalendar_Destroy(VOID)
{
    delete g_pLiberCalendar;
    g_pLiberCalendar = NULL;
}
