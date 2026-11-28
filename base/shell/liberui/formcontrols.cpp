/*
 * PROJECT:     LiberNT Liber UI
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Buttons, switches, check boxes, radio buttons, text fields, segmented controls, tabs,
 *              navigation items, info bars, dialogs and tooltips
 * COPYRIGHT:   Copyright 2026 Eitan Hadar <eitanhadar4@gmail.com>
 */

#include "precomp.h"

namespace LiberUI
{

static void CopyLabel(LPWSTR target, SIZE_T cch, LPCWSTR source)
{
    LUI_ASSERT(target && cch);
    lstrcpynW(target, source ? source : L"", (int)cch);
}

static Argb ControlInk(const ThemeTokens &tokens, BOOL enabled)
{
    return tokens.Color(enabled ? LIBER_COLOR_TEXT1 : LIBER_COLOR_TEXT_DISABLED);
}

static LONG TextWidth(LPCWSTR text, LIBER_TYPE style)
{
    LONG width = MeasureText(text, style, 0.0f).cx;
    if (width <= 0 && text)
        width = (LONG)lstrlenW(text) * LiberTypeRamp[style].Size * 55 / 100;
    return width;
}

static RectF Pressed(const RectF &rc, BOOL pressed)
{
    if (!pressed)
        return rc;
    RectF scaled = { rc.X + rc.Width * 0.01f, rc.Y + rc.Height * 0.01f, rc.Width * 0.98f, rc.Height * 0.98f };
    return scaled;
}

Button::Button(LPCWSTR text, LPCWSTR icon, ButtonKind kind, int height, BOOL capsule)
    : m_Kind(kind), m_Capsule(capsule), m_Height(height)
{
    LUI_ASSERT(height == LIBER_CONTROL_SM || height == LIBER_CONTROL_MD || height == LIBER_CONTROL_LG);
    CopyLabel(m_Text, _countof(m_Text), text);
    CopyLabel(m_Icon, _countof(m_Icon), icon);
    SetSize(-1, height);
    SetFocusable(TRUE);
}

void Button::SetText(LPCWSTR text)
{
    CopyLabel(m_Text, _countof(m_Text), text);
    InvalidateLayout();
    Invalidate();
}

void Button::OnClick()
{
    Raise(ControlClicked);
}

SIZE Button::MeasureOverride(SIZE available)
{
    LONG pad = m_Capsule ? 16 : 12;
    LONG width = 2 * pad;

    if (m_Icon[0])
        width += 20 + (m_Text[0] ? 8 : 0);
    if (m_Text[0])
        width += TextWidth(m_Text, LIBER_TYPE_BODY) + 1;
    if (!m_Text[0] && m_Icon[0])
        width = m_Height;
    SIZE size = { min(available.cx, width), m_Height };
    return size;
}

void Button::PaintOverride(PaintContext &context)
{
    const ThemeTokens &tokens = context.Tokens;
    BOOL enabled = IsEnabled(), hover = HasState(StateHover), down = HasState(StatePressed);
    RectF rc = Pressed(ToRectF(Bounds()), down && enabled);
    float radius = m_Capsule || (!m_Text[0] && m_Icon[0]) ? rc.Height / 2 : (float)LIBER_RADIUS_CONTROL;
    LIBER_COLOR fill = LIBER_COLOR_FILL_CONTROL, ink = LIBER_COLOR_TEXT1;
    BOOL stroke = FALSE, filled = TRUE;

    switch (m_Kind)
    {
        case ButtonKind::Accent:
            fill = !enabled ? LIBER_COLOR_FILL_CONTROL_DISABLED
                   : down   ? LIBER_COLOR_ACCENT_PRESSED
                   : hover  ? LIBER_COLOR_ACCENT_HOVER
                            : LIBER_COLOR_ACCENT;
            ink = enabled ? LIBER_COLOR_ON_ACCENT : LIBER_COLOR_TEXT_DISABLED;
            break;
        case ButtonKind::Danger:
            fill = enabled ? LIBER_COLOR_DANGER_FILL : LIBER_COLOR_FILL_CONTROL_DISABLED;
            ink = enabled ? LIBER_COLOR_ON_ACCENT : LIBER_COLOR_TEXT_DISABLED;
            break;
        case ButtonKind::Subtle:
            filled = enabled && (hover || down);
            fill = down ? LIBER_COLOR_FILL_SUBTLE_PRESSED : LIBER_COLOR_FILL_SUBTLE_HOVER;
            ink = enabled ? LIBER_COLOR_TEXT1 : LIBER_COLOR_TEXT_DISABLED;
            break;
        case ButtonKind::Standard:
        default:
            fill = !enabled ? LIBER_COLOR_FILL_CONTROL_DISABLED
                   : down   ? LIBER_COLOR_FILL_CONTROL_PRESSED
                   : hover  ? LIBER_COLOR_FILL_CONTROL_HOVER
                            : LIBER_COLOR_FILL_CONTROL;
            ink = enabled ? LIBER_COLOR_TEXT1 : LIBER_COLOR_TEXT_DISABLED;
            stroke = TRUE;
            break;
    }
    if (filled)
        context.Painter.FillRoundRect(rc, radius, tokens.Color(fill));
    if (stroke)
        context.Painter.StrokeRoundRect(rc, radius, tokens.Color(LIBER_COLOR_STROKE_CONTROL), 1.0f);
    if (HasState(StateFocused))
    {
        RectF ring = { rc.X - 2.0f, rc.Y - 2.0f, rc.Width + 4.0f, rc.Height + 4.0f };
        context.Painter.StrokeRoundRect(ring, radius + 2.0f, tokens.Color(LIBER_COLOR_ACCENT_RING), 2.0f);
    }

    float content = (m_Icon[0] ? 20.0f : 0.0f) + (m_Icon[0] && m_Text[0] ? 8.0f : 0.0f) +
                    (m_Text[0] ? (float)TextWidth(m_Text, LIBER_TYPE_BODY) : 0.0f);
    float x = rc.X + (rc.Width - content) / 2;
    if (m_Icon[0])
    {
        RectF icon = { x, rc.Y + (rc.Height - 20.0f) / 2, 20.0f, 20.0f };
        context.Painter.Icon(m_Icon, icon, tokens.Color(ink));
        x += 28.0f;
    }
    if (m_Text[0])
    {
        RectF text = { x, rc.Y, rc.X + rc.Width - x, rc.Height };
        context.Painter.Text(m_Text, text, LIBER_TYPE_BODY, tokens.Color(ink), TextMiddle | TextEllipsis);
    }
}

Switch::Switch(LPCWSTR label)
{
    CopyLabel(m_Label, _countof(m_Label), label);
    SetSize(-1, LIBER_CONTROL_MD);
    SetFocusable(TRUE);
    m_Knob.Set(0.0);
}

void Switch::SetOn(BOOL on)
{
    if (!!IsOn() == !!on)
        return;
    SetState(StateChecked, on);
    m_Knob.Start(m_Knob.Value(AnimationClock::Now()), on ? 1.0 : 0.0, LIBER_DUR_SPRING, Easing::Spring,
                 AnimationClock::Now());
    Animate(LIBER_DUR_SPRING);
    Invalidate();
}

void Switch::OnClick()
{
    SetOn(!IsOn());
    Raise(ControlToggled);
}

BOOL Switch::OnKeyDown(UINT vk)
{
    if (vk != VK_SPACE)
        return FALSE;
    OnClick();
    return TRUE;
}

double Switch::KnobPosition(ULONGLONG now) const
{
    return m_Knob.Value(now);
}

SIZE Switch::MeasureOverride(SIZE available)
{
    LONG width = LIBER_SWITCH_WIDTH + (m_Label[0] ? 12 + TextWidth(m_Label, LIBER_TYPE_BODY) + 1 : 0);
    SIZE size = { min(available.cx, width), LIBER_CONTROL_MD };
    return size;
}

void Switch::PaintOverride(PaintContext &context)
{
    const ThemeTokens &tokens = context.Tokens;
    RectF rc = ToRectF(Bounds());
    RectF track = { rc.X, rc.Y + (rc.Height - LIBER_SWITCH_HEIGHT) / 2, (float)LIBER_SWITCH_WIDTH,
                    (float)LIBER_SWITCH_HEIGHT };
    float t = (float)KnobPosition(context.Now), r = track.Height / 2;
    BOOL enabled = IsEnabled();

    if (IsOn())
    {
        context.Painter.FillRoundRect(track, r, tokens.Color(!enabled ? LIBER_COLOR_FILL_CONTROL_DISABLED
                                                             : HasState(StateHover) ? LIBER_COLOR_ACCENT_HOVER
                                                                                    : LIBER_COLOR_ACCENT));
    }
    else
    {
        context.Painter.FillRoundRect(track, r, tokens.Color(HasState(StateHover) ? LIBER_COLOR_FILL_CONTROL_HOVER
                                                                                  : LIBER_COLOR_FILL_CONTROL_ALT));
        context.Painter.StrokeRoundRect(track, r, tokens.Color(LIBER_COLOR_STROKE_CONTROL_STRONG), 1.0f);
    }
    float dot = 10.0f + (track.Height - 6.0f - 10.0f) * (t < 0.0f ? 0.0f : t > 1.0f ? 1.0f : t);
    float cx = track.X + r + (track.Width - 2 * r) * t;
    RectF knob = { cx - dot / 2, track.Y + (track.Height - dot) / 2, dot, dot };
    context.Painter.FillEllipse(knob, tokens.Color(IsOn() ? LIBER_COLOR_ON_ACCENT : LIBER_COLOR_TEXT2));
    if (HasState(StateFocused))
    {
        RectF ring = { track.X - 2.0f, track.Y - 2.0f, track.Width + 4.0f, track.Height + 4.0f };
        context.Painter.StrokeRoundRect(ring, r + 2.0f, tokens.Color(LIBER_COLOR_ACCENT_RING), 2.0f);
    }
    if (m_Label[0])
    {
        RectF text = { track.X + track.Width + 12.0f, rc.Y, rc.Width - track.Width - 12.0f, rc.Height };
        context.Painter.Text(m_Label, text, LIBER_TYPE_BODY, ControlInk(tokens, enabled), TextMiddle | TextEllipsis);
    }
}

CheckBox::CheckBox(LPCWSTR label) : m_Indeterminate(FALSE)
{
    CopyLabel(m_Label, _countof(m_Label), label);
    SetSize(-1, LIBER_CONTROL_MD);
    SetFocusable(TRUE);
}

void CheckBox::SetChecked(BOOL checked)
{
    m_Indeterminate = FALSE;
    SetState(StateChecked, checked);
    Invalidate();
}

void CheckBox::SetIndeterminate()
{
    m_Indeterminate = TRUE;
    SetState(StateChecked, FALSE);
    Invalidate();
}

void CheckBox::OnClick()
{
    SetChecked(m_Indeterminate ? TRUE : !IsChecked());
    Raise(ControlToggled);
}

BOOL CheckBox::OnKeyDown(UINT vk)
{
    if (vk != VK_SPACE)
        return FALSE;
    OnClick();
    return TRUE;
}

SIZE CheckBox::MeasureOverride(SIZE available)
{
    LONG width = 20 + (m_Label[0] ? 8 + TextWidth(m_Label, LIBER_TYPE_BODY) + 1 : 0);
    SIZE size = { min(available.cx, width), LIBER_CONTROL_MD };
    return size;
}

void CheckBox::PaintOverride(PaintContext &context)
{
    const ThemeTokens &tokens = context.Tokens;
    RectF rc = ToRectF(Bounds());
    RectF box = { rc.X, rc.Y + (rc.Height - 20.0f) / 2, 20.0f, 20.0f };
    BOOL enabled = IsEnabled(), marked = IsChecked() || m_Indeterminate;

    if (marked)
    {
        context.Painter.FillEllipse(box, tokens.Color(!enabled ? LIBER_COLOR_FILL_CONTROL_DISABLED
                                                     : HasState(StateHover) ? LIBER_COLOR_ACCENT_HOVER
                                                                            : LIBER_COLOR_ACCENT));
        if (m_Indeterminate)
        {
            RectF dash = { box.X + 4.0f, box.Y + 4.0f, 12.0f, 12.0f };
            context.Painter.Icon(L"subtract_12_regular", dash, tokens.Color(LIBER_COLOR_ON_ACCENT));
        }
        else
        {
            PointF a = { box.X + 5.5f, box.Y + 10.5f }, b = { box.X + 9.0f, box.Y + 14.0f };
            PointF c = { box.X + 23.0f, box.Y + 0.0f };
            PointF edge = { box.X + 15.2f, box.Y + 7.8f };
            context.Painter.Line(a, b, tokens.Color(LIBER_COLOR_ON_ACCENT), 2.0f);
            context.Painter.Line(b, edge, tokens.Color(LIBER_COLOR_ON_ACCENT), 2.0f);
            context.Painter.Line(edge, c, tokens.Color(LIBER_COLOR_ACCENT), 2.0f);
        }
    }
    else
    {
        context.Painter.FillEllipse(box, tokens.Color(HasState(StateHover) ? LIBER_COLOR_FILL_CONTROL_HOVER
                                                                          : LIBER_COLOR_FILL_CONTROL_ALT));
        RectF edge = { box.X + 0.5f, box.Y + 0.5f, box.Width - 1.0f, box.Height - 1.0f };
        context.Painter.StrokeRoundRect(edge, edge.Width / 2, tokens.Color(LIBER_COLOR_STROKE_CONTROL_STRONG), 1.0f);
    }
    if (HasState(StateFocused))
    {
        RectF ring = { box.X - 2.0f, box.Y - 2.0f, box.Width + 4.0f, box.Height + 4.0f };
        context.Painter.StrokeRoundRect(ring, ring.Width / 2, tokens.Color(LIBER_COLOR_ACCENT_RING), 2.0f);
    }
    if (m_Label[0])
    {
        RectF text = { box.X + 28.0f, rc.Y, rc.Width - 28.0f, rc.Height };
        context.Painter.Text(m_Label, text, LIBER_TYPE_BODY, ControlInk(tokens, enabled), TextMiddle | TextEllipsis);
    }
}

RadioButton::RadioButton(LPCWSTR label, UINT group) : m_Group(group)
{
    CopyLabel(m_Label, _countof(m_Label), label);
    SetSize(-1, LIBER_CONTROL_MD);
    SetFocusable(TRUE);
}

void RadioButton::SetChecked(BOOL checked)
{
    if (checked && Parent())
    {
        for (Widget *sibling = Parent()->FirstChild(); sibling; sibling = sibling->Next())
        {
            if (sibling != this)
                sibling->OnSiblingSelected(this, m_Group);
        }
    }
    SetState(StateChecked, checked);
    Invalidate();
}

void RadioButton::OnSiblingSelected(Widget *selected, UINT group)
{
    UNREFERENCED_PARAMETER(selected);
    if (group == m_Group && IsChecked())
    {
        SetState(StateChecked, FALSE);
        Invalidate();
    }
}

void RadioButton::OnClick()
{
    if (IsChecked())
        return;
    SetChecked(TRUE);
    Raise(ControlValueChanged);
}

BOOL RadioButton::OnKeyDown(UINT vk)
{
    if (vk != VK_SPACE)
        return FALSE;
    OnClick();
    return TRUE;
}

SIZE RadioButton::MeasureOverride(SIZE available)
{
    LONG width = 20 + (m_Label[0] ? 8 + TextWidth(m_Label, LIBER_TYPE_BODY) + 1 : 0);
    SIZE size = { min(available.cx, width), LIBER_CONTROL_MD };
    return size;
}

void RadioButton::PaintOverride(PaintContext &context)
{
    const ThemeTokens &tokens = context.Tokens;
    RectF rc = ToRectF(Bounds());
    RectF box = { rc.X, rc.Y + (rc.Height - 20.0f) / 2, 20.0f, 20.0f };
    BOOL enabled = IsEnabled();

    if (IsChecked())
    {
        context.Painter.FillEllipse(box, tokens.Color(enabled ? LIBER_COLOR_ACCENT : LIBER_COLOR_FILL_CONTROL_DISABLED));
        float dot = HasState(StatePressed) ? 6.0f : HasState(StateHover) ? 10.0f : 8.0f;
        RectF inner = { box.X + (20.0f - dot) / 2, box.Y + (20.0f - dot) / 2, dot, dot };
        context.Painter.FillEllipse(inner, tokens.Color(LIBER_COLOR_ON_ACCENT));
    }
    else
    {
        context.Painter.FillEllipse(box, tokens.Color(HasState(StateHover) ? LIBER_COLOR_FILL_CONTROL_HOVER
                                                                          : LIBER_COLOR_FILL_CONTROL_ALT));
        RectF edge = { box.X + 0.5f, box.Y + 0.5f, box.Width - 1.0f, box.Height - 1.0f };
        context.Painter.StrokeRoundRect(edge, edge.Width / 2, tokens.Color(LIBER_COLOR_STROKE_CONTROL_STRONG), 1.0f);
    }
    if (HasState(StateFocused))
    {
        RectF ring = { box.X - 2.0f, box.Y - 2.0f, box.Width + 4.0f, box.Height + 4.0f };
        context.Painter.StrokeRoundRect(ring, ring.Width / 2, tokens.Color(LIBER_COLOR_ACCENT_RING), 2.0f);
    }
    if (m_Label[0])
    {
        RectF text = { box.X + 28.0f, rc.Y, rc.Width - 28.0f, rc.Height };
        context.Painter.Text(m_Label, text, LIBER_TYPE_BODY, ControlInk(tokens, enabled), TextMiddle | TextEllipsis);
    }
}

TextField::TextField(LPCWSTR placeholder, TextFieldKind kind) : m_Kind(kind), m_Length(0)
{
    CopyLabel(m_Placeholder, _countof(m_Placeholder), placeholder);
    m_Text[0] = 0;
    SetSize(-1, LIBER_CONTROL_MD);
    SetFocusable(TRUE);
}

void TextField::SetText(LPCWSTR text)
{
    CopyLabel(m_Text, _countof(m_Text), text);
    m_Length = (UINT)lstrlenW(m_Text);
    Invalidate();
}

BOOL TextField::OnChar(WCHAR ch)
{
    if (ch == L'\b')
    {
        if (!m_Length)
            return FALSE;
        m_Text[--m_Length] = 0;
    }
    else if (ch >= L' ' && ch != 0x7F)
    {
        if (m_Length + 1 >= _countof(m_Text))
            return FALSE;
        m_Text[m_Length++] = ch;
        m_Text[m_Length] = 0;
    }
    else
    {
        return FALSE;
    }
    Invalidate();
    Raise(ControlValueChanged);
    return TRUE;
}

BOOL TextField::OnKeyDown(UINT vk)
{
    if (vk == VK_ESCAPE && m_Length && m_Kind == TextFieldKind::Search)
    {
        SetText(L"");
        Raise(ControlValueChanged);
        return TRUE;
    }
    return FALSE;
}

void TextField::DisplayText(LPWSTR out, UINT cch) const
{
    LUI_ASSERT(out && cch);
    if (m_Kind != TextFieldKind::Password)
    {
        CopyLabel(out, cch, m_Text);
        return;
    }
    UINT i;
    for (i = 0; i < m_Length && i + 1 < cch; i++)
        out[i] = 0x25CF;
    out[i] = 0;
}

void TextField::PaintOverride(PaintContext &context)
{
    const ThemeTokens &tokens = context.Tokens;
    RectF rc = ToRectF(Bounds());
    BOOL focused = HasState(StateFocused), search = m_Kind == TextFieldKind::Search;
    float radius = search ? rc.Height / 2 : (float)LIBER_RADIUS_CONTROL;
    WCHAR shown[_countof(m_Text)];

    context.Painter.FillRoundRect(rc, radius, tokens.Color(focused ? LIBER_COLOR_FILL_INPUT_FOCUS
                                                     : HasState(StateHover) ? LIBER_COLOR_FILL_INPUT_HOVER
                                                                            : LIBER_COLOR_FILL_INPUT));
    context.Painter.StrokeRoundRect(rc, radius, tokens.Color(LIBER_COLOR_STROKE_CONTROL), 1.0f);
    if (search)
    {
        if (focused)
        {
            RectF ring = { rc.X - 2.0f, rc.Y - 2.0f, rc.Width + 4.0f, rc.Height + 4.0f };
            context.Painter.StrokeRoundRect(ring, radius + 2.0f, tokens.Color(LIBER_COLOR_ACCENT_RING), 2.0f);
        }
        RectF icon = { rc.X + 12.0f, rc.Y + (rc.Height - 16.0f) / 2, 16.0f, 16.0f };
        context.Painter.Icon(L"search_16_regular", icon, tokens.Color(LIBER_COLOR_TEXT2));
    }
    else
    {
        RectF bottom = { rc.X + radius, rc.Y + rc.Height - (focused ? 2.0f : 1.0f), rc.Width - 2 * radius,
                         focused ? 2.0f : 1.0f };
        context.Painter.FillRect(bottom, tokens.Color(focused ? LIBER_COLOR_ACCENT : LIBER_COLOR_STROKE_INPUT_BOTTOM));
    }
    DisplayText(shown, _countof(shown));
    float left = search ? 36.0f : 12.0f;
    RectF text = { rc.X + left, rc.Y, rc.Width - left - 12.0f, rc.Height };
    if (shown[0])
        context.Painter.Text(shown, text, LIBER_TYPE_BODY, tokens.Color(LIBER_COLOR_TEXT1), TextMiddle | TextEllipsis);
    else
        context.Painter.Text(m_Placeholder, text, LIBER_TYPE_BODY, tokens.Color(LIBER_COLOR_TEXT3), TextMiddle | TextEllipsis);
    if (focused)
    {
        float caret = rc.X + left + (shown[0] ? (float)TextWidth(shown, LIBER_TYPE_BODY) : 0.0f) + 1.0f;
        RectF bar = { min(caret, rc.X + rc.Width - 12.0f), rc.Y + 8.0f, 1.0f, rc.Height - 16.0f };
        context.Painter.FillRect(bar, tokens.Color(LIBER_COLOR_TEXT1));
    }
}

SegmentedControl::SegmentedControl() : m_Count(0), m_Selected(0)
{
    SetSize(-1, LIBER_CONTROL_MD);
    SetFocusable(TRUE);
    m_Slide.Set(0.0);
}

UINT SegmentedControl::AddOption(LPCWSTR text)
{
    LUI_ASSERT(m_Count < _countof(m_Options));
    if (m_Count >= _countof(m_Options))
        return m_Count;
    CopyLabel(m_Options[m_Count], _countof(m_Options[m_Count]), text);
    InvalidateLayout();
    return m_Count++;
}

void SegmentedControl::Select(UINT index, BOOL animate)
{
    LUI_ASSERT(index < m_Count);
    if (index >= m_Count || index == m_Selected)
        return;
    ULONGLONG now = AnimationClock::Now();
    if (animate)
    {
        m_Slide.Start(m_Slide.Value(now), (double)index, LIBER_DUR_NORMAL, Easing::Out, now);
        Animate(LIBER_DUR_NORMAL);
    }
    else
    {
        m_Slide.Set((double)index);
    }
    m_Selected = index;
    Invalidate();
}

UINT SegmentedControl::OptionAt(LONG x) const
{
    const RECT &rc = Bounds();
    LONG width = rc.right - rc.left - 4;
    if (!m_Count || width <= 0)
        return 0;
    LONG offset = x - rc.left - 2;
    if (offset < 0)
        return 0;
    UINT index = (UINT)(offset * (LONG)m_Count / width);
    return index < m_Count ? index : m_Count - 1;
}

void SegmentedControl::OnPointerUp(POINT pt)
{
    UINT index = OptionAt(pt.x);
    if (index != m_Selected)
    {
        Select(index, TRUE);
        Raise(ControlValueChanged);
    }
}

BOOL SegmentedControl::OnKeyDown(UINT vk)
{
    if ((vk == VK_LEFT && m_Selected > 0) || (vk == VK_RIGHT && m_Selected + 1 < m_Count))
    {
        Select(vk == VK_LEFT ? m_Selected - 1 : m_Selected + 1, TRUE);
        Raise(ControlValueChanged);
        return TRUE;
    }
    return FALSE;
}

SIZE SegmentedControl::MeasureOverride(SIZE available)
{
    LONG widest = 0;
    for (UINT i = 0; i < m_Count; i++)
        widest = max(widest, TextWidth(m_Options[i], LIBER_TYPE_BODY) + 1);
    SIZE size = { min(available.cx, (LONG)(4 + m_Count * (widest + 24))), LIBER_CONTROL_MD };
    return size;
}

void SegmentedControl::PaintOverride(PaintContext &context)
{
    const ThemeTokens &tokens = context.Tokens;
    RectF rc = ToRectF(Bounds());

    context.Painter.FillRoundRect(rc, rc.Height / 2, tokens.Color(LIBER_COLOR_FILL_CONTROL_ALT));
    context.Painter.StrokeRoundRect(rc, rc.Height / 2, tokens.Color(LIBER_COLOR_STROKE_CONTROL), 1.0f);
    if (!m_Count)
        return;
    float cell = (rc.Width - 4.0f) / m_Count;
    float at = (float)m_Slide.Value(context.Now);
    RectF block = { rc.X + 2.0f + cell * at, rc.Y + 2.0f, cell, rc.Height - 4.0f };
    const LIBER_SHADOW &shadow = tokens.Shadow(LIBER_ELEVATION_E2);
    context.Painter.FillShadow(block, block.Height / 2, (float)shadow.Contact.Blur, (Argb)shadow.Contact.Argb);
    context.Painter.FillRoundRect(block, block.Height / 2, tokens.Color(LIBER_COLOR_FILL_CONTROL));
    for (UINT i = 0; i < m_Count; i++)
    {
        RectF text = { rc.X + 2.0f + cell * i, rc.Y, cell, rc.Height };
        context.Painter.Text(m_Options[i], text, i == m_Selected ? LIBER_TYPE_BODY_STRONG : LIBER_TYPE_BODY,
                             tokens.Color(i == m_Selected ? LIBER_COLOR_TEXT1 : LIBER_COLOR_TEXT2),
                             TextCenter | TextMiddle | TextEllipsis);
    }
    if (HasState(StateFocused))
    {
        RectF ring = { rc.X - 2.0f, rc.Y - 2.0f, rc.Width + 4.0f, rc.Height + 4.0f };
        context.Painter.StrokeRoundRect(ring, ring.Height / 2, tokens.Color(LIBER_COLOR_ACCENT_RING), 2.0f);
    }
}

TabStrip::TabStrip() : m_Count(0), m_Selected(0)
{
    SetSize(-1, LIBER_CONTROL_LG);
    SetFocusable(TRUE);
}

UINT TabStrip::AddTab(LPCWSTR text)
{
    LUI_ASSERT(m_Count < _countof(m_Tabs));
    if (m_Count >= _countof(m_Tabs))
        return m_Count;
    CopyLabel(m_Tabs[m_Count], _countof(m_Tabs[m_Count]), text);
    InvalidateLayout();
    return m_Count++;
}

void TabStrip::Select(UINT index)
{
    LUI_ASSERT(index < m_Count);
    if (index >= m_Count)
        return;
    m_Selected = index;
    Invalidate();
}

UINT TabStrip::TabAt(LONG x) const
{
    LONG left = Bounds().left;
    for (UINT i = 0; i < m_Count; i++)
    {
        LONG width = TextWidth(m_Tabs[i], LIBER_TYPE_BODY_STRONG) + 24;
        if (x < left + width)
            return i;
        left += width;
    }
    return m_Count ? m_Count - 1 : 0;
}

void TabStrip::OnPointerUp(POINT pt)
{
    UINT index = TabAt(pt.x);
    if (index != m_Selected)
    {
        Select(index);
        Raise(ControlValueChanged);
    }
}

BOOL TabStrip::OnKeyDown(UINT vk)
{
    if ((vk == VK_LEFT && m_Selected > 0) || (vk == VK_RIGHT && m_Selected + 1 < m_Count))
    {
        Select(vk == VK_LEFT ? m_Selected - 1 : m_Selected + 1);
        Raise(ControlValueChanged);
        return TRUE;
    }
    return FALSE;
}

SIZE TabStrip::MeasureOverride(SIZE available)
{
    LONG width = 0;
    for (UINT i = 0; i < m_Count; i++)
        width += TextWidth(m_Tabs[i], LIBER_TYPE_BODY_STRONG) + 24;
    SIZE size = { min(available.cx, width), LIBER_CONTROL_LG };
    return size;
}

void TabStrip::PaintOverride(PaintContext &context)
{
    const ThemeTokens &tokens = context.Tokens;
    RectF rc = ToRectF(Bounds());
    float x = rc.X;

    for (UINT i = 0; i < m_Count; i++)
    {
        float width = (float)TextWidth(m_Tabs[i], LIBER_TYPE_BODY_STRONG) + 24.0f;
        RectF text = { x, rc.Y, width, rc.Height - 3.0f };
        BOOL selected = i == m_Selected;
        context.Painter.Text(m_Tabs[i], text, selected ? LIBER_TYPE_BODY_STRONG : LIBER_TYPE_BODY,
                             tokens.Color(selected ? LIBER_COLOR_TEXT1 : LIBER_COLOR_TEXT2), TextCenter | TextMiddle);
        if (selected)
        {
            RectF bar = { x + width / 2 - 8.0f, rc.Y + rc.Height - 3.0f, 16.0f, 3.0f };
            context.Painter.FillRoundRect(bar, 1.5f, tokens.Color(LIBER_COLOR_ACCENT));
        }
        x += width;
    }
}

NavItem::NavItem(LPCWSTR icon, LPCWSTR selectedIcon, LPCWSTR text, LPCWSTR badge)
{
    CopyLabel(m_Icon, _countof(m_Icon), icon);
    CopyLabel(m_SelectedIcon, _countof(m_SelectedIcon), selectedIcon && *selectedIcon ? selectedIcon : icon);
    CopyLabel(m_Text, _countof(m_Text), text);
    CopyLabel(m_Badge, _countof(m_Badge), badge);
    SetSize(-1, 36);
    SetFocusable(TRUE);
}

void NavItem::SetSelected(BOOL selected)
{
    if (selected && Parent())
    {
        for (Widget *sibling = Parent()->FirstChild(); sibling; sibling = sibling->Next())
        {
            if (sibling != this)
                sibling->OnSiblingSelected(this, LIBER_NAV_GROUP);
        }
    }
    SetState(StateChecked, selected);
    Invalidate();
}

void NavItem::OnSiblingSelected(Widget *selected, UINT group)
{
    UNREFERENCED_PARAMETER(selected);
    if (group == LIBER_NAV_GROUP && IsSelected())
    {
        SetState(StateChecked, FALSE);
        Invalidate();
    }
}

void NavItem::OnClick()
{
    if (!IsSelected())
        SetSelected(TRUE);
    Raise(ControlClicked);
}

void NavItem::PaintOverride(PaintContext &context)
{
    const ThemeTokens &tokens = context.Tokens;
    RectF rc = ToRectF(Bounds());
    BOOL selected = IsSelected();

    if (selected || HasState(StateHover) || HasState(StatePressed))
    {
        context.Painter.FillRoundRect(rc, (float)LIBER_RADIUS_ITEM,
                                      tokens.Color(HasState(StatePressed) ? LIBER_COLOR_FILL_SUBTLE_PRESSED
                                                   : selected            ? LIBER_COLOR_FILL_SUBTLE_SELECTED
                                                                         : LIBER_COLOR_FILL_SUBTLE_HOVER));
    }
    if (selected)
    {
        RectF bar = { rc.X, rc.Y + (rc.Height - 16.0f) / 2, 3.0f, 16.0f };
        context.Painter.FillRoundRect(bar, 1.5f, tokens.Color(LIBER_COLOR_ACCENT));
    }
    RectF icon = { rc.X + 12.0f, rc.Y + (rc.Height - 20.0f) / 2, 20.0f, 20.0f };
    context.Painter.Icon(selected ? m_SelectedIcon : m_Icon, icon,
                         tokens.Color(selected ? LIBER_COLOR_ACCENT_TEXT : LIBER_COLOR_TEXT2));
    float badge = m_Badge[0] ? (float)TextWidth(m_Badge, LIBER_TYPE_CAPTION) + 12.0f : 0.0f;
    RectF text = { rc.X + 44.0f, rc.Y, rc.Width - 56.0f - badge, rc.Height };
    context.Painter.Text(m_Text, text, LIBER_TYPE_BODY, tokens.Color(LIBER_COLOR_TEXT1), TextMiddle | TextEllipsis);
    if (m_Badge[0])
    {
        RectF pill = { rc.X + rc.Width - 12.0f - badge, rc.Y + (rc.Height - 16.0f) / 2, badge, 16.0f };
        context.Painter.FillRoundRect(pill, 8.0f, tokens.Color(LIBER_COLOR_FILL_CONTROL_STRONG));
        context.Painter.Text(m_Badge, pill, LIBER_TYPE_CAPTION, tokens.Color(LIBER_COLOR_ON_ACCENT),
                             TextCenter | TextMiddle);
    }
}

InfoBar::InfoBar(InfoSeverity severity, LPCWSTR title, LPCWSTR message) : m_Severity(severity)
{
    CopyLabel(m_Title, _countof(m_Title), title);
    CopyLabel(m_Message, _countof(m_Message), message);
    SetSize(-1, 48);
}

LIBER_COLOR InfoBar::Background() const
{
    switch (m_Severity)
    {
        case InfoSeverity::Success: return LIBER_COLOR_SUCCESS_BG;
        case InfoSeverity::Caution: return LIBER_COLOR_CAUTION_BG;
        case InfoSeverity::Critical: return LIBER_COLOR_CRITICAL_BG;
        default: return LIBER_COLOR_INFO_BG;
    }
}

LIBER_COLOR InfoBar::Foreground() const
{
    switch (m_Severity)
    {
        case InfoSeverity::Success: return LIBER_COLOR_SUCCESS;
        case InfoSeverity::Caution: return LIBER_COLOR_CAUTION;
        case InfoSeverity::Critical: return LIBER_COLOR_CRITICAL;
        default: return LIBER_COLOR_INFO;
    }
}

LPCWSTR InfoBar::IconName() const
{
    switch (m_Severity)
    {
        case InfoSeverity::Success: return L"checkmark_circle_20_filled";
        case InfoSeverity::Caution: return L"warning_20_filled";
        case InfoSeverity::Critical: return L"error_circle_20_filled";
        default: return L"info_20_filled";
    }
}

void InfoBar::PaintOverride(PaintContext &context)
{
    const ThemeTokens &tokens = context.Tokens;
    RectF rc = ToRectF(Bounds());

    context.Painter.FillRoundRect(rc, (float)LIBER_RADIUS_CARD, tokens.Color(Background()));
    context.Painter.StrokeRoundRect(rc, (float)LIBER_RADIUS_CARD, tokens.Color(LIBER_COLOR_STROKE_CARD), 1.0f);
    RectF icon = { rc.X + 16.0f, rc.Y + (rc.Height - 20.0f) / 2, 20.0f, 20.0f };
    context.Painter.Icon(IconName(), icon, tokens.Color(Foreground()));
    float title = (float)TextWidth(m_Title, LIBER_TYPE_BODY_STRONG) + 12.0f;
    RectF head = { rc.X + 48.0f, rc.Y, title, rc.Height };
    context.Painter.Text(m_Title, head, LIBER_TYPE_BODY_STRONG, tokens.Color(LIBER_COLOR_TEXT1), TextMiddle);
    RectF body = { head.X + title, rc.Y, rc.Width - 64.0f - title, rc.Height };
    context.Painter.Text(m_Message, body, LIBER_TYPE_BODY, tokens.Color(LIBER_COLOR_TEXT1), TextMiddle | TextEllipsis);
}

DialogPanel::DialogPanel(LPCWSTR title, LPCWSTR message) : StackPanel(Orientation::Vertical, 12)
{
    Thickness pad = { 24, 24, 24, 24 };
    SetPadding(pad);
    SetSize(LIBER_DIALOG_WIDTH, -1);
    m_Title = new Label(title, LIBER_TYPE_SUBTITLE, LIBER_COLOR_TEXT1, TextLeft | TextMiddle);
    m_Message = new Label(message, LIBER_TYPE_BODY, LIBER_COLOR_TEXT1, TextLeft | TextWrap);
    m_Buttons = new StackPanel(Orientation::Horizontal, 8);
    Thickness gap = { 0, 12, 0, 0 };
    m_Buttons->SetMargin(gap);
    m_Buttons->SetAlign(Align::End, Align::Center);
    AddChild(m_Title);
    AddChild(m_Message);
    AddChild(m_Buttons);
}

Button *DialogPanel::AddButton(LPCWSTR text, UINT id, BOOL primary, IControlListener *listener)
{
    Button *button = new Button(text, NULL, primary ? ButtonKind::Accent : ButtonKind::Standard, LIBER_CONTROL_MD,
                                FALSE);
    button->SetId(id);
    button->SetListener(listener);
    m_Buttons->AddChild(button);
    return button;
}

void DialogPanel::PaintOverride(PaintContext &context)
{
    const ThemeTokens &tokens = context.Tokens;
    RectF rc = ToRectF(Bounds());

    const LIBER_SHADOW &shadow = tokens.Shadow(LIBER_ELEVATION_E64);
    RectF ambient = { rc.X + shadow.Ambient.X, rc.Y + shadow.Ambient.Y, rc.Width, rc.Height };
    context.Painter.FillShadow(ambient, (float)LIBER_RADIUS_WINDOW, (float)shadow.Ambient.Blur, (Argb)shadow.Ambient.Argb,
                               &rc);
    context.Painter.FillRoundRect(rc, (float)LIBER_RADIUS_WINDOW, tokens.Color(LIBER_COLOR_SURFACE_CARD_SOLID));
    context.Painter.StrokeRoundRect(rc, (float)LIBER_RADIUS_WINDOW, tokens.Color(LIBER_COLOR_STROKE_CARD), 1.0f);
}

TooltipPanel::TooltipPanel() : m_Glass(FALSE)
{
    m_Text[0] = 0;
}

void TooltipPanel::SetText(LPCWSTR text)
{
    CopyLabel(m_Text, _countof(m_Text), text);
    InvalidateLayout();
    Invalidate();
}

SIZE TooltipPanel::MeasureOverride(SIZE available)
{
    SIZE size = { min(available.cx, TextWidth(m_Text, LIBER_TYPE_CAPTION) + 1 + 16), 28 };
    return size;
}

void TooltipPanel::PaintOverride(PaintContext &context)
{
    const ThemeTokens &tokens = context.Tokens;
    RectF rc = ToRectF(Bounds());
    GlassTint tint = tokens.Material(LIBER_MATERIAL_THICK, context.Backdrop);

    context.Painter.FillRoundRect(rc, (float)LIBER_RADIUS_R6, OklchToArgb(tint.Color, m_Glass ? tint.Alpha : 1.0));
    RectF edge = { rc.X + 0.5f, rc.Y + 0.5f, rc.Width - 1.0f, rc.Height - 1.0f };
    context.Painter.StrokeRoundRect(edge, (float)LIBER_RADIUS_R6 - 0.5f, tokens.Color(LIBER_COLOR_GLASS_EDGE), 1.0f);
    RectF text = { rc.X + 8.0f, rc.Y, rc.Width - 16.0f, rc.Height };
    context.Painter.Text(m_Text, text, LIBER_TYPE_CAPTION, tokens.Color(LIBER_COLOR_TEXT1), TextCenter | TextMiddle);
}

}
