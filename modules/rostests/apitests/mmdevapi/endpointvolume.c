/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test that the endpoint volume of a playback and of a recording device activates and follows its controls
 */

#include <apitest.h>

#define COBJMACROS
#include <windows.h>
#include <objbase.h>
#include <math.h>
#include <initguid.h>
#include <mmdeviceapi.h>
#include <endpointvolume.h>

/* Reads the endpoint volume, sets a level and a mute state, and checks both come back */
static VOID
TestEndpointVolume(IMMDevice *Device, EDataFlow Flow)
{
    IAudioEndpointVolume *Volume = NULL;
    DWORD Hardware = 0;
    float Original = -1.0f, Level, Min = 0.0f, Max = 0.0f, Increment = 0.0f, Set;
    BOOL Mute = FALSE, SetMute = TRUE;
    HRESULT hr;

    hr = IMMDevice_Activate(Device, &IID_IAudioEndpointVolume, CLSCTX_INPROC_SERVER, NULL, (void **)&Volume);
    ok(hr == S_OK, "flow %u: IAudioEndpointVolume activation returned 0x%08lx\n", Flow, hr);
    if (FAILED(hr))
        return;

    hr = IAudioEndpointVolume_GetMasterVolumeLevelScalar(Volume, &Original);
    ok(hr == S_OK, "flow %u: GetMasterVolumeLevelScalar returned 0x%08lx\n", Flow, hr);
    ok(hr != S_OK || (Original >= 0.0f && Original <= 1.0f), "flow %u: level %f\n", Flow, Original);

    hr = IAudioEndpointVolume_QueryHardwareSupport(Volume, &Hardware);
    ok(hr == S_OK, "flow %u: QueryHardwareSupport returned 0x%08lx\n", Flow, hr);
    ok(hr != S_OK || (Hardware & ENDPOINT_HARDWARE_SUPPORT_VOLUME),
       "flow %u: the endpoint does not report hardware volume (0x%08lx)\n", Flow, Hardware);

    hr = IAudioEndpointVolume_GetVolumeRange(Volume, &Min, &Max, &Increment);
    ok(hr == S_OK, "flow %u: GetVolumeRange returned 0x%08lx\n", Flow, hr);
    ok(hr != S_OK || (Min < Max && Increment > 0.0f), "flow %u: range %f..%f step %f\n", Flow, Min, Max, Increment);

    if (hr == S_OK)
    {
        Set = Original < 0.5f ? 0.75f : 0.25f;
        hr = IAudioEndpointVolume_SetMasterVolumeLevelScalar(Volume, Set, NULL);
        ok(hr == S_OK, "flow %u: SetMasterVolumeLevelScalar returned 0x%08lx\n", Flow, hr);
        Level = -1.0f;
        hr = IAudioEndpointVolume_GetMasterVolumeLevelScalar(Volume, &Level);
        ok(hr == S_OK && fabsf(Level - Set) <= Increment / (Max - Min) + 0.001f,
           "flow %u: level after set %f, hr 0x%08lx\n", Flow, Level, hr);
    }

    hr = IAudioEndpointVolume_GetMute(Volume, &Mute);
    ok(hr == S_OK, "flow %u: GetMute returned 0x%08lx\n", Flow, hr);
    if (hr == S_OK)
    {
        SetMute = !Mute;
        hr = IAudioEndpointVolume_SetMute(Volume, SetMute, NULL);
        ok(hr == S_OK, "flow %u: SetMute returned 0x%08lx\n", Flow, hr);
        Mute = !SetMute;
        hr = IAudioEndpointVolume_GetMute(Volume, &Mute);
        ok(hr == S_OK && Mute == SetMute, "flow %u: mute after set %d (set %d), hr 0x%08lx\n", Flow, Mute, SetMute, hr);
        /* Leave the device as found */
        IAudioEndpointVolume_SetMute(Volume, !SetMute, NULL);
    }
    if (Original >= 0.0f)
        IAudioEndpointVolume_SetMasterVolumeLevelScalar(Volume, Original, NULL);
    IAudioEndpointVolume_Release(Volume);
}

START_TEST(endpointvolume)
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
        TestEndpointVolume(Device, Flow);
        IMMDevice_Release(Device);
    }
    if (Enumerator)
        IMMDeviceEnumerator_Release(Enumerator);
    CoUninitialize();
}
