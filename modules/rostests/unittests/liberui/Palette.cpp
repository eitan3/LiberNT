/*
 * PROJECT:     LiberNT Liber UI - Unit-tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test the wallpaper accent extraction on synthetic images
 * COPYRIGHT:   Copyright 2026 Eitan Hadar <eitanhadar4@gmail.com>
 */

#include <apitest.h>

#include <liberui.h>

#include <math.h>
#include <stdlib.h>

using namespace LiberUI;

static DWORD *MakeImage(UINT width, UINT height, Argb left, Argb right, UINT split)
{
    DWORD *pixels = (DWORD *)malloc(width * height * sizeof(DWORD));
    if (!pixels)
        return NULL;
    for (UINT y = 0; y < height; y++)
        for (UINT x = 0; x < width; x++)
            pixels[y * width + x] = x < split ? left : right;
    return pixels;
}

static void TestSolid()
{
    PaletteResult result;
    DWORD *pixels = MakeImage(192, 108, 0xFF1F78C2, 0xFF1F78C2, 192);
    Oklch source = ArgbToOklch(0xFF1F78C2);

    ok(ExtractPalette(pixels, 192, 108, 192 * 4, &result), "ExtractPalette failed\n");
    ok(result.Count == 1, "%u candidates\n", result.Count);
    ok(result.Candidates[0].AccentColor.H == floor(source.H + 0.5), "hue %f, expected %f\n",
       result.Candidates[0].AccentColor.H, floor(source.H + 0.5));
    double chroma = source.C * LIBER_ACCENT_WALLPAPER_CHROMA_SCALE;
    if (chroma > LIBER_ACCENT_CHROMA_MAX)
        chroma = LIBER_ACCENT_CHROMA_MAX;
    ok(fabs(result.Candidates[0].AccentColor.C - chroma) < 0.0006, "chroma %f, expected %f\n",
       result.Candidates[0].AccentColor.C, chroma);
    ok(result.Candidates[0].AccentColor.L == AccentBaseLightness(result.Candidates[0].AccentColor.H), "lightness %f\n",
       result.Candidates[0].AccentColor.L);
    ok(fabs(result.AverageLuma - Luma(0xFF1F78C2)) < 1e-6, "luma %f\n", result.AverageLuma);
    ok(fabs(result.Candidates[0].Score - source.C) < 1e-3, "score %f\n", result.Candidates[0].Score);
    free(pixels);
}

static void TestTwoHues()
{
    PaletteResult result;
    DWORD *pixels = MakeImage(200, 100, 0xFFD42F34, 0xFF0071E0, 120);
    double red = ArgbToOklch(0xFFD42F34).H;
    double blue = ArgbToOklch(0xFF0071E0).H;

    ok(ExtractPalette(pixels, 200, 100, 200 * 4, &result), "ExtractPalette failed\n");
    ok(result.Count == 2, "%u candidates\n", result.Count);
    ok(HueDistance(result.Candidates[0].AccentColor.H, red) <= 1.0, "first hue %f, expected red %f\n",
       result.Candidates[0].AccentColor.H, red);
    ok(HueDistance(result.Candidates[1].AccentColor.H, blue) <= 1.0, "second hue %f, expected blue %f\n",
       result.Candidates[1].AccentColor.H, blue);
    ok(result.Candidates[0].Score > result.Candidates[1].Score, "candidates are ranked\n");
    ok(HueDistance(result.Candidates[0].AccentColor.H, result.Candidates[1].AccentColor.H) >= LIBER_PALETTE_MIN_HUE_GAP,
       "candidates are apart\n");
    free(pixels);
}

static void TestCloseHues()
{
    PaletteResult result;
    Oklch a = { 0.56, 0.15, 230 };
    Oklch b = { 0.56, 0.15, 255 };
    DWORD *pixels = MakeImage(96, 54, OklchToArgb(a, 1.0), OklchToArgb(b, 1.0), 60);

    ok(ExtractPalette(pixels, 96, 54, 96 * 4, &result), "ExtractPalette failed\n");
    ok(result.Count == 1, "hues 25 degrees apart gave %u candidates\n", result.Count);
    double expected = ArgbToOklch(OklchToArgb(a, 1.0)).H;
    ok(HueDistance(result.Candidates[0].AccentColor.H, expected) <= 1.0, "larger area wins, hue %f, expected %f\n",
       result.Candidates[0].AccentColor.H, expected);
    free(pixels);
}

static void TestMonochrome()
{
    PaletteResult result;
    DWORD *pixels = MakeImage(64, 64, 0xFF202020, 0xFFC0C0C0, 32);

    ok(ExtractPalette(pixels, 64, 64, 64 * 4, &result), "ExtractPalette failed\n");
    ok(result.Count == 1, "%u candidates\n", result.Count);
    ok(result.Candidates[0].AccentColor.C == LIBER_ACCENT_MONOCHROME_C &&
       result.Candidates[0].AccentColor.L == LIBER_ACCENT_MONOCHROME_L, "low-chroma candidate %f %f\n",
       result.Candidates[0].AccentColor.C, result.Candidates[0].AccentColor.L);
    ok(result.Candidates[0].AccentColor.H == LIBER_ACCENT_FALLBACK_HUE, "fallback hue %f\n",
       result.Candidates[0].AccentColor.H);
    ok(fabs(result.AverageLuma - (Luma(0xFF202020) + Luma(0xFFC0C0C0)) / 2.0) < 1e-3, "luma %f\n", result.AverageLuma);
    free(pixels);
}

static void TestTooSmallArea()
{
    PaletteResult result;
    DWORD *pixels = MakeImage(100, 100, 0xFF0071E0, 0xFF808080, 1);

    ok(ExtractPalette(pixels, 100, 100, 100 * 4, &result), "ExtractPalette failed\n");
    ok(result.Count == 1 && result.Candidates[0].AccentColor.C == LIBER_ACCENT_MONOCHROME_C,
       "a 1%% sliver is ignored: %u candidates, chroma %f\n", result.Count, result.Candidates[0].AccentColor.C);
    free(pixels);
}

static void TestArguments()
{
    PaletteResult result;
    DWORD pixel = 0xFF0071E0;
    DWORD padded[8] = { 0xFF0071E0, 0xFF0071E0, 0, 0, 0xFF0071E0, 0xFF0071E0, 0, 0 };

    ok(!ExtractPalette(NULL, 1, 1, 4, &result), "NULL pixels accepted\n");
    ok(!ExtractPalette(&pixel, 0, 1, 4, &result), "zero width accepted\n");
    ok(!ExtractPalette(&pixel, 1, 1, 2, &result), "short stride accepted\n");
    ok(!ExtractPalette(&pixel, 1, 1, 4, NULL), "NULL result accepted\n");
    ok(ExtractPalette(&pixel, 1, 1, 4, &result) && result.Count == 1, "1x1 image\n");
    ok(ExtractPalette(padded, 2, 2, 16, &result) && result.Count == 1 &&
       HueDistance(result.Candidates[0].AccentColor.H, ArgbToOklch(0xFF0071E0).H) <= 1.0, "padded stride\n");
}

START_TEST(Palette)
{
    LONG failures = AssertionFailures();

    TestSolid();
    TestTwoHues();
    TestCloseHues();
    TestMonochrome();
    TestTooSmallArea();
    TestArguments();
    ok(AssertionFailures() == failures, "%ld assertions failed\n", AssertionFailures() - failures);
}
