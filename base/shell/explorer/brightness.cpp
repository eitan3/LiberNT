/*
 * PROJECT:     LiberNT Explorer
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Display brightness through the LCD device, shared by the brightness flyouts
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <windows.h>
#include <winioctl.h>
#include <ntddvdeo.h>

#include "brightness.h"

volatile LONG g_TfyBrightnessSelfTick;

HANDLE
TfyOpenLcd(VOID)
{
    return CreateFileW(L"\\\\.\\LCD", GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                       OPEN_EXISTING, 0, NULL);
}

BOOL
TfyBrightnessQuery(int *pnPercent)
{
    DISPLAY_BRIGHTNESS Display;
    HANDLE hLcd = TfyOpenLcd();
    DWORD cb = 0;
    BOOL bRet;

    if (hLcd == INVALID_HANDLE_VALUE)
        return FALSE;
    bRet = DeviceIoControl(hLcd, IOCTL_VIDEO_QUERY_DISPLAY_BRIGHTNESS, NULL, 0, &Display, sizeof(Display), &cb, NULL) &&
           cb >= sizeof(Display);
    CloseHandle(hLcd);
    if (bRet)
        *pnPercent = (Display.ucDisplayPolicy & DISPLAYPOLICY_DC) ? Display.ucDCBrightness : Display.ucACBrightness;
    return bRet;
}

BOOL
TfyBrightnessSet(int nPercent)
{
    DISPLAY_BRIGHTNESS Display;
    HANDLE hLcd = TfyOpenLcd();
    DWORD cb = 0;
    BOOL bRet = FALSE;

    if (hLcd == INVALID_HANDLE_VALUE)
        return FALSE;
    if (DeviceIoControl(hLcd, IOCTL_VIDEO_QUERY_DISPLAY_BRIGHTNESS, NULL, 0, &Display, sizeof(Display), &cb, NULL) &&
        cb >= sizeof(Display))
    {
        nPercent = max(0, min(nPercent, 100));
        if (Display.ucDisplayPolicy & DISPLAYPOLICY_DC)
        {
            Display.ucDisplayPolicy = DISPLAYPOLICY_DC;
            Display.ucDCBrightness = (UCHAR)nPercent;
        }
        else
        {
            Display.ucDisplayPolicy = DISPLAYPOLICY_AC;
            Display.ucACBrightness = (UCHAR)nPercent;
        }
        InterlockedExchange(&g_TfyBrightnessSelfTick, (LONG)GetTickCount());
        bRet = DeviceIoControl(hLcd, IOCTL_VIDEO_SET_DISPLAY_BRIGHTNESS, &Display, sizeof(Display), NULL, 0, &cb, NULL);
    }
    CloseHandle(hLcd);
    return bRet;
}
