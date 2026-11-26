/*
 * PROJECT:     LiberNT Explorer
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Pieces shared by the Liber shell surfaces
 * COPYRIGHT:   Copyright 2026 Eitan Hadar <eitanhadar4@gmail.com>
 */

#pragma once

#include <liberui.h>

#define LIBER_FLYOUT_GAP 4

class CLiberPanel : public LiberUI::StackPanel
{
public:
    CLiberPanel(LiberUI::Orientation orientation, int gap, float radius, LIBER_MATERIAL_KIND material)
        : LiberUI::StackPanel(orientation, gap), m_Radius(radius), m_Material(material)
    {
    }

protected:
    void PaintOverride(LiberUI::PaintContext &context)
    {
        LiberUI::RectF rc = LiberUI::ToRectF(Bounds());
        LiberUI::GlassTint tint = context.Tokens.Material(m_Material, 0.5);

        context.Painter.FillRoundRect(rc, m_Radius, LiberUI::OklchToArgb(tint.Color, 1.0));
        context.Painter.StrokeRoundRect(rc, m_Radius, context.Tokens.Color(LIBER_COLOR_STROKE_CARD), 1.0f);
    }

private:
    float m_Radius;
    LIBER_MATERIAL_KIND m_Material;
};

VOID LiberShell_BuildTokens(LiberUI::ThemeTokens *pTokens);
VOID LiberShell_ApplyFont(LiberUI::Surface *pSurface);
VOID LiberShell_SetDarkMode(BOOL bDark);
VOID LiberShell_PlaceAbove(const RECT *prcAnchor, int widthPx, int heightPx, POINT *pptTopLeft);

VOID LiberControlCenter_Toggle(IN const RECT *prcAnchor);
BOOL LiberControlCenter_IsOpen(VOID);
VOID LiberControlCenter_Close(VOID);

BOOL LiberStart_Matches(LPCWSTR pszName, LPCWSTR pszQuery);
VOID LiberStart_Toggle(IN const RECT *prcAnchor);
BOOL LiberStart_IsOpen(VOID);
VOID LiberStart_Destroy(VOID);
