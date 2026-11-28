/*
 * PROJECT:     LiberNT Explorer
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Pieces shared by the Liber shell surfaces
 * COPYRIGHT:   Copyright 2026 Eitan Hadar <eitanhadar4@gmail.com>
 */

#pragma once

#include <liberui.h>
#include <strsafe.h>

#define LIBER_FLYOUT_GAP 4
#define LIBER_FLYOUT_INSET 12

class CLiberPillButton : public LiberUI::Control
{
public:
    CLiberPillButton(LPCWSTR text)
    {
        SetText(text);
        SetSize(-1, 24);
    }

    void SetText(LPCWSTR text)
    {
        StringCchCopyW(m_Text, _countof(m_Text), text ? text : L"");
        InvalidateLayout();
        Invalidate();
    }

    void OnClick() { Raise(LiberUI::ControlClicked); }

protected:
    SIZE MeasureOverride(SIZE available)
    {
        LONG text = LiberUI::MeasureText(m_Text, LIBER_TYPE_CAPTION, 0.0f).cx;
        if (text <= 0)
            text = (LONG)lstrlenW(m_Text) * LiberTypeRamp[LIBER_TYPE_CAPTION].Size * 55 / 100;
        SIZE size = { min(available.cx, (LONG)(12 + text + 1 + 4 + 12 + 10)), 24 };
        return size;
    }

    void PaintOverride(LiberUI::PaintContext &context)
    {
        LiberUI::RectF rc = LiberUI::ToRectF(Bounds());
        const LiberUI::ThemeTokens &tokens = context.Tokens;

        context.Painter.FillRoundRect(rc, rc.Height / 2,
                                      tokens.Color(HasState(LiberUI::StatePressed) ? LIBER_COLOR_FILL_CONTROL_PRESSED
                                                   : HasState(LiberUI::StateHover) ? LIBER_COLOR_FILL_CONTROL_HOVER
                                                                                   : LIBER_COLOR_FILL_CONTROL));
        context.Painter.StrokeRoundRect(rc, rc.Height / 2, tokens.Color(LIBER_COLOR_STROKE_CONTROL), 1.0f);
        LiberUI::RectF text = { rc.X + 12.0f, rc.Y, rc.Width - 38.0f, rc.Height };
        context.Painter.Text(m_Text, text, LIBER_TYPE_CAPTION, tokens.Color(LIBER_COLOR_TEXT1),
                             LiberUI::TextMiddle | LiberUI::TextEllipsis);
        LiberUI::RectF chevron = { rc.X + rc.Width - 24.0f, rc.Y + 6.0f, 12.0f, 12.0f };
        context.Painter.Icon(L"chevron_right_12_regular", chevron, tokens.Color(LIBER_COLOR_TEXT1));
    }

private:
    WCHAR m_Text[32];
};

class CLiberSectionHead : public LiberUI::Panel
{
public:
    CLiberSectionHead(LPCWSTR title, LPCWSTR action, UINT actionId, LiberUI::IControlListener *listener)
    {
        m_Title = new LiberUI::Label(title, LIBER_TYPE_BODY_STRONG, LIBER_COLOR_TEXT1,
                                     LiberUI::TextLeft | LiberUI::TextMiddle);
        m_Title->SetAlign(LiberUI::Align::Stretch, LiberUI::Align::Center);
        m_Action = new CLiberPillButton(action);
        m_Action->SetId(actionId);
        m_Action->SetListener(listener);
        m_Action->SetAlign(LiberUI::Align::End, LiberUI::Align::Center);
        Thickness pad = { 12, 0, 0, 0 };
        SetPadding(pad);
        SetSize(-1, 24);
        AddChild(m_Title);
        AddChild(m_Action);
        SetAction(action);
    }

    void SetTitle(LPCWSTR title) { m_Title->SetText(title); }
    void SetAction(LPCWSTR action)
    {
        if (action)
            m_Action->SetText(action);
        m_Action->SetVisible(action != NULL);
    }

    CLiberSectionHead(const CLiberSectionHead &) = delete;
    CLiberSectionHead &operator=(const CLiberSectionHead &) = delete;

private:
    typedef LiberUI::Thickness Thickness;
    LiberUI::Label *m_Title;
    CLiberPillButton *m_Action;
};

BOOL LiberShell_GlassEnabled(VOID);
BOOL LiberShell_ReadGlassEnabled(VOID);

static inline void LiberShell_PaintGlass(LiberUI::PaintContext &context, const LiberUI::RectF &rc, float radius,
                                         LIBER_MATERIAL_KIND material)
{
    LiberUI::GlassTint tint = context.Tokens.Material(material, 0.5);
    LiberUI::RectF edge = { rc.X + 0.5f, rc.Y + 0.5f, rc.Width - 1.0f, rc.Height - 1.0f };
    LiberUI::RectF top = { rc.X, rc.Y, rc.Width, radius };
    LiberUI::RectF highlight = { rc.X + 0.5f, rc.Y + 1.5f, rc.Width - 1.0f, rc.Height - 2.0f };

    context.Painter.FillRoundRect(rc, radius,
                                  LiberUI::OklchToArgb(tint.Color, LiberShell_GlassEnabled() ? tint.Alpha : 1.0));
    context.Painter.StrokeRoundRect(edge, radius - 0.5f, context.Tokens.Color(LIBER_COLOR_GLASS_EDGE), 1.0f);
    context.Painter.PushClip(top, 0.0f);
    context.Painter.StrokeRoundRect(highlight, radius - 1.5f, context.Tokens.Color(LIBER_COLOR_GLASS_EDGE_TOP), 1.0f);
    context.Painter.PopClip();
}

VOID LiberShell_ApplyMaterial(LiberUI::Surface *pSurface, const LiberUI::ThemeTokens *pTokens,
                              LIBER_MATERIAL_KIND material, float radiusDip);

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
        LiberShell_PaintGlass(context, rc, min(m_Radius, rc.Height / 2), m_Material);
    }

private:
    float m_Radius;
    LIBER_MATERIAL_KIND m_Material;
};

VOID LiberShell_BuildTokens(LiberUI::ThemeTokens *pTokens);
#define LIBER_WALLPAPER_CENTER  0
#define LIBER_WALLPAPER_STRETCH 2
#define LIBER_WALLPAPER_FIT     6
#define LIBER_WALLPAPER_FILL    10
#define LIBER_WALLPAPER_INK_LUMA 0.4

VOID LiberShell_MapToWallpaper(const RECT *prcScreen, SIZE screen, SIZE image, UINT style, RECT *prcImage);
double LiberShell_Luminance(DWORD argb);
BOOL LiberShell_WallpaperLuma(const RECT *prcScreen, double *pLuma);
#define LIBER_LUMA_GRID_W 64
#define LIBER_LUMA_GRID_H 36
VOID LiberShell_BuildLumaGrid(const DWORD *pPixels, UINT width, UINT height, float *pGrid);
BOOL LiberShell_GridLuma(const float *pGrid, SIZE image, const RECT *prcImage, double *pLuma);
BOOL LiberShell_WallpaperPalette(LiberUI::PaletteResult *pPalette);
LiberUI::Accent LiberShell_ResolveAccent(DWORD dwChoice, const LiberUI::PaletteResult *pPalette);
VOID LiberShell_BuildWallpaperInkTokens(LiberUI::ThemeTokens *pTokens, const RECT *prcScreen);
VOID LiberShell_ApplyFont(LiberUI::Surface *pSurface);
BOOL LiberShell_SetDarkMode(BOOL bDark);
VOID LiberShell_FlyoutOrigin(const RECT *prcMonitor, UINT dpi, const RECT *prcAnchor, int widthPx, int heightPx,
                             BOOL bCenter, POINT *pptTopLeft);
VOID LiberShell_PlaceFlyout(const RECT *prcAnchor, int widthPx, int heightPx, BOOL bCenter, POINT *pptTopLeft);

typedef struct _LIBER_RECENT_FILE
{
    WCHAR szLink[MAX_PATH];
    WCHAR szName[MAX_PATH];
    SYSTEMTIME stWritten;
} LIBER_RECENT_FILE;

#define LIBER_CALENDAR_CELLS 42

typedef struct _LIBER_CALENDAR_DAY
{
    WORD wYear;
    WORD wMonth;
    WORD wDay;
    BOOL bInMonth;
} LIBER_CALENDAR_DAY;

UINT LiberCalendar_DaysInMonth(UINT year, UINT month);
UINT LiberCalendar_DayOfWeek(UINT year, UINT month, UINT day);
VOID LiberCalendar_MonthGrid(UINT year, UINT month, UINT firstDayOfWeek, LIBER_CALENDAR_DAY *pDays);
VOID LiberCalendar_Toggle(IN const RECT *prcAnchor);
BOOL LiberCalendar_IsOpen(VOID);
VOID LiberCalendar_Destroy(VOID);
VOID LiberOverflow_Toggle(IN const RECT *prcAnchor);
VOID LiberOverflow_Destroy(VOID);

typedef struct _LIBER_MENU_ITEM
{
    UINT Id;
    LPCWSTR Text;
    LPCWSTR Icon;
    LPCWSTR Hint;
    BOOL Separator;
    BOOL Disabled;
} LIBER_MENU_ITEM;

UINT LiberMenu_Track(const LIBER_MENU_ITEM *pItems, UINT cItems, POINT anchor);
VOID LiberMenu_Origin(const RECT *prcWork, POINT anchor, int widthPx, int heightPx, POINT *pptTopLeft);
BOOL LiberMenu_DismissesOn(UINT msg, POINT pt, const RECT *prcMenu);
BOOL LiberMenu_DismissedOutside(POINT *ppt);

BOOL LiberStart_LoadPins(LPWSTR pszPins, DWORD cchPins, UINT *pcPins);
BOOL LiberStart_IsPinned(LPCWSTR pszPins, LPCWSTR pszPath);
BOOL LiberStart_SetPinned(LPCWSTR pszPins, LPCWSTR pszPath, BOOL bPinned);
UINT LiberStart_DefaultPinRank(LPCWSTR pszTarget);
BOOL LiberStart_SystemApp(UINT index, LPWSTR pszPath, UINT cchPath, LPCWSTR *ppszName);

UINT LiberStart_LoadRecentFiles(LIBER_RECENT_FILE *pFiles, UINT cMax);
VOID LiberStart_FormatWhen(const SYSTEMTIME *pNow, const SYSTEMTIME *pThen, LPWSTR pszText, UINT cchText);
UINT LiberStart_ScrollOffset(UINT offset, UINT count, UINT columns, UINT visible, int rows);

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
