/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test that the microphone privacy settings decide which programs may record
 */

#include <apitest.h>

#define COBJMACROS
#include <windows.h>
#include <objbase.h>
#include <strsafe.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <libernt/micconsent.h>

typedef struct _SAVED_VALUE
{
    LPCWSTR Key;
    LPCWSTR Name;
    DWORD Type;
    BYTE Data[64];
    DWORD Size;
    BOOL Present;
} SAVED_VALUE;

static WCHAR AppKey[MAX_PATH * 2];
static SAVED_VALUE Saved[] =
{
    { MIC_CONSENT_KEY, L"Value" },
    { MIC_CONSENT_APPS_KEY, L"Value" },
    { MIC_CONSENT_STRICT_KEY, MIC_CONSENT_STRICT_VALUE },
    { AppKey, L"Value" },
};

static BOOL
SetValue(HKEY Root, LPCWSTR Key, LPCWSTR Name, DWORD Type, const VOID *Data, DWORD Size)
{
    HKEY hKey;
    LSTATUS Error;

    Error = RegCreateKeyExW(Root, Key, 0, NULL, 0, KEY_SET_VALUE | KEY_WOW64_64KEY, NULL, &hKey, NULL);
    if (Error)
        return FALSE;
    Error = RegSetValueExW(hKey, Name, 0, Type, Data, Size);
    RegCloseKey(hKey);
    return Error == ERROR_SUCCESS;
}

static VOID
SaveValues(VOID)
{
    for (UINT i = 0; i < ARRAYSIZE(Saved); i++)
    {
        Saved[i].Size = sizeof(Saved[i].Data);
        Saved[i].Present = RegGetValueW(HKEY_CURRENT_USER, Saved[i].Key, Saved[i].Name, RRF_RT_ANY | RRF_SUBKEY_WOW6464KEY,
                                        &Saved[i].Type, Saved[i].Data, &Saved[i].Size) == ERROR_SUCCESS;
    }
}

static VOID
RestoreValues(VOID)
{
    for (UINT i = 0; i < ARRAYSIZE(Saved); i++)
    {
        if (Saved[i].Present)
            RegSetKeyValueW(HKEY_CURRENT_USER, Saved[i].Key, Saved[i].Name, Saved[i].Type, Saved[i].Data, Saved[i].Size);
        else
            RegDeleteKeyValueW(HKEY_CURRENT_USER, Saved[i].Key, Saved[i].Name);
    }
}

static BOOL
SetConsent(LPCWSTR Key, LPCWSTR Value)
{
    HKEY hKey;
    BOOL Ok;

    if (Value)
        return SetValue(HKEY_CURRENT_USER, Key, L"Value", REG_SZ, Value, (lstrlenW(Value) + 1) * sizeof(WCHAR));
    Ok = !RegOpenKeyExW(HKEY_CURRENT_USER, Key, 0, KEY_SET_VALUE | KEY_WOW64_64KEY, &hKey);
    if (Ok)
    {
        RegDeleteValueW(hKey, L"Value");
        RegCloseKey(hKey);
    }
    return Ok;
}

static BOOL
SetStrict(BOOL On)
{
    DWORD Value = On;

    return SetValue(HKEY_CURRENT_USER, MIC_CONSENT_STRICT_KEY, MIC_CONSENT_STRICT_VALUE, REG_DWORD, &Value, sizeof(Value));
}

/* Initialize a shared capture stream on the default recording device */
static HRESULT
OpenCapture(IMMDeviceEnumerator *Enumerator)
{
    IMMDevice *Device;
    IAudioClient *Client;
    WAVEFORMATEX *Mix;
    HRESULT hr;

    hr = IMMDeviceEnumerator_GetDefaultAudioEndpoint(Enumerator, eCapture, eConsole, &Device);
    if (FAILED(hr))
        return hr;
    hr = IMMDevice_Activate(Device, &IID_IAudioClient, CLSCTX_INPROC_SERVER, NULL, (void **)&Client);
    IMMDevice_Release(Device);
    if (FAILED(hr))
        return hr;
    hr = IAudioClient_GetMixFormat(Client, &Mix);
    if (SUCCEEDED(hr))
    {
        hr = IAudioClient_Initialize(Client, AUDCLNT_SHAREMODE_SHARED, 0, 1000000, 0, Mix, NULL);
        CoTaskMemFree(Mix);
    }
    IAudioClient_Release(Client);
    return hr;
}

static BOOL
KeyExists(LPCWSTR Key)
{
    HKEY hKey;
    BOOL Exists = !RegOpenKeyExW(HKEY_CURRENT_USER, Key, 0, KEY_READ | KEY_WOW64_64KEY, &hKey);

    if (Exists)
        RegCloseKey(hKey);
    return Exists;
}

static VOID
TestStore(LPCWSTR Image)
{
    WCHAR Name[16];

    MicConsentAppKeyName(L"C:\\Program Files\\App\\app.exe", Name, ARRAYSIZE(Name));
    ok(!wcscmp(Name, L"C:#Program File"), "truncated key name is %ls\n", Name);

    ok(SetConsent(MIC_CONSENT_KEY, NULL), "clearing the microphone Value failed\n");
    ok(SetConsent(MIC_CONSENT_APPS_KEY, NULL), "clearing the desktop apps Value failed\n");
    ok(SetConsent(AppKey, NULL), "clearing the app Value failed\n");
    ok(SetStrict(FALSE), "turning strict mode off failed\n");
    ok(MicConsentCheck(Image), "default is not allow\n");

    ok(SetConsent(MIC_CONSENT_KEY, L"Deny"), "setting the microphone Value failed\n");
    ok(KeyExists(MIC_CONSENT_KEY), "the microphone consent key was not created\n");
    ok(!MicConsentCheck(Image), "global switch off still allows\n");
    ok(SetConsent(MIC_CONSENT_KEY, L"Allow"), "setting the microphone Value failed\n");
    ok(SetConsent(MIC_CONSENT_APPS_KEY, L"Deny"), "setting the desktop apps Value failed\n");
    ok(KeyExists(MIC_CONSENT_APPS_KEY), "the desktop apps consent key was not created\n");
    ok(!MicConsentCheck(Image), "desktop apps switch off still allows\n");
    ok(SetConsent(MIC_CONSENT_APPS_KEY, L"Allow"), "setting the desktop apps Value failed\n");

    ok(SetStrict(TRUE), "turning strict mode on failed\n");
    ok(KeyExists(MIC_CONSENT_STRICT_KEY), "the strict mode key was not created\n");
    ok(!MicConsentCheck(Image), "strict mode allows an unlisted app\n");
    ok(SetConsent(AppKey, L"Allow"), "allowing the app failed\n");
    ok(KeyExists(AppKey), "the app consent key was not created\n");
    ok(MicConsentCheck(Image), "strict mode refuses a listed app\n");
    ok(SetConsent(AppKey, L"Deny"), "refusing the app failed\n");
    ok(!MicConsentCheck(Image), "strict mode allows an app set to Deny\n");
}

static VOID
TestEnforcement(LPCWSTR Image)
{
    IMMDeviceEnumerator *Enumerator = NULL;
    HRESULT hr;

    hr = CoCreateInstance(&CLSID_MMDeviceEnumerator, NULL, CLSCTX_INPROC_SERVER, &IID_IMMDeviceEnumerator,
                          (void **)&Enumerator);
    ok(hr == S_OK, "CoCreateInstance returned 0x%08lx\n", hr);
    if (FAILED(hr))
        return;

    ok(SetConsent(MIC_CONSENT_KEY, NULL), "clearing the microphone Value failed\n");
    ok(SetConsent(MIC_CONSENT_APPS_KEY, NULL), "clearing the desktop apps Value failed\n");
    ok(SetConsent(AppKey, NULL), "clearing the app Value failed\n");
    ok(SetStrict(FALSE), "turning strict mode off failed\n");
    hr = OpenCapture(Enumerator);
    if (hr == E_NOTFOUND || hr == AUDCLNT_E_DEVICE_INVALIDATED)
    {
        skip("No recording device (0x%08lx)\n", hr);
        IMMDeviceEnumerator_Release(Enumerator);
        return;
    }
    ok(hr == S_OK, "allowed capture returned 0x%08lx\n", hr);

    ok(SetStrict(TRUE), "turning strict mode on failed\n");
    hr = OpenCapture(Enumerator);
    ok(hr == E_ACCESSDENIED, "unlisted app in strict mode: 0x%08lx\n", hr);

    ok(SetConsent(AppKey, L"Allow"), "allowing the app failed\n");
    hr = OpenCapture(Enumerator);
    ok(hr == S_OK, "listed app in strict mode: 0x%08lx\n", hr);

    ok(SetStrict(FALSE), "turning strict mode off failed\n");
    ok(SetConsent(MIC_CONSENT_KEY, L"Deny"), "setting the microphone Value failed\n");
    hr = OpenCapture(Enumerator);
    ok(hr == E_ACCESSDENIED, "global switch off: 0x%08lx\n", hr);

    IMMDeviceEnumerator_Release(Enumerator);
}

START_TEST(micprivacy)
{
    WCHAR Image[MAX_PATH];
    SIZE_T Length;
    DWORD Length_ = GetModuleFileNameW(NULL, Image, ARRAYSIZE(Image));

    if (!Length_ || Length_ >= ARRAYSIZE(Image))
    {
        skip("GetModuleFileNameW failed or truncated (%lu)\n", Length_);
        return;
    }
    StringCchCopyW(AppKey, ARRAYSIZE(AppKey), MIC_CONSENT_APPS_KEY L"\\");
    Length = wcslen(AppKey);
    MicConsentAppKeyName(Image, AppKey + Length, ARRAYSIZE(AppKey) - Length);

    SaveValues();
    TestStore(Image);
    CoInitializeEx(NULL, COINIT_MULTITHREADED);
    TestEnforcement(Image);
    CoUninitialize();
    RestoreValues();
}
