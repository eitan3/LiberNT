/*
 * PROJECT:     LiberNT Win32k
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Window snap zones and placements shared by win32k and its tests
 * COPYRIGHT:   Copyright 2026 Eitan Hadar <eitanhadar4@gmail.com>
 */

#pragma once

/* FIXME: corner zone not measured on Windows 11; 48 px at 96 DPI is a guess */
#define USER_SNAP_CORNER_96            48u

static __inline BOOL
UserSnapIsQuarter(UINT Edge)
{
    return Edge == HTTOPLEFT || Edge == HTTOPRIGHT || Edge == HTBOTTOMLEFT || Edge == HTBOTTOMRIGHT;
}

static __inline BOOL
UserSnapIsLeft(UINT Edge)
{
    return Edge == HTLEFT || Edge == HTTOPLEFT || Edge == HTBOTTOMLEFT;
}

static __inline UINT
UserSnapZone(const RECT *Work, POINT Pt, ULONG Dpi)
{
    LONG Corner = (LONG)(USER_SNAP_CORNER_96 * (Dpi ? Dpi : 96u) / 96u);

    if (Pt.x <= Work->left || Pt.x >= Work->right - 1)
    {
        BOOL Left = Pt.x <= Work->left;
        if (Pt.y < Work->top + Corner)
            return Left ? HTTOPLEFT : HTTOPRIGHT;
        if (Pt.y >= Work->bottom - Corner)
            return Left ? HTBOTTOMLEFT : HTBOTTOMRIGHT;
        return Left ? HTLEFT : HTRIGHT;
    }
    if (Pt.y <= Work->top)
    {
        if (Pt.x < Work->left + Corner)
            return HTTOPLEFT;
        if (Pt.x >= Work->right - Corner)
            return HTTOPRIGHT;
        return HTTOP;
    }
    return HTNOWHERE;
}

static __inline BOOL
UserSnapZoneHeld(const RECT *Work, UINT Edge, POINT Pt, ULONG Dpi, LONG Slack)
{
    LONG Corner = (LONG)(USER_SNAP_CORNER_96 * (Dpi ? Dpi : 96u) / 96u);
    BOOL Left = UserSnapIsLeft(Edge);
    BOOL OnSide = Left ? Pt.x <= Work->left + Slack : Pt.x >= Work->right - 1 - Slack;
    BOOL NearX = Left ? Pt.x < Work->left + Corner + Slack : Pt.x >= Work->right - Corner - Slack;

    switch (Edge)
    {
        case HTLEFT:
        case HTRIGHT:
            return OnSide;
        case HTTOP:
            return Pt.y <= Work->top + Slack;
        case HTTOPLEFT:
        case HTTOPRIGHT:
            return (OnSide && Pt.y < Work->top + Corner + Slack) || (Pt.y <= Work->top + Slack && NearX);
        case HTBOTTOMLEFT:
        case HTBOTTOMRIGHT:
            return OnSide && Pt.y >= Work->bottom - Corner - Slack;
    }
    return FALSE;
}

static __inline void
UserSnapRect(const RECT *Work, UINT Edge, LONG Width, LONG Height, RECT *Pos)
{
    *Pos = *Work;
    if (Edge == HTTOP)
    {
        Pos->bottom = Pos->top + Height;
        return;
    }
    if (UserSnapIsLeft(Edge))
        Pos->right = Pos->left + Width;
    else
        Pos->left = Pos->right - Width;
    if (Edge == HTBOTTOMLEFT || Edge == HTBOTTOMRIGHT)
        Pos->top = Pos->bottom - Height;
    else
        Pos->bottom = Pos->top + Height;
}

static __inline UINT
UserSnapEdgeFromRect(UINT Side, const RECT *Window, const RECT *Work)
{
    if (Side != HTLEFT && Side != HTRIGHT)
        return Side;
    if ((LONGLONG)(Window->bottom - Window->top) * 4 > (LONGLONG)(Work->bottom - Work->top) * 3)
        return Side;
    if ((LONGLONG)Window->top + Window->bottom < (LONGLONG)Work->top + Work->bottom)
        return Side == HTLEFT ? HTTOPLEFT : HTTOPRIGHT;
    return Side == HTLEFT ? HTBOTTOMLEFT : HTBOTTOMRIGHT;
}

