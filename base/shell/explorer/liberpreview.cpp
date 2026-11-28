/*
 * PROJECT:     LiberNT Explorer
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Liber dock window previews
 * COPYRIGHT:   Copyright 2026 Eitan Hadar <eitanhadar4@gmail.com>
 */

#include "precomp.h"

#include "libershell.h"
#include "liberdock.h"

using namespace LiberUI;

#define LIBER_PREVIEW_PAD     8
#define LIBER_PREVIEW_TITLE_H 28
#define WM_LIBER_PREVIEW_DONE (WM_APP + 1)

typedef struct _LIBER_PREVIEW_CAPTURE
{
    HWND Sink;
    ULONG Generation;
    UINT Count;
    HWND Windows[LIBER_PREVIEW_MAX];
    HICON Thumbs[LIBER_PREVIEW_MAX];
    SIZE Sizes[LIBER_PREVIEW_MAX];
} LIBER_PREVIEW_CAPTURE;

static HICON CaptureThumbnail(HWND hwnd, SIZE *pSize);

static DWORD WINAPI CaptureThread(LPVOID param)
{
    LIBER_PREVIEW_CAPTURE *capture = (LIBER_PREVIEW_CAPTURE *)param;

    ATLASSERT(capture && capture->Count <= LIBER_PREVIEW_MAX);
    for (UINT i = 0; i < capture->Count; i++)
        capture->Thumbs[i] = CaptureThumbnail(capture->Windows[i], &capture->Sizes[i]);
    if (!PostMessageW(capture->Sink, WM_LIBER_PREVIEW_DONE, capture->Generation, (LPARAM)capture))
    {
        for (UINT i = 0; i < capture->Count; i++)
        {
            if (capture->Thumbs[i])
                DestroyIcon(capture->Thumbs[i]);
        }
        delete capture;
    }
    return 0;
}

static HICON CaptureThumbnail(HWND hwnd, SIZE *pSize)
{
    RECT rc;
    HICON icon = NULL;

    pSize->cx = pSize->cy = 0;
    if (!IsWindow(hwnd) || IsIconic(hwnd) || IsHungAppWindow(hwnd) || !GetWindowRect(hwnd, &rc) || IsRectEmpty(&rc))
        return NULL;
    SIZE window = { rc.right - rc.left, rc.bottom - rc.top };
    SIZE limit = { LIBER_PREVIEW_THUMB_W, LIBER_PREVIEW_THUMB_H };
    SIZE thumb = LiberDock_FitThumbnail(window, limit);
    HDC screen = GetDC(NULL);
    HDC full = CreateCompatibleDC(screen), scaled = CreateCompatibleDC(screen);
    HBITMAP fullBitmap = CreateCompatibleBitmap(screen, window.cx, window.cy);
    BITMAPINFO bmi = {};
    bmi.bmiHeader.biSize = sizeof(bmi.bmiHeader);
    bmi.bmiHeader.biWidth = thumb.cx;
    bmi.bmiHeader.biHeight = -thumb.cy;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;
    DWORD *pixels = NULL;
    HBITMAP color = CreateDIBSection(screen, &bmi, DIB_RGB_COLORS, (void **)&pixels, NULL, 0);
    HBITMAP mask = CreateBitmap(thumb.cx, thumb.cy, 1, 1, NULL);

    if (full && scaled && fullBitmap && color && mask)
    {
        HGDIOBJ oldFull = SelectObject(full, fullBitmap), oldScaled = SelectObject(scaled, color);
        if (PrintWindow(hwnd, full, PW_RENDERFULLCONTENT) || PrintWindow(hwnd, full, 0))
        {
            SetStretchBltMode(scaled, HALFTONE);
            SetBrushOrgEx(scaled, 0, 0, NULL);
            StretchBlt(scaled, 0, 0, thumb.cx, thumb.cy, full, 0, 0, window.cx, window.cy, SRCCOPY);
            GdiFlush();
            for (LONG i = 0; i < thumb.cx * thumb.cy; i++)
                pixels[i] |= 0xFF000000u;
            SelectObject(scaled, oldScaled);
            ICONINFO info = { TRUE, 0, 0, mask, color };
            icon = CreateIconIndirect(&info);
            if (icon)
                *pSize = thumb;
        }
        else
        {
            SelectObject(scaled, oldScaled);
        }
        SelectObject(full, oldFull);
    }
    if (mask)
        DeleteObject(mask);
    if (color)
        DeleteObject(color);
    if (fullBitmap)
        DeleteObject(fullBitmap);
    if (scaled)
        DeleteDC(scaled);
    if (full)
        DeleteDC(full);
    ReleaseDC(NULL, screen);
    return icon;
}

class CPreviewPanel : public StackPanel
{
public:
    CPreviewPanel() : StackPanel(Orientation::Horizontal, 0)
    {
        Thickness pad = { LIBER_PREVIEW_PAD, LIBER_PREVIEW_PAD, LIBER_PREVIEW_PAD, LIBER_PREVIEW_PAD };
        SetPadding(pad);
    }

protected:
    void PaintOverride(PaintContext &context)
    {
        LiberShell_PaintGlass(context, ToRectF(Bounds()), (float)LIBER_RADIUS_TILE, LIBER_MATERIAL_THICK);
    }
};

enum
{
    LIBER_PREVIEW_SWITCH = 1,
    LIBER_PREVIEW_CLOSE
};

class CPreviewColumn : public Control
{
public:
    CPreviewColumn(HWND hwnd, HICON fallback) : m_hwnd(hwnd), m_Thumb(NULL), m_Fallback(fallback)
    {
        m_Title[0] = 0;
        GetWindowTextW(hwnd, m_Title, _countof(m_Title));
        m_ThumbSize.cx = m_ThumbSize.cy = 0;
        SetSize(LIBER_PREVIEW_THUMB_W + 2 * LIBER_PREVIEW_PAD,
                LIBER_PREVIEW_TITLE_H + LIBER_PREVIEW_THUMB_H + 2 * LIBER_PREVIEW_PAD);
        SetHitTestable(TRUE);
    }

    ~CPreviewColumn()
    {
        if (m_Thumb)
            DestroyIcon(m_Thumb);
    }

    HWND Window() const { return m_hwnd; }

    void SetThumb(HICON thumb, SIZE size)
    {
        if (m_Thumb)
            DestroyIcon(m_Thumb);
        m_Thumb = thumb;
        m_ThumbSize = size;
        Invalidate();
    }

    void OnPointerUp(POINT pt)
    {
        RECT close = CloseRect();
        SetId(PtInRect(&close, pt) ? LIBER_PREVIEW_CLOSE : LIBER_PREVIEW_SWITCH);
        Raise(ControlClicked);
    }

    RECT CloseRect() const
    {
        const RECT &rc = Bounds();
        RECT close = { rc.right - LIBER_PREVIEW_PAD - 24, rc.top + 2, rc.right - LIBER_PREVIEW_PAD, rc.top + 26 };
        return close;
    }

protected:
    void PaintOverride(PaintContext &context)
    {
        const ThemeTokens &tokens = context.Tokens;
        RectF rc = ToRectF(Bounds());

        if (HasState(StateHover) || HasState(StatePressed))
            context.Painter.FillRoundRect(rc, (float)LIBER_RADIUS_TILE_ITEM,
                                          tokens.Color(HasState(StatePressed) ? LIBER_COLOR_FILL_SUBTLE_PRESSED
                                                                              : LIBER_COLOR_FILL_SUBTLE_HOVER));
        RectF title = { rc.X + LIBER_PREVIEW_PAD, rc.Y, rc.Width - 2 * LIBER_PREVIEW_PAD - 28.0f,
                        (float)LIBER_PREVIEW_TITLE_H };
        context.Painter.Text(m_Title, title, LIBER_TYPE_CAPTION, tokens.Color(LIBER_COLOR_TEXT1),
                             TextMiddle | TextEllipsis);
        if (HasState(StateHover))
        {
            RECT close = CloseRect();
            RectF x = { (float)close.left + 6.0f, (float)close.top + 6.0f, 12.0f, 12.0f };
            context.Painter.Icon(L"dismiss_12_regular", x, tokens.Color(LIBER_COLOR_TEXT1));
        }
        RectF area = { rc.X + LIBER_PREVIEW_PAD, rc.Y + LIBER_PREVIEW_TITLE_H, (float)LIBER_PREVIEW_THUMB_W,
                       (float)LIBER_PREVIEW_THUMB_H };
        if (m_Thumb)
        {
            RectF thumb = { area.X + (area.Width - m_ThumbSize.cx) / 2, area.Y + (area.Height - m_ThumbSize.cy) / 2,
                            (float)m_ThumbSize.cx, (float)m_ThumbSize.cy };
            context.Painter.Image(m_Thumb, thumb);
        }
        else if (m_Fallback)
        {
            RectF icon = { area.X + (area.Width - 48.0f) / 2, area.Y + (area.Height - 48.0f) / 2, 48.0f, 48.0f };
            context.Painter.Image(m_Fallback, icon);
        }
    }

private:
    HWND m_hwnd;
    WCHAR m_Title[128];
    HICON m_Thumb;
    SIZE m_ThumbSize;
    HICON m_Fallback;
};

class CLiberPreview final : public IControlListener
{
public:
    CLiberPreview() : m_Root(NULL), m_Count(0), m_Sink(NULL), m_Generation(0)
    {
        ZeroMemory(m_Columns, sizeof(m_Columns));
    }

    ~CLiberPreview()
    {
        if (m_Sink)
            DestroyWindow(m_Sink);
        Clear();
        m_Surface.SetRoot(NULL);
        m_Surface.Destroy();
        delete m_Root;
    }

    BOOL Show(const HWND *windows, UINT count, HICON icon, const RECT *prcAnchor)
    {
        if (!m_Root)
        {
            if (!m_Surface.Create(NULL, L"Liber Preview", WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE))
                return FALSE;
            m_Surface.SetInputShape((float)LIBER_RADIUS_TILE);
            m_Surface.SetElevation(LIBER_ELEVATION_E32, (float)LIBER_RADIUS_TILE);
            m_Root = new CPreviewPanel();
            m_Surface.SetRoot(m_Root);
        }
        if (IsOpen() && SameWindows(windows, count))
            return TRUE;
        Clear();
        for (UINT i = 0; i < count && m_Count < _countof(m_Columns); i++)
        {
            if (!IsWindow(windows[i]))
                continue;
            CPreviewColumn *column = new CPreviewColumn(windows[i], icon);
            column->SetListener(this);
            m_Columns[m_Count++] = column;
            m_Root->AddChild(column);
        }
        if (!m_Count)
            return FALSE;
        StartCapture();
        LiberShell_BuildTokens(&m_Tokens);
        LiberShell_ApplyFont(&m_Surface);
        m_Surface.SetTokens(&m_Tokens);
        LiberShell_ApplyMaterial(&m_Surface, &m_Tokens, LIBER_MATERIAL_THICK, (float)LIBER_RADIUS_TILE);
        SIZE available = { LayoutInfinite, LayoutInfinite };
        SIZE size = m_Root->Measure(available);
        UINT dpi = m_Surface.Dpi();
        POINT pt;
        LiberShell_PlaceFlyout(prcAnchor, ScaleToDpi(size.cx, dpi), ScaleToDpi(size.cy, dpi), LIBER_FLYOUT_ANCHOR_CENTER, &pt);
        m_Surface.SetPosition(pt.x, pt.y, size.cx, size.cy);
        LiberShell_SampleBackdrop(&m_Surface, &m_Tokens, FALSE);
        m_Surface.RequestLayout();
        m_Surface.Render(AnimationClock::Now());
        if (!IsWindowVisible(m_Surface.Window()))
        {
            ShowWindow(m_Surface.Window(), SW_SHOWNOACTIVATE);
            m_Surface.TransitionIn(8.0f, LIBER_DUR_GENTLE);
        }
        return TRUE;
    }

    void Hide()
    {
        if (m_Surface.Window() && IsWindowVisible(m_Surface.Window()) && !m_Surface.IsClosing())
            m_Surface.TransitionOut(8.0f, LIBER_DUR_FAST);
    }

    BOOL IsOpen() const
    {
        return m_Surface.Window() && IsWindowVisible(m_Surface.Window()) && !m_Surface.IsClosing();
    }

    BOOL Contains(POINT screen) const
    {
        RECT rc;
        return IsOpen() && GetWindowRect(m_Surface.Window(), &rc) && PtInRect(&rc, screen);
    }

    void OnControlEvent(Widget *source, ControlEvent event)
    {
        CPreviewColumn *column = static_cast<CPreviewColumn *>(source);
        HWND hwnd = column->Window();
        UINT id = column->Id();

        UNREFERENCED_PARAMETER(event);
        Hide();
        if (!IsWindow(hwnd))
            return;
        if (id == LIBER_PREVIEW_CLOSE)
            PostMessageW(hwnd, WM_SYSCOMMAND, SC_CLOSE, 0);
        else
            SwitchToThisWindow(hwnd, TRUE);
    }

private:
    BOOL SameWindows(const HWND *windows, UINT count) const
    {
        UINT live = 0;
        for (UINT i = 0; i < count && live < _countof(m_Columns); i++)
        {
            if (!IsWindow(windows[i]))
                continue;
            if (live >= m_Count || m_Columns[live]->Window() != windows[i])
                return FALSE;
            live++;
        }
        return live == m_Count;
    }

    static LRESULT CALLBACK SinkProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
    {
        CLiberPreview *self = (CLiberPreview *)GetWindowLongPtrW(hwnd, GWLP_USERDATA);

        if (msg != WM_LIBER_PREVIEW_DONE)
            return DefWindowProcW(hwnd, msg, wParam, lParam);
        LIBER_PREVIEW_CAPTURE *capture = (LIBER_PREVIEW_CAPTURE *)lParam;
        BOOL current = self && (ULONG)wParam == self->m_Generation;
        ATLASSERT(capture && capture->Count <= LIBER_PREVIEW_MAX);
        for (UINT i = 0; i < capture->Count; i++)
        {
            if (current && self->Adopt(capture->Windows[i], capture->Thumbs[i], capture->Sizes[i]))
                continue;
            if (capture->Thumbs[i])
                DestroyIcon(capture->Thumbs[i]);
        }
        delete capture;
        if (current && self->IsOpen())
            self->m_Surface.Render(AnimationClock::Now());
        return 0;
    }

    BOOL Adopt(HWND hwnd, HICON thumb, SIZE size)
    {
        if (!thumb)
            return FALSE;
        for (UINT i = 0; i < m_Count; i++)
        {
            if (m_Columns[i]->Window() == hwnd)
            {
                m_Columns[i]->SetThumb(thumb, size);
                return TRUE;
            }
        }
        return FALSE;
    }

    void StartCapture()
    {
        if (!m_Sink)
        {
            WNDCLASSW wc = {};
            wc.lpfnWndProc = SinkProc;
            wc.hInstance = GetModuleHandleW(NULL);
            wc.lpszClassName = L"LiberPreviewSink";
            RegisterClassW(&wc);
            m_Sink = CreateWindowExW(0, wc.lpszClassName, NULL, 0, 0, 0, 0, 0, HWND_MESSAGE, NULL, wc.hInstance, NULL);
            if (!m_Sink)
                return;
            SetWindowLongPtrW(m_Sink, GWLP_USERDATA, (LONG_PTR)this);
        }
        LIBER_PREVIEW_CAPTURE *capture = new LIBER_PREVIEW_CAPTURE();
        if (!capture)
            return;
        capture->Sink = m_Sink;
        capture->Generation = ++m_Generation;
        capture->Count = m_Count;
        for (UINT i = 0; i < m_Count; i++)
            capture->Windows[i] = m_Columns[i]->Window();
        HANDLE thread = CreateThread(NULL, 0, CaptureThread, capture, 0, NULL);
        if (thread)
            CloseHandle(thread);
        else
            delete capture;
    }

    void Clear()
    {
        for (UINT i = 0; i < m_Count; i++)
        {
            m_Root->RemoveChild(m_Columns[i]);
            delete m_Columns[i];
            m_Columns[i] = NULL;
        }
        m_Count = 0;
    }

    Surface m_Surface;
    ThemeTokens m_Tokens;
    CPreviewPanel *m_Root;
    CPreviewColumn *m_Columns[LIBER_PREVIEW_MAX];
    UINT m_Count;
    HWND m_Sink;
    ULONG m_Generation;
};

static CLiberPreview *g_pLiberPreview;

BOOL LiberPreview_Show(const HWND *pWindows, UINT cWindows, HICON hIcon, IN const RECT *prcAnchor)
{
    if (!g_pLiberPreview)
        g_pLiberPreview = new CLiberPreview();
    return g_pLiberPreview && g_pLiberPreview->Show(pWindows, cWindows, hIcon, prcAnchor);
}

VOID LiberPreview_Hide(VOID)
{
    if (g_pLiberPreview)
        g_pLiberPreview->Hide();
}

BOOL LiberPreview_IsOpen(VOID)
{
    return g_pLiberPreview && g_pLiberPreview->IsOpen();
}

BOOL LiberPreview_Contains(POINT screen)
{
    return g_pLiberPreview && g_pLiberPreview->Contains(screen);
}

VOID LiberPreview_Destroy(VOID)
{
    delete g_pLiberPreview;
    g_pLiberPreview = NULL;
}
