/*
 * PROJECT:     LiberNT Liber UI - Unit-tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test the Direct3D painter, its text and icon rasterizers and the surface pixels
 * COPYRIGHT:   Copyright 2026 Eitan Hadar <eitanhadar4@gmail.com>
 */

#include <apitest.h>

#include <liberui.h>
#include <libernt/dwmframe.h>

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

static void TestFonts()
{
    FontSet fonts;

    ok(!wcscmp(fonts.Family(), LIBER_FONT_UI), "default family %ls\n", fonts.Family());
    ok(!fonts.SetFamily(L"No Such Font Family 1234") && fonts.UsesFallback() && !wcscmp(fonts.Family(), LIBER_FONT_UI),
       "missing family falls back to %ls\n", fonts.Family());
    ok(fonts.Format(LIBER_TYPE_BODY) != NULL, "body text format\n");
    ok(fonts.Format(LIBER_TYPE_BODY) == fonts.Format(LIBER_TYPE_BODY), "text formats are cached\n");
    ok(fonts.LoadedCount() == 1, "only the used style is created: %u\n", fonts.LoadedCount());
    fonts.Reset();
    ok(fonts.LoadedCount() == 0, "reset frees the formats\n");
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

static void TestInvalidStyles()
{
    FontSet fonts;
    LONG failures = AssertionFailures();

    ok(fonts.Tracking(LIBER_TYPE_COUNT) == 0.0f, "out-of-range tracking is 0\n");
    ok(fonts.Format(LIBER_TYPE_COUNT) == fonts.Format(LIBER_TYPE_BODY), "out-of-range format falls back to body\n");
    ok(AssertionFailures() > failures, "out-of-range styles assert\n");
}

typedef void (*PAINT_SCRIPT)(IPainter &painter);

class ScriptWidget : public Widget
{
public:
    ScriptWidget(PAINT_SCRIPT script) : Script(script) {}
    PAINT_SCRIPT Script;

protected:
    void PaintOverride(PaintContext &context) { Script(context.Painter); }
};

static BOOL RenderScript(PAINT_SCRIPT script, UINT dpi, DWORD *pixels, BOOL *gpu)
{
    Surface surface;
    StackPanel *root = new StackPanel(Orientation::Horizontal, 0);
    ScriptWidget *widget = new ScriptWidget(script);
    ThemeTokens tokens;
    BOOL rendered;

    ZeroMemory(pixels, (SIZE_T)ScaleToDpi(64, dpi) * ScaleToDpi(64, dpi) * sizeof(DWORD));
    widget->SetSize(64, 64);
    root->AddChild(widget);
    if (!surface.Create(NULL, L"LiberUI GPU painter test", WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE))
    {
        delete root;
        return FALSE;
    }
    surface.SetDpi(dpi);
    surface.SetTokens(&tokens);
    surface.SetRoot(root);
    surface.SetPosition(-2000, -2000, 64, 64);
    *gpu = surface.IsGpu();
    rendered = surface.Render(0) && surface.ReadPixels(pixels, ScaleToDpi(64, dpi) * 4);
    surface.SetRoot(NULL);
    surface.Destroy();
    delete root;
    return rendered;
}

static void ScriptRound(IPainter &p) { p.FillRoundRect(R(0, 0, 64, 64), 16.0f, 0xFFFF0000); }
static void ScriptHalf(IPainter &p) { p.FillRect(R(0, 0, 64, 64), 0x80FFFFFF); }
static void ScriptSmall(IPainter &p) { p.FillRect(R(0, 0, 10, 10), 0xFF00FF00); }
static void ScriptClip(IPainter &p)
{
    p.PushClip(R(0, 0, 32, 64), 0.0f);
    p.FillRect(R(0, 0, 64, 64), 0xFF0000FF);
    p.PopClip();
}
static void ScriptStroke(IPainter &p) { p.StrokeRoundRect(R(0, 0, 64, 64), 8.0f, 0xFFFFFFFF, 1.0f); }
static void ScriptEllipse(IPainter &p) { p.FillEllipse(R(0, 0, 64, 64), 0xFF00FF00); }
static void ScriptArc(IPainter &p) { p.StrokeArc(R(8, 8, 48, 48), -90.0f, 90.0f, 0xFFFF0000, 6.0f); }
static void ScriptLine(IPainter &p) { p.Line(PointF{ 0, 32 }, PointF{ 64, 32 }, 0xFF0000FF, 2.0f); }
static void ScriptText(IPainter &p)
{
    p.Text(L"Liber", R(0, 0, 64, 64), LIBER_TYPE_BODY_STRONG, 0xFF000000, TextCenter | TextMiddle);
}
static void ScriptIcon(IPainter &p) { p.Icon(L"settings_20_regular", R(0, 0, 64, 64), 0xFFFFFFFF); }
static void ScriptNestedClip(IPainter &p)
{
    p.PushClip(R(0, 0, 64, 64), 0.0f);
    p.PushClip(R(8, 8, 48, 48), 16.0f);
    p.FillRect(R(0, 0, 64, 64), 0xFFFF0000);
    p.PopClip();
    p.PopClip();
}
static void ScriptInnerSquare(IPainter &p)
{
    p.PushClip(R(0, 0, 64, 64), 24.0f);
    p.PushClip(R(24, 24, 16, 16), 0.0f);
    p.FillRect(R(0, 0, 64, 64), 0xFF00FF00);
    p.PopClip();
    p.PopClip();
}
static void ScriptEmptyClip(IPainter &p)
{
    p.PushClip(R(0, 0, 20, 64), 0.0f);
    p.PushClip(R(40, 0, 20, 64), 0.0f);
    p.FillRect(R(0, 0, 64, 64), 0xFFFF0000);
    p.PopClip();
    p.PopClip();
}
static void ScriptShortLine(IPainter &p) { p.Line(PointF{ 16, 32 }, PointF{ 48, 32 }, 0xFF0000FF, 8.0f); }
static void ScriptImage(IPainter &p)
{
    HICON alpha = MakeIcon(0x80FF0000), opaque = MakeIcon(0x000000FF);
    p.Image(alpha, R(0, 0, 16, 16));
    p.Image(opaque, R(32, 32, 16, 16));
    p.Image(NULL, R(0, 32, 16, 16));
    DestroyIcon(alpha);
    DestroyIcon(opaque);
}

static SIZE g_Measured, g_Empty, g_Wrapped;
static void ScriptMeasure(IPainter &p)
{
    g_Measured = p.MeasureText(L"Liber", LIBER_TYPE_BODY, 0.0f);
    g_Empty = p.MeasureText(L"", LIBER_TYPE_CAPTION, 0.0f);
    g_Wrapped = p.MeasureText(L"Light and clear, content first", LIBER_TYPE_BODY, 60.0f);
}

static void ScriptTextColor(IPainter &p)
{
    p.Text(L"Liber", R(0, 0, 64, 64), LIBER_TYPE_BODY_STRONG, 0xFFFF0000, TextLeft | TextTop);
}

static void ScriptGradient(IPainter &p)
{
    p.FillGradient(R(0, 0, 64, 64), 0.0f, PointF{ 0, 0 }, PointF{ 64, 0 }, 0xFFFF0000, 0xFF0000FF);
}
static void ScriptRoundGradient(IPainter &p)
{
    p.FillGradient(R(0, 0, 64, 64), 16.0f, PointF{ 0, 0 }, PointF{ 64, 64 }, 0xFF1A8FE8, 0xFF0A4FB4);
}
static void ScriptShadow(IPainter &p) { p.FillShadow(R(16, 16, 32, 32), 8.0f, 8.0f, 0xFF000000); }
static void ScriptHollowShadow(IPainter &p)
{
    RectF shape = R(16, 16, 32, 32);
    p.FillShadow(shape, 8.0f, 8.0f, 0xFF000000, &shape);
}
static void ScriptSharpShadow(IPainter &p) { p.FillShadow(R(16, 16, 32, 32), 0.0f, 0.0f, 0xFF000000); }

static UINT CountInk(const DWORD *px, UINT count)
{
    UINT inked = 0;
    for (UINT i = 0; i < count; i++)
        inked += ArgbA(px[i]) != 0;
    return inked;
}

static void TestGpuPainter()
{
    static DWORD px[128 * 128];
    BOOL gpu = FALSE;

    ok(RenderScript(ScriptRound, 96, px, &gpu), "round rect rendered\n");
    if (!gpu)
    {
        skip("Direct3D 11 is not available\n");
        return;
    }
    ok(px[32 * 64 + 32] == 0xFFFF0000, "center is opaque red: %08lX\n", px[32 * 64 + 32]);
    ok(px[0] == 0, "rounded corner stays transparent: %08lX\n", px[0]);
    ok(px[32 * 64] == 0xFFFF0000, "straight edge is filled: %08lX\n", px[32 * 64]);
    ok(ArgbA(px[4 * 64 + 4]) > 0 && ArgbA(px[4 * 64 + 4]) < 255, "corner edge is anti-aliased: %08lX\n",
       px[4 * 64 + 4]);

    ok(RenderScript(ScriptHalf, 96, px, &gpu), "half fill rendered\n");
    ok(Near(px[10 * 64 + 10], 0x80808080, 1), "translucent fill is premultiplied: %08lX\n", px[10 * 64 + 10]);

    ok(RenderScript(ScriptSmall, 192, px, &gpu), "high DPI fill rendered\n");
    ok(px[19 * 128 + 19] == 0xFF00FF00 && px[21 * 128 + 21] == 0, "DIPs scale with DPI: %08lX %08lX\n",
       px[19 * 128 + 19], px[21 * 128 + 21]);

    ok(RenderScript(ScriptClip, 96, px, &gpu), "clip rendered\n");
    ok(px[10 * 64 + 10] == 0xFF0000FF && px[10 * 64 + 50] == 0, "clip limits drawing: %08lX %08lX\n",
       px[10 * 64 + 10], px[10 * 64 + 50]);

    ok(RenderScript(ScriptNestedClip, 96, px, &gpu), "nested clip rendered\n");
    ok(px[9 * 64 + 9] == 0 && px[32 * 64 + 32] == 0xFFFF0000 && px[4 * 64 + 32] == 0,
       "nested clip keeps the inner rounded rect: %08lX %08lX %08lX\n", px[9 * 64 + 9], px[32 * 64 + 32],
       px[4 * 64 + 32]);

    ok(RenderScript(ScriptInnerSquare, 96, px, &gpu), "inner square clip rendered\n");
    ok(px[24 * 64 + 24] == 0xFF00FF00 && px[39 * 64 + 39] == 0xFF00FF00,
       "a square clip inside a rounded one keeps its corners: %08lX %08lX\n", px[24 * 64 + 24], px[39 * 64 + 39]);

    ok(RenderScript(ScriptEmptyClip, 96, px, &gpu), "empty clip rendered\n");
    ok(CountInk(px, 64 * 64) == 0, "disjoint clips draw nothing: %u\n", CountInk(px, 64 * 64));

    ok(RenderScript(ScriptStroke, 96, px, &gpu), "stroke rendered\n");
    ok(px[32 * 64] != 0 && px[32 * 64 + 32] == 0, "stroke draws the edge only: %08lX %08lX\n", px[32 * 64],
       px[32 * 64 + 32]);

    ok(RenderScript(ScriptEllipse, 96, px, &gpu), "ellipse rendered\n");
    ok(px[32 * 64 + 32] == 0xFF00FF00 && px[0] == 0 && px[63] == 0, "ellipse: %08lX %08lX\n", px[32 * 64 + 32], px[0]);

    ok(RenderScript(ScriptArc, 96, px, &gpu), "arc rendered\n");
    ok(ArgbR(px[8 * 64 + 32]) > 200, "arc starts at the top: %08lX\n", px[8 * 64 + 32]);
    ok(ArgbR(px[32 * 64 + 56]) > 200, "a quarter arc reaches the right: %08lX\n", px[32 * 64 + 56]);
    ok(px[56 * 64 + 32] == 0 && px[32 * 64 + 8] == 0, "the rest of the circle is empty: %08lX %08lX\n",
       px[56 * 64 + 32], px[32 * 64 + 8]);

    ok(RenderScript(ScriptLine, 96, px, &gpu), "line rendered\n");
    ok(ArgbB(px[32 * 64 + 32]) > 200 && px[20 * 64 + 32] == 0, "line: %08lX %08lX\n", px[32 * 64 + 32],
       px[20 * 64 + 32]);

    ok(RenderScript(ScriptShortLine, 96, px, &gpu), "short line rendered\n");
    ok(ArgbB(px[32 * 64 + 17]) > 200 && px[32 * 64 + 13] == 0 && px[32 * 64 + 50] == 0,
       "lines have flat caps: %08lX %08lX %08lX\n", px[32 * 64 + 17], px[32 * 64 + 13], px[32 * 64 + 50]);

    ok(RenderScript(ScriptText, 96, px, &gpu), "text rendered\n");
    UINT inked = CountInk(px, 64 * 64);
    ok(inked > 20 && px[0] == 0, "text drew %u pixels\n", inked);

    ok(RenderScript(ScriptIcon, 96, px, &gpu), "icon rendered\n");
    inked = CountInk(px, 64 * 64);
    ok(inked > 50, "icon drew %u pixels\n", inked);

    ok(RenderScript(ScriptImage, 96, px, &gpu), "app icons rendered\n");
    ok(Near(px[8 * 64 + 8], 0x80800000, 3), "an alpha icon is drawn premultiplied: %08lX\n", px[8 * 64 + 8]);
    ok(Near(px[40 * 64 + 40], 0xFF0000FF, 2), "an icon without alpha is opaque: %08lX\n", px[40 * 64 + 40]);
    ok(px[40 * 64 + 8] == 0, "a NULL icon draws nothing\n");

    ok(RenderScript(ScriptMeasure, 96, px, &gpu), "text measured\n");
    ok(g_Measured.cx > 10 && g_Measured.cx < 64 && g_Measured.cy == LiberTypeRamp[LIBER_TYPE_BODY].LineHeight,
       "text measures %ld x %ld\n", g_Measured.cx, g_Measured.cy);
    ok(g_Empty.cx == 0 && g_Empty.cy == 16, "empty text measures %ld x %ld\n", g_Empty.cx, g_Empty.cy);
    ok(g_Wrapped.cy >= 2 * LiberTypeRamp[LIBER_TYPE_BODY].LineHeight, "a narrow width wraps text: %ld\n",
       g_Wrapped.cy);

    ok(RenderScript(ScriptTextColor, 96, px, &gpu), "coloured text rendered\n");
    UINT red = 0, other = 0;
    for (int i = 0; i < 64 * 64; i++)
    {
        if (ArgbA(px[i]) > 200)
        {
            red += ArgbR(px[i]) > 200 && ArgbG(px[i]) < 40 && ArgbB(px[i]) < 40;
            other += !(ArgbR(px[i]) > 200 && ArgbG(px[i]) < 40 && ArgbB(px[i]) < 40);
        }
    }
    ok(red > 10 && other == 0, "text takes the brush colour: %u red, %u other\n", red, other);
    ok(CountInk(px, 64 * 32) > 0 && CountInk(px + 64 * 32, 64 * 32) == 0, "top-aligned text stays at the top\n");

    ok(RenderScript(ScriptGradient, 96, px, &gpu), "gradient rendered\n");
    DWORD left = px[32 * 64 + 0], middle = px[32 * 64 + 32], right = px[32 * 64 + 63];
    ok(ArgbR(left) > 240 && ArgbB(left) < 16, "gradient starts red: %08lX\n", left);
    ok(ArgbB(right) > 240 && ArgbR(right) < 16, "gradient ends blue: %08lX\n", right);
    ok(ArgbR(middle) > 100 && ArgbR(middle) < 155 && ArgbB(middle) > 100 && ArgbB(middle) < 155 &&
       ArgbA(middle) == 255, "gradient midpoint mixes both: %08lX\n", middle);
    ok(px[0 * 64 + 10] == px[63 * 64 + 10], "a horizontal gradient is constant down a column\n");

    ok(RenderScript(ScriptRoundGradient, 96, px, &gpu), "rounded gradient rendered\n");
    ok(px[0] == 0 && ArgbA(px[32 * 64 + 32]) == 255, "a rounded gradient keeps its corners clear: %08lX\n", px[0]);

    ok(RenderScript(ScriptShadow, 96, px, &gpu), "shadow rendered\n");
    ok(ArgbA(px[32 * 64 + 32]) > 240, "shadow is solid in the middle: %08lX\n", px[32 * 64 + 32]);
    ok(ArgbA(px[32 * 64 + 16]) > 96 && ArgbA(px[32 * 64 + 16]) < 160, "shadow is about half at the edge: %08lX\n",
       px[32 * 64 + 16]);
    ok(ArgbA(px[32 * 64 + 10]) > 0 && ArgbA(px[32 * 64 + 10]) < ArgbA(px[32 * 64 + 16]),
       "shadow fades outside the shape: %08lX\n", px[32 * 64 + 10]);
    ok(px[32 * 64 + 1] == 0 && px[0] == 0, "shadow ends past the blur: %08lX\n", px[32 * 64 + 1]);
    ok(ArgbA(px[32 * 64 + 20]) == ArgbA(px[32 * 64 + 43]) && ArgbA(px[20 * 64 + 32]) == ArgbA(px[43 * 64 + 32]),
       "shadow is symmetric\n");

    ok(RenderScript(ScriptHollowShadow, 96, px, &gpu), "hollow shadow rendered\n");
    ok(px[32 * 64 + 32] == 0 && px[24 * 64 + 24] == 0, "a shadow under glass leaves the shape clear: %08lX\n",
       px[32 * 64 + 32]);
    ok(ArgbA(px[32 * 64 + 12]) > 0 && ArgbA(px[52 * 64 + 32]) > 0, "it still falls outside the shape\n");

    ok(RenderScript(ScriptSharpShadow, 96, px, &gpu), "sharp shadow rendered\n");
    ok(px[20 * 64 + 20] == 0xFF000000 && px[10 * 64 + 10] == 0, "a zero blur shadow is the plain shape\n");
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

    surface.SetPosition(120, 100, 80, 80);
    ok(surface.Render(0), "render after a move\n");
    GetWindowRect(surface.Window(), &rc);
    ok(rc.left == 120 && rc.top == 100, "a move without a resize places the window at (%ld,%ld)\n", rc.left, rc.top);
    surface.SetPosition(100, 100, 80, 80);
    surface.Render(0);

    ok(surface.RecreateGpu() && surface.IsGpu(), "the device is recreated on the same window\n");
    ok(surface.Render(0), "render after recreating the device\n");
    Pump(400);
    red = ScreenPixel(140, 140);
    ok(red == RGB(255, 0, 0), "the recreated device presents: %06lX\n", red);

    RECT box;
    ok(GetWindowRgnBox(surface.Window(), &box) == ERROR, "no input shape by default\n");
    surface.SetInputShape(20.0f);
    ok(surface.Render(0), "render after setting the input shape\n");
    HRGN shape = CreateRectRgn(0, 0, 0, 0);
    ok(shape && GetWindowRgn(surface.Window(), shape) != ERROR, "the input shape sets a window region\n");
    if (shape)
    {
        ok(!PtInRegion(shape, 1, 1) && PtInRegion(shape, 40, 40) && PtInRegion(shape, 40, 0),
           "transparent corners are outside the input shape\n");
        DeleteObject(shape);
    }
    surface.SetInputShape(0.0f);
    surface.Render(0);
    ok(GetWindowRgnBox(surface.Window(), &box) == ERROR, "clearing the input shape removes the region\n");

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

class ClickWidget : public Widget
{
public:
    ClickWidget() : Clicks(0), Menus(0)
    {
        SetHitTestable(TRUE);
        SetFocusable(TRUE);
        MenuAt.x = MenuAt.y = 0;
    }
    void OnClick() { Clicks++; }
    BOOL OnContextMenu(POINT screen)
    {
        Menus++;
        MenuAt = screen;
        return TRUE;
    }
    UINT Clicks;
    UINT Menus;
    POINT MenuAt;
};

static void TestSurfaceInput()
{
    Surface surface;
    StackPanel *root = new StackPanel(Orientation::Horizontal, 0);
    ClickWidget *button = new ClickWidget();
    ThemeTokens tokens;

    button->SetSize(40, 40);
    root->AddChild(button);
    ok(surface.Create(NULL, L"LiberUI input test", WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE),
       "surface created\n");
    surface.SetDpi(96);
    surface.SetTokens(&tokens);
    surface.SetRoot(root);
    surface.SetPosition(-1000, -1000, 40, 40);
    ok(surface.Render(0), "render\n");

    SendMessageW(surface.Window(), WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(10, 10));
    ok(button->HasState(StatePressed), "mouse down presses the widget\n");
    SendMessageW(surface.Window(), WM_LBUTTONUP, 0, MAKELPARAM(10, 10));
    ok(button->Clicks == 1, "a click through window messages reaches the widget, %u clicks\n", button->Clicks);
    ok(!button->HasState(StatePressed) && GetCapture() != surface.Window(), "released and capture freed\n");

    SendMessageW(surface.Window(), WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(10, 10));
    ok(GetCapture() == surface.Window(), "a press captures the mouse\n");
    ReleaseCapture();
    ok(!button->HasState(StatePressed) && button->Clicks == 1, "losing capture cancels the press\n");

    POINT expected = { 12, 14 };
    ClientToScreen(surface.Window(), &expected);
    SendMessageW(surface.Window(), WM_RBUTTONUP, 0, MAKELPARAM(12, 14));
    ok(button->Menus == 1 && button->MenuAt.x == expected.x && button->MenuAt.y == expected.y && button->Clicks == 1,
       "a right click asks the widget for its menu at the screen point (%ld,%ld)\n", button->MenuAt.x,
       button->MenuAt.y);

    UINT presents = surface.PresentCount();
    ShowWindow(surface.Window(), SW_SHOWNOACTIVATE);
    ok(surface.PresentCount() > presents, "showing the surface presents its pixels again\n");
    ShowWindow(surface.Window(), SW_HIDE);

    surface.SetRoot(NULL);
    surface.Destroy();
    delete root;
}

static void TestTransitions()
{
    typedef HANDLE (WINAPI *PFN_SETTHREADDPIAWARENESSCONTEXT)(HANDLE);
    PFN_SETTHREADDPIAWARENESSCONTEXT setAwareness = (PFN_SETTHREADDPIAWARENESSCONTEXT)GetProcAddress(
        GetModuleHandleW(L"user32.dll"), "SetThreadDpiAwarenessContext");
    HANDLE previous = setAwareness ? setAwareness((HANDLE)-4) : NULL;
    Surface surface;
    StackPanel *root = new StackPanel(Orientation::Horizontal, 0);
    FillWidget *fill = new FillWidget(0xFFFF0000);
    ThemeTokens tokens;
    RECT rc;

    fill->SetSize(80, 80);
    root->AddChild(fill);
    ok(surface.Create(NULL, L"LiberUI transition test", WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE),
       "surface created\n");
    surface.SetDpi(96);
    surface.SetTokens(&tokens);
    surface.SetRoot(root);
    surface.SetPosition(300, 300, 80, 80);
    surface.Render(0);
    ShowWindow(surface.Window(), SW_SHOWNOACTIVATE);
    Pump(200);

    surface.SetOpacity(0.5f);
    Pump(200);
    COLORREF half = ScreenPixel(340, 340);
    ok(GetRValue(half) > 60 && GetRValue(half) < 255 && half != RGB(255, 0, 0), "half opacity blends with the desktop: %06lX\n",
       half);
    surface.SetOpacity(1.0f);
    Pump(200);
    ok(ScreenPixel(340, 340) == RGB(255, 0, 0), "full opacity is opaque again\n");

    if (!AnimationsEnabled())
    {
        skip("client area animations are off\n");
    }
    else
    {
        surface.TransitionIn(32.0f, 300);
        ok(surface.IsTransitioning() && surface.OffsetY() > 24.0f && surface.Opacity() < 0.3f,
           "a rise starts low and faint: %f %f\n", surface.OffsetY(), surface.Opacity());
        GetWindowRect(surface.Window(), &rc);
        ok(rc.top > 300 + 24, "the window starts below its place: %ld\n", rc.top);
        Pump(450);
        GetWindowRect(surface.Window(), &rc);
        ok(!surface.IsTransitioning() && surface.OffsetY() == 0.0f && surface.Opacity() == 1.0f && rc.top == 300,
           "the rise settles in place: %f %f %ld\n", surface.OffsetY(), surface.Opacity(), rc.top);

        surface.TransitionOut(24.0f, 150);
        ok(surface.IsTransitioning() && IsWindowVisible(surface.Window()), "an exit starts visible\n");
        Pump(300);
        ok(!surface.IsTransitioning() && !IsWindowVisible(surface.Window()), "an exit ends hidden\n");
        ok(surface.OffsetY() == 0.0f && surface.Opacity() == 1.0f, "an exit resets for the next open\n");
    }

    surface.SetRoot(NULL);
    surface.Destroy();
    delete root;
    if (setAwareness && previous)
        setAwareness(previous);
}

static void TestMaterial()
{
    Surface surface;
    ThemeTokens tokens;

    ok(surface.Create(NULL, L"LiberUI material test", WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE), "surface created\n");
    surface.SetDpi(96);
    surface.SetElevation(LIBER_ELEVATION_E32, 28.0f);
    ok(GetPropW(surface.Window(), DWM_PROP_LIBER_MATERIAL) == NULL && surface.Material() == -1,
       "no material by default\n");
    surface.SetMaterial(LIBER_MATERIAL_THICK, 0xFF10171E, 0xB8, 28.0f);
    ULONG packed = (ULONG)(ULONG_PTR)GetPropW(surface.Window(), DWM_PROP_LIBER_MATERIAL);
    ok((packed & DWM_LIBER_MATERIAL_VALID) && DWM_LIBER_MATERIAL_KIND(packed) == LIBER_MATERIAL_THICK + 1 &&
           DWM_LIBER_MATERIAL_LAYER(packed) == DWM_LIBER_LAYER_SHELL &&
           DWM_LIBER_MATERIAL_ELEVATION(packed) == LIBER_ELEVATION_E32 + 1 &&
           DWM_LIBER_MATERIAL_EDGE(packed) == DWM_LIBER_EDGE_GLASS && DWM_LIBER_MATERIAL_ALPHA(packed) == 0xB8 &&
           (packed & ~DWM_LIBER_MATERIAL_MASK) == 0,
       "the material is packed for the compositor: %08lX\n", packed);
    ok((ULONG_PTR)GetPropW(surface.Window(), DWM_PROP_LIBER_MATERIAL_TINT) == (ULONG_PTR)RGB(0x10, 0x17, 0x1E) + 1,
       "the tint is a COLORREF plus one\n");
    ok((ULONG_PTR)GetPropW(surface.Window(), DWM_PROP_CORNER_RADIUS) == 28, "the corner radius is in pixels\n");
    surface.SetDpi(192);
    ok((ULONG_PTR)GetPropW(surface.Window(), DWM_PROP_CORNER_RADIUS) == 56, "the radius follows the DPI\n");
    surface.SetMaterial(LIBER_MATERIAL_THICK, 0xFF10171E, 0xB8, 40.0f);
    ok(GetPropW(surface.Window(), DWM_PROP_CORNER_RADIUS) == NULL, "radii the compositor cannot round are left out\n");
    surface.SetMaterial(-1, 0, 0, 0.0f);
    ok(GetPropW(surface.Window(), DWM_PROP_LIBER_MATERIAL) == NULL &&
           GetPropW(surface.Window(), DWM_PROP_LIBER_MATERIAL_TINT) == NULL && surface.Material() == -1,
       "clearing the material removes the properties\n");
    surface.Destroy();
}

static BYTE PackedAlpha(HWND hwnd)
{
    return (BYTE)DWM_LIBER_MATERIAL_ALPHA((ULONG)(ULONG_PTR)GetPropW(hwnd, DWM_PROP_LIBER_MATERIAL));
}

static BYTE TokenAlpha(const ThemeTokens &tokens, double luma)
{
    return (BYTE)(tokens.Material(LIBER_MATERIAL_THICK, luma).Alpha * 255.0 + 0.5);
}

static void TestBackdrop()
{
    Surface surface;
    ThemeTokens tokens;
    ThemeParams params = { ThemeMode::Light, AccentPreset(LIBER_ACCENT_AZURE), SurfaceTint::Soft };

    tokens.Build(params);
    ok(surface.Create(NULL, L"LiberUI backdrop test", WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE), "surface created\n");
    surface.SetDpi(96);
    surface.SetTokens(&tokens);
    surface.SetPosition(40, 40, 80, 80);
    ok(surface.BackdropLuma(AnimationClock::Now()) == 0.5, "the backdrop starts at the middle\n");
    surface.SetMaterial(LIBER_MATERIAL_THICK, 0xFFF3F8FE, TokenAlpha(tokens, 0.5), 8.0f);
    surface.SetBackdropLuma(0.08);
    ok(surface.BackdropLuma(AnimationClock::Now()) == 0.08 && surface.BackdropTarget() == 0.08,
       "a hidden surface takes the backdrop at once\n");
    ok(PackedAlpha(surface.Window()) == TokenAlpha(tokens, 0.08), "the material follows a dark backdrop: %u %u\n",
       PackedAlpha(surface.Window()), TokenAlpha(tokens, 0.08));
    ok(TokenAlpha(tokens, 0.08) > TokenAlpha(tokens, 0.92), "light glass is more solid over a dark backdrop\n");

    ShowWindow(surface.Window(), SW_SHOWNOACTIVATE);
    surface.SetBackdropLuma(0.92);
    ok(surface.BackdropTarget() == 0.92, "the new backdrop is the target\n");
    double start = surface.BackdropLuma(AnimationClock::Now());
    ok(start < 0.2, "a visible surface does not jump to the new backdrop: %f\n", start);
    Pump(150);
    double middle = surface.BackdropLuma(AnimationClock::Now());
    ok(middle > start && middle < 0.92, "the backdrop moves over time: %f\n", middle);
    Pump(LIBER_DUR_ADAPTIVE_TINT + 200);
    ok(surface.BackdropLuma(AnimationClock::Now()) == 0.92, "the backdrop arrives after the transition\n");
    ok(PackedAlpha(surface.Window()) == TokenAlpha(tokens, 0.92), "the material follows the new backdrop: %u %u\n",
       PackedAlpha(surface.Window()), TokenAlpha(tokens, 0.92));
    surface.SetBackdropLuma(0.92 + 0.5 / 512.0);
    ok(surface.BackdropTarget() == 0.92, "changes below one alpha step are ignored\n");
    surface.Destroy();
    ok(surface.BackdropLuma(AnimationClock::Now()) == 0.92, "a destroyed surface keeps its last target\n");
}

static void TestShadow()
{
    Surface surface;
    StackPanel *root = new StackPanel(Orientation::Horizontal, 0);
    FillWidget *fill = new FillWidget(0xFFFF0000);
    ThemeTokens tokens;

    Thickness margin = ShadowMargin(tokens.Shadow(LIBER_ELEVATION_E32));
    ok(margin.Left == 48 && margin.Top == 30 && margin.Right == 48 && margin.Bottom == 66,
       "shadow-32 needs %d,%d,%d,%d of margin\n", margin.Left, margin.Top, margin.Right, margin.Bottom);

    fill->SetSize(100, 60);
    root->AddChild(fill);
    ok(surface.Create(NULL, L"LiberUI shadow test", WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE),
       "surface created\n");
    surface.SetDpi(96);
    surface.SetTokens(&tokens);
    surface.SetRoot(root);
    surface.SetPosition(-1500, -1500, 100, 60);
    ok(surface.Render(0) && surface.ShadowSurface() == NULL, "no shadow without an elevation\n");

    surface.SetElevation(LIBER_ELEVATION_E32, 8.0f);
    ShowWindow(surface.Window(), SW_SHOWNOACTIVATE);
    surface.Render(0);
    Surface *shadow = surface.ShadowSurface();
    ok(shadow && shadow->Window(), "an elevation creates a shadow window\n");
    if (shadow && shadow->Window())
    {
        RECT rc;
        LONG_PTR ex = GetWindowLongPtrW(shadow->Window(), GWL_EXSTYLE);
        GetWindowRect(shadow->Window(), &rc);
        ok(rc.left == -1500 - 48 && rc.top == -1500 - 30 && rc.right == -1400 + 48 && rc.bottom == -1440 + 66,
           "shadow window covers the margins: (%ld,%ld,%ld,%ld)\n", rc.left, rc.top, rc.right, rc.bottom);
        ok((ex & WS_EX_TRANSPARENT) && (ex & WS_EX_LAYERED) && (ex & WS_EX_TOPMOST) && shadow->IsGpu(),
           "the shadow is a click-through topmost Direct3D window\n");
        POINT probe = { -1500 - 40, -1500 + 30 };
        ok(WindowFromPoint(probe) != shadow->Window(), "the shadow does not take the pointer\n");
        ok(IsWindowVisible(shadow->Window()), "the shadow shows with the surface\n");
        ok(GetWindow(surface.Window(), GW_HWNDNEXT) == shadow->Window(), "the shadow sits right below the surface\n");

        static DWORD px[196 * 156];
        ok(shadow->ReadPixels(px, 196 * 4), "shadow pixels\n");
        DWORD inside = px[(30 + 30) * 196 + 48 + 50], above = px[2 * 196 + 98], below = px[(30 + 60 + 20) * 196 + 98];
        ok(ArgbA(inside) > 0x28, "the shadow is dense under the shape: %08lX\n", inside);
        ok(ArgbA(below) > ArgbA(above), "the shadow falls below the shape: %08lX above %08lX\n", below, above);
        ok(ArgbR(inside) == 0 && ArgbG(inside) == 0 && ArgbB(inside) == 0, "the shadow is black: %08lX\n", inside);

        surface.SetPosition(-1400, -1500, 100, 60);
        surface.Render(0);
        GetWindowRect(shadow->Window(), &rc);
        ok(rc.left == -1400 - 48, "the shadow follows a move: %ld\n", rc.left);

        ShowWindow(surface.Window(), SW_HIDE);
        ok(!IsWindowVisible(shadow->Window()), "the shadow hides with the surface\n");
    }
    surface.SetRoot(NULL);
    surface.Destroy();
    ok(surface.ShadowSurface() == NULL, "destroying the surface removes its shadow\n");
    delete root;
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
    ok(surface.IsGpu(), "every surface draws with Direct3D\n");
    surface.SetDpi(96);
    surface.SetTokens(&tokens);
    surface.SetRoot(root);
    surface.SetPosition(-1000, -1000, 40, 10);
    ok(surface.Render(0), "render\n");
    ok(surface.PixelSize().cx == 40 && surface.PixelSize().cy == 10, "buffer %ld x %ld\n", surface.PixelSize().cx,
       surface.PixelSize().cy);
    static DWORD px[80 * 20];
    ok(surface.ReadPixels(px, 40 * 4), "read back\n");
    ok(px[5 * 40 + 5] == 0xFFFF0000 && px[5 * 40 + 35] == 0xFF0000FF, "widgets painted: %08lX %08lX\n",
       px[5 * 40 + 5], px[5 * 40 + 35]);
    ok(!surface.HasDirty(), "nothing dirty after render\n");

    UINT before = right->Paints;
    ok(surface.Render(0) && right->Paints == before, "clean surface does not repaint\n");

    left->Color = 0xFF00FF00;
    left->Invalidate();
    ok(surface.HasDirty() && surface.Dirty().right == 20, "only the left widget is dirty\n");
    surface.Render(0);
    surface.ReadPixels(px, 40 * 4);
    ok(px[5 * 40 + 5] == 0xFF00FF00 && px[5 * 40 + 35] == 0xFF0000FF, "partial repaint keeps the rest: %08lX %08lX\n",
       px[5 * 40 + 5], px[5 * 40 + 35]);

    surface.SetDpi(192);
    surface.Render(0);
    ok(surface.PixelSize().cx == 80 && surface.PixelSize().cy == 20, "DPI resizes the buffer\n");
    surface.ReadPixels(px, 80 * 4);
    ok(px[10 * 80 + 70] == 0xFF0000FF, "high DPI paint %08lX\n", px[10 * 80 + 70]);
    POINT dip = { 70, 10 };
    dip = surface.PixelToDip(dip);
    ok(dip.x == 35 && dip.y == 5, "pixel to DIP %ld,%ld\n", dip.x, dip.y);

    surface.Input().PointerMove(dip);
    surface.SetRoot(NULL);
    ok(surface.Input().Root() == NULL && root->Host() == NULL, "root detached\n");
    ok(surface.Fonts().Format(LIBER_TYPE_BODY) != NULL && surface.Fonts().LoadedCount() > 0, "a text format is loaded\n");
    surface.Destroy();
    ok(surface.Window() == NULL, "window destroyed\n");
    ok(surface.Fonts().LoadedCount() == 0, "destroying the surface frees its text formats\n");
    delete root;
}

START_TEST(Painter)
{
    LONG failures = AssertionFailures();

    if (!Startup())
    {
        skip("DirectWrite is not available\n");
        return;
    }
    TestFonts();
    TestSurface();
    TestGpuSurface();
    TestGpuPainter();
    TestShadow();
    TestMaterial();
    TestBackdrop();
    TestTransitions();
    TestSurfaceInput();
    ok(AssertionFailures() == failures, "%ld assertions failed\n", AssertionFailures() - failures);
    TestInvalidStyles();
    Shutdown();
}
