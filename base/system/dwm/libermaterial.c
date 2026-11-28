/*
 * PROJECT:     LiberNT Desktop Window Manager
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Decode the Liber material a window requests
 * COPYRIGHT:   Copyright 2026 Eitan Hadar <eitanhadar4@gmail.com>
 */

#include <windows.h>

#include "libermaterial.h"

#include <assert.h>

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
