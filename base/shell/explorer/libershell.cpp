/*
 * PROJECT:     LiberNT Explorer
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Pieces shared by the Liber shell surfaces
 * COPYRIGHT:   Copyright 2026 Eitan Hadar <eitanhadar4@gmail.com>
 */

#include <windows.h>
#include <atlbase.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <strsafe.h>
#include <math.h>
#include <wincodec.h>

#include "experience.h"
#include "libershell.h"

#define PERSONALIZE_KEY L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize"

static BOOL g_LiberGlass = TRUE;

static BOOL ReadDwordDefault(HKEY root, LPCWSTR key, LPCWSTR name, DWORD fallback)
{
    DWORD value = 0, cb = sizeof(value);
    if (RegGetValueW(root, key, name, RRF_RT_REG_DWORD, NULL, &value, &cb) != ERROR_SUCCESS)
        return fallback != 0;
    return value != 0;
}

BOOL LiberShell_ReadGlassEnabled(VOID)
{
    return ReadDwordDefault(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
                            L"EnableTransparency", 1) &&
           ReadDwordDefault(HKEY_CURRENT_USER, L"Software\\ReactOS\\DWM", L"EnableBlur", 1) &&
           ReadDwordDefault(HKEY_CURRENT_USER, L"Software\\ReactOS\\DWM", L"EnableAcrylic", 1);
}

BOOL LiberShell_GlassEnabled(VOID)
{
    return g_LiberGlass;
}

VOID LiberShell_ApplyMaterial(LiberUI::Surface *pSurface, const LiberUI::ThemeTokens *pTokens,
                              LIBER_MATERIAL_KIND material, float radiusDip)
{
    if (!g_LiberGlass || !pTokens)
    {
        pSurface->SetMaterial(-1, 0, 0, 0.0f);
        return;
    }
    LiberShell_SampleBackdrop(pSurface, pTokens, material == LIBER_MATERIAL_THIN);
    LiberUI::GlassTint tint = pTokens->Material(material, pSurface->BackdropLuma(LiberUI::AnimationClock::Now()));
    pSurface->SetMaterial((int)material, LiberUI::OklchToArgb(tint.Color, 1.0), (BYTE)(tint.Alpha * 255.0 + 0.5),
                          radiusDip);
}

static LiberUI::Accent CurrentAccent(VOID);
static int g_TaskbarReserve = LIBER_TASKBAR_DOCK;

VOID LiberShell_SetTaskbarReserve(int reserveDip)
{
    ATLASSERT(reserveDip > 0);
    g_TaskbarReserve = reserveDip;
}

VOID LiberShell_BuildTokens(LiberUI::ThemeTokens *pTokens)
{
    g_LiberGlass = LiberShell_ReadGlassEnabled();
    LiberUI::ThemeParams params = { LiberExperience_IsDark() ? LiberUI::ThemeMode::Dark : LiberUI::ThemeMode::Light,
                                    CurrentAccent(),
                                    (LiberUI::SurfaceTint)LiberExperience_SurfaceTint() };
    pTokens->Build(params);
    if (pTokens->IsDark())
        LiberExperience_SetDimShade(LiberShell_DimShade(pTokens->Color(LIBER_COLOR_SURFACE_BASE)));
}

COLORREF LiberShell_DimShade(LiberUI::Argb surfaceBase)
{
    return RGB(LiberUI::ArgbR(surfaceBase), LiberUI::ArgbG(surfaceBase), LiberUI::ArgbB(surfaceBase));
}

VOID LiberShell_MapToWallpaper(const RECT *prcScreen, SIZE screen, SIZE image, UINT style, RECT *prcImage)
{
    double scaleX = 1.0, scaleY = 1.0, offsetX = 0.0, offsetY = 0.0;

    SetRectEmpty(prcImage);
    if (screen.cx <= 0 || screen.cy <= 0 || image.cx <= 0 || image.cy <= 0)
        return;
    if (style == LIBER_WALLPAPER_STRETCH)
    {
        scaleX = (double)screen.cx / image.cx;
        scaleY = (double)screen.cy / image.cy;
    }
    else if (style == LIBER_WALLPAPER_FILL || style == LIBER_WALLPAPER_FIT)
    {
        double sx = (double)screen.cx / image.cx, sy = (double)screen.cy / image.cy;
        scaleX = scaleY = style == LIBER_WALLPAPER_FILL ? max(sx, sy) : min(sx, sy);
    }
    offsetX = (screen.cx - image.cx * scaleX) / 2.0;
    offsetY = (screen.cy - image.cy * scaleY) / 2.0;
    prcImage->left = (LONG)floor((prcScreen->left - offsetX) / scaleX);
    prcImage->top = (LONG)floor((prcScreen->top - offsetY) / scaleY);
    prcImage->right = (LONG)ceil((prcScreen->right - offsetX) / scaleX);
    prcImage->bottom = (LONG)ceil((prcScreen->bottom - offsetY) / scaleY);
    RECT bounds = { 0, 0, image.cx, image.cy };
    if (!IntersectRect(prcImage, prcImage, &bounds))
        SetRectEmpty(prcImage);
}

static double LinearChannel(BYTE value)
{
    double c = value / 255.0;
    return c <= 0.04045 ? c / 12.92 : pow((c + 0.055) / 1.055, 2.4);
}

double LiberShell_Luminance(DWORD argb)
{
    return 0.2126 * LinearChannel((BYTE)(argb >> 16)) + 0.7152 * LinearChannel((BYTE)(argb >> 8)) +
           0.0722 * LinearChannel((BYTE)argb);
}

static BOOL LoadWallpaperPixels(LPCWSTR pszPath, DWORD **ppPixels, UINT *pWidth, UINT *pHeight)
{
    IWICImagingFactory *factory = NULL;
    IWICBitmapDecoder *decoder = NULL;
    IWICBitmapFrameDecode *frame = NULL;
    IWICFormatConverter *converter = NULL;
    DWORD *pixels = NULL;
    HRESULT hr;

    *ppPixels = NULL;
    hr = CoCreateInstance(CLSID_WICImagingFactory, NULL, CLSCTX_INPROC_SERVER, IID_IWICImagingFactory,
                          (void **)&factory);
    if (SUCCEEDED(hr))
        hr = factory->CreateDecoderFromFilename(pszPath, NULL, GENERIC_READ, WICDecodeMetadataCacheOnDemand, &decoder);
    if (SUCCEEDED(hr))
        hr = decoder->GetFrame(0, &frame);
    if (SUCCEEDED(hr))
        hr = factory->CreateFormatConverter(&converter);
    if (SUCCEEDED(hr))
        hr = converter->Initialize(frame, GUID_WICPixelFormat32bppBGRA, WICBitmapDitherTypeNone, NULL, 0.0,
                                   WICBitmapPaletteTypeCustom);
    if (SUCCEEDED(hr))
        hr = converter->GetSize(pWidth, pHeight);
    if (SUCCEEDED(hr) && (!*pWidth || !*pHeight || *pWidth > 16384 || *pHeight > 16384))
        hr = E_FAIL;
    if (SUCCEEDED(hr))
    {
        pixels = (DWORD *)LocalAlloc(LMEM_FIXED, (SIZE_T)*pWidth * *pHeight * sizeof(DWORD));
        hr = pixels ? converter->CopyPixels(NULL, *pWidth * sizeof(DWORD), *pWidth * *pHeight * sizeof(DWORD),
                                            (BYTE *)pixels)
                    : E_OUTOFMEMORY;
    }
    if (SUCCEEDED(hr))
        *ppPixels = pixels;
    else if (pixels)
        LocalFree(pixels);
    if (converter)
        converter->Release();
    if (frame)
        frame->Release();
    if (decoder)
        decoder->Release();
    if (factory)
        factory->Release();
    return SUCCEEDED(hr);
}

VOID LiberShell_BuildLumaGrid(const DWORD *pPixels, UINT width, UINT height, float *pGrid)
{
    ATLASSERT(pPixels && pGrid && width && height);
    for (UINT gy = 0; gy < LIBER_LUMA_GRID_H; gy++)
    {
        UINT y0 = gy * height / LIBER_LUMA_GRID_H, y1 = max(y0 + 1, (gy + 1) * height / LIBER_LUMA_GRID_H);
        for (UINT gx = 0; gx < LIBER_LUMA_GRID_W; gx++)
        {
            UINT x0 = gx * width / LIBER_LUMA_GRID_W, x1 = max(x0 + 1, (gx + 1) * width / LIBER_LUMA_GRID_W);
            UINT stepX = max(1u, (x1 - x0) / 8), stepY = max(1u, (y1 - y0) / 8), count = 0;
            double sum = 0.0;
            for (UINT y = y0; y < y1 && y < height; y += stepY)
            {
                for (UINT x = x0; x < x1 && x < width; x += stepX)
                {
                    sum += LiberShell_Luminance(pPixels[(SIZE_T)y * width + x]);
                    count++;
                }
            }
            pGrid[gy * LIBER_LUMA_GRID_W + gx] = count ? (float)(sum / count) : 0.0f;
        }
    }
}

BOOL LiberShell_GridLuma(const float *pGrid, SIZE image, const RECT *prcImage, double *pLuma)
{
    double sum = 0.0;
    UINT samples = 0;

    *pLuma = 0.0;
    if (!pGrid || image.cx <= 0 || image.cy <= 0 || !prcImage || IsRectEmpty(prcImage))
        return FALSE;
    for (UINT y = 0; y < 10; y++)
    {
        for (UINT x = 0; x < 10; x++)
        {
            LONG px = prcImage->left + (prcImage->right - prcImage->left) * (LONG)(x * 2 + 1) / 20;
            LONG py = prcImage->top + (prcImage->bottom - prcImage->top) * (LONG)(y * 2 + 1) / 20;
            if (px < 0 || py < 0 || px >= image.cx || py >= image.cy)
                continue;
            UINT gx = (UINT)((LONGLONG)px * LIBER_LUMA_GRID_W / image.cx);
            UINT gy = (UINT)((LONGLONG)py * LIBER_LUMA_GRID_H / image.cy);
            ATLASSERT(gx < LIBER_LUMA_GRID_W && gy < LIBER_LUMA_GRID_H);
            sum += pGrid[gy * LIBER_LUMA_GRID_W + gx];
            samples++;
        }
    }
    if (!samples)
        return FALSE;
    *pLuma = sum / samples;
    return TRUE;
}

static struct
{
    WCHAR Path[MAX_PATH];
    UINT Style;
    FILETIME Written;
    BOOL Loaded;
    BOOL Valid;
    SIZE Image;
    float Grid[LIBER_LUMA_GRID_H * LIBER_LUMA_GRID_W];
    LiberUI::PaletteResult Palette;
} g_Wallpaper;

static BOOL LoadWallpaper(UINT *pStyle)
{
    WCHAR szPath[MAX_PATH], szStyle[8];
    DWORD cb = sizeof(szStyle);
    WIN32_FILE_ATTRIBUTE_DATA info;
    DWORD *pixels = NULL;
    UINT width = 0, height = 0;

    *pStyle = LIBER_WALLPAPER_FILL;
    if (!SystemParametersInfoW(SPI_GETDESKWALLPAPER, _countof(szPath), szPath, 0) || !szPath[0])
        return FALSE;
    if (RegGetValueW(HKEY_CURRENT_USER, L"Control Panel\\Desktop", L"WallpaperStyle", RRF_RT_REG_SZ, NULL, szStyle,
                     &cb) == ERROR_SUCCESS)
    {
        *pStyle = (UINT)_wtoi(szStyle);
    }
    if (!GetFileAttributesExW(szPath, GetFileExInfoStandard, &info))
        return FALSE;
    if (g_Wallpaper.Loaded && !lstrcmpiW(g_Wallpaper.Path, szPath) &&
        !CompareFileTime(&g_Wallpaper.Written, &info.ftLastWriteTime))
    {
        return g_Wallpaper.Valid;
    }
    ZeroMemory(&g_Wallpaper, sizeof(g_Wallpaper));
    StringCchCopyW(g_Wallpaper.Path, _countof(g_Wallpaper.Path), szPath);
    g_Wallpaper.Written = info.ftLastWriteTime;
    g_Wallpaper.Loaded = TRUE;
    if (!LoadWallpaperPixels(szPath, &pixels, &width, &height))
        return FALSE;
    g_Wallpaper.Image.cx = (LONG)width;
    g_Wallpaper.Image.cy = (LONG)height;
    LiberShell_BuildLumaGrid(pixels, width, height, g_Wallpaper.Grid);
    if (!LiberUI::ExtractPalette(pixels, width, height, width * sizeof(DWORD), &g_Wallpaper.Palette))
        ZeroMemory(&g_Wallpaper.Palette, sizeof(g_Wallpaper.Palette));
    LocalFree(pixels);
    g_Wallpaper.Valid = TRUE;
    return TRUE;
}

BOOL LiberShell_WallpaperLuma(const RECT *prcScreen, double *pLuma)
{
    UINT style;
    RECT area;

    *pLuma = 0.0;
    if (!prcScreen || !LoadWallpaper(&style))
        return FALSE;
    SIZE screen = { GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN) };
    LiberShell_MapToWallpaper(prcScreen, screen, g_Wallpaper.Image, style, &area);
    return LiberShell_GridLuma(g_Wallpaper.Grid, g_Wallpaper.Image, &area, pLuma);
}

BOOL LiberShell_WallpaperPalette(LiberUI::PaletteResult *pPalette)
{
    UINT style;

    ZeroMemory(pPalette, sizeof(*pPalette));
    if (!LoadWallpaper(&style) || !g_Wallpaper.Palette.Count)
        return FALSE;
    *pPalette = g_Wallpaper.Palette;
    return TRUE;
}

LiberUI::Accent LiberShell_ResolveAccent(DWORD dwChoice, const LiberUI::PaletteResult *pPalette)
{
    if (dwChoice < LIBER_ACCENT_PRESET_COUNT)
        return LiberUI::AccentPreset((LIBER_ACCENT_PRESET_ID)dwChoice);
    if (pPalette && pPalette->Count)
        return pPalette->Candidates[0].AccentColor;
    return LiberUI::AccentPreset(LIBER_ACCENT_AZURE);
}

BOOL LiberShell_BackdropLumaAt(const float *pGrid, SIZE image, UINT style, SIZE screen, const RECT *prcSurface,
                               const RECT *pOccluders, UINT cOccluders, double occluderLuma, double dim,
                               double *pLuma)
{
    double sum = 0.0;
    UINT samples = 0;

    *pLuma = 0.0;
    ATLASSERT(dim >= 0.0 && dim <= 1.0);
    if (!prcSurface || IsRectEmpty(prcSurface))
        return FALSE;
    for (UINT y = 0; y < 10; y++)
    {
        for (UINT x = 0; x < 10; x++)
        {
            POINT pt = { prcSurface->left + (prcSurface->right - prcSurface->left) * (LONG)(x * 2 + 1) / 20,
                         prcSurface->top + (prcSurface->bottom - prcSurface->top) * (LONG)(y * 2 + 1) / 20 };
            BOOL covered = FALSE;
            for (UINT i = 0; i < cOccluders && !covered; i++)
                covered = PtInRect(&pOccluders[i], pt);
            if (covered)
            {
                sum += occluderLuma;
                samples++;
                continue;
            }
            RECT point = { pt.x, pt.y, pt.x + 1, pt.y + 1 }, area;
            double luma;
            LiberShell_MapToWallpaper(&point, screen, image, style, &area);
            if (pGrid && LiberShell_GridLuma(pGrid, image, &area, &luma))
            {
                sum += luma * (1.0 - dim);
                samples++;
            }
        }
    }
    if (!samples)
        return FALSE;
    *pLuma = sum / samples;
    return TRUE;
}

typedef struct _LIBER_OCCLUDERS
{
    RECT Rects[64];
    UINT Count;
} LIBER_OCCLUDERS;

static BOOL CALLBACK CollectOccluder(HWND hwnd, LPARAM lParam)
{
    LIBER_OCCLUDERS *occluders = (LIBER_OCCLUDERS *)lParam;
    WCHAR szClass[32];
    RECT rc;

    if (occluders->Count >= _countof(occluders->Rects))
        return FALSE;
    if (!IsWindowVisible(hwnd) || IsIconic(hwnd) || hwnd == GetShellWindow() || !GetWindowRect(hwnd, &rc) ||
        IsRectEmpty(&rc))
    {
        return TRUE;
    }
    if (GetClassNameW(hwnd, szClass, _countof(szClass)) &&
        (!lstrcmpW(szClass, L"LiberSurface") || !lstrcmpW(szClass, L"Progman") || !lstrcmpW(szClass, L"WorkerW")))
    {
        return TRUE;
    }
    occluders->Rects[occluders->Count++] = rc;
    return TRUE;
}

BOOL LiberShell_SampleBackdrop(LiberUI::Surface *pSurface, const LiberUI::ThemeTokens *pTokens, BOOL bDesktopLayer)
{
    LIBER_OCCLUDERS occluders;
    UINT style;
    RECT rc;
    double luma, dim = 0.0;

    if (!pSurface || !pTokens || !pSurface->Window() || !GetWindowRect(pSurface->Window(), &rc) || !LoadWallpaper(&style))
        return FALSE;
    occluders.Count = 0;
    if (!bDesktopLayer)
        EnumWindows(CollectOccluder, (LPARAM)&occluders);
    if (pTokens->IsDark())
        dim = LiberUI::WallpaperDim(g_Wallpaper.Palette.AverageLuma);
    SIZE screen = { GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN) };
    double occluderLuma = LiberShell_Luminance(pTokens->Color(LIBER_COLOR_SURFACE_PANE));
    if (!LiberShell_BackdropLumaAt(g_Wallpaper.Grid, g_Wallpaper.Image, style, screen, &rc, occluders.Rects,
                                   occluders.Count, occluderLuma, dim, &luma))
    {
        return FALSE;
    }
    pSurface->SetBackdropLuma(luma);
    return TRUE;
}

static LiberUI::Accent CurrentAccent(VOID)
{
    LiberUI::PaletteResult palette;
    DWORD choice = LiberExperience_Accent();

    if (choice < LIBER_ACCENT_PRESET_COUNT)
        return LiberShell_ResolveAccent(choice, NULL);
    return LiberShell_ResolveAccent(choice, LiberShell_WallpaperPalette(&palette) ? &palette : NULL);
}

VOID LiberShell_BuildWallpaperInkTokens(LiberUI::ThemeTokens *pTokens, const RECT *prcScreen)
{
    double luma = 0.0;
    BOOL dark = !prcScreen || !LiberShell_WallpaperLuma(prcScreen, &luma) || luma < LIBER_WALLPAPER_INK_LUMA;
    LiberUI::ThemeParams params = { dark ? LiberUI::ThemeMode::Dark : LiberUI::ThemeMode::Light,
                                    CurrentAccent(),
                                    (LiberUI::SurfaceTint)LiberExperience_SurfaceTint() };
    pTokens->Build(params);
}

VOID LiberShell_ApplyFont(LiberUI::Surface *pSurface)
{
    WCHAR family[LF_FACESIZE];

    if (!LiberExperience_UiFont(family, _countof(family)))
        family[0] = 0;
    pSurface->Fonts().SetFamily(family[0] ? family : LIBER_FONT_UI);
}

static DWORD WINAPI BroadcastColorSet(LPVOID pContext)
{
    DWORD_PTR result;

    UNREFERENCED_PARAMETER(pContext);
    SendMessageTimeoutW(HWND_BROADCAST, WM_SETTINGCHANGE, 0, (LPARAM)L"ImmersiveColorSet", SMTO_ABORTIFHUNG, 200,
                        &result);
    return 0;
}

BOOL LiberShell_SetDarkMode(BOOL bDark)
{
    HKEY key;
    DWORD light = bDark ? 0 : 1;
    LSTATUS status;

    if (RegCreateKeyExW(HKEY_CURRENT_USER, PERSONALIZE_KEY, 0, NULL, 0, KEY_SET_VALUE, NULL, &key, NULL) !=
        ERROR_SUCCESS)
    {
        return FALSE;
    }
    status = RegSetValueExW(key, L"AppsUseLightTheme", 0, REG_DWORD, (const BYTE *)&light, sizeof(light));
    if (status == ERROR_SUCCESS)
        status = RegSetValueExW(key, L"SystemUsesLightTheme", 0, REG_DWORD, (const BYTE *)&light, sizeof(light));
    RegCloseKey(key);
    if (status != ERROR_SUCCESS)
        return FALSE;
    if (!QueueUserWorkItem(BroadcastColorSet, NULL, WT_EXECUTELONGFUNCTION))
        BroadcastColorSet(NULL);
    return TRUE;
}

static int ScreenDpi(VOID)
{
    int dpi = 96;
    HDC screen = GetDC(NULL);

    if (screen)
    {
        dpi = GetDeviceCaps(screen, LOGPIXELSX);
        ReleaseDC(NULL, screen);
    }
    return dpi;
}

VOID LiberShell_FlyoutOrigin(const RECT *prcMonitor, UINT dpi, const RECT *prcAnchor, int widthPx, int heightPx,
                             BOOL bCenter, POINT *pptTopLeft)
{
    int inset = LiberUI::ScaleToDpi(LIBER_FLYOUT_INSET, dpi);

    if (bCenter)
        pptTopLeft->x = prcMonitor->left + (prcMonitor->right - prcMonitor->left - widthPx) / 2;
    else
        pptTopLeft->x = prcAnchor->right - widthPx;
    if (pptTopLeft->x + widthPx > prcMonitor->right - inset)
        pptTopLeft->x = prcMonitor->right - inset - widthPx;
    if (pptTopLeft->x < prcMonitor->left + inset)
        pptTopLeft->x = prcMonitor->left + inset;
    pptTopLeft->y = prcMonitor->bottom - LiberUI::ScaleToDpi(g_TaskbarReserve + LIBER_FLYOUT_GAP, dpi) - heightPx;
    if (pptTopLeft->y < prcMonitor->top + inset)
        pptTopLeft->y = prcMonitor->top + inset;
}

VOID LiberShell_PlaceFlyout(const RECT *prcAnchor, int widthPx, int heightPx, BOOL bCenter, POINT *pptTopLeft)
{
    MONITORINFO mi = { sizeof(mi) };

    if (!GetMonitorInfoW(MonitorFromRect(prcAnchor, MONITOR_DEFAULTTONEAREST), &mi))
        SetRect(&mi.rcMonitor, 0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN));
    LiberShell_FlyoutOrigin(&mi.rcMonitor, (UINT)ScreenDpi(), prcAnchor, widthPx, heightPx, bCenter, pptTopLeft);
}

static ULONGLONG DayNumber(const SYSTEMTIME *pTime)
{
    SYSTEMTIME day = *pTime;
    FILETIME ft;

    day.wHour = day.wMinute = day.wSecond = day.wMilliseconds = 0;
    day.wDayOfWeek = 0;
    if (!SystemTimeToFileTime(&day, &ft))
        return 0;
    return (((ULONGLONG)ft.dwHighDateTime << 32) | ft.dwLowDateTime) / (10000000ULL * 60 * 60 * 24);
}

static ULONGLONG MinuteNumber(const SYSTEMTIME *pTime)
{
    SYSTEMTIME minute = *pTime;
    FILETIME ft;

    minute.wSecond = minute.wMilliseconds = 0;
    minute.wDayOfWeek = 0;
    if (!SystemTimeToFileTime(&minute, &ft))
        return 0;
    return (((ULONGLONG)ft.dwHighDateTime << 32) | ft.dwLowDateTime) / (10000000ULL * 60);
}

VOID LiberStart_FormatWhen(const SYSTEMTIME *pNow, const SYSTEMTIME *pThen, LPWSTR pszText, UINT cchText)
{
    ULONGLONG nowMinute = MinuteNumber(pNow), thenMinute = MinuteNumber(pThen);
    ULONGLONG nowDay = DayNumber(pNow), thenDay = DayNumber(pThen);
    WCHAR szTime[32];

    if (!cchText)
        return;
    pszText[0] = 0;
    if (thenMinute <= nowMinute && nowMinute - thenMinute < 60)
    {
        if (nowMinute == thenMinute)
            StringCchCopyW(pszText, cchText, L"Just now");
        else
            StringCchPrintfW(pszText, cchText, L"%u min ago", (UINT)(nowMinute - thenMinute));
        return;
    }
    if (!GetTimeFormatW(LOCALE_USER_DEFAULT, TIME_NOSECONDS, pThen, NULL, szTime, _countof(szTime)))
        szTime[0] = 0;
    if (thenDay == nowDay)
        StringCchPrintfW(pszText, cchText, L"Today %s", szTime);
    else if (thenDay + 1 == nowDay)
        StringCchPrintfW(pszText, cchText, L"Yesterday %s", szTime);
    else if (!GetDateFormatW(LOCALE_USER_DEFAULT, DATE_SHORTDATE, pThen, NULL, pszText, (int)cchText))
        pszText[0] = 0;
}

UINT LiberStart_ScrollOffset(UINT offset, UINT count, UINT columns, UINT visible, int rows)
{
    UINT last = 0;

    if (!columns)
        return 0;
    if (count > visible)
        last = ((count - visible + columns - 1) / columns) * columns;
    if (rows < 0)
        return (UINT)-rows * columns >= offset ? 0 : offset - (UINT)-rows * columns;
    offset += (UINT)rows * columns;
    return offset > last ? last : offset;
}

UINT LiberStart_LoadRecentFiles(LIBER_RECENT_FILE *pFiles, UINT cMax)
{
    WCHAR szDir[MAX_PATH], szPattern[MAX_PATH];
    WIN32_FIND_DATAW fd;
    FILETIME written[32];
    UINT count = 0;

    if (!cMax || !SHGetSpecialFolderPathW(NULL, szDir, CSIDL_RECENT, FALSE) ||
        FAILED(StringCchPrintfW(szPattern, _countof(szPattern), L"%s\\*.lnk", szDir)))
    {
        return 0;
    }
    HANDLE find = FindFirstFileW(szPattern, &fd);
    if (find == INVALID_HANDLE_VALUE)
        return 0;
    do
    {
        UINT slot;
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
            continue;
        for (slot = count; slot > 0 && CompareFileTime(&written[slot - 1], &fd.ftLastWriteTime) < 0; slot--)
        {
            if (slot < cMax && slot < _countof(written))
            {
                pFiles[slot] = pFiles[slot - 1];
                written[slot] = written[slot - 1];
            }
        }
        if (slot >= cMax || slot >= _countof(written))
            continue;
        LIBER_RECENT_FILE &file = pFiles[slot];
        written[slot] = fd.ftLastWriteTime;
        StringCchPrintfW(file.szLink, _countof(file.szLink), L"%s\\%s", szDir, fd.cFileName);
        StringCchCopyW(file.szName, _countof(file.szName), fd.cFileName);
        PathRemoveExtensionW(file.szName);
        FILETIME local;
        if (!FileTimeToLocalFileTime(&fd.ftLastWriteTime, &local) || !FileTimeToSystemTime(&local, &file.stWritten))
            ZeroMemory(&file.stWritten, sizeof(file.stWritten));
        if (count < cMax && count < _countof(written))
            count++;
    } while (FindNextFileW(find, &fd));
    FindClose(find);
    return count;
}

UINT LiberCalendar_DaysInMonth(UINT year, UINT month)
{
    static const BYTE Days[12] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
    BOOL leap = (year % 4 == 0 && year % 100 != 0) || year % 400 == 0;

    if (month < 1 || month > 12)
        return 0;
    return Days[month - 1] + (month == 2 && leap ? 1 : 0);
}

UINT LiberCalendar_DayOfWeek(UINT year, UINT month, UINT day)
{
    static const BYTE Offsets[12] = { 0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4 };

    if (month < 3)
        year--;
    return (year + year / 4 - year / 100 + year / 400 + Offsets[month - 1] + day) % 7;
}

VOID LiberCalendar_MonthGrid(UINT year, UINT month, UINT firstDayOfWeek, LIBER_CALENDAR_DAY *pDays)
{
    UINT lead = (LiberCalendar_DayOfWeek(year, month, 1) + 7 - firstDayOfWeek % 7) % 7;
    UINT prevMonth = month == 1 ? 12 : month - 1, prevYear = month == 1 ? year - 1 : year;
    UINT nextMonth = month == 12 ? 1 : month + 1, nextYear = month == 12 ? year + 1 : year;
    UINT prevDays = LiberCalendar_DaysInMonth(prevYear, prevMonth);
    UINT days = LiberCalendar_DaysInMonth(year, month);

    for (UINT i = 0; i < LIBER_CALENDAR_CELLS; i++)
    {
        LIBER_CALENDAR_DAY &cell = pDays[i];
        if (i < lead)
        {
            cell.wYear = (WORD)prevYear;
            cell.wMonth = (WORD)prevMonth;
            cell.wDay = (WORD)(prevDays - lead + 1 + i);
            cell.bInMonth = FALSE;
        }
        else if (i - lead < days)
        {
            cell.wYear = (WORD)year;
            cell.wMonth = (WORD)month;
            cell.wDay = (WORD)(i - lead + 1);
            cell.bInMonth = TRUE;
        }
        else
        {
            cell.wYear = (WORD)nextYear;
            cell.wMonth = (WORD)nextMonth;
            cell.wDay = (WORD)(i - lead - days + 1);
            cell.bInMonth = FALSE;
        }
    }
}

VOID LiberMenu_Origin(const RECT *prcWork, POINT anchor, int widthPx, int heightPx, POINT *pptTopLeft)
{
    pptTopLeft->x = anchor.x;
    pptTopLeft->y = anchor.y - heightPx;
    if (pptTopLeft->x + widthPx > prcWork->right)
        pptTopLeft->x = anchor.x - widthPx;
    if (pptTopLeft->x < prcWork->left)
        pptTopLeft->x = prcWork->left;
    if (pptTopLeft->y < prcWork->top)
        pptTopLeft->y = anchor.y;
    if (pptTopLeft->y + heightPx > prcWork->bottom)
        pptTopLeft->y = prcWork->bottom - heightPx;
}

BOOL LiberMenu_DismissesOn(UINT msg, POINT pt, const RECT *prcMenu)
{
    switch (msg)
    {
        case WM_LBUTTONDOWN:
        case WM_RBUTTONDOWN:
        case WM_MBUTTONDOWN:
        case WM_XBUTTONDOWN:
        case WM_NCLBUTTONDOWN:
        case WM_NCRBUTTONDOWN:
        case WM_NCMBUTTONDOWN:
        case WM_NCXBUTTONDOWN:
            return !prcMenu || !PtInRect(prcMenu, pt);
    }
    return FALSE;
}

#define LIBER_START_KEY L"Software\\LiberNT\\Shell\\Start"
#define LIBER_START_PINS 64

BOOL LiberStart_LoadPins(LPWSTR pszPins, DWORD cchPins, UINT *pcPins)
{
    DWORD cb = cchPins * sizeof(WCHAR);
    UINT count = 0;

    if (pcPins)
        *pcPins = 0;
    if (!pszPins || cchPins < 2)
        return FALSE;
    pszPins[0] = pszPins[1] = 0;
    cb = (cchPins - 2) * sizeof(WCHAR);
    if (RegGetValueW(HKEY_CURRENT_USER, LIBER_START_KEY, L"Pins", RRF_RT_REG_MULTI_SZ, NULL, pszPins, &cb) !=
        ERROR_SUCCESS)
    {
        pszPins[0] = pszPins[1] = 0;
        return FALSE;
    }
    ATLASSERT(cb <= (cchPins - 2) * sizeof(WCHAR));
    pszPins[cb / sizeof(WCHAR)] = pszPins[cb / sizeof(WCHAR) + 1] = 0;
    for (LPCWSTR p = pszPins; *p; p += lstrlenW(p) + 1)
        count++;
    if (pcPins)
        *pcPins = count;
    return TRUE;
}

static const LPCWSTR LiberStartDefaultPins[] = {
    L"explorer.exe", L"systemsettings.exe", L"control.exe", L"lntget.exe", L"terminal.exe", L"notepad.exe",
    L"calc.exe",     L"mspaint.exe",        L"wordpad.exe", L"taskmgr.exe", L"cmd.exe",     L"camera.exe",
};

UINT LiberStart_DefaultPinRank(LPCWSTR pszTarget)
{
    LPCWSTR name = pszTarget && *pszTarget ? PathFindFileNameW(pszTarget) : NULL;

    for (UINT i = 0; name && i < _countof(LiberStartDefaultPins); i++)
    {
        if (!lstrcmpiW(name, LiberStartDefaultPins[i]))
            return i;
    }
    return UINT_MAX;
}

BOOL LiberStart_SystemApp(UINT index, LPWSTR pszPath, UINT cchPath, LPCWSTR *ppszName)
{
    static const struct
    {
        LPCWSTR Path;
        LPCWSTR Name;
    } Apps[] = {
        { L"%SystemRoot%\\ImmersiveControlPanel\\systemsettings.exe", L"Settings" },
        { L"%SystemRoot%\\system32\\control.exe", L"Control Panel" },
    };
    DWORD cch;

    if (index >= _countof(Apps) || !pszPath || !cchPath)
        return FALSE;
    cch = ExpandEnvironmentStringsW(Apps[index].Path, pszPath, cchPath);
    if (!cch || cch > cchPath)
        pszPath[0] = 0;
    if (ppszName)
        *ppszName = Apps[index].Name;
    return TRUE;
}

BOOL LiberStart_IsPinned(LPCWSTR pszPins, LPCWSTR pszPath)
{
    for (LPCWSTR p = pszPins; p && *p; p += lstrlenW(p) + 1)
    {
        if (!lstrcmpiW(p, pszPath))
            return TRUE;
    }
    return FALSE;
}

BOOL LiberStart_SetPinned(LPCWSTR pszPins, LPCWSTR pszPath, BOOL bPinned)
{
    WCHAR szOut[LIBER_START_PINS * MAX_PATH + 1];
    LPWSTR out = szOut;
    SIZE_T room = _countof(szOut) - 1;
    UINT count = 0;
    HKEY key;
    LSTATUS status;

    if (!pszPath || !*pszPath)
        return FALSE;
    for (LPCWSTR p = pszPins; p && *p; p += lstrlenW(p) + 1)
    {
        SIZE_T len = (SIZE_T)lstrlenW(p) + 1;
        if (!lstrcmpiW(p, pszPath) || len > room || count >= LIBER_START_PINS)
            continue;
        CopyMemory(out, p, len * sizeof(WCHAR));
        out += len;
        room -= len;
        count++;
    }
    if (bPinned)
    {
        SIZE_T len = (SIZE_T)lstrlenW(pszPath) + 1;
        if (len > room || count >= LIBER_START_PINS)
            return FALSE;
        CopyMemory(out, pszPath, len * sizeof(WCHAR));
        out += len;
    }
    *out++ = 0;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, LIBER_START_KEY, 0, NULL, 0, KEY_SET_VALUE, NULL, &key, NULL) !=
        ERROR_SUCCESS)
    {
        return FALSE;
    }
    status = RegSetValueExW(key, L"Pins", 0, REG_MULTI_SZ, (const BYTE *)szOut, (DWORD)((out - szOut) * sizeof(WCHAR)));
    RegCloseKey(key);
    return status == ERROR_SUCCESS;
}

BOOL LiberStart_Matches(LPCWSTR pszName, LPCWSTR pszQuery)
{
    if (!pszQuery || !*pszQuery)
        return TRUE;
    if (!pszName || !*pszName)
        return FALSE;
    return StrStrIW(pszName, pszQuery) != NULL;
}
