/*
 * PROJECT:     LiberNT Explorer
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Data providers and placement of the Liber desktop widgets
 * COPYRIGHT:   Copyright 2026 Eitan Hadar <eitanhadar4@gmail.com>
 */

#pragma once

#define LIBER_WIDGETS_VALUE     L"Widgets"
#define LIBER_WIDGET_TILE       160
#define LIBER_WIDGET_GAP        12
#define LIBER_WIDGET_MARGIN     24
#define LIBER_WIDGET_COUNT      3

enum LIBER_WIDGET_KIND
{
    LIBER_WIDGET_DATE,
    LIBER_WIDGET_WEATHER,
    LIBER_WIDGET_SYSTEM
};

typedef struct _LIBER_DATE_DATA
{
    WCHAR szDayName[32];
    WCHAR szMonthYear[48];
    int Day;
    int IsoWeek;
} LIBER_DATE_DATA;

typedef struct _LIBER_WEATHER_DATA
{
    LPCWSTR City;
    LPCWSTR Condition;
    LPCWSTR Icon;
    int Temperature;
    int High;
    int Low;
    BOOL Sample;
} LIBER_WEATHER_DATA;

typedef struct _LIBER_SYSTEM_DATA
{
    int CpuPercent;
    int MemoryPercent;
    int StoragePercent;
} LIBER_SYSTEM_DATA;

typedef struct _LIBER_CPU_SAMPLE
{
    ULONGLONG Idle;
    ULONGLONG Kernel;
    ULONGLONG User;
} LIBER_CPU_SAMPLE;

class IWidgetProvider
{
public:
    virtual LIBER_WIDGET_KIND Kind() const = 0;
    virtual BOOL Refresh() = 0;
    virtual UINT RefreshMs() const = 0;

protected:
    ~IWidgetProvider() {}
};

class CDateProvider final : public IWidgetProvider
{
public:
    CDateProvider();
    LIBER_WIDGET_KIND Kind() const { return LIBER_WIDGET_DATE; }
    BOOL Refresh();
    UINT RefreshMs() const { return 60000; }
    const LIBER_DATE_DATA &Data() const { return m_Data; }
    void SetDate(const SYSTEMTIME &st);

private:
    LIBER_DATE_DATA m_Data;
};

class CMockWeatherProvider final : public IWidgetProvider
{
public:
    CMockWeatherProvider();
    LIBER_WIDGET_KIND Kind() const { return LIBER_WIDGET_WEATHER; }
    BOOL Refresh();
    UINT RefreshMs() const { return 600000; }
    const LIBER_WEATHER_DATA &Data() const { return m_Data; }
    void SetSlot(UINT slot);

private:
    LIBER_WEATHER_DATA m_Data;
    UINT m_Slot;
};

class CSystemStatsProvider final : public IWidgetProvider
{
public:
    CSystemStatsProvider();
    LIBER_WIDGET_KIND Kind() const { return LIBER_WIDGET_SYSTEM; }
    BOOL Refresh();
    UINT RefreshMs() const { return 2000; }
    const LIBER_SYSTEM_DATA &Data() const { return m_Data; }

private:
    LIBER_SYSTEM_DATA m_Data;
    LIBER_CPU_SAMPLE m_Last;
    BOOL m_HaveLast;
};

int LiberWidget_IsoWeek(const SYSTEMTIME *pst);
int LiberWidget_CpuPercent(const LIBER_CPU_SAMPLE *pBefore, const LIBER_CPU_SAMPLE *pAfter);
UINT LiberWidget_MockSlot(const SYSTEMTIME *pst);
VOID LiberWidget_Layout(const RECT *prcWork, UINT dpi, RECT prcWidgets[LIBER_WIDGET_COUNT]);
