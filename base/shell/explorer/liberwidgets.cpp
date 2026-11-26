/*
 * PROJECT:     LiberNT Explorer
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Liber desktop widgets
 * COPYRIGHT:   Copyright 2026 Eitan Hadar <eitanhadar4@gmail.com>
 */

#include "precomp.h"

#include "experience.h"
#include "libershell.h"
#include "widgetmodel.h"

using namespace LiberUI;

#define LIBER_WIDGET_TIMER 1

class CWidgetCard : public Widget
{
public:
    CWidgetCard(IWidgetProvider *provider) : m_Provider(provider) {}

protected:
    void PaintCard(PaintContext &context)
    {
        RectF rc = ToRectF(Bounds());
        GlassTint tint = context.Tokens.Material(LIBER_MATERIAL_THIN, 0.5);

        context.Painter.FillRoundRect(rc, (float)LIBER_RADIUS_WIDGET, OklchToArgb(tint.Color, 1.0));
        context.Painter.StrokeRoundRect(rc, (float)LIBER_RADIUS_WIDGET, context.Tokens.Color(LIBER_COLOR_STROKE_CARD),
                                        1.0f);
    }

    static RectF Inset(const RectF &rc, float x, float y, float width, float height)
    {
        RectF r = { rc.X + x, rc.Y + y, width, height };
        return r;
    }

    IWidgetProvider *m_Provider;
};

class CDateCard : public CWidgetCard
{
public:
    CDateCard(CDateProvider *provider) : CWidgetCard(provider), m_Date(provider) {}

protected:
    void PaintOverride(PaintContext &context)
    {
        const LIBER_DATE_DATA &data = m_Date->Data();
        const ThemeTokens &tokens = context.Tokens;
        RectF rc = ToRectF(Bounds());
        WCHAR szDay[8], szWeek[48];

        PaintCard(context);
        StringCchPrintfW(szDay, _countof(szDay), L"%d", data.Day);
        StringCchPrintfW(szWeek, _countof(szWeek), L"Week %d \x00B7 No events", data.IsoWeek);
        context.Painter.Text(data.szDayName, Inset(rc, 16, 14, rc.Width - 32, 20), LIBER_TYPE_BODY_STRONG,
                             tokens.Color(LIBER_COLOR_ACCENT_TEXT), TextLeft | TextEllipsis);
        context.Painter.Text(szDay, Inset(rc, 16, 34, rc.Width - 32, 52), LIBER_TYPE_TITLE_LARGE,
                             tokens.Color(LIBER_COLOR_TEXT1), TextLeft);
        context.Painter.Text(data.szMonthYear, Inset(rc, 16, 90, rc.Width - 32, 20), LIBER_TYPE_BODY,
                             tokens.Color(LIBER_COLOR_TEXT1), TextLeft | TextEllipsis);
        context.Painter.Text(szWeek, Inset(rc, 16, 120, rc.Width - 32, 16), LIBER_TYPE_CAPTION,
                             tokens.Color(LIBER_COLOR_TEXT2), TextLeft | TextEllipsis);
    }

private:
    CDateProvider *m_Date;
};

class CWeatherCard : public CWidgetCard
{
public:
    CWeatherCard(CMockWeatherProvider *provider) : CWidgetCard(provider), m_Weather(provider) {}

protected:
    void PaintOverride(PaintContext &context)
    {
        const LIBER_WEATHER_DATA &data = m_Weather->Data();
        const ThemeTokens &tokens = context.Tokens;
        RectF rc = ToRectF(Bounds());
        WCHAR szTemp[16], szRange[48];

        PaintCard(context);
        StringCchPrintfW(szTemp, _countof(szTemp), L"%d\x00B0", data.Temperature);
        StringCchPrintfW(szRange, _countof(szRange), L"H %d\x00B0  L %d\x00B0", data.High, data.Low);
        context.Painter.Text(data.City, Inset(rc, 16, 14, rc.Width - 32, 20), LIBER_TYPE_BODY_STRONG,
                             tokens.Color(LIBER_COLOR_TEXT1), TextLeft | TextEllipsis);
        context.Painter.Icon(data.Icon, Inset(rc, rc.Width - 44, 14, 28, 28), tokens.Color(LIBER_COLOR_ACCENT));
        context.Painter.Text(szTemp, Inset(rc, 16, 38, rc.Width - 32, 52), LIBER_TYPE_TITLE_LARGE,
                             tokens.Color(LIBER_COLOR_TEXT1), TextLeft);
        context.Painter.Text(data.Condition, Inset(rc, 16, 94, rc.Width - 32, 20), LIBER_TYPE_BODY,
                             tokens.Color(LIBER_COLOR_TEXT1), TextLeft | TextEllipsis);
        context.Painter.Text(data.Sample ? L"Sample data" : szRange, Inset(rc, 16, 120, rc.Width - 32, 16),
                             LIBER_TYPE_CAPTION, tokens.Color(LIBER_COLOR_TEXT2), TextLeft | TextEllipsis);
    }

private:
    CMockWeatherProvider *m_Weather;
};

class CSystemCard : public CWidgetCard
{
public:
    CSystemCard(CSystemStatsProvider *provider) : CWidgetCard(provider), m_Stats(provider) {}

protected:
    void Ring(PaintContext &context, const RectF &rc, int percent, LPCWSTR label)
    {
        const ThemeTokens &tokens = context.Tokens;
        float size = 72.0f;
        RectF ring = { rc.X + (rc.Width - size) / 2, rc.Y, size, size };
        WCHAR szValue[8];

        context.Painter.StrokeArc(ring, 0.0f, 360.0f, tokens.Color(LIBER_COLOR_FILL_TRACK), 8.0f);
        if (percent > 0)
            context.Painter.StrokeArc(ring, -90.0f, 3.6f * (float)(percent > 100 ? 100 : percent),
                                      tokens.Color(LIBER_COLOR_ACCENT), 8.0f);
        StringCchPrintfW(szValue, _countof(szValue), L"%d%%", percent);
        context.Painter.Text(szValue, ring, LIBER_TYPE_BODY_STRONG, tokens.Color(LIBER_COLOR_TEXT1),
                             TextCenter | TextMiddle);
        RectF text = { rc.X, rc.Y + size + 10.0f, rc.Width, 16.0f };
        context.Painter.Text(label, text, LIBER_TYPE_CAPTION, tokens.Color(LIBER_COLOR_TEXT2), TextCenter | TextMiddle);
    }

    void PaintOverride(PaintContext &context)
    {
        const LIBER_SYSTEM_DATA &data = m_Stats->Data();
        RectF rc = ToRectF(Bounds());
        float column = (rc.Width - 32.0f) / 3.0f;

        PaintCard(context);
        Ring(context, Inset(rc, 16, 28, column, 110), data.CpuPercent, L"CPU");
        Ring(context, Inset(rc, 16 + column, 28, column, 110), data.MemoryPercent, L"Memory");
        Ring(context, Inset(rc, 16 + 2 * column, 28, column, 110), data.StoragePercent, L"Storage");
    }

private:
    CSystemStatsProvider *m_Stats;
};

class CLiberWidgets
{
public:
    CLiberWidgets() : m_hwndSink(NULL)
    {
        ZeroMemory(m_Cards, sizeof(m_Cards));
        ZeroMemory(m_Due, sizeof(m_Due));
    }
    ~CLiberWidgets() { Stop(); }

    HRESULT Start(HWND hwndOwner);
    void Stop();

private:
    static LRESULT CALLBACK SinkProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    void Place();
    void Tick(BOOL force);
    IWidgetProvider *Provider(UINT i);

    HWND m_hwndSink;
    ThemeTokens m_Tokens;
    Surface m_Surfaces[LIBER_WIDGET_COUNT];
    CWidgetCard *m_Cards[LIBER_WIDGET_COUNT];
    ULONGLONG m_Due[LIBER_WIDGET_COUNT];
    CDateProvider m_Date;
    CMockWeatherProvider m_Weather;
    CSystemStatsProvider m_Stats;
};

static CLiberWidgets *g_pLiberWidgets;

IWidgetProvider *CLiberWidgets::Provider(UINT i)
{
    switch (i)
    {
        case LIBER_WIDGET_DATE:
            return &m_Date;
        case LIBER_WIDGET_WEATHER:
            return &m_Weather;
        default:
            return &m_Stats;
    }
}

void CLiberWidgets::Place()
{
    RECT work, rcs[LIBER_WIDGET_COUNT];

    if (!SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0))
        SetRect(&work, 0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN));
    LiberWidget_Layout(&work, m_Surfaces[0].Dpi(), rcs);
    for (UINT i = 0; i < LIBER_WIDGET_COUNT; i++)
    {
        UINT dpi = m_Surfaces[i].Dpi();
        m_Surfaces[i].SetPosition(rcs[i].left, rcs[i].top, MulDiv(rcs[i].right - rcs[i].left, 96, (int)dpi),
                                  MulDiv(rcs[i].bottom - rcs[i].top, 96, (int)dpi));
    }
}

void CLiberWidgets::Tick(BOOL force)
{
    ULONGLONG now = GetTickCount64();

    for (UINT i = 0; i < LIBER_WIDGET_COUNT; i++)
    {
        IWidgetProvider *provider = Provider(i);
        if (!force && now < m_Due[i])
            continue;
        m_Due[i] = now + provider->RefreshMs();
        if ((provider->Refresh() || force) && m_Cards[i])
        {
            m_Cards[i]->Invalidate();
            m_Surfaces[i].Render(AnimationClock::Now());
        }
    }
}

LRESULT CALLBACK CLiberWidgets::SinkProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    CLiberWidgets *self = g_pLiberWidgets;

    if (self && msg == WM_TIMER && wParam == LIBER_WIDGET_TIMER)
    {
        self->Tick(FALSE);
        return 0;
    }
    if (self && (msg == WM_SETTINGCHANGE || msg == WM_DISPLAYCHANGE))
    {
        LiberShell_BuildTokens(&self->m_Tokens);
        for (UINT i = 0; i < LIBER_WIDGET_COUNT; i++)
        {
            LiberShell_ApplyFont(&self->m_Surfaces[i]);
            self->m_Surfaces[i].SetTokens(&self->m_Tokens);
        }
        self->Place();
        self->Tick(TRUE);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

HRESULT CLiberWidgets::Start(HWND hwndOwner)
{
    WNDCLASSEXW wc = { sizeof(wc) };

    if (!LiberUI::Startup())
        return E_FAIL;

    wc.lpfnWndProc = SinkProc;
    wc.hInstance = hExplorerInstance;
    wc.lpszClassName = L"LiberWidgetsSink";
    RegisterClassExW(&wc);
    m_hwndSink = CreateWindowExW(WS_EX_TOOLWINDOW, wc.lpszClassName, NULL, WS_POPUP, 0, 0, 0, 0, NULL, NULL,
                                 hExplorerInstance, NULL);
    if (!m_hwndSink)
    {
        LiberUI::Shutdown();
        return HRESULT_FROM_WIN32(GetLastError());
    }

    LiberShell_BuildTokens(&m_Tokens);
    m_Cards[LIBER_WIDGET_DATE] = new CDateCard(&m_Date);
    m_Cards[LIBER_WIDGET_WEATHER] = new CWeatherCard(&m_Weather);
    m_Cards[LIBER_WIDGET_SYSTEM] = new CSystemCard(&m_Stats);
    for (UINT i = 0; i < LIBER_WIDGET_COUNT; i++)
    {
        if (!m_Surfaces[i].Create(hwndOwner, L"Liber Widget", WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE))
            return E_FAIL;
        LiberShell_ApplyFont(&m_Surfaces[i]);
        m_Surfaces[i].SetTokens(&m_Tokens);
        m_Surfaces[i].SetRoot(m_Cards[i]);
    }
    g_pLiberWidgets = this;
    Place();
    Tick(TRUE);
    for (UINT i = 0; i < LIBER_WIDGET_COUNT; i++)
    {
        SetWindowPos(m_Surfaces[i].Window(), HWND_BOTTOM, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
    }
    SetTimer(m_hwndSink, LIBER_WIDGET_TIMER, 1000, NULL);
    return S_OK;
}

void CLiberWidgets::Stop()
{
    if (g_pLiberWidgets == this)
        g_pLiberWidgets = NULL;
    if (m_hwndSink)
    {
        KillTimer(m_hwndSink, LIBER_WIDGET_TIMER);
        DestroyWindow(m_hwndSink);
        m_hwndSink = NULL;
        LiberUI::Shutdown();
    }
    for (UINT i = 0; i < LIBER_WIDGET_COUNT; i++)
    {
        m_Surfaces[i].SetRoot(NULL);
        m_Surfaces[i].Destroy();
        delete m_Cards[i];
        m_Cards[i] = NULL;
    }
}

HRESULT LiberWidgets_Start(HWND hwndOwner)
{
    if (g_pLiberWidgets || !LiberExperience_WidgetsEnabled())
        return S_FALSE;

    CLiberWidgets *widgets = new CLiberWidgets();
    if (!widgets)
        return E_OUTOFMEMORY;
    HRESULT hr = widgets->Start(hwndOwner);
    if (FAILED(hr))
        delete widgets;
    return hr;
}

VOID LiberWidgets_Stop(VOID)
{
    delete g_pLiberWidgets;
}
