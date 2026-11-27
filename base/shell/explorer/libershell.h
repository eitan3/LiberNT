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

        float radius = min(m_Radius, rc.Height / 2);
        LiberUI::RectF edge = { rc.X + 0.5f, rc.Y + 0.5f, rc.Width - 1.0f, rc.Height - 1.0f };
        LiberUI::RectF top = { rc.X, rc.Y, rc.Width, radius };

        context.Painter.FillRoundRect(rc, radius, LiberUI::OklchToArgb(tint.Color, 1.0));
        context.Painter.StrokeRoundRect(edge, radius - 0.5f, context.Tokens.Color(LIBER_COLOR_GLASS_EDGE), 1.0f);
        context.Painter.PushClip(top, 0.0f);
        LiberUI::RectF highlight = { rc.X + 0.5f, rc.Y + 1.5f, rc.Width - 1.0f, rc.Height - 2.0f };
        context.Painter.StrokeRoundRect(highlight, radius - 1.5f, context.Tokens.Color(LIBER_COLOR_GLASS_EDGE_TOP), 1.0f);
        context.Painter.PopClip();
    }

private:
    float m_Radius;
    LIBER_MATERIAL_KIND m_Material;
};

VOID LiberShell_BuildTokens(LiberUI::ThemeTokens *pTokens);
VOID LiberShell_BuildWallpaperInkTokens(LiberUI::ThemeTokens *pTokens);
VOID LiberShell_ApplyFont(LiberUI::Surface *pSurface);
BOOL LiberShell_SetDarkMode(BOOL bDark);
VOID LiberShell_PlaceAbove(const RECT *prcAnchor, int widthPx, int heightPx, POINT *pptTopLeft);

VOID LiberControlCenter_Toggle(IN const RECT *prcAnchor);
BOOL LiberControlCenter_IsOpen(VOID);
VOID LiberControlCenter_Close(VOID);

BOOL LiberStart_Matches(LPCWSTR pszName, LPCWSTR pszQuery);
VOID LiberStart_Toggle(IN const RECT *prcAnchor);
BOOL LiberStart_IsOpen(VOID);
VOID LiberStart_Destroy(VOID);

HRESULT LiberWidgets_Start(HWND hwndOwner);
VOID LiberWidgets_Stop(VOID);
VOID LiberWidgets_Sync(HWND hwndOwner);
