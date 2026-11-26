/*
 * PROJECT:     LiberNT Liber UI
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Extract accent color candidates from a wallpaper
 * COPYRIGHT:   Copyright 2026 Eitan Hadar <eitanhadar4@gmail.com>
 */

#include "precomp.h"

namespace LiberUI
{

static const double Pi = 3.14159265358979323846;

struct HueBucket
{
    UINT Count;
    double Chroma;
    double X;
    double Y;
};

struct BucketScore
{
    double Hue;
    double Chroma;
    double Score;
};

static DWORD SampleBox(const DWORD *pixels, UINT width, UINT height, UINT stride, UINT x0, UINT y0, UINT x1, UINT y1)
{
    double r = 0.0, g = 0.0, b = 0.0;
    UINT count = 0;

    LUI_ASSERT(x0 < x1 && x1 <= width && y0 < y1 && y1 <= height);
    UNREFERENCED_PARAMETER(width);
    UNREFERENCED_PARAMETER(height);

    for (UINT y = y0; y < y1; y++)
    {
        const DWORD *row = (const DWORD *)((const BYTE *)pixels + (SIZE_T)y * stride);
        for (UINT x = x0; x < x1; x++)
        {
            r += ArgbR(row[x]);
            g += ArgbG(row[x]);
            b += ArgbB(row[x]);
            count++;
        }
    }
    LUI_ASSERT(count != 0);
    return MakeArgb(0xFF, (BYTE)(r / count + 0.5), (BYTE)(g / count + 0.5), (BYTE)(b / count + 0.5));
}

BOOL ExtractPalette(const DWORD *pixels, UINT width, UINT height, UINT stride, PaletteResult *result)
{
    HueBucket buckets[LIBER_PALETTE_BUCKETS];
    BucketScore scores[LIBER_PALETTE_BUCKETS];
    UINT sampleWidth, sampleHeight, scored = 0;
    double lumaSum = 0.0, meanX = 0.0, meanY = 0.0;

    if (!pixels || !result || width == 0 || height == 0 || stride < width * sizeof(DWORD))
        return FALSE;

    ZeroMemory(result, sizeof(*result));
    ZeroMemory(buckets, sizeof(buckets));

    sampleWidth = width < LIBER_PALETTE_SAMPLE_WIDTH ? width : LIBER_PALETTE_SAMPLE_WIDTH;
    sampleHeight = (UINT)((double)sampleWidth * height / width + 0.5);
    if (sampleHeight == 0)
        sampleHeight = 1;
    if (sampleHeight > height)
        sampleHeight = height;

    for (UINT sy = 0; sy < sampleHeight; sy++)
    {
        UINT y0 = (UINT)((ULONGLONG)sy * height / sampleHeight);
        UINT y1 = (UINT)((ULONGLONG)(sy + 1) * height / sampleHeight);
        if (y1 <= y0)
            y1 = y0 + 1;

        for (UINT sx = 0; sx < sampleWidth; sx++)
        {
            UINT x0 = (UINT)((ULONGLONG)sx * width / sampleWidth);
            UINT x1 = (UINT)((ULONGLONG)(sx + 1) * width / sampleWidth);
            if (x1 <= x0)
                x1 = x0 + 1;

            DWORD pixel = SampleBox(pixels, width, height, stride, x0, y0, x1, y1);
            Oklch lch = ArgbToOklch(pixel);
            double angle = lch.H * Pi / 180.0;

            lumaSum += Luma(pixel);
            meanX += cos(angle) * lch.C;
            meanY += sin(angle) * lch.C;
            if (lch.C < LIBER_PALETTE_MIN_CHROMA || lch.L < LIBER_PALETTE_MIN_L || lch.L > LIBER_PALETTE_MAX_L)
                continue;

            UINT index = (UINT)(lch.H / (360.0 / LIBER_PALETTE_BUCKETS)) % LIBER_PALETTE_BUCKETS;
            buckets[index].Count++;
            buckets[index].Chroma += lch.C;
            buckets[index].X += cos(angle) * lch.C;
            buckets[index].Y += sin(angle) * lch.C;
        }
    }

    double total = (double)sampleWidth * sampleHeight;
    result->AverageLuma = lumaSum / total;

    for (UINT i = 0; i < LIBER_PALETTE_BUCKETS; i++)
    {
        double share = buckets[i].Count / total;
        if (share <= LIBER_PALETTE_MIN_SHARE)
            continue;

        BucketScore score;
        score.Hue = NormalizeHue(atan2(buckets[i].Y, buckets[i].X) * 180.0 / Pi);
        score.Chroma = buckets[i].Chroma / buckets[i].Count;
        score.Score = pow(share, LIBER_PALETTE_SCORE_EXPONENT) * score.Chroma;

        UINT at = scored++;
        while (at > 0 && scores[at - 1].Score < score.Score)
        {
            scores[at] = scores[at - 1];
            at--;
        }
        scores[at] = score;
    }

    for (UINT i = 0; i < scored && result->Count < LIBER_PALETTE_MAX_CANDIDATES; i++)
    {
        BOOL separate = TRUE;

        for (UINT j = 0; j < result->Count; j++)
        {
            if (HueDistance(scores[i].Hue, result->Candidates[j].AccentColor.H) < LIBER_PALETTE_MIN_HUE_GAP)
            {
                separate = FALSE;
                break;
            }
        }
        if (!separate)
            continue;

        Accent accent = AccentFromWallpaper(scores[i].Hue, scores[i].Chroma);
        accent.H = NormalizeHue(floor(accent.H + 0.5));
        accent.C = floor(accent.C * 1000.0 + 0.5) / 1000.0;
        result->Candidates[result->Count].AccentColor = accent;
        result->Candidates[result->Count].Score = scores[i].Score;
        result->Count++;
    }

    if (result->Count == 0)
    {
        Accent accent;
        double length = sqrt(meanX * meanX + meanY * meanY) / total;

        accent.H = length < 1e-4 ? LIBER_ACCENT_FALLBACK_HUE : floor(NormalizeHue(atan2(meanY, meanX) * 180.0 / Pi) + 0.5);
        accent.H = NormalizeHue(accent.H);
        accent.C = LIBER_ACCENT_MONOCHROME_C;
        accent.L = LIBER_ACCENT_MONOCHROME_L;
        result->Candidates[0].AccentColor = accent;
        result->Candidates[0].Score = 0.0;
        result->Count = 1;
    }

    LUI_ASSERT(result->Count >= 1 && result->Count <= LIBER_PALETTE_MAX_CANDIDATES);
    return TRUE;
}

}
