/*
 * PROJECT:     LiberNT Explorer
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Calendar event store shared by the calendar flyouts
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <windows.h>
#include <strsafe.h>

#include "calstore.h"

VOID TfyCalDayName(int year, int month, int day, LPWSTR psz, size_t cch)
{
    StringCchPrintfW(psz, cch, L"%04d%02d%02d", year, month, day);
}

int TfyCalDigits(LPCWSTR psz, int n)
{
    int v = 0, i;

    for (i = 0; i < n; i++)
    {
        if (psz[i] < L'0' || psz[i] > L'9')
            return -1;
        v = v * 10 + (psz[i] - L'0');
    }
    return v;
}

int TfyCalNameToDay(LPCWSTR psz, int year, int month)
{
    int v = TfyCalDigits(psz, 8);

    if (v < 0 || psz[8] != 0)
        return 0;
    if (v / 10000 != year || (v / 100) % 100 != month)
        return 0;
    return v % 100;
}

BOOL TfyCalParseStored(LPCWSTR psz, TFYCALEVENT *pev)
{
    if (lstrlenW(psz) <= TFY_CAL_STOREPFX)
        return FALSE;
    if (psz[0] == L'-')
    {
        pev->nMinutes = TFY_CAL_ALLDAY;
        pev->nDuration = 0;
    }
    else
    {
        int hm = TfyCalDigits(psz, 4);
        int dur = TfyCalDigits(psz + 5, 4);

        if (hm < 0 || dur < 0 || hm / 100 > 23 || hm % 100 > 59)
            return FALSE;
        pev->nMinutes = (hm / 100) * 60 + hm % 100;
        pev->nDuration = (dur > 0 && dur <= 1440) ? dur : 60;
    }
    StringCchCopyW(pev->szTitle, _countof(pev->szTitle), psz + TFY_CAL_STOREPFX);
    return pev->szTitle[0] != 0;
}

VOID TfyCalFormatStored(const TFYCALEVENT *pev, LPWSTR psz, size_t cch)
{
    if (pev->nMinutes == TFY_CAL_ALLDAY)
    {
        StringCchPrintfW(psz, cch, L"---- ---- %s", pev->szTitle);
        return;
    }
    StringCchPrintfW(psz, cch, L"%02d%02d %04d %s", pev->nMinutes / 60,
                     pev->nMinutes % 60, pev->nDuration, pev->szTitle);
}

LPCWSTR TfyCalScanTime(LPCWSTR p, int *pMinutes)
{
    LPCWSTR q;
    int h, m, n = 0;

    while (n < 2 && p[n] >= L'0' && p[n] <= L'9')
        n++;
    if (n == 0 || p[n] != L':')
        return NULL;
    h = TfyCalDigits(p, n);
    m = TfyCalDigits(p + n + 1, 2);
    if (h < 0 || m < 0)
        return NULL;
    q = p + n + 3;
    while (*q == L' ')
        q++;
    if ((*q == L'a' || *q == L'A' || *q == L'p' || *q == L'P') &&
        (q[1] == L'm' || q[1] == L'M'))
    {
        if (h == 12)
            h = 0;
        if (*q == L'p' || *q == L'P')
            h += 12;
        q += 2;
    }
    if (h > 23 || m > 59)
        return NULL;
    *pMinutes = h * 60 + m;
    return q;
}

BOOL TfyCalParseInput(LPCWSTR psz, TFYCALEVENT *pev)
{
    LPCWSTR p = psz, pTitle, q;
    int start = 0, end = 0, n;

    while (*p == L' ' || *p == L'\t')
        p++;
    pTitle = p;
    pev->nMinutes = TFY_CAL_ALLDAY;
    pev->nDuration = 0;
    q = TfyCalScanTime(p, &start);
    if (q != NULL)
    {
        pev->nMinutes = start;
        pev->nDuration = 60;
        if (*q == L'-' || *q == L'\x2013')
        {
            LPCWSTR r = TfyCalScanTime(q + 1, &end);

            if (r != NULL)
            {
                if (end > start)
                    pev->nDuration = end - start;
                q = r;
            }
        }
        while (*q == L' ' || *q == L'\t')
            q++;
        pTitle = q;
    }
    StringCchCopyW(pev->szTitle, _countof(pev->szTitle), pTitle);
    n = lstrlenW(pev->szTitle);
    while (n > 0 && (pev->szTitle[n - 1] == L' ' || pev->szTitle[n - 1] == L'\t'))
        pev->szTitle[--n] = 0;
    if (pev->nMinutes != TFY_CAL_ALLDAY && pev->nMinutes + pev->nDuration > 1440)
        pev->nDuration = 1440 - pev->nMinutes;
    return pev->szTitle[0] != 0;
}

VOID TfyCalLoadDay(int year, int month, int day, TFYCALDAY *pday)
{
    WCHAR szName[16];
    WCHAR *pszData;
    HKEY hKey = NULL;

    pday->count = 0;
    if (year <= 0)
        return;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, TFY_CAL_KEY, 0, KEY_QUERY_VALUE,
                      &hKey) != ERROR_SUCCESS)
        return;
    pszData = (WCHAR *)LocalAlloc(LPTR, TFY_CAL_DATA * sizeof(WCHAR));
    if (pszData)
    {
        DWORD cb = (DWORD)((TFY_CAL_DATA - 2) * sizeof(WCHAR)), type = 0;

        TfyCalDayName(year, month, day, szName, _countof(szName));
        if (RegQueryValueExW(hKey, szName, NULL, &type, (LPBYTE)pszData, &cb) ==
                ERROR_SUCCESS && type == REG_MULTI_SZ)
        {
            LPCWSTR p;

            for (p = pszData; *p != 0; p += lstrlenW(p) + 1)
            {
                if (pday->count >= TFY_CAL_MAXEVENTS)
                    break;
                if (TfyCalParseStored(p, &pday->ev[pday->count]))
                    pday->count++;
            }
        }
        LocalFree(pszData);
    }
    RegCloseKey(hKey);
}

VOID TfyCalSaveDay(int year, int month, int day, const TFYCALDAY *pday)
{
    WCHAR szName[16], szLine[96];
    WCHAR *pszData;
    HKEY hKey = NULL;
    int i, o = 0;

    if (year <= 0)
        return;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, TFY_CAL_KEY, 0, NULL, 0,
                        KEY_SET_VALUE, NULL, &hKey, NULL) != ERROR_SUCCESS)
        return;
    TfyCalDayName(year, month, day, szName, _countof(szName));
    if (pday->count == 0)
    {
        RegDeleteValueW(hKey, szName);
        RegCloseKey(hKey);
        return;
    }
    pszData = (WCHAR *)LocalAlloc(LPTR, TFY_CAL_DATA * sizeof(WCHAR));
    if (pszData)
    {
        for (i = 0; i < pday->count; i++)
        {
            int cch;

            TfyCalFormatStored(&pday->ev[i], szLine, _countof(szLine));
            cch = lstrlenW(szLine) + 1;
            if (o + cch + 1 > TFY_CAL_DATA)
                break;
            CopyMemory(&pszData[o], szLine, cch * sizeof(WCHAR));
            o += cch;
        }
        pszData[o++] = 0;
        RegSetValueExW(hKey, szName, 0, REG_MULTI_SZ, (const BYTE *)pszData,
                       (DWORD)(o * sizeof(WCHAR)));
        LocalFree(pszData);
    }
    RegCloseKey(hKey);
}

VOID TfyCalLoadMonthCounts(int year, int month, int *pCounts)
{
    HKEY hKey = NULL;
    WCHAR *pszData;
    DWORD i = 0;

    ZeroMemory(pCounts, 32 * sizeof(int));
    if (RegOpenKeyExW(HKEY_CURRENT_USER, TFY_CAL_KEY, 0, KEY_QUERY_VALUE,
                      &hKey) != ERROR_SUCCESS)
        return;
    pszData = (WCHAR *)LocalAlloc(LPTR, TFY_CAL_DATA * sizeof(WCHAR));
    if (!pszData)
    {
        RegCloseKey(hKey);
        return;
    }
    for (;;)
    {
        WCHAR szName[32];
        DWORD cch = _countof(szName);
        DWORD cb = (DWORD)((TFY_CAL_DATA - 2) * sizeof(WCHAR));
        DWORD type = 0;
        LPCWSTR p;
        int day, n = 0;

        ZeroMemory(pszData, TFY_CAL_DATA * sizeof(WCHAR));
        if (RegEnumValueW(hKey, i++, szName, &cch, NULL, &type,
                          (LPBYTE)pszData, &cb) != ERROR_SUCCESS)
            break;
        if (type != REG_MULTI_SZ)
            continue;
        day = TfyCalNameToDay(szName, year, month);
        if (day < 1 || day > 31)
            continue;
        for (p = pszData; *p != 0; p += lstrlenW(p) + 1)
            n++;
        pCounts[day] = n;
    }
    LocalFree(pszData);
    RegCloseKey(hKey);
}


VOID TfyCalSortDates(int *pDates, int count)
{
    int i, j, v;

    for (i = 1; i < count; i++)
    {
        v = pDates[i];
        for (j = i; j > 0 && pDates[j - 1] > v; j--)
            pDates[j] = pDates[j - 1];
        pDates[j] = v;
    }
}

int TfyCalCollectDates(int fromDate, int *pDates, int maxDates)
{
    HKEY hKey = NULL;
    DWORD i = 0;
    int count = 0;

    if (RegOpenKeyExW(HKEY_CURRENT_USER, TFY_CAL_KEY, 0, KEY_QUERY_VALUE,
                      &hKey) != ERROR_SUCCESS)
        return 0;
    for (;;)
    {
        WCHAR szName[32];
        DWORD cch = _countof(szName);
        int v;

        if (count >= maxDates)
            break;
        if (RegEnumValueW(hKey, i++, szName, &cch, NULL, NULL, NULL,
                          NULL) != ERROR_SUCCESS)
            break;
        v = TfyCalDigits(szName, 8);
        if (v <= 0 || szName[8] != 0 || v < fromDate)
            continue;
        pDates[count++] = v;
    }
    RegCloseKey(hKey);
    TfyCalSortDates(pDates, count);
    return count;
}

int TfyCalLoadAgenda(int fromDate, TFYCALROW *pRows, int maxRows)
{
    int *pDates;
    int nDates, i, j, count = 0;

    pDates = (int *)LocalAlloc(LPTR, TFY_CAL_MAXDATES * sizeof(int));
    if (!pDates)
        return 0;
    nDates = TfyCalCollectDates(fromDate, pDates, TFY_CAL_MAXDATES);
    for (i = 0; i < nDates && count < maxRows; i++)
    {
        TFYCALDAY day;
        int date = pDates[i];

        TfyCalLoadDay(date / 10000, (date / 100) % 100, date % 100, &day);
        if (day.count == 0)
            continue;
        pRows[count].date = date;
        pRows[count].bHeader = TRUE;
        ZeroMemory(&pRows[count].ev, sizeof(TFYCALEVENT));
        count++;
        for (j = 0; j < day.count && count < maxRows; j++)
        {
            pRows[count].date = date;
            pRows[count].bHeader = FALSE;
            pRows[count].ev = day.ev[j];
            count++;
        }
    }
    LocalFree(pDates);
    return count;
}
