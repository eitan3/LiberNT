/*
 * PROJECT:     LiberNT Liber UI - Unit-tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test the GDI+ painter and the layered surface pixels
 * COPYRIGHT:   Copyright 2026 Eitan Hadar <eitanhadar4@gmail.com>
 */

#include <apitest.h>

#include <liberui.h>

#include <stdlib.h>

using namespace LiberUI;

static BOOL Near(DWORD actual, DWORD expected, int tolerance)
{
    for (int shift = 0; shift < 32; shift += 8)
    {
        int a = (int)((actual >> shift) & 0xFF);
        int e = (int)((expected >> shift) & 0xFF);
        if (abs(a - e) > tolerance)
            return FALSE;
    }
    return TRUE;
}

static RectF R(float x, float y, float w, float h)
{
    RectF rc = { x, y, w, h };
    return rc;
}

static void TestShapes()
{
    const int size = 64;
    DWORD *bits = (DWORD *)calloc(size * size, sizeof(DWORD));
    FontSet fonts;

    {
        GdiplusPainter painter(bits, size, size, size * 4, 96, &fonts);
        ok(painter.IsValid(), "painter created\n");
        painter.FillRoundRect(R(0, 0, 64, 64), 16.0f, 0xFFFF0000);
    }
    ok(bits[32 * size + 32] == 0xFFFF0000, "center is opaque red: %08lX\n", bits[32 * size + 32]);
    ok(bits[0] == 0, "rounded corner stays transparent: %08lX\n", bits[0]);
    ok(bits[32 * size] == 0xFFFF0000, "straight edge is filled: %08lX\n", bits[32 * size]);

    {
        GdiplusPainter painter(bits, size, size, size * 4, 96, &fonts);
        painter.Clear(0);
        painter.FillRect(R(0, 0, 64, 64), 0x80FFFFFF);
    }
    ok(Near(bits[10 * size + 10], 0x80808080, 1), "translucent fill is premultiplied: %08lX\n", bits[10 * size + 10]);

    {
        GdiplusPainter painter(bits, size, size, size * 4, 192, &fonts);
        painter.Clear(0);
        painter.FillRect(R(0, 0, 10, 10), 0xFF00FF00);
    }
    ok(bits[19 * size + 19] == 0xFF00FF00 && bits[21 * size + 21] == 0, "DIPs scale with DPI: %08lX %08lX\n",
       bits[19 * size + 19], bits[21 * size + 21]);

    {
        GdiplusPainter painter(bits, size, size, size * 4, 96, &fonts);
        painter.Clear(0xFF000000);
        painter.PushClip(R(0, 0, 32, 64), 0.0f);
        painter.Clear(0);
        painter.FillRect(R(0, 0, 64, 64), 0xFF0000FF);
        painter.PopClip();
    }
    ok(bits[10 * size + 10] == 0xFF0000FF && bits[10 * size + 50] == 0xFF000000, "clip limits drawing: %08lX %08lX\n",
       bits[10 * size + 10], bits[10 * size + 50]);

    {
        GdiplusPainter painter(bits, size, size, size * 4, 96, &fonts);
        painter.Clear(0);
        painter.StrokeRoundRect(R(0, 0, 64, 64), 8.0f, 0xFFFFFFFF, 1.0f);
    }
    ok(bits[32 * size] != 0 && bits[32 * size + 32] == 0, "stroke draws the edge only: %08lX %08lX\n", bits[32 * size],
       bits[32 * size + 32]);

    {
        GdiplusPainter painter(bits, size, size, size * 4, 96, &fonts);
        painter.Clear(0);
        painter.Text(L"Liber", R(0, 0, 64, 64), LIBER_TYPE_BODY_STRONG, 0xFF000000, TextCenter | TextMiddle);
        SIZE measured = painter.MeasureText(L"Liber", LIBER_TYPE_BODY, 0.0f);
        ok(measured.cx > 10 && measured.cx < 64 && measured.cy == LiberTypeRamp[LIBER_TYPE_BODY].LineHeight,
           "text measures %ld x %ld\n", measured.cx, measured.cy);
        SIZE empty = painter.MeasureText(L"", LIBER_TYPE_CAPTION, 0.0f);
        ok(empty.cx == 0 && empty.cy == 16, "empty text measures %ld x %ld\n", empty.cx, empty.cy);
    }
    UINT inked = 0;
    for (int i = 0; i < size * size; i++)
        inked += bits[i] != 0;
    ok(inked > 20, "text drew %u pixels\n", inked);

    free(bits);
}

static void TestFonts()
{
    FontSet fonts;

    ok(!wcscmp(fonts.Family(), LIBER_FONT_UI), "default family %ls\n", fonts.Family());
    ok(!fonts.SetFamily(L"No Such Font Family 1234") && fonts.UsesFallback() && !wcscmp(fonts.Family(), LIBER_FONT_UI),
       "missing family falls back to %ls\n", fonts.Family());
    ok(fonts.Font(LIBER_TYPE_BODY) != NULL, "body font\n");
    ok(fonts.Font(LIBER_TYPE_BODY) == fonts.Font(LIBER_TYPE_BODY), "fonts are cached\n");
    ok(fonts.Tracking(LIBER_TYPE_DISPLAY) == 68 * -20 / 1000.0f && fonts.Tracking(LIBER_TYPE_BODY) == 0.0f,
       "tracking %f\n", fonts.Tracking(LIBER_TYPE_DISPLAY));
}

class FillWidget : public Widget
{
public:
    FillWidget(Argb color) : Color(color), Paints(0) {}
    Argb Color;
    UINT Paints;

protected:
    void PaintOverride(PaintContext &context)
    {
        Paints++;
        context.Painter.FillRect(ToRectF(Bounds()), Color);
    }
};

static HICON MakeIcon(DWORD pixel)
{
    DWORD bits[16 * 16];
    BYTE mask[16 * 2] = { 0 };
    ICONINFO info;
    HICON icon;

    for (int i = 0; i < 16 * 16; i++)
        bits[i] = pixel;
    ZeroMemory(&info, sizeof(info));
    info.fIcon = TRUE;
    info.hbmColor = CreateBitmap(16, 16, 1, 32, bits);
    info.hbmMask = CreateBitmap(16, 16, 1, 1, mask);
    icon = CreateIconIndirect(&info);
    DeleteObject(info.hbmColor);
    DeleteObject(info.hbmMask);
    return icon;
}

static void TestImage()
{
    const int size = 32;
    DWORD *bits = (DWORD *)calloc(size * size, sizeof(DWORD));
    FontSet fonts;
    HICON alpha = MakeIcon(0x80FF0000);
    HICON opaque = MakeIcon(0x000000FF);

    ok(alpha && opaque, "icons created\n");
    {
        GdiplusPainter painter(bits, size, size, size * 4, 96, &fonts);
        painter.Image(alpha, R(0, 0, 16, 16));
        painter.Image(opaque, R(16, 16, 16, 16));
        painter.Image(NULL, R(0, 16, 16, 16));
    }
    ok(Near(bits[8 * size + 8], 0x80800000, 2), "alpha icon is drawn premultiplied: %08lX\n", bits[8 * size + 8]);
    ok(bits[24 * size + 24] == 0xFF0000FF, "an icon without alpha is opaque: %08lX\n", bits[24 * size + 24]);
    ok(bits[24 * size + 8] == 0, "a NULL icon draws nothing\n");
    DestroyIcon(alpha);
    DestroyIcon(opaque);
    free(bits);
}

static void TestInvalidStyles()
{
    FontSet fonts;
    LPCWSTR face = NULL;
    INT style = -1;
    LONG failures = AssertionFailures();

    ok(fonts.Tracking(LIBER_TYPE_COUNT) == 0.0f, "out-of-range tracking is 0\n");
    ok(fonts.Font(LIBER_TYPE_COUNT) == fonts.Font(LIBER_TYPE_BODY), "out-of-range font falls back to body\n");
    fonts.Face((LIBER_TYPE)-1, &face, &style);
    ok(face != NULL && style >= 0, "out-of-range face falls back to body\n");
    ok(AssertionFailures() > failures, "out-of-range styles assert\n");
}

static void Pump(DWORD ms)
{
    ULONGLONG end = GetTickCount64() + ms;
    MSG msg;

    while (GetTickCount64() < end)
    {
        while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE))
        {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        Sleep(10);
    }
}

static COLORREF ScreenPixel(int x, int y)
{
    HDC screen = GetDC(NULL);
    COLORREF color = screen ? GetPixel(screen, x, y) : CLR_INVALID;
    if (screen)
        ReleaseDC(NULL, screen);
    return color;
}

static void TestGpuSurface()
{
    typedef HANDLE (WINAPI *PFN_SETTHREADDPIAWARENESSCONTEXT)(HANDLE);
    PFN_SETTHREADDPIAWARENESSCONTEXT setAwareness = (PFN_SETTHREADDPIAWARENESSCONTEXT)GetProcAddress(
        GetModuleHandleW(L"user32.dll"), "SetThreadDpiAwarenessContext");
    HANDLE previous = setAwareness ? setAwareness((HANDLE)-4) : NULL;
    Surface surface;
    StackPanel *root = new StackPanel(Orientation::Horizontal, 0);
    FillWidget *fill = new FillWidget(0xFFFF0000);
    ThemeTokens tokens;

    fill->SetSize(80, 80);
    root->AddChild(fill);
    ok(surface.Create(NULL, L"LiberUI D3D test", WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE),
       "surface created\n");
    if (!surface.IsGpu())
    {
        skip("Direct3D 11 composition is not available\n");
        surface.Destroy();
        delete root;
        if (setAwareness && previous)
            setAwareness(previous);
        return;
    }
    ok((GetWindowLongPtrW(surface.Window(), GWL_EXSTYLE) & 0x00200000L) != 0, "the window has no redirection bitmap\n");
    ok((GetWindowLongPtrW(surface.Window(), GWL_EXSTYLE) & WS_EX_LAYERED) == 0, "the window is not layered\n");

    surface.SetDpi(96);
    surface.SetTokens(&tokens);
    surface.SetRoot(root);
    surface.SetPosition(100, 100, 80, 80);
    UINT presents = surface.PresentCount();
    ok(surface.Render(0) && surface.PresentCount() == presents + 1, "render presents through the swap chain\n");
    RECT rc;
    GetWindowRect(surface.Window(), &rc);
    ok(rc.left == 100 && rc.top == 100 && rc.right == 180 && rc.bottom == 180, "window placed at (%ld,%ld,%ld,%ld)\n",
       rc.left, rc.top, rc.right, rc.bottom);

    ShowWindow(surface.Window(), SW_SHOWNOACTIVATE);
    Pump(400);
    COLORREF red = ScreenPixel(140, 140);
    ok(red == RGB(255, 0, 0), "the composed pixel is red: %06lX\n", red);

    fill->Color = 0xFF0000FF;
    fill->Invalidate();
    surface.Render(0);
    Pump(400);
    COLORREF blue = ScreenPixel(140, 140);
    ok(blue == RGB(0, 0, 255), "a second present updates the screen: %06lX\n", blue);

    fill->Color = 0x00000000;
    fill->Invalidate();
    surface.Render(0);
    Pump(400);
    COLORREF through = ScreenPixel(140, 140);
    ok(through != RGB(0, 0, 255) && through != RGB(255, 0, 0), "transparent pixels show what is below: %06lX\n",
       through);

    surface.SetRoot(NULL);
    surface.Destroy();
    ok(!surface.IsGpu() && surface.Window() == NULL, "destroyed\n");
    delete root;
    if (setAwareness && previous)
        setAwareness(previous);
}

static void TestSurface()
{
    Surface surface;
    StackPanel *root = new StackPanel(Orientation::Horizontal, 0);
    FillWidget *left = new FillWidget(0xFFFF0000);
    FillWidget *right = new FillWidget(0xFF0000FF);
    ThemeTokens tokens;

    left->SetSize(20, -1);
    right->SetSize(20, -1);
    root->AddChild(left);
    root->AddChild(right);

    ok(surface.Create(NULL, L"LiberUI test", WS_EX_TOOLWINDOW), "surface created\n");
    surface.SetDpi(96);
    surface.SetTokens(&tokens);
    surface.SetRoot(root);
    surface.SetPosition(-1000, -1000, 40, 10);
    ok(surface.Render(0), "render\n");
    ok(surface.PixelSize().cx == 40 && surface.PixelSize().cy == 10, "buffer %ld x %ld\n", surface.PixelSize().cx,
       surface.PixelSize().cy);
    const DWORD *px = surface.Pixels();
    ok(px && px[5 * 40 + 5] == 0xFFFF0000 && px[5 * 40 + 35] == 0xFF0000FF, "widgets painted: %08lX %08lX\n",
       px ? px[5 * 40 + 5] : 0, px ? px[5 * 40 + 35] : 0);
    ok(!surface.HasDirty(), "nothing dirty after render\n");

    UINT before = right->Paints;
    ok(surface.Render(0) && right->Paints == before, "clean surface does not repaint\n");

    left->Color = 0xFF00FF00;
    left->Invalidate();
    ok(surface.HasDirty() && surface.Dirty().right == 20, "only the left widget is dirty\n");
    surface.Render(0);
    px = surface.Pixels();
    ok(px[5 * 40 + 5] == 0xFF00FF00 && px[5 * 40 + 35] == 0xFF0000FF, "partial repaint keeps the rest: %08lX %08lX\n",
       px[5 * 40 + 5], px[5 * 40 + 35]);

    surface.SetDpi(192);
    surface.Render(0);
    ok(surface.PixelSize().cx == 80 && surface.PixelSize().cy == 20, "DPI resizes the buffer\n");
    px = surface.Pixels();
    ok(px[10 * 80 + 70] == 0xFF0000FF, "high DPI paint %08lX\n", px[10 * 80 + 70]);
    POINT dip = { 70, 10 };
    dip = surface.PixelToDip(dip);
    ok(dip.x == 35 && dip.y == 5, "pixel to DIP %ld,%ld\n", dip.x, dip.y);

    surface.Input().PointerMove(dip);
    surface.SetRoot(NULL);
    ok(surface.Input().Root() == NULL && root->Host() == NULL, "root detached\n");
    surface.Destroy();
    ok(surface.Window() == NULL, "window destroyed\n");
    delete root;
}

START_TEST(Painter)
{
    LONG failures = AssertionFailures();

    if (!Startup())
    {
        skip("GDI+ is not available\n");
        return;
    }
    TestShapes();
    TestFonts();
    TestImage();
    TestSurface();
    TestGpuSurface();
    ok(AssertionFailures() == failures, "%ld assertions failed\n", AssertionFailures() - failures);
    TestInvalidStyles();
    Shutdown();
}
