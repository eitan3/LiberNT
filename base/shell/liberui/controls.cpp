/*
 * PROJECT:     LiberNT Liber UI
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Basic controls: label, icon button, toggle tile and fat slider
 * COPYRIGHT:   Copyright 2026 Eitan Hadar <eitanhadar4@gmail.com>
 */

#include "precomp.h"

namespace LiberUI
{

static void CopyText(LPWSTR target, UINT cch, LPCWSTR source)
{
    lstrcpynW(target, source ? source : L"", (int)cch);
}

Control::Control()
    : m_Listener(NULL), m_Id(0)
{
    SetHitTestable(TRUE);
    SetFocusable(TRUE);
}

void Control::Raise(ControlEvent event)
{
    if (m_Listener)
        m_Listener->OnControlEvent(this, event);
}

Label::Label(LPCWSTR text, LIBER_TYPE style, LIBER_COLOR color, UINT flags)
    : m_Style(style), m_Color(color), m_Flags(flags)
{
    LUI_ASSERT(style >= 0 && style < LIBER_TYPE_COUNT && color >= 0 && color < LIBER_COLOR_COUNT);
    CopyText(m_Text, _countof(m_Text), text);
}

void Label::SetText(LPCWSTR text)
{
    if (!lstrcmpW(m_Text, text ? text : L""))
        return;
    CopyText(m_Text, _countof(m_Text), text);
    Invalidate();
}

SIZE Label::MeasureOverride(SIZE available)
{
    const LIBER_TYPE_STYLE &ramp = LiberTypeRamp[m_Style];
    LONG estimate = (LONG)(lstrlenW(m_Text) * ramp.Size * 55 / 100);
    SIZE size = { estimate < available.cx ? estimate : available.cx, ramp.LineHeight };
    return size;
}

void Label::PaintOverride(PaintContext &context)
{
    context.Painter.Text(m_Text, ToRectF(Bounds()), m_Style, context.Tokens.Color(m_Color), m_Flags | TextEllipsis);
}

IconButton::IconButton(LPCWSTR icon, int size)
{
    LUI_ASSERT(size > 0);
    CopyText(m_Icon, _countof(m_Icon), icon);
    SetSize(size, size);
}

void IconButton::SetIcon(LPCWSTR icon)
{
    CopyText(m_Icon, _countof(m_Icon), icon);
    Invalidate();
}

void IconButton::OnClick()
{
    Raise(ControlClicked);
}

void IconButton::PaintOverride(PaintContext &context)
{
    RectF rc = ToRectF(Bounds());
    const ThemeTokens &tokens = context.Tokens;

    if (HasState(StatePressed))
        context.Painter.FillEllipse(rc, tokens.Color(LIBER_COLOR_FILL_SUBTLE_PRESSED));
    else if (HasState(StateHover))
        context.Painter.FillEllipse(rc, tokens.Color(LIBER_COLOR_FILL_SUBTLE_HOVER));
    float size = 20.0f;
    RectF icon = { rc.X + (rc.Width - size) / 2, rc.Y + (rc.Height - size) / 2, size, size };
    context.Painter.Icon(m_Icon, icon, tokens.Color(IsEnabled() ? LIBER_COLOR_TEXT1 : LIBER_COLOR_TEXT_DISABLED));
}

ToggleTile::ToggleTile(LPCWSTR icon, LPCWSTR label, BOOL toggles)
    : m_Toggles(toggles)
{
    CopyText(m_Icon, _countof(m_Icon), icon);
    CopyText(m_Label, _countof(m_Label), label);
}

void ToggleTile::SetLabel(LPCWSTR label)
{
    CopyText(m_Label, _countof(m_Label), label);
    Invalidate();
}

void ToggleTile::OnClick()
{
    if (m_Toggles)
    {
        SetOn(!IsOn());
        Raise(ControlToggled);
    }
    else
    {
        Raise(ControlClicked);
    }
}

void ToggleTile::PaintOverride(PaintContext &context)
{
    RectF rc = ToRectF(Bounds());
    const ThemeTokens &tokens = context.Tokens;
    BOOL on = IsOn();
    LIBER_COLOR fill = on ? LIBER_COLOR_ACCENT : HasState(StateHover) ? LIBER_COLOR_FILL_TILE_HOVER : LIBER_COLOR_FILL_TILE;

    if (on && HasState(StateHover))
        fill = LIBER_COLOR_ACCENT_HOVER;
    if (on && HasState(StatePressed))
        fill = LIBER_COLOR_ACCENT_PRESSED;
    context.Painter.FillRoundRect(rc, (float)LIBER_RADIUS_TILE_ITEM, tokens.Color(fill));

    LIBER_COLOR ink = on ? LIBER_COLOR_ON_ACCENT : LIBER_COLOR_TEXT1;
    RectF icon = { rc.X + 12.0f, rc.Y + (rc.Height - 20.0f) / 2, 20.0f, 20.0f };
    RectF text = { rc.X + 40.0f, rc.Y, rc.Width - 48.0f, rc.Height };
    context.Painter.Icon(m_Icon, icon, tokens.Color(ink));
    context.Painter.Text(m_Label, text, LIBER_TYPE_BODY_STRONG, tokens.Color(ink), TextMiddle | TextEllipsis);
}

Slider::Slider(LPCWSTR icon, int maximum)
    : m_Value(0), m_Maximum(maximum > 0 ? maximum : 100)
{
    LUI_ASSERT(maximum > 0);
    CopyText(m_Icon, _countof(m_Icon), icon);
    SetSize(-1, LIBER_CONTROL_LG);
}

void Slider::SetIcon(LPCWSTR icon)
{
    CopyText(m_Icon, _countof(m_Icon), icon);
    Invalidate();
}

void Slider::SetValue(int value)
{
    int clamped = value < 0 ? 0 : value > m_Maximum ? m_Maximum : value;
    if (clamped == m_Value)
        return;
    m_Value = clamped;
    Invalidate();
}

void Slider::Change(int value)
{
    int before = m_Value;
    SetValue(value);
    if (m_Value != before)
        Raise(ControlValueChanged);
}

int Slider::ValueAt(LONG x) const
{
    const RECT &rc = Bounds();
    LONG inset = (rc.bottom - rc.top) / 2;
    LONG width = (rc.right - rc.left) - 2 * inset;

    if (width <= 0)
        return 0;
    LONG offset = x - rc.left - inset;
    if (offset <= 0)
        return 0;
    if (offset >= width)
        return m_Maximum;
    return (int)((offset * m_Maximum + width / 2) / width);
}

void Slider::OnPointerDown(POINT pt)
{
    Change(ValueAt(pt.x));
}

void Slider::OnPointerMove(POINT pt)
{
    if (HasState(StatePressed))
        Change(ValueAt(pt.x));
}

BOOL Slider::OnKeyDown(UINT vk)
{
    int step = m_Maximum / 100 > 0 ? m_Maximum / 100 : 1;

    switch (vk)
    {
        case VK_LEFT:
        case VK_DOWN:
            Change(m_Value - step);
            return TRUE;
        case VK_RIGHT:
        case VK_UP:
            Change(m_Value + step);
            return TRUE;
        case VK_PRIOR:
            Change(m_Value + step * 10);
            return TRUE;
        case VK_NEXT:
            Change(m_Value - step * 10);
            return TRUE;
        case VK_HOME:
            Change(0);
            return TRUE;
        case VK_END:
            Change(m_Maximum);
            return TRUE;
    }
    return FALSE;
}

void Slider::PaintOverride(PaintContext &context)
{
    RectF rc = ToRectF(Bounds());
    const ThemeTokens &tokens = context.Tokens;
    float radius = rc.Height / 2;
    float fillWidth = rc.Height + (rc.Width - rc.Height) * (float)m_Value / (float)m_Maximum;

    context.Painter.FillRoundRect(rc, radius, tokens.Color(LIBER_COLOR_FILL_TRACK));
    RectF fill = { rc.X, rc.Y, fillWidth, rc.Height };
    context.Painter.FillRoundRect(fill, radius, tokens.Color(LIBER_COLOR_ACCENT));
    RectF icon = { rc.X + (rc.Height - 20.0f) / 2, rc.Y + (rc.Height - 20.0f) / 2, 20.0f, 20.0f };
    context.Painter.Icon(m_Icon, icon, tokens.Color(LIBER_COLOR_ON_ACCENT));
    if (HasState(StateFocused))
        context.Painter.StrokeRoundRect(rc, radius, tokens.Color(LIBER_COLOR_ACCENT_RING), 2.0f);
}

}
