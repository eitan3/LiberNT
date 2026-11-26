/*
 * PROJECT:     LiberNT Explorer - Unit-tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Pin the calendar event store formats shared by the calendar flyouts
 * COPYRIGHT:   Copyright 2026 Eitan Hadar <eitanhadar4@gmail.com>
 */

#include <apitest.h>

#include <windows.h>

#include "calstore.h"

static void TestNames()
{
    WCHAR name[16];

    TfyCalDayName(2026, 11, 26, name, _countof(name));
    ok(!wcscmp(name, L"20261126"), "day name %ls\n", name);
    ok(TfyCalNameToDay(L"20261126", 2026, 11) == 26, "name to day\n");
    ok(TfyCalNameToDay(L"20261126", 2026, 12) == 0, "other month\n");
    ok(TfyCalNameToDay(L"2026112", 2026, 11) == 0, "short name\n");
    ok(TfyCalNameToDay(L"202611261", 2026, 11) == 0, "long name\n");
    ok(TfyCalDigits(L"0912", 4) == 912 && TfyCalDigits(L"09a2", 4) == -1, "digits\n");
}

static void TestStored()
{
    TFYCALEVENT ev;
    WCHAR line[96];

    ok(TfyCalParseStored(L"0930 0045 Standup", &ev), "timed event\n");
    ok(ev.nMinutes == 9 * 60 + 30 && ev.nDuration == 45 && !wcscmp(ev.szTitle, L"Standup"), "timed fields\n");
    TfyCalFormatStored(&ev, line, _countof(line));
    ok(!wcscmp(line, L"0930 0045 Standup"), "round trip %ls\n", line);

    ok(TfyCalParseStored(L"---- ---- Holiday", &ev) && ev.nMinutes == TFY_CAL_ALLDAY && ev.nDuration == 0,
       "all-day event\n");
    TfyCalFormatStored(&ev, line, _countof(line));
    ok(!wcscmp(line, L"---- ---- Holiday"), "all-day round trip %ls\n", line);

    ok(TfyCalParseStored(L"2330 0000 Late", &ev) && ev.nDuration == 60, "zero duration defaults to 60\n");
    ok(TfyCalParseStored(L"2330 9999 Late", &ev) && ev.nDuration == 60, "huge duration defaults to 60\n");
    ok(!TfyCalParseStored(L"2460 0030 Bad", &ev), "minute 60 rejected\n");
    ok(!TfyCalParseStored(L"2500 0030 Bad", &ev), "hour 25 rejected\n");
    ok(!TfyCalParseStored(L"0930 0045 ", &ev), "empty title rejected\n");
    ok(!TfyCalParseStored(L"0930", &ev), "short line rejected\n");
}

static void TestInput()
{
    TFYCALEVENT ev;
    int minutes = -1;

    ok(TfyCalScanTime(L"9:05 rest", &minutes) != NULL && minutes == 9 * 60 + 5, "9:05\n");
    ok(TfyCalScanTime(L"12:00 am", &minutes) != NULL && minutes == 0, "12 am is midnight\n");
    ok(TfyCalScanTime(L"1:30pm", &minutes) != NULL && minutes == 13 * 60 + 30, "1:30pm\n");
    ok(TfyCalScanTime(L"24:00", &minutes) == NULL, "24:00 rejected\n");
    ok(TfyCalScanTime(L"x", &minutes) == NULL, "no time\n");

    ok(TfyCalParseInput(L"  10:00-11:30 Review  ", &ev), "range input\n");
    ok(ev.nMinutes == 600 && ev.nDuration == 90 && !wcscmp(ev.szTitle, L"Review"), "range fields %d %d '%ls'\n",
       ev.nMinutes, ev.nDuration, ev.szTitle);
    ok(TfyCalParseInput(L"23:30 Night", &ev) && ev.nDuration == 30, "duration clipped at midnight: %d\n",
       ev.nDuration);
    ok(TfyCalParseInput(L"Birthday", &ev) && ev.nMinutes == TFY_CAL_ALLDAY, "no time is all day\n");
    ok(!TfyCalParseInput(L"   ", &ev), "blank input rejected\n");
}

static void TestSort()
{
    int dates[] = { 20261201, 20261126, 20270101, 20261127 };

    TfyCalSortDates(dates, _countof(dates));
    ok(dates[0] == 20261126 && dates[1] == 20261127 && dates[2] == 20261201 && dates[3] == 20270101, "sorted\n");
}

START_TEST(Calendar)
{
    TestNames();
    TestStored();
    TestInput();
    TestSort();
}
