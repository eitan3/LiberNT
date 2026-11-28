/*
 * PROJECT:     LiberNT Desktop Window Manager
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Decode the Liber material a window requests
 * COPYRIGHT:   Copyright 2026 Eitan Hadar <eitanhadar4@gmail.com>
 */

#pragma once

#include <libernt/dwmframe.h>
#include <liberdesign/liber_tokens.h>

typedef struct _DWM_LIBER_MATERIAL_INFO
{
    LIBER_MATERIAL_KIND Kind;
    ULONG BlurRadius;
    ULONG Layer;
    BOOLEAN Adaptive;
    BOOLEAN HasElevation;
    LIBER_ELEVATION Elevation;
    ULONG Edge;
    BYTE TintAlpha;
    COLORREF TintColor;
} DWM_LIBER_MATERIAL_INFO, *PDWM_LIBER_MATERIAL_INFO;

BOOLEAN
DwmLiberResolveMaterial(
    _In_ const DWM_WIN *Window,
    _In_ ULONG Dpi,
    _Out_ PDWM_LIBER_MATERIAL_INFO Info);

ULONG
DwmLiberSolidColor(
    _In_ const DWM_LIBER_MATERIAL_INFO *Info);

typedef struct _DWM_LIBER_BLUR
{
    ULONG Radius;
    FLOAT Saturation;
} DWM_LIBER_BLUR, *PDWM_LIBER_BLUR;

BOOLEAN
DwmLiberBlur(
    _In_ const DWM_WIN *Window,
    _In_ ULONG Dpi,
    _In_ ULONG MaxRadius,
    _Out_ PDWM_LIBER_BLUR Blur);

VOID
DwmLiberApplyMaterial(
    _Inout_ DWM_WIN *Window);

ULONG
DwmLiberMaxBlurRadius(
    _In_ ULONG Dpi);

typedef struct _DWM_LIBER_DIM
{
    BOOLEAN Enabled;
    COLORREF Shade;
} DWM_LIBER_DIM, *PDWM_LIBER_DIM;

double
DwmLiberDimAmount(
    _In_ double AverageLuma);

double
DwmLiberDimBackdrop(
    _Inout_ ULONG *Bits,
    _In_ ULONG Width,
    _In_ ULONG Height,
    _In_ ULONG Stride,
    _In_ COLORREF Shade);

VOID
DwmLiberReadDim(
    _Out_ PDWM_LIBER_DIM Dim);
