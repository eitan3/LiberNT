/*
 * PROJECT:     LiberNT Liber UI
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     CPU coverage of glyphs, icon paths and app icons uploaded as Direct3D textures
 * COPYRIGHT:   Copyright 2026 Eitan Hadar <eitanhadar4@gmail.com>
 */

#pragma once

namespace LiberUI
{

SIZE MeasureTextDip(FontSet *fonts, LPCWSTR text, LIBER_TYPE style, float maxWidth);
BOOL RasterTextCoverage(FontSet *fonts, LPCWSTR text, LIBER_TYPE style, UINT flags, const RectF &local, float scale,
                        BYTE *coverage, UINT width, UINT height);
BOOL RasterSvgCoverage(const char *svg, const RectF &local, float scale, BYTE *coverage, UINT width, UINT height);
BOOL RasterIconImage(HICON icon, const RectF &local, float scale, DWORD *pixels, UINT width, UINT height);

}
