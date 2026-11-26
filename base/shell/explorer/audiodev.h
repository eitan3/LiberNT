/*
 * PROJECT:     LiberNT Explorer
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Audio output device enumeration shared by the volume flyouts
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

struct TFYAUDIODEV
{
    WCHAR szId[256];
    WCHAR szName[128];
    BOOL bDefault;
};

VOID TfyGetDeviceFriendlyName(IMMDevice *pDevice, LPWSTR pszName, SIZE_T cchName);
VOID TfyEnumRenderDevices(IMMDeviceEnumerator *pEnum, CAtlArray<TFYAUDIODEV> &Devices);
BOOL TfySetDefaultRenderDevice(LPCWSTR pszId);
