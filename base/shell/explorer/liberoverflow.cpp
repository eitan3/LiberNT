/*
 * PROJECT:     LiberNT Explorer
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Liber tray overflow menu of background apps
 * COPYRIGHT:   Copyright 2026 Eitan Hadar <eitanhadar4@gmail.com>
 */

#include "precomp.h"

#include "libershell.h"

using namespace LiberUI;

#define LIBER_OVERFLOW_WIDTH 254
#define LIBER_OVERFLOW_PAD       4
#define LIBER_OVERFLOW_HEADER_H  24
#define LIBER_OVERFLOW_ITEMS     16

class CMenuPanel : public StackPanel
{
public:
    CMenuPanel() : StackPanel(Orientation::Vertical, 0)
    {
        Thickness pad = { LIBER_OVERFLOW_PAD, LIBER_OVERFLOW_PAD, LIBER_OVERFLOW_PAD, LIBER_OVERFLOW_PAD };
        SetPadding(pad);
    }

protected:
    void PaintOverride(PaintContext &context)
    {
        RectF rc = ToRectF(Bounds());
        GlassTint tint = context.Tokens.Material(LIBER_MATERIAL_THICK, 0.5);
        RectF edge = { rc.X + 0.5f, rc.Y + 0.5f, rc.Width - 1.0f, rc.Height - 1.0f };

        context.Painter.FillRoundRect(rc, (float)LIBER_RADIUS_MENU, OklchToArgb(tint.Color, 1.0));
        context.Painter.StrokeRoundRect(edge, LIBER_RADIUS_MENU - 0.5f, context.Tokens.Color(LIBER_COLOR_GLASS_EDGE),
                                        1.0f);
    }
};

class CMenuItem : public Control
{
public:
    CMenuItem(UINT id) : m_Icon(NULL)
    {
        ZeroMemory(&m_Item, sizeof(m_Item));
        SetId(id);
        SetSize(-1, LIBER_MENU_ITEM_HEIGHT);
    }

    ~CMenuItem()
    {
        Reset();
    }

    void Set(const TRAYICONITEM *item, HIMAGELIST himl)
    {
        Reset();
        if (item)
        {
            m_Item = *item;
            if (himl && item->iImage >= 0)
                m_Icon = ImageList_GetIcon(himl, item->iImage, ILD_TRANSPARENT);
        }
        SetVisible(item != NULL);
        Invalidate();
    }

    const TRAYICONITEM &Item() const { return m_Item; }
    void OnClick() { Raise(ControlClicked); }

protected:
    void PaintOverride(PaintContext &context)
    {
        RectF rc = ToRectF(Bounds());
        const ThemeTokens &tokens = context.Tokens;

        if (HasState(StateHover) || HasState(StatePressed) || HasState(StateFocused))
            context.Painter.FillRoundRect(rc, (float)LIBER_RADIUS_CONTROL,
                                          tokens.Color(HasState(StatePressed) ? LIBER_COLOR_FILL_SUBTLE_PRESSED
                                                                              : LIBER_COLOR_FILL_SUBTLE_HOVER));
        RectF icon = { rc.X + 10.0f, rc.Y + (rc.Height - 16.0f) / 2, 16.0f, 16.0f };
        if (m_Icon)
            context.Painter.Image(m_Icon, icon);
        RectF label = { icon.X + 16.0f + 12.0f, rc.Y, rc.Width - 48.0f, rc.Height };
        context.Painter.Text(m_Item.szTip[0] ? m_Item.szTip : L"Notification icon", label, LIBER_TYPE_BODY,
                             tokens.Color(LIBER_COLOR_TEXT1), TextMiddle | TextEllipsis);
    }

private:
    void Reset()
    {
        if (m_Icon)
            DestroyIcon(m_Icon);
        m_Icon = NULL;
    }

    TRAYICONITEM m_Item;
    HICON m_Icon;
};

class COverflowSurface : public Surface
{
public:
    COverflowSurface() : m_HiddenAt(0) {}
    ULONGLONG m_HiddenAt;
    void Hide()
    {
        if (Window() && IsWindowVisible(Window()) && !IsClosing())
        {
            TransitionOut(8.0f, 150);
            m_HiddenAt = GetTickCount64();
        }
    }

protected:
    LRESULT HandleMessage(UINT msg, WPARAM wParam, LPARAM lParam)
    {
        if ((msg == WM_ACTIVATE && LOWORD(wParam) == WA_INACTIVE) || (msg == WM_KEYDOWN && wParam == VK_ESCAPE))
        {
            Hide();
            return 0;
        }
        return Surface::HandleMessage(msg, wParam, lParam);
    }
};

class CLiberOverflow final : public IControlListener
{
public:
    CLiberOverflow() : m_Root(NULL), m_Empty(NULL), m_Pager(NULL)
    {
        ZeroMemory(m_Items, sizeof(m_Items));
        ZeroMemory(&m_List, sizeof(m_List));
    }

    ~CLiberOverflow()
    {
        m_Surface.SetRoot(NULL);
        m_Surface.Destroy();
        delete m_Root;
    }

    BOOL Open(const RECT *prcAnchor);
    void Close() { m_Surface.Hide(); }
    BOOL IsOpen() const { return m_Surface.Window() && IsWindowVisible(m_Surface.Window()) && !m_Surface.IsClosing(); }
    BOOL JustClosed() const { return m_Surface.m_HiddenAt && GetTickCount64() - m_Surface.m_HiddenAt < 250; }
    void OnControlEvent(Widget *source, ControlEvent event);

private:
    void Build();

    COverflowSurface m_Surface;
    ThemeTokens m_Tokens;
    CMenuPanel *m_Root;
    Label *m_Empty;
    CMenuItem *m_Items[LIBER_OVERFLOW_ITEMS];
    TRAYICONLIST m_List;
    HWND m_Pager;
};

static CLiberOverflow *g_pLiberOverflow;

static HWND FindPager(VOID)
{
    HWND hwnd = FindWindowW(L"Shell_TrayWnd", NULL);
    hwnd = hwnd ? FindWindowExW(hwnd, NULL, L"TrayNotifyWnd", NULL) : NULL;
    return hwnd ? FindWindowExW(hwnd, NULL, L"SysPager", NULL) : NULL;
}

void CLiberOverflow::Build()
{
    Label *header = new Label(L"Running in background", LIBER_TYPE_CAPTION, LIBER_COLOR_TEXT3, TextLeft | TextMiddle);
    Thickness headerMargin = { 10, 2, 10, 0 };

    m_Root = new CMenuPanel();
    header->SetSize(-1, LIBER_OVERFLOW_HEADER_H - 2);
    header->SetMargin(headerMargin);
    m_Root->AddChild(header);
    m_Empty = new Label(L"No background apps", LIBER_TYPE_BODY, LIBER_COLOR_TEXT2, TextLeft | TextMiddle);
    m_Empty->SetSize(-1, LIBER_MENU_ITEM_HEIGHT);
    Thickness emptyMargin = { 10, 0, 10, 0 };
    m_Empty->SetMargin(emptyMargin);
    m_Root->AddChild(m_Empty);
    for (UINT i = 0; i < LIBER_OVERFLOW_ITEMS; i++)
    {
        m_Items[i] = new CMenuItem(i);
        m_Items[i]->SetListener(this);
        m_Root->AddChild(m_Items[i]);
    }
}

BOOL CLiberOverflow::Open(const RECT *prcAnchor)
{
    UINT shown = 0;

    if (!m_Root)
    {
        if (!m_Surface.Create(NULL, L"Liber Background Apps", WS_EX_TOPMOST | WS_EX_TOOLWINDOW))
            return FALSE;
        m_Surface.SetInputShape((float)LIBER_RADIUS_MENU);
        m_Surface.SetElevation(LIBER_ELEVATION_E32, (float)LIBER_RADIUS_MENU);
        Build();
        m_Surface.SetRoot(m_Root);
    }

    ZeroMemory(&m_List, sizeof(m_List));
    m_Pager = FindPager();
    if (m_Pager)
        SendMessageW(m_Pager, TNWM_GETTRAYICONS, TRUE, (LPARAM)&m_List);
    for (UINT i = 0; i < m_List.cItems && i < _countof(m_List.Items) && shown < LIBER_OVERFLOW_ITEMS; i++)
        m_Items[shown++]->Set(&m_List.Items[i], m_List.himl);
    for (UINT i = shown; i < LIBER_OVERFLOW_ITEMS; i++)
        m_Items[i]->Set(NULL, NULL);
    m_Empty->SetVisible(shown == 0);

    LiberShell_BuildTokens(&m_Tokens);
    LiberShell_ApplyFont(&m_Surface);
    m_Surface.SetTokens(&m_Tokens);

    SIZE available = { LIBER_OVERFLOW_WIDTH, LayoutInfinite };
    SIZE size = m_Root->Measure(available);
    UINT dpi = m_Surface.Dpi();
    POINT pt;
    LiberShell_PlaceFlyout(prcAnchor, ScaleToDpi(LIBER_OVERFLOW_WIDTH, dpi), ScaleToDpi(size.cy, dpi), FALSE, &pt);
    m_Surface.SetPosition(pt.x, pt.y, LIBER_OVERFLOW_WIDTH, size.cy);
    m_Surface.RequestLayout();
    m_Surface.Render(AnimationClock::Now());
    ShowWindow(m_Surface.Window(), SW_SHOW);
    m_Surface.TransitionIn(8.0f, 220);
    SetForegroundWindow(m_Surface.Window());
    return TRUE;
}

void CLiberOverflow::OnControlEvent(Widget *source, ControlEvent event)
{
    UINT id = static_cast<Control *>(source)->Id();
    static const UINT Messages[] = { WM_LBUTTONDOWN, WM_LBUTTONUP, NIN_SELECT };

    UNREFERENCED_PARAMETER(event);
    if (id >= LIBER_OVERFLOW_ITEMS || !m_Pager)
        return;
    TRAYICONITEM item = m_Items[id]->Item();
    Close();
    for (UINT i = 0; i < _countof(Messages); i++)
    {
        TRAYICONEVENT tie = { item.hWnd, item.uID, Messages[i] };
        SendMessageW(m_Pager, TNWM_TRAYICONEVENT, 0, (LPARAM)&tie);
    }
}

VOID LiberOverflow_Toggle(IN const RECT *prcAnchor)
{
    if (!g_pLiberOverflow)
        g_pLiberOverflow = new CLiberOverflow();
    if (!g_pLiberOverflow)
        return;
    if (g_pLiberOverflow->IsOpen())
        g_pLiberOverflow->Close();
    else if (!g_pLiberOverflow->JustClosed())
        g_pLiberOverflow->Open(prcAnchor);
}

VOID LiberOverflow_Destroy(VOID)
{
    delete g_pLiberOverflow;
    g_pLiberOverflow = NULL;
}
