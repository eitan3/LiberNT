/*
 * PROJECT:     LiberNT header
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Change the default audio endpoint of a role for the current user
 */

#pragma once

#include <mmdeviceapi.h>

/*
 * mmdevapi (Wine) keeps the user's default endpoints under this key of its
 * wdmaud.drv backend; GetDefaultAudioEndpoint reads it and the endpoint
 * notification thread reports a change of it through OnDefaultDeviceChanged.
 * Windows changes the default through an undocumented interface, so every
 * LiberNT sound UI writes this store through the function below.
 */
#define MMDEV_DEFAULT_KEY L"Software\\Wine\\Drivers\\wdmaud.drv"

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
