/*
 * PROJECT:     LiberNT Liber UI - Unit-tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test OKLCH conversion and ARGB helpers against a CSS Color 4 renderer
 * COPYRIGHT:   Copyright 2026 Eitan Hadar <eitanhadar4@gmail.com>
 */

#include <apitest.h>

#include <liberui.h>

#include <math.h>

using namespace LiberUI;

static BOOL Near(Argb actual, Argb expected, int tolerance)
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

static void CheckColor(double l, double c, double h, double alpha, Argb expected)
{
    Oklch color = { l, c, h };
    Argb actual = OklchToArgb(color, alpha);
    ok(Near(actual, expected, 1), "oklch(%.3f %.4f %.1f / %.2f) = %08lX, expected %08lX\n", l, c, h, alpha, actual,
       expected);
}

struct RendererVector
{
    double L, C, H;
    Argb Expected;
};

static const RendererVector Vectors[] =
{
    { 0.56, 0.19, 255, 0xFF0071E0 },
    { 0.605, 0.19, 255, 0xFF1580EF },
    { 0.51, 0.19, 255, 0xFF0061CF },
    { 0.48, 0.19, 255, 0xFF0058C4 },
    { 0.78, 0.1368, 255, 0xFF79BAFF },
    { 0.17, 0.03, 255, 0xFF06101C },
    { 0.54, 0.2, 277, 0xFF5758DF },
    { 0.55, 0.2, 302, 0xFF8949D0 },
    { 0.58, 0.2, 352, 0xFFCB3587 },
    { 0.57, 0.2, 25, 0xFFD42F34 },
    { 0.62, 0.18, 48, 0xFFD85A00 },
    { 0.66, 0.15, 76, 0xFFC68300 },
    { 0.2, 0.04, 76, 0xFF201301 },
    { 0.56, 0.16, 150, 0xFF008D3C },
    { 0.56, 0.11, 190, 0xFF008883 },
    { 0.5, 0.02, 260, 0xFF5D646F },
    { 0.56, 0.14, 249, 0xFF1F78C2 },
    { 0.78, 0.1008, 249, 0xFF84BDF5 },
    { 0.17, 0.03, 249, 0xFF05101C },
    { 0.56, 0.15, 150, 0xFF0E8C41 },
    { 0.55, 0.2, 25, 0xFFCC272E },
    { 0.60, 0.14, 70, 0xFFB46E00 },
    { 0.80, 0.15, 150, 0xFF6ED889 },
    { 0.76, 0.15, 25, 0xFFFF8880 },
    { 0.62, 0.21, 25, 0xFFEA3C3F },
    { 0.7, 0.4, 150, 0xFF00D600 },
    { 0.9, 0.3, 30, 0xFFFF7053 },
};

static void TestRendererVectors()
{
    for (UINT i = 0; i < _countof(Vectors); i++)
        CheckColor(Vectors[i].L, Vectors[i].C, Vectors[i].H, 1.0, Vectors[i].Expected);
}

static void TestRoundTrip()
{
    static const Argb Samples[] = { 0xFF1F78C2, 0xFF84BDF5, 0xFFEDF4FB, 0xFF10171E, 0xFF808080, 0xFFC68300 };

    for (UINT i = 0; i < _countof(Samples); i++)
    {
        Oklch lch = ArgbToOklch(Samples[i]);
        Argb back = OklchToArgb(lch, 1.0);
        ok(back == Samples[i], "round trip of %08lX gave %08lX\n", Samples[i], back);
    }

    Oklch white = ArgbToOklch(0xFFFFFFFF);
    ok(fabs(white.L - 1.0) < 1e-4 && white.C < 1e-4, "white is L %f C %f\n", white.L, white.C);
    Oklch black = ArgbToOklch(0xFF000000);
    ok(fabs(black.L) < 1e-6 && black.C < 1e-6, "black is L %f C %f\n", black.L, black.C);
    Oklch gray = ArgbToOklch(0xFF777777);
    ok(gray.C < 1e-4, "gray has chroma %f\n", gray.C);
}

static void TestTransfer()
{
    ok(SrgbToLinear(0.0) == 0.0 && fabs(SrgbToLinear(1.0) - 1.0) < 1e-12, "sRGB transfer endpoints\n");
    ok(fabs(SrgbToLinear(0.04045) - 0.04045 / 12.92) < 1e-12, "sRGB linear segment\n");
    for (int i = 0; i <= 255; i++)
    {
        double v = i / 255.0;
        if (fabs(LinearToSrgb(SrgbToLinear(v)) - v) > 1e-9)
        {
            ok(FALSE, "transfer round trip failed at %d\n", i);
            break;
        }
    }
    ok(SrgbToLinear(-0.5) == -SrgbToLinear(0.5), "transfer is odd-symmetric for out-of-gamut values\n");
}

static void TestHelpers()
{
    ok(MakeArgb(0x12, 0x34, 0x56, 0x78) == 0x12345678, "MakeArgb\n");
    ok(ArgbToColorRef(0x80112233) == RGB(0x11, 0x22, 0x33), "ArgbToColorRef\n");
    ok(WithAlpha(0xFF112233, 0.5) == 0x80112233, "WithAlpha 0.5 = %08lX\n", WithAlpha(0xFF112233, 0.5));
    ok(WithAlpha(0x00112233, 2.0) == 0xFF112233, "WithAlpha clamps\n");
    ok(Premultiply(0xFFFF8000) == 0xFFFF8000, "opaque premultiply is identity\n");
    ok(Premultiply(0x80FFFFFF) == 0x80808080, "Premultiply = %08lX\n", Premultiply(0x80FFFFFF));
    ok(Premultiply(0x00FFFFFF) == 0, "transparent premultiply is zero\n");
    ok(BlendOver(0xFFFF0000, 0xFF0000FF) == 0xFFFF0000, "opaque over\n");
    ok(BlendOver(0x00FF0000, 0xFF0000FF) == 0xFF0000FF, "transparent over\n");
    ok(Near(BlendOver(0x80FFFFFF, 0xFF000000), 0xFF808080, 1), "half white over black = %08lX\n",
       BlendOver(0x80FFFFFF, 0xFF000000));
    ok(BlendOver(0, 0) == 0, "nothing over nothing\n");
    ok(Mix(0xFF000000, 0xFFFFFFFF, 0.5) == 0xFF808080, "Mix = %08lX\n", Mix(0xFF000000, 0xFFFFFFFF, 0.5));
    ok(Mix(0xFF000000, 0xFFFFFFFF, 3.0) == 0xFFFFFFFF, "Mix clamps\n");
    ok(fabs(Luma(0xFFFFFFFF) - 1.0) < 1e-9 && Luma(0xFF000000) == 0.0, "Luma endpoints\n");
    ok(fabs(Luma(0xFF00FF00) - 0.7152) < 1e-9, "Luma of green = %f\n", Luma(0xFF00FF00));
    ok(NormalizeHue(-30.0) == 330.0 && NormalizeHue(720.0) == 0.0, "NormalizeHue\n");
    ok(HueDistance(350.0, 10.0) == 20.0 && HueDistance(10.0, 350.0) == 20.0, "HueDistance wraps\n");
    ok(HueDistance(0.0, 180.0) == 180.0 && HueDistance(95.0, 249.0) == 154.0, "HueDistance\n");
}

START_TEST(Color)
{
    LONG failures = AssertionFailures();

    TestRendererVectors();
    TestRoundTrip();
    TestTransfer();
    TestHelpers();
    ok(AssertionFailures() == failures, "%ld assertions failed\n", AssertionFailures() - failures);
}
