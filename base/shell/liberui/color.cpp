/*
 * PROJECT:     LiberNT Liber UI
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     OKLab and OKLCH color conversion and ARGB helpers
 * COPYRIGHT:   Copyright 2026 Eitan Hadar <eitanhadar4@gmail.com>
 */

#include "precomp.h"

namespace LiberUI
{

static const double Pi = 3.14159265358979323846;

static double Cbrt(double value)
{
    return value < 0.0 ? -pow(-value, 1.0 / 3.0) : pow(value, 1.0 / 3.0);
}

static double Clamp01(double value)
{
    return value < 0.0 ? 0.0 : value > 1.0 ? 1.0 : value;
}

static BYTE ToByte(double value)
{
    return (BYTE)floor(Clamp01(value) * 255.0 + 0.5);
}

double SrgbToLinear(double value)
{
    double magnitude = fabs(value);
    double result = magnitude <= 0.04045 ? magnitude / 12.92 : pow((magnitude + 0.055) / 1.055, 2.4);
    return value < 0.0 ? -result : result;
}

double LinearToSrgb(double value)
{
    double magnitude = fabs(value);
    double result = magnitude <= 0.0031308 ? magnitude * 12.92 : 1.055 * pow(magnitude, 1.0 / 2.4) - 0.055;
    return value < 0.0 ? -result : result;
}

double NormalizeHue(double hue)
{
    double result = fmod(hue, 360.0);
    if (result < 0.0)
        result += 360.0;
    LUI_ASSERT(result >= 0.0 && result < 360.0);
    return result;
}

double HueDistance(double a, double b)
{
    double d = fabs(NormalizeHue(a) - NormalizeHue(b));
    return d > 180.0 ? 360.0 - d : d;
}

Oklab OklchToOklab(const Oklch &color)
{
    double h = color.H * Pi / 180.0;
    Oklab lab = { color.L, color.C * cos(h), color.C * sin(h) };
    return lab;
}

Oklch OklabToOklch(const Oklab &color)
{
    double c = sqrt(color.A * color.A + color.B * color.B);
    double h = c < 1e-9 ? 0.0 : NormalizeHue(atan2(color.B, color.A) * 180.0 / Pi);
    Oklch lch = { color.L, c, h };
    return lch;
}

LinearRgb OklabToLinearRgb(const Oklab &color)
{
    double l = color.L + 0.3963377774 * color.A + 0.2158037573 * color.B;
    double m = color.L - 0.1055613458 * color.A - 0.0638541728 * color.B;
    double s = color.L - 0.0894841775 * color.A - 1.2914855480 * color.B;

    l = l * l * l;
    m = m * m * m;
    s = s * s * s;

    LinearRgb rgb = {
        +4.0767416621 * l - 3.3077115913 * m + 0.2309699292 * s,
        -1.2684380046 * l + 2.6097574011 * m - 0.3413193965 * s,
        -0.0041960863 * l - 0.7034186147 * m + 1.7076147010 * s,
    };
    return rgb;
}

Oklab LinearRgbToOklab(const LinearRgb &color)
{
    double l = Cbrt(0.4122214708 * color.R + 0.5363325363 * color.G + 0.0514459929 * color.B);
    double m = Cbrt(0.2119034982 * color.R + 0.6806995451 * color.G + 0.1073969566 * color.B);
    double s = Cbrt(0.0883024619 * color.R + 0.2817188376 * color.G + 0.6299787005 * color.B);

    Oklab lab = {
        0.2104542553 * l + 0.7936177850 * m - 0.0040720468 * s,
        1.9779984951 * l - 2.4285922050 * m + 0.4505937099 * s,
        0.0259040371 * l + 0.7827717662 * m - 0.8086757660 * s,
    };
    return lab;
}

Argb OklchToArgb(const Oklch &color, double alpha)
{
    LinearRgb rgb = OklabToLinearRgb(OklchToOklab(color));
    return MakeArgb(ToByte(alpha), ToByte(LinearToSrgb(rgb.R)), ToByte(LinearToSrgb(rgb.G)),
                    ToByte(LinearToSrgb(rgb.B)));
}

Oklch ArgbToOklch(Argb color)
{
    LinearRgb rgb = { SrgbToLinear(ArgbR(color) / 255.0), SrgbToLinear(ArgbG(color) / 255.0),
                      SrgbToLinear(ArgbB(color) / 255.0) };
    return OklabToOklch(LinearRgbToOklab(rgb));
}

Argb WithAlpha(Argb color, double alpha)
{
    return (color & 0x00FFFFFF) | ((Argb)ToByte(alpha) << 24);
}

static BYTE MulDiv255(UINT a, UINT b)
{
    UINT v = a * b + 128;
    return (BYTE)((v + (v >> 8)) >> 8);
}

Argb Premultiply(Argb color)
{
    BYTE a = ArgbA(color);
    return MakeArgb(a, MulDiv255(ArgbR(color), a), MulDiv255(ArgbG(color), a), MulDiv255(ArgbB(color), a));
}

Argb BlendOver(Argb top, Argb bottom)
{
    double ta = ArgbA(top) / 255.0;
    double ba = ArgbA(bottom) / 255.0;
    double oa = ta + ba * (1.0 - ta);
    if (oa <= 0.0)
        return 0;

    double r = (ArgbR(top) * ta + ArgbR(bottom) * ba * (1.0 - ta)) / oa;
    double g = (ArgbG(top) * ta + ArgbG(bottom) * ba * (1.0 - ta)) / oa;
    double b = (ArgbB(top) * ta + ArgbB(bottom) * ba * (1.0 - ta)) / oa;
    return MakeArgb(ToByte(oa), ToByte(r / 255.0), ToByte(g / 255.0), ToByte(b / 255.0));
}

Argb Mix(Argb from, Argb to, double t)
{
    t = Clamp01(t);
    return MakeArgb(ToByte((ArgbA(from) + (ArgbA(to) - ArgbA(from)) * t) / 255.0),
                    ToByte((ArgbR(from) + (ArgbR(to) - ArgbR(from)) * t) / 255.0),
                    ToByte((ArgbG(from) + (ArgbG(to) - ArgbG(from)) * t) / 255.0),
                    ToByte((ArgbB(from) + (ArgbB(to) - ArgbB(from)) * t) / 255.0));
}

double Luma(Argb color)
{
    return (0.2126 * ArgbR(color) + 0.7152 * ArgbG(color) + 0.0722 * ArgbB(color)) / 255.0;
}

}
