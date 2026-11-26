/*
 * PROJECT:     LiberNT Liber UI
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Resolve the design tokens of a theme from its accent and surface tint
 * COPYRIGHT:   Copyright 2026 Eitan Hadar <eitanhadar4@gmail.com>
 */

#include "precomp.h"

namespace LiberUI
{

static LONG g_AssertFailures;

void AssertFailed(const char *file, int line, const char *condition)
{
    char text[512];

    InterlockedIncrement(&g_AssertFailures);
    wsprintfA(text, "LiberUI: %s(%d): assertion %s failed\n", file, line, condition);
    OutputDebugStringA(text);
}

LONG AssertionFailures()
{
    return g_AssertFailures;
}

static double Clamp(double value, double low, double high)
{
    return value < low ? low : value > high ? high : value;
}

Accent AccentPreset(LIBER_ACCENT_PRESET_ID id)
{
    LUI_ASSERT(id >= 0 && id < LIBER_ACCENT_PRESET_COUNT);
    if (id < 0 || id >= LIBER_ACCENT_PRESET_COUNT)
        id = LIBER_ACCENT_AZURE;

    const LIBER_ACCENT_PRESET &preset = LiberAccentPresets[id];
    Accent accent = { preset.H, preset.C, preset.L };
    return accent;
}

double AccentBaseLightness(double hue)
{
    double boost = 1.0 - HueDistance(hue, LIBER_ACCENT_BOOST_HUE) / LIBER_ACCENT_BOOST_WIDTH;
    double l = LIBER_ACCENT_BASE_L + LIBER_ACCENT_BOOST_L * (boost > 0.0 ? boost : 0.0);
    return floor(l * 1000.0 + 0.5) / 1000.0;
}

Accent AccentFromWallpaper(double hue, double chroma)
{
    Accent accent;

    accent.H = NormalizeHue(hue);
    accent.C = Clamp(chroma * LIBER_ACCENT_WALLPAPER_CHROMA_SCALE, LIBER_ACCENT_CHROMA_MIN, LIBER_ACCENT_CHROMA_MAX);
    accent.L = AccentBaseLightness(accent.H);
    LUI_ASSERT(accent.C >= LIBER_ACCENT_CHROMA_MIN && accent.C <= LIBER_ACCENT_CHROMA_MAX);
    return accent;
}

Oklch AccentInk(const Accent &accent)
{
    Oklch ink;

    if (accent.L > LIBER_ACCENT_INK_THRESHOLD_L)
    {
        ink.L = LIBER_ACCENT_INK_DARK_L;
        ink.C = LIBER_ACCENT_INK_DARK_C;
    }
    else
    {
        ink.L = LIBER_ACCENT_INK_LIGHT_L;
        ink.C = LIBER_ACCENT_INK_LIGHT_C;
    }
    ink.H = accent.H;
    return ink;
}

double TintFactor(SurfaceTint tint, double accentChroma)
{
    UINT level = (UINT)tint;

    LUI_ASSERT(level < LIBER_TINT_LEVEL_COUNT);
    if (level >= LIBER_TINT_LEVEL_COUNT)
        level = (UINT)SurfaceTint::Soft;
    if (accentChroma < 0.0)
        accentChroma = 0.0;

    double weight = accentChroma / LIBER_TINT_CHROMA_REF;
    return LiberTintLevels[level] * (weight < 1.0 ? weight : 1.0);
}

double WallpaperDim(double averageLuma)
{
    return Clamp((averageLuma - LIBER_DIM_THRESHOLD) * LIBER_DIM_SCALE, 0.0, LIBER_DIM_MAX);
}

ThemeTokens::ThemeTokens()
{
    ThemeParams params = { ThemeMode::Light, AccentPreset(LIBER_ACCENT_AZURE), SurfaceTint::Soft };
    Build(params);
}

Argb ThemeTokens::Evaluate(const LIBER_COLOR_DEF &def) const
{
    const Accent &accent = m_Params.AccentColor;
    Oklch color;

    switch (def.Kind)
    {
        case LIBER_COLOR_KIND_FIXED:
            return def.Argb;

        case LIBER_COLOR_KIND_OKLCH:
            color.L = def.L;
            color.C = def.C;
            color.H = def.H;
            break;

        case LIBER_COLOR_KIND_ACCENT:
            color.L = def.Relative ? accent.L + def.L : def.L;
            if (def.LMax > 0.0f && color.L > def.LMax)
                color.L = def.LMax;
            color.C = def.CScale != 0.0f ? accent.C * def.CScale : def.C;
            color.H = accent.H;
            break;

        case LIBER_COLOR_KIND_INK:
            color = AccentInk(accent);
            break;

        case LIBER_COLOR_KIND_TINT:
            color.L = def.L;
            color.C = def.C + def.C1 * m_Tint;
            color.H = accent.H;
            break;

        default:
            LUI_ASSERT(FALSE);
            return 0;
    }

    LUI_ASSERT(color.L >= 0.0 && color.L <= 1.0 && color.C >= 0.0);
    return OklchToArgb(color, def.Alpha);
}

void ThemeTokens::Build(const ThemeParams &params)
{
    LUI_ASSERT(params.Mode == ThemeMode::Light || params.Mode == ThemeMode::Dark);
    LUI_ASSERT(params.AccentColor.C >= 0.0 && params.AccentColor.L >= 0.0 && params.AccentColor.L <= 1.0);

    m_Params = params;
    m_Params.AccentColor.H = NormalizeHue(params.AccentColor.H);
    m_Tint = TintFactor(params.Tint, params.AccentColor.C);

    const LIBER_COLOR_DEF *defs = IsDark() ? LiberColorsDark : LiberColorsLight;
    for (UINT i = 0; i < LIBER_COLOR_COUNT; i++)
        m_Colors[i] = Evaluate(defs[i]);
}

Argb ThemeTokens::Color(LIBER_COLOR id) const
{
    LUI_ASSERT(id >= 0 && id < LIBER_COLOR_COUNT);
    if (id < 0 || id >= LIBER_COLOR_COUNT)
        return 0;
    return m_Colors[id];
}

const LIBER_GLASS &ThemeTokens::Glass() const
{
    return IsDark() ? LiberGlassDark : LiberGlassLight;
}

const LIBER_SHADOW &ThemeTokens::Shadow(LIBER_ELEVATION id) const
{
    LUI_ASSERT(id >= 0 && id < LIBER_ELEVATION_COUNT);
    if (id < 0 || id >= LIBER_ELEVATION_COUNT)
        id = LIBER_ELEVATION_E2;
    return IsDark() ? LiberShadowsDark[id] : LiberShadowsLight[id];
}

GlassTint ThemeTokens::Material(LIBER_MATERIAL_KIND kind, double backdropLuma) const
{
    const LIBER_GLASS &glass = Glass();
    GlassTint tint;

    LUI_ASSERT(kind >= 0 && kind < LIBER_MATERIAL_COUNT);
    if (kind < 0 || kind >= LIBER_MATERIAL_COUNT)
        kind = LIBER_MATERIAL_REGULAR;

    double bl = Clamp(backdropLuma, 0.0, 1.0);
    double a0 = kind == LIBER_MATERIAL_MICA ? glass.MicaA0 : glass.A0;
    double a1 = kind == LIBER_MATERIAL_MICA ? glass.MicaA1 : glass.A1;

    tint.Color.L = glass.L0 + (glass.L1 - glass.L0) * bl;
    tint.Color.C = glass.C0 + glass.C1 * m_Tint;
    tint.Color.H = m_Params.AccentColor.H;
    tint.Alpha = Clamp((a0 + (a1 - a0) * bl) * LiberMaterials[kind].K, 0.0, LIBER_GLASS_ALPHA_MAX);
    tint.Value = OklchToArgb(tint.Color, tint.Alpha);
    return tint;
}

}
