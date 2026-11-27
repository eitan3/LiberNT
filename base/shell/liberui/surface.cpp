/*
 * PROJECT:     LiberNT Liber UI
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Layered top-level window that paints a widget tree with per-pixel alpha
 * COPYRIGHT:   Copyright 2026 Eitan Hadar <eitanhadar4@gmail.com>
 */

#include "precomp.h"
#include "gpu.h"

#include <windowsx.h>
#include <strsafe.h>

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
      m_Bits(NULL), m_LayoutPending(TRUE), m_Tracking(FALSE), m_Gpu(NULL), m_Presents(0), m_Owner(NULL), m_ExStyle(0),
      m_InputRadius(0.0f), m_Recovering(FALSE)
{
    m_Title[0] = L'\0';
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

BOOL Surface::CreateWindowFor(BOOL gpu)
{
    HINSTANCE instance = GetModuleHandleW(NULL);

    LUI_ASSERT(m_hwnd == NULL && m_Gpu == NULL);
    if (gpu)
    {
        m_Gpu = GpuCreate();
        if (m_Gpu)
        {
            CreateWindowExW(WS_EX_NOREDIRECTIONBITMAP | m_ExStyle, SurfaceClass, m_Title, WS_POPUP, 0, 0, 0, 0,
                            m_Owner, NULL, instance, this);
            if (m_hwnd && !GpuAttach(m_Gpu, m_hwnd))
                DestroyWindow(m_hwnd);
            if (!m_hwnd)
            {
                GpuDestroy(m_Gpu);
                m_Gpu = NULL;
            }
        }
    }
    if (!m_hwnd)
    {
        CreateWindowExW(WS_EX_LAYERED | m_ExStyle, SurfaceClass, m_Title, WS_POPUP, 0, 0, 0, 0, m_Owner, NULL,
                        instance, this);
    }
    LUI_ASSERT(!m_Gpu || m_hwnd);
    return m_hwnd != NULL;
}

BOOL Surface::Create(HWND owner, LPCWSTR title, DWORD exStyle, DWORD flags)
{
    LUI_ASSERT(m_hwnd == NULL);
    if (m_hwnd || !RegisterSurfaceClass(GetModuleHandleW(NULL), WndProc))
        return FALSE;

    m_Owner = owner;
    m_ExStyle = exStyle;
    StringCchCopyW(m_Title, _countof(m_Title), title ? title : L"");
    if (!CreateWindowFor(!(flags & SurfaceNoGpu)))
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

void Surface::SetInputShape(float radiusDip)
{
    LUI_ASSERT(radiusDip >= 0.0f);
    if (radiusDip < 0.0f)
        radiusDip = 0.0f;
    if (radiusDip == m_InputRadius)
        return;
    m_InputRadius = radiusDip;
    SetRectEmpty(&m_Placed);
}

void Surface::ApplyInputShape()
{
    HRGN region = NULL;
    RECT box;

    if (!m_hwnd || (m_InputRadius <= 0.0f && GetWindowRgnBox(m_hwnd, &box) == ERROR))
        return;
    if (m_InputRadius > 0.0f && m_SizePx.cx > 0 && m_SizePx.cy > 0)
    {
        int diameter = (int)(m_InputRadius * (float)m_Dpi / 96.0f * 2.0f) - 2;
        diameter = max(0, min(diameter, (int)min(m_SizePx.cx, m_SizePx.cy)));
        region = CreateRoundRectRgn(0, 0, m_SizePx.cx + 1, m_SizePx.cy + 1, diameter, diameter);
        if (!region)
            return;
    }
    if (!SetWindowRgn(m_hwnd, region, FALSE) && region)
        DeleteObject(region);
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
    if (m_Gpu)
    {
        BOOL recreated;
        if (!GpuEnsureCanvas(m_Gpu, (UINT)px.cx, (UINT)px.cy, &recreated))
            return FALSE;
        if (recreated || px.cx != m_SizePx.cx || px.cy != m_SizePx.cy)
        {
            m_SizePx = px;
            SetRect(&m_Dirty, 0, 0, m_SizeDip.cx, m_SizeDip.cy);
        }
        return TRUE;
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

BOOL Surface::RenderPass(ULONGLONG now, BOOL *lost)
{
    *lost = FALSE;
    if (!EnsureBuffer())
    {
        *lost = GpuDeviceLost(m_Gpu);
        return FALSE;
    }
    if (m_LayoutPending)
        Layout();
    if (IsRectEmpty(&m_Dirty))
    {
        RECT placed = { m_Position.x, m_Position.y, m_Position.x + m_SizePx.cx, m_Position.y + m_SizePx.cy };
        if (!EqualRect(&placed, &m_Placed) && !Present())
            *lost = GpuDeviceLost(m_Gpu);
        return !*lost;
    }

    const ThemeTokens *tokens = m_Tokens;
    ThemeTokens fallback;
    if (!tokens)
        tokens = &fallback;

    RECT dirty = m_Dirty;

    if (m_Gpu)
    {
        RECT px = { MulDiv(dirty.left, (int)m_Dpi, 96) - 1, MulDiv(dirty.top, (int)m_Dpi, 96) - 1,
                    MulDiv(dirty.right, (int)m_Dpi, 96) + 1, MulDiv(dirty.bottom, (int)m_Dpi, 96) + 1 };
        RECT all = { 0, 0, m_SizePx.cx, m_SizePx.cy };
        IntersectRect(&px, &px, &all);

        D3DPainter painter(m_Gpu, m_Dpi, &m_Fonts, px);
        if (!painter.IsValid())
        {
            *lost = GpuDeviceLost(m_Gpu);
            return FALSE;
        }
        SetRectEmpty(&m_Dirty);
        painter.Clear(0);
        if (m_Root)
        {
            PaintContext context = { painter, *tokens, now };
            m_Root->Paint(context);
        }
    }
    else
    {
        GdiplusPainter painter(m_Bits, m_SizePx.cx, m_SizePx.cy, m_SizePx.cx * 4, m_Dpi, &m_Fonts);
        if (!painter.IsValid())
            return FALSE;
        SetRectEmpty(&m_Dirty);

        RectF clip = ToRectF(dirty);
        painter.PushClip(clip, 0.0f);
        painter.Clear(0);
        if (m_Root)
        {
            PaintContext context = { painter, *tokens, now };
            m_Root->Paint(context);
        }
        painter.PopClip();
    }

    if (GpuDeviceLost(m_Gpu) || (!Present() && GpuDeviceLost(m_Gpu)))
    {
        *lost = TRUE;
        return FALSE;
    }
    return TRUE;
}

BOOL Surface::Render(ULONGLONG now)
{
    BOOL lost;

    if (RenderPass(now, &lost))
        return TRUE;
    if (!lost || !RecreateGpu())
        return FALSE;
    return RenderPass(now, &lost);
}

BOOL Surface::RecreateGpu()
{
    LUI_ASSERT(!m_Recovering);
    if (!m_hwnd || !m_Gpu || m_Recovering)
        return FALSE;

    m_Recovering = TRUE;
    GpuDestroy(m_Gpu);
    m_Gpu = GpuCreate();
    if (m_Gpu && !GpuAttach(m_Gpu, m_hwnd))
    {
        GpuDestroy(m_Gpu);
        m_Gpu = NULL;
    }
    if (!m_Gpu)
    {
        BOOL visible = IsWindowVisible(m_hwnd);
        HWND old = m_hwnd;
        DestroyWindow(old);
        LUI_ASSERT(m_hwnd == NULL);
        m_hwnd = NULL;
        m_Tracking = FALSE;
        if (CreateWindowFor(FALSE) && visible)
            ShowWindow(m_hwnd, SW_SHOWNOACTIVATE);
    }
    m_SizePx.cx = m_SizePx.cy = 0;
    SetRectEmpty(&m_Placed);
    SetRect(&m_Dirty, 0, 0, m_SizeDip.cx, m_SizeDip.cy);
    m_Recovering = FALSE;
    return m_hwnd != NULL;
}

BOOL Surface::ReadPixels(DWORD *pixels, UINT stride) const
{
    if (!pixels || m_SizePx.cx <= 0 || stride < (UINT)m_SizePx.cx * sizeof(DWORD))
        return FALSE;
    if (m_Gpu)
        return GpuReadCanvas(m_Gpu, pixels, stride);
    if (!m_Bits)
        return FALSE;
    for (LONG y = 0; y < m_SizePx.cy; y++)
        CopyMemory((BYTE *)pixels + (SIZE_T)y * stride, m_Bits + (SIZE_T)y * m_SizePx.cx, m_SizePx.cx * sizeof(DWORD));
    return TRUE;
}

BOOL Surface::Present()
{
    if (!m_hwnd || (m_Gpu ? !GpuHasCanvas(m_Gpu) : !m_Dc))
        return FALSE;

    POINT source = { 0, 0 };
    POINT position = m_Position;
    SIZE size = m_SizePx;
    BLENDFUNCTION blend = { AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };
    RECT placed = { position.x, position.y, position.x + size.cx, position.y + size.cy };
    BOOL reshaped = m_Placed.right - m_Placed.left != size.cx || m_Placed.bottom - m_Placed.top != size.cy ||
                    IsRectEmpty(&m_Placed);

    if (!m_Gpu)
    {
        if (!UpdateLayeredWindow(m_hwnd, NULL, &position, &size, m_Dc, &source, 0, &blend, ULW_ALPHA))
            return FALSE;
    }
    else
    {
        if (!EqualRect(&placed, &m_Placed))
        {
            SetWindowPos(m_hwnd, NULL, placed.left, placed.top, size.cx, size.cy,
                         SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOOWNERZORDER);
        }
        if (!GpuPresentCanvas(m_Gpu))
            return FALSE;
    }
    if (reshaped)
        ApplyInputShape();
    m_Placed = placed;
    m_Presents++;
    return TRUE;
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
            m_Input.PointerUp(PixelToDip(pt));
            if (GetCapture() == m_hwnd)
                ReleaseCapture();
            Render(AnimationClock::Now());
            return 0;

        case WM_WINDOWPOSCHANGED:
            if (((const WINDOWPOS *)lParam)->flags & SWP_SHOWWINDOW)
                Present();
            break;

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
