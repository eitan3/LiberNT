/*
 * PROJECT:     LiberNT Shell Frame
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     Raw SVG source of the embedded Fluent UI System Icons
 * COPYRIGHT:   Copyright 2026 Eitan Hadar <eitanhadar4@gmail.com>
 */

#include <windows.h>

#include "iconsource.h"

namespace ShellFrame
{

struct ICON_SOURCE
{
    LPCWSTR Name;
    const char *Svg;
};

extern const ICON_SOURCE IconSources[];
extern const unsigned IconSourceCount;

const char *IconSvg(LPCWSTR name)
{
    if (!name)
        return NULL;
    for (unsigned i = 0; i < IconSourceCount; i++)
    {
        if (!lstrcmpW(IconSources[i].Name, name))
            return IconSources[i].Svg;
    }
    return NULL;
}

}
