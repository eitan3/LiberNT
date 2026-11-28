/*
 * PROJECT:     LiberNT Liber UI
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Retained widget tree: ownership, state, measure, arrange, paint and hit testing
 * COPYRIGHT:   Copyright 2026 Eitan Hadar <eitanhadar4@gmail.com>
 */

#include "precomp.h"

namespace LiberUI
{

static int ClampSize(int value)
{
    return value < 0 ? 0 : value > LayoutInfinite ? LayoutInfinite : value;
}

Widget::Widget()
    : m_Parent(NULL), m_FirstChild(NULL), m_LastChild(NULL), m_Next(NULL), m_Prev(NULL), m_Host(NULL),
      m_HAlign(Align::Stretch), m_VAlign(Align::Stretch), m_State(0), m_Focusable(FALSE), m_HitTestable(FALSE)
{
    m_Tooltip[0] = 0;
    SetRectEmpty(&m_Bounds);
    m_Desired.cx = m_Desired.cy = 0;
    m_Fixed.cx = m_Fixed.cy = -1;
    m_Min.cx = m_Min.cy = 0;
    ZeroMemory(&m_Margin, sizeof(m_Margin));
}

Widget::~Widget()
{
    LUI_ASSERT(m_Parent == NULL);

    Widget *child = m_FirstChild;
    while (child)
    {
        Widget *next = child->m_Next;
        child->m_Parent = NULL;
        child->m_Next = child->m_Prev = NULL;
        delete child;
        child = next;
    }
    m_FirstChild = m_LastChild = NULL;
}

void Widget::AddChild(Widget *child)
{
    LUI_ASSERT(child != NULL && child != this);
    if (!child || child == this)
        return;
    LUI_ASSERT(child->m_Parent == NULL && child->m_Next == NULL && child->m_Prev == NULL);
    if (child->m_Parent)
        return;

    for (Widget *p = this; p; p = p->m_Parent)
    {
        LUI_ASSERT(p != child);
        if (p == child)
            return;
    }

    child->m_Parent = this;
    child->m_Prev = m_LastChild;
    if (m_LastChild)
        m_LastChild->m_Next = child;
    else
        m_FirstChild = child;
    m_LastChild = child;
    InvalidateLayout();
}

void Widget::RemoveChild(Widget *child)
{
    LUI_ASSERT(child != NULL && child->m_Parent == this);
    if (!child || child->m_Parent != this)
        return;

    IWidgetHost *host = Host();
    RECT old = child->m_Bounds;

    if (child->m_Prev)
        child->m_Prev->m_Next = child->m_Next;
    else
        m_FirstChild = child->m_Next;
    if (child->m_Next)
        child->m_Next->m_Prev = child->m_Prev;
    else
        m_LastChild = child->m_Prev;

    child->m_Parent = NULL;
    child->m_Next = child->m_Prev = NULL;
    LUI_ASSERT((m_FirstChild == NULL) == (m_LastChild == NULL));

    if (host)
    {
        host->OnWidgetRemoved(child);
        host->InvalidateRect(old);
        host->RequestLayout();
    }
}

UINT Widget::ChildCount() const
{
    UINT count = 0;
    for (Widget *child = m_FirstChild; child; child = child->m_Next)
        count++;
    return count;
}

IWidgetHost *Widget::Host() const
{
    const Widget *root = this;
    while (root->m_Parent)
        root = root->m_Parent;
    return root->m_Host;
}

void Widget::SetHost(IWidgetHost *host)
{
    LUI_ASSERT(m_Parent == NULL);
    m_Host = host;
}

void Widget::SetSize(int width, int height)
{
    m_Fixed.cx = width;
    m_Fixed.cy = height;
    InvalidateLayout();
}

void Widget::SetMinSize(int width, int height)
{
    LUI_ASSERT(width >= 0 && height >= 0);
    m_Min.cx = width < 0 ? 0 : width;
    m_Min.cy = height < 0 ? 0 : height;
    InvalidateLayout();
}

void Widget::SetAlign(Align horizontal, Align vertical)
{
    m_HAlign = horizontal;
    m_VAlign = vertical;
    InvalidateLayout();
}

void Widget::SetState(UINT state, BOOL on)
{
    UINT next = on ? (m_State | state) : (m_State & ~state);
    if (next == m_State)
        return;

    BOOL layout = ((next ^ m_State) & StateHidden) != 0;
    if (layout)
        Invalidate();
    m_State = next;
    Invalidate();
    if (layout)
        InvalidateLayout();
}

BOOL Widget::IsEnabled() const
{
    for (const Widget *w = this; w; w = w->m_Parent)
    {
        if (w->HasState(StateDisabled))
            return FALSE;
    }
    return TRUE;
}

BOOL Widget::IsVisibleInTree() const
{
    for (const Widget *w = this; w; w = w->m_Parent)
    {
        if (!w->IsVisible())
            return FALSE;
    }
    return TRUE;
}

void Widget::SetVisible(BOOL visible)
{
    SetState(StateHidden, !visible);
}

void Widget::SetEnabled(BOOL enabled)
{
    SetState(StateDisabled, !enabled);
}

SIZE Widget::Measure(SIZE available)
{
    SIZE inner, content, result;

    if (!IsVisible())
    {
        m_Desired.cx = m_Desired.cy = 0;
        return m_Desired;
    }

    inner.cx = ClampSize(available.cx - m_Margin.Left - m_Margin.Right);
    inner.cy = ClampSize(available.cy - m_Margin.Top - m_Margin.Bottom);
    if (m_Fixed.cx >= 0)
        inner.cx = m_Fixed.cx;
    if (m_Fixed.cy >= 0)
        inner.cy = m_Fixed.cy;

    content = MeasureOverride(inner);
    LUI_ASSERT(content.cx >= 0 && content.cy >= 0);

    if (m_Fixed.cx >= 0)
        content.cx = m_Fixed.cx;
    if (m_Fixed.cy >= 0)
        content.cy = m_Fixed.cy;
    if (content.cx < m_Min.cx)
        content.cx = m_Min.cx;
    if (content.cy < m_Min.cy)
        content.cy = m_Min.cy;

    result.cx = ClampSize(content.cx + m_Margin.Left + m_Margin.Right);
    result.cy = ClampSize(content.cy + m_Margin.Top + m_Margin.Bottom);
    m_Desired = result;
    return result;
}

static void PlaceAxis(Align align, BOOL fixed, LONG start, LONG end, int desired, LONG *outStart, LONG *outEnd)
{
    int space = end - start;
    int size = (align == Align::Stretch && !fixed) ? space : (desired < space ? desired : space);

    if (size < 0)
        size = 0;
    switch (align)
    {
        case Align::Center:
            *outStart = start + (space - size) / 2;
            break;
        case Align::End:
            *outStart = end - size;
            break;
        default:
            *outStart = start;
            break;
    }
    *outEnd = *outStart + size;
}

void Widget::Arrange(const RECT &slot)
{
    RECT bounds;

    if (!IsVisible())
    {
        SetRect(&bounds, slot.left, slot.top, slot.left, slot.top);
    }
    else
    {
        RECT inner = { slot.left + m_Margin.Left, slot.top + m_Margin.Top, slot.right - m_Margin.Right,
                       slot.bottom - m_Margin.Bottom };
        if (inner.right < inner.left)
            inner.right = inner.left;
        if (inner.bottom < inner.top)
            inner.bottom = inner.top;

        PlaceAxis(m_HAlign, m_Fixed.cx >= 0, inner.left, inner.right, m_Desired.cx - m_Margin.Left - m_Margin.Right,
                  &bounds.left, &bounds.right);
        PlaceAxis(m_VAlign, m_Fixed.cy >= 0, inner.top, inner.bottom, m_Desired.cy - m_Margin.Top - m_Margin.Bottom,
                  &bounds.top, &bounds.bottom);
    }

    if (!EqualRect(&bounds, &m_Bounds))
    {
        Invalidate();
        m_Bounds = bounds;
        Invalidate();
    }
    LUI_ASSERT(m_Bounds.left <= m_Bounds.right && m_Bounds.top <= m_Bounds.bottom);
    if (IsVisible())
        ArrangeOverride(m_Bounds);
}

SIZE Widget::MeasureChildMax(Widget *parent, SIZE available)
{
    SIZE result = { 0, 0 };

    for (Widget *child = parent->m_FirstChild; child; child = child->m_Next)
    {
        SIZE size = child->Measure(available);
        if (size.cx > result.cx)
            result.cx = size.cx;
        if (size.cy > result.cy)
            result.cy = size.cy;
    }
    return result;
}

SIZE Widget::MeasureOverride(SIZE available)
{
    return MeasureChildMax(this, available);
}

void Widget::ArrangeOverride(const RECT &content)
{
    for (Widget *child = m_FirstChild; child; child = child->m_Next)
        child->Arrange(content);
}

void Widget::Paint(PaintContext &context)
{
    if (!IsVisible() || IsRectEmpty(&m_Bounds))
        return;

    PaintOverride(context);
    for (Widget *child = m_FirstChild; child; child = child->m_Next)
        child->Paint(context);
}

Widget *Widget::HitTest(POINT pt)
{
    if (!IsVisible() || !PtInRect(&m_Bounds, pt))
        return NULL;

    Widget *found = NULL;
    for (Widget *child = m_FirstChild; child; child = child->m_Next)
    {
        Widget *hit = child->HitTest(pt);
        if (hit)
            found = hit;
    }
    if (found)
        return found;
    return m_HitTestable ? this : NULL;
}

void Widget::Invalidate()
{
    IWidgetHost *host = Host();
    if (host && !IsRectEmpty(&m_Bounds) && IsVisibleInTree())
        host->InvalidateRect(m_Bounds);
}

void Widget::InvalidateLayout()
{
    IWidgetHost *host = Host();
    if (host)
        host->RequestLayout();
}

Widget *Widget::MoveFocus(Widget *from, FocusMove move)
{
    UNREFERENCED_PARAMETER(from);
    UNREFERENCED_PARAMETER(move);
    return NULL;
}

BOOL Widget::IsAncestorOf(const Widget *widget) const
{
    for (const Widget *w = widget; w; w = w->m_Parent)
    {
        if (w == this)
            return TRUE;
    }
    return FALSE;
}

void Widget::Animate(UINT durationMs)
{
    IWidgetHost *host = Host();
    if (host)
        host->RequestFrames(durationMs);
    Invalidate();
}

void Widget::SetTooltip(LPCWSTR text)
{
    lstrcpynW(m_Tooltip, text ? text : L"", _countof(m_Tooltip));
}

}
