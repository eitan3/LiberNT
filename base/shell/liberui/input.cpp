/*
 * PROJECT:     LiberNT Liber UI
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Pointer hover, press, capture and keyboard focus routing
 * COPYRIGHT:   Copyright 2026 Eitan Hadar <eitanhadar4@gmail.com>
 */

#include "precomp.h"

namespace LiberUI
{

static Widget *PreOrderNext(Widget *root, Widget *node)
{
    if (node->FirstChild() && node->IsVisible())
        return node->FirstChild();
    while (node && node != root)
    {
        if (node->Next())
            return node->Next();
        node = node->Parent();
    }
    return NULL;
}

Widget *NextInTree(Widget *root, Widget *from, BOOL forward)
{
    Widget *first = NULL, *last = NULL, *before = NULL, *after = NULL;
    BOOL passed = from == NULL;

    if (!root)
        return NULL;
    LUI_ASSERT(!from || root->IsAncestorOf(from));

    for (Widget *node = root; node; node = PreOrderNext(root, node))
    {
        if (node == from)
        {
            passed = TRUE;
            continue;
        }
        if (!node->IsFocusable() || !node->IsVisibleInTree())
            continue;
        if (!first)
            first = node;
        last = node;
        if (!passed)
            before = node;
        else if (!after)
            after = node;
    }

    if (forward)
        return after ? after : first;
    if (!from)
        return last;
    return before ? before : last;
}

InputRouter::InputRouter()
    : m_Root(NULL), m_Hover(NULL), m_Pressed(NULL), m_Focus(NULL), m_Capture(NULL)
{
}

void InputRouter::SetRoot(Widget *root)
{
    if (m_Root)
        Forget(m_Root);
    m_Root = root;
}

void InputRouter::Forget(Widget *widget)
{
    if (!widget)
        return;
    if (m_Hover && widget->IsAncestorOf(m_Hover))
    {
        m_Hover->SetState(StateHover, FALSE);
        m_Hover = NULL;
    }
    if (m_Pressed && widget->IsAncestorOf(m_Pressed))
    {
        m_Pressed->SetState(StatePressed, FALSE);
        m_Pressed = NULL;
    }
    if (m_Focus && widget->IsAncestorOf(m_Focus))
    {
        m_Focus->SetState(StateFocused, FALSE);
        m_Focus = NULL;
    }
    if (m_Capture && widget->IsAncestorOf(m_Capture))
        m_Capture = NULL;
}

void InputRouter::SetHover(Widget *widget)
{
    if (widget == m_Hover)
        return;

    Widget *old = m_Hover;
    m_Hover = widget;
    if (old)
    {
        old->SetState(StateHover, FALSE);
        old->OnPointerLeave();
    }
    if (widget)
    {
        widget->SetState(StateHover, TRUE);
        widget->OnPointerEnter();
    }
}

void InputRouter::PointerMove(POINT pt)
{
    if (!m_Root)
        return;

    Widget *hit = m_Root->HitTest(pt);
    if (m_Capture)
    {
        SetHover(hit == m_Capture ? m_Capture : NULL);
        m_Capture->OnPointerMove(pt);
        return;
    }

    SetHover(hit && hit->IsEnabled() ? hit : NULL);
    if (m_Hover)
        m_Hover->OnPointerMove(pt);
}

void InputRouter::PointerLeave()
{
    if (!m_Capture)
        SetHover(NULL);
}

void InputRouter::PointerDown(POINT pt)
{
    if (!m_Root)
        return;

    Widget *hit = m_Root->HitTest(pt);
    if (!hit || !hit->IsEnabled())
        return;

    if (m_Pressed && m_Pressed != hit)
        m_Pressed->SetState(StatePressed, FALSE);
    m_Pressed = hit;
    m_Capture = hit;
    SetHover(hit);
    hit->SetState(StatePressed, TRUE);
    if (hit->IsFocusable())
        SetFocus(hit);
    hit->OnPointerDown(pt);
}

void InputRouter::PointerUp(POINT pt)
{
    Widget *pressed = m_Pressed;

    m_Capture = NULL;
    m_Pressed = NULL;
    if (pressed)
    {
        pressed->SetState(StatePressed, FALSE);
        pressed->OnPointerUp(pt);
        if (m_Root && m_Root->IsAncestorOf(pressed) && m_Root->HitTest(pt) == pressed && pressed->IsEnabled())
            pressed->OnClick();
    }
    PointerMove(pt);
}

BOOL InputRouter::SetFocus(Widget *widget)
{
    LUI_ASSERT(!widget || (m_Root && m_Root->IsAncestorOf(widget)));
    if (widget && (!widget->IsFocusable() || !widget->IsVisibleInTree()))
        return FALSE;
    if (widget == m_Focus)
        return TRUE;

    if (m_Focus)
        m_Focus->SetState(StateFocused, FALSE);
    m_Focus = widget;
    if (widget)
        widget->SetState(StateFocused, TRUE);

    IWidgetHost *host = m_Root ? m_Root->Host() : NULL;
    if (host)
        host->OnFocusChanged(widget);
    return TRUE;
}

BOOL InputRouter::MoveFocusInOrder(BOOL forward)
{
    Widget *next = NextInTree(m_Root, m_Focus, forward);
    return next ? SetFocus(next) : FALSE;
}

static BOOL ArrowMove(UINT vk, FocusMove *move)
{
    switch (vk)
    {
        case VK_LEFT:
            *move = FocusMove::Left;
            return TRUE;
        case VK_RIGHT:
            *move = FocusMove::Right;
            return TRUE;
        case VK_UP:
            *move = FocusMove::Up;
            return TRUE;
        case VK_DOWN:
            *move = FocusMove::Down;
            return TRUE;
    }
    return FALSE;
}

BOOL InputRouter::KeyDown(UINT vk, BOOL shift)
{
    FocusMove move;

    if (m_Focus && !m_Focus->IsVisibleInTree())
        SetFocus(NULL);

    if (m_Focus && m_Focus->OnKeyDown(vk))
        return TRUE;

    if (vk == VK_TAB)
        return MoveFocusInOrder(!shift);

    if (ArrowMove(vk, &move))
    {
        if (!m_Focus)
            return MoveFocusInOrder(TRUE);
        for (Widget *from = m_Focus, *p = m_Focus->Parent(); p; from = p, p = p->Parent())
        {
            Widget *target = p->MoveFocus(from, move);
            if (target)
                return SetFocus(target);
        }
        return FALSE;
    }

    if ((vk == VK_RETURN || vk == VK_SPACE) && m_Focus && m_Focus->IsEnabled())
    {
        m_Focus->OnClick();
        return TRUE;
    }
    return FALSE;
}

}
