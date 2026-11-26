/*
 * PROJECT:     LiberNT Explorer - Unit-tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test the audio output enumeration shared by the volume flyouts
 * COPYRIGHT:   Copyright 2026 Eitan Hadar <eitanhadar4@gmail.com>
 */

#include <apitest.h>

#include <windows.h>
#include <atlbase.h>
#include <atlcoll.h>
#include <initguid.h>
#include <mmdeviceapi.h>

#include "audiodev.h"

START_TEST(Audio)
{
    WCHAR name[128];
    CAtlArray<TFYAUDIODEV> devices;
    CComPtr<IMMDeviceEnumerator> pEnum;
    HRESULT hr;

    TfyGetDeviceFriendlyName(NULL, name, _countof(name));
    ok(!wcscmp(name, L"Speakers"), "fallback name %ls\n", name);

    TFYAUDIODEV stale;
    ZeroMemory(&stale, sizeof(stale));
    devices.Add(stale);
    TfyEnumRenderDevices(NULL, devices);
    ok(devices.GetCount() == 0, "no enumerator gives no devices, %u\n", (UINT)devices.GetCount());

    CoInitializeEx(NULL, COINIT_MULTITHREADED);
    hr = CoCreateInstance(CLSID_MMDeviceEnumerator, NULL, CLSCTX_INPROC_SERVER, IID_IMMDeviceEnumerator,
                          (void **)&pEnum);
    if (FAILED(hr))
    {
        skip("no MMDevice enumerator (0x%08lX)\n", hr);
        CoUninitialize();
        return;
    }

    TfyEnumRenderDevices(pEnum, devices);
    UINT defaults = 0;
    for (size_t i = 0; i < devices.GetCount(); i++)
    {
        ok(devices[i].szId[0] != 0 && devices[i].szName[0] != 0, "device %u has an id and a name\n", (UINT)i);
        defaults += devices[i].bDefault ? 1 : 0;
    }
    ok(devices.GetCount() <= 16, "at most 16 devices, %u\n", (UINT)devices.GetCount());
    ok(defaults <= 1, "%u default devices\n", defaults);

    CComPtr<IMMDevice> pDefault;
    if (SUCCEEDED(pEnum->GetDefaultAudioEndpoint(eRender, eMultimedia, &pDefault)) && devices.GetCount() > 0)
        ok(defaults == 1, "the default endpoint is marked, %u marked\n", defaults);

    pDefault.Release();
    pEnum.Release();
    CoUninitialize();
}
