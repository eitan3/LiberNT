/*
 * PROJECT:     LiberNT DWM - Unit-tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test decoding of the Liber material window property and its backdrop blur
 * COPYRIGHT:   Copyright 2026 Eitan Hadar <eitanhadar4@gmail.com>
 */

#include <apitest.h>

#include <windows.h>

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

START_TEST(Material)
{
    TestPacking();
    TestResolve();
    TestRejected();
    TestBlur();
}
