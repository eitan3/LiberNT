/*
 * PROJECT:     LiberNT Explorer
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Data providers and placement of the Liber desktop widgets
 * COPYRIGHT:   Copyright 2026 Eitan Hadar <eitanhadar4@gmail.com>
 */

#include <windows.h>
#include <strsafe.h>

#include "widgetmodel.h"

static int DayOfWeek(int year, int month, int day)
{
    static const int offsets[] = { 0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4 };

    if (month < 3)
        year -= 1;
    int sunday = (year + year / 4 - year / 100 + year / 400 + offsets[month - 1] + day) % 7;
    return sunday == 0 ? 7 : sunday;
}

static BOOL IsLeap(int year)
{
    return (year % 4 == 0 && year % 100 != 0) || year % 400 == 0;
}

static int WeeksInYear(int year)
{
    int p = (year + year / 4 - year / 100 + year / 400) % 7;
    int q = ((year - 1) + (year - 1) / 4 - (year - 1) / 100 + (year - 1) / 400) % 7;
    return (p == 4 || q == 3) ? 53 : 52;
}

int LiberWidget_IsoWeek(const SYSTEMTIME *pst)
{
    static const int before[] = { 0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334 };
    int year = pst->wYear, month = pst->wMonth, day = pst->wDay;

    if (month < 1 || month > 12 || day < 1 || day > 31)
        return 0;
    int ordinal = before[month - 1] + day + (month > 2 && IsLeap(year) ? 1 : 0);
    int week = (ordinal - DayOfWeek(year, month, day) + 10) / 7;
    if (week < 1)
        return WeeksInYear(year - 1);
    if (week > WeeksInYear(year))
        return 1;
    return week;
}

int LiberWidget_CpuPercent(const LIBER_CPU_SAMPLE *pBefore, const LIBER_CPU_SAMPLE *pAfter)
{
    if (pAfter->Idle < pBefore->Idle || pAfter->Kernel < pBefore->Kernel || pAfter->User < pBefore->User)
        return 0;
    ULONGLONG idle = pAfter->Idle - pBefore->Idle;
    ULONGLONG total = (pAfter->Kernel - pBefore->Kernel) + (pAfter->User - pBefore->User);
    if (total == 0 || idle > total)
        return 0;
    return (int)(((total - idle) * 100 + total / 2) / total);
}

UINT LiberWidget_MockSlot(const SYSTEMTIME *pst)
{
    return (UINT)(pst->wHour / 6) % 4;
}

VOID LiberWidget_Layout(const RECT *prcWork, UINT dpi, RECT prcWidgets[LIBER_WIDGET_COUNT])
{
    int tile = MulDiv(LIBER_WIDGET_TILE, (int)dpi, 96);
    int gap = MulDiv(LIBER_WIDGET_GAP, (int)dpi, 96);
    int margin = MulDiv(LIBER_WIDGET_MARGIN, (int)dpi, 96);
    LONG right = prcWork->right - margin;
    LONG top = prcWork->top + margin;

    SetRect(&prcWidgets[LIBER_WIDGET_DATE], right - 2 * tile - gap, top, right - tile - gap, top + tile);
    SetRect(&prcWidgets[LIBER_WIDGET_WEATHER], right - tile, top, right, top + tile);
    SetRect(&prcWidgets[LIBER_WIDGET_SYSTEM], right - 2 * tile - gap, top + tile + gap, right, top + 2 * tile + gap);
}

CDateProvider::CDateProvider()
{
    ZeroMemory(&m_Data, sizeof(m_Data));
}

void CDateProvider::SetDate(const SYSTEMTIME &st)
{
    GetDateFormatW(LOCALE_USER_DEFAULT, 0, &st, L"dddd", m_Data.szDayName, _countof(m_Data.szDayName));
    GetDateFormatW(LOCALE_USER_DEFAULT, 0, &st, L"MMMM yyyy", m_Data.szMonthYear, _countof(m_Data.szMonthYear));
    m_Data.Day = st.wDay;
    m_Data.IsoWeek = LiberWidget_IsoWeek(&st);
}

BOOL CDateProvider::Refresh()
{
    SYSTEMTIME st;
    int day = m_Data.Day;

    GetLocalTime(&st);
    SetDate(st);
    return day != m_Data.Day;
}

static const struct
{
    LPCWSTR Condition;
    int Temperature, High, Low;
} g_MockWeather[4] =
{
    { L"Clear", 14, 17, 9 },
    { L"Partly cloudy", 18, 21, 11 },
    { L"Light rain", 15, 16, 12 },
    { L"Cloudy", 12, 15, 8 },
};

CMockWeatherProvider::CMockWeatherProvider()
    : m_Slot((UINT)-1)
{
    ZeroMemory(&m_Data, sizeof(m_Data));
    SetSlot(0);
}

void CMockWeatherProvider::SetSlot(UINT slot)
{
    slot %= _countof(g_MockWeather);
    m_Slot = slot;
    m_Data.City = L"Sample city";
    m_Data.Condition = g_MockWeather[slot].Condition;
    m_Data.Icon = L"weather_moon_20_regular";
    m_Data.Temperature = g_MockWeather[slot].Temperature;
    m_Data.High = g_MockWeather[slot].High;
    m_Data.Low = g_MockWeather[slot].Low;
    m_Data.Sample = TRUE;
}

BOOL CMockWeatherProvider::Refresh()
{
    SYSTEMTIME st;
    UINT before = m_Slot;

    GetLocalTime(&st);
    SetSlot(LiberWidget_MockSlot(&st));
    return before != m_Slot;
}

static ULONGLONG FileTime64(const FILETIME &ft)
{
    return ((ULONGLONG)ft.dwHighDateTime << 32) | ft.dwLowDateTime;
}

CSystemStatsProvider::CSystemStatsProvider()
    : m_HaveLast(FALSE)
{
    ZeroMemory(&m_Data, sizeof(m_Data));
    ZeroMemory(&m_Last, sizeof(m_Last));
}

BOOL CSystemStatsProvider::Refresh()
{
    FILETIME idle, kernel, user;
    MEMORYSTATUSEX memory = { sizeof(memory) };
    ULARGE_INTEGER freeBytes, totalBytes;
    WCHAR szRoot[MAX_PATH];
    LIBER_SYSTEM_DATA before = m_Data;

    if (GetSystemTimes(&idle, &kernel, &user))
    {
        LIBER_CPU_SAMPLE now = { FileTime64(idle), FileTime64(kernel), FileTime64(user) };
        if (m_HaveLast)
            m_Data.CpuPercent = LiberWidget_CpuPercent(&m_Last, &now);
        m_Last = now;
        m_HaveLast = TRUE;
    }
    if (GlobalMemoryStatusEx(&memory))
        m_Data.MemoryPercent = (int)memory.dwMemoryLoad;
    if (GetWindowsDirectoryW(szRoot, _countof(szRoot)) >= 3)
    {
        szRoot[3] = 0;
        if (GetDiskFreeSpaceExW(szRoot, &freeBytes, &totalBytes, NULL) && totalBytes.QuadPart)
            m_Data.StoragePercent = (int)(((totalBytes.QuadPart - freeBytes.QuadPart) * 100 + totalBytes.QuadPart / 2) /
                                          totalBytes.QuadPart);
    }
    return memcmp(&before, &m_Data, sizeof(before)) != 0;
}
