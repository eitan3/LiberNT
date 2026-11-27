/*
 * PROJECT:     LiberNT Explorer - Unit-tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test which notification icons the tray overflow lists
 * COPYRIGHT:   Copyright 2026 Eitan Hadar <eitanhadar4@gmail.com>
 */

#include <apitest.h>

#include <windows.h>
#include <shellapi.h>

#include "trayflyouts.h"

static void TestFlyoutPosition()
{
    RECT monitor = { 0, 0, 1920, 1080 }, anchor = { 1700, 1018, 1912, 1066 }, empty = { 0, 0, 0, 0 };
    RECT bottomBar = { 0, 1032, 1920, 1080 }, topBar = { 0, 0, 1920, 48 };
    SIZE size = { 300, 400 };
    POINT pt;

    TrayFlyout_Position(&empty, &anchor, &monitor, size, FALSE, 12, &pt);
    ok(pt.x == 1920 - 12 - 300 && pt.y == 1018 - 12 - 400, "an invisible owner places above the anchor: %ld,%ld\n",
       pt.x, pt.y);
    TrayFlyout_Position(NULL, &anchor, &monitor, size, TRUE, 12, &pt);
    ok(pt.x == 1920 - 12 - 300 && pt.y == 606, "a centered flyout is kept on the monitor: %ld,%ld\n", pt.x, pt.y);
    TrayFlyout_Position(&bottomBar, &anchor, &monitor, size, FALSE, 12, &pt);
    ok(pt.y == 1032 - 12 - 400, "a bottom taskbar places above the bar: %ld\n", pt.y);
    TrayFlyout_Position(&topBar, &anchor, &monitor, size, FALSE, 12, &pt);
    ok(pt.y == 48 + 12, "a top taskbar places below the bar: %ld\n", pt.y);
}

START_TEST(Tray)
{
    TestFlyoutPosition();
    ok(TrayIcon_Listed(0, FALSE, FALSE), "an overflowed icon is listed\n");
    ok(!TrayIcon_Listed(0, TRUE, FALSE), "a promoted icon stays on the taskbar\n");
    ok(TrayIcon_Listed(0, TRUE, TRUE), "the Liber overflow lists promoted icons too\n");
    ok(TrayIcon_Listed(0, FALSE, TRUE), "and overflowed ones\n");
    ok(!TrayIcon_Listed(NIS_HIDDEN, FALSE, TRUE), "a hidden icon is never listed\n");
    ok(!TrayIcon_Listed(NIS_HIDDEN, TRUE, FALSE), "a hidden promoted icon is not listed\n");
    ok(TrayIcon_Listed(NIS_SHAREDICON, TRUE, TRUE), "other state bits do not matter\n");
}
