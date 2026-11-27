/*
 * PROJECT:     LiberNT Liber UI
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     CPU coverage of icon paths and app icons uploaded as Direct3D textures
 * COPYRIGHT:   Copyright 2026 Eitan Hadar <eitanhadar4@gmail.com>
 */

#include "precomp.h"
#include "raster.h"

#include <stdlib.h>
#include <string.h>

namespace LiberUI
{

struct CoverageBuffer
{
    float *Accum;
    UINT Width;
    UINT Height;
};

static void AccumulateLine(CoverageBuffer &buffer, float x0, float y0, float x1, float y1)
{
    float dir = 1.0f;
    UINT stride = buffer.Width + 2;

    if (y0 == y1)
        return;
    if (y0 > y1)
    {
        float t;
        dir = -1.0f;
        t = x0; x0 = x1; x1 = t;
        t = y0; y0 = y1; y1 = t;
    }
    float dxdy = (x1 - x0) / (y1 - y0);
    float x = x0;
    if (y0 < 0.0f)
    {
        x -= y0 * dxdy;
        y0 = 0.0f;
    }
    if (y1 > (float)buffer.Height)
        y1 = (float)buffer.Height;
    if (y0 >= y1)
        return;

    for (int y = (int)floorf(y0); y < (int)ceilf(y1); y++)
    {
        float top = max((float)y, y0), bottom = min((float)y + 1.0f, y1);
        float dy = bottom - top;
        float xnext = x + dxdy * dy;
        float d = dy * dir;
        float a = min(max(min(x, xnext), 0.0f), (float)buffer.Width);
        float b = min(max(max(x, xnext), 0.0f), (float)buffer.Width);
        float *row = buffer.Accum + (SIZE_T)y * stride;
        int ai = (int)floorf(a), bi = (int)ceilf(b);

        if (bi <= ai + 1)
        {
            float mid = 0.5f * (a + b) - (float)ai;
            row[ai] += d - d * mid;
            row[ai + 1] += d * mid;
        }
        else
        {
            float s = 1.0f / (b - a);
            float af = a - (float)ai;
            float a0 = 0.5f * s * (1.0f - af) * (1.0f - af);
            float bf = b - (float)bi + 1.0f;
            float am = 0.5f * s * bf * bf;
            row[ai] += d * a0;
            if (bi == ai + 2)
            {
                row[ai + 1] += d * (1.0f - a0 - am);
            }
            else
            {
                float a1 = s * (1.5f - af);
                row[ai + 1] += d * (a1 - a0);
                for (int xi = ai + 2; xi < bi - 1; xi++)
                    row[xi] += d * s;
                float a2 = a1 + (float)(bi - ai - 3) * s;
                row[bi - 1] += d * (1.0f - a2 - am);
            }
            row[bi] += d * am;
        }
        x = xnext;
    }
}

struct PathCursor
{
    CoverageBuffer *Buffer;
    float Scale;
    float OffsetX;
    float OffsetY;
    float X, Y;
    float StartX, StartY;
};

static void PathLine(PathCursor &cursor, float x, float y)
{
    AccumulateLine(*cursor.Buffer, cursor.OffsetX + cursor.X * cursor.Scale, cursor.OffsetY + cursor.Y * cursor.Scale,
                   cursor.OffsetX + x * cursor.Scale, cursor.OffsetY + y * cursor.Scale);
    cursor.X = x;
    cursor.Y = y;
}

static void PathCubic(PathCursor &cursor, float x1, float y1, float x2, float y2, float x3, float y3)
{
    float x0 = cursor.X, y0 = cursor.Y;
    float length = (fabsf(x1 - x0) + fabsf(y1 - y0) + fabsf(x2 - x1) + fabsf(y2 - y1) + fabsf(x3 - x2) +
                    fabsf(y3 - y2)) * cursor.Scale;
    int steps = max(4, min(64, (int)(length / 2.0f)));

    for (int i = 1; i <= steps; i++)
    {
        float t = (float)i / steps, u = 1.0f - t;
        float x = u * u * u * x0 + 3 * u * u * t * x1 + 3 * u * t * t * x2 + t * t * t * x3;
        float y = u * u * u * y0 + 3 * u * u * t * y1 + 3 * u * t * t * y2 + t * t * t * y3;
        PathLine(cursor, x, y);
    }
}

static const char *SkipSeparators(const char *p)
{
    while (*p == ' ' || *p == ',' || *p == '\t' || *p == '\r' || *p == '\n')
        p++;
    return p;
}

static BOOL ReadNumber(const char **cursor, float *value)
{
    const char *p = SkipSeparators(*cursor);
    char *end;

    if (!((*p >= '0' && *p <= '9') || *p == '-' || *p == '+' || *p == '.'))
        return FALSE;
    *value = (float)strtod(p, &end);
    if (end == p)
        return FALSE;
    *cursor = end;
    return TRUE;
}

static void ClosePath(PathCursor &cursor)
{
    if (cursor.X != cursor.StartX || cursor.Y != cursor.StartY)
        PathLine(cursor, cursor.StartX, cursor.StartY);
}

static BOOL ParsePath(const char *p, PathCursor &cursor)
{
    char command = 0;
    BOOL open = FALSE;

    for (;;)
    {
        float a[6];

        p = SkipSeparators(p);
        if (!*p || *p == '"')
            break;
        if ((*p >= 'A' && *p <= 'Z') || (*p >= 'a' && *p <= 'z'))
            command = *p++;
        BOOL relative = command >= 'a';
        switch (command)
        {
            case 'M':
            case 'm':
                if (!ReadNumber(&p, &a[0]) || !ReadNumber(&p, &a[1]))
                    return FALSE;
                if (open)
                    ClosePath(cursor);
                if (relative)
                {
                    a[0] += cursor.X;
                    a[1] += cursor.Y;
                }
                cursor.X = cursor.StartX = a[0];
                cursor.Y = cursor.StartY = a[1];
                open = TRUE;
                command = relative ? 'l' : 'L';
                break;
            case 'L':
            case 'l':
                if (!ReadNumber(&p, &a[0]) || !ReadNumber(&p, &a[1]))
                    return FALSE;
                PathLine(cursor, a[0] + (relative ? cursor.X : 0), a[1] + (relative ? cursor.Y : 0));
                break;
            case 'H':
            case 'h':
                if (!ReadNumber(&p, &a[0]))
                    return FALSE;
                PathLine(cursor, a[0] + (relative ? cursor.X : 0), cursor.Y);
                break;
            case 'V':
            case 'v':
                if (!ReadNumber(&p, &a[0]))
                    return FALSE;
                PathLine(cursor, cursor.X, a[0] + (relative ? cursor.Y : 0));
                break;
            case 'C':
            case 'c':
                for (int i = 0; i < 6; i++)
                {
                    if (!ReadNumber(&p, &a[i]))
                        return FALSE;
                    if (relative)
                        a[i] += (i & 1) ? cursor.Y : cursor.X;
                }
                PathCubic(cursor, a[0], a[1], a[2], a[3], a[4], a[5]);
                break;
            case 'Z':
            case 'z':
                ClosePath(cursor);
                cursor.X = cursor.StartX;
                cursor.Y = cursor.StartY;
                open = FALSE;
                command = 0;
                break;
            default:
                return FALSE;
        }
    }
    if (open)
        ClosePath(cursor);
    return TRUE;
}

static float ViewBox(const char *svg)
{
    const char *p = strstr(svg, "viewBox=\"");
    float value[4];

    if (!p)
        return 0.0f;
    p += 9;
    for (int i = 0; i < 4; i++)
    {
        if (!ReadNumber(&p, &value[i]))
            return 0.0f;
    }
    return max(value[2], value[3]);
}

BOOL RasterSvgCoverage(const char *svg, const RectF &local, float scale, BYTE *coverage, UINT width, UINT height)
{
    float view = svg ? ViewBox(svg) : 0.0f;
    CoverageBuffer buffer;
    const char *p = svg;
    BOOL drawn = FALSE;

    LUI_ASSERT(coverage && width && height && scale > 0.0f);
    if (view <= 0.0f || !coverage || !width || !height)
        return FALSE;
    buffer.Width = width;
    buffer.Height = height;
    buffer.Accum = (float *)calloc((SIZE_T)(width + 2) * (height + 1), sizeof(float));
    if (!buffer.Accum)
        return FALSE;

    float box = min(local.Width, local.Height) * scale;
    PathCursor cursor;
    ZeroMemory(&cursor, sizeof(cursor));
    cursor.Buffer = &buffer;
    cursor.Scale = box / view;
    cursor.OffsetX = local.X * scale + (local.Width * scale - box) / 2.0f;
    cursor.OffsetY = local.Y * scale + (local.Height * scale - box) / 2.0f;
    while ((p = strstr(p, " d=\"")) != NULL)
    {
        p += 4;
        drawn |= ParsePath(p, cursor);
    }

    for (UINT y = 0; y < height; y++)
    {
        float sum = 0.0f;
        const float *row = buffer.Accum + (SIZE_T)y * (width + 2);
        for (UINT x = 0; x < width; x++)
        {
            sum += row[x];
            float value = fabsf(sum);
            coverage[(SIZE_T)y * width + x] = (BYTE)(min(value, 1.0f) * 255.0f + 0.5f);
        }
    }
    free(buffer.Accum);
    return drawn;
}

static BOOL IconBits(HICON icon, DWORD **bits, int *width, int *height)
{
    ICONINFO info;
    BITMAP bm;
    BOOL ok = FALSE;

    *bits = NULL;
    if (!icon || !GetIconInfo(icon, &info))
        return FALSE;
    if (info.hbmColor && GetObjectW(info.hbmColor, sizeof(bm), &bm) == sizeof(bm) && bm.bmWidth > 0 &&
        bm.bmHeight > 0 && bm.bmWidth <= 256 && bm.bmHeight <= 256)
    {
        int w = bm.bmWidth, h = bm.bmHeight;
        DWORD *pixels = (DWORD *)LocalAlloc(LPTR, (SIZE_T)w * h * sizeof(DWORD));
        BITMAPINFO bmi;
        HDC dc = CreateCompatibleDC(NULL);

        ZeroMemory(&bmi, sizeof(bmi));
        bmi.bmiHeader.biSize = sizeof(bmi.bmiHeader);
        bmi.bmiHeader.biWidth = w;
        bmi.bmiHeader.biHeight = -h;
        bmi.bmiHeader.biPlanes = 1;
        bmi.bmiHeader.biBitCount = 32;
        bmi.bmiHeader.biCompression = BI_RGB;
        if (pixels && dc && GetDIBits(dc, info.hbmColor, 0, h, pixels, &bmi, DIB_RGB_COLORS) == h)
        {
            BOOL hasAlpha = FALSE;
            for (int i = 0; i < w * h && !hasAlpha; i++)
                hasAlpha = (pixels[i] & 0xFF000000) != 0;
            if (!hasAlpha)
            {
                DWORD *mask = (DWORD *)LocalAlloc(LPTR, (SIZE_T)w * h * sizeof(DWORD));
                BOOL masked = mask && info.hbmMask &&
                              GetDIBits(dc, info.hbmMask, 0, h, mask, &bmi, DIB_RGB_COLORS) == h;
                for (int i = 0; i < w * h; i++)
                    pixels[i] |= (masked && (mask[i] & 0x00FFFFFF)) ? 0 : 0xFF000000;
                if (mask)
                    LocalFree(mask);
            }
            *bits = pixels;
            *width = w;
            *height = h;
            pixels = NULL;
            ok = TRUE;
        }
        if (pixels)
            LocalFree(pixels);
        if (dc)
            DeleteDC(dc);
    }
    if (info.hbmColor)
        DeleteObject(info.hbmColor);
    if (info.hbmMask)
        DeleteObject(info.hbmMask);
    return ok;
}

static void SampleIcon(const DWORD *bits, int width, int height, float sx0, float sy0, float sx1, float sy1,
                       float out[4])
{
    float sum[4] = { 0, 0, 0, 0 }, weight = 0.0f;

    for (int y = max(0, (int)floorf(sy0)); y < min(height, (int)ceilf(sy1)); y++)
    {
        float wy = min((float)y + 1.0f, sy1) - max((float)y, sy0);
        for (int x = max(0, (int)floorf(sx0)); x < min(width, (int)ceilf(sx1)); x++)
        {
            float wx = min((float)x + 1.0f, sx1) - max((float)x, sx0);
            float w = wx * wy;
            DWORD p = bits[(SIZE_T)y * width + x];
            float a = (float)(p >> 24) / 255.0f;
            sum[0] += w * a;
            sum[1] += w * a * (float)((p >> 16) & 0xFF);
            sum[2] += w * a * (float)((p >> 8) & 0xFF);
            sum[3] += w * a * (float)(p & 0xFF);
            weight += w;
        }
    }
    for (int i = 0; i < 4; i++)
        out[i] = weight > 0.0f ? sum[i] / weight : 0.0f;
}

BOOL RasterIconImage(HICON icon, const RectF &local, float scale, DWORD *pixels, UINT width, UINT height)
{
    DWORD *bits;
    int iw, ih;

    LUI_ASSERT(pixels && width && height && scale > 0.0f);
    if (!pixels || !IconBits(icon, &bits, &iw, &ih))
        return FALSE;
    float left = local.X * scale, top = local.Y * scale, w = local.Width * scale, h = local.Height * scale;
    for (UINT y = 0; y < height; y++)
    {
        for (UINT x = 0; x < width; x++)
        {
            float u0 = ((float)x - left) / w * iw, u1 = ((float)x + 1.0f - left) / w * iw;
            float v0 = ((float)y - top) / h * ih, v1 = ((float)y + 1.0f - top) / h * ih;
            float c[4];
            if (u1 <= 0.0f || v1 <= 0.0f || u0 >= iw || v0 >= ih)
                continue;
            SampleIcon(bits, iw, ih, u0, v0, u1, v1, c);
            pixels[(SIZE_T)y * width + x] = ((DWORD)(c[0] * 255.0f + 0.5f) << 24) | ((DWORD)(c[1] + 0.5f) << 16) |
                                            ((DWORD)(c[2] + 0.5f) << 8) | (DWORD)(c[3] + 0.5f);
        }
    }
    LocalFree(bits);
    return TRUE;
}

}
