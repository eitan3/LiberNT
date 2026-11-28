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

ULONG
DwmLiberFadeAlpha(
    _In_ ULONG Alpha,
    _In_ LONG Row,
    _In_ LONG Height);

typedef struct _DWM_LIBER_SHADOW_LAYER
{
    LONG Offset;
    double Sigma;
    LONG Support;
    ULONG Opacity;
} DWM_LIBER_SHADOW_LAYER, *PDWM_LIBER_SHADOW_LAYER;

typedef struct _DWM_LIBER_SHADOW
{
    DWM_LIBER_SHADOW_LAYER Ambient;
    DWM_LIBER_SHADOW_LAYER Contact;
} DWM_LIBER_SHADOW, *PDWM_LIBER_SHADOW;

VOID
DwmLiberShadowLayer(
    _In_ const LIBER_SHADOW_LAYER *Token,
    _In_ ULONG Dpi,
    _Out_ PDWM_LIBER_SHADOW_LAYER Layer);

VOID
DwmLiberWindowShadow(
    _In_ BOOLEAN Dark,
    _In_ BOOLEAN Active,
    _In_ ULONG Dpi,
    _Out_ PDWM_LIBER_SHADOW Shadow);

VOID
DwmLiberShadowMargins(
    _In_ ULONG Dpi,
    _Out_ RECT *Margins);

typedef struct _DWM_LIBER_EDGE_COLORS
{
    ULONG Outline;
    ULONG Bevel;
} DWM_LIBER_EDGE_COLORS, *PDWM_LIBER_EDGE_COLORS;

VOID
DwmLiberFrameEdgeColors(
    _In_ BOOLEAN Dark,
    _In_ ULONG Edge,
    _Out_ PDWM_LIBER_EDGE_COLORS Colors);

VOID
DwmLiberFrameEdgeCoverage(
    _In_ LONG X,
    _In_ LONG Y,
    _In_ LONG Width,
    _In_ LONG Height,
    _In_ ULONG Radius,
    _Out_ ULONG *Outline,
    _Out_ ULONG *Bevel);
