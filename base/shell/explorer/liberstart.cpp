/*
 * PROJECT:     LiberNT Explorer
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Liber Start menu
 * COPYRIGHT:   Copyright 2026 Eitan Hadar <eitanhadar4@gmail.com>
 */

#include "precomp.h"

#include "libershell.h"
#include "startmodel.h"

using namespace LiberUI;

#define LIBER_START_WIDTH   650
#define LIBER_START_COLUMNS 6
#define LIBER_START_ROWS    3
#define LIBER_START_TILES   (LIBER_START_COLUMNS * LIBER_START_ROWS)
#define LIBER_START_TILE_H  84

enum
{
    LST_POWER = 1,
    LST_TILE = 100
};

class CAppTile : public Control
{
public:
    CAppTile() : m_Item(NULL)
    {
        SetSize(-1, LIBER_START_TILE_H);
    }

    void SetItem(SM2ITEM *item)
    {
        m_Item = item;
        SetVisible(item != NULL);
        Invalidate();
    }

    SM2ITEM *Item() const { return m_Item; }
    void OnClick() { Raise(ControlClicked); }

protected:
    void PaintOverride(PaintContext &context)
    {
        RectF rc = ToRectF(Bounds());
        const ThemeTokens &tokens = context.Tokens;

        if (!m_Item)
            return;
        if (HasState(StateHover) || HasState(StatePressed) || HasState(StateFocused))
            context.Painter.FillRoundRect(rc, (float)LIBER_RADIUS_CARD,
                                          tokens.Color(HasState(StatePressed) ? LIBER_COLOR_FILL_SUBTLE_PRESSED
                                                                              : LIBER_COLOR_FILL_SUBTLE_HOVER));
        if (!m_Item->bSmallTried)
        {
            m_Item->bSmallTried = TRUE;
            m_Item->hLargeIcon = SM2LoadShellIcon(m_Item->szPath, TRUE);
        }
        RectF icon = { rc.X + (rc.Width - 40.0f) / 2, rc.Y + 10.0f, 40.0f, 40.0f };
        if (m_Item->hLargeIcon)
            context.Painter.Image(m_Item->hLargeIcon, icon);
        RectF text = { rc.X + 4.0f, rc.Y + 54.0f, rc.Width - 8.0f, 20.0f };
        context.Painter.Text(m_Item->szName, text, LIBER_TYPE_CAPTION, tokens.Color(LIBER_COLOR_TEXT1),
                             TextCenter | TextMiddle | TextEllipsis);
    }

private:
    SM2ITEM *m_Item;
};

class CSearchBox : public Widget
{
public:
    CSearchBox()
    {
        m_Query[0] = 0;
        SetSize(-1, LIBER_CONTROL_LG);
    }

    LPCWSTR Query() const { return m_Query; }
    BOOL Append(WCHAR ch)
    {
        int len = lstrlenW(m_Query);
        if (len + 1 >= (int)_countof(m_Query))
            return FALSE;
        m_Query[len] = ch;
        m_Query[len + 1] = 0;
        Invalidate();
        return TRUE;
    }
    BOOL Backspace()
    {
        int len = lstrlenW(m_Query);
        if (len == 0)
            return FALSE;
        m_Query[len - 1] = 0;
        Invalidate();
        return TRUE;
    }
    void Clear()
    {
        m_Query[0] = 0;
        Invalidate();
    }

protected:
    void PaintOverride(PaintContext &context)
    {
        RectF rc = ToRectF(Bounds());
        const ThemeTokens &tokens = context.Tokens;
        float radius = rc.Height / 2;

        context.Painter.FillRoundRect(rc, radius, tokens.Color(LIBER_COLOR_FILL_INPUT));
        context.Painter.StrokeRoundRect(rc, radius, tokens.Color(m_Query[0] ? LIBER_COLOR_ACCENT : LIBER_COLOR_STROKE_CONTROL),
                                        m_Query[0] ? 2.0f : 1.0f);
        RectF icon = { rc.X + 14.0f, rc.Y + (rc.Height - 16.0f) / 2, 16.0f, 16.0f };
        context.Painter.Icon(L"search_16_regular", icon, tokens.Color(LIBER_COLOR_TEXT2));
        RectF text = { rc.X + 40.0f, rc.Y, rc.Width - 56.0f, rc.Height };
        if (m_Query[0])
            context.Painter.Text(m_Query, text, LIBER_TYPE_BODY, tokens.Color(LIBER_COLOR_TEXT1), TextMiddle | TextEllipsis);
        else
            context.Painter.Text(L"Type to search apps", text, LIBER_TYPE_BODY, tokens.Color(LIBER_COLOR_TEXT3),
                                 TextMiddle | TextEllipsis);
    }

private:
    WCHAR m_Query[64];
};

class CStartSurface : public Surface
{
public:
    CStartSurface() : m_Owner(NULL), m_HiddenAt(0) {}
    class CLiberStart *m_Owner;
    ULONGLONG m_HiddenAt;
    void Hide()
    {
        if (Window() && IsWindowVisible(Window()))
        {
            ShowWindow(Window(), SW_HIDE);
            m_HiddenAt = GetTickCount64();
        }
    }

protected:
    LRESULT HandleMessage(UINT msg, WPARAM wParam, LPARAM lParam);
};

class CLiberStart final : public IControlListener
{
public:
    CLiberStart();
    ~CLiberStart();

    BOOL Open(const RECT *prcAnchor);
    void Close() { m_Surface.Hide(); }
    BOOL IsOpen() const { return m_Surface.Window() && IsWindowVisible(m_Surface.Window()); }
    BOOL JustClosed() const { return m_Surface.m_HiddenAt && GetTickCount64() - m_Surface.m_HiddenAt < 250; }
    void OnControlEvent(Widget *source, ControlEvent event);
    BOOL OnChar(WCHAR ch);
    BOOL OnKey(UINT vk);

private:
    void Build();
    void Filter();
    void ShowPowerMenu();

    CStartSurface m_Surface;
    ThemeTokens m_Tokens;
    CLiberPanel *m_Root;
    CSearchBox *m_Search;
    Label *m_Heading;
    CAppTile *m_Tiles[LIBER_START_TILES];
    Label *m_User;
    SM2ITEM m_Tree;
    CAtlArray<SM2ITEM *> m_Apps;
    BOOL m_Loaded;
};

static CLiberStart *g_pLiberStart;

static int __cdecl CompareApps(const void *a, const void *b)
{
    return lstrcmpiW((*(SM2ITEM *const *)a)->szName, (*(SM2ITEM *const *)b)->szName);
}

static void LiberStart_SortApps(SM2ITEM **apps, SIZE_T count)
{
    if (count > 1)
        qsort(apps, count, sizeof(apps[0]), CompareApps);
}

LRESULT CStartSurface::HandleMessage(UINT msg, WPARAM wParam, LPARAM lParam)
{
    if (msg == WM_ACTIVATE && LOWORD(wParam) == WA_INACTIVE)
    {
        Hide();
        return 0;
    }
    if (msg == WM_CHAR && m_Owner && m_Owner->OnChar((WCHAR)wParam))
    {
        Render(AnimationClock::Now());
        return 0;
    }
    if (msg == WM_KEYDOWN && m_Owner && m_Owner->OnKey((UINT)wParam))
    {
        Render(AnimationClock::Now());
        return 0;
    }
    return Surface::HandleMessage(msg, wParam, lParam);
}

CLiberStart::CLiberStart()
    : m_Root(NULL), m_Search(NULL), m_Heading(NULL), m_User(NULL), m_Loaded(FALSE)
{
    ZeroMemory(m_Tiles, sizeof(m_Tiles));
    m_Surface.m_Owner = this;
}

CLiberStart::~CLiberStart()
{
    m_Surface.SetRoot(NULL);
    m_Surface.Destroy();
    delete m_Root;
    m_Tree.Clear();
}

void CLiberStart::Build()
{
    Thickness pad = { 32, 24, 32, 20 };
    GridPanel *grid = new GridPanel(LIBER_START_COLUMNS, 8, 4);
    StackPanel *footer = new StackPanel(Orientation::Horizontal, 8);
    IconButton *power = new IconButton(L"power_20_regular", LIBER_CONTROL_LG);
    WCHAR szUser[257];
    DWORD cch = _countof(szUser);

    m_Root = new CLiberPanel(Orientation::Vertical, 16, (float)LIBER_RADIUS_FLYOUT, LIBER_MATERIAL_THICK);
    m_Root->SetPadding(pad);
    m_Search = new CSearchBox();
    m_Heading = new Label(L"Apps", LIBER_TYPE_BODY_STRONG, LIBER_COLOR_TEXT1, TextLeft | TextMiddle);
    m_Heading->SetSize(-1, 20);
    for (UINT i = 0; i < LIBER_START_TILES; i++)
    {
        m_Tiles[i] = new CAppTile();
        m_Tiles[i]->SetId(LST_TILE + i);
        m_Tiles[i]->SetListener(this);
        grid->AddChild(m_Tiles[i]);
    }
    if (!GetUserNameW(szUser, &cch))
        szUser[0] = 0;
    m_User = new Label(szUser, LIBER_TYPE_BODY, LIBER_COLOR_TEXT1, TextLeft | TextMiddle);
    m_User->SetSize(LIBER_START_WIDTH - 64 - LIBER_CONTROL_LG - 8, LIBER_CONTROL_LG);
    power->SetId(LST_POWER);
    power->SetListener(this);
    footer->AddChild(m_User);
    footer->AddChild(power);

    m_Root->AddChild(m_Search);
    m_Root->AddChild(m_Heading);
    m_Root->AddChild(grid);
    m_Root->AddChild(footer);
}

void CLiberStart::Filter()
{
    UINT shown = 0;
    LPCWSTR query = m_Search->Query();

    for (SIZE_T i = 0; i < m_Apps.GetCount() && shown < LIBER_START_TILES; i++)
    {
        if (LiberStart_Matches(m_Apps[i]->szName, query))
            m_Tiles[shown++]->SetItem(m_Apps[i]);
    }
    for (UINT i = shown; i < LIBER_START_TILES; i++)
        m_Tiles[i]->SetItem(NULL);
    m_Heading->SetText(query[0] ? (shown ? L"Best matches" : L"No apps match") : L"Apps");
    m_Surface.RequestLayout();
    m_Surface.InvalidateRect(m_Root->Bounds());
}

BOOL CLiberStart::OnChar(WCHAR ch)
{
    if (ch == L'\b')
    {
        if (!m_Search->Backspace())
            return FALSE;
    }
    else if (ch >= L' ')
    {
        if (!m_Search->Append(ch))
            return FALSE;
    }
    else
    {
        return FALSE;
    }
    Filter();
    return TRUE;
}

BOOL CLiberStart::OnKey(UINT vk)
{
    if (vk == VK_ESCAPE)
    {
        if (m_Search->Query()[0])
        {
            m_Search->Clear();
            Filter();
        }
        else
        {
            Close();
        }
        return TRUE;
    }
    if (vk == VK_RETURN && !m_Surface.Input().Focus() && m_Tiles[0]->Item())
    {
        SM2LaunchItem(m_Tiles[0]->Item());
        Close();
        return TRUE;
    }
    return FALSE;
}

BOOL CLiberStart::Open(const RECT *prcAnchor)
{
    if (!m_Root)
    {
        if (!m_Surface.Create(NULL, L"Liber Start", WS_EX_TOPMOST | WS_EX_TOOLWINDOW))
            return FALSE;
        m_Surface.SetInputShape((float)LIBER_RADIUS_FLYOUT);
        Build();
        m_Surface.SetRoot(m_Root);
    }
    if (!m_Loaded)
    {
        SM2BuildTree(&m_Tree);
        m_Apps.SetCount(0);
        SM2FlattenTree(&m_Tree, m_Apps);
        LiberStart_SortApps(m_Apps.GetData(), m_Apps.GetCount());
        m_Loaded = TRUE;
    }

    m_Search->Clear();
    m_Surface.Input().SetFocus(NULL);
    LiberShell_BuildTokens(&m_Tokens);
    LiberShell_ApplyFont(&m_Surface);
    m_Surface.SetTokens(&m_Tokens);
    Filter();

    SIZE available = { LIBER_START_WIDTH, LayoutInfinite };
    SIZE size = m_Root->Measure(available);
    UINT dpi = m_Surface.Dpi();
    HMONITOR monitor = MonitorFromRect(prcAnchor, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi = { sizeof(mi) };
    GetMonitorInfoW(monitor, &mi);
    int width = ScaleToDpi(LIBER_START_WIDTH, dpi);
    int height = ScaleToDpi(size.cy, dpi);
    POINT pt = { mi.rcMonitor.left + (mi.rcMonitor.right - mi.rcMonitor.left - width) / 2,
                 prcAnchor->top - ScaleToDpi(LIBER_FLYOUT_GAP, dpi) - height };
    if (pt.y < mi.rcMonitor.top)
        pt.y = mi.rcMonitor.top;

    m_Surface.SetPosition(pt.x, pt.y, LIBER_START_WIDTH, size.cy);
    m_Surface.RequestLayout();
    m_Surface.Render(AnimationClock::Now());
    ShowWindow(m_Surface.Window(), SW_SHOW);
    SetForegroundWindow(m_Surface.Window());
    return TRUE;
}

void CLiberStart::ShowPowerMenu()
{
    static const struct { INT Cmd; LPCWSTR Text; } Items[] =
    {
        { SM2F_LOCK, L"Lock" },
        { SM2F_SLEEP, L"Sleep" },
        { SM2F_HIBERNATE, L"Hibernate" },
        { SM2F_LOGOFF, L"Sign out" },
        { SM2F_RESTART, L"Restart" },
        { SM2F_SHUTDOWN, L"Shut down" },
    };
    HMENU menu = CreatePopupMenu();
    POINT pt;

    if (!menu)
        return;
    for (UINT i = 0; i < _countof(Items); i++)
    {
        if (StartMenu2_IsPowerCommandAllowed(Items[i].Cmd))
            AppendMenuW(menu, MF_STRING, (UINT_PTR)Items[i].Cmd, Items[i].Text);
    }
    GetCursorPos(&pt);
    INT cmd = (INT)TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_BOTTOMALIGN, pt.x, pt.y, 0,
                                  m_Surface.Window(), NULL);
    DestroyMenu(menu);
    if (cmd > 0)
    {
        Close();
        StartMenu2_DoPowerCommand(NULL, cmd);
    }
}

void CLiberStart::OnControlEvent(Widget *source, ControlEvent event)
{
    Control *control = static_cast<Control *>(source);

    UNREFERENCED_PARAMETER(event);
    if (control->Id() == LST_POWER)
    {
        ShowPowerMenu();
        return;
    }
    if (control->Id() >= LST_TILE && control->Id() < LST_TILE + LIBER_START_TILES)
    {
        SM2ITEM *item = m_Tiles[control->Id() - LST_TILE]->Item();
        if (item)
        {
            Close();
            SM2LaunchItem(item);
        }
    }
}

VOID LiberStart_Toggle(IN const RECT *prcAnchor)
{
    if (!g_pLiberStart)
        g_pLiberStart = new CLiberStart();
    if (!g_pLiberStart)
        return;
    if (g_pLiberStart->IsOpen())
        g_pLiberStart->Close();
    else if (!g_pLiberStart->JustClosed())
        g_pLiberStart->Open(prcAnchor);
}

BOOL LiberStart_IsOpen(VOID)
{
    return g_pLiberStart && g_pLiberStart->IsOpen();
}

VOID LiberStart_Destroy(VOID)
{
    delete g_pLiberStart;
    g_pLiberStart = NULL;
}
