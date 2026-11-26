/*
 * PROJECT:     LiberNT Explorer
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Audio output device enumeration shared by the volume flyouts
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <windows.h>
#include <atlbase.h>
#include <atlcoll.h>
#include <mmdeviceapi.h>
#include <strsafe.h>

#include "audiodev.h"

static const PROPERTYKEY TFY_PKEY_Device_FriendlyName =
    { { 0xa45c254e, 0xdf1c, 0x4efd, { 0x80, 0x20, 0x67, 0xd1, 0x46, 0xa8, 0x50, 0xe0 } }, 14 };

VOID
TfyGetDeviceFriendlyName(IMMDevice *pDevice, LPWSTR pszName, SIZE_T cchName)
{
    CComPtr<IPropertyStore> pProps;
    StringCchCopyW(pszName, cchName, L"Speakers");
    if (!pDevice || FAILED(pDevice->OpenPropertyStore(STGM_READ, &pProps)))
        return;
    PROPVARIANT var;
    PropVariantInit(&var);
    if (SUCCEEDED(pProps->GetValue(TFY_PKEY_Device_FriendlyName, &var)) &&
        var.vt == VT_LPWSTR && var.pwszVal && var.pwszVal[0])
    {
        StringCchCopyW(pszName, cchName, var.pwszVal);
    }
    PropVariantClear(&var);
}

VOID
TfyEnumRenderDevices(IMMDeviceEnumerator *pEnum, CAtlArray<TFYAUDIODEV> &Devices)
{
    CComPtr<IMMDeviceCollection> pCollection;
    CComPtr<IMMDevice> pDefault;
    LPWSTR pszDefaultId = NULL;
    UINT nCount = 0;
    Devices.SetCount(0);
    if (!pEnum)
        return;
    if (SUCCEEDED(pEnum->GetDefaultAudioEndpoint(eRender, eMultimedia, &pDefault)))
        pDefault->GetId(&pszDefaultId);
    if (SUCCEEDED(pEnum->EnumAudioEndpoints(eRender, DEVICE_STATE_ACTIVE, &pCollection)) &&
        SUCCEEDED(pCollection->GetCount(&nCount)))
    {
        for (UINT i = 0; i < nCount && Devices.GetCount() < 16; i++)
        {
            CComPtr<IMMDevice> pDevice;
            LPWSTR pszId = NULL;
            TFYAUDIODEV dev;
            ZeroMemory(&dev, sizeof(dev));
            if (FAILED(pCollection->Item(i, &pDevice)) || FAILED(pDevice->GetId(&pszId)) || !pszId)
                continue;
            StringCchCopyW(dev.szId, _countof(dev.szId), pszId);
            dev.bDefault = (pszDefaultId && !wcscmp(pszDefaultId, pszId));
            CoTaskMemFree(pszId);
            TfyGetDeviceFriendlyName(pDevice, dev.szName, _countof(dev.szName));
            Devices.Add(dev);
        }
    }
    if (pszDefaultId)
        CoTaskMemFree(pszDefaultId);
}

BOOL
TfySetDefaultRenderDevice(LPCWSTR pszId)
{
    HKEY hKey;
    BOOL bRet = FALSE;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, L"Software\\Wine\\Drivers\\wdmaud.drv", 0, NULL, 0,
                        KEY_SET_VALUE, NULL, &hKey, NULL) == ERROR_SUCCESS)
    {
        bRet = RegSetValueExW(hKey, L"DefaultOutput", 0, REG_SZ, (const BYTE *)pszId,
                              (DWORD)((wcslen(pszId) + 1) * sizeof(WCHAR))) == ERROR_SUCCESS;
        RegCloseKey(hKey);
    }
    return bRet;
}
