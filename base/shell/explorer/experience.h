/*
 * PROJECT:     LiberNT Explorer
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Choose between the Classic and the Liber desktop experience
 * COPYRIGHT:   Copyright 2026 Eitan Hadar <eitanhadar4@gmail.com>
 */

#pragma once

#define LIBER_DESIGN_KEY        L"Software\\LiberNT\\Shell\\Design"
#define LIBER_EXPERIENCE_VALUE  L"Experience"
#define LIBER_EXPERIENCE_CLASSIC 0
#define LIBER_EXPERIENCE_LIBER   1
#define LIBER_ACCENT_FOLLOW      0xFFFFFFFF

DWORD LiberExperience_Get(VOID);
BOOL LiberExperience_IsLiber(VOID);
BOOL LiberExperience_IsDark(VOID);
DWORD LiberExperience_SurfaceTint(VOID);
BOOL LiberExperience_WidgetsEnabled(VOID);
DWORD LiberExperience_Accent(VOID);
DWORD LiberExperience_TaskbarStyle(VOID);
BOOL LiberExperience_TaskbarLeft(VOID);
VOID LiberExperience_SetDimShade(COLORREF shade);
BOOL LiberExperience_UiFont(LPWSTR pszFamily, DWORD cchFamily);
