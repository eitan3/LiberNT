/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test that the shared-mode mix format follows the endpoint's PKEY_AudioEngine_DeviceFormat
 */

#include <apitest.h>

#define COBJMACROS
#include <windows.h>
#include <objbase.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <mmreg.h>
#include <initguid.h>
#include <ks.h>
#include <ksmedia.h>

static const PROPERTYKEY DeviceFormatKey =
    { { 0xf19f064d, 0x082c, 0x4e27, { 0xbc, 0x73, 0x68, 0x82, 0xa1, 0xbb, 0x8e, 0x4c } }, 0 };

static VOID
SetPcmFormat(WAVEFORMATEXTENSIBLE *Format, DWORD Rate, WORD Bits, WORD Channels)
{
    ZeroMemory(Format, sizeof(*Format));
    Format->Format.wFormatTag = WAVE_FORMAT_EXTENSIBLE;
    Format->Format.nChannels = Channels;
    Format->Format.nSamplesPerSec = Rate;
    Format->Format.wBitsPerSample = Bits;
    Format->Format.nBlockAlign = Channels * Bits / 8;
    Format->Format.nAvgBytesPerSec = Rate * Format->Format.nBlockAlign;
    Format->Format.cbSize = sizeof(*Format) - sizeof(Format->Format);
    Format->Samples.wValidBitsPerSample = Bits;
    Format->dwChannelMask = Channels == 1 ? SPEAKER_FRONT_CENTER : SPEAKER_FRONT_LEFT | SPEAKER_FRONT_RIGHT;
    Format->SubFormat = KSDATAFORMAT_SUBTYPE_PCM;
}

static HRESULT
GetMix(IMMDevice *Device, WAVEFORMATEX **Mix)
{
    IAudioClient *Client;
    HRESULT hr;

    *Mix = NULL;
    hr = IMMDevice_Activate(Device, &IID_IAudioClient, CLSCTX_INPROC_SERVER, NULL, (void **)&Client);
    if (FAILED(hr))
        return hr;
    hr = IAudioClient_GetMixFormat(Client, Mix);
    IAudioClient_Release(Client);
    return hr;
}

/* Another format the device accepts than the current mix: other rate first, else the other channel count */
static BOOL
FindOtherFormat(IMMDevice *Device, const WAVEFORMATEX *Mix, WAVEFORMATEXTENSIBLE *Other)
{
    static const DWORD Rates[] = { 44100, 48000, 22050, 96000, 11025 };
    IAudioClient *Client;
    WORD Channels;
    UINT Index;
    BOOL Found = FALSE;

    if (FAILED(IMMDevice_Activate(Device, &IID_IAudioClient, CLSCTX_INPROC_SERVER, NULL, (void **)&Client)))
        return FALSE;
    for (Channels = Mix->nChannels; !Found && Channels >= 1; Channels--)
    {
        for (Index = 0; !Found && Index < ARRAYSIZE(Rates); Index++)
        {
            if (Channels == Mix->nChannels && Rates[Index] == Mix->nSamplesPerSec)
                continue;
            SetPcmFormat(Other, Rates[Index], 16, Channels);
            Found = IAudioClient_IsFormatSupported(Client, AUDCLNT_SHAREMODE_EXCLUSIVE, &Other->Format, NULL) == S_OK;
        }
    }
    IAudioClient_Release(Client);
    return Found;
}

static VOID
TestEndpoint(IMMDevice *Device, EDataFlow Flow)
{
    IPropertyStore *Store = NULL;
    PROPVARIANT Saved, Value;
    WAVEFORMATEXTENSIBLE Other, Bogus;
    WAVEFORMATEX *Mix = NULL, *NewMix = NULL;
    HRESULT hr;

    hr = GetMix(Device, &Mix);
    ok(hr == S_OK, "GetMixFormat returned 0x%08lx\n", hr);
    if (FAILED(hr))
        return;
    if (!FindOtherFormat(Device, Mix, &Other))
    {
        skip("Flow %u endpoint offers no second format\n", Flow);
        CoTaskMemFree(Mix);
        return;
    }

    hr = IMMDevice_OpenPropertyStore(Device, STGM_READWRITE, &Store);
    if (FAILED(hr))
    {
        skip("Cannot write the endpoint properties (0x%08lx); the test needs administrator rights\n", hr);
        CoTaskMemFree(Mix);
        return;
    }
    PropVariantInit(&Saved);
    hr = IPropertyStore_GetValue(Store, &DeviceFormatKey, &Saved);
    ok(hr == S_OK && Saved.vt == VT_BLOB, "PKEY_AudioEngine_DeviceFormat: hr 0x%08lx, vt %u\n", hr, Saved.vt);

    PropVariantInit(&Value);
    Value.vt = VT_BLOB;
    Value.blob.cbSize = sizeof(Other);
    Value.blob.pBlobData = (BYTE *)&Other;
    hr = IPropertyStore_SetValue(Store, &DeviceFormatKey, &Value);
    if (FAILED(hr))
    {
        skip("SetValue(PKEY_AudioEngine_DeviceFormat) returned 0x%08lx; the test needs administrator rights\n", hr);
        goto done;
    }

    hr = GetMix(Device, &NewMix);
    ok(hr == S_OK, "GetMixFormat returned 0x%08lx\n", hr);
    if (SUCCEEDED(hr))
    {
        ok(NewMix->nSamplesPerSec == Other.Format.nSamplesPerSec, "mix rate %lu, expected %lu\n",
           NewMix->nSamplesPerSec, Other.Format.nSamplesPerSec);
        ok(NewMix->nChannels == Other.Format.nChannels, "mix channels %u, expected %u\n",
           NewMix->nChannels, Other.Format.nChannels);
        CoTaskMemFree(NewMix);
        NewMix = NULL;
    }

    /* A format the device does not offer leaves the driver's choice in force */
    SetPcmFormat(&Bogus, 12345, 16, Other.Format.nChannels);
    Value.blob.pBlobData = (BYTE *)&Bogus;
    ok(IPropertyStore_SetValue(Store, &DeviceFormatKey, &Value) == S_OK, "SetValue failed\n");
    if (SUCCEEDED(GetMix(Device, &NewMix)))
    {
        ok(NewMix->nSamplesPerSec != 12345, "the unsupported rate became the mix rate\n");
        CoTaskMemFree(NewMix);
    }

done:
    if (Saved.vt == VT_BLOB)
        IPropertyStore_SetValue(Store, &DeviceFormatKey, &Saved);
    PropVariantClear(&Saved);
    IPropertyStore_Release(Store);
    CoTaskMemFree(Mix);
}

START_TEST(deviceformat)
{
    IMMDeviceEnumerator *Enumerator = NULL;
    IMMDevice *Device;
    EDataFlow Flow;
    HRESULT hr;

    CoInitializeEx(NULL, COINIT_MULTITHREADED);
    hr = CoCreateInstance(&CLSID_MMDeviceEnumerator, NULL, CLSCTX_INPROC_SERVER,
                          &IID_IMMDeviceEnumerator, (void **)&Enumerator);
    ok(hr == S_OK, "CoCreateInstance returned 0x%08lx\n", hr);
    for (Flow = eRender; SUCCEEDED(hr) && Flow <= eCapture; Flow++)
    {
        if (FAILED(IMMDeviceEnumerator_GetDefaultAudioEndpoint(Enumerator, Flow, eConsole, &Device)))
        {
            skip("No default endpoint for flow %u\n", Flow);
            continue;
        }
        TestEndpoint(Device, Flow);
        IMMDevice_Release(Device);
    }
    if (Enumerator)
        IMMDeviceEnumerator_Release(Enumerator);
    CoUninitialize();
}
