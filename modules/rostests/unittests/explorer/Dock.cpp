/*
 * PROJECT:     LiberNT Explorer - Unit-tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test the Liber experience switch, dock placement and task window rules
 * COPYRIGHT:   Copyright 2026 Eitan Hadar <eitanhadar4@gmail.com>
 */

#include <apitest.h>

#include <windows.h>
#include <math.h>
#include <liberui.h>

#include "experience.h"
#include "liberdock.h"
#include "libershell.h"

static void CheckRect(const RECT &rc, LONG l, LONG t, LONG r, LONG b, const char *what)
{
    ok(rc.left == l && rc.top == t && rc.right == r && rc.bottom == b,
       "%s is (%ld,%ld,%ld,%ld), expected (%ld,%ld,%ld,%ld)\n", what, rc.left, rc.top, rc.right, rc.bottom, l, t, r, b);
}

static void TestLayout()
{
    RECT monitor = { 0, 0, 1920, 1080 };
    LIBER_DOCK_LAYOUT layout;

    ok(LiberDock_Width(10) == 514, "ten cells make the spec's 514 px dock: %d\n", LiberDock_Width(10));
    ok(LiberDock_Width(1) == 64, "one cell: %d\n", LiberDock_Width(1));
    WCHAR label[8];
    ok(LiberDock_LanguageLabel(MAKELANGID(LANG_ENGLISH, SUBLANG_ENGLISH_US), label, _countof(label)) &&
       !wcscmp(label, L"ENG"), "English shows ENG: %S\n", label);
    ok(LiberDock_LanguageLabel(MAKELANGID(LANG_FRENCH, SUBLANG_FRENCH), label, _countof(label)) &&
       !wcscmp(label, L"FRA"), "French shows FRA: %S\n", label);
    ok(!LiberDock_LanguageLabel(MAKELANGID(LANG_ENGLISH, SUBLANG_ENGLISH_US), label, 3), "a short buffer fails\n");
    ok(LiberDock_HopOffset(0.0) == 0.0 && LiberDock_HopOffset(1.0) == 0.0 && LiberDock_HopOffset(1.5) == 0.0,
       "the launch hop rests at both ends\n");
    ok(fabs(LiberDock_HopOffset(0.5) - LIBER_DOCK_HOP) < 0.001 && LiberDock_HopOffset(0.25) > 0.0 &&
       fabs(LiberDock_HopOffset(0.25) - LiberDock_HopOffset(0.75)) < 0.001, "the hop peaks halfway\n");
    ok(LiberDock_SystemPillWidth(TRUE) == 100 && LiberDock_SystemPillWidth(FALSE) == 70,
       "system group is %d px with a battery and %d px without\n", LiberDock_SystemPillWidth(TRUE),
       LiberDock_SystemPillWidth(FALSE));

    LiberDock_Layout(&monitor, 96, 5, 112, &layout);
    CheckRect(layout.Dock, (1920 - 264) / 2, 1080 - 8 - 60, (1920 - 264) / 2 + 264, 1080 - 8, "dock at 96 DPI");
    CheckRect(layout.Tray, 1920 - 12 - 112, 1012 + 6, 1920 - 12, 1012 + 6 + 48, "tray at 96 DPI");
    ok((layout.Tray.top + layout.Tray.bottom) == (layout.Dock.top + layout.Dock.bottom), "tray sits on the dock's center line\n");
    ok(LIBER_TASKBAR_DOCK - (1080 - layout.Dock.top) == 8, "dock fits the reserved 76 px\n");

    RECT second = { 1920, 0, 3840, 1080 };
    LiberDock_Layout(&second, 144, 1, 112, &layout);
    CheckRect(layout.Dock, 1920 + (1920 - 96) / 2, 1080 - 12 - 90, 1920 + (1920 - 96) / 2 + 96, 1080 - 12,
              "one cell at 144 DPI on a second monitor");
    CheckRect(layout.Tray, 3840 - 18 - 168, 978 + 9, 3840 - 18, 978 + 9 + 72, "tray at 144 DPI");
}

static void TestWorkArea()
{
    RECT monitor = { 0, 0, 1920, 1080 }, work;

    LiberDock_WorkArea(&monitor, 96, &work);
    CheckRect(work, 0, 0, 1920, 1080 - LIBER_TASKBAR_DOCK, "work area at 96 DPI");
    LIBER_DOCK_LAYOUT layout;
    LiberDock_Layout(&monitor, 96, 5, 112, &layout);
    ok(work.bottom <= layout.Dock.top && work.bottom <= layout.Tray.top, "the work area ends above the dock\n");

    RECT second = { 1920, -200, 3840, 880 };
    LiberDock_WorkArea(&second, 144, &work);
    CheckRect(work, 1920, -200, 3840, 880 - 114, "work area at 144 DPI on a second monitor");

    RECT tiny = { 0, 0, 640, 60 };
    LiberDock_WorkArea(&tiny, 96, &work);
    CheckRect(work, 0, 0, 640, 0, "a monitor shorter than the dock");
}

static void TestTaskWindows()
{
    HWND normal = CreateWindowExW(0, L"STATIC", L"Liber test window", WS_OVERLAPPEDWINDOW, -2000, -2000, 100, 100,
                                  NULL, NULL, NULL, NULL);
    HWND tool = CreateWindowExW(WS_EX_TOOLWINDOW, L"STATIC", L"Liber tool window", WS_POPUP, -2000, -2000, 100, 100,
                                NULL, NULL, NULL, NULL);
    HWND untitled = CreateWindowExW(0, L"STATIC", L"", WS_OVERLAPPEDWINDOW, -2000, -2000, 100, 100, NULL, NULL, NULL,
                                    NULL);
    HWND owned = CreateWindowExW(0, L"STATIC", L"Liber owned window", WS_OVERLAPPEDWINDOW, -2000, -2000, 100, 100,
                                 normal, NULL, NULL, NULL);

    ok(normal && tool && untitled && owned, "test windows created\n");
    ok(!LiberDock_IsTaskWindow(normal), "a hidden window is not a task\n");
    ShowWindow(normal, SW_SHOWNOACTIVATE);
    ShowWindow(tool, SW_SHOWNOACTIVATE);
    ShowWindow(untitled, SW_SHOWNOACTIVATE);
    ShowWindow(owned, SW_SHOWNOACTIVATE);
    ok(LiberDock_IsTaskWindow(normal), "a visible titled top-level window is a task\n");
    ok(!LiberDock_IsTaskWindow(tool), "a tool window is not a task\n");
    ok(!LiberDock_IsTaskWindow(untitled), "an untitled window is not a task\n");
    ok(!LiberDock_IsTaskWindow(owned), "an owned window is not a task\n");
    ok(!LiberDock_IsTaskWindow(NULL), "NULL is not a task\n");
    DestroyWindow(owned);
    DestroyWindow(untitled);
    DestroyWindow(tool);
    DestroyWindow(normal);
    ok(!LiberDock_IsTaskWindow(normal), "a destroyed window is not a task\n");

    ok(LiberDock_SameApp(L"C:\\Windows\\notepad.exe", L"c:\\windows\\NOTEPAD.EXE"), "paths compare without case\n");
    ok(!LiberDock_SameApp(L"C:\\Windows\\notepad.exe", L"C:\\Windows\\regedit.exe"), "different apps\n");
    ok(!LiberDock_SameApp(L"", L"") && !LiberDock_SameApp(NULL, L"x"), "empty paths never match\n");
}

static void TestExperience()
{
    HKEY key;
    DWORD saved = 0, size = sizeof(saved), type = 0, value;
    BOOL had;

    if (RegCreateKeyExW(HKEY_CURRENT_USER, LIBER_DESIGN_KEY, 0, NULL, 0, KEY_QUERY_VALUE | KEY_SET_VALUE, NULL, &key,
                        NULL) != ERROR_SUCCESS)
    {
        skip("cannot open the design key\n");
        return;
    }
    had = RegQueryValueExW(key, LIBER_EXPERIENCE_VALUE, NULL, &type, (BYTE *)&saved, &size) == ERROR_SUCCESS;

    RegDeleteValueW(key, LIBER_EXPERIENCE_VALUE);
    ok(LiberExperience_Get() == LIBER_EXPERIENCE_CLASSIC && !LiberExperience_IsLiber(), "Classic by default\n");
    value = LIBER_EXPERIENCE_LIBER;
    RegSetValueExW(key, LIBER_EXPERIENCE_VALUE, 0, REG_DWORD, (BYTE *)&value, sizeof(value));
    ok(LiberExperience_IsLiber(), "Liber when the value is 1\n");
    value = 7;
    RegSetValueExW(key, LIBER_EXPERIENCE_VALUE, 0, REG_DWORD, (BYTE *)&value, sizeof(value));
    ok(LiberExperience_Get() == LIBER_EXPERIENCE_CLASSIC, "unknown values mean Classic\n");
    RegSetValueExW(key, LIBER_EXPERIENCE_VALUE, 0, REG_SZ, (BYTE *)L"1", 4);
    ok(LiberExperience_Get() == LIBER_EXPERIENCE_CLASSIC, "a string value means Classic\n");

    RegDeleteValueW(key, LIBER_EXPERIENCE_VALUE);
    if (had)
        RegSetValueExW(key, LIBER_EXPERIENCE_VALUE, 0, type, (BYTE *)&saved, size);
    RegCloseKey(key);
}

static void TestFlyoutOrigin()
{
    RECT monitor = { 0, 0, 1600, 900 }, tray = { 1317, 838, 1588, 886 }, dock = { 543, 832, 1057, 892 };
    POINT pt;

    LiberShell_FlyoutOrigin(&monitor, 96, &tray, 384, 474, FALSE, &pt);
    ok(pt.x == 1588 - 384 && pt.y == 900 - 80 - 474, "control center sits above the reserved taskbar at %ld,%ld\n",
       pt.x, pt.y);
    LiberShell_FlyoutOrigin(&monitor, 96, &dock, 660, 700, TRUE, &pt);
    ok(pt.x == (1600 - 660) / 2 && pt.y == 900 - 80 - 700, "Start is centered at %ld,%ld\n", pt.x, pt.y);

    RECT edge = { 1500, 838, 1600, 886 };
    LiberShell_FlyoutOrigin(&monitor, 96, &edge, 384, 474, FALSE, &pt);
    ok(pt.x == 1600 - 12 - 384, "kept 12 px from the right edge: %ld\n", pt.x);
    RECT left = { 0, 838, 50, 886 };
    LiberShell_FlyoutOrigin(&monitor, 96, &left, 384, 474, FALSE, &pt);
    ok(pt.x == 12, "kept 12 px from the left edge: %ld\n", pt.x);
    LiberShell_FlyoutOrigin(&monitor, 96, &tray, 384, 2000, FALSE, &pt);
    ok(pt.y == 12, "a tall flyout stays below the top inset: %ld\n", pt.y);
    LiberShell_FlyoutOrigin(&monitor, 144, &tray, 576, 600, FALSE, &pt);
    ok(pt.y == 900 - 120 - 600, "gap and reserve scale with DPI: %ld\n", pt.y);
}

static void TestCalendarGrid()
{
    LIBER_CALENDAR_DAY days[LIBER_CALENDAR_CELLS];

    ok(LiberCalendar_DaysInMonth(2024, 2) == 29 && LiberCalendar_DaysInMonth(2026, 2) == 28 &&
       LiberCalendar_DaysInMonth(1900, 2) == 28 && LiberCalendar_DaysInMonth(2000, 2) == 29,
       "February follows the leap year rules\n");
    ok(LiberCalendar_DaysInMonth(2026, 11) == 30 && LiberCalendar_DaysInMonth(2026, 12) == 31 &&
       LiberCalendar_DaysInMonth(2026, 13) == 0, "month lengths\n");
    ok(LiberCalendar_DayOfWeek(2026, 11, 27) == 5 && LiberCalendar_DayOfWeek(2026, 11, 1) == 0 &&
       LiberCalendar_DayOfWeek(2000, 1, 1) == 6, "weekdays (0 is Sunday)\n");

    LiberCalendar_MonthGrid(2026, 11, 0, days);
    ok(days[0].wMonth == 11 && days[0].wDay == 1 && days[0].bInMonth, "November 2026 starts on a Sunday\n");
    ok(days[29].wDay == 30 && days[29].bInMonth && days[30].wMonth == 12 && days[30].wDay == 1 && !days[30].bInMonth,
       "the grid runs into December\n");

    LiberCalendar_MonthGrid(2026, 11, 1, days);
    ok(days[0].wMonth == 10 && days[0].wDay == 26 && !days[0].bInMonth && days[6].wDay == 1 && days[6].bInMonth,
       "a Monday-first grid matches the spec demo: %u/%u\n", days[0].wMonth, days[0].wDay);
    ok(days[32].wDay == 27 && days[32].wMonth == 11, "the 27th sits in the fifth row, column five\n");
    ok(days[41].wMonth == 12 && days[41].wDay == 6, "six full weeks: %u/%u\n", days[41].wMonth, days[41].wDay);

    LiberCalendar_MonthGrid(2027, 1, 0, days);
    ok(days[0].wYear == 2026 && days[0].wMonth == 12 && days[0].wDay == 27 && days[5].wYear == 2027 &&
       days[5].wDay == 1, "January 2027 reaches back into December 2026\n");
}

static void TestWallpaperMapping()
{
    RECT tray = { 1600 - 12 - 232, 900 - 62, 1600 - 12, 900 - 14 }, out;
    SIZE screen = { 1600, 900 }, wide = { 3200, 1800 }, tall = { 900, 1600 };

    LiberShell_MapToWallpaper(&tray, screen, wide, LIBER_WALLPAPER_FILL, &out);
    ok(out.left == 2 * tray.left && out.top == 2 * tray.top && out.right == 2 * tray.right && out.bottom == 2 * tray.bottom,
       "fill at the same aspect scales the rect: (%ld,%ld,%ld,%ld)\n", out.left, out.top, out.right, out.bottom);
    LiberShell_MapToWallpaper(&tray, screen, tall, LIBER_WALLPAPER_FILL, &out);
    ok(out.right <= 900 && out.left >= 0 && out.bottom > out.top, "fill crops a tall image: (%ld,%ld,%ld,%ld)\n",
       out.left, out.top, out.right, out.bottom);
    LiberShell_MapToWallpaper(&tray, screen, tall, LIBER_WALLPAPER_FIT, &out);
    ok(IsRectEmpty(&out), "fit leaves the bottom right corner outside a tall image\n");
    LiberShell_MapToWallpaper(&tray, screen, tall, LIBER_WALLPAPER_STRETCH, &out);
    ok(out.right == 894 && out.bottom == 1576, "stretch maps each axis: (%ld,%ld)\n",
       out.right, out.bottom);
    RECT center = { 790, 440, 810, 460 };
    SIZE image = { 100, 100 };
    LiberShell_MapToWallpaper(&center, screen, image, LIBER_WALLPAPER_CENTER, &out);
    ok(out.left == 40 && out.top == 40 && out.right == 60 && out.bottom == 60, "center keeps pixels 1:1\n");

    ok(LiberShell_Luminance(0xFF000000) == 0.0 && LiberShell_Luminance(0xFFFFFFFF) > 0.999, "black and white\n");
    ok(fabs(LiberShell_Luminance(0xFF808080) - 0.2159) < 0.001, "mid grey is linearised: %f\n",
       LiberShell_Luminance(0xFF808080));
    ok(LiberShell_Luminance(0xFF00FF00) > LiberShell_Luminance(0xFFFF0000) &&
       LiberShell_Luminance(0xFFFF0000) > LiberShell_Luminance(0xFF0000FF), "green is brightest, blue darkest\n");
}

static void TestMenuOrigin()
{
    RECT work = { 0, 0, 1600, 824 };
    POINT pt, anchor = { 800, 600 };

    LiberMenu_Origin(&work, anchor, 200, 100, &pt);
    ok(pt.x == 800 && pt.y == 500, "a menu opens above and right of the pointer: %ld,%ld\n", pt.x, pt.y);
    anchor.x = 1550;
    LiberMenu_Origin(&work, anchor, 200, 100, &pt);
    ok(pt.x == 1350, "a menu near the right edge opens to the left: %ld\n", pt.x);
    anchor.x = 10;
    anchor.y = 50;
    LiberMenu_Origin(&work, anchor, 200, 100, &pt);
    ok(pt.x == 10 && pt.y == 50, "a menu near the top opens below the pointer: %ld,%ld\n", pt.x, pt.y);
    anchor.y = 820;
    LiberMenu_Origin(&work, anchor, 200, 900, &pt);
    ok(pt.y == 824 - 900, "a menu taller than the work area is pinned to its bottom: %ld\n", pt.y);
}

static void TestStartPins()
{
    static WCHAR saved[64 * MAX_PATH + 1], pins[64 * MAX_PATH + 1];
    UINT count = 0;
    BOOL had = LiberStart_LoadPins(saved, _countof(saved), &count);
    HKEY key;

    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\LiberNT\\Shell\\Start", 0, KEY_SET_VALUE, &key) == ERROR_SUCCESS)
    {
        RegDeleteValueW(key, L"Pins");
        RegCloseKey(key);
    }
    ok(!LiberStart_LoadPins(pins, _countof(pins), &count) && count == 0 && pins[0] == 0, "no pins by default\n");
    ok(LiberStart_SetPinned(pins, L"C:\\Apps\\One.lnk", TRUE), "pin one\n");
    ok(LiberStart_LoadPins(pins, _countof(pins), &count) && count == 1 && !wcscmp(pins, L"C:\\Apps\\One.lnk"),
       "one pin stored\n");
    ok(LiberStart_SetPinned(pins, L"C:\\Apps\\Two.lnk", TRUE) && LiberStart_LoadPins(pins, _countof(pins), &count) &&
       count == 2, "pins append\n");
    ok(LiberStart_IsPinned(pins, L"c:\\apps\\two.LNK") && !LiberStart_IsPinned(pins, L"C:\\Apps\\Three.lnk"),
       "pins compare without case\n");
    ok(LiberStart_SetPinned(pins, L"C:\\APPS\\ONE.LNK", FALSE) && LiberStart_LoadPins(pins, _countof(pins), &count) &&
       count == 1 && !wcscmp(pins, L"C:\\Apps\\Two.lnk"), "unpin removes regardless of case\n");
    ok(LiberStart_SetPinned(pins, L"C:\\Apps\\Two.lnk", TRUE) && LiberStart_LoadPins(pins, _countof(pins), &count) &&
       count == 1, "pinning twice keeps one entry\n");
    ok(LiberStart_SetPinned(pins, L"C:\\Apps\\Two.lnk", FALSE) && LiberStart_LoadPins(pins, _countof(pins), &count) &&
       count == 0, "an empty pin list is kept as empty, not as the default\n");
    ok(!LiberStart_SetPinned(pins, L"", TRUE), "an empty path is refused\n");
    ok(!LiberStart_LoadPins(pins, 1, &count), "a tiny buffer is refused\n");

    ok(LiberStart_SetPinned(pins, L"C:\\Apps\\Long-one.lnk", TRUE) &&
           LiberStart_LoadPins(pins, _countof(pins), &count) &&
           LiberStart_SetPinned(pins, L"C:\\Apps\\Long-two.lnk", TRUE) &&
           LiberStart_LoadPins(pins, _countof(pins), &count) && count == 2,
       "two long pins loaded into the buffer\n");
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\LiberNT\\Shell\\Start", 0, KEY_SET_VALUE, &key) == ERROR_SUCCESS)
    {
        static const WCHAR Unterminated[] = L"C:\\A.lnk";
        ok(RegSetValueExW(key, L"Pins", 0, REG_MULTI_SZ, (const BYTE *)Unterminated,
                          (DWORD)(lstrlenW(Unterminated) * sizeof(WCHAR))) == ERROR_SUCCESS,
           "store a list without its terminators\n");
        RegCloseKey(key);
        ok(LiberStart_LoadPins(pins, _countof(pins), &count) && count == 1 && !wcscmp(pins, Unterminated),
           "an unterminated list is read as one pin, not with stale pins after it: %u\n", count);
    }
    else
    {
        skip("cannot write the Start key\n");
    }
    WCHAR small2[8];
    ok(!LiberStart_LoadPins(small2, _countof(small2), &count) || small2[_countof(small2) - 1] == 0 ||
           small2[_countof(small2) - 2] == 0,
       "a short buffer never ends without a terminator\n");

    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\LiberNT\\Shell\\Start", 0, KEY_SET_VALUE, &key) == ERROR_SUCCESS)
    {
        if (had)
        {
            DWORD chars = 0;
            for (LPCWSTR p = saved; *p; p += lstrlenW(p) + 1)
                chars = (DWORD)(p - saved) + lstrlenW(p) + 1;
            RegSetValueExW(key, L"Pins", 0, REG_MULTI_SZ, (const BYTE *)saved, (chars + 1) * sizeof(WCHAR));
        }
        else
        {
            RegDeleteValueW(key, L"Pins");
        }
        RegCloseKey(key);
    }
}

static void TestRecentHelpers()
{
    SYSTEMTIME now = { 2026, 11, 5, 27, 11, 20, 0, 0 }, then = now;
    WCHAR text[64], time[32];

    LiberStart_FormatWhen(&now, &then, text, _countof(text));
    ok(!wcscmp(text, L"Just now"), "same minute: %S\n", text);
    then.wMinute = 20 - 41 + 60;
    then.wHour = 10;
    LiberStart_FormatWhen(&now, &then, text, _countof(text));
    ok(!wcscmp(text, L"41 min ago"), "41 minutes: %S\n", text);
    then = now;
    then.wHour = 6;
    then.wMinute = 18;
    GetTimeFormatW(LOCALE_USER_DEFAULT, TIME_NOSECONDS, &then, NULL, time, _countof(time));
    LiberStart_FormatWhen(&now, &then, text, _countof(text));
    ok(!wcsncmp(text, L"Today ", 6) && !wcscmp(text + 6, time), "earlier today: %S\n", text);
    then.wDay = 26;
    LiberStart_FormatWhen(&now, &then, text, _countof(text));
    ok(!wcsncmp(text, L"Yesterday ", 10), "yesterday: %S\n", text);
    SYSTEMTIME first = { 2026, 12, 2, 1, 0, 5, 0, 0 }, last = { 2026, 11, 1, 30, 23, 50, 0, 0 };
    LiberStart_FormatWhen(&first, &last, text, _countof(text));
    ok(!wcscmp(text, L"15 min ago"), "minutes across midnight and a month: %S\n", text);
    then.wDay = 20;
    LiberStart_FormatWhen(&now, &then, text, _countof(text));
    ok(wcsncmp(text, L"Today", 5) && wcsncmp(text, L"Yesterday", 9) && text[0], "older shows a date: %S\n", text);

    ok(LiberStart_ScrollOffset(0, 40, 6, 18, 1) == 6, "one row down\n");
    ok(LiberStart_ScrollOffset(18, 40, 6, 18, 1) == 24, "last full page starts at 24\n");
    ok(LiberStart_ScrollOffset(24, 40, 6, 18, 1) == 24, "does not scroll past the end\n");
    ok(LiberStart_ScrollOffset(6, 40, 6, 18, -1) == 0 && LiberStart_ScrollOffset(0, 40, 6, 18, -1) == 0,
       "scrolls back to the top and no further\n");
    ok(LiberStart_ScrollOffset(0, 10, 6, 18, 1) == 0, "nothing to scroll when everything fits\n");

    LIBER_RECENT_FILE files[6];
    UINT count = LiberStart_LoadRecentFiles(files, _countof(files));
    ok(count <= 6, "at most six recent files: %u\n", count);
    for (UINT i = 1; i < count; i++)
    {
        FILETIME a, b;
        SystemTimeToFileTime(&files[i - 1].stWritten, &a);
        SystemTimeToFileTime(&files[i].stWritten, &b);
        ok(CompareFileTime(&a, &b) >= 0, "recent file %u is not newer than the one before\n", i);
        ok(GetFileAttributesW(files[i].szLink) != INVALID_FILE_ATTRIBUTES, "recent link %u exists\n", i);
    }
    ok(LiberStart_LoadRecentFiles(files, 0) == 0, "no room returns nothing\n");
}

static void TestDesignValues()
{
    HKEY key;
    DWORD savedTint = 0, tintSize = sizeof(savedTint), value;
    WCHAR savedFont[LF_FACESIZE], font[LF_FACESIZE];
    DWORD fontSize = sizeof(savedFont);
    BOOL hadTint, hadFont;

    if (RegCreateKeyExW(HKEY_CURRENT_USER, LIBER_DESIGN_KEY, 0, NULL, 0, KEY_QUERY_VALUE | KEY_SET_VALUE, NULL, &key,
                        NULL) != ERROR_SUCCESS)
    {
        skip("cannot open the design key\n");
        return;
    }
    hadTint = RegQueryValueExW(key, L"SurfaceTint", NULL, NULL, (BYTE *)&savedTint, &tintSize) == ERROR_SUCCESS;
    hadFont = RegQueryValueExW(key, L"UiFont", NULL, NULL, (BYTE *)savedFont, &fontSize) == ERROR_SUCCESS;

    RegDeleteValueW(key, L"SurfaceTint");
    ok(LiberExperience_SurfaceTint() == 1, "Soft tint by default\n");
    value = 2;
    RegSetValueExW(key, L"SurfaceTint", 0, REG_DWORD, (BYTE *)&value, sizeof(value));
    ok(LiberExperience_SurfaceTint() == 2, "Vivid tint\n");
    value = 9;
    RegSetValueExW(key, L"SurfaceTint", 0, REG_DWORD, (BYTE *)&value, sizeof(value));
    ok(LiberExperience_SurfaceTint() == 1, "unknown tint means Soft\n");

    RegDeleteValueW(key, L"UiFont");
    ok(!LiberExperience_UiFont(font, _countof(font)) && font[0] == 0, "no desktop font by default\n");
    RegSetValueExW(key, L"UiFont", 0, REG_SZ, (BYTE *)L"Verdana", sizeof(L"Verdana"));
    ok(LiberExperience_UiFont(font, _countof(font)) && !wcscmp(font, L"Verdana"), "desktop font %ls\n", font);
    ok(!LiberExperience_UiFont(font, 3) && font[0] == 0, "a short buffer is refused\n");
    ok(!LiberExperience_UiFont(NULL, 10), "NULL buffer\n");

    RegDeleteValueW(key, L"SurfaceTint");
    RegDeleteValueW(key, L"UiFont");
    if (hadTint)
        RegSetValueExW(key, L"SurfaceTint", 0, REG_DWORD, (BYTE *)&savedTint, tintSize);
    if (hadFont)
        RegSetValueExW(key, L"UiFont", 0, REG_SZ, (BYTE *)savedFont, fontSize);
    RegCloseKey(key);
}

static void TestStartSearch()
{
    ok(LiberStart_Matches(L"Notepad", L""), "an empty query matches everything\n");
    ok(LiberStart_Matches(L"Notepad", NULL), "a NULL query matches everything\n");
    ok(LiberStart_Matches(L"Notepad", L"pad"), "substring\n");
    ok(LiberStart_Matches(L"Command Prompt", L"PROMPT"), "case-insensitive\n");
    ok(!LiberStart_Matches(L"Notepad", L"paint"), "no match\n");
    ok(!LiberStart_Matches(L"", L"a") && !LiberStart_Matches(NULL, L"a"), "empty names never match a query\n");
}

static void TestWallpaperCache()
{
    RECT tray = { 0, 0, 200, 48 }, other = { 100, 100, 300, 148 };
    double first = -1.0, second = -2.0, moved = -3.0;
    LARGE_INTEGER freq, t0, t1;

    if (!LiberShell_WallpaperLuma(&tray, &first))
    {
        skip("no decodable wallpaper\n");
        return;
    }
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&t0);
    for (int i = 0; i < 20; i++)
        ok(LiberShell_WallpaperLuma(&tray, &second), "the luminance is read again\n");
    QueryPerformanceCounter(&t1);
    ok(second == first, "the same wallpaper and area give the same luminance: %f %f\n", first, second);
    ok((t1.QuadPart - t0.QuadPart) * 1000 / freq.QuadPart < 20,
       "repeated reads do not decode the wallpaper again: %lld ms\n", (t1.QuadPart - t0.QuadPart) * 1000 / freq.QuadPart);
    ok(LiberShell_WallpaperLuma(&other, &moved) && moved >= 0.0 && moved <= 1.0, "another area is sampled\n");
    ok(LiberShell_WallpaperLuma(&tray, &second) && second == first, "the first area reads the same again\n");
}

static void TestCellRect()
{
    RECT dock = { 400, 1012, 400 + LiberDock_Width(5), 1012 + LIBER_DOCK_HEIGHT }, cell;

    LiberDock_CellRect(&dock, 96, 0, &cell);
    CheckRect(cell, 408, 1018, 456, 1066, "the Start cell");
    LiberDock_CellRect(&dock, 96, 2, &cell);
    CheckRect(cell, 508, 1018, 556, 1066, "the first app cell follows Start and Search");
    LiberDock_CellRect(&dock, 96, 4, &cell);
    ok(cell.right == dock.right - LIBER_DOCK_PAD_X, "the last cell ends at the dock padding: %ld\n", cell.right);
    ok(cell.bottom == dock.bottom - LIBER_DOCK_PAD_Y, "cells are centred vertically: %ld\n", cell.bottom);

    RECT scaled = { 100, 200, 100 + LiberUI::ScaleToDpi(LiberDock_Width(3), 144), 200 + LiberUI::ScaleToDpi(LIBER_DOCK_HEIGHT, 144) };
    LiberDock_CellRect(&scaled, 144, 2, &cell);
    CheckRect(cell, 100 + LiberUI::ScaleToDpi(108, 144), 200 + LiberUI::ScaleToDpi(6, 144), 100 + LiberUI::ScaleToDpi(156, 144),
              200 + LiberUI::ScaleToDpi(54, 144), "an app cell at 150%");
    ok(cell.right <= scaled.right && cell.bottom <= scaled.bottom, "the cell stays inside the dock at 150%%\n");
}

static void TestMenuDismiss()
{
    RECT menu = { 100, 100, 300, 260 };
    POINT inside = { 150, 150 }, outside = { 50, 50 }, edge = { 300, 150 };

    ok(LiberMenu_DismissesOn(WM_LBUTTONDOWN, outside, &menu), "a left click outside closes the menu\n");
    ok(LiberMenu_DismissesOn(WM_RBUTTONDOWN, outside, &menu), "so does a right click\n");
    ok(LiberMenu_DismissesOn(WM_MBUTTONDOWN, outside, &menu) && LiberMenu_DismissesOn(WM_XBUTTONDOWN, outside, &menu),
       "and the middle and extra buttons\n");
    ok(LiberMenu_DismissesOn(WM_NCLBUTTONDOWN, outside, &menu), "a click on a caption outside closes it\n");
    ok(!LiberMenu_DismissesOn(WM_LBUTTONDOWN, inside, &menu), "a click inside is for the menu\n");
    ok(LiberMenu_DismissesOn(WM_LBUTTONDOWN, edge, &menu), "the right edge is outside the menu\n");
    ok(!LiberMenu_DismissesOn(WM_MOUSEMOVE, outside, &menu), "moving outside keeps the menu\n");
    ok(!LiberMenu_DismissesOn(WM_LBUTTONUP, outside, &menu), "a release outside keeps the menu\n");
    ok(!LiberMenu_DismissesOn(WM_MOUSEWHEEL, outside, &menu), "scrolling outside keeps the menu\n");
    ok(LiberMenu_DismissesOn(WM_LBUTTONDOWN, inside, NULL), "without a menu rectangle every click dismisses\n");
}

static void TestStartDefaults()
{
    WCHAR path[MAX_PATH], expected[MAX_PATH];
    LPCWSTR name = NULL;

    ok(LiberStart_DefaultPinRank(L"C:\\Windows\\explorer.exe") == 0, "File Explorer leads the default pins\n");
    ok(LiberStart_DefaultPinRank(L"C:\\Windows\\ImmersiveControlPanel\\SystemSettings.exe") == 1,
       "Settings follows, case-insensitively\n");
    ok(LiberStart_DefaultPinRank(L"control.exe") == 2, "Control Panel is third\n");
    ok(LiberStart_DefaultPinRank(L"C:\\Windows\\system32\\notepad.exe") <
           LiberStart_DefaultPinRank(L"C:\\Windows\\system32\\calc.exe"),
       "the default pins keep their order\n");
    ok(LiberStart_DefaultPinRank(L"C:\\Windows\\system32\\sol.exe") == UINT_MAX, "a game is not a default pin\n");
    ok(LiberStart_DefaultPinRank(L"C:\\Windows\\system32\\notepad.exe.lnk") == UINT_MAX,
       "only the exact file name counts\n");
    ok(LiberStart_DefaultPinRank(L"") == UINT_MAX && LiberStart_DefaultPinRank(NULL) == UINT_MAX,
       "no target, no rank\n");

    ok(LiberStart_SystemApp(0, path, _countof(path), &name) && name && !lstrcmpW(name, L"Settings"),
       "Settings is a built-in Start entry\n");
    ExpandEnvironmentStringsW(L"%SystemRoot%\\ImmersiveControlPanel\\systemsettings.exe", expected, _countof(expected));
    ok(!lstrcmpiW(path, expected), "Settings path %S\n", path);
    ok(LiberStart_SystemApp(1, path, _countof(path), &name) && name && !lstrcmpW(name, L"Control Panel"),
       "Control Panel is a built-in Start entry\n");
    ExpandEnvironmentStringsW(L"%SystemRoot%\\system32\\control.exe", expected, _countof(expected));
    ok(!lstrcmpiW(path, expected), "Control Panel path %S\n", path);
    ok(GetFileAttributesW(path) != INVALID_FILE_ATTRIBUTES, "control.exe exists where the entry points\n");
    ok(!LiberStart_SystemApp(2, path, _countof(path), &name), "two built-in entries\n");
    ok(LiberStart_SystemApp(1, path, 4, &name) && path[0] == 0, "a short buffer yields an empty path\n");
    ok(!LiberStart_SystemApp(0, NULL, 0, NULL), "no buffer is rejected\n");
}

static void TestPillWidth()
{
    static const LPCWSTR Labels[] = { L"More", L"All", L"Back", L"WWWW" };

    if (!LiberUI::Startup())
    {
        skip("DirectWrite is not available\n");
        return;
    }
    for (UINT i = 0; i < _countof(Labels); i++)
    {
        CLiberPillButton pill(Labels[i]);
        SIZE available = { 600, 24 };
        SIZE size = pill.Measure(available);
        LONG text = LiberUI::MeasureText(Labels[i], LIBER_TYPE_CAPTION, 0.0f).cx;
        ok(text > 0, "%S measures a width\n", Labels[i]);
        ok(size.cx - 38 >= text, "%S fits its pill: %ld text in %ld\n", Labels[i], text, size.cx - 38);
        ok(size.cx - 38 <= text + 2, "%S pill hugs its text: %ld for %ld\n", Labels[i], size.cx - 38, text);
        ok(size.cy == 24, "%S pill is 24 high\n", Labels[i]);
    }
    ok(LiberUI::MeasureText(L"WWWW", LIBER_TYPE_CAPTION, 0.0f).cx >
           LiberUI::MeasureText(L"iiii", LIBER_TYPE_CAPTION, 0.0f).cx,
       "measurement follows the glyphs, not the character count\n");
    CLiberPillButton narrow(L"More");
    SIZE tight = { 30, 24 };
    ok(narrow.Measure(tight).cx == 30, "a pill never exceeds the space it is given\n");
    LiberUI::Shutdown();
}

static void TestLumaGrid(void)
{
    static DWORD pixels[128 * 72];
    static float grid[LIBER_LUMA_GRID_W * LIBER_LUMA_GRID_H];
    SIZE image = { 128, 72 };
    RECT left = { 0, 0, 64, 72 }, right = { 64, 0, 128, 72 }, all = { 0, 0, 128, 72 }, empty = { 10, 10, 10, 20 };
    double luma = -1.0;

    for (UINT y = 0; y < 72; y++)
        for (UINT x = 0; x < 128; x++)
            pixels[y * 128 + x] = x < 64 ? 0xFF000000 : 0xFFFFFFFF;
    LiberShell_BuildLumaGrid(pixels, 128, 72, grid);
    ok(grid[0] == 0.0f && grid[LIBER_LUMA_GRID_W - 1] > 0.99f, "the grid follows the image: %f %f\n", grid[0],
       grid[LIBER_LUMA_GRID_W - 1]);
    ok(LiberShell_GridLuma(grid, image, &left, &luma) && luma < 0.01, "the black half reads dark: %f\n", luma);
    ok(LiberShell_GridLuma(grid, image, &right, &luma) && luma > 0.99, "the white half reads bright: %f\n", luma);
    ok(LiberShell_GridLuma(grid, image, &all, &luma) && fabs(luma - 0.5) < 0.01, "the whole image is grey: %f\n",
       luma);
    ok(!LiberShell_GridLuma(grid, image, &empty, &luma), "an empty area has no luminance\n");
    SIZE none = { 0, 0 };
    ok(!LiberShell_GridLuma(grid, none, &all, &luma), "an image without size has no luminance\n");

    LiberShell_BuildLumaGrid(pixels, 3, 2, grid);
    ok(grid[LIBER_LUMA_GRID_W * LIBER_LUMA_GRID_H - 1] >= 0.0f, "a tiny image still fills every cell\n");
}

static void TestBackdropLuma(void)
{
    static float grid[LIBER_LUMA_GRID_W * LIBER_LUMA_GRID_H];
    SIZE image = { 1280, 720 }, screen = { 1280, 720 };
    RECT surface = { 100, 100, 300, 200 }, empty = { 5, 5, 5, 5 };
    RECT cover = { 0, 0, 1280, 720 }, half = { 0, 0, 200, 720 };
    double luma = -1.0;

    for (UINT i = 0; i < _countof(grid); i++)
        grid[i] = 0.8f;
    ok(LiberShell_BackdropLumaAt(grid, image, LIBER_WALLPAPER_FILL, screen, &surface, NULL, 0, 0.1, 0.0, &luma) &&
           fabs(luma - 0.8) < 1e-6,
       "the wallpaper alone gives its luminance: %f\n", luma);
    ok(LiberShell_BackdropLumaAt(grid, image, LIBER_WALLPAPER_FILL, screen, &surface, &cover, 1, 0.1, 0.0, &luma) &&
           fabs(luma - 0.1) < 1e-6,
       "a window covering the surface gives the window's luminance: %f\n", luma);
    ok(LiberShell_BackdropLumaAt(grid, image, LIBER_WALLPAPER_FILL, screen, &surface, &half, 1, 0.0, 0.0, &luma) &&
           fabs(luma - 0.4) < 1e-6,
       "a window over half the surface averages both: %f\n", luma);
    ok(LiberShell_BackdropLumaAt(grid, image, LIBER_WALLPAPER_FILL, screen, &surface, NULL, 0, 0.1, 0.5, &luma) &&
           fabs(luma - 0.4) < 1e-6,
       "the dark-mode dim darkens only the wallpaper: %f\n", luma);
    ok(LiberShell_BackdropLumaAt(grid, image, LIBER_WALLPAPER_FILL, screen, &surface, &cover, 1, 0.3, 0.5, &luma) &&
           fabs(luma - 0.3) < 1e-6,
       "windows are not dimmed: %f\n", luma);
    ok(!LiberShell_BackdropLumaAt(grid, image, LIBER_WALLPAPER_FILL, screen, &empty, NULL, 0, 0.1, 0.0, &luma),
       "an empty surface has no backdrop\n");
    ok(!LiberShell_BackdropLumaAt(NULL, image, LIBER_WALLPAPER_FILL, screen, &surface, NULL, 0, 0.1, 0.0, &luma),
       "no wallpaper and no window gives no backdrop\n");
    ok(LiberShell_BackdropLumaAt(NULL, image, LIBER_WALLPAPER_FILL, screen, &surface, &cover, 1, 0.2, 0.0, &luma) &&
           fabs(luma - 0.2) < 1e-6,
       "windows alone still give a backdrop: %f\n", luma);
}

static void TestAccentChoice(void)
{
    LiberUI::PaletteResult palette;
    LiberUI::Accent accent, azure = LiberUI::AccentPreset(LIBER_ACCENT_AZURE);

    accent = LiberShell_ResolveAccent(LIBER_ACCENT_AMBER, NULL);
    ok(accent.H == LiberUI::AccentPreset(LIBER_ACCENT_AMBER).H, "a preset is used as chosen: %f\n", accent.H);
    ZeroMemory(&palette, sizeof(palette));
    palette.Count = 1;
    palette.Candidates[0].AccentColor = LiberUI::AccentFromWallpaper(150.0, 0.12);
    accent = LiberShell_ResolveAccent(LIBER_ACCENT_FOLLOW, &palette);
    ok(accent.H == 150.0 && accent.C == palette.Candidates[0].AccentColor.C,
       "following the wallpaper uses its first candidate: %f %f\n", accent.H, accent.C);
    accent = LiberShell_ResolveAccent(LIBER_ACCENT_TEAL, &palette);
    ok(accent.H == LiberUI::AccentPreset(LIBER_ACCENT_TEAL).H, "a preset wins over the wallpaper\n");
    palette.Count = 0;
    accent = LiberShell_ResolveAccent(LIBER_ACCENT_FOLLOW, &palette);
    ok(accent.H == azure.H && accent.C == azure.C, "a wallpaper without colors falls back to azure\n");
    accent = LiberShell_ResolveAccent(LIBER_ACCENT_FOLLOW, NULL);
    ok(accent.H == azure.H, "no wallpaper falls back to azure\n");
    accent = LiberShell_ResolveAccent(LIBER_ACCENT_PRESET_COUNT, NULL);
    ok(accent.H == azure.H, "an unknown choice is treated as following a missing wallpaper\n");

    HKEY key;
    DWORD saved = 0, size = sizeof(saved);
    BOOL had = RegGetValueW(HKEY_CURRENT_USER, L"Software\\LiberNT\\Shell\\Design", L"Accent", RRF_RT_REG_DWORD, NULL,
                            &saved, &size) == ERROR_SUCCESS;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, L"Software\\LiberNT\\Shell\\Design", 0, NULL, 0, KEY_SET_VALUE, NULL, &key,
                        NULL) != ERROR_SUCCESS)
    {
        skip("cannot write the Design key\n");
        return;
    }
    RegDeleteValueW(key, L"Accent");
    ok(LiberExperience_Accent() == LIBER_ACCENT_FOLLOW, "the accent follows the wallpaper by default\n");
    DWORD value = LIBER_ACCENT_GREEN;
    RegSetValueExW(key, L"Accent", 0, REG_DWORD, (const BYTE *)&value, sizeof(value));
    ok(LiberExperience_Accent() == LIBER_ACCENT_GREEN, "a stored preset is read back\n");
    if (had)
        RegSetValueExW(key, L"Accent", 0, REG_DWORD, (const BYTE *)&saved, sizeof(saved));
    else
        RegDeleteValueW(key, L"Accent");
    RegCloseKey(key);
}

START_TEST(Dock)
{
    TestLumaGrid();
    TestAccentChoice();
    TestBackdropLuma();
    TestPillWidth();
    TestStartDefaults();
    TestMenuDismiss();
    TestCellRect();
    TestWallpaperCache();
    TestStartSearch();
    TestDesignValues();
    TestLayout();
    TestWorkArea();
    TestFlyoutOrigin();
    TestRecentHelpers();
    TestCalendarGrid();
    TestWallpaperMapping();
    TestMenuOrigin();
    TestStartPins();
    TestTaskWindows();
    TestExperience();
}
