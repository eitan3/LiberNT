/*
 * PROJECT:     LiberNT Explorer
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Liber glass context menus
 * COPYRIGHT:   Copyright 2026 Eitan Hadar <eitanhadar4@gmail.com>
 */

#include "precomp.h"

#include "libershell.h"

using namespace LiberUI;

#define LIBER_MENU_PAD        4
#define LIBER_MENU_SEPARATOR  9
#define LIBER_MENU_MAX_ITEMS  24

class CLiberMenuPanel : public StackPanel
{
public:
    CLiberMenuPanel() : StackPanel(Orientation::Vertical, 0)
    {
        Thickness pad = { LIBER_MENU_PAD, LIBER_MENU_PAD, LIBER_MENU_PAD, LIBER_MENU_PAD };
        SetPadding(pad);
    }

protected:
    void PaintOverride(PaintContext &context)
    {
        LiberShell_PaintGlass(context, ToRectF(Bounds()), (float)LIBER_RADIUS_MENU, LIBER_MATERIAL_THICK);
    }
};

class CLiberMenuItem : public Control
{
public:
    CLiberMenuItem(const LIBER_MENU_ITEM &item) : m_Item(item)
    {
        SetId(item.Id);
        SetSize(-1, item.Separator ? LIBER_MENU_SEPARATOR : LIBER_MENU_ITEM_HEIGHT);
        SetEnabled(!item.Disabled && !item.Separator);
        SetHitTestable(!item.Separator);
        SetFocusable(!item.Separator);
    }

    void OnClick() { Raise(ControlClicked); }

protected:
    void PaintOverride(PaintContext &context)
    {
        RectF rc = ToRectF(Bounds());
        const ThemeTokens &tokens = context.Tokens;

        if (m_Item.Separator)
        {
            RectF line = { rc.X - LIBER_MENU_PAD, rc.Y + (rc.Height - 1.0f) / 2, rc.Width + 2 * LIBER_MENU_PAD, 1.0f };
            context.Painter.FillRect(line, tokens.Color(LIBER_COLOR_STROKE_DIVIDER));
            return;
        }
        if (IsEnabled() && (HasState(StateHover) || HasState(StatePressed) || HasState(StateFocused)))
            context.Painter.FillRoundRect(rc, (float)LIBER_RADIUS_CONTROL,
                                          tokens.Color(HasState(StatePressed) ? LIBER_COLOR_FILL_SUBTLE_PRESSED
                                                                              : LIBER_COLOR_FILL_SUBTLE_HOVER));
        LIBER_COLOR text = IsEnabled() ? LIBER_COLOR_TEXT1 : LIBER_COLOR_TEXT_DISABLED;
        float glyph = m_Item.Icon && wcsstr(m_Item.Icon, L"_20_") ? 20.0f : 16.0f;
        RectF icon = { rc.X + 10.0f + (16.0f - glyph) / 2, rc.Y + (rc.Height - glyph) / 2, glyph, glyph };
        if (m_Item.Icon)
            context.Painter.Icon(m_Item.Icon, icon, tokens.Color(IsEnabled() ? LIBER_COLOR_TEXT2 : text));
        float hintWidth = m_Item.Hint ? 72.0f : 0.0f;
        RectF label = { rc.X + 38.0f, rc.Y, rc.Width - 48.0f - hintWidth, rc.Height };
        context.Painter.Text(m_Item.Text, label, LIBER_TYPE_BODY, tokens.Color(text), TextMiddle | TextEllipsis);
        if (m_Item.Hint)
        {
            RectF hint = { rc.X + rc.Width - 10.0f - hintWidth, rc.Y, hintWidth, rc.Height };
            context.Painter.Text(m_Item.Hint, hint, LIBER_TYPE_CAPTION, tokens.Color(LIBER_COLOR_TEXT3),
                                 TextRight | TextMiddle);
        }
    }

private:
    LIBER_MENU_ITEM m_Item;
};

class CLiberMenuSurface : public Surface
{
public:
    CLiberMenuSurface() : m_Done(FALSE) {}
    BOOL m_Done;

protected:
    LRESULT HandleMessage(UINT msg, WPARAM wParam, LPARAM lParam)
    {
        if ((msg == WM_ACTIVATE && LOWORD(wParam) == WA_INACTIVE) || (msg == WM_KEYDOWN && wParam == VK_ESCAPE))
        {
            m_Done = TRUE;
            return 0;
        }
        return Surface::HandleMessage(msg, wParam, lParam);
    }
};

class CLiberMenu final : public IControlListener
{
public:
    CLiberMenu() : m_Result(0) {}

    void OnControlEvent(Widget *source, ControlEvent event)
    {
        UNREFERENCED_PARAMETER(event);
        m_Result = static_cast<Control *>(source)->Id();
        m_Surface.m_Done = TRUE;
    }

    UINT Track(const LIBER_MENU_ITEM *items, UINT count, POINT anchor);

private:
    CLiberMenuSurface m_Surface;
    ThemeTokens m_Tokens;
    UINT m_Result;
};

UINT CLiberMenu::Track(const LIBER_MENU_ITEM *items, UINT count, POINT anchor)
{
    CLiberMenuPanel *root;
    MSG msg = { 0 };

    if (!items || !count || !m_Surface.Create(NULL, L"Liber Menu", WS_EX_TOPMOST | WS_EX_TOOLWINDOW))
        return 0;
    root = new CLiberMenuPanel();
    for (UINT i = 0; i < count && i < LIBER_MENU_MAX_ITEMS; i++)
    {
        CLiberMenuItem *item = new CLiberMenuItem(items[i]);
        item->SetListener(this);
        root->AddChild(item);
    }
    m_Surface.SetInputShape((float)LIBER_RADIUS_MENU);
    m_Surface.SetElevation(LIBER_ELEVATION_E32, (float)LIBER_RADIUS_MENU);
    LiberShell_BuildTokens(&m_Tokens);
    LiberShell_ApplyFont(&m_Surface);
    m_Surface.SetTokens(&m_Tokens);
    LiberShell_ApplyMaterial(&m_Surface, &m_Tokens, LIBER_MATERIAL_THICK, (float)LIBER_RADIUS_MENU);
    m_Surface.SetRoot(root);

    SIZE available = { LIBER_MENU_MIN_WIDTH, LayoutInfinite };
    SIZE size = root->Measure(available);
    UINT dpi = m_Surface.Dpi();
    MONITORINFO mi = { sizeof(mi) };
    GetMonitorInfoW(MonitorFromPoint(anchor, MONITOR_DEFAULTTONEAREST), &mi);
    POINT pt;
    LiberMenu_Origin(&mi.rcWork, anchor, ScaleToDpi(LIBER_MENU_MIN_WIDTH, dpi), ScaleToDpi(size.cy, dpi), &pt);
    m_Surface.SetPosition(pt.x, pt.y, LIBER_MENU_MIN_WIDTH, size.cy);
    m_Surface.Render(AnimationClock::Now());
    ShowWindow(m_Surface.Window(), SW_SHOW);
    m_Surface.TransitionIn(4.0f, LIBER_DUR_NORMAL);
    SetForegroundWindow(m_Surface.Window());

    while (!m_Surface.m_Done && IsWindow(m_Surface.Window()) && GetMessageW(&msg, NULL, 0, 0) > 0)
    {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    if (msg.message == WM_QUIT)
        PostQuitMessage((int)msg.wParam);
    m_Surface.SetRoot(NULL);
    m_Surface.Destroy();
    delete root;
    return m_Result;
}

UINT LiberMenu_Track(const LIBER_MENU_ITEM *pItems, UINT cItems, POINT anchor)
{
    CLiberMenu menu;
    return menu.Track(pItems, cItems, anchor);
}
