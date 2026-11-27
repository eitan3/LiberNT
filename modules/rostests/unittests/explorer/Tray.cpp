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

START_TEST(Tray)
{
    ok(TrayIcon_Listed(0, FALSE, FALSE), "an overflowed icon is listed\n");
    ok(!TrayIcon_Listed(0, TRUE, FALSE), "a promoted icon stays on the taskbar\n");
    ok(TrayIcon_Listed(0, TRUE, TRUE), "the Liber overflow lists promoted icons too\n");
    ok(TrayIcon_Listed(0, FALSE, TRUE), "and overflowed ones\n");
    ok(!TrayIcon_Listed(NIS_HIDDEN, FALSE, TRUE), "a hidden icon is never listed\n");
    ok(!TrayIcon_Listed(NIS_HIDDEN, TRUE, FALSE), "a hidden promoted icon is not listed\n");
    ok(TrayIcon_Listed(NIS_SHAREDICON, TRUE, TRUE), "other state bits do not matter\n");
}
