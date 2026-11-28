/*
 * COPYRIGHT:        See COPYING in the top level directory
 * PROJECT:          ReactOS Win32k subsystem
 * PURPOSE:          Window snap preview animation
 * FILE:             win32ss/user/ntuser/wndsnap.c
 * PROGRAMER:
 */

#include <win32k.h>
#include <libernt/winsnap.h>

DBG_DEFAULT_CHANNEL(UserWinpos);

/*
 * The snap preview is an ordinary layered popup. The compositor owns its
 * pixels, its translucency and its grow animation, so nothing here draws on
 * the screen, saves what is underneath, or has to repaint on a timer. It is
 * created on the dragging thread, from a server-side class, so showing it
 * never calls back to user mode.
 */
#define SNAP_PREVIEW_ALPHA        110
#define SNAP_PREVIEW_BORDER_WIDTH 2
#define SNAP_PREVIEW_ORIGIN_HALF  10
#define SNAP_PREVIEW_FILL         RGB(176, 214, 244)
#define SNAP_PREVIEW_BORDER       RGB(94, 150, 214)
#define SNAP_PREVIEW_LIBER_PIXEL  0x80808080u

static struct
{
    HWND hwnd;      /* Preview popup, kept hidden between snap zones */
    UINT Edge;      /* Armed edge, HTNOWHERE when nothing is armed */
    RECT rcTarget;  /* Where the window lands if the button is released */
} gSnapPreview = { NULL, HTNOWHERE, { 0, 0, 0, 0 } };

static PWND
IntSnapPreviewWindow(VOID)
{
    PWND pwnd;

    if (gSnapPreview.hwnd == NULL)
        return NULL;

    pwnd = UserGetWindowObject(gSnapPreview.hwnd);
    if (pwnd == NULL || pwnd->head.pti != PsGetCurrentThreadWin32Thread())
    {
        gSnapPreview.hwnd = NULL;
        return NULL;
    }
    return pwnd;
}

static BOOL
IntSnapPreviewPaintGlass(HDC hdc, LONG cx, LONG cy)
{
    ULONG Pixel = SNAP_PREVIEW_LIBER_PIXEL;
    HBITMAP hbm, hbmOld;
    HDC hdcMem;
    BOOL Done = FALSE;

    hbm = GreCreateBitmap(1, 1, 1, 32, &Pixel);
    if (hbm == NULL)
        return FALSE;
    hdcMem = NtGdiCreateCompatibleDC(hdc);
    if (hdcMem != NULL)
    {
        hbmOld = NtGdiSelectBitmap(hdcMem, hbm);
        if (hbmOld != NULL)
        {
            Done = NtGdiStretchBlt(hdc, 0, 0, cx, cy, hdcMem, 0, 0, 1, 1, SRCCOPY, CLR_INVALID);
            NtGdiSelectBitmap(hdcMem, hbmOld);
        }
        GreDeleteObject(hdcMem);
    }
    GreDeleteObject(hbm);
    return Done;
}

static VOID
IntSnapPreviewPaint(PWND pwnd)
{
    LONG cx = pwnd->rcWindow.right - pwnd->rcWindow.left;
    LONG cy = pwnd->rcWindow.bottom - pwnd->rcWindow.top;
    LONG bw = min(SNAP_PREVIEW_BORDER_WIDTH, cx / 2);
    LONG bh = min(SNAP_PREVIEW_BORDER_WIDTH, cy / 2);
    HBRUSH hbrFill, hbrBorder, hbrOld;
    HDC hdc;

    hdc = UserGetWindowDC(pwnd);
    if (hdc == NULL)
        return;

    if (g_bLiberExperience && IntSnapPreviewPaintGlass(hdc, cx, cy))
    {
        UserReleaseDC(pwnd, hdc, FALSE);
        return;
    }

    hbrFill = IntGdiCreateSolidBrush(SNAP_PREVIEW_FILL);
    hbrBorder = IntGdiCreateSolidBrush(SNAP_PREVIEW_BORDER);
    if (hbrFill && hbrBorder)
    {
        hbrOld = NtGdiSelectBrush(hdc, hbrFill);
        NtGdiPatBlt(hdc, 0, 0, cx, cy, PATCOPY);

        NtGdiSelectBrush(hdc, hbrBorder);
        NtGdiPatBlt(hdc, 0, 0, cx, bh, PATCOPY);
        NtGdiPatBlt(hdc, 0, cy - bh, cx, bh, PATCOPY);
        NtGdiPatBlt(hdc, 0, bh, bw, cy - 2 * bh, PATCOPY);
        NtGdiPatBlt(hdc, cx - bw, bh, bw, cy - 2 * bh, PATCOPY);
        NtGdiSelectBrush(hdc, hbrOld);
    }

    if (hbrFill)
        GreDeleteObject(hbrFill);
    if (hbrBorder)
        GreDeleteObject(hbrBorder);

    UserReleaseDC(pwnd, hdc, FALSE);
}

static PWND
co_IntSnapPreviewCreate(PWND pwndDrag, const RECT *prc)
{
    UNICODE_STRING ClassName;
    LARGE_STRING WindowName;
    CREATESTRUCTW Cs;
    PWND pwnd;

    /* Do not borrow the built-in Message class here. Its shared-heap base can
     * still be referenced by message-only infrastructure, and asking the class
     * manager to migrate that live base to a desktop heap is invalid. The
     * private class uses the same server-side procedure without sharing its
     * lifetime. */
    RtlInitUnicodeString(&ClassName, SNAP_PREVIEW_CLASS_NAME);

    RtlZeroMemory(&WindowName, sizeof(WindowName));
    RtlZeroMemory(&Cs, sizeof(Cs));
    Cs.x = prc->left;
    Cs.y = prc->top;
    Cs.cx = prc->right - prc->left;
    Cs.cy = prc->bottom - prc->top;
    Cs.style = WS_POPUP;
    Cs.dwExStyle = WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE;
    if (pwndDrag->ExStyle & WS_EX_TOPMOST)
        Cs.dwExStyle |= WS_EX_TOPMOST;
    Cs.hInstance = hModClient; /* Server side winproc */
    Cs.lpszName = (LPCWSTR)&WindowName;
    Cs.lpszClass = (LPCWSTR)&ClassName;

    pwnd = co_UserCreateWindowEx(&Cs, &ClassName, &WindowName, NULL, WINVER, NULL);
    if (pwnd == NULL)
    {
        ERR("Could not create the snap preview window\n");
        return NULL;
    }

    IntSetLayeredWindowAttributes(pwnd, 0, SNAP_PREVIEW_ALPHA, LWA_ALPHA);
    if (g_bLiberExperience && AtomDwmLiberMaterial != 0 && AtomDwmCornerRadius != 0)
    {
        ULONG Dpi = (gpsi && gpsi->dmLogPixels) ? gpsi->dmLogPixels : 96;
        ULONG Material = DWM_LIBER_MATERIAL_PACK(DWM_LIBER_MATERIAL_ULTRATHIN, FALSE, DWM_LIBER_LAYER_SHELL, 0,
                                                 DWM_LIBER_EDGE_GLASS, 0);
        ASSERT((Material & ~DWM_LIBER_MATERIAL_MASK) == 0);
        UserSetProp(pwnd, AtomDwmLiberMaterial, (HANDLE)(ULONG_PTR)Material, FALSE);
        UserSetProp(pwnd, AtomDwmCornerRadius, (HANDLE)(ULONG_PTR)DwmLiberWindowCornerRadius(Dpi), FALSE);
    }
    gSnapPreview.hwnd = UserHMGetHandle(pwnd);
    return pwnd;
}

/* --- Public interface --- */

UINT FASTCALL
IntSnapPreviewEdge(VOID)
{
    return gSnapPreview.Edge;
}

/*
 * Arm Edge and show where the window will land. Nothing happens when the
 * same edge and target are already armed, which is the case for nearly
 * every mouse message while the pointer slides along a screen edge.
 */
VOID FASTCALL
co_IntSnapPreviewShow(PWND pwndDrag, UINT Edge, const RECT *prcTarget, POINT ptCursor)
{
    USER_REFERENCE_ENTRY Ref;
    RECTL rcFrom;
    PWND pwnd;

    if (gSnapPreview.Edge == Edge &&
        RtlEqualMemory(&gSnapPreview.rcTarget, prcTarget, sizeof(*prcTarget)))
    {
        return;
    }

    gSnapPreview.Edge = Edge;
    gSnapPreview.rcTarget = *prcTarget;

    /* Without a compositor the edge stays armed but there is no surface that
     * could show a translucent preview without fighting the window below. */
    if (!IntCompositionIsEnabled())
        return;

    pwnd = IntSnapPreviewWindow();
    if (pwnd == NULL)
        pwnd = co_IntSnapPreviewCreate(pwndDrag, prcTarget);
    if (pwnd == NULL)
        return;

    UserRefObjectCo(pwnd, &Ref);

    /* Directly below the dragged window, so the window passes over it */
    co_WinPosSetWindowPos(pwnd,
                          UserHMGetHandle(pwndDrag),
                          prcTarget->left,
                          prcTarget->top,
                          prcTarget->right - prcTarget->left,
                          prcTarget->bottom - prcTarget->top,
                          SWP_NOACTIVATE | SWP_SHOWWINDOW);
    IntSnapPreviewPaint(pwnd);

    /* The compositor keeps a window out of the scene while its first WM_PAINT
     * is outstanding, and a thread busy dragging may not dispatch one for the
     * better part of a second. The contents are complete, so say so. */
    co_UserRedrawWindow(pwnd, NULL, NULL,
                        RDW_VALIDATE | RDW_NOFRAME | RDW_NOERASE |
                        RDW_NOINTERNALPAINT | RDW_ALLCHILDREN);

    rcFrom.left   = max(ptCursor.x - SNAP_PREVIEW_ORIGIN_HALF, prcTarget->left);
    rcFrom.top    = max(ptCursor.y - SNAP_PREVIEW_ORIGIN_HALF, prcTarget->top);
    rcFrom.right  = min(ptCursor.x + SNAP_PREVIEW_ORIGIN_HALF, prcTarget->right);
    rcFrom.bottom = min(ptCursor.y + SNAP_PREVIEW_ORIGIN_HALF, prcTarget->bottom);
    IntCompositionAnimateMove(pwnd, &rcFrom);

    UserDerefObjectCo(pwnd);
}

VOID FASTCALL
co_IntSnapPreviewHide(VOID)
{
    USER_REFERENCE_ENTRY Ref;
    PWND pwnd;

    gSnapPreview.Edge = HTNOWHERE;
    RECTL_vSetEmptyRect(&gSnapPreview.rcTarget);

    pwnd = IntSnapPreviewWindow();
    if (pwnd == NULL || !(pwnd->style & WS_VISIBLE))
        return;

    UserRefObjectCo(pwnd, &Ref);
    co_WinPosSetWindowPos(pwnd, NULL, 0, 0, 0, 0,
                          SWP_HIDEWINDOW | SWP_NOMOVE | SWP_NOSIZE |
                          SWP_NOZORDER | SWP_NOACTIVATE);
    UserDerefObjectCo(pwnd);
}

VOID FASTCALL
co_IntSnapPreviewDestroy(VOID)
{
    PWND pwnd;

    gSnapPreview.Edge = HTNOWHERE;
    RECTL_vSetEmptyRect(&gSnapPreview.rcTarget);

    pwnd = IntSnapPreviewWindow();
    gSnapPreview.hwnd = NULL;
    if (pwnd != NULL)
        co_UserDestroyWindow(pwnd);
}

/* --- Window snap logic --- */

UINT
GetSnapActivationPoint(PWND Wnd, POINT pt)
{
    // TODO: SPI_GETMOUSEDOCKTHRESHOLD
    RECT wa;
    if (!GetSnapSetting(bDockMoving))
        return HTNOWHERE;
    UserSystemParametersInfo(SPI_GETWORKAREA, 0, &wa, 0); /* FIXME: MultiMon of PWND */

    return UserSnapZone(&wa, pt, (gpsi && gpsi->dmLogPixels) ? gpsi->dmLogPixels : 96);
}

BOOL
IsPointHoldingSnapEdge(UINT Edge, POINT pt)
{
    RECT wa;

    if (Edge == HTNOWHERE)
        return FALSE;

    UserSystemParametersInfo(SPI_GETWORKAREA, 0, &wa, 0); /* FIXME: MultiMon of PWND */

    return UserSnapZoneHeld(&wa, Edge, pt, (gpsi && gpsi->dmLogPixels) ? gpsi->dmLogPixels : 96, SNAP_RELEASE_SLACK);
}

/* Windows 10 (1903?)
BOOL APIENTRY
NtUserIsWindowArranged(HWND hWnd)
{
    PWND pwnd = UserGetWindowObject(hWnd);
    return pwnd && IntIsWindowSnapped(pwnd);
}
*/

UINT FASTCALL
IntGetWindowSnapEdge(PWND Wnd)
{
    UINT Side;
    RECT wa;

    if (Wnd->ExStyle2 & WS_EX2_VERTICALLYMAXIMIZEDLEFT)
        Side = HTLEFT;
    else if (Wnd->ExStyle2 & WS_EX2_VERTICALLYMAXIMIZEDRIGHT)
        Side = HTRIGHT;
    else
        return HTNOWHERE;
    /* FIXME: Windows 11 keeps a quarter snap in state LiberNT has no field for, so it is read back
     * from the window rect, and a minimized quarter restores as a half */
    if (Wnd->style & WS_MINIMIZE)
        return Side;
    UserSystemParametersInfo(SPI_GETWORKAREA, 0, &wa, 0); /* FIXME: MultiMon of PWND */
    return UserSnapEdgeFromRect(Side, &Wnd->rcWindow, &wa);
}

VOID FASTCALL
co_IntCalculateSnapPosition(PWND Wnd, UINT Edge, OUT RECT *Pos)
{
    POINT maxs, mint, maxt;
    UINT width, height;
    UserSystemParametersInfo(SPI_GETWORKAREA, 0, Pos, 0); /* FIXME: MultiMon of PWND */

    co_WinPosGetMinMaxInfo(Wnd, &maxs, NULL, &mint, &maxt);
    width = Pos->right - Pos->left;
    width = min(min(max(width / 2, mint.x), maxt.x), width);
    height = Pos->bottom - Pos->top;
    if (UserSnapIsQuarter(Edge))
        height = min(min(max(height / 2, mint.y), maxt.y), height);
    else
        height = min(max(height, mint.y), maxt.y);

    switch (Edge)
    {
    case HTTOP: /* Maximized (Calculate RECT snap preview for SC_MOVE) */
        height = min(Pos->bottom - Pos->top, maxs.y);
        break;
    case HTLEFT:
    case HTRIGHT:
    case HTTOPLEFT:
    case HTTOPRIGHT:
    case HTBOTTOMLEFT:
    case HTBOTTOMRIGHT:
        break;
    default:
        ERR("Unexpected snap edge %#x\n", Edge);
        Pos->bottom = Pos->top + height;
        return;
    }
    UserSnapRect(Pos, Edge, width, height, Pos);
}

VOID FASTCALL
co_IntSnapWindow(PWND Wnd, UINT Edge)
{
    RECT newPos;
    RECT oldPos = Wnd->rcWindow;
    BOOLEAN wasSnapped = IntIsWindowSnapped(Wnd);
    UINT normal = !(Wnd->style & (WS_MAXIMIZE | WS_MINIMIZE));
    USER_REFERENCE_ENTRY ref;
    BOOLEAN hasRef = FALSE;

    if (Edge == HTTOP)
    {
        co_IntSendMessage(UserHMGetHandle(Wnd), WM_SYSCOMMAND, SC_MAXIMIZE, 0);
        return;
    }
    else if (Edge != HTNOWHERE)
    {
        UserRefObjectCo(Wnd, &ref);
        hasRef = TRUE;
        co_IntCalculateSnapPosition(Wnd, Edge, &newPos);
        IntSetSnapInfo(Wnd, Edge, (wasSnapped || !normal) ? NULL : &Wnd->rcWindow);
    }
    else if (wasSnapped)
    {
        if (!normal)
        {
            IntSetSnapEdge(Wnd, HTNOWHERE);
            return;
        }
        newPos = Wnd->InternalPos.NormalRect; /* Copy RECT now before it is lost */
        IntSetSnapInfo(Wnd, HTNOWHERE, NULL);
    }
    else
    {
        return; /* Already unsnapped, do nothing */
    }

    TRACE("WindowSnap: %d->%d\n", IntGetWindowSnapEdge(Wnd), Edge);
    co_WinPosSetWindowPos(Wnd, HWND_TOP,
                          newPos.left,
                          newPos.top,
                          newPos.right - newPos.left,
                          newPos.bottom - newPos.top,
                          0);
    IntCompositionAnimateMove(Wnd, (const RECTL *)&oldPos);
    if (hasRef)
        UserDerefObjectCo(Wnd);
}

VOID FASTCALL
IntSetSnapEdge(PWND Wnd, UINT Edge)
{
    UINT styleMask = WS_EX2_VERTICALLYMAXIMIZEDLEFT | WS_EX2_VERTICALLYMAXIMIZEDRIGHT;
    UINT style = 0;
    switch (Edge)
    {
    case HTNOWHERE:
        style = 0;
        break;
    case HTTOP: /* Maximize throws away the snap */
        style = 0;
        break;
    case HTLEFT:
    case HTTOPLEFT:
    case HTBOTTOMLEFT:
        style = WS_EX2_VERTICALLYMAXIMIZEDLEFT;
        break;
    case HTRIGHT:
    case HTTOPRIGHT:
    case HTBOTTOMRIGHT:
        style = WS_EX2_VERTICALLYMAXIMIZEDRIGHT;
        break;
    default:
        ERR("Unexpected snap edge %#x\n", Edge);
    }
    Wnd->ExStyle2 = (Wnd->ExStyle2 & ~styleMask) | style;
}

VOID FASTCALL
IntSetSnapInfo(PWND Wnd, UINT Edge, IN const RECT *Pos OPTIONAL)
{
    RECT r;
    IntSetSnapEdge(Wnd, Edge);
    if (Edge == HTNOWHERE)
    {
        RECTL_vSetEmptyRect(&r);
        Pos = (Wnd->style & WS_MINIMIZE) ? NULL : &r;
    }
    if (Pos)
    {
        Wnd->InternalPos.NormalRect = *Pos;
    }
}
