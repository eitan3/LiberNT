/*
 * PROJECT:     LiberNT header
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Change the default audio endpoint of a role for the current user, and its format
 * COPYRIGHT:   Copyright 2026 LiberNT contributors
 */

#pragma once

#include <mmdeviceapi.h>
#include <mmreg.h>
#include <winreg.h>
#include <string.h>

/*
 * mmdevapi (Wine) keeps the user's default endpoints under this key of its
 * wdmaud.drv backend; GetDefaultAudioEndpoint reads it and the endpoint
 * notification thread reports a change of it through OnDefaultDeviceChanged.
 * Windows changes the default through an undocumented interface, so every
 * LiberNT sound UI writes this store through the function below.
 *
 * Windows lets a standard user choose the Default Format of a device; the
 * endpoint properties live under HKLM, so the user's choice is kept under
 * the same per-user key as a REG_BINARY WAVEFORMATEXTENSIBLE named after the
 * endpoint's GUID, where mmdevapi reads it before the device property.
 */
#define MMDEV_DEFAULT_KEY L"Software\\Wine\\Drivers\\wdmaud.drv"
#define MMDEV_FORMATS_KEY MMDEV_DEFAULT_KEY L"\\Formats"

static __inline LPCWSTR
MMDevDefaultValueName(EDataFlow Flow, ERole Role)
{
    if (Flow == eRender)
        return Role == eCommunications ? L"DefaultVoiceOutput" : L"DefaultOutput";
    if (Flow == eCapture)
        return Role == eCommunications ? L"DefaultVoiceInput" : L"DefaultInput";
    return NULL;
}

/* eConsole and eMultimedia share one default, as on Windows */
static __inline LONG
MMDevSetDefaultEndpoint(EDataFlow Flow, ERole Role, LPCWSTR EndpointId)
{
    LPCWSTR ValueName = MMDevDefaultValueName(Flow, Role);
    HKEY Key;
    LONG Error;

    if (!ValueName || !EndpointId || !EndpointId[0])
        return ERROR_INVALID_PARAMETER;

    Error = RegCreateKeyExW(HKEY_CURRENT_USER, MMDEV_DEFAULT_KEY, 0, NULL, 0, KEY_SET_VALUE, NULL, &Key, NULL);
    if (Error != ERROR_SUCCESS)
        return Error;
    Error = RegSetValueExW(Key, ValueName, 0, REG_SZ, (const BYTE *)EndpointId,
                           (DWORD)((lstrlenW(EndpointId) + 1) * sizeof(WCHAR)));
    RegCloseKey(Key);
    return Error;
}

/* The value name of an endpoint's per-user format: the GUID part of its ID */
static __inline BOOL
MMDevFormatValueName(LPCWSTR EndpointId, LPWSTR Name, SIZE_T Chars)
{
    LPCWSTR Guid = wcsrchr(EndpointId, L'{');
    SIZE_T Length;

    if (!Guid)
        return FALSE;
    Length = wcslen(Guid);
    if (Length + 1 > Chars)
        return FALSE;
    CopyMemory(Name, Guid, (Length + 1) * sizeof(WCHAR));
    return TRUE;
}

/* The format the user chose for the endpoint (a WAVEFORMATEXTENSIBLE), or ERROR_FILE_NOT_FOUND */
static __inline LONG
MMDevGetDeviceFormat(LPCWSTR EndpointId, WAVEFORMATEXTENSIBLE *Format)
{
    WCHAR Name[40];
    DWORD Size = sizeof(*Format);

    if (!MMDevFormatValueName(EndpointId, Name, _countof(Name)))
        return ERROR_INVALID_PARAMETER;
    if (RegGetValueW(HKEY_CURRENT_USER, MMDEV_FORMATS_KEY, Name, RRF_RT_REG_BINARY | RRF_SUBKEY_WOW6464KEY, NULL,
                     Format, &Size) != ERROR_SUCCESS)
        return ERROR_FILE_NOT_FOUND;
    return Size == sizeof(*Format) ? ERROR_SUCCESS : ERROR_FILE_NOT_FOUND;
}

/* Stores the user's format for the endpoint; Format NULL returns the choice to the device property */
static __inline LONG
MMDevSetDeviceFormat(LPCWSTR EndpointId, const WAVEFORMATEXTENSIBLE *Format)
{
    WCHAR Name[40];
    HKEY Key;
    LONG Error;

    if (!MMDevFormatValueName(EndpointId, Name, _countof(Name)))
        return ERROR_INVALID_PARAMETER;
    if (!Format)
    {
        Error = RegOpenKeyExW(HKEY_CURRENT_USER, MMDEV_FORMATS_KEY, 0, KEY_SET_VALUE | KEY_WOW64_64KEY, &Key);
        if (Error != ERROR_SUCCESS)
            return Error == ERROR_FILE_NOT_FOUND ? ERROR_SUCCESS : Error;
        Error = RegDeleteValueW(Key, Name);
        RegCloseKey(Key);
        return Error == ERROR_FILE_NOT_FOUND ? ERROR_SUCCESS : Error;
    }
    Error = RegCreateKeyExW(HKEY_CURRENT_USER, MMDEV_FORMATS_KEY, 0, NULL, 0, KEY_SET_VALUE | KEY_WOW64_64KEY, NULL,
                            &Key, NULL);
    if (Error != ERROR_SUCCESS)
        return Error;
    Error = RegSetValueExW(Key, Name, 0, REG_BINARY, (const BYTE *)Format, sizeof(*Format));
    RegCloseKey(Key);
    return Error;
}
