/*
 * PROJECT:     LiberNT Explorer - Unit-tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test the Liber experience switch, dock placement and task window rules
 * COPYRIGHT:   Copyright 2026 Eitan Hadar <eitanhadar4@gmail.com>
 */

#include <apitest.h>

#include <windows.h>
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

    LiberDock_Layout(&monitor, 96, 5, 112, &layout);
    CheckRect(layout.Dock, (1920 - 252) / 2, 1080 - 8 - 60, (1920 - 252) / 2 + 252, 1080 - 8, "dock at 96 DPI");
    CheckRect(layout.Tray, 1920 - 8 - 112, 1012 + 6, 1920 - 8, 1012 + 6 + 48, "tray at 96 DPI");
    ok((layout.Tray.top + layout.Tray.bottom) == (layout.Dock.top + layout.Dock.bottom), "tray sits on the dock's center line\n");
    ok(LIBER_TASKBAR_DOCK - (1080 - layout.Dock.top) == 8, "dock fits the reserved 76 px\n");

    RECT second = { 1920, 0, 3840, 1080 };
    LiberDock_Layout(&second, 144, 1, 112, &layout);
    CheckRect(layout.Dock, 1920 + (1920 - 90) / 2, 1080 - 12 - 90, 1920 + (1920 - 90) / 2 + 90, 1080 - 12,
              "one cell at 144 DPI on a second monitor");
    CheckRect(layout.Tray, 3840 - 12 - 168, 978 + 9, 3840 - 12, 978 + 9 + 72, "tray at 144 DPI");
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

static void TestPlaceAbove()
{
    HDC screen = GetDC(NULL);
    UINT dpi = screen ? (UINT)GetDeviceCaps(screen, LOGPIXELSX) : 96;
    MONITORINFO mi = { sizeof(mi) };
    POINT pt;

    if (screen)
        ReleaseDC(NULL, screen);
    GetMonitorInfoW(MonitorFromPoint(POINT(), MONITOR_DEFAULTTOPRIMARY), &mi);

    RECT anchor = { mi.rcMonitor.right - 200, mi.rcMonitor.bottom - 60, mi.rcMonitor.right - 8, mi.rcMonitor.bottom - 12 };
    LiberShell_PlaceAbove(&anchor, 372, 300, &pt);
    ok(pt.x == anchor.right - 372, "right aligned to the anchor: %ld\n", pt.x);
    ok(pt.y == anchor.top - LiberUI::ScaleToDpi(LIBER_FLYOUT_GAP, dpi) - 300, "above the anchor with the gap: %ld\n", pt.y);

    RECT left = { mi.rcMonitor.left, mi.rcMonitor.bottom - 60, mi.rcMonitor.left + 50, mi.rcMonitor.bottom - 12 };
    LiberShell_PlaceAbove(&left, 372, 300, &pt);
    ok(pt.x == mi.rcMonitor.left, "kept on screen on the left: %ld\n", pt.x);

    RECT high = { mi.rcMonitor.right - 200, mi.rcMonitor.top + 10, mi.rcMonitor.right - 8, mi.rcMonitor.top + 50 };
    LiberShell_PlaceAbove(&high, 372, 300, &pt);
    ok(pt.y == mi.rcMonitor.top, "kept on screen at the top: %ld\n", pt.y);
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

START_TEST(Dock)
{
    TestStartSearch();
    TestDesignValues();
    TestLayout();
    TestPlaceAbove();
    TestTaskWindows();
    TestExperience();
}
