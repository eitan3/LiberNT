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

#define LIBER_START_WIDTH    660
#define LIBER_START_COLUMNS  6
#define LIBER_START_ROWS     3
#define LIBER_START_TILES    (LIBER_START_COLUMNS * LIBER_START_ROWS)
#define LIBER_START_TILE_H   88
#define LIBER_START_RECENT   6
#define LIBER_START_ROW_H    56
#define LIBER_START_FOOTER_H 68

#define LIBER_START_PIN_CHARS (64 * MAX_PATH + 1)

enum
{
    LSM_PIN_START = 1,
    LSM_UNPIN_START,
    LSM_PIN_TASKBAR,
    LSM_OPEN_LOCATION
};

class CLiberStart;
static void LiberStart_TileMenu(SM2ITEM *item, POINT screen);

enum
{
    LST_POWER = 1,
    LST_ALL_APPS,
    LST_MORE,
    LST_USER,
    LST_TILE = 100,
    LST_RECENT = 200
};

static HICON LoadAppIcon(SM2ITEM *item)
{
    if (!lstrcmpiW(PathFindExtensionW(item->szPath), L".lnk"))
    {
        CComPtr<IShellLinkW> link;
        CComPtr<IPersistFile> persist;
        WCHAR location[MAX_PATH] = L"", expanded[MAX_PATH];
        int index = 0;
        HICON icon = NULL;

        if (SUCCEEDED(link.CoCreateInstance(CLSID_ShellLink, IID_IShellLinkW)) &&
            SUCCEEDED(link->QueryInterface(IID_PPV_ARG(IPersistFile, &persist))) &&
            SUCCEEDED(persist->Load(item->szPath, STGM_READ)) &&
            SUCCEEDED(link->GetIconLocation(location, _countof(location), &index)) && location[0])
        {
            DWORD cch = ExpandEnvironmentStringsW(location, expanded, _countof(expanded));
            if (cch && cch <= _countof(expanded) && ExtractIconExW(expanded, index, &icon, NULL, 1) && icon)
                return icon;
        }
        if (SM2GetTargetPath(item))
        {
            icon = SM2LoadShellIcon(item->szTargetPath, TRUE);
            if (icon)
                return icon;
        }
    }
    return SM2LoadShellIcon(item->szPath, TRUE);
}

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
    BOOL OnContextMenu(POINT screen)
    {
        if (!m_Item)
            return FALSE;
        LiberStart_TileMenu(m_Item, screen);
        return TRUE;
    }

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
            m_Item->hLargeIcon = LoadAppIcon(m_Item);
        }
        RectF icon = { rc.X + (rc.Width - 40.0f) / 2, rc.Y + 12.0f, 40.0f, 40.0f };
        if (m_Item->hLargeIcon)
            context.Painter.Image(m_Item->hLargeIcon, icon);
        RectF text = { rc.X + 4.0f, rc.Y + 60.0f, rc.Width - 8.0f, 16.0f };
        context.Painter.Text(m_Item->szName, text, LIBER_TYPE_CAPTION, tokens.Color(LIBER_COLOR_TEXT1),
                             TextCenter | TextMiddle | TextEllipsis);
    }

private:
    SM2ITEM *m_Item;
};

class CRecentRow : public Control
{
public:
    CRecentRow() : m_Icon(NULL)
    {
        m_Path[0] = m_Name[0] = m_When[0] = 0;
        SetSize(-1, LIBER_START_ROW_H);
    }

    ~CRecentRow()
    {
        Reset();
    }

    void Set(const LIBER_RECENT_FILE *file, const SYSTEMTIME *now)
    {
        Reset();
        if (file)
        {
            StringCchCopyW(m_Path, _countof(m_Path), file->szLink);
            StringCchCopyW(m_Name, _countof(m_Name), file->szName);
            LiberStart_FormatWhen(now, &file->stWritten, m_When, _countof(m_When));
            SHFILEINFOW sfi = { 0 };
            if (SHGetFileInfoW(m_Path, 0, &sfi, sizeof(sfi), SHGFI_ICON | SHGFI_LARGEICON))
                m_Icon = sfi.hIcon;
        }
        SetVisible(file != NULL);
        Invalidate();
    }

    LPCWSTR Path() const { return m_Path; }
    void OnClick() { Raise(ControlClicked); }

protected:
    void PaintOverride(PaintContext &context)
    {
        RectF rc = ToRectF(Bounds());
        const ThemeTokens &tokens = context.Tokens;

        if (HasState(StateHover) || HasState(StatePressed) || HasState(StateFocused))
            context.Painter.FillRoundRect(rc, (float)LIBER_RADIUS_R6,
                                          tokens.Color(HasState(StatePressed) ? LIBER_COLOR_FILL_SUBTLE_PRESSED
                                                                              : LIBER_COLOR_FILL_SUBTLE_HOVER));
        RectF icon = { rc.X + 12.0f, rc.Y + (rc.Height - 32.0f) / 2, 32.0f, 32.0f };
        if (m_Icon)
            context.Painter.Image(m_Icon, icon);
        RectF name = { rc.X + 56.0f, rc.Y + 10.0f, rc.Width - 68.0f, 20.0f };
        RectF when = { rc.X + 56.0f, rc.Y + 30.0f, rc.Width - 68.0f, 16.0f };
        context.Painter.Text(m_Name, name, LIBER_TYPE_BODY, tokens.Color(LIBER_COLOR_TEXT1), TextMiddle | TextEllipsis);
        context.Painter.Text(m_When, when, LIBER_TYPE_CAPTION, tokens.Color(LIBER_COLOR_TEXT2), TextMiddle | TextEllipsis);
    }

private:
    void Reset()
    {
        if (m_Icon)
            DestroyIcon(m_Icon);
        m_Icon = NULL;
    }

    WCHAR m_Path[MAX_PATH];
    WCHAR m_Name[MAX_PATH];
    WCHAR m_When[64];
    HICON m_Icon;
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

        context.Painter.FillRoundRect(rc, radius, tokens.Color(m_Query[0] ? LIBER_COLOR_FILL_INPUT_FOCUS
                                                                          : LIBER_COLOR_FILL_INPUT));
        if (m_Query[0])
        {
            RectF ring = { rc.X - 1.0f, rc.Y - 1.0f, rc.Width + 2.0f, rc.Height + 2.0f };
            context.Painter.StrokeRoundRect(ring, radius + 1.0f, tokens.Color(LIBER_COLOR_ACCENT_RING), 2.0f);
        }
        else
        {
            context.Painter.StrokeRoundRect(rc, radius, tokens.Color(LIBER_COLOR_STROKE_CONTROL), 1.0f);
        }
        RectF icon = { rc.X + 13.0f, rc.Y + (rc.Height - 20.0f) / 2, 20.0f, 20.0f };
        context.Painter.Icon(L"search_20_regular", icon, tokens.Color(LIBER_COLOR_TEXT2));
        RectF text = { rc.X + 41.0f, rc.Y, rc.Width - 53.0f, rc.Height };
        if (m_Query[0])
            context.Painter.Text(m_Query, text, LIBER_TYPE_BODY, tokens.Color(LIBER_COLOR_TEXT1), TextMiddle | TextEllipsis);
        else
            context.Painter.Text(L"Search apps, settings and documents", text, LIBER_TYPE_BODY,
                                 tokens.Color(LIBER_COLOR_TEXT3), TextMiddle | TextEllipsis);
    }

private:
    WCHAR m_Query[64];
};

class CStartFooter : public Panel
{
public:
    CStartFooter()
    {
        Thickness pad = { 40, (LIBER_START_FOOTER_H - 44) / 2, 40, (LIBER_START_FOOTER_H - 44) / 2 };
        SetPadding(pad);
        SetSize(-1, LIBER_START_FOOTER_H);
    }

protected:
    void PaintOverride(PaintContext &context)
    {
        RectF rc = ToRectF(Bounds());
        RectF flyout = Parent() ? ToRectF(Parent()->Bounds()) : rc;

        context.Painter.PushClip(flyout, (float)LIBER_RADIUS_FLYOUT);
        context.Painter.FillRect(rc, context.Tokens.Color(LIBER_COLOR_FILL_SUBTLE_HOVER));
        RectF divider = { rc.X, rc.Y, rc.Width, 1.0f };
        context.Painter.FillRect(divider, context.Tokens.Color(LIBER_COLOR_STROKE_DIVIDER));
        context.Painter.PopClip();
    }
};

class CUserButton : public Control
{
public:
    CUserButton(LPCWSTR name)
    {
        StringCchCopyW(m_Name, _countof(m_Name), name ? name : L"");
        SetSize(-1, 44);
    }

    void OnClick() { Raise(ControlClicked); }

protected:
    SIZE MeasureOverride(SIZE available)
    {
        LONG text = MeasureText(m_Name, LIBER_TYPE_BODY, 0.0f).cx;
        if (text <= 0)
            text = (LONG)lstrlenW(m_Name) * 8;
        SIZE size = { min(available.cx, (LONG)(6 + 32 + 12 + text + 1 + 16)), 44 };
        return size;
    }

    void PaintOverride(PaintContext &context)
    {
        RectF rc = ToRectF(Bounds());
        const ThemeTokens &tokens = context.Tokens;

        if (HasState(StateHover) || HasState(StatePressed))
            context.Painter.FillRoundRect(rc, rc.Height / 2, tokens.Color(HasState(StatePressed)
                                                                              ? LIBER_COLOR_FILL_SUBTLE_PRESSED
                                                                              : LIBER_COLOR_FILL_SUBTLE_HOVER));
        RectF avatar = { rc.X + 6.0f, rc.Y + (rc.Height - 32.0f) / 2, 32.0f, 32.0f };
        context.Painter.FillEllipse(avatar, tokens.Color(LIBER_COLOR_ACCENT_SUBTLE));
        RectF glyph = { avatar.X + 6.0f, avatar.Y + 6.0f, 20.0f, 20.0f };
        context.Painter.Icon(L"person_20_regular", glyph, tokens.Color(LIBER_COLOR_ACCENT_TEXT));
        RectF name = { avatar.X + 44.0f, rc.Y, rc.Width - 60.0f, rc.Height };
        context.Painter.Text(m_Name, name, LIBER_TYPE_BODY, tokens.Color(LIBER_COLOR_TEXT1), TextMiddle | TextEllipsis);
    }

private:
    WCHAR m_Name[64];
};

class CStartSurface : public Surface
{
public:
    CStartSurface() : m_Owner(NULL), m_HiddenAt(0), m_InMenu(FALSE) {}
    class CLiberStart *m_Owner;
    ULONGLONG m_HiddenAt;
    BOOL m_InMenu;
    void Hide()
    {
        if (Window() && IsWindowVisible(Window()) && !IsClosing())
        {
            TransitionOut(24.0f, 150);
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
    BOOL IsOpen() const { return m_Surface.Window() && IsWindowVisible(m_Surface.Window()) && !m_Surface.IsClosing(); }
    BOOL JustClosed() const { return m_Surface.m_HiddenAt && GetTickCount64() - m_Surface.m_HiddenAt < 250; }
    void OnControlEvent(Widget *source, ControlEvent event);
    BOOL OnChar(WCHAR ch);
    BOOL OnKey(UINT vk);
    BOOL OnWheel(int delta);
    void TileMenu(SM2ITEM *item, POINT screen);
    UINT TrackMenu(const LIBER_MENU_ITEM *items, UINT count, POINT screen);

private:
    void Build();
    void Filter();
    void LoadRecent();
    void AddSystemApps();
    UINT FillDefaultPins();
    void ShowPowerMenu(BOOL user);

    CStartSurface m_Surface;
    ThemeTokens m_Tokens;
    CLiberPanel *m_Root;
    CSearchBox *m_Search;
    CLiberSectionHead *m_PinnedHead;
    CLiberSectionHead *m_RecentHead;
    CAppTile *m_Tiles[LIBER_START_TILES];
    GridPanel *m_RecentGrid;
    CRecentRow *m_Recent[LIBER_START_RECENT];
    SM2ITEM m_Tree;
    CAtlArray<SM2ITEM *> m_Apps;
    BOOL m_Loaded;
    BOOL m_AllApps;
    UINT m_Offset;
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
        if (!m_InMenu)
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
    if (msg == WM_MOUSEWHEEL && m_Owner && m_Owner->OnWheel(GET_WHEEL_DELTA_WPARAM(wParam)))
    {
        Render(AnimationClock::Now());
        return 0;
    }
    return Surface::HandleMessage(msg, wParam, lParam);
}

CLiberStart::CLiberStart()
    : m_Root(NULL), m_Search(NULL), m_PinnedHead(NULL), m_RecentHead(NULL), m_RecentGrid(NULL), m_Loaded(FALSE),
      m_AllApps(FALSE), m_Offset(0)
{
    ZeroMemory(m_Tiles, sizeof(m_Tiles));
    ZeroMemory(m_Recent, sizeof(m_Recent));
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
    Thickness searchPad = { 8, 8, 8, 0 };
    Thickness bodyPad = { 32, 22, 32, 12 };
    StackPanel *searchRow = new StackPanel(Orientation::Vertical, 0);
    StackPanel *body = new StackPanel(Orientation::Vertical, 10);
    GridPanel *grid = new GridPanel(LIBER_START_COLUMNS, 2, 2);
    CStartFooter *footer = new CStartFooter();
    IconButton *power = new IconButton(L"power_20_regular", LIBER_CONTROL_MD);
    WCHAR szUser[257];
    DWORD cch = _countof(szUser);

    m_Root = new CLiberPanel(Orientation::Vertical, 0, (float)LIBER_RADIUS_FLYOUT, LIBER_MATERIAL_THICK);
    searchRow->SetPadding(searchPad);
    body->SetPadding(bodyPad);
    m_Search = new CSearchBox();
    searchRow->AddChild(m_Search);

    m_PinnedHead = new CLiberSectionHead(L"Pinned", L"All apps", LST_ALL_APPS, this);
    for (UINT i = 0; i < LIBER_START_TILES; i++)
    {
        m_Tiles[i] = new CAppTile();
        m_Tiles[i]->SetId(LST_TILE + i);
        m_Tiles[i]->SetListener(this);
        grid->AddChild(m_Tiles[i]);
    }
    m_RecentHead = new CLiberSectionHead(L"Recommended", L"More", LST_MORE, this);
    Thickness headGap = { 0, 12, 0, 0 };
    m_RecentHead->SetMargin(headGap);
    m_RecentGrid = new GridPanel(2, 2, 2);
    for (UINT i = 0; i < LIBER_START_RECENT; i++)
    {
        m_Recent[i] = new CRecentRow();
        m_Recent[i]->SetId(LST_RECENT + i);
        m_Recent[i]->SetListener(this);
        m_RecentGrid->AddChild(m_Recent[i]);
    }
    body->AddChild(m_PinnedHead);
    body->AddChild(grid);
    body->AddChild(m_RecentHead);
    body->AddChild(m_RecentGrid);

    if (!GetUserNameW(szUser, &cch))
        szUser[0] = 0;
    CUserButton *user = new CUserButton(szUser);
    user->SetId(LST_USER);
    user->SetListener(this);
    user->SetAlign(Align::Start, Align::Center);
    power->SetId(LST_POWER);
    power->SetListener(this);
    power->SetAlign(Align::End, Align::Center);
    Panel *footerRow = new Panel();
    footerRow->SetSize(-1, 44);
    footerRow->AddChild(user);
    footerRow->AddChild(power);
    footer->AddChild(footerRow);

    m_Root->AddChild(searchRow);
    m_Root->AddChild(body);
    m_Root->AddChild(footer);
}

void CLiberStart::LoadRecent()
{
    LIBER_RECENT_FILE files[LIBER_START_RECENT];
    SYSTEMTIME now;
    UINT count = LiberStart_LoadRecentFiles(files, _countof(files));

    GetLocalTime(&now);
    for (UINT i = 0; i < LIBER_START_RECENT; i++)
        m_Recent[i]->Set(i < count ? &files[i] : NULL, &now);
}

void CLiberStart::AddSystemApps()
{
    WCHAR path[MAX_PATH];
    LPCWSTR name = NULL;

    for (UINT i = 0; LiberStart_SystemApp(i, path, _countof(path), &name); i++)
    {
        BOOL present = FALSE;

        if (!path[0] || GetFileAttributesW(path) == INVALID_FILE_ATTRIBUTES)
            continue;
        for (SIZE_T j = 0; !present && j < m_Apps.GetCount(); j++)
        {
            present = !lstrcmpiW(m_Apps[j]->szPath, path) ||
                      (SM2GetTargetPath(m_Apps[j]) && !lstrcmpiW(m_Apps[j]->szTargetPath, path));
        }
        if (present)
            continue;
        SM2ITEM *item = new SM2ITEM();
        if (!item)
            continue;
        item->szName = name;
        item->szPath = path;
        m_Tree.Children.Add(item);
        m_Apps.Add(item);
    }
}

UINT CLiberStart::FillDefaultPins()
{
    UINT shown = 0;

    for (UINT rank = 0; shown < LIBER_START_TILES; rank++)
    {
        BOOL more = FALSE;
        for (SIZE_T i = 0; i < m_Apps.GetCount() && shown < LIBER_START_TILES; i++)
        {
            LPCWSTR target = SM2GetTargetPath(m_Apps[i]) ? (LPCWSTR)m_Apps[i]->szTargetPath : (LPCWSTR)m_Apps[i]->szPath;
            UINT appRank = LiberStart_DefaultPinRank(target);
            if (appRank == rank)
                m_Tiles[shown++]->SetItem(m_Apps[i]);
            else if (appRank != UINT_MAX && appRank > rank)
                more = TRUE;
        }
        if (!more)
            break;
    }
    return shown;
}

void CLiberStart::Filter()
{
    UINT shown = 0, matched = 0, pinCount = 0;
    LPCWSTR query = m_Search->Query();
    BOOL searching = query[0] != 0;
    static WCHAR s_Pins[LIBER_START_PIN_CHARS];
    BOOL pinned = !searching && !m_AllApps && LiberStart_LoadPins(s_Pins, _countof(s_Pins), &pinCount);

    if (pinned)
    {
        for (LPCWSTR pin = s_Pins; *pin && shown < LIBER_START_TILES; pin += lstrlenW(pin) + 1)
        {
            for (SIZE_T i = 0; i < m_Apps.GetCount(); i++)
            {
                if (!lstrcmpiW(m_Apps[i]->szPath, pin))
                {
                    m_Tiles[shown++]->SetItem(m_Apps[i]);
                    break;
                }
            }
        }
    }
    if (!pinned && !searching && !m_AllApps)
        shown = FillDefaultPins();
    for (SIZE_T i = 0; !pinned && (searching || m_AllApps) && i < m_Apps.GetCount() && shown < LIBER_START_TILES; i++)
    {
        if (!LiberStart_Matches(m_Apps[i]->szName, query))
            continue;
        if (!searching && m_AllApps && matched++ < m_Offset)
            continue;
        m_Tiles[shown++]->SetItem(m_Apps[i]);
    }
    for (UINT i = shown; i < LIBER_START_TILES; i++)
        m_Tiles[i]->SetItem(NULL);
    m_PinnedHead->SetTitle(searching ? (shown ? L"Best matches" : L"No apps match") : m_AllApps ? L"All apps" : L"Pinned");
    m_PinnedHead->SetAction(searching ? NULL : m_AllApps ? L"Back" : L"All apps");
    m_RecentHead->SetVisible(!searching && !m_AllApps);
    m_RecentGrid->SetVisible(!searching && !m_AllApps);
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
        else if (m_AllApps)
        {
            m_AllApps = FALSE;
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

BOOL CLiberStart::OnWheel(int delta)
{
    UINT offset;

    if (!m_AllApps || m_Search->Query()[0])
        return FALSE;
    offset = LiberStart_ScrollOffset(m_Offset, (UINT)m_Apps.GetCount(), LIBER_START_COLUMNS, LIBER_START_TILES,
                                     delta < 0 ? 1 : -1);
    if (offset == m_Offset)
        return FALSE;
    m_Offset = offset;
    Filter();
    return TRUE;
}

BOOL CLiberStart::Open(const RECT *prcAnchor)
{
    if (!m_Root)
    {
        if (!m_Surface.Create(NULL, L"Liber Start", WS_EX_TOPMOST | WS_EX_TOOLWINDOW))
            return FALSE;
        m_Surface.SetInputShape((float)LIBER_RADIUS_FLYOUT);
        m_Surface.SetElevation(LIBER_ELEVATION_E32, (float)LIBER_RADIUS_FLYOUT);
        Build();
        m_Surface.SetRoot(m_Root);
    }
    if (!m_Loaded)
    {
        SM2BuildTree(&m_Tree);
        m_Apps.SetCount(0);
        SM2FlattenTree(&m_Tree, m_Apps);
        AddSystemApps();
        LiberStart_SortApps(m_Apps.GetData(), m_Apps.GetCount());
        m_Loaded = TRUE;
    }

    m_Search->Clear();
    m_AllApps = FALSE;
    m_Offset = 0;
    m_Surface.Input().SetFocus(NULL);
    LiberShell_BuildTokens(&m_Tokens);
    LiberShell_ApplyFont(&m_Surface);
    m_Surface.SetTokens(&m_Tokens);
    LiberShell_ApplyMaterial(&m_Surface, &m_Tokens, LIBER_MATERIAL_THICK, (float)LIBER_RADIUS_FLYOUT);
    LoadRecent();
    Filter();

    SIZE available = { LIBER_START_WIDTH, LayoutInfinite };
    SIZE size = m_Root->Measure(available);
    UINT dpi = m_Surface.Dpi();
    POINT pt;
    LiberShell_PlaceFlyout(prcAnchor, ScaleToDpi(LIBER_START_WIDTH, dpi), ScaleToDpi(size.cy, dpi), TRUE, &pt);

    m_Surface.SetPosition(pt.x, pt.y, LIBER_START_WIDTH, size.cy);
    LiberShell_SampleBackdrop(&m_Surface, &m_Tokens, FALSE);
    m_Surface.RequestLayout();
    m_Surface.Render(AnimationClock::Now());
    ShowWindow(m_Surface.Window(), SW_SHOW);
    m_Surface.TransitionIn(32.0f, 300);
    SetForegroundWindow(m_Surface.Window());
    return TRUE;
}

UINT CLiberStart::TrackMenu(const LIBER_MENU_ITEM *items, UINT count, POINT screen)
{
    UINT cmd;

    m_Surface.m_InMenu = TRUE;
    cmd = LiberMenu_Track(items, count, screen);
    m_Surface.m_InMenu = FALSE;
    if (IsOpen())
    {
        POINT outside;
        RECT rc;
        if (!cmd && LiberMenu_DismissedOutside(&outside) && GetWindowRect(m_Surface.Window(), &rc) &&
            !PtInRect(&rc, outside))
        {
            Close();
        }
        else
        {
            SetForegroundWindow(m_Surface.Window());
        }
    }
    return cmd;
}

void CLiberStart::ShowPowerMenu(BOOL user)
{
    static const struct { INT Cmd; LPCWSTR Text; LPCWSTR Icon; BOOL User; } Items[] =
    {
        { SM2F_LOCK, L"Lock", L"lock_closed_16_regular", TRUE },
        { SM2F_LOGOFF, L"Sign out", L"person_16_regular", TRUE },
        { SM2F_SLEEP, L"Sleep", L"weather_moon_16_regular", FALSE },
        { SM2F_HIBERNATE, L"Hibernate", L"history_16_regular", FALSE },
        { SM2F_RESTART, L"Restart", L"arrow_counterclockwise_16_regular", FALSE },
        { SM2F_SHUTDOWN, L"Shut down", L"power_20_regular", FALSE },
    };
    LIBER_MENU_ITEM menu[_countof(Items)];
    UINT count = 0;
    POINT pt;

    ZeroMemory(menu, sizeof(menu));
    for (UINT i = 0; i < _countof(Items); i++)
    {
        if (Items[i].User != user || !StartMenu2_IsPowerCommandAllowed(Items[i].Cmd))
            continue;
        menu[count].Id = (UINT)Items[i].Cmd;
        menu[count].Text = Items[i].Text;
        menu[count].Icon = Items[i].Icon;
        count++;
    }
    GetCursorPos(&pt);
    INT cmd = (INT)TrackMenu(menu, count, pt);
    if (cmd > 0)
    {
        Close();
        StartMenu2_DoPowerCommand(NULL, cmd);
    }
}

void CLiberStart::TileMenu(SM2ITEM *item, POINT screen)
{
    static WCHAR s_Pins[LIBER_START_PIN_CHARS];
    UINT pinCount = 0;
    BOOL hasPins = LiberStart_LoadPins(s_Pins, _countof(s_Pins), &pinCount);
    BOOL isPinned;

    if (!hasPins)
    {
        LPWSTR out = s_Pins;
        for (UINT i = 0; i < LIBER_START_TILES && m_Tiles[i]->Item(); i++)
        {
            SIZE_T len = (SIZE_T)m_Tiles[i]->Item()->szPath.GetLength() + 1;
            CopyMemory(out, (LPCWSTR)m_Tiles[i]->Item()->szPath, len * sizeof(WCHAR));
            out += len;
        }
        *out = 0;
    }
    isPinned = LiberStart_IsPinned(s_Pins, item->szPath) && (hasPins || !m_AllApps);
    LIBER_MENU_ITEM menu[] = {
        { isPinned ? (UINT)LSM_UNPIN_START : (UINT)LSM_PIN_START, isPinned ? L"Unpin from Start" : L"Pin to Start",
          L"pin_16_regular", NULL, FALSE, FALSE },
        { LSM_PIN_TASKBAR, L"Pin to taskbar", L"panel_bottom_20_regular", NULL, FALSE,
          TaskbarPin_IsDisabled() || !TaskbarPin_IsPinnable(item->szPath) },
        { 0, NULL, NULL, NULL, TRUE, FALSE },
        { LSM_OPEN_LOCATION, L"Open file location", L"folder_open_16_regular", NULL, FALSE, FALSE },
    };
    switch (TrackMenu(menu, _countof(menu), screen))
    {
        case LSM_PIN_START:
        case LSM_UNPIN_START:
            LiberStart_SetPinned(s_Pins, item->szPath, !isPinned);
            Filter();
            m_Surface.Render(AnimationClock::Now());
            break;
        case LSM_PIN_TASKBAR:
            TaskbarPin_Create(item->szPath);
            break;
        case LSM_OPEN_LOCATION:
        {
            WCHAR szArgs[MAX_PATH + 16];
            Close();
            StringCchPrintfW(szArgs, _countof(szArgs), L"/select,\"%s\"", (LPCWSTR)item->szPath);
            ShellExecuteW(NULL, NULL, L"explorer.exe", szArgs, NULL, SW_SHOWNORMAL);
            break;
        }
    }
}

static void LiberStart_TileMenu(SM2ITEM *item, POINT screen)
{
    if (g_pLiberStart && item)
        g_pLiberStart->TileMenu(item, screen);
}

void CLiberStart::OnControlEvent(Widget *source, ControlEvent event)
{
    Control *control = static_cast<Control *>(source);

    UNREFERENCED_PARAMETER(event);
    switch (control->Id())
    {
        case LST_POWER:
            ShowPowerMenu(FALSE);
            return;
        case LST_ALL_APPS:
            m_AllApps = !m_AllApps;
            m_Offset = 0;
            Filter();
            m_Surface.Render(AnimationClock::Now());
            return;
        case LST_MORE:
            Close();
            SM2OpenCsidlFolder(CSIDL_RECENT);
            return;
        case LST_USER:
            ShowPowerMenu(TRUE);
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
    else if (control->Id() >= LST_RECENT && control->Id() < LST_RECENT + LIBER_START_RECENT)
    {
        LPCWSTR path = m_Recent[control->Id() - LST_RECENT]->Path();
        if (path[0])
        {
            Close();
            ShellExecuteW(NULL, NULL, path, NULL, NULL, SW_SHOWNORMAL);
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
