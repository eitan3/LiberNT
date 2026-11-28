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
    TestCaptionButtons();
    TestFrameShadow();
    TestFrameEdge();
}
