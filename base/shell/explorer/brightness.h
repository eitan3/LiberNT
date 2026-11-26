/*
 * PROJECT:     LiberNT Explorer
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Display brightness through the LCD device, shared by the brightness flyouts
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

extern volatile LONG g_TfyBrightnessSelfTick;

HANDLE TfyOpenLcd(VOID);
BOOL TfyBrightnessQuery(int *pnPercent);
BOOL TfyBrightnessSet(int nPercent);
