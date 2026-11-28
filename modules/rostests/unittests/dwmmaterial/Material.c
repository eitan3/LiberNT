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

START_TEST(Material)
{
    TestPacking();
    TestResolve();
    TestRejected();
    TestBlur();
    TestDim();
    TestFade();
    TestWindowMotion();
}
