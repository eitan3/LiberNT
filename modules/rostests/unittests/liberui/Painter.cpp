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

typedef void (*PAINT_SCRIPT)(IPainter &painter);

class ScriptWidget : public Widget
{
public:
    ScriptWidget(PAINT_SCRIPT script) : Script(script) {}
    PAINT_SCRIPT Script;

protected:
    void PaintOverride(PaintContext &context) { Script(context.Painter); }
};

static BOOL RenderScript(PAINT_SCRIPT script, UINT dpi, DWORD *pixels, BOOL *gpu, DWORD flags = 0)
{
    Surface surface;
    StackPanel *root = new StackPanel(Orientation::Horizontal, 0);
    ScriptWidget *widget = new ScriptWidget(script);
    ThemeTokens tokens;
    BOOL rendered;

    ZeroMemory(pixels, (SIZE_T)ScaleToDpi(64, dpi) * ScaleToDpi(64, dpi) * sizeof(DWORD));
    widget->SetSize(64, 64);
    root->AddChild(widget);
    if (!surface.Create(NULL, L"LiberUI GPU painter test", WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, flags))
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
static void ScriptOffsetText(IPainter &p)
{
    p.Text(L"Liber", R(0.5f, 0.25f, 64, 64), LIBER_TYPE_BODY_STRONG, 0xFF000000, TextCenter | TextMiddle);
}

static UINT CountInk(const DWORD *px, UINT count)
{
    UINT inked = 0;
    for (UINT i = 0; i < count; i++)
        inked += ArgbA(px[i]) != 0;
    return inked;
}

static UINT AlphaMismatches(const DWORD *a, const DWORD *b, UINT count, int tolerance)
{
    UINT mismatches = 0;
    for (UINT i = 0; i < count; i++)
        mismatches += abs((int)ArgbA(a[i]) - (int)ArgbA(b[i])) > tolerance;
    return mismatches;
}

static ULONG Coverage(const DWORD *px, UINT count)
{
    ULONG sum = 0;
    for (UINT i = 0; i < count; i++)
        sum += ArgbA(px[i]);
    return sum;
}

static void TestGpuParity()
{
    static const struct
    {
        PAINT_SCRIPT Script;
        const char *Name;
        UINT Allowed;
    } Cases[] = {
        { ScriptRound, "round rect", 0 },     { ScriptStroke, "stroke", 0 },      { ScriptEllipse, "ellipse", 0 },
        { ScriptArc, "arc", 0 },              { ScriptLine, "line", 0 },          { ScriptShortLine, "short line", 0 },
        { ScriptClip, "clip", 0 },            { ScriptNestedClip, "nested clip", 16 },
        { ScriptEmptyClip, "empty clip", 0 }, { ScriptText, "text", 24 },         { ScriptOffsetText, "offset text", 24 },
        { ScriptIcon, "icon", 24 },
    };
    static DWORD gpuPx[64 * 64], cpuPx[64 * 64];
    BOOL gpu = FALSE, cpu = TRUE;

    for (UINT i = 0; i < _countof(Cases); i++)
    {
        ok(RenderScript(Cases[i].Script, 96, gpuPx, &gpu), "%s rendered on the GPU\n", Cases[i].Name);
        ok(RenderScript(Cases[i].Script, 96, cpuPx, &cpu, SurfaceNoGpu), "%s rendered on the CPU\n", Cases[i].Name);
        ok(!cpu, "%s CPU surface is not on the GPU\n", Cases[i].Name);
        if (!gpu)
            continue;
        UINT mismatches = AlphaMismatches(gpuPx, cpuPx, 64 * 64, 128);
        ok(mismatches <= Cases[i].Allowed, "%s: %u pixels disagree between Direct3D and GDI+\n", Cases[i].Name,
           mismatches);
        ULONG gpuCover = Coverage(gpuPx, 64 * 64), cpuCover = Coverage(cpuPx, 64 * 64);
        ULONG delta = gpuCover > cpuCover ? gpuCover - cpuCover : cpuCover - gpuCover;
        ok(delta * 10 <= max(cpuCover, 255UL), "%s: coverage %lu on Direct3D, %lu on GDI+\n", Cases[i].Name, gpuCover,
           cpuCover);
    }
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
    ClickWidget() : Clicks(0)
    {
        SetHitTestable(TRUE);
        SetFocusable(TRUE);
    }
    void OnClick() { Clicks++; }
    UINT Clicks;
};

static void TestSurfaceInput(DWORD flags)
{
    Surface surface;
    StackPanel *root = new StackPanel(Orientation::Horizontal, 0);
    ClickWidget *button = new ClickWidget();
    ThemeTokens tokens;

    button->SetSize(40, 40);
    root->AddChild(button);
    ok(surface.Create(NULL, L"LiberUI input test", WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, flags),
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

    UINT presents = surface.PresentCount();
    ShowWindow(surface.Window(), SW_SHOWNOACTIVATE);
    ok(surface.PresentCount() > presents, "showing the surface presents its pixels again\n");
    ShowWindow(surface.Window(), SW_HIDE);

    surface.SetRoot(NULL);
    surface.Destroy();
    delete root;
}

static void TestSurface(DWORD flags)
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

    ok(surface.Create(NULL, L"LiberUI test", WS_EX_TOOLWINDOW, flags), "surface created\n");
    if (flags & SurfaceNoGpu)
    {
        ok(!surface.IsGpu(), "the CPU surface does not use Direct3D\n");
        ok((GetWindowLongPtrW(surface.Window(), GWL_EXSTYLE) & WS_EX_LAYERED) != 0, "the CPU surface is layered\n");
    }
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
    ok(surface.Fonts().Font(LIBER_TYPE_BODY) != NULL && surface.Fonts().LoadedCount() > 0, "a font is loaded\n");
    surface.Destroy();
    ok(surface.Window() == NULL, "window destroyed\n");
    ok(surface.Fonts().LoadedCount() == 0, "destroying the surface frees its GDI+ fonts\n");
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
    TestSurface(0);
    TestSurface(SurfaceNoGpu);
    TestGpuSurface();
    TestGpuPainter();
    TestGpuParity();
    TestSurfaceInput(0);
    TestSurfaceInput(SurfaceNoGpu);
    ok(AssertionFailures() == failures, "%ld assertions failed\n", AssertionFailures() - failures);
    TestInvalidStyles();
    Shutdown();
}
