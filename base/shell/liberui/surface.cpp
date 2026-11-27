/*
 * PROJECT:     LiberNT Liber UI
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Layered top-level window that paints a widget tree with per-pixel alpha
 * COPYRIGHT:   Copyright 2026 Eitan Hadar <eitanhadar4@gmail.com>
 */

#include "precomp.h"

#include <windowsx.h>

namespace LiberUI
{

#ifndef WS_EX_NOREDIRECTIONBITMAP
#define WS_EX_NOREDIRECTIONBITMAP 0x00200000L
#endif

static const WCHAR SurfaceClass[] = L"LiberSurface";

static BOOL RegisterSurfaceClass(HINSTANCE instance, WNDPROC proc)
{
    WNDCLASSEXW wc;

    if (GetClassInfoExW(instance, SurfaceClass, &wc))
        return TRUE;

    ZeroMemory(&wc, sizeof(wc));
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = proc;
    wc.hInstance = instance;
    wc.hCursor = LoadCursorW(NULL, (LPCWSTR)IDC_ARROW);
    wc.lpszClassName = SurfaceClass;
    return RegisterClassExW(&wc) != 0;
}

Surface::Surface()
    : m_hwnd(NULL), m_Dpi(96), m_Tokens(NULL), m_Root(NULL), m_Dc(NULL), m_Bitmap(NULL), m_OldBitmap(NULL),
      m_Bits(NULL), m_LayoutPending(TRUE), m_Tracking(FALSE), m_Gpu(NULL), m_Presents(0)
{
    m_Position.x = m_Position.y = 0;
    m_SizeDip.cx = m_SizeDip.cy = 0;
    m_SizePx.cx = m_SizePx.cy = 0;
    SetRectEmpty(&m_Dirty);
    SetRectEmpty(&m_Placed);
}

Surface::~Surface()
{
    Destroy();
    SetRoot(NULL);
    FreeBuffer();
}

BOOL Surface::Create(HWND owner, LPCWSTR title, DWORD exStyle)
{
    HINSTANCE instance = GetModuleHandleW(NULL);

    LUI_ASSERT(m_hwnd == NULL);
    if (m_hwnd || !RegisterSurfaceClass(instance, WndProc))
        return FALSE;

    m_Gpu = GpuCreate();
    if (m_Gpu)
    {
        m_hwnd = CreateWindowExW(WS_EX_NOREDIRECTIONBITMAP | exStyle, SurfaceClass, title ? title : L"", WS_POPUP, 0, 0,
                                 0, 0, owner, NULL, instance, this);
        if (m_hwnd && !GpuAttach(m_Gpu, m_hwnd))
        {
            DestroyWindow(m_hwnd);
            m_hwnd = NULL;
        }
        if (!m_hwnd)
        {
            GpuDestroy(m_Gpu);
            m_Gpu = NULL;
        }
    }
    if (!m_hwnd)
    {
        m_hwnd = CreateWindowExW(WS_EX_LAYERED | exStyle, SurfaceClass, title ? title : L"", WS_POPUP, 0, 0, 0, 0,
                                 owner, NULL, instance, this);
    }
    if (!m_hwnd)
        return FALSE;

    HDC screen = GetDC(NULL);
    if (screen)
    {
        SetDpi(GetDeviceCaps(screen, LOGPIXELSX));
        ReleaseDC(NULL, screen);
    }
    return TRUE;
}

void Surface::Destroy()
{
    if (m_Gpu)
    {
        GpuDestroy(m_Gpu);
        m_Gpu = NULL;
    }
    SetRectEmpty(&m_Placed);
    if (m_hwnd)
    {
        HWND hwnd = m_hwnd;
        DestroyWindow(hwnd);
        LUI_ASSERT(m_hwnd == NULL);
        m_hwnd = NULL;
    }
}

void Surface::SetDpi(UINT dpi)
{
    LUI_ASSERT(dpi >= 48 && dpi <= 960);
    if (dpi < 48 || dpi > 960 || dpi == m_Dpi)
        return;
    m_Dpi = dpi;
    m_LayoutPending = TRUE;
    SetRect(&m_Dirty, 0, 0, m_SizeDip.cx, m_SizeDip.cy);
}

void Surface::SetTokens(const ThemeTokens *tokens)
{
    m_Tokens = tokens;
    SetRect(&m_Dirty, 0, 0, m_SizeDip.cx, m_SizeDip.cy);
}

void Surface::SetRoot(Widget *root)
{
    if (m_Root == root)
        return;
    if (m_Root)
    {
        m_Input.Forget(m_Root);
        m_Root->SetHost(NULL);
    }
    m_Root = root;
    m_Input.SetRoot(root);
    if (root)
    {
        LUI_ASSERT(root->Parent() == NULL);
        root->SetHost(this);
    }
    m_LayoutPending = TRUE;
    SetRect(&m_Dirty, 0, 0, m_SizeDip.cx, m_SizeDip.cy);
}

void Surface::SetPosition(int x, int y, int widthDip, int heightDip)
{
    LUI_ASSERT(widthDip >= 0 && heightDip >= 0);
    m_Position.x = x;
    m_Position.y = y;
    if (widthDip != m_SizeDip.cx || heightDip != m_SizeDip.cy)
    {
        m_SizeDip.cx = widthDip < 0 ? 0 : widthDip;
        m_SizeDip.cy = heightDip < 0 ? 0 : heightDip;
        m_LayoutPending = TRUE;
        SetRect(&m_Dirty, 0, 0, m_SizeDip.cx, m_SizeDip.cy);
    }
}

POINT Surface::PixelToDip(POINT pt) const
{
    POINT result = { MulDiv(pt.x, 96, (int)m_Dpi), MulDiv(pt.y, 96, (int)m_Dpi) };
    return result;
}

void Surface::InvalidateRect(const RECT &rc)
{
    RECT clipped, all = { 0, 0, m_SizeDip.cx, m_SizeDip.cy };
    if (IntersectRect(&clipped, &rc, &all))
        UnionRect(&m_Dirty, &m_Dirty, &clipped);
}

void Surface::RequestLayout()
{
    m_LayoutPending = TRUE;
}

void Surface::SetCapture(Widget *widget)
{
    m_Input.SetCapture(widget);
    if (m_hwnd)
    {
        if (widget)
            ::SetCapture(m_hwnd);
        else if (GetCapture() == m_hwnd)
            ReleaseCapture();
    }
}

void Surface::OnFocusChanged(Widget *widget)
{
    UNREFERENCED_PARAMETER(widget);
}

void Surface::OnWidgetRemoved(Widget *widget)
{
    m_Input.Forget(widget);
}

void Surface::FreeBuffer()
{
    if (m_Dc)
    {
        if (m_OldBitmap)
            SelectObject(m_Dc, m_OldBitmap);
        DeleteDC(m_Dc);
    }
    if (m_Bitmap)
        DeleteObject(m_Bitmap);
    m_Dc = NULL;
    m_Bitmap = NULL;
    m_OldBitmap = NULL;
    m_Bits = NULL;
    m_SizePx.cx = m_SizePx.cy = 0;
}

BOOL Surface::EnsureBuffer()
{
    SIZE px = { ScaleToDpi(m_SizeDip.cx, m_Dpi), ScaleToDpi(m_SizeDip.cy, m_Dpi) };

    if (px.cx <= 0 || px.cy <= 0)
    {
        FreeBuffer();
        return FALSE;
    }
    if (m_Bits && px.cx == m_SizePx.cx && px.cy == m_SizePx.cy)
        return TRUE;

    FreeBuffer();

    BITMAPINFO info;
    ZeroMemory(&info, sizeof(info));
    info.bmiHeader.biSize = sizeof(info.bmiHeader);
    info.bmiHeader.biWidth = px.cx;
    info.bmiHeader.biHeight = -px.cy;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;

    m_Dc = CreateCompatibleDC(NULL);
    if (!m_Dc)
        return FALSE;
    m_Bitmap = CreateDIBSection(m_Dc, &info, DIB_RGB_COLORS, (void **)&m_Bits, NULL, 0);
    if (!m_Bitmap || !m_Bits)
    {
        FreeBuffer();
        return FALSE;
    }
    m_OldBitmap = SelectObject(m_Dc, m_Bitmap);
    m_SizePx = px;
    ZeroMemory(m_Bits, (SIZE_T)px.cx * px.cy * sizeof(DWORD));
    SetRect(&m_Dirty, 0, 0, m_SizeDip.cx, m_SizeDip.cy);
    return TRUE;
}

void Surface::Layout()
{
    if (!m_Root)
    {
        m_LayoutPending = FALSE;
        return;
    }

    SIZE available = m_SizeDip;
    RECT slot = { 0, 0, m_SizeDip.cx, m_SizeDip.cy };
    m_Root->Measure(available);
    m_Root->Arrange(slot);
    m_LayoutPending = FALSE;
}

BOOL Surface::Render(ULONGLONG now)
{
    if (!EnsureBuffer())
        return FALSE;
    if (m_LayoutPending)
        Layout();
    if (IsRectEmpty(&m_Dirty))
        return TRUE;

    const ThemeTokens *tokens = m_Tokens;
    ThemeTokens fallback;
    if (!tokens)
        tokens = &fallback;

    RECT dirty = m_Dirty;
    SetRectEmpty(&m_Dirty);

    GdiplusPainter painter(m_Bits, m_SizePx.cx, m_SizePx.cy, m_SizePx.cx * 4, m_Dpi, &m_Fonts);
    if (!painter.IsValid())
        return FALSE;

    RectF clip = ToRectF(dirty);
    painter.PushClip(clip, 0.0f);
    painter.Clear(0);
    if (m_Root)
    {
        PaintContext context = { painter, *tokens, now };
        m_Root->Paint(context);
    }
    painter.PopClip();

    Present();
    return TRUE;
}

void Surface::Present()
{
    if (!m_hwnd || !m_Dc)
        return;

    POINT source = { 0, 0 };
    POINT position = m_Position;
    SIZE size = m_SizePx;
    BLENDFUNCTION blend = { AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };

    if (!m_Gpu)
    {
        if (UpdateLayeredWindow(m_hwnd, NULL, &position, &size, m_Dc, &source, 0, &blend, ULW_ALPHA))
            m_Presents++;
        return;
    }

    RECT placed = { position.x, position.y, position.x + size.cx, position.y + size.cy };
    if (!EqualRect(&placed, &m_Placed))
    {
        SetWindowPos(m_hwnd, NULL, placed.left, placed.top, size.cx, size.cy,
                     SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOOWNERZORDER);
        m_Placed = placed;
    }
    if (!GpuPresent(m_Gpu, m_Bits, (UINT)size.cx, (UINT)size.cy))
    {
        GpuDestroy(m_Gpu);
        m_Gpu = GpuCreate();
        if (m_Gpu && !GpuAttach(m_Gpu, m_hwnd))
        {
            GpuDestroy(m_Gpu);
            m_Gpu = NULL;
        }
        if (!m_Gpu || !GpuPresent(m_Gpu, m_Bits, (UINT)size.cx, (UINT)size.cy))
            return;
    }
    m_Presents++;
}

LRESULT CALLBACK Surface::WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    Surface *self;

    if (msg == WM_NCCREATE)
    {
        self = (Surface *)((LPCREATESTRUCTW)lParam)->lpCreateParams;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)self);
        self->m_hwnd = hwnd;
    }
    else
    {
        self = (Surface *)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    }

    if (!self)
        return DefWindowProcW(hwnd, msg, wParam, lParam);

    LRESULT result = self->HandleMessage(msg, wParam, lParam);
    if (msg == WM_NCDESTROY)
    {
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
        self->m_hwnd = NULL;
    }
    return result;
}

LRESULT Surface::HandleMessage(UINT msg, WPARAM wParam, LPARAM lParam)
{
    POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };

    switch (msg)
    {
        case WM_MOUSEMOVE:
            if (!m_Tracking)
            {
                TRACKMOUSEEVENT track = { sizeof(track), TME_LEAVE, m_hwnd, 0 };
                m_Tracking = TrackMouseEvent(&track);
            }
            m_Input.PointerMove(PixelToDip(pt));
            Render(AnimationClock::Now());
            return 0;

        case WM_MOUSELEAVE:
            m_Tracking = FALSE;
            m_Input.PointerLeave();
            Render(AnimationClock::Now());
            return 0;

        case WM_LBUTTONDOWN:
            m_Input.PointerDown(PixelToDip(pt));
            if (m_Input.Capture())
                ::SetCapture(m_hwnd);
            Render(AnimationClock::Now());
            return 0;

        case WM_LBUTTONUP:
            if (GetCapture() == m_hwnd)
                ReleaseCapture();
            m_Input.PointerUp(PixelToDip(pt));
            Render(AnimationClock::Now());
            return 0;

        case WM_CAPTURECHANGED:
            if ((HWND)lParam != m_hwnd && m_Input.Capture())
            {
                POINT outside = { -1, -1 };
                m_Input.PointerUp(outside);
                Render(AnimationClock::Now());
            }
            return 0;

        case WM_KEYDOWN:
            if (m_Input.KeyDown((UINT)wParam, GetKeyState(VK_SHIFT) < 0))
            {
                Render(AnimationClock::Now());
                return 0;
            }
            break;

        case WM_DPICHANGED:
            SetDpi(HIWORD(wParam));
            Render(AnimationClock::Now());
            return 0;

        case WM_PAINT:
        {
            PAINTSTRUCT ps;
            BeginPaint(m_hwnd, &ps);
            EndPaint(m_hwnd, &ps);
            return 0;
        }
    }
    return DefWindowProcW(m_hwnd, msg, wParam, lParam);
}

}
