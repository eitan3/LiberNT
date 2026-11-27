/*
 * PROJECT:     LiberNT Liber UI - Unit-tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test the generated design tokens and the theme derivation
 * COPYRIGHT:   Copyright 2026 Eitan Hadar <eitanhadar4@gmail.com>
 */

#include <apitest.h>

#include <liberui.h>

#include <math.h>

using namespace LiberUI;

static BOOL Near(Argb actual, Argb expected)
{
    for (int shift = 0; shift < 32; shift += 8)
    {
        int a = (int)((actual >> shift) & 0xFF);
        int e = (int)((expected >> shift) & 0xFF);
        if (abs(a - e) > 1)
            return FALSE;
    }
    return TRUE;
}

static void TestGeneratedConstants()
{
    ok(LIBER_TOKENS_VERSION == 1, "version %d\n", LIBER_TOKENS_VERSION);
    ok(LIBER_RADIUS_FLYOUT == 28 && LIBER_FLYOUT_PAD == 12, "flyout radius %d pad %d\n", LIBER_RADIUS_FLYOUT,
       LIBER_FLYOUT_PAD);
    ok(LIBER_RADIUS_TILE == LIBER_RADIUS_FLYOUT - LIBER_FLYOUT_PAD, "tile radius %d\n", LIBER_RADIUS_TILE);
    ok(LIBER_RADIUS_TILE_ITEM == LIBER_RADIUS_TILE - LIBER_TILE_PAD && LIBER_RADIUS_TILE_ITEM == 10,
       "tile item radius %d\n", LIBER_RADIUS_TILE_ITEM);
    ok(LIBER_RADIUS_GROUP == 10 && LIBER_RADIUS_GROUP_INNER == 3 && LIBER_GROUP_GAP == 2, "group radii\n");
    ok(LIBER_TASKBAR_DOCK == 76 && LIBER_TASKBAR_DOCK_TOUCHED == 60 && LIBER_TASKBAR_CLASSIC == 48, "taskbar heights\n");
    ok(LIBER_DOCK_CELL == 48 && LIBER_DOCK_ICON == 36 && LIBER_DOCK_RUNNING_DOT == 5, "dock metrics\n");
    ok(LIBER_DUR_INSTANT == 80 && LIBER_DUR_NORMAL == 220 && LIBER_DUR_SPRING == 520 && LIBER_DUR_ADAPTIVE_TINT == 420,
       "durations\n");
    ok(LIBER_SPACE_12 == 12 && LIBER_ICON_48 == 48, "space and icon sizes\n");

    ok(LiberTypeRamp[LIBER_TYPE_CAPTION].Size == 12 && LiberTypeRamp[LIBER_TYPE_CAPTION].LineHeight == 16,
       "caption ramp\n");
    ok(LiberTypeRamp[LIBER_TYPE_BODY_STRONG].Weight == 600 && LiberTypeRamp[LIBER_TYPE_BODY].Weight == 400,
       "body weights\n");
    ok(LiberTypeRamp[LIBER_TYPE_DISPLAY].Size == 68 && LiberTypeRamp[LIBER_TYPE_DISPLAY].TrackingPermille == -20,
       "display ramp\n");
    for (int i = 0; i < LIBER_TYPE_COUNT; i++)
        ok(LiberTypeRamp[i].Weight == 400 || LiberTypeRamp[i].Weight == 600, "type %d weight %d\n", i,
           LiberTypeRamp[i].Weight);

    ok(LiberMaterials[LIBER_MATERIAL_ULTRATHIN].Blur == 14 && LiberMaterials[LIBER_MATERIAL_MICA].Blur == 90,
       "material blurs\n");
    ok(LiberMaterials[LIBER_MATERIAL_THICK].K == 1.14f && LiberMaterials[LIBER_MATERIAL_THIN].K == 0.78f,
       "material k\n");
    ok(LiberShadowsLight[LIBER_ELEVATION_E16].Ambient.Y == 8 && LiberShadowsLight[LIBER_ELEVATION_E16].Ambient.Blur == 26
       && LiberShadowsLight[LIBER_ELEVATION_E16].Ambient.Argb == 0x21000000, "shadow 16 ambient\n");
    ok(LiberShadowsDark[LIBER_ELEVATION_E64].Ambient.Argb == 0x99000000 &&
       LiberShadowsDark[LIBER_ELEVATION_E64].Contact.Argb == 0x52000000, "dark shadow 64\n");
    ok(LiberGlassLight.Edge == 0x80FFFFFF && LiberGlassDark.EdgeTop == 0x33FFFFFF, "glass edges\n");
    ok(!wcscmp(LiberAccentPresets[LIBER_ACCENT_GRAPHITE].Name, L"Graphite"), "preset names\n");
}

struct ThemeVector
{
    LIBER_COLOR Id;
    Argb Light;
    Argb Dark;
};

static void CheckTheme(const Accent &accent, SurfaceTint tint, const ThemeVector *vectors, UINT count, const char *name)
{
    ThemeTokens light, dark;
    ThemeParams params = { ThemeMode::Light, accent, tint };

    light.Build(params);
    params.Mode = ThemeMode::Dark;
    dark.Build(params);
    ok(!light.IsDark() && dark.IsDark(), "%s: modes\n", name);

    for (UINT i = 0; i < count; i++)
    {
        if (vectors[i].Light)
            ok(Near(light.Color(vectors[i].Id), vectors[i].Light), "%s light color %d = %08lX, expected %08lX\n", name,
               vectors[i].Id, light.Color(vectors[i].Id), vectors[i].Light);
        if (vectors[i].Dark)
            ok(Near(dark.Color(vectors[i].Id), vectors[i].Dark), "%s dark color %d = %08lX, expected %08lX\n", name,
               vectors[i].Id, dark.Color(vectors[i].Id), vectors[i].Dark);
    }
}

static void TestLightfall()
{
    static const ThemeVector Vectors[] =
    {
        { LIBER_COLOR_ACCENT, 0xFF1F78C2, 0xFF84BDF5 },
        { LIBER_COLOR_ACCENT_HOVER, 0xFF3086D1, 0xFF77B0E8 },
        { LIBER_COLOR_ACCENT_PRESSED, 0xFF0269B2, 0xFF6AA0D6 },
        { LIBER_COLOR_ACCENT_TEXT, 0xFF0060A8, 0xFF95C9FE },
        { LIBER_COLOR_ACCENT_SUBTLE, 0x1F1F78C2, 0x2984BDF5 },
        { LIBER_COLOR_ACCENT_RING, 0x801F78C2, 0x8C84BDF5 },
        { LIBER_COLOR_ON_ACCENT, 0xFFFFFFFF, 0xFF05101C },
        { LIBER_COLOR_SURFACE_BASE, 0xFFEDF4FB, 0xFF10171E },
        { LIBER_COLOR_SURFACE_PANE, 0xFFF3F8FE, 0xFF11181F },
        { LIBER_COLOR_SURFACE_CARD_SOLID, 0, 0xFF1A2530 },
        { LIBER_COLOR_TEXT1, 0xE6000000, 0xF5FFFFFF },
        { LIBER_COLOR_TEXT2, 0x9E000000, 0xB8FFFFFF },
        { LIBER_COLOR_INFO, 0x9E000000, 0xB8FFFFFF },
        { LIBER_COLOR_FILL_PLATE_HOVER, 0x33FFFFFF, 0x0FFFFFFF },
        { LIBER_COLOR_STROKE_CARD, 0x11000000, 0x11FFFFFF },
        { LIBER_COLOR_STROKE_CONTROL_TOP, 0x12000000, 0x1FFFFFFF },
        { LIBER_COLOR_STROKE_CONTROL_BOTTOM, 0x2B000000, 0x13FFFFFF },
        { LIBER_COLOR_SUCCESS, 0xFF0E8C41, 0xFF6ED889 },
        { LIBER_COLOR_CRITICAL, 0xFFCC272E, 0xFFFF8880 },
        { LIBER_COLOR_CAUTION, 0xFFB46E00, 0 },
        { LIBER_COLOR_DANGER_FILL, 0, 0xFFEA3C3F },
        { LIBER_COLOR_FILL_INPUT_FOCUS, 0xFFFFFFFF, 0xB8141417 },
    };
    Accent accent = { 249, 0.14, 0.56 };

    CheckTheme(accent, SurfaceTint::Soft, Vectors, _countof(Vectors), "Lightfall");
}

static void TestPresets()
{
    static const struct
    {
        LIBER_ACCENT_PRESET_ID Id;
        Argb Light[5];
        Argb Dark[5];
    } Presets[] =
    {
        { LIBER_ACCENT_AZURE, { 0xFF0071E0, 0xFF1580EF, 0xFF0061CF, 0xFF0058C4, 0xFFFFFFFF },
                              { 0xFF79BAFF, 0xFF6DADFF, 0xFF5F9EEC, 0xFF8BC7FF, 0xFF06101C } },
        { LIBER_ACCENT_INDIGO, { 0xFF5758DF, 0xFF6367EF, 0xFF4B48CE, 0xFF443EC3, 0xFFFFFFFF },
                               { 0xFFA1ADFF, 0xFF95A1FF, 0xFF8691F1, 0xFFAFBCFF, 0xFF0C0E1C } },
        { LIBER_ACCENT_ORANGE, { 0xFFD85A00, 0xFFE86904, 0xFFC74A00, 0xFFAE3200, 0xFFFFFFFF },
                               { 0xFFFA9D6B, 0xFFEC905F, 0xFFDA8152, 0xFFFFAC7F, 0xFF1A0B04 } },
        { LIBER_ACCENT_AMBER, { 0xFFC68300, 0xFFD49100, 0xFFB57300, 0xFF925200, 0xFF201301 },
                              { 0xFFDFAE64, 0xFFD2A158, 0xFFC1924B, 0xFFE9BC79, 0xFF170D02 } },
        { LIBER_ACCENT_TEAL, { 0xFF008883, 0xFF009690, 0xFF007974, 0xFF00706B, 0xFFFFFFFF },
                             { 0xFF78C8C3, 0xFF6BBBB6, 0xFF5EABA6, 0xFF8BD4CE, 0xFF001312 } },
        { LIBER_ACCENT_GRAPHITE, { 0xFF5D646F, 0xFF69717C, 0xFF4F5661, 0xFF474D58, 0xFFFFFFFF },
                                 { 0xFFB2B8C1, 0xFFA6ABB4, 0xFF969CA4, 0xFFBFC4CD, 0xFF080F1C } },
    };
    static const LIBER_COLOR Ids[5] = { LIBER_COLOR_ACCENT, LIBER_COLOR_ACCENT_HOVER, LIBER_COLOR_ACCENT_PRESSED,
                                        LIBER_COLOR_ACCENT_TEXT, LIBER_COLOR_ON_ACCENT };

    for (UINT p = 0; p < _countof(Presets); p++)
    {
        ThemeVector vectors[5];
        for (UINT i = 0; i < 5; i++)
        {
            vectors[i].Id = Ids[i];
            vectors[i].Light = Presets[p].Light[i];
            vectors[i].Dark = Presets[p].Dark[i];
        }
        char name[32];
        wsprintfA(name, "%ls", LiberAccentPresets[Presets[p].Id].Name);
        CheckTheme(AccentPreset(Presets[p].Id), SurfaceTint::Soft, vectors, 5, name);
    }

    Accent azure = AccentPreset(LIBER_ACCENT_AZURE);
    ok(azure.H == 255.0f && azure.C == 0.19f && azure.L == 0.56f, "Azure preset %f %f %f\n", azure.H, azure.C, azure.L);
}

static void TestAccentRules()
{
    ok(AccentBaseLightness(95.0) == 0.68, "boost peak %f\n", AccentBaseLightness(95.0));
    ok(AccentBaseLightness(249.0) == 0.56, "no boost %f\n", AccentBaseLightness(249.0));
    ok(AccentBaseLightness(40.0) == 0.56 && AccentBaseLightness(150.0) == 0.56, "boost edges\n");
    ok(AccentBaseLightness(76.0) == 0.639, "amber-like hue %f\n", AccentBaseLightness(76.0));
    ok(AccentBaseLightness(95.0 + 360.0) == 0.68, "hue wraps\n");

    Accent a = AccentFromWallpaper(249.0, 0.104);
    ok(a.H == 249.0 && fabs(a.C - 0.1404) < 1e-9 && a.L == 0.56, "wallpaper accent %f %f %f\n", a.H, a.C, a.L);
    a = AccentFromWallpaper(-10.0, 0.01);
    ok(a.H == 350.0 && a.C == LIBER_ACCENT_CHROMA_MIN, "low chroma clamps to %f\n", a.C);
    a = AccentFromWallpaper(10.0, 0.5);
    ok(a.C == LIBER_ACCENT_CHROMA_MAX, "high chroma clamps to %f\n", a.C);

    Accent amber = AccentPreset(LIBER_ACCENT_AMBER);
    Oklch ink = AccentInk(amber);
    ok(ink.L == LIBER_ACCENT_INK_DARK_L && ink.C == LIBER_ACCENT_INK_DARK_C && ink.H == amber.H, "dark ink on amber\n");
    ink = AccentInk(AccentPreset(LIBER_ACCENT_ORANGE));
    ok(ink.L == 1.0 && ink.C == 0.0, "white ink on orange\n");

    ok(TintFactor(SurfaceTint::Off, 0.19) == 0.0, "tint off\n");
    ok(TintFactor(SurfaceTint::Soft, 0.19) == 1.0, "tint soft\n");
    ok(TintFactor(SurfaceTint::Vivid, 0.19) == 2.5, "tint vivid\n");
    ok(fabs(TintFactor(SurfaceTint::Vivid, 0.04) - 1.25) < 1e-9, "low chroma weakens tint\n");
    ok(TintFactor(SurfaceTint::Soft, -1.0) == 0.0, "negative chroma\n");

    ok(WallpaperDim(0.2) == 0.0 && WallpaperDim(0.32) == 0.0, "no dim below threshold\n");
    ok(fabs(WallpaperDim(0.6) - 0.252) < 1e-9, "dim %f\n", WallpaperDim(0.6));
    ok(WallpaperDim(1.0) == 0.5, "dim cap\n");
}

static void TestSurfaceTint()
{
    ThemeTokens tokens;
    ThemeParams params = { ThemeMode::Light, AccentPreset(LIBER_ACCENT_AZURE), SurfaceTint::Off };

    tokens.Build(params);
    Oklch off = ArgbToOklch(tokens.Color(LIBER_COLOR_SURFACE_BASE));
    params.Tint = SurfaceTint::Vivid;
    tokens.Build(params);
    Oklch vivid = ArgbToOklch(tokens.Color(LIBER_COLOR_SURFACE_BASE));
    ok(off.C < vivid.C, "vivid tint adds chroma: %f < %f\n", off.C, vivid.C);
    Oklch expected = { 0.965, 0.004 + 0.008 * 2.5, 255.0 };
    ok(tokens.Color(LIBER_COLOR_SURFACE_BASE) == OklchToArgb(expected, 1.0), "vivid surface base %08lX\n",
       tokens.Color(LIBER_COLOR_SURFACE_BASE));
    ok(tokens.Tint() == 2.5, "tint factor %f\n", tokens.Tint());
}

static void TestMaterials()
{
    ThemeTokens tokens;
    ThemeParams params = { ThemeMode::Light, AccentPreset(LIBER_ACCENT_AZURE), SurfaceTint::Soft };

    tokens.Build(params);
    GlassTint t = tokens.Material(LIBER_MATERIAL_REGULAR, 0.08);
    ok(fabs(t.Color.L - 0.94876) < 1e-5 && fabs(t.Alpha - 0.776) < 1e-6, "light bl .08: L %f A %f\n", t.Color.L,
       t.Alpha);
    t = tokens.Material(LIBER_MATERIAL_REGULAR, 0.92);
    ok(fabs(t.Color.L - 0.98824) < 1e-5 && fabs(t.Alpha - 0.524) < 1e-6, "light bl .92: L %f A %f\n", t.Color.L, t.Alpha);
    t = tokens.Material(LIBER_MATERIAL_THICK, 0.0);
    ok(fabs(t.Alpha - 0.912) < 1e-6, "thick alpha %f\n", t.Alpha);
    t = tokens.Material(LIBER_MATERIAL_MICA, 1.0);
    ok(fabs(t.Alpha - 0.74) < 1e-6, "mica alpha %f\n", t.Alpha);
    t = tokens.Material(LIBER_MATERIAL_REGULAR, 5.0);
    ok(fabs(t.Alpha - 0.5) < 1e-6, "backdrop luma clamps, alpha %f\n", t.Alpha);
    ok(ArgbA(t.Value) == 128, "tint value alpha %u\n", ArgbA(t.Value));
    ok(fabs(t.Color.C - 0.014) < 1e-6 && t.Color.H == 255.0, "glass chroma %f hue %f\n", t.Color.C, t.Color.H);

    params.Mode = ThemeMode::Dark;
    tokens.Build(params);
    t = tokens.Material(LIBER_MATERIAL_REGULAR, 0.08);
    ok(fabs(t.Color.L - 0.3168) < 1e-5 && fabs(t.Alpha - 0.4624) < 1e-6, "dark bl .08: L %f A %f\n", t.Color.L,
       t.Alpha);
    t = tokens.Material(LIBER_MATERIAL_REGULAR, 0.92);
    ok(fabs(t.Color.L - 0.1782) < 1e-5 && fabs(t.Alpha - 0.6976) < 1e-6, "dark bl .92: L %f A %f\n", t.Color.L,
       t.Alpha);
    t = tokens.Material(LIBER_MATERIAL_THICK, 1.0);
    ok(t.Alpha <= LIBER_GLASS_ALPHA_MAX, "alpha capped %f\n", t.Alpha);
    ok(tokens.Shadow(LIBER_ELEVATION_E32).Ambient.Argb == 0x75000000, "dark shadow from the theme\n");
    ok(tokens.Glass().Saturate == 1.5f, "dark saturation\n");
}

START_TEST(Tokens)
{
    LONG failures = AssertionFailures();

    TestGeneratedConstants();
    TestLightfall();
    TestPresets();
    TestAccentRules();
    TestSurfaceTint();
    TestMaterials();
    ok(AssertionFailures() == failures, "%ld assertions failed\n", AssertionFailures() - failures);
}
