/*
 * PROJECT:     LiberNT Explorer - Unit-tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test the Liber widget providers and placement
 * COPYRIGHT:   Copyright 2026 Eitan Hadar <eitanhadar4@gmail.com>
 */

#include <apitest.h>

#include <windows.h>

#include "widgetmodel.h"

static SYSTEMTIME Date(WORD year, WORD month, WORD day, WORD hour = 12)
{
    SYSTEMTIME st;
    ZeroMemory(&st, sizeof(st));
    st.wYear = year;
    st.wMonth = month;
    st.wDay = day;
    st.wHour = hour;
    return st;
}

static void TestIsoWeek()
{
    static const struct { WORD Y, M, D; int Week; } cases[] =
    {
        { 2026, 11, 26, 48 },
        { 2026, 1, 1, 1 },
        { 2025, 12, 29, 1 },
        { 2021, 1, 3, 53 },
        { 2021, 1, 4, 1 },
        { 2020, 12, 31, 53 },
        { 2024, 12, 30, 1 },
        { 2024, 2, 29, 9 },
        { 2023, 1, 1, 52 },
        { 2027, 1, 1, 53 },
    };

    for (UINT i = 0; i < _countof(cases); i++)
    {
        SYSTEMTIME st = Date(cases[i].Y, cases[i].M, cases[i].D);
        int week = LiberWidget_IsoWeek(&st);
        ok(week == cases[i].Week, "%04u-%02u-%02u is week %d, expected %d\n", cases[i].Y, cases[i].M, cases[i].D, week,
           cases[i].Week);
    }
    SYSTEMTIME bad = Date(2026, 13, 1);
    ok(LiberWidget_IsoWeek(&bad) == 0, "invalid month\n");
}

static void TestCpu()
{
    LIBER_CPU_SAMPLE a = { 1000, 2000, 1000 };
    LIBER_CPU_SAMPLE b = { 1600, 2800, 1200 };

    ok(LiberWidget_CpuPercent(&a, &b) == 40, "busy %d%%\n", LiberWidget_CpuPercent(&a, &b));
    ok(LiberWidget_CpuPercent(&a, &a) == 0, "no time passed\n");
    ok(LiberWidget_CpuPercent(&b, &a) == 0, "counters going back\n");
    LIBER_CPU_SAMPLE idle = { 1800, 2800, 1000 };
    ok(LiberWidget_CpuPercent(&a, &idle) == 0, "fully idle\n");
}

static void TestProviders()
{
    CDateProvider date;
    SYSTEMTIME st = Date(2026, 11, 26);

    date.SetDate(st);
    ok(date.Data().Day == 26 && date.Data().IsoWeek == 48 && date.Data().szDayName[0] && date.Data().szMonthYear[0],
       "date data %d %d '%ls' '%ls'\n", date.Data().Day, date.Data().IsoWeek, date.Data().szDayName,
       date.Data().szMonthYear);
    ok(date.Kind() == LIBER_WIDGET_DATE && date.RefreshMs() == 60000, "date provider\n");

    CMockWeatherProvider weather;
    ok(weather.Data().Sample && weather.Data().City && weather.Data().Condition, "mock weather is labeled as sample\n");
    weather.SetSlot(2);
    ok(!wcscmp(weather.Data().Condition, L"Light rain") && weather.Data().Low <= weather.Data().High, "slot 2\n");
    weather.SetSlot(6);
    ok(!wcscmp(weather.Data().Condition, L"Light rain"), "slots wrap\n");
    SYSTEMTIME morning = Date(2026, 11, 26, 7), night = Date(2026, 11, 26, 23);
    ok(LiberWidget_MockSlot(&morning) == 1 && LiberWidget_MockSlot(&night) == 3, "mock slot by hour\n");

    CSystemStatsProvider stats;
    stats.Refresh();
    Sleep(50);
    stats.Refresh();
    const LIBER_SYSTEM_DATA &data = stats.Data();
    ok(data.CpuPercent >= 0 && data.CpuPercent <= 100, "cpu %d\n", data.CpuPercent);
    ok(data.MemoryPercent > 0 && data.MemoryPercent <= 100, "memory %d\n", data.MemoryPercent);
    ok(data.StoragePercent > 0 && data.StoragePercent <= 100, "storage %d\n", data.StoragePercent);
}

static void TestLayout()
{
    RECT work = { 0, 0, 1600, 824 };
    RECT rc[LIBER_WIDGET_COUNT];

    LiberWidget_Layout(&work, 96, rc);
    ok(rc[LIBER_WIDGET_WEATHER].left == 1416 && rc[LIBER_WIDGET_WEATHER].top == 20 &&
       rc[LIBER_WIDGET_WEATHER].right == 1580 && rc[LIBER_WIDGET_WEATHER].bottom == 184,
       "weather matches the spec demo at (%ld,%ld,%ld,%ld)\n", rc[LIBER_WIDGET_WEATHER].left,
       rc[LIBER_WIDGET_WEATHER].top, rc[LIBER_WIDGET_WEATHER].right, rc[LIBER_WIDGET_WEATHER].bottom);
    ok(rc[LIBER_WIDGET_DATE].left == 1240 && rc[LIBER_WIDGET_DATE].right == rc[LIBER_WIDGET_WEATHER].left - 12,
       "date left of weather with the gap\n");
    ok(rc[LIBER_WIDGET_SYSTEM].left == 1240 && rc[LIBER_WIDGET_SYSTEM].top == 196 && rc[LIBER_WIDGET_SYSTEM].right == 1580 &&
       rc[LIBER_WIDGET_SYSTEM].bottom == 308, "system monitor is the 340 x 112 wide card below\n");

    LiberWidget_Layout(&work, 192, rc);
    ok(rc[LIBER_WIDGET_WEATHER].right == 1600 - 40 && rc[LIBER_WIDGET_WEATHER].bottom - rc[LIBER_WIDGET_WEATHER].top == 328 &&
       rc[LIBER_WIDGET_SYSTEM].bottom - rc[LIBER_WIDGET_SYSTEM].top == 224, "scaled at 192 DPI\n");
}

START_TEST(Widgets)
{
    TestIsoWeek();
    TestCpu();
    TestProviders();
    TestLayout();
}
