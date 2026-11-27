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
        SIZE size = { min(available.cx, (LONG)(12 + lstrlenW(m_Text) * 6 + 4 + 12 + 10)), 24 };
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

private:
    typedef LiberUI::Thickness Thickness;
    LiberUI::Label *m_Title;
    CLiberPillButton *m_Action;
};

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
