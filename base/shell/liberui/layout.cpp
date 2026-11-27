/*
 * PROJECT:     LiberNT Liber UI
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Stack and grid layout panels
 * COPYRIGHT:   Copyright 2026 Eitan Hadar <eitanhadar4@gmail.com>
 */

#include "precomp.h"

namespace LiberUI
{

static int Sub(int value, int amount)
{
    if (value >= LayoutInfinite)
        return LayoutInfinite;
    value -= amount;
    return value < 0 ? 0 : value;
}

static int Add(int value, int amount)
{
    value += amount;
    return value > LayoutInfinite ? LayoutInfinite : value;
}

Panel::Panel()
{
    ZeroMemory(&m_Padding, sizeof(m_Padding));
}

SIZE Panel::Shrink(SIZE available) const
{
    SIZE result = { Sub(available.cx, m_Padding.Left + m_Padding.Right),
                    Sub(available.cy, m_Padding.Top + m_Padding.Bottom) };
    return result;
}

SIZE Panel::Grow(SIZE size) const
{
    SIZE result = { Add(size.cx, m_Padding.Left + m_Padding.Right), Add(size.cy, m_Padding.Top + m_Padding.Bottom) };
    return result;
}

RECT Panel::ContentRect(const RECT &content) const
{
    RECT rc = { content.left + m_Padding.Left, content.top + m_Padding.Top, content.right - m_Padding.Right,
                content.bottom - m_Padding.Bottom };
    if (rc.right < rc.left)
        rc.right = rc.left;
    if (rc.bottom < rc.top)
        rc.bottom = rc.top;
    return rc;
}

SIZE Panel::MeasureOverride(SIZE available)
{
    return Grow(MeasureChildMax(this, Shrink(available)));
}

void Panel::ArrangeOverride(const RECT &content)
{
    RECT rc = ContentRect(content);
    for (Widget *child = FirstChild(); child; child = child->Next())
        child->Arrange(rc);
}

StackPanel::StackPanel(Orientation orientation, int gap)
    : m_Orientation(orientation), m_Gap(gap)
{
    LUI_ASSERT(gap >= 0);
}

SIZE StackPanel::MeasureOverride(SIZE available)
{
    SIZE inner = Shrink(available);
    BOOL horizontal = m_Orientation == Orientation::Horizontal;
    int main = 0, cross = 0;
    UINT visible = 0;

    for (Widget *child = FirstChild(); child; child = child->Next())
    {
        if (!child->IsVisible())
        {
            child->Measure(inner);
            continue;
        }

        SIZE slot = inner;
        int used = main + (visible ? m_Gap : 0);
        if (horizontal)
            slot.cx = Sub(inner.cx, used);
        else
            slot.cy = Sub(inner.cy, used);

        SIZE size = child->Measure(slot);
        main = Add(used, horizontal ? size.cx : size.cy);
        int c = horizontal ? size.cy : size.cx;
        if (c > cross)
            cross = c;
        visible++;
    }

    SIZE result;
    result.cx = horizontal ? main : cross;
    result.cy = horizontal ? cross : main;
    return Grow(result);
}

void StackPanel::ArrangeOverride(const RECT &content)
{
    RECT rc = ContentRect(content);
    BOOL horizontal = m_Orientation == Orientation::Horizontal;
    LONG at = horizontal ? rc.left : rc.top;
    UINT visible = 0;

    for (Widget *child = FirstChild(); child; child = child->Next())
    {
        if (!child->IsVisible())
        {
            child->Arrange(rc);
            continue;
        }

        if (visible++)
            at += m_Gap;

        RECT slot = rc;
        if (horizontal)
        {
            slot.left = at;
            slot.right = at + child->Desired().cx;
            at = slot.right;
        }
        else
        {
            slot.top = at;
            slot.bottom = at + child->Desired().cy;
            at = slot.bottom;
        }
        child->Arrange(slot);
    }
}

static Widget *FocusableIn(Widget *widget)
{
    if (!widget || !widget->IsVisible())
        return NULL;
    if (widget->IsFocusable())
        return widget;
    for (Widget *child = widget->FirstChild(); child; child = child->Next())
    {
        Widget *found = FocusableIn(child);
        if (found)
            return found;
    }
    return NULL;
}

static Widget *ChildAt(Widget *parent, int index)
{
    for (Widget *child = parent->FirstChild(); child; child = child->Next())
    {
        if (!child->IsVisible())
            continue;
        if (index-- == 0)
            return child;
    }
    return NULL;
}

static int IndexOf(Widget *parent, Widget *target)
{
    int index = 0;
    for (Widget *child = parent->FirstChild(); child; child = child->Next())
    {
        if (!child->IsVisible())
            continue;
        if (child == target)
            return index;
        index++;
    }
    return -1;
}

static Widget *StepFocus(Widget *parent, Widget *from, int step)
{
    int index = IndexOf(parent, from);
    if (index < 0 || step == 0)
        return NULL;

    for (int i = index + step; i >= 0; i += step)
    {
        Widget *child = ChildAt(parent, i);
        if (!child)
            return NULL;
        Widget *target = FocusableIn(child);
        if (target)
            return target;
    }
    return NULL;
}

Widget *StackPanel::MoveFocus(Widget *from, FocusMove move)
{
    BOOL horizontal = m_Orientation == Orientation::Horizontal;

    switch (move)
    {
        case FocusMove::Left:
            return horizontal ? StepFocus(this, from, -1) : NULL;
        case FocusMove::Right:
            return horizontal ? StepFocus(this, from, 1) : NULL;
        case FocusMove::Up:
            return horizontal ? NULL : StepFocus(this, from, -1);
        case FocusMove::Down:
            return horizontal ? NULL : StepFocus(this, from, 1);
    }
    return NULL;
}

GridPanel::GridPanel(UINT columns, int columnGap, int rowGap)
    : m_Columns(columns ? columns : 1), m_ColumnGap(columnGap), m_RowGap(rowGap)
{
    LUI_ASSERT(columns >= 1 && columnGap >= 0 && rowGap >= 0);
}

SIZE GridPanel::MeasureOverride(SIZE available)
{
    SIZE inner = Shrink(available);
    int gaps = m_ColumnGap * (int)(m_Columns - 1);
    int cell = inner.cx >= LayoutInfinite ? LayoutInfinite : Sub(inner.cx, gaps) / (int)m_Columns;
    int widest = 0, height = 0, rowHeight = 0;
    UINT column = 0, rows = 0;

    for (Widget *child = FirstChild(); child; child = child->Next())
    {
        SIZE slot = { cell, LayoutInfinite };
        SIZE size = child->Measure(slot);
        if (!child->IsVisible())
            continue;

        if (size.cx > widest)
            widest = size.cx;
        if (size.cy > rowHeight)
            rowHeight = size.cy;
        if (++column == m_Columns)
        {
            height += (rows++ ? m_RowGap : 0) + rowHeight;
            rowHeight = 0;
            column = 0;
        }
    }
    if (column)
        height += (rows++ ? m_RowGap : 0) + rowHeight;

    SIZE result;
    result.cx = inner.cx >= LayoutInfinite ? widest * (int)m_Columns + gaps : inner.cx;
    result.cy = height;
    return Grow(result);
}

void GridPanel::ArrangeOverride(const RECT &content)
{
    RECT rc = ContentRect(content);
    int gaps = m_ColumnGap * (int)(m_Columns - 1);
    int cell = Sub(rc.right - rc.left, gaps) / (int)m_Columns;
    LONG top = rc.top;
    UINT column = 0;
    Widget *rowStart = NULL;

    for (Widget *child = FirstChild(); child; child = child->Next())
    {
        if (!child->IsVisible())
        {
            child->Arrange(rc);
            continue;
        }
        if (column == 0)
            rowStart = child;

        if (++column == m_Columns)
        {
            int rowHeight = 0;
            UINT placed = 0;
            for (Widget *w = rowStart; w && placed < column; w = w->Next())
            {
                if (!w->IsVisible())
                    continue;
                if (w->Desired().cy > rowHeight)
                    rowHeight = w->Desired().cy;
                placed++;
            }

            placed = 0;
            for (Widget *w = rowStart; w && placed < column; w = w->Next())
            {
                if (!w->IsVisible())
                    continue;
                LONG left = rc.left + (LONG)placed * (cell + m_ColumnGap);
                RECT slot = { left, top, left + cell, top + rowHeight };
                w->Arrange(slot);
                placed++;
            }
            top += rowHeight + m_RowGap;
            column = 0;
        }
    }

    if (column)
    {
        int rowHeight = 0;
        UINT placed = 0;
        for (Widget *w = rowStart; w; w = w->Next())
        {
            if (w->IsVisible() && w->Desired().cy > rowHeight)
                rowHeight = w->Desired().cy;
        }
        for (Widget *w = rowStart; w; w = w->Next())
        {
            if (!w->IsVisible())
                continue;
            LONG left = rc.left + (LONG)placed * (cell + m_ColumnGap);
            RECT slot = { left, top, left + cell, top + rowHeight };
            w->Arrange(slot);
            placed++;
        }
    }
}

Widget *GridPanel::MoveFocus(Widget *from, FocusMove move)
{
    switch (move)
    {
        case FocusMove::Left:
            return StepFocus(this, from, -1);
        case FocusMove::Right:
            return StepFocus(this, from, 1);
        case FocusMove::Up:
            return StepFocus(this, from, -(int)m_Columns);
        case FocusMove::Down:
            return StepFocus(this, from, (int)m_Columns);
    }
    return NULL;
}

}
