/*
 * PROJECT:     LiberNT Explorer
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Calendar event store shared by the calendar flyouts
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

#define TFY_CAL_KEY       L"Software\\ReactOS\\Explorer\\Calendar"
#define TFY_CAL_MAXEVENTS 64
#define TFY_CAL_DATA      6144
#define TFY_CAL_ALLDAY    (-1)
#define TFY_CAL_STOREPFX  10
#define TFY_CAL_MAXDATES  512

struct TFYCALEVENT
{
    int nMinutes;
    int nDuration;
    WCHAR szTitle[64];
};

struct TFYCALDAY
{
    TFYCALEVENT ev[TFY_CAL_MAXEVENTS];
    int count;
};

struct TFYCALROW
{
    int date;
    int bHeader;
    TFYCALEVENT ev;
};

VOID TfyCalDayName(int year, int month, int day, LPWSTR psz, size_t cch);
int TfyCalDigits(LPCWSTR psz, int n);
int TfyCalNameToDay(LPCWSTR psz, int year, int month);
BOOL TfyCalParseStored(LPCWSTR psz, TFYCALEVENT *pev);
VOID TfyCalFormatStored(const TFYCALEVENT *pev, LPWSTR psz, size_t cch);
LPCWSTR TfyCalScanTime(LPCWSTR p, int *pMinutes);
BOOL TfyCalParseInput(LPCWSTR psz, TFYCALEVENT *pev);
VOID TfyCalLoadDay(int year, int month, int day, TFYCALDAY *pday);
VOID TfyCalSaveDay(int year, int month, int day, const TFYCALDAY *pday);
VOID TfyCalLoadMonthCounts(int year, int month, int *pCounts);
VOID TfyCalSortDates(int *pDates, int count);
int TfyCalCollectDates(int fromDate, int *pDates, int maxDates);
int TfyCalLoadAgenda(int fromDate, TFYCALROW *pRows, int maxRows);
