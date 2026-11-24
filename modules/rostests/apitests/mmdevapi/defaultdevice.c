/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test that the sound UIs' default endpoint change reaches GetDefaultAudioEndpoint and the notification clients
 */

#include <apitest.h>

#define COBJMACROS
#include <windows.h>
#include <objbase.h>
#include <mmdeviceapi.h>
#include <libernt/mmdevdefault.h>

typedef struct _DEFAULT_WATCH
{
    IMMNotificationClient IMMNotificationClient_iface;
    HANDLE Event;
    EDataFlow Flow;
    ERole Role;
    WCHAR Id[256];
} DEFAULT_WATCH;

static DEFAULT_WATCH *impl_from_IMMNotificationClient(IMMNotificationClient *iface)
{
    return CONTAINING_RECORD(iface, DEFAULT_WATCH, IMMNotificationClient_iface);
}

static HRESULT WINAPI Watch_QueryInterface(IMMNotificationClient *iface, REFIID riid, void **ppv)
{
    if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_IMMNotificationClient))
    {
        *ppv = iface;
        return S_OK;
    }
    *ppv = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI Watch_AddRef(IMMNotificationClient *iface) { return 2; }
static ULONG WINAPI Watch_Release(IMMNotificationClient *iface) { return 1; }

static HRESULT WINAPI Watch_OnDeviceStateChanged(IMMNotificationClient *iface, LPCWSTR id, DWORD state) { return S_OK; }
static HRESULT WINAPI Watch_OnDeviceAdded(IMMNotificationClient *iface, LPCWSTR id) { return S_OK; }
static HRESULT WINAPI Watch_OnDeviceRemoved(IMMNotificationClient *iface, LPCWSTR id) { return S_OK; }
static HRESULT WINAPI Watch_OnPropertyValueChanged(IMMNotificationClient *iface, LPCWSTR id, const PROPERTYKEY key) { return S_OK; }

static HRESULT WINAPI Watch_OnDefaultDeviceChanged(IMMNotificationClient *iface, EDataFlow flow, ERole role, LPCWSTR id)
{
    DEFAULT_WATCH *This = impl_from_IMMNotificationClient(iface);

    if (flow == This->Flow && role == This->Role && id)
    {
        lstrcpynW(This->Id, id, ARRAYSIZE(This->Id));
        SetEvent(This->Event);
    }
    return S_OK;
}

static const IMMNotificationClientVtbl WatchVtbl =
{
    Watch_QueryInterface,
    Watch_AddRef,
    Watch_Release,
    Watch_OnDeviceStateChanged,
    Watch_OnDeviceAdded,
    Watch_OnDeviceRemoved,
    Watch_OnDefaultDeviceChanged,
    Watch_OnPropertyValueChanged,
};

static VOID
TestValueNames(VOID)
{
    ok(!wcscmp(MMDevDefaultValueName(eRender, eConsole), L"DefaultOutput"), "render console\n");
    ok(!wcscmp(MMDevDefaultValueName(eRender, eMultimedia), L"DefaultOutput"), "render multimedia\n");
    ok(!wcscmp(MMDevDefaultValueName(eRender, eCommunications), L"DefaultVoiceOutput"), "render communications\n");
    ok(!wcscmp(MMDevDefaultValueName(eCapture, eConsole), L"DefaultInput"), "capture console\n");
    ok(!wcscmp(MMDevDefaultValueName(eCapture, eCommunications), L"DefaultVoiceInput"), "capture communications\n");
    ok(MMDevDefaultValueName(eAll, eConsole) == NULL, "eAll has a value name\n");
    ok(MMDevSetDefaultEndpoint(eRender, eConsole, L"") == ERROR_INVALID_PARAMETER, "empty id accepted\n");
    ok(MMDevSetDefaultEndpoint(eAll, eConsole, L"x") == ERROR_INVALID_PARAMETER, "eAll accepted\n");
}

/* Save or restore the user's value so the test leaves the default as it found it */
static VOID
SaveValue(LPCWSTR Name, WCHAR *Value, DWORD Chars, BOOL *Present)
{
    DWORD Size = Chars * sizeof(WCHAR);

    *Present = RegGetValueW(HKEY_CURRENT_USER, MMDEV_DEFAULT_KEY, Name, RRF_RT_REG_SZ, NULL, Value, &Size) == ERROR_SUCCESS;
}

static VOID
RestoreValue(LPCWSTR Name, LPCWSTR Value, BOOL Present)
{
    HKEY Key;

    if (RegOpenKeyExW(HKEY_CURRENT_USER, MMDEV_DEFAULT_KEY, 0, KEY_SET_VALUE, &Key) != ERROR_SUCCESS)
        return;
    if (Present)
        RegSetValueExW(Key, Name, 0, REG_SZ, (const BYTE *)Value, (lstrlenW(Value) + 1) * sizeof(WCHAR));
    else
        RegDeleteValueW(Key, Name);
    RegCloseKey(Key);
}

static VOID
TestSwitch(IMMDeviceEnumerator *Enumerator, EDataFlow Flow)
{
    IMMDeviceCollection *Collection = NULL;
    IMMDevice *Device = NULL, *Default = NULL;
    LPWSTR DefaultId = NULL, OtherId = NULL, NewId = NULL;
    WCHAR Saved[256];
    BOOL Present;
    UINT Count = 0, Index;
    DEFAULT_WATCH Watch;
    LPCWSTR ValueName = MMDevDefaultValueName(Flow, eConsole);
    HRESULT hr;

    hr = IMMDeviceEnumerator_EnumAudioEndpoints(Enumerator, Flow, DEVICE_STATE_ACTIVE, &Collection);
    ok(hr == S_OK, "EnumAudioEndpoints returned 0x%08lx\n", hr);
    if (FAILED(hr))
        return;
    IMMDeviceCollection_GetCount(Collection, &Count);
    if (Count < 2 ||
        FAILED(IMMDeviceEnumerator_GetDefaultAudioEndpoint(Enumerator, Flow, eConsole, &Default)))
    {
        skip("Flow %u has %u active endpoints, two are needed\n", Flow, Count);
        IMMDeviceCollection_Release(Collection);
        return;
    }
    IMMDevice_GetId(Default, &DefaultId);
    IMMDevice_Release(Default);

    for (Index = 0; Index < Count && !OtherId; Index++)
    {
        LPWSTR Id = NULL;
        if (SUCCEEDED(IMMDeviceCollection_Item(Collection, Index, &Device)))
        {
            if (SUCCEEDED(IMMDevice_GetId(Device, &Id)) && wcscmp(Id, DefaultId))
                OtherId = Id;
            else
                CoTaskMemFree(Id);
            IMMDevice_Release(Device);
        }
    }
    IMMDeviceCollection_Release(Collection);
    ok(OtherId != NULL, "no second endpoint id\n");
    if (!OtherId)
    {
        CoTaskMemFree(DefaultId);
        return;
    }

    SaveValue(ValueName, Saved, ARRAYSIZE(Saved), &Present);

    ZeroMemory(&Watch, sizeof(Watch));
    Watch.IMMNotificationClient_iface.lpVtbl = &WatchVtbl;
    Watch.Event = CreateEventW(NULL, FALSE, FALSE, NULL);
    Watch.Flow = Flow;
    Watch.Role = eConsole;
    hr = IMMDeviceEnumerator_RegisterEndpointNotificationCallback(Enumerator, &Watch.IMMNotificationClient_iface);
    ok(hr == S_OK, "RegisterEndpointNotificationCallback returned 0x%08lx\n", hr);

    ok(MMDevSetDefaultEndpoint(Flow, eConsole, OtherId) == ERROR_SUCCESS, "MMDevSetDefaultEndpoint failed\n");

    ok(WaitForSingleObject(Watch.Event, 5000) == WAIT_OBJECT_0, "no OnDefaultDeviceChanged for flow %u\n", Flow);
    ok(!wcscmp(Watch.Id, OtherId), "notified %ls, expected %ls\n", Watch.Id, OtherId);

    hr = IMMDeviceEnumerator_GetDefaultAudioEndpoint(Enumerator, Flow, eMultimedia, &Default);
    ok(hr == S_OK, "GetDefaultAudioEndpoint returned 0x%08lx\n", hr);
    if (SUCCEEDED(hr))
    {
        IMMDevice_GetId(Default, &NewId);
        ok(NewId && !wcscmp(NewId, OtherId), "default is %ls, expected %ls\n", NewId, OtherId);
        CoTaskMemFree(NewId);
        IMMDevice_Release(Default);
    }

    RestoreValue(ValueName, Saved, Present);
    WaitForSingleObject(Watch.Event, 5000);
    IMMDeviceEnumerator_UnregisterEndpointNotificationCallback(Enumerator, &Watch.IMMNotificationClient_iface);
    CloseHandle(Watch.Event);
    CoTaskMemFree(OtherId);
    CoTaskMemFree(DefaultId);
}

START_TEST(defaultdevice)
{
    IMMDeviceEnumerator *Enumerator = NULL;
    HRESULT hr;

    TestValueNames();

    CoInitializeEx(NULL, COINIT_MULTITHREADED);
    hr = CoCreateInstance(&CLSID_MMDeviceEnumerator, NULL, CLSCTX_INPROC_SERVER,
                          &IID_IMMDeviceEnumerator, (void **)&Enumerator);
    ok(hr == S_OK, "CoCreateInstance returned 0x%08lx\n", hr);
    if (SUCCEEDED(hr))
    {
        TestSwitch(Enumerator, eRender);
        TestSwitch(Enumerator, eCapture);
        IMMDeviceEnumerator_Release(Enumerator);
    }
    CoUninitialize();
}
