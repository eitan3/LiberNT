/*
 * PROJECT:     LiberNT Desktop Window Manager
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Decode the Liber material a window requests
 * COPYRIGHT:   Copyright 2026 Eitan Hadar <eitanhadar4@gmail.com>
 */

#include <windows.h>

#include "libermaterial.h"

#include <assert.h>
#include <math.h>

C_ASSERT(DWM_LIBER_MATERIAL_MAX == LIBER_MATERIAL_COUNT);
C_ASSERT(DWM_LIBER_ELEVATION_MAX == LIBER_ELEVATION_COUNT);

BOOLEAN
DwmLiberResolveMaterial(
    _In_ const DWM_WIN *Window,
    _In_ ULONG Dpi,
    _Out_ PDWM_LIBER_MATERIAL_INFO Info)
{
    ULONG Packed, Kind, Elevation;

    assert(Window != NULL && Info != NULL);
    ZeroMemory(Info, sizeof(*Info));

    Packed = Window->LiberMaterial;
    Kind = DWM_LIBER_MATERIAL_KIND(Packed);
    Elevation = DWM_LIBER_MATERIAL_ELEVATION(Packed);
    if (!(Packed & DWM_LIBER_MATERIAL_VALID) || (Packed & ~DWM_LIBER_MATERIAL_MASK) != 0 ||
        Kind == DWM_LIBER_MATERIAL_NONE || Kind > DWM_LIBER_MATERIAL_MAX ||
        DWM_LIBER_MATERIAL_LAYER(Packed) > DWM_LIBER_LAYER_SHELL || Elevation > DWM_LIBER_ELEVATION_MAX ||
        Window->LiberMaterialTint > 0x00FFFFFF)
    {
        return FALSE;
    }

    if (Dpi == 0)
        Dpi = 96;

    Info->Kind = (LIBER_MATERIAL_KIND)(Kind - 1);
    Info->BlurRadius = (ULONG)MulDiv(LiberMaterials[Info->Kind].Blur, (int)Dpi, 96);
    Info->Layer = DWM_LIBER_MATERIAL_LAYER(Packed);
    Info->Adaptive = (Packed & DWM_LIBER_MATERIAL_ADAPTIVE) != 0;
    Info->HasElevation = Elevation != 0;
    Info->Elevation = Elevation != 0 ? (LIBER_ELEVATION)(Elevation - 1) : LIBER_ELEVATION_E2;
    Info->Edge = DWM_LIBER_MATERIAL_EDGE(Packed);
    Info->TintAlpha = (BYTE)DWM_LIBER_MATERIAL_ALPHA(Packed);
    Info->TintColor = (COLORREF)Window->LiberMaterialTint;

    assert(Info->Kind < LIBER_MATERIAL_COUNT);
    assert(!Info->HasElevation || Info->Elevation < LIBER_ELEVATION_COUNT);
    return TRUE;
}

BOOLEAN
DwmLiberBlur(
    _In_ const DWM_WIN *Window,
    _In_ ULONG Dpi,
    _In_ ULONG MaxRadius,
    _Out_ PDWM_LIBER_BLUR Blur)
{
    DWM_LIBER_MATERIAL_INFO Info;
    ULONG Luma;

    assert(Window != NULL && Blur != NULL);
    ZeroMemory(Blur, sizeof(*Blur));
    if (!DwmLiberResolveMaterial(Window, Dpi, &Info) || Info.Kind == LIBER_MATERIAL_MICA)
        return FALSE;

    Blur->Radius = min(max(Info.BlurRadius, 1u), MaxRadius);
    Luma = (GetRValue(Info.TintColor) * 54u + GetGValue(Info.TintColor) * 183u + GetBValue(Info.TintColor) * 19u) >> 8;
    Blur->Saturation = Luma < 128 ? LiberGlassDark.Saturate : LiberGlassLight.Saturate;
    return TRUE;
}

VOID
DwmLiberApplyMaterial(
    _Inout_ DWM_WIN *Window)
{
    DWM_LIBER_MATERIAL_INFO Info;

    assert(Window != NULL);
    if (!DwmLiberResolveMaterial(Window, 96, &Info) || Info.Kind == LIBER_MATERIAL_MICA)
        return;
    Window->BlurFlags |= DWM_BLUR_ENABLE | DWM_BLUR_REGION_ENTIRE_WINDOW;
    Window->BlurRectCount = 0;
}

ULONG
DwmLiberMaxBlurRadius(
    _In_ ULONG Dpi)
{
    ULONG Radius = 0;
    UINT Index;

    for (Index = 0; Index < LIBER_MATERIAL_COUNT; ++Index)
    {
        if (Index != LIBER_MATERIAL_MICA)
            Radius = max(Radius, (ULONG)LiberMaterials[Index].Blur);
    }
    return (ULONG)MulDiv((int)Radius, Dpi ? (int)Dpi : 96, 96);
}

double
DwmLiberDimAmount(
    _In_ double AverageLuma)
{
    double Dim = (AverageLuma - LIBER_DIM_THRESHOLD) * LIBER_DIM_SCALE;

    return Dim < 0.0 ? 0.0 : Dim > LIBER_DIM_MAX ? LIBER_DIM_MAX : Dim;
}

double
DwmLiberDimBackdrop(
    _Inout_ ULONG *Bits,
    _In_ ULONG Width,
    _In_ ULONG Height,
    _In_ ULONG Stride,
    _In_ COLORREF Shade)
{
    double Sum = 0.0, Dim;
    ULONG Samples = 0, X, Y, Keep, Add[3];

    assert(Bits != NULL && Stride >= Width * sizeof(ULONG));
    if (Bits == NULL || Width == 0 || Height == 0)
        return 0.0;
    for (Y = 0; Y < Height; Y += 4)
    {
        const ULONG *Row = (const ULONG *)((const BYTE *)Bits + (SIZE_T)Y * Stride);
        for (X = 0; X < Width; X += 4)
        {
            ULONG Pixel = Row[X];
            Sum += (0.2126 * ((Pixel >> 16) & 0xFF) + 0.7152 * ((Pixel >> 8) & 0xFF) + 0.0722 * (Pixel & 0xFF)) / 255.0;
            Samples++;
        }
    }
    Dim = DwmLiberDimAmount(Sum / Samples);
    if (Dim <= 0.0)
        return 0.0;
    Keep = (ULONG)((1.0 - Dim) * 256.0 + 0.5);
    Add[0] = (ULONG)(GetRValue(Shade) * Dim + 0.5);
    Add[1] = (ULONG)(GetGValue(Shade) * Dim + 0.5);
    Add[2] = (ULONG)(GetBValue(Shade) * Dim + 0.5);
    for (Y = 0; Y < Height; ++Y)
    {
        ULONG *Row = (ULONG *)((BYTE *)Bits + (SIZE_T)Y * Stride);
        for (X = 0; X < Width; ++X)
        {
            ULONG Pixel = Row[X];
            ULONG R = ((((Pixel >> 16) & 0xFF) * Keep) >> 8) + Add[0];
            ULONG G = ((((Pixel >> 8) & 0xFF) * Keep) >> 8) + Add[1];
            ULONG B = (((Pixel & 0xFF) * Keep) >> 8) + Add[2];
            Row[X] = (Pixel & 0xFF000000u) | (min(R, 255u) << 16) | (min(G, 255u) << 8) | min(B, 255u);
        }
    }
    return Dim;
}

ULONG
DwmLiberFadeAlpha(
    _In_ ULONG Alpha,
    _In_ LONG Row,
    _In_ LONG Height)
{
    assert(Alpha <= 255);
    if (Height <= 0 || Row < 0)
        return 0;
    if (Row >= Height)
        return Alpha;
    return (ULONG)(((ULONGLONG)Alpha * (ULONG)(2 * Row + 1)) / (ULONG)(2 * Height));
}

static DWORD
DwmLiberReadDword(
    _In_ PCWSTR Key,
    _In_ PCWSTR Value,
    _In_ DWORD Default)
{
    DWORD Data = Default, Size = sizeof(Data);

    if (RegGetValueW(HKEY_CURRENT_USER, Key, Value, RRF_RT_REG_DWORD, NULL, &Data, &Size) != ERROR_SUCCESS)
        return Default;
    return Data;
}

VOID
DwmLiberReadDim(
    _Out_ PDWM_LIBER_DIM Dim)
{
    static const WCHAR Design[] = L"Software\\LiberNT\\Shell\\Design";

    Dim->Enabled = DwmLiberReadDword(Design, L"Experience", 0) == 1 &&
                   DwmLiberReadDword(L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
                                     L"SystemUsesLightTheme", 1) == 0;
    Dim->Shade = DwmLiberReadDword(Design, L"WallpaperDim", 0) == 1 ? DwmLiberReadDword(Design, L"DimShade", 0) & 0xFFFFFF
                                                                   : RGB(0, 0, 0);
}

ULONG
DwmLiberSolidColor(
    _In_ const DWM_LIBER_MATERIAL_INFO *Info)
{
    assert(Info != NULL);
    return 0xFF000000u | ((ULONG)GetRValue(Info->TintColor) << 16) | ((ULONG)GetGValue(Info->TintColor) << 8) |
           (ULONG)GetBValue(Info->TintColor);
}

VOID
DwmLiberShadowLayer(
    _In_ const LIBER_SHADOW_LAYER *Token,
    _In_ ULONG Dpi,
    _Out_ PDWM_LIBER_SHADOW_LAYER Layer)
{
    assert(Token != NULL && Layer != NULL);
    assert(Token->X == 0 && Token->Y >= 0 && Token->Blur > 0);
    if (Dpi == 0)
        Dpi = 96;
    Layer->Offset = MulDiv(Token->Y, (int)Dpi, 96);
    Layer->Sigma = (double)Token->Blur * (double)Dpi / 192.0;
    if (Layer->Sigma < 0.5)
        Layer->Sigma = 0.5;
    Layer->Support = (LONG)ceil(Layer->Sigma * 2.5);
    Layer->Opacity = (((ULONG)Token->Argb >> 24) * 1000u + 127u) / 255u;
    assert(Layer->Support >= 2 && Layer->Opacity <= 1000);
}

VOID
DwmLiberWindowShadow(
    _In_ BOOLEAN Dark,
    _In_ BOOLEAN Active,
    _In_ ULONG Dpi,
    _Out_ PDWM_LIBER_SHADOW Shadow)
{
    const LIBER_SHADOW *Token = &(Dark ? LiberShadowsDark : LiberShadowsLight)[Active ? LIBER_ELEVATION_E64
                                                                                       : LIBER_ELEVATION_WINDOW_INACTIVE];

    assert(Shadow != NULL);
    DwmLiberShadowLayer(&Token->Ambient, Dpi, &Shadow->Ambient);
    DwmLiberShadowLayer(&Token->Contact, Dpi, &Shadow->Contact);
}

VOID
DwmLiberShadowMargins(
    _In_ ULONG Dpi,
    _Out_ RECT *Margins)
{
    UINT Index;

    assert(Margins != NULL);
    SetRect(Margins, DWM_LIBER_FRAME_OUTSET, DWM_LIBER_FRAME_OUTSET, DWM_LIBER_FRAME_OUTSET, DWM_LIBER_FRAME_OUTSET);
    for (Index = 0; Index < 4; ++Index)
    {
        DWM_LIBER_SHADOW Shadow;
        const DWM_LIBER_SHADOW_LAYER *Layers[2];
        UINT Layer;

        DwmLiberWindowShadow((Index & 1) != 0, (Index & 2) != 0, Dpi, &Shadow);
        Layers[0] = &Shadow.Ambient;
        Layers[1] = &Shadow.Contact;
        for (Layer = 0; Layer < 2; ++Layer)
        {
            Margins->left = max(Margins->left, Layers[Layer]->Support);
            Margins->right = max(Margins->right, Layers[Layer]->Support);
            Margins->top = max(Margins->top, Layers[Layer]->Support - Layers[Layer]->Offset);
            Margins->bottom = max(Margins->bottom, Layers[Layer]->Support + Layers[Layer]->Offset);
        }
    }
}

VOID
DwmLiberFrameEdgeColors(
    _In_ BOOLEAN Dark,
    _In_ ULONG Edge,
    _Out_ PDWM_LIBER_EDGE_COLORS Colors)
{
    const LIBER_GLASS *Glass = Dark ? &LiberGlassDark : &LiberGlassLight;

    assert(Colors != NULL);
    assert(Edge <= DWM_LIBER_EDGE_SEAM);
    Colors->Outline = Edge == DWM_LIBER_EDGE_WINDOW || Edge == DWM_LIBER_EDGE_SEAM ? (ULONG)Glass->Edge : 0;
    Colors->Bevel = Edge == DWM_LIBER_EDGE_WINDOW ? (ULONG)Glass->EdgeTop : 0;
}

static double
DwmLiberRoundRectCover(
    _In_ double X,
    _In_ double Y,
    _In_ double Left,
    _In_ double Top,
    _In_ double Right,
    _In_ double Bottom,
    _In_ double Radius)
{
    double Qx = fabs(X - (Left + Right) * 0.5) - ((Right - Left) * 0.5 - Radius);
    double Qy = fabs(Y - (Top + Bottom) * 0.5) - ((Bottom - Top) * 0.5 - Radius);
    double Ox = Qx > 0.0 ? Qx : 0.0, Oy = Qy > 0.0 ? Qy : 0.0, Inner = Qx > Qy ? Qx : Qy;
    double Cover = 0.5 - (sqrt(Ox * Ox + Oy * Oy) + (Inner < 0.0 ? Inner : 0.0) - Radius);

    return Cover <= 0.0 ? 0.0 : Cover >= 1.0 ? 1.0 : Cover;
}

VOID
DwmLiberFrameEdgeCoverage(
    _In_ LONG X,
    _In_ LONG Y,
    _In_ LONG Width,
    _In_ LONG Height,
    _In_ ULONG Radius,
    _Out_ ULONG *Outline,
    _Out_ ULONG *Bevel)
{
    double R = (double)min(Radius, (ULONG)max(min(Width, Height) / 2, 0));
    double Px = (double)X + 0.5, Py = (double)Y + 0.5, Window, Outer, Below;

    assert(Outline != NULL && Bevel != NULL);
    *Outline = 0;
    *Bevel = 0;
    if (Width <= 0 || Height <= 0)
        return;
    Window = DwmLiberRoundRectCover(Px, Py, 0.0, 0.0, Width, Height, R);
    Outer = DwmLiberRoundRectCover(Px, Py, -DWM_LIBER_FRAME_OUTSET, -DWM_LIBER_FRAME_OUTSET,
                                   (double)Width + DWM_LIBER_FRAME_OUTSET, (double)Height + DWM_LIBER_FRAME_OUTSET,
                                   R > 0.0 ? R + DWM_LIBER_FRAME_OUTSET : 0.0);
    Below = DwmLiberRoundRectCover(Px, Py, 0.0, 1.0, Width, (double)Height + 1.0, R);
    *Outline = Outer > Window ? (ULONG)((Outer - Window) * 256.0 + 0.5) : 0;
    *Bevel = (ULONG)(Window * (1.0 - Below) * 256.0 + 0.5);
    assert(*Outline <= 256 && *Bevel <= 256);
}

static ULONG
DwmLiberPixelFromColorRef(
    _In_ COLORREF Color)
{
    return ((ULONG)GetRValue(Color) << 16) | ((ULONG)GetGValue(Color) << 8) | (ULONG)GetBValue(Color);
}

static ULONG
DwmLiberClampChannel(
    _In_ LONG Value)
{
    return Value < 0 ? 0u : Value > 255 ? 255u : (ULONG)Value;
}

VOID
DwmLiberMicaSchedule(
    _In_ ULONG Dpi,
    _Out_ PDWM_LIBER_MICA_SCHEDULE Schedule)
{
    double Sigma, Factor2, Low, Ideal;
    ULONG Factor = 1, Lower, Index;
    LONG Smaller;

    assert(Schedule != NULL);
    if (Dpi == 0)
        Dpi = 96;
    Sigma = (double)LiberMaterials[LIBER_MATERIAL_MICA].Blur * (double)Dpi / 96.0;
    while (Factor * 2 <= DWM_LIBER_MICA_FACTOR_MAX && Sigma / (double)(Factor * 2) >= 4.0)
        Factor *= 2;
    Factor2 = (double)Factor * (double)Factor;
    Low = Factor == 1 ? Sigma * Sigma : (Sigma * Sigma - (Factor2 - 1.0) / 12.0 - Factor2 / 6.0) / Factor2;
    if (Low < 0.25)
        Low = 0.25;
    Ideal = sqrt(12.0 * Low / DWM_LIBER_MICA_BOXES + 1.0);
    Lower = (ULONG)floor(Ideal);
    if ((Lower & 1) == 0)
        Lower--;
    Smaller = (LONG)floor((12.0 * Low - DWM_LIBER_MICA_BOXES * (double)Lower * Lower -
                           4.0 * DWM_LIBER_MICA_BOXES * Lower - 3.0 * DWM_LIBER_MICA_BOXES) /
                          (-4.0 * Lower - 4.0) + 0.5);
    Smaller = max(0, min(Smaller, DWM_LIBER_MICA_BOXES));
    Schedule->Factor = Factor;
    for (Index = 0; Index < DWM_LIBER_MICA_BOXES; ++Index)
        Schedule->Radius[Index] = (((LONG)Index < Smaller ? Lower : Lower + 2) - 1) / 2;
    assert(Lower >= 1 && (Lower & 1) == 1);
    assert(Factor >= 1 && Factor <= DWM_LIBER_MICA_FACTOR_MAX && (Factor & (Factor - 1)) == 0);
}

double
DwmLiberMicaVariance(
    _In_ const DWM_LIBER_MICA_SCHEDULE *Schedule)
{
    double Factor2, Variance = 0.0;
    ULONG Index;

    assert(Schedule != NULL && Schedule->Factor >= 1);
    Factor2 = (double)Schedule->Factor * (double)Schedule->Factor;
    for (Index = 0; Index < DWM_LIBER_MICA_BOXES; ++Index)
    {
        double Width = 2.0 * Schedule->Radius[Index] + 1.0;
        Variance += (Width * Width - 1.0) / 12.0;
    }
    Variance *= Factor2;
    if (Schedule->Factor > 1)
        Variance += (Factor2 - 1.0) / 12.0 + Factor2 / 6.0;
    return Variance;
}

SIZE_T
DwmLiberMicaScratchCount(
    _In_ LONG Width,
    _In_ LONG Height,
    _In_ ULONG Factor)
{
    SIZE_T Columns, Rows;

    if (Width <= 0 || Height <= 0 || Factor == 0)
        return 0;
    Columns = ((SIZE_T)Width + Factor - 1) / Factor;
    Rows = ((SIZE_T)Height + Factor - 1) / Factor;
    return Columns * Rows * 3 * 2;
}

static VOID
DwmLiberBoxPass(
    _In_ const float *Source,
    _Out_ float *Dest,
    _In_ LONG Count,
    _In_ LONG Lines,
    _In_ SIZE_T Step,
    _In_ SIZE_T LineStep,
    _In_ LONG Radius)
{
    float Scale = 1.0f / (float)(2 * Radius + 1);
    LONG Line, Index, Tap;
    ULONG Channel;

    assert(Count > 0 && Radius >= 0);
    for (Line = 0; Line < Lines; ++Line)
    {
        const float *In = Source + (SIZE_T)Line * LineStep;
        float *Out = Dest + (SIZE_T)Line * LineStep;

        for (Channel = 0; Channel < 3; ++Channel)
        {
            float Sum = 0.0f;

            for (Tap = -Radius; Tap <= Radius; ++Tap)
                Sum += In[(SIZE_T)max(0, min(Tap, Count - 1)) * Step + Channel];
            for (Index = 0; Index < Count; ++Index)
            {
                Out[(SIZE_T)Index * Step + Channel] = Sum * Scale;
                Sum += In[(SIZE_T)min(Index + Radius + 1, Count - 1) * Step + Channel] -
                       In[(SIZE_T)max(Index - Radius, 0) * Step + Channel];
            }
        }
    }
}

BOOLEAN
DwmLiberMicaBuild(
    _In_ const ULONG *Wallpaper,
    _In_ LONG Width,
    _In_ LONG Height,
    _In_ ULONG Stride,
    _In_ const DWM_LIBER_MICA_SCHEDULE *Schedule,
    _Inout_ float *Scratch,
    _In_ SIZE_T ScratchCount,
    _Out_ ULONG *Output)
{
    LONG Factor, Columns, Rows, X, Y, Cell;
    float *Grid, *Temp, Inverse;
    SIZE_T Cells;
    ULONG Pass;

    assert(Schedule != NULL);
    if (Wallpaper == NULL || Output == NULL || Scratch == NULL || Width <= 0 || Height <= 0 ||
        Stride < (ULONG)Width * sizeof(ULONG) || Schedule->Factor == 0 ||
        ScratchCount < DwmLiberMicaScratchCount(Width, Height, Schedule->Factor))
    {
        return FALSE;
    }
    Factor = (LONG)Schedule->Factor;
    Columns = (Width + Factor - 1) / Factor;
    Rows = (Height + Factor - 1) / Factor;
    Cells = (SIZE_T)Columns * Rows;
    Grid = Scratch;
    Temp = Scratch + Cells * 3;
    ZeroMemory(Grid, Cells * 3 * sizeof(float));
    for (Y = 0; Y < Height; ++Y)
    {
        const ULONG *Row = (const ULONG *)((const BYTE *)Wallpaper + (SIZE_T)Y * Stride);
        float *Line = Grid + (SIZE_T)(Y / Factor) * Columns * 3;

        for (X = 0; X < Width; ++X)
        {
            float *Sum = Line + (SIZE_T)(X / Factor) * 3;
            ULONG Pixel = Row[X];

            Sum[0] += (float)((Pixel >> 16) & 0xFF);
            Sum[1] += (float)((Pixel >> 8) & 0xFF);
            Sum[2] += (float)(Pixel & 0xFF);
        }
    }
    for (Y = 0; Y < Rows; ++Y)
    {
        LONG CellHeight = min(Factor, Height - Y * Factor);

        for (X = 0; X < Columns; ++X)
        {
            LONG CellWidth = min(Factor, Width - X * Factor);
            float Scale = 1.0f / (float)(CellWidth * CellHeight);

            assert(CellWidth > 0 && CellHeight > 0);
            for (Cell = 0; Cell < 3; ++Cell)
                Grid[((SIZE_T)Y * Columns + X) * 3 + Cell] *= Scale;
        }
    }
    for (Pass = 0; Pass < DWM_LIBER_MICA_BOXES; ++Pass)
    {
        LONG Radius = (LONG)Schedule->Radius[Pass];

        DwmLiberBoxPass(Grid, Temp, Columns, Rows, 3, (SIZE_T)Columns * 3, Radius);
        DwmLiberBoxPass(Temp, Grid, Rows, Columns, (SIZE_T)Columns * 3, 3, Radius);
    }
    Inverse = 1.0f / (float)Factor;
    for (Y = 0; Y < Height; ++Y)
    {
        float Fy = ((float)Y + 0.5f) * Inverse - 0.5f, Ty;
        LONG Y0 = (LONG)floorf(Fy), Y1;
        ULONG *Row = Output + (SIZE_T)Y * Width;

        Ty = Fy - (float)Y0;
        Y1 = max(0, min(Y0 + 1, Rows - 1));
        Y0 = max(0, min(Y0, Rows - 1));
        for (X = 0; X < Width; ++X)
        {
            float Fx = ((float)X + 0.5f) * Inverse - 0.5f, Tx;
            LONG X0 = (LONG)floorf(Fx), X1;
            const float *A, *B, *C, *D;
            ULONG Value = 0xFF000000u;

            Tx = Fx - (float)X0;
            X1 = max(0, min(X0 + 1, Columns - 1));
            X0 = max(0, min(X0, Columns - 1));
            A = Grid + ((SIZE_T)Y0 * Columns + X0) * 3;
            B = Grid + ((SIZE_T)Y0 * Columns + X1) * 3;
            C = Grid + ((SIZE_T)Y1 * Columns + X0) * 3;
            D = Grid + ((SIZE_T)Y1 * Columns + X1) * 3;
            for (Cell = 0; Cell < 3; ++Cell)
            {
                float Top = A[Cell] + (B[Cell] - A[Cell]) * Tx;
                float Bottom = C[Cell] + (D[Cell] - C[Cell]) * Tx;
                LONG Channel = (LONG)floorf(Top + (Bottom - Top) * Ty + 0.5f);

                Value |= DwmLiberClampChannel(Channel) << (16 - 8 * Cell);
            }
            Row[X] = Value;
        }
    }
    return TRUE;
}

VOID
DwmLiberReadTintSource(
    _Out_ PDWM_LIBER_TINT_SOURCE Source)
{
    static const WCHAR Design[] = L"Software\\LiberNT\\Shell\\Design";
    DWORD Hue = DwmLiberReadDword(Design, L"GlassHue", (DWORD)(DWM_LIBER_TINT_HUE * 100.0));
    DWORD Tint = DwmLiberReadDword(Design, L"GlassTint", 0);

    assert(Source != NULL);
    Source->Hue = Hue < 36000 ? Hue / 100.0 : DWM_LIBER_TINT_HUE;
    Source->Tint = Tint <= 2500 ? Tint / 1000.0 : 0.0;
}

static double
DwmLiberLinearToSrgb(
    _In_ double Value)
{
    double Magnitude = fabs(Value);
    double Result = Magnitude <= 0.0031308 ? Magnitude * 12.92 : 1.055 * pow(Magnitude, 1.0 / 2.4) - 0.055;

    return Value < 0.0 ? -Result : Result;
}

static BYTE
DwmLiberUnitToByte(
    _In_ double Value)
{
    return (BYTE)floor((Value < 0.0 ? 0.0 : Value > 1.0 ? 1.0 : Value) * 255.0 + 0.5);
}

COLORREF
DwmLiberOklchToRgb(
    _In_ double L,
    _In_ double C,
    _In_ double H)
{
    double Angle = H * 3.14159265358979323846 / 180.0;
    double A = C * cos(Angle), B = C * sin(Angle);
    double Lc = L + 0.3963377774 * A + 0.2158037573 * B;
    double Mc = L - 0.1055613458 * A - 0.0638541728 * B;
    double Sc = L - 0.0894841775 * A - 1.2914855480 * B;

    Lc = Lc * Lc * Lc;
    Mc = Mc * Mc * Mc;
    Sc = Sc * Sc * Sc;
    return RGB(DwmLiberUnitToByte(DwmLiberLinearToSrgb(4.0767416621 * Lc - 3.3077115913 * Mc + 0.2309699292 * Sc)),
               DwmLiberUnitToByte(DwmLiberLinearToSrgb(-1.2684380046 * Lc + 2.6097574011 * Mc - 0.3413193965 * Sc)),
               DwmLiberUnitToByte(DwmLiberLinearToSrgb(-0.0041960863 * Lc - 0.7034186147 * Mc + 1.7076147010 * Sc)));
}

VOID
DwmLiberMicaTint(
    _In_ BOOLEAN Dark,
    _In_ double BackdropLuma,
    _In_ const DWM_LIBER_TINT_SOURCE *Source,
    _Out_ PDWM_LIBER_MICA Mica)
{
    const LIBER_GLASS *Glass = Dark ? &LiberGlassDark : &LiberGlassLight;
    double Luma = BackdropLuma < 0.0 ? 0.0 : BackdropLuma > 1.0 ? 1.0 : BackdropLuma;
    double Alpha = (Glass->MicaA0 + (Glass->MicaA1 - Glass->MicaA0) * Luma) * LiberMaterials[LIBER_MATERIAL_MICA].K;

    assert(Source != NULL && Mica != NULL);
    Alpha = Alpha < 0.0 ? 0.0 : Alpha > LIBER_GLASS_ALPHA_MAX ? LIBER_GLASS_ALPHA_MAX : Alpha;
    Mica->Tint = DwmLiberPixelFromColorRef(DwmLiberOklchToRgb(Glass->L0 + (Glass->L1 - Glass->L0) * Luma,
                                                              Glass->C0 + Glass->C1 * Source->Tint, Source->Hue));
    Mica->Alpha = (ULONG)(Alpha * 255.0 + 0.5);
    Mica->Saturation = (ULONG)(Glass->Saturate * 256.0 + 0.5);
    assert(Mica->Alpha <= 255 && Mica->Tint <= 0x00FFFFFF);
}

double
DwmLiberMicaLuma(
    _In_ const ULONG *Bits,
    _In_ LONG Width,
    _In_ LONG Height,
    _In_ ULONG Stride,
    _In_ const RECT *Rect)
{
    LONG Left, Top, Right, Bottom, Columns, Rows, I, J;
    double Sum = 0.0;

    assert(Rect != NULL);
    if (Bits == NULL || Width <= 0 || Height <= 0 || Stride < (ULONG)Width * sizeof(ULONG))
        return 0.5;
    Left = max(Rect->left, 0);
    Top = max(Rect->top, 0);
    Right = min(Rect->right, Width);
    Bottom = min(Rect->bottom, Height);
    if (Left >= Right || Top >= Bottom)
        return 0.5;
    Columns = min(Right - Left, DWM_LIBER_MICA_SAMPLES);
    Rows = min(Bottom - Top, DWM_LIBER_MICA_SAMPLES);
    for (J = 0; J < Rows; ++J)
    {
        LONG Y = Top + (LONG)(((LONGLONG)(2 * J + 1) * (Bottom - Top)) / (2 * Rows));
        const ULONG *Row = (const ULONG *)((const BYTE *)Bits + (SIZE_T)Y * Stride);

        assert(Y >= Top && Y < Bottom);
        for (I = 0; I < Columns; ++I)
        {
            ULONG Pixel = Row[Left + (LONG)(((LONGLONG)(2 * I + 1) * (Right - Left)) / (2 * Columns))];

            Sum += (0.2126 * ((Pixel >> 16) & 0xFF) + 0.7152 * ((Pixel >> 8) & 0xFF) + 0.0722 * (Pixel & 0xFF)) / 255.0;
        }
    }
    return Sum / ((double)Columns * Rows);
}

BOOLEAN
DwmWindowHasBase(
    _In_ const DWM_WIN *Window)
{
    assert(Window != NULL);
    return Window->Generation != 0 && Window->BaseUpdateId != 0;
}

BOOLEAN
DwmWindowHasContent(
    _In_ const DWM_WIN *Window,
    _In_ BOOLEAN HasDxLayer)
{
    assert(Window != NULL);
    return DwmWindowHasBase(Window) || HasDxLayer;
}

BOOLEAN
DwmLiberClassifyMica(
    _Inout_ DWM_WIN *Window)
{
    assert(Window != NULL);
    if (!(Window->LayerFlags & DWM_WINDOW_LIBER_FRAME) || (Window->LiberMaterial & DWM_LIBER_MATERIAL_VALID) ||
        Window->BackdropRegion == 0 || Window->BackdropOpacity >= 255 ||
        (Window->BackdropType != DWM_BACKDROP_TRANSIENT && Window->BackdropType != DWM_BACKDROP_MAIN))
    {
        return FALSE;
    }
    if (Window->BackdropType == DWM_BACKDROP_TRANSIENT)
        Window->BackdropRegion = DWM_BACKDROP_REGION_NONCLIENT;
    Window->BackdropType = DWM_BACKDROP_MAIN;
    Window->LiberMaterial = DWM_LIBER_MATERIAL_PACK(LIBER_MATERIAL_MICA + 1, FALSE, DWM_LIBER_LAYER_WINDOW, 0,
                                                    DWM_LIBER_EDGE_NONE, 0);
    Window->LiberMaterialTint = 0;
    assert(DWM_LIBER_MATERIAL_KIND(Window->LiberMaterial) == LIBER_MATERIAL_MICA + 1);
    return TRUE;
}

static BOOLEAN
DwmLiberIsMicaWindow(
    _In_ const DWM_WIN *Window,
    _Out_ PDWM_LIBER_MATERIAL_INFO Info)
{
    if (!(Window->LayerFlags & DWM_WINDOW_LIBER_FRAME) || Window->BackdropType != DWM_BACKDROP_MAIN)
    {
        ZeroMemory(Info, sizeof(*Info));
        return FALSE;
    }
    return DwmLiberResolveMaterial(Window, 96, Info) && Info->Kind == LIBER_MATERIAL_MICA;
}

VOID
DwmLiberApplyMica(
    _Inout_ DWM_WIN *Window,
    _In_opt_ const ULONG *MicaBits,
    _In_ LONG Width,
    _In_ LONG Height,
    _In_ LONG OriginX,
    _In_ LONG OriginY,
    _In_ const DWM_LIBER_TINT_SOURCE *Source)
{
    DWM_LIBER_MATERIAL_INFO Info;
    DWM_LIBER_MICA Mica;
    RECT Rect;

    assert(Window != NULL && Source != NULL);
    if (!DwmLiberIsMicaWindow(Window, &Info))
        return;
    if (MicaBits == NULL || Width <= 0 || Height <= 0)
    {
        Window->BackdropRegion = 0;
        return;
    }
    /* The frame paints its active or inactive key; light added near it must be measured from that one key. */
    if (Window->LayerFlags & DWM_WINDOW_ACTIVE)
        Window->BackdropColorization = Window->BackdropColor;
    else
        Window->BackdropColor = Window->BackdropColorization;
    SetRect(&Rect, Window->x - OriginX, Window->y - OriginY, Window->x - OriginX + Window->cx,
            Window->y - OriginY + Window->cy);
    DwmLiberMicaTint((Window->LayerFlags & DWM_WINDOW_DARK) != 0,
                     DwmLiberMicaLuma(MicaBits, Width, Height, (ULONG)Width * sizeof(ULONG), &Rect), Source, &Mica);
    Window->LiberMaterial = DWM_LIBER_MATERIAL_PACK(LIBER_MATERIAL_MICA + 1, FALSE, DWM_LIBER_LAYER_WINDOW, 0,
                                                    DWM_LIBER_EDGE_NONE, Mica.Alpha);
    Window->LiberMaterialTint = RGB((Mica.Tint >> 16) & 0xFF, (Mica.Tint >> 8) & 0xFF, Mica.Tint & 0xFF);
}

BOOLEAN
DwmLiberWindowMica(
    _In_ const DWM_WIN *Window,
    _Out_ PDWM_LIBER_MICA Mica)
{
    DWM_LIBER_MATERIAL_INFO Info;

    assert(Window != NULL && Mica != NULL);
    ZeroMemory(Mica, sizeof(*Mica));
    if (Window->BackdropRegion == 0 || !DwmLiberIsMicaWindow(Window, &Info))
        return FALSE;
    Mica->Tint = DwmLiberPixelFromColorRef(Info.TintColor);
    Mica->Alpha = Info.TintAlpha;
    Mica->Saturation =
        (ULONG)(((Window->LayerFlags & DWM_WINDOW_DARK) ? LiberGlassDark : LiberGlassLight).Saturate * 256.0f + 0.5f);
    return TRUE;
}

ULONG
DwmLiberMicaPixel(
    _In_ ULONG Source,
    _In_ ULONG Key,
    _In_ ULONG Weight,
    _In_ ULONG Base,
    _In_ const DWM_LIBER_MICA *Mica)
{
    LONG Luma = (LONG)((((Base >> 16) & 0xFF) * 54u + ((Base >> 8) & 0xFF) * 183u + (Base & 0xFF) * 19u) >> 8);
    ULONG Result = 0, Shift;

    assert(Mica != NULL && Weight <= 255 && Mica->Alpha <= 255);
    for (Shift = 0; Shift <= 16; Shift += 8)
    {
        LONG Channel = (LONG)((Base >> Shift) & 0xFF);
        LONG Saturated = (LONG)DwmLiberClampChannel(Luma + (Channel - Luma) * (LONG)Mica->Saturation / 256);
        LONG Material = (LONG)((((Mica->Tint >> Shift) & 0xFF) * Mica->Alpha +
                                (ULONG)Saturated * (255u - Mica->Alpha) + 127u) / 255u);
        LONG Value = (LONG)((Source >> Shift) & 0xFF) +
                     (Material - (LONG)((Key >> Shift) & 0xFF)) * (LONG)Weight / 255;

        Result |= DwmLiberClampChannel(Value) << Shift;
    }
    return Result;
}
