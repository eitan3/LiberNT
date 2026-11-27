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

ULONG
DwmLiberSolidColor(
    _In_ const DWM_LIBER_MATERIAL_INFO *Info)
{
    assert(Info != NULL);
    return 0xFF000000u | ((ULONG)GetRValue(Info->TintColor) << 16) | ((ULONG)GetGValue(Info->TintColor) << 8) |
           (ULONG)GetBValue(Info->TintColor);
}
