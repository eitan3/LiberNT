/*
 * PROJECT:     LiberNT DWM - Unit-tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test decoding of the Liber material window property and its backdrop blur
 * COPYRIGHT:   Copyright 2026 Eitan Hadar <eitanhadar4@gmail.com>
 */

#include <apitest.h>

#include <windows.h>
#include <math.h>

#include "libermaterial.h"
#include <libernt/winsnap.h>

static BOOLEAN Resolve(ULONG Packed, ULONG Tint, ULONG Dpi, PDWM_LIBER_MATERIAL_INFO Info)
{
    DWM_WIN Window;

    ZeroMemory(&Window, sizeof(Window));
    Window.LiberMaterial = Packed;
    Window.LiberMaterialTint = Tint;
    return DwmLiberResolveMaterial(&Window, Dpi, Info);
}

static void TestPacking(void)
{
    ULONG Packed = DWM_LIBER_MATERIAL_PACK(4, TRUE, DWM_LIBER_LAYER_SHELL, 5, DWM_LIBER_EDGE_GLASS, 0xA8);

    ok(DWM_LIBER_MATERIAL_KIND(Packed) == 4, "kind %lu\n", DWM_LIBER_MATERIAL_KIND(Packed));
    ok(DWM_LIBER_MATERIAL_LAYER(Packed) == DWM_LIBER_LAYER_SHELL, "layer %lu\n", DWM_LIBER_MATERIAL_LAYER(Packed));
    ok(DWM_LIBER_MATERIAL_ELEVATION(Packed) == 5, "elevation %lu\n", DWM_LIBER_MATERIAL_ELEVATION(Packed));
    ok(DWM_LIBER_MATERIAL_EDGE(Packed) == DWM_LIBER_EDGE_GLASS, "edge %lu\n", DWM_LIBER_MATERIAL_EDGE(Packed));
    ok(DWM_LIBER_MATERIAL_ALPHA(Packed) == 0xA8, "alpha %lu\n", DWM_LIBER_MATERIAL_ALPHA(Packed));
    ok((Packed & DWM_LIBER_MATERIAL_ADAPTIVE) && (Packed & DWM_LIBER_MATERIAL_VALID), "flags %08lX\n", Packed);
    ok((Packed & ~DWM_LIBER_MATERIAL_MASK) == 0, "packed value %08lX fits the mask\n", Packed);
    ok((DWM_LIBER_MATERIAL_PACK(0xF, 1, 3, 0xF, 3, 0xFF) & ~DWM_LIBER_MATERIAL_MASK) == 0, "all fields fit the mask\n");
}

static void TestResolve(void)
{
    DWM_LIBER_MATERIAL_INFO Info;
    ULONG Packed = DWM_LIBER_MATERIAL_PACK(LIBER_MATERIAL_THICK + 1, TRUE, DWM_LIBER_LAYER_SHELL,
                                           LIBER_ELEVATION_E32 + 1, DWM_LIBER_EDGE_GLASS, 0xE8);

    ok(Resolve(Packed, RGB(0x12, 0x34, 0x56), 96, &Info), "thick material rejected\n");
    ok(Info.Kind == LIBER_MATERIAL_THICK && Info.BlurRadius == 60, "kind %d blur %lu\n", Info.Kind, Info.BlurRadius);
    ok(Info.Layer == DWM_LIBER_LAYER_SHELL && Info.Adaptive && Info.Edge == DWM_LIBER_EDGE_GLASS, "fields\n");
    ok(Info.HasElevation && Info.Elevation == LIBER_ELEVATION_E32, "elevation %d\n", Info.Elevation);
    ok(Info.TintAlpha == 0xE8 && Info.TintColor == RGB(0x12, 0x34, 0x56), "tint %02X %06lX\n", Info.TintAlpha,
       Info.TintColor);
    ok(DwmLiberSolidColor(&Info) == 0xFF123456, "solid color %08lX\n", DwmLiberSolidColor(&Info));

    ok(Resolve(DWM_LIBER_MATERIAL_PACK(LIBER_MATERIAL_REGULAR + 1, FALSE, 0, 0, 0, 0), 0, 144, &Info),
       "regular material rejected\n");
    ok(Info.BlurRadius == 60 && !Info.HasElevation && !Info.Adaptive, "regular at 144 DPI blur %lu\n", Info.BlurRadius);
    ok(Resolve(DWM_LIBER_MATERIAL_PACK(LIBER_MATERIAL_MICA + 1, FALSE, 0, 0, 0, 0), 0, 0, &Info) &&
       Info.BlurRadius == 90, "mica with DPI 0 uses 96: blur %lu\n", Info.BlurRadius);
    ok(Resolve(DWM_LIBER_MATERIAL_PACK(LIBER_MATERIAL_ULTRATHIN + 1, FALSE, 0, DWM_LIBER_ELEVATION_MAX, 0, 0), 0, 96,
               &Info) && Info.Elevation == LIBER_ELEVATION_WINDOW_INACTIVE, "last elevation\n");
}

static void TestRejected(void)
{
    DWM_LIBER_MATERIAL_INFO Info;
    ULONG Good = DWM_LIBER_MATERIAL_PACK(1, FALSE, 0, 0, 0, 0x80);

    ok(!Resolve(0, 0, 96, &Info), "no material accepted\n");
    ok(Info.Kind == 0 && Info.BlurRadius == 0 && Info.TintAlpha == 0, "rejected info is zeroed\n");
    ok(!Resolve(Good & ~DWM_LIBER_MATERIAL_VALID, 0, 96, &Info), "missing valid bit accepted\n");
    ok(!Resolve(DWM_LIBER_MATERIAL_PACK(0, FALSE, 0, 0, 0, 0x80), 0, 96, &Info), "kind 0 accepted\n");
    ok(!Resolve(DWM_LIBER_MATERIAL_PACK(DWM_LIBER_MATERIAL_MAX + 1, FALSE, 0, 0, 0, 0), 0, 96, &Info),
       "kind 6 accepted\n");
    ok(!Resolve(DWM_LIBER_MATERIAL_PACK(1, FALSE, 3, 0, 0, 0), 0, 96, &Info), "layer 3 accepted\n");
    ok(!Resolve(DWM_LIBER_MATERIAL_PACK(1, FALSE, 0, DWM_LIBER_ELEVATION_MAX + 1, 0, 0), 0, 96, &Info),
       "elevation 8 accepted\n");
    ok(!Resolve(Good | 0x00000080u, 0, 96, &Info), "reserved bit 7 accepted\n");
    ok(!Resolve(Good | 0x01000000u, 0, 96, &Info), "reserved bit 24 accepted\n");
    ok(!Resolve(Good, 0x01000000u, 96, &Info), "tint above 24 bits accepted\n");
    ok(Resolve(Good, 0x00FFFFFFu, 96, &Info) && Info.TintColor == 0x00FFFFFF, "largest tint\n");
}

static void TestBlur(void)
{
    DWM_WIN Window;
    DWM_LIBER_BLUR Blur;

    ZeroMemory(&Window, sizeof(Window));
    ok(!DwmLiberBlur(&Window, 96, 64, &Blur) && Blur.Radius == 0, "no material, no Liber blur\n");
    DwmLiberApplyMaterial(&Window);
    ok(Window.BlurFlags == 0, "a window without a material keeps its blur flags\n");

    Window.LiberMaterial = DWM_LIBER_MATERIAL_PACK(LIBER_MATERIAL_THICK + 1, FALSE, DWM_LIBER_LAYER_SHELL, 0,
                                                   DWM_LIBER_EDGE_GLASS, 0xB8);
    Window.LiberMaterialTint = RGB(0xF3, 0xF8, 0xFE);
    Window.BlurRectCount = 3;
    ok(DwmLiberBlur(&Window, 96, 64, &Blur) && Blur.Radius == 60, "thick glass blurs 60 px at 96 DPI: %lu\n",
       Blur.Radius);
    ok(Blur.Saturation == LiberGlassLight.Saturate, "a light tint uses the light glass saturation: %f\n",
       Blur.Saturation);
    ok(DwmLiberBlur(&Window, 144, 64, &Blur) && Blur.Radius == 64, "the radius is capped by the compositor\n");
    ok(DwmLiberBlur(&Window, 96, 48, &Blur) && Blur.Radius == 48, "and by the software path's cap\n");
    Window.LiberMaterialTint = RGB(0x10, 0x17, 0x1E);
    ok(DwmLiberBlur(&Window, 96, 64, &Blur) && Blur.Saturation == LiberGlassDark.Saturate,
       "a dark tint uses the dark glass saturation\n");
    Window.LiberMaterial = DWM_LIBER_MATERIAL_PACK(LIBER_MATERIAL_THIN + 1, FALSE, DWM_LIBER_LAYER_SHELL, 0,
                                                   DWM_LIBER_EDGE_GLASS, 0x80);
    ok(DwmLiberBlur(&Window, 96, 64, &Blur) && Blur.Radius == 24, "thin glass blurs 24 px: %lu\n", Blur.Radius);
    Window.LiberMaterial = DWM_LIBER_MATERIAL_PACK(LIBER_MATERIAL_REGULAR + 1, FALSE, DWM_LIBER_LAYER_SHELL, 0,
                                                   DWM_LIBER_EDGE_GLASS, 0x80);
    ok(DwmLiberBlur(&Window, 192, 64, &Blur) && Blur.Radius == 64, "regular glass at 192 DPI is capped: %lu\n",
       Blur.Radius);

    DwmLiberApplyMaterial(&Window);
    ok((Window.BlurFlags & DWM_BLUR_ENABLE) && (Window.BlurFlags & DWM_BLUR_REGION_ENTIRE_WINDOW) &&
       Window.BlurRectCount == 0, "a Liber material blurs behind the whole window\n");

    ZeroMemory(&Window, sizeof(Window));
    Window.LiberMaterial = DWM_LIBER_MATERIAL_PACK(LIBER_MATERIAL_MICA + 1, FALSE, DWM_LIBER_LAYER_WINDOW, 0,
                                                   DWM_LIBER_EDGE_WINDOW, 0x80);
    ok(!DwmLiberBlur(&Window, 96, 64, &Blur), "mica is not a blur behind\n");
    DwmLiberApplyMaterial(&Window);
    ok(Window.BlurFlags == 0, "mica leaves the blur flags alone\n");

    ok(DwmLiberMaxBlurRadius(96) == 60 && DwmLiberMaxBlurRadius(192) == 120, "largest reach %lu\n",
       DwmLiberMaxBlurRadius(96));
}

static void TestDim(void)
{
    static ULONG bits[8 * 8];
    UINT i;
    double dim;

    ok(DwmLiberDimAmount(0.2) == 0.0 && DwmLiberDimAmount(0.32) == 0.0, "dark wallpapers are not dimmed\n");
    ok(fabs(DwmLiberDimAmount(0.5) - 0.162) < 1e-9, "dimming grows past the threshold: %f\n", DwmLiberDimAmount(0.5));
    ok(DwmLiberDimAmount(1.0) == 0.5 && DwmLiberDimAmount(0.9) == 0.5, "dimming stops at half\n");

    for (i = 0; i < ARRAYSIZE(bits); i++)
        bits[i] = 0xFF202020;
    dim = DwmLiberDimBackdrop(bits, 8, 8, 8 * sizeof(ULONG), RGB(0, 0, 0));
    ok(dim == 0.0 && bits[0] == 0xFF202020, "a dark backdrop is left alone: %f %08lX\n", dim, bits[0]);

    for (i = 0; i < ARRAYSIZE(bits); i++)
        bits[i] = 0xFFFFFFFF;
    dim = DwmLiberDimBackdrop(bits, 8, 8, 8 * sizeof(ULONG), RGB(0, 0, 0));
    ok(dim == 0.5 && bits[0] == 0xFF7F7F7F && bits[63] == 0xFF7F7F7F, "a white backdrop is dimmed by half: %08lX\n",
       bits[0]);

    for (i = 0; i < ARRAYSIZE(bits); i++)
        bits[i] = 0xFFFFFFFF;
    dim = DwmLiberDimBackdrop(bits, 8, 8, 8 * sizeof(ULONG), RGB(0x10, 0x20, 0xC8));
    ok(dim == 0.5 && bits[0] == 0xFF878FE3, "a tinted dim adds the shade: %08lX\n", bits[0]);

    for (i = 0; i < ARRAYSIZE(bits); i++)
        bits[i] = 0x00FFFFFF;
    DwmLiberDimBackdrop(bits, 8, 8, 8 * sizeof(ULONG), RGB(0, 0, 0));
    ok((bits[0] & 0xFF000000) == 0, "alpha is kept\n");
    ok(DwmLiberDimBackdrop(bits, 0, 8, 8 * sizeof(ULONG), 0) == 0.0, "an empty backdrop is not dimmed\n");
}

static void TestFade(void)
{
    DWM_WIN Window;
    DWM_LIBER_MATERIAL_INFO Info;

    ok(DwmLiberFadeAlpha(255, 0, 100) == 1, "the top row is nearly clear: %lu\n", DwmLiberFadeAlpha(255, 0, 100));
    ok(DwmLiberFadeAlpha(255, 50, 100) == 128, "the middle row is half: %lu\n", DwmLiberFadeAlpha(255, 50, 100));
    ok(DwmLiberFadeAlpha(255, 99, 100) == 253, "the bottom row is nearly full: %lu\n", DwmLiberFadeAlpha(255, 99, 100));
    ok(DwmLiberFadeAlpha(128, 99, 100) == 127, "the window alpha scales the fade\n");
    ok(DwmLiberFadeAlpha(255, -1, 100) == 0 && DwmLiberFadeAlpha(255, 100, 100) == 255, "rows outside clamp\n");
    ok(DwmLiberFadeAlpha(255, 0, 0) == 0, "an empty window is clear\n");

    ZeroMemory(&Window, sizeof(Window));
    Window.LiberMaterial = DWM_LIBER_MATERIAL_PACK(LIBER_MATERIAL_THIN + 1, FALSE, DWM_LIBER_LAYER_SHELL, 0,
                                                   DWM_LIBER_EDGE_NONE, 0) | DWM_LIBER_MATERIAL_FADE;
    ok((Window.LiberMaterial & ~DWM_LIBER_MATERIAL_MASK) == 0, "the fade bit is part of the material\n");
    ok(DwmLiberResolveMaterial(&Window, 96, &Info) && Info.Kind == LIBER_MATERIAL_THIN && Info.TintAlpha == 0,
       "a fading material still resolves\n");
    DwmLiberApplyMaterial(&Window);
    ok((Window.BlurFlags & DWM_BLUR_ENABLE) && (Window.BlurFlags & DWM_BLUR_REGION_ENTIRE_WINDOW),
       "a fading material blurs behind the window\n");
}

static void TestWindowMotion(void)
{
    ok(DwmLiberWindowCornerRadius(96) == 12 && DwmLiberWindowCornerRadius(144) == 18 && DwmLiberWindowCornerRadius(0) == 12,
       "Liber windows use the spec's 12 px radius, scaled by DPI\n");
    ok(DwmLiberMinimizeAlpha(0, 65536) == 255, "the window starts opaque\n");
    ok(DwmLiberMinimizeAlpha(65536 * 40 / 100, 65536) == 255, "it stays opaque for the first 40%% of the travel\n");
    ok(DwmLiberMinimizeAlpha(65536 * 70 / 100, 65536) > 120 && DwmLiberMinimizeAlpha(65536 * 70 / 100, 65536) < 135,
       "it is half faded at 70%%: %lu\n", DwmLiberMinimizeAlpha(65536 * 70 / 100, 65536));
    ok(DwmLiberMinimizeAlpha(65536, 65536) == 0 && DwmLiberMinimizeAlpha(70000, 65536) == 0, "it is gone at the icon\n");
    ok(DwmLiberMinimizeAlpha(10, 0) == 255, "no scale, no fade\n");
    ok(DWM_LIBER_ANIM_MINIMIZE_MS == 300 && DWM_LIBER_ANIM_RESTORE_MS == 340 && DWM_LIBER_ANIM_MOVE_MS == 300,
       "minimize 300 ms, restore 340 ms, maximize and snap 300 ms\n");
}

static void TestCaptionButtons(void)
{
    RECT rc;

    DwmLiberCaptionButtonRect(800, 40, 96, 0, &rc);
    ok(rc.left == 754 && rc.top == 6 && rc.right == 794 && rc.bottom == 34,
       "the close button is 40x28 with equal top and right insets: %ld,%ld,%ld,%ld\n", rc.left, rc.top, rc.right, rc.bottom);
    DwmLiberCaptionButtonRect(800, 40, 96, 2, &rc);
    ok(rc.left == 674 && rc.right == 714 && rc.top == 6, "the minimize button sits two buttons to the left\n");
    DwmLiberCaptionButtonRect(800, 20, 96, 0, &rc);
    ok(rc.top == 0 && rc.bottom == 20 && rc.right == 800, "a short caption clamps the button height\n");
    DwmLiberCaptionButtonRect(1200, 60, 144, 0, &rc);
    ok(rc.right - rc.left == 60 && rc.bottom - rc.top == 42 && rc.top == 9 && rc.right == 1191,
       "buttons scale with the DPI\n");
}

static void TestFrameShadow(void)
{
    DWM_LIBER_SHADOW Shadow;
    RECT Margins;
    UINT Index;

    DwmLiberWindowShadow(FALSE, TRUE, 96, &Shadow);
    ok(Shadow.Ambient.Offset == 36 && Shadow.Ambient.Sigma == 45.0 && Shadow.Ambient.Support == 113 &&
       Shadow.Ambient.Opacity == 302,
       "the active light ambient layer is shadow-64 0 36 90 #0000004d: %ld %g %ld %lu\n", Shadow.Ambient.Offset,
       Shadow.Ambient.Sigma, Shadow.Ambient.Support, Shadow.Ambient.Opacity);
    ok(Shadow.Contact.Offset == 10 && Shadow.Contact.Sigma == 13.0 && Shadow.Contact.Support == 33 &&
       Shadow.Contact.Opacity == 141,
       "the active light contact layer is 0 10 26 #00000024: %ld %g %ld %lu\n", Shadow.Contact.Offset,
       Shadow.Contact.Sigma, Shadow.Contact.Support, Shadow.Contact.Opacity);
    DwmLiberWindowShadow(TRUE, TRUE, 96, &Shadow);
    ok(Shadow.Ambient.Opacity == 600 && Shadow.Contact.Opacity == 322 && Shadow.Ambient.Offset == 36 &&
       Shadow.Ambient.Support == 113,
       "dark doubles the opacity and keeps the geometry: %lu %lu\n", Shadow.Ambient.Opacity, Shadow.Contact.Opacity);
    DwmLiberWindowShadow(FALSE, FALSE, 96, &Shadow);
    ok(Shadow.Ambient.Offset == 14 && Shadow.Ambient.Sigma == 18.0 && Shadow.Ambient.Opacity == 161 &&
       Shadow.Contact.Offset == 3 && Shadow.Contact.Sigma == 4.0 && Shadow.Contact.Opacity == 78,
       "an inactive window uses the lighter windowInactive elevation: %ld %g %lu, %ld %g %lu\n",
       Shadow.Ambient.Offset, Shadow.Ambient.Sigma, Shadow.Ambient.Opacity, Shadow.Contact.Offset,
       Shadow.Contact.Sigma, Shadow.Contact.Opacity);
    DwmLiberWindowShadow(TRUE, FALSE, 96, &Shadow);
    ok(Shadow.Ambient.Opacity == 341 && Shadow.Contact.Opacity == 200, "dark inactive: %lu %lu\n",
       Shadow.Ambient.Opacity, Shadow.Contact.Opacity);
    DwmLiberWindowShadow(FALSE, TRUE, 144, &Shadow);
    ok(Shadow.Ambient.Offset == 54 && Shadow.Ambient.Sigma == 67.5 && Shadow.Ambient.Support == 169 &&
       Shadow.Contact.Offset == 15 && Shadow.Ambient.Opacity == 302,
       "offsets and blur scale with DPI, opacity does not: %ld %g %ld\n", Shadow.Ambient.Offset,
       Shadow.Ambient.Sigma, Shadow.Ambient.Support);
    DwmLiberWindowShadow(FALSE, TRUE, 0, &Shadow);
    ok(Shadow.Ambient.Offset == 36 && Shadow.Ambient.Sigma == 45.0, "DPI 0 uses 96\n");

    for (Index = 0; Index < LIBER_ELEVATION_COUNT; ++Index)
    {
        DWM_LIBER_SHADOW_LAYER Layer;

        DwmLiberShadowLayer(&LiberShadowsLight[Index].Contact, 96, &Layer);
        ok(Layer.Support >= 2 && Layer.Sigma >= 0.5 && Layer.Opacity <= 1000, "elevation %u contact %ld %g %lu\n",
           Index, Layer.Support, Layer.Sigma, Layer.Opacity);
        DwmLiberShadowLayer(&LiberShadowsDark[Index].Ambient, 96, &Layer);
        ok(Layer.Support >= 2 && Layer.Opacity <= 1000, "elevation %u dark ambient %ld %lu\n", Index,
           Layer.Support, Layer.Opacity);
    }

    DwmLiberShadowMargins(96, &Margins);
    ok(Margins.left == 113 && Margins.right == 113 && Margins.top == 77 && Margins.bottom == 149,
       "the margins hold 2.5 sigma of the 90 px blur around its 36 px drop: %ld %ld %ld %ld\n", Margins.left,
       Margins.top, Margins.right, Margins.bottom);
    DwmLiberShadowMargins(192, &Margins);
    ok(Margins.left == 225 && Margins.top == 153 && Margins.bottom == 297, "margins at 192 DPI: %ld %ld %ld\n",
       Margins.left, Margins.top, Margins.bottom);
}

static void TestFrameEdge(void)
{
    DWM_LIBER_EDGE_COLORS Colors;
    ULONG Outline, Bevel, Mirror, MirrorBevel;
    double Sum = 0.0, Perimeter;
    LONG x, y;

    ok(DwmLiberFrameEdgeFor(FALSE, FALSE, 12) == DWM_LIBER_EDGE_WINDOW, "a rounded window gets the outline\n");
    ok(DwmLiberFrameEdgeFor(FALSE, TRUE, 0) == DWM_LIBER_EDGE_SEAM, "a snapped window gets the seam\n");
    ok(DwmLiberFrameEdgeFor(TRUE, FALSE, 0) == DWM_LIBER_EDGE_NONE &&
       DwmLiberFrameEdgeFor(TRUE, TRUE, 12) == DWM_LIBER_EDGE_NONE, "a maximized window has no outline\n");
    ok(DwmLiberFrameEdgeFor(FALSE, FALSE, 0) == DWM_LIBER_EDGE_NONE, "a square unsnapped window has none\n");

    ok(DwmLiberFrameEdge(DWM_WINDOW_LIBER_FRAME | DWM_WINDOW_LIBER_EDGE_PACK(DWM_LIBER_EDGE_SEAM)) ==
           DWM_LIBER_EDGE_SEAM &&
       DwmLiberFrameEdge(DWM_WINDOW_LIBER_FRAME | DWM_WINDOW_LIBER_EDGE_PACK(DWM_LIBER_EDGE_WINDOW) |
                         DWM_WINDOW_ACTIVE | DWM_LWA_ALPHA) == DWM_LIBER_EDGE_WINDOW,
       "the edge round-trips through the layer flags\n");
    ok(DwmLiberFrameEdge(DWM_WINDOW_LIBER_EDGE_PACK(DWM_LIBER_EDGE_WINDOW)) == DWM_LIBER_EDGE_NONE,
       "no edge outside Liber frames\n");
    ok(((DWM_WINDOW_LIBER_FRAME | DWM_WINDOW_LIBER_EDGE_MASK) &
        (DWM_LWA_COLORKEY | DWM_LWA_ALPHA | DWM_WINDOW_FRAME_ALPHA | DWM_WINDOW_DX_SCANOUT | DWM_WINDOW_DX_RETAINED |
         DWM_WINDOW_DX_PREMULTIPLIED_ALPHA | DWM_WINDOW_PREMULTIPLIED_ALPHA | DWM_WINDOW_DARK | DWM_WINDOW_ACTIVE |
         DWM_WINDOW_NC_SHADOW)) == 0,
       "the Liber frame bits do not overlap the other layer flags\n");

    DwmLiberFrameEdgeColors(FALSE, DWM_LIBER_EDGE_WINDOW, &Colors);
    ok(Colors.Outline == 0x80FFFFFF && Colors.Bevel == 0xD9FFFFFF, "light glass edge and edge-top: %08lX %08lX\n",
       Colors.Outline, Colors.Bevel);
    DwmLiberFrameEdgeColors(TRUE, DWM_LIBER_EDGE_WINDOW, &Colors);
    ok(Colors.Outline == 0x1AFFFFFF && Colors.Bevel == 0x33FFFFFF, "dark glass edge and edge-top: %08lX %08lX\n",
       Colors.Outline, Colors.Bevel);
    DwmLiberFrameEdgeColors(TRUE, DWM_LIBER_EDGE_SEAM, &Colors);
    ok(Colors.Outline == 0x1AFFFFFF && Colors.Bevel == 0, "a seam is the line without the bevel\n");
    DwmLiberFrameEdgeColors(FALSE, DWM_LIBER_EDGE_NONE, &Colors);
    ok(Colors.Outline == 0 && Colors.Bevel == 0, "no edge, no color\n");

    DwmLiberFrameEdgeCoverage(50, -1, 100, 80, 12, &Outline, &Bevel);
    ok(Outline == 256 && Bevel == 0, "the outline is the row above the window: %lu %lu\n", Outline, Bevel);
    DwmLiberFrameEdgeCoverage(50, 0, 100, 80, 12, &Outline, &Bevel);
    ok(Outline == 0 && Bevel == 256, "the bevel is the window's top row: %lu %lu\n", Outline, Bevel);
    DwmLiberFrameEdgeCoverage(50, 1, 100, 80, 12, &Outline, &Bevel);
    ok(Outline == 0 && Bevel == 0, "the bevel is one pixel: %lu %lu\n", Outline, Bevel);
    DwmLiberFrameEdgeCoverage(-1, 40, 100, 80, 12, &Outline, &Bevel);
    DwmLiberFrameEdgeCoverage(100, 40, 100, 80, 12, &Mirror, &MirrorBevel);
    ok(Outline == 256 && Mirror == 256 && Bevel == 0 && MirrorBevel == 0, "the sides are outside the window\n");
    DwmLiberFrameEdgeCoverage(0, 40, 100, 80, 12, &Outline, &Bevel);
    ok(Outline == 0 && Bevel == 0, "nothing is drawn over the window's side\n");
    DwmLiberFrameEdgeCoverage(50, 80, 100, 80, 12, &Outline, &Bevel);
    ok(Outline == 256 && Bevel == 0, "the outline runs under the window\n");
    DwmLiberFrameEdgeCoverage(50, 79, 100, 80, 12, &Outline, &Bevel);
    ok(Outline == 0 && Bevel == 0, "and there is no bevel at the bottom\n");
    DwmLiberFrameEdgeCoverage(101, 40, 100, 80, 12, &Outline, &Bevel);
    ok(Outline == 0, "the outline is one pixel\n");
    DwmLiberFrameEdgeCoverage(-1, -1, 100, 80, 12, &Outline, &Bevel);
    ok(Outline == 0 && Bevel == 0, "a rounded outline leaves the square corner empty\n");
    DwmLiberFrameEdgeCoverage(3, 3, 100, 80, 12, &Outline, &Bevel);
    DwmLiberFrameEdgeCoverage(96, 3, 100, 80, 12, &Mirror, &MirrorBevel);
    ok(Outline > 64 && Outline < 200 && Bevel > 64 && Bevel < 200 && Mirror == Outline && MirrorBevel == Bevel,
       "the outline and the bevel follow the corner arc: %lu %lu %lu %lu\n", Outline, Bevel, Mirror, MirrorBevel);
    DwmLiberFrameEdgeCoverage(-1, -1, 100, 80, 0, &Outline, &Bevel);
    ok(Outline == 256, "a square seam closes its corners: %lu\n", Outline);
    DwmLiberFrameEdgeCoverage(10, 10, 0, 80, 12, &Outline, &Bevel);
    ok(Outline == 0 && Bevel == 0, "an empty window has no edge\n");

    for (y = -2; y <= 81; ++y)
    {
        for (x = -2; x <= 101; ++x)
        {
            DwmLiberFrameEdgeCoverage(x, y, 100, 80, 12, &Outline, &Bevel);
            Sum += Outline / 256.0;
        }
    }
    Perimeter = 2.0 * (100 + 80) - 8.0 * 12 + 2.0 * 3.14159265358979 * 12.5;
    ok(fabs(Sum - Perimeter) < 3.0, "the outline is one pixel wide all around: %g against %g\n", Sum, Perimeter);
}

static POINT
Pt(LONG x, LONG y)
{
    POINT p;
    p.x = x;
    p.y = y;
    return p;
}

static void TestSnapZones(void)
{
    RECT work = { 0, 0, 1920, 1004 }, pos, win;

    ok(UserSnapZone(&work, Pt(0, 500), 96) == HTLEFT, "the left edge snaps a half\n");
    ok(UserSnapZone(&work, Pt(1919, 500), 96) == HTRIGHT, "the right edge snaps a half\n");
    ok(UserSnapZone(&work, Pt(900, 0), 96) == HTTOP, "the top edge maximizes\n");
    ok(UserSnapZone(&work, Pt(900, 500), 96) == HTNOWHERE, "the middle does nothing\n");
    ok(UserSnapZone(&work, Pt(0, 10), 96) == HTTOPLEFT && UserSnapZone(&work, Pt(10, 0), 96) == HTTOPLEFT,
       "both sides of the top-left corner snap a quarter\n");
    ok(UserSnapZone(&work, Pt(1919, 47), 96) == HTTOPRIGHT && UserSnapZone(&work, Pt(1919, 48), 96) == HTRIGHT,
       "the corner zone is 48 px at 96 DPI\n");
    ok(UserSnapZone(&work, Pt(1919, 71), 144) == HTTOPRIGHT, "and scales with DPI\n");
    ok(UserSnapZone(&work, Pt(0, 1003), 96) == HTBOTTOMLEFT && UserSnapZone(&work, Pt(1919, 1060), 96) == HTBOTTOMRIGHT,
       "the bottom corners snap a quarter, below the work area too\n");

    ok(UserSnapZoneHeld(&work, HTTOPLEFT, Pt(10, 50), 96, 12), "a quarter holds within the slack\n");
    ok(!UserSnapZoneHeld(&work, HTTOPLEFT, Pt(10, 70), 96, 12), "and drops past it\n");
    ok(UserSnapZoneHeld(&work, HTTOPLEFT, Pt(55, 5), 96, 12), "a top-left quarter holds along the top edge\n");
    ok(UserSnapZoneHeld(&work, HTBOTTOMRIGHT, Pt(1910, 950), 96, 12), "a bottom-right quarter holds\n");
    ok(!UserSnapZoneHeld(&work, HTBOTTOMRIGHT, Pt(1900, 950), 96, 12), "until it leaves the side\n");
    ok(UserSnapZoneHeld(&work, HTLEFT, Pt(12, 500), 96, 12) && !UserSnapZoneHeld(&work, HTLEFT, Pt(13, 500), 96, 12),
       "a half keeps the 12 px slack\n");
    ok(UserSnapZoneHeld(&work, HTTOP, Pt(500, 12), 96, 12) && !UserSnapZoneHeld(&work, HTNOWHERE, Pt(0, 0), 96, 12),
       "the top edge holds and nothing armed holds nothing\n");

    UserSnapRect(&work, HTTOPLEFT, 960, 502, &pos);
    ok(pos.left == 0 && pos.top == 0 && pos.right == 960 && pos.bottom == 502, "top-left quarter\n");
    UserSnapRect(&work, HTBOTTOMRIGHT, 960, 502, &pos);
    ok(pos.left == 960 && pos.top == 502 && pos.right == 1920 && pos.bottom == 1004, "bottom-right quarter\n");
    UserSnapRect(&work, HTRIGHT, 960, 1004, &pos);
    ok(pos.left == 960 && pos.top == 0 && pos.bottom == 1004, "right half\n");
    pos = work;
    UserSnapRect(&pos, HTBOTTOMLEFT, 960, 502, &pos);
    ok(pos.left == 0 && pos.top == 502 && pos.right == 960 && pos.bottom == 1004, "the work area may be the output\n");

    UserSnapRect(&work, HTTOPRIGHT, 960, 502, &win);
    ok(UserSnapEdgeFromRect(HTRIGHT, &win, &work) == HTTOPRIGHT, "a top quarter reads back\n");
    UserSnapRect(&work, HTBOTTOMLEFT, 960, 502, &win);
    ok(UserSnapEdgeFromRect(HTLEFT, &win, &work) == HTBOTTOMLEFT, "a bottom quarter reads back\n");
    UserSnapRect(&work, HTLEFT, 960, 1004, &win);
    ok(UserSnapEdgeFromRect(HTLEFT, &win, &work) == HTLEFT, "a half reads back as a half\n");
    ok(UserSnapEdgeFromRect(HTNOWHERE, &win, &work) == HTNOWHERE, "an unsnapped window stays unsnapped\n");
    win.bottom -= 16;
    ok(UserSnapEdgeFromRect(HTLEFT, &win, &work) == HTLEFT, "a half stays a half when the work area grows under it\n");
    UserSnapRect(&work, HTBOTTOMRIGHT, 960, 502, &win);
    work.bottom += 16;
    ok(UserSnapEdgeFromRect(HTRIGHT, &win, &work) == HTBOTTOMRIGHT, "a bottom quarter stays one when the work area grows\n");
    work.bottom -= 16;
    ok(UserSnapIsQuarter(HTBOTTOMRIGHT) && !UserSnapIsQuarter(HTRIGHT) && UserSnapIsLeft(HTBOTTOMLEFT) &&
           !UserSnapIsLeft(HTTOPRIGHT),
       "edge classification\n");
}

static void TestOpenMotion(void)
{
    RECT window = { 100, 200, 900, 800 }, from;

    DwmLiberOpenFromRect(&window, &from);
    ok(from.left == 124 && from.top == 218 && from.right == 876 && from.bottom == 782,
       "a window opens from 94%% of its size around its center\n");
    ok(DWM_LIBER_ANIM_OPEN_MS == 280 && DWM_LIBER_ANIM_OPEN_MS / DWM_TRANSITION_UNIT_MS <= 0xFF,
       "the 280 ms open fits the transition packing\n");
    window.right = window.left;
    DwmLiberOpenFromRect(&window, &from);
    ok(from.left == from.right, "an empty window stays empty\n");
}

static void TestCloseMotion(void)
{
    RECT window = { 100, 200, 900, 800 }, shown, from;

    DwmLiberCloseRect(&window, 0, 65536, &shown);
    ok(EqualRect(&shown, &window), "a closing window starts at its own rectangle\n");
    DwmLiberCloseRect(&window, 32768, 65536, &shown);
    ok(shown.left == 112 && shown.top == 209 && shown.right == 888 && shown.bottom == 791,
       "halfway it is at 97%% around its center: %ld,%ld,%ld,%ld\n", shown.left, shown.top, shown.right, shown.bottom);
    DwmLiberCloseRect(&window, 65536, 65536, &shown);
    ok(shown.left == 124 && shown.top == 218 && shown.right == 876 && shown.bottom == 782,
       "it ends at 94%% of its size around its center\n");
    DwmLiberCloseRect(&window, 90000, 65536, &from);
    ok(EqualRect(&from, &shown), "progress past the end stays at the end\n");
    DwmLiberCloseRect(&window, 5, 0, &from);
    ok(EqualRect(&from, &shown), "a zero range ends the motion\n");
    ok(DwmLiberCloseAlpha(0, 65536) == 255 && DwmLiberCloseAlpha(32768, 65536) == 128 &&
           DwmLiberCloseAlpha(65536, 65536) == 0 && DwmLiberCloseAlpha(70000, 65536) == 0 &&
           DwmLiberCloseAlpha(1, 0) == 0,
       "a closing window fades from opaque to transparent: %lu\n", DwmLiberCloseAlpha(32768, 65536));
    ok(DWM_LIBER_ANIM_CLOSE_MS == 200 && DWM_LIBER_ANIM_CLOSE_SCALE == 94, "the close takes 200 ms to 94%%\n");
    window.right = window.left;
    window.bottom = window.top + 1;
    DwmLiberCloseRect(&window, 65536, 65536, &shown);
    ok(shown.left == shown.right && shown.top == window.top && shown.bottom == window.bottom,
       "a degenerate window does not invert\n");
}

static BOOLEAN ChannelsNear(ULONG A, ULONG B, ULONG Tolerance)
{
    ULONG Shift;

    for (Shift = 0; Shift <= 16; Shift += 8)
    {
        LONG Delta = (LONG)((A >> Shift) & 0xFF) - (LONG)((B >> Shift) & 0xFF);
        if ((ULONG)(Delta < 0 ? -Delta : Delta) > Tolerance)
            return FALSE;
    }
    return TRUE;
}

static ULONG PixelOf(COLORREF Color)
{
    return ((ULONG)GetRValue(Color) << 16) | ((ULONG)GetGValue(Color) << 8) | GetBValue(Color);
}

static void TestMicaSchedule(void)
{
    static const ULONG Dpis[] = { 96, 120, 144, 192, 288 };
    DWM_LIBER_MICA_SCHEDULE Schedule;
    UINT Index, Box;

    ok(LiberMaterials[LIBER_MATERIAL_MICA].Blur == 90 && LiberMaterials[LIBER_MATERIAL_MICA].K == 1.0f,
       "mica is blur 90 with k 1\n");
    DwmLiberMicaSchedule(96, &Schedule);
    ok(Schedule.Factor == 16 && Schedule.Radius[0] == 5 && Schedule.Radius[1] == 5 && Schedule.Radius[2] == 5,
       "96 DPI mica blurs a 1/16 copy with three 11 px boxes: %lu %lu %lu %lu\n", Schedule.Factor,
       Schedule.Radius[0], Schedule.Radius[1], Schedule.Radius[2]);
    for (Index = 0; Index < ARRAYSIZE(Dpis); ++Index)
    {
        double Sigma = 90.0 * Dpis[Index] / 96.0, Reached;

        DwmLiberMicaSchedule(Dpis[Index], &Schedule);
        Reached = sqrt(DwmLiberMicaVariance(&Schedule));
        ok(fabs(Reached - Sigma) / Sigma < 0.05, "%lu DPI reaches sigma %f for %f\n", Dpis[Index], Reached, Sigma);
        ok(Schedule.Factor <= DWM_LIBER_MICA_FACTOR_MAX && (Schedule.Factor & (Schedule.Factor - 1)) == 0 &&
               Sigma / Schedule.Factor >= 4.0,
           "%lu DPI keeps at least 4 low-resolution pixels of sigma: factor %lu\n", Dpis[Index], Schedule.Factor);
        for (Box = 1; Box < DWM_LIBER_MICA_BOXES; ++Box)
            ok(Schedule.Radius[Box] >= Schedule.Radius[Box - 1] && Schedule.Radius[Box] - Schedule.Radius[0] <= 1,
               "%lu DPI box %u differs from the first by at most one step\n", Dpis[Index], Box);
    }
    DwmLiberMicaSchedule(0, &Schedule);
    ok(Schedule.Factor == 16, "DPI 0 means 96\n");
    ok(DwmLiberMicaScratchCount(1280, 800, 16) == 80 * 50 * 6 && DwmLiberMicaScratchCount(17, 1, 16) == 2 * 6 &&
           DwmLiberMicaScratchCount(0, 10, 16) == 0,
       "scratch holds two RGB grids of the rounded-up low-resolution size\n");
}

static void TestMicaBuild(void)
{
    static ULONG Wallpaper[256 * 48], Output[256 * 48];
    static float Scratch[16 * 3 * 6];
    DWM_LIBER_MICA_SCHEDULE Schedule;
    SIZE_T Count;
    UINT Index;
    LONG X;
    BOOLEAN Monotonic = TRUE, Symmetric = TRUE;

    DwmLiberMicaSchedule(96, &Schedule);
    Count = DwmLiberMicaScratchCount(256, 48, Schedule.Factor);
    ok(Count <= ARRAYSIZE(Scratch), "test scratch is large enough: %lu\n", (ULONG)Count);

    for (Index = 0; Index < ARRAYSIZE(Wallpaper); ++Index)
        Wallpaper[Index] = 0x00204060;
    ok(DwmLiberMicaBuild(Wallpaper, 100, 30, 256 * sizeof(ULONG), &Schedule, Scratch, Count, Output),
       "a uniform wallpaper builds\n");
    ok(Output[0] == 0xFF204060 && Output[99] == 0xFF204060 && Output[29 * 100 + 57] == 0xFF204060,
       "a uniform wallpaper stays uniform and opaque: %08lX %08lX\n", Output[0], Output[29 * 100 + 57]);

    for (Index = 0; Index < ARRAYSIZE(Wallpaper); ++Index)
        Wallpaper[Index] = (Index % 256) < 128 ? 0 : 0x00FFFFFF;
    ok(DwmLiberMicaBuild(Wallpaper, 256, 48, 256 * sizeof(ULONG), &Schedule, Scratch, Count, Output),
       "a step wallpaper builds\n");
    for (X = 1; X < 256; ++X)
        Monotonic = Monotonic && (Output[20 * 256 + X] & 0xFF) >= (Output[20 * 256 + X - 1] & 0xFF);
    for (X = 0; X < 128; ++X)
        Symmetric = Symmetric && abs((LONG)(Output[X] & 0xFF) + (LONG)(Output[255 - X] & 0xFF) - 255) <= 2;
    ok(Monotonic, "the blurred step rises monotonically\n");
    ok(Symmetric, "the blurred step is symmetric around the edge\n");
    ok(abs((LONG)(Output[256 + 97] & 0xFF) - 93) <= 8,
       "30.5 px left of the edge the step reaches Phi(-30.5/90): %lu\n", Output[256 + 97] & 0xFF);
    ok((Output[0] & 0xFF) > 0 && (Output[0] & 0xFF) < 30 && (Output[47 * 256 + 255] & 0xFF) > 225,
       "the 90 px blur reaches the far ends: %lu %lu\n", Output[0] & 0xFF, Output[47 * 256 + 255] & 0xFF);
    ok(Output[20 * 256 + 100] == Output[40 * 256 + 100], "a vertical step leaves columns uniform\n");

    ok(!DwmLiberMicaBuild(Wallpaper, 256, 48, 256 * sizeof(ULONG), &Schedule, Scratch, Count - 1, Output),
       "too small a scratch is refused\n");
    ok(!DwmLiberMicaBuild(Wallpaper, 256, 48, 255 * sizeof(ULONG), &Schedule, Scratch, Count, Output),
       "too small a stride is refused\n");
    ok(!DwmLiberMicaBuild(NULL, 256, 48, 256 * sizeof(ULONG), &Schedule, Scratch, Count, Output),
       "no wallpaper is refused\n");
}

static void TestMicaTint(void)
{
    DWM_LIBER_TINT_SOURCE Neutral = { DWM_LIBER_TINT_HUE, 0.0 }, Soft = { 255.0, 1.0 };
    DWM_LIBER_MICA Mica, Other;

    ok(DwmLiberOklchToRgb(1.0, 0.0, 0.0) == RGB(255, 255, 255) && DwmLiberOklchToRgb(0.0, 0.0, 0.0) == RGB(0, 0, 0),
       "OKLCH white and black\n");
    ok(ChannelsNear(DwmLiberOklchToRgb(0.627955, 0.257683, 29.2339), RGB(255, 0, 0), 1) &&
           ChannelsNear(DwmLiberOklchToRgb(0.519752, 0.176858, 142.4953), RGB(0, 128, 0), 1),
       "OKLCH red and green: %06lX %06lX\n", DwmLiberOklchToRgb(0.627955, 0.257683, 29.2339),
       DwmLiberOklchToRgb(0.519752, 0.176858, 142.4953));
    ok(GetRValue(DwmLiberOklchToRgb(0.7, 0.4, 150.0)) == 0 && GetGValue(DwmLiberOklchToRgb(0.7, 0.4, 150.0)) > 200,
       "out-of-gamut channels are clipped one by one: %06lX\n", DwmLiberOklchToRgb(0.7, 0.4, 150.0));

    DwmLiberMicaTint(TRUE, 0.0, &Neutral, &Mica);
    ok(Mica.Alpha == 184 && Mica.Saturation == 384, "dark mica on a black wallpaper is 0.72 opaque: %lu %lu\n",
       Mica.Alpha, Mica.Saturation);
    ok(ChannelsNear(Mica.Tint, PixelOf(DwmLiberOklchToRgb(0.33, 0.008, 260.0)), 1),
       "dark mica on black is oklch(0.33 0.008 260): %06lX\n", Mica.Tint);
    DwmLiberMicaTint(TRUE, 1.0, &Neutral, &Mica);
    ok(Mica.Alpha == 219, "dark mica on a white wallpaper is 0.86 opaque: %lu\n", Mica.Alpha);
    DwmLiberMicaTint(FALSE, 0.0, &Neutral, &Mica);
    ok(abs((LONG)Mica.Alpha - 230) <= 1 && Mica.Saturation == 435, "light mica on black is 0.9 opaque: %lu %lu\n", Mica.Alpha,
       Mica.Saturation);
    DwmLiberMicaTint(FALSE, 1.0, &Neutral, &Mica);
    ok(Mica.Alpha == 189, "light mica on white is 0.74 opaque: %lu\n", Mica.Alpha);
    ok(ChannelsNear(Mica.Tint, PixelOf(DwmLiberOklchToRgb(0.992, 0.006, 260.0)), 1),
       "light mica on white is oklch(0.992 0.006 260): %06lX\n", Mica.Tint);
    DwmLiberMicaTint(FALSE, 0.5, &Neutral, &Mica);
    ok(Mica.Alpha == 209, "the alpha follows the backdrop luminance linearly: %lu\n", Mica.Alpha);
    DwmLiberMicaTint(FALSE, 2.0, &Neutral, &Other);
    DwmLiberMicaTint(FALSE, 1.0, &Neutral, &Mica);
    ok(Other.Alpha == Mica.Alpha && Other.Tint == Mica.Tint, "luminance above 1 is clamped\n");
    DwmLiberMicaTint(TRUE, -1.0, &Neutral, &Other);
    DwmLiberMicaTint(TRUE, 0.0, &Neutral, &Mica);
    ok(Other.Alpha == Mica.Alpha && Other.Tint == Mica.Tint, "luminance below 0 is clamped\n");
    DwmLiberMicaTint(TRUE, 0.0, &Soft, &Mica);
    ok(ChannelsNear(Mica.Tint, PixelOf(DwmLiberOklchToRgb(0.33, 0.019, 255.0)), 1) &&
           GetBValue(DwmLiberOklchToRgb(0.33, 0.019, 255.0)) > GetRValue(DwmLiberOklchToRgb(0.33, 0.019, 255.0)),
       "a soft surface tint adds c1 chroma at the accent hue: %06lX\n", Mica.Tint);
}

static void TestMicaLuma(void)
{
    static ULONG Bits[40 * 20];
    RECT Rect;
    UINT Index;

    for (Index = 0; Index < ARRAYSIZE(Bits); ++Index)
        Bits[Index] = (Index % 40) < 20 ? 0xFFFFFFFF : 0xFF000000;
    SetRect(&Rect, 0, 0, 20, 20);
    ok(fabs(DwmLiberMicaLuma(Bits, 40, 20, 40 * sizeof(ULONG), &Rect) - 1.0) < 1e-9, "white is 1\n");
    SetRect(&Rect, 20, 0, 40, 20);
    ok(DwmLiberMicaLuma(Bits, 40, 20, 40 * sizeof(ULONG), &Rect) == 0.0, "black is 0\n");
    SetRect(&Rect, 0, 0, 40, 20);
    ok(fabs(DwmLiberMicaLuma(Bits, 40, 20, 40 * sizeof(ULONG), &Rect) - 0.5) < 1e-9,
       "10 x 10 samples split evenly across the window\n");
    SetRect(&Rect, -100, -5, 20, 10);
    ok(fabs(DwmLiberMicaLuma(Bits, 40, 20, 40 * sizeof(ULONG), &Rect) - 1.0) < 1e-9,
       "an offscreen part is clipped away\n");
    SetRect(&Rect, 50, 0, 60, 10);
    ok(DwmLiberMicaLuma(Bits, 40, 20, 40 * sizeof(ULONG), &Rect) == 0.5, "an offscreen window reads 0.5\n");
    SetRect(&Rect, 0, 0, 3, 2);
    ok(fabs(DwmLiberMicaLuma(Bits, 40, 20, 40 * sizeof(ULONG), &Rect) - 1.0) < 1e-9, "a tiny window samples itself\n");
    for (Index = 0; Index < ARRAYSIZE(Bits); ++Index)
        Bits[Index] = 0xFF00FF00;
    SetRect(&Rect, 0, 0, 40, 20);
    ok(fabs(DwmLiberMicaLuma(Bits, 40, 20, 40 * sizeof(ULONG), &Rect) - 0.7152) < 1e-9, "Rec. 709 luminance\n");
}

static void MicaFrame(DWM_WIN *Window)
{
    ZeroMemory(Window, sizeof(*Window));
    Window->x = 10;
    Window->y = 5;
    Window->cx = 20;
    Window->cy = 10;
    Window->LayerFlags = DWM_WINDOW_LIBER_FRAME | DWM_WINDOW_DARK;
    Window->BackdropType = DWM_BACKDROP_TRANSIENT;
    Window->BackdropRegion = DWM_BACKDROP_REGION_NONCLIENT;
    Window->BackdropOpacity = 222;
    Window->BackdropColor = RGB(28, 32, 40);
    Window->BackdropColorization = RGB(39, 43, 51);
}

static void TestMicaWindow(void)
{
    static ULONG Bits[40 * 20];
    DWM_LIBER_TINT_SOURCE Source = { 200.0, 1.0 };
    DWM_LIBER_MICA Mica, Expected;
    DWM_WIN Window, Before;
    UINT Index;

    for (Index = 0; Index < ARRAYSIZE(Bits); ++Index)
        Bits[Index] = 0xFFFFFFFF;

    MicaFrame(&Window);
    Window.LayerFlags &= ~DWM_WINDOW_LIBER_FRAME;
    Before = Window;
    ok(!DwmLiberClassifyMica(&Window) && !memcmp(&Window, &Before, sizeof(Window)),
       "a classic frame keeps its transient glass\n");
    DwmLiberApplyMica(&Window, Bits, 40, 20, 0, 0, &Source);
    ok(!memcmp(&Window, &Before, sizeof(Window)) && !DwmLiberWindowMica(&Window, &Mica),
       "and is never given mica\n");

    MicaFrame(&Window);
    Window.BackdropOpacity = 255;
    ok(!DwmLiberClassifyMica(&Window), "an opaque theme material is not mica\n");
    MicaFrame(&Window);
    Window.BackdropRegion = 0;
    ok(!DwmLiberClassifyMica(&Window), "a frame without a backdrop region is not mica\n");
    MicaFrame(&Window);
    Window.BackdropType = DWM_BACKDROP_NONE;
    ok(!DwmLiberClassifyMica(&Window), "a frame with no system backdrop is not mica\n");
    MicaFrame(&Window);
    Window.LiberMaterial = DWM_LIBER_MATERIAL_PACK(LIBER_MATERIAL_THICK + 1, FALSE, 0, 0, 0, 0x80);
    ok(!DwmLiberClassifyMica(&Window) && Window.BackdropType == DWM_BACKDROP_TRANSIENT,
       "a window's own Liber material wins\n");

    MicaFrame(&Window);
    ok(DwmLiberClassifyMica(&Window), "a Liber frame with transient non-client glass becomes mica\n");
    ok(Window.BackdropType == DWM_BACKDROP_MAIN && DWM_LIBER_MATERIAL_KIND(Window.LiberMaterial) ==
           LIBER_MATERIAL_MICA + 1 && Window.BackdropRegion == DWM_BACKDROP_REGION_NONCLIENT &&
           Window.BackdropColor == RGB(28, 32, 40),
       "it keeps its key and region on the main backdrop\n");
    Before = Window;
    ok(!DwmLiberClassifyMica(&Window) && !memcmp(&Window, &Before, sizeof(Window)), "classifying again is a no-op\n");

    DwmLiberApplyMica(&Window, Bits, 40, 20, 0, 0, &Source);
    DwmLiberMicaTint(TRUE, 1.0, &Source, &Expected);
    ok(Window.BackdropColor == RGB(39, 43, 51) && Window.BackdropColorization == RGB(39, 43, 51),
       "an inactive mica frame measures from its inactive key\n");
    ok(DwmLiberWindowMica(&Window, &Mica), "the classified frame resolves as mica\n");
    ok(Mica.Tint == Expected.Tint && Mica.Alpha == Expected.Alpha && Mica.Saturation == 384,
       "its tint comes from the wallpaper under it: %06lX/%06lX %lu/%lu %lu\n", Mica.Tint, Expected.Tint, Mica.Alpha,
       Expected.Alpha, Mica.Saturation);

    for (Index = 0; Index < ARRAYSIZE(Bits); ++Index)
        Bits[Index] = (Index % 40) < 20 ? 0xFFFFFFFF : 0xFF000000;
    Window.x = 100;
    Window.y = 100;
    DwmLiberApplyMica(&Window, Bits, 40, 20, 100, 100, &Source);
    DwmLiberWindowMica(&Window, &Mica);
    ok(Mica.Alpha == Expected.Alpha, "the sample follows the composition origin\n");
    Window.x = 110;
    DwmLiberApplyMica(&Window, Bits, 40, 20, 100, 100, &Source);
    DwmLiberMicaTint(TRUE, 0.5, &Source, &Expected);
    DwmLiberWindowMica(&Window, &Mica);
    ok(Mica.Alpha == Expected.Alpha, "moving the window over darker wallpaper changes its tint: %lu %lu\n", Mica.Alpha,
       Expected.Alpha);

    MicaFrame(&Window);
    Window.LayerFlags |= DWM_WINDOW_ACTIVE;
    DwmLiberClassifyMica(&Window);
    DwmLiberApplyMica(&Window, Bits, 40, 20, 0, 0, &Source);
    ok(Window.BackdropColor == RGB(28, 32, 40) && Window.BackdropColorization == RGB(28, 32, 40),
       "an active mica frame measures from its active key\n");
    Window.LayerFlags &= ~DWM_WINDOW_DARK;
    ok(DwmLiberWindowMica(&Window, &Mica) && Mica.Saturation == 435, "a light frame uses the light saturation\n");
    DwmLiberApplyMica(&Window, NULL, 40, 20, 0, 0, &Source);
    ok(Window.BackdropRegion == 0 && !DwmLiberWindowMica(&Window, &Mica),
       "without a mica wallpaper the frame shows its own pixels\n");

    MicaFrame(&Window);
    Window.BackdropType = DWM_BACKDROP_MAIN;
    ok(DwmLiberClassifyMica(&Window) && Window.BackdropType == DWM_BACKDROP_MAIN,
       "an app's DWMSBT_MAINWINDOW frame is mica in the Liber experience\n");
}

static void TestMicaPixel(void)
{
    DWM_LIBER_MICA Mica = { 0x102030, 128, 256 };
    ULONG Key = 0x1C2028;

    ok(DwmLiberMicaPixel(Key, Key, 255, 0x404040, &Mica) == 0x283038,
       "the key becomes the tint over the wallpaper: %06lX\n", DwmLiberMicaPixel(Key, Key, 255, 0x404040, &Mica));
    ok(DwmLiberMicaPixel(Key + 0x101010, Key, 255, 0x404040, &Mica) == 0x384048,
       "a pixel near the key keeps its difference as light\n");
    ok(DwmLiberMicaPixel(0x123456, Key, 0, 0x404040, &Mica) == 0x123456, "weight 0 leaves the pixel alone\n");
    ok(DwmLiberMicaPixel(Key, Key, 128, 0x404040, &Mica) == 0x222830,
       "a partial weight moves halfway: %06lX\n", DwmLiberMicaPixel(Key, Key, 128, 0x404040, &Mica));
    Mica.Alpha = 0;
    ok(DwmLiberMicaPixel(0, 0, 255, 0x804020, &Mica) == 0x804020, "saturation 1 keeps the wallpaper\n");
    Mica.Saturation = 512;
    ok(DwmLiberMicaPixel(0, 0, 255, 0x804020, &Mica) == 0xB53500,
       "saturation 2 doubles the distance from the luminance and clips: %06lX\n",
       DwmLiberMicaPixel(0, 0, 255, 0x804020, &Mica));
    ok(DwmLiberMicaPixel(0xFFFFFF, 0, 255, 0xFFFFFF, &Mica) == 0xFFFFFF, "channels clip at white\n");
    Mica.Alpha = 255;
    Mica.Tint = 0x00FF00;
    ok(DwmLiberMicaPixel(0x101010, 0x101010, 255, 0xFF0000, &Mica) == 0x00FF00, "an opaque tint hides the wallpaper\n");
}

START_TEST(Material)
{
    TestPacking();
    TestResolve();
    TestRejected();
    TestBlur();
    TestDim();
    TestFade();
    TestWindowMotion();
    TestSnapZones();
    TestOpenMotion();
    TestCloseMotion();
    TestCaptionButtons();
    TestFrameShadow();
    TestFrameEdge();
    TestMicaSchedule();
    TestMicaBuild();
    TestMicaTint();
    TestMicaLuma();
    TestMicaWindow();
    TestMicaPixel();
}
