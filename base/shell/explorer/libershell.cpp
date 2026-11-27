/*
 * PROJECT:     LiberNT Explorer
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Pieces shared by the Liber shell surfaces
 * COPYRIGHT:   Copyright 2026 Eitan Hadar <eitanhadar4@gmail.com>
 */

#include <windows.h>
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
    LiberUI::GlassTint tint = pTokens->Material(material, 0.5);
    pSurface->SetMaterial((int)material, LiberUI::OklchToArgb(tint.Color, 1.0), (BYTE)(tint.Alpha * 255.0 + 0.5),
                          radiusDip);
}

VOID LiberShell_BuildTokens(LiberUI::ThemeTokens *pTokens)
{
    g_LiberGlass = LiberShell_ReadGlassEnabled();
    LiberUI::ThemeParams params = { LiberExperience_IsDark() ? LiberUI::ThemeMode::Dark : LiberUI::ThemeMode::Light,
                                    LiberUI::AccentPreset(LIBER_ACCENT_AZURE),
                                    (LiberUI::SurfaceTint)LiberExperience_SurfaceTint() };
    pTokens->Build(params);
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

BOOL LiberShell_WallpaperLuma(const RECT *prcScreen, double *pLuma)
{
    WCHAR szPath[MAX_PATH], szStyle[8];
    DWORD cb = sizeof(szStyle);
    UINT style = LIBER_WALLPAPER_FILL;
    DWORD *pixels = NULL;
    UINT width = 0, height = 0;
    double sum = 0.0;
    UINT samples = 0;

    *pLuma = 0.0;
    if (!SystemParametersInfoW(SPI_GETDESKWALLPAPER, _countof(szPath), szPath, 0) || !szPath[0])
        return FALSE;
    if (RegGetValueW(HKEY_CURRENT_USER, L"Control Panel\\Desktop", L"WallpaperStyle", RRF_RT_REG_SZ, NULL, szStyle,
                     &cb) == ERROR_SUCCESS)
    {
        style = (UINT)_wtoi(szStyle);
    }
    if (!LoadWallpaperPixels(szPath, &pixels, &width, &height))
        return FALSE;
    SIZE screen = { GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN) }, image = { (LONG)width, (LONG)height };
    RECT area;
    LiberShell_MapToWallpaper(prcScreen, screen, image, style, &area);
    for (UINT y = 0; y < 10 && !IsRectEmpty(&area); y++)
    {
        for (UINT x = 0; x < 10; x++)
        {
            INT px = area.left + (INT)((area.right - area.left) * (x * 2 + 1) / 20);
            INT py = area.top + (INT)((area.bottom - area.top) * (y * 2 + 1) / 20);
            if (px >= 0 && py >= 0 && (UINT)px < width && (UINT)py < height)
            {
                sum += LiberShell_Luminance(pixels[(SIZE_T)py * width + px]);
                samples++;
            }
        }
    }
    LocalFree(pixels);
    if (!samples)
        return FALSE;
    *pLuma = sum / samples;
    return TRUE;
}

VOID LiberShell_BuildWallpaperInkTokens(LiberUI::ThemeTokens *pTokens, const RECT *prcScreen)
{
    double luma = 0.0;
    BOOL dark = !prcScreen || !LiberShell_WallpaperLuma(prcScreen, &luma) || luma < LIBER_WALLPAPER_INK_LUMA;
    LiberUI::ThemeParams params = { dark ? LiberUI::ThemeMode::Dark : LiberUI::ThemeMode::Light,
                                    LiberUI::AccentPreset(LIBER_ACCENT_AZURE),
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
    pptTopLeft->y = prcMonitor->bottom - LiberUI::ScaleToDpi(LIBER_TASKBAR_DOCK + LIBER_FLYOUT_GAP, dpi) - heightPx;
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
    if (RegGetValueW(HKEY_CURRENT_USER, LIBER_START_KEY, L"Pins", RRF_RT_REG_MULTI_SZ, NULL, pszPins, &cb) !=
        ERROR_SUCCESS)
    {
        pszPins[0] = pszPins[1] = 0;
        return FALSE;
    }
    for (LPCWSTR p = pszPins; *p; p += lstrlenW(p) + 1)
        count++;
    if (pcPins)
        *pcPins = count;
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
