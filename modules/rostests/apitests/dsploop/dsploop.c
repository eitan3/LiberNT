/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     The loop through the DSP's endpoint in user mode: a 1 kHz tone rendered into the 'Loopback' render endpoint and captured from the 'Loopback' capture endpoint in parallel through the audio stack, 48000 frames at 48 kHz stereo in 16 valid bits, then in 24 and 32 when the shared-mode engine takes them, compared from the first matching frame (plan §8 C4)
 * COPYRIGHT:   Copyright 2026 LiberNT contributors
 */

#include <apitest.h>
#include <apitest_expected.h>

#define COBJMACROS
#include <windows.h>
#include <objbase.h>
/* before the headers that name the GUIDs and the property keys: they define them here, once */
#include <initguid.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <mmreg.h>
#include <ks.h>
#include <ksmedia.h>
#include <devpkey.h>
#include <stdlib.h>
#include "compare.h"

#define DSPLOOP_RATE             48000
#define DSPLOOP_CHANNELS         2
/* one second of tone; the capture holds a second of latency before it as well */
#define DSPLOOP_FRAMES           48000
#define DSPLOOP_MAX_OFFSET       48000
#define DSPLOOP_CAPTURE_FRAMES   (DSPLOOP_FRAMES + DSPLOOP_MAX_OFFSET)
/* the capture's buffer and the render's, in 100 ns units: 200 ms and 100 ms, the loop's own latency kept small and known */
#define DSPLOOP_CAPTURE_BUFFER   2000000
#define DSPLOOP_RENDER_BUFFER    1000000
/* the wall-clock budget of a run, in milliseconds: the two seconds of audio with room for a slow guest */
#define DSPLOOP_RUN_MS           15000
/* the PCM's name, as the endpoint driver's INF puts it into both friendly names of the topology's PCM */
#define DSPLOOP_NAME             L"Loopback"

/* A run's outcome */
#define DSPLOOP_OK               0
#define DSPLOOP_FAILED           1
#define DSPLOOP_SKIPPED          2

/* Whether an endpoint's friendly names carry the text: the interface's, then the device's */
static
BOOL
EndpointNamed(
    IMMDevice *Device,
    const WCHAR *Text)
{
    IPropertyStore *Store = NULL;
    PROPVARIANT Value;
    BOOL Found = FALSE;
    HRESULT hr;

    PropVariantInit(&Value);
    hr = IMMDevice_OpenPropertyStore(Device, STGM_READ, &Store);
    if (FAILED(hr))
        return FALSE;
    hr = IPropertyStore_GetValue(Store, (const PROPERTYKEY *)&DEVPKEY_DeviceInterface_FriendlyName, &Value);
    if (SUCCEEDED(hr) && Value.vt == VT_LPWSTR && Value.pwszVal && wcsstr(Value.pwszVal, Text))
        Found = TRUE;
    PropVariantClear(&Value);
    if (!Found)
    {
        hr = IPropertyStore_GetValue(Store, (const PROPERTYKEY *)&DEVPKEY_Device_FriendlyName, &Value);
        if (SUCCEEDED(hr) && Value.vt == VT_LPWSTR && Value.pwszVal && wcsstr(Value.pwszVal, Text))
            Found = TRUE;
        PropVariantClear(&Value);
    }
    IPropertyStore_Release(Store);
    return Found;
}

/* The active endpoint of a flow whose names carry the PCM's name */
static
HRESULT
FindEndpoint(
    IMMDeviceEnumerator *Enumerator,
    EDataFlow Flow,
    _Out_ IMMDevice **Device)
{
    IMMDeviceCollection *Collection = NULL;
    IMMDevice *Candidate;
    UINT Count = 0, I;
    HRESULT hr;

    *Device = NULL;
    hr = IMMDeviceEnumerator_EnumAudioEndpoints(Enumerator, Flow, DEVICE_STATE_ACTIVE, &Collection);
    if (FAILED(hr))
        return hr;
    if (SUCCEEDED(IMMDeviceCollection_GetCount(Collection, &Count)))
    {
        for (I = 0; I < Count && *Device == NULL; I++)
        {
            if (SUCCEEDED(IMMDeviceCollection_Item(Collection, I, &Candidate)))
            {
                if (EndpointNamed(Candidate, DSPLOOP_NAME))
                    *Device = Candidate;
                else
                    IMMDevice_Release(Candidate);
            }
        }
    }
    IMMDeviceCollection_Release(Collection);
    return *Device ? S_OK : HRESULT_FROM_WIN32(ERROR_NOT_FOUND);
}

/* The tone's format at a count of valid bits: plain PCM at 16, extensible at 24 valid in a 32-bit word and at 32, as the endpoint's rule has them */
static
VOID
ToneFormat(
    WAVEFORMATEXTENSIBLE *Format,
    WORD ValidBits)
{
    WORD Container = ValidBits == 24 ? 32 : ValidBits;

    ZeroMemory(Format, sizeof(*Format));
    Format->Format.nChannels = DSPLOOP_CHANNELS;
    Format->Format.nSamplesPerSec = DSPLOOP_RATE;
    Format->Format.wBitsPerSample = Container;
    Format->Format.nBlockAlign = DSPLOOP_CHANNELS * Container / 8;
    Format->Format.nAvgBytesPerSec = DSPLOOP_RATE * Format->Format.nBlockAlign;
    if (ValidBits == 16)
    {
        Format->Format.wFormatTag = WAVE_FORMAT_PCM;
        return;
    }
    Format->Format.wFormatTag = WAVE_FORMAT_EXTENSIBLE;
    Format->Format.cbSize = sizeof(*Format) - sizeof(Format->Format);
    Format->Samples.wValidBitsPerSample = ValidBits;
    Format->dwChannelMask = SPEAKER_FRONT_LEFT | SPEAKER_FRONT_RIGHT;
    Format->SubFormat = KSDATAFORMAT_SUBTYPE_PCM;
}

/* One tone sample into the stream: a little-endian word, the valid bits at the top of a 32-bit word when there are 24 of them */
static
VOID
StoreSample(
    BYTE *At,
    WORD ValidBits,
    int32_t Sample)
{
    uint32_t Word = (uint32_t)(ValidBits == 24 ? Sample * 256 : Sample);

    At[0] = (BYTE)Word;
    At[1] = (BYTE)(Word >> 8);
    if (ValidBits != 16)
    {
        At[2] = (BYTE)(Word >> 16);
        At[3] = (BYTE)(Word >> 24);
    }
}

/* Count frames of the tone from First into the client's buffer */
static
VOID
StoreTone(
    BYTE *Data,
    UINT32 First,
    UINT32 Count,
    WORD ValidBits,
    UINT32 FrameBytes)
{
    UINT32 SampleBytes = FrameBytes / DSPLOOP_CHANNELS, N, C;

    for (N = 0; N < Count; N++)
    {
        for (C = 0; C < DSPLOOP_CHANNELS; C++)
            StoreSample(Data + (N * DSPLOOP_CHANNELS + C) * SampleBytes, ValidBits, DsploopToneSample(First + N, ValidBits));
    }
}

/* A stream the loop has open: its client, its event and its service */
typedef struct _DSPLOOP_STREAM
{
    IAudioClient *Client;
    HANDLE Event;
    IAudioRenderClient *Render;
    IAudioCaptureClient *Capture;
    /* the render buffer, in frames */
    UINT32 Capacity;
} DSPLOOP_STREAM;

/*
 * A shared-mode event-driven stream: the flags are the caller's, as the
 * sibling apitests open theirs (the render apitest lets the engine convert
 * the client's format; the capture apitest asks for the events alone). The
 * status of Initialize is the caller's to read: the engine's refusal of a
 * format is AUDCLNT_E_UNSUPPORTED_FORMAT.
 */
static
HRESULT
OpenClient(
    IMMDevice *Device,
    const WAVEFORMATEX *Format,
    REFERENCE_TIME Duration,
    DWORD Flags,
    REFIID ServiceId,
    _Out_ void **Service,
    _Out_ IAudioClient **Client,
    _Out_ HANDLE *Event)
{
    HRESULT hr;

    *Service = NULL;
    *Client = NULL;
    *Event = NULL;
    hr = IMMDevice_Activate(Device, &IID_IAudioClient, CLSCTX_ALL, NULL, (void **)Client);
    if (FAILED(hr))
        return hr;
    hr = IAudioClient_Initialize(*Client, AUDCLNT_SHAREMODE_SHARED, Flags | AUDCLNT_STREAMFLAGS_EVENTCALLBACK,
                                 Duration, 0, Format, NULL);
    if (FAILED(hr))
        goto Fail;
    *Event = CreateEventW(NULL, FALSE, FALSE, NULL);
    if (!*Event)
    {
        hr = E_OUTOFMEMORY;
        goto Fail;
    }
    hr = IAudioClient_SetEventHandle(*Client, *Event);
    if (FAILED(hr))
        goto Fail;
    hr = IAudioClient_GetService(*Client, ServiceId, Service);
    if (FAILED(hr))
        goto Fail;
    return S_OK;

Fail:
    if (*Event)
        CloseHandle(*Event);
    *Event = NULL;
    IAudioClient_Release(*Client);
    *Client = NULL;
    return hr;
}

static
VOID
CloseClient(
    IUnknown *Service,
    IAudioClient *Client,
    HANDLE Event)
{
    if (Service)
        IUnknown_Release(Service);
    if (Client)
        IAudioClient_Release(Client);
    if (Event)
        CloseHandle(Event);
}

/*
 * One run: the capture opened and started first, into a ring; the render
 * opened, its buffer filled with the tone's first frames and started; then
 * each wake of either event is served until the ring is full or the budget
 * is spent. The tone is written once, DSPLOOP_FRAMES frames of it, and
 * nothing after it. The comparison (compare.h) and its lines end the run.
 */
static
int
RunLoop(
    IMMDevice *RenderDevice,
    IMMDevice *CaptureDevice,
    WORD ValidBits,
    const char *Label)
{
    WAVEFORMATEXTENSIBLE Format;
    const WAVEFORMATEX *Wave = (const WAVEFORMATEX *)&Format;
    DSPLOOP_STREAM Render, Capture;
    DSPLOOP_MATCH Match;
    HANDLE Events[2];
    BYTE *Ring = NULL, *Data;
    UINT32 FrameBytes, Written = 0, Captured = 0, Next, Frames, Padding, Free, Take, First, Held;
    DWORD Flags, Deadline, Wait, Started = 0, Elapsed = 0;
    UINT64 DevicePosition, QpcPosition;
    HRESULT hr;
    int Result = DSPLOOP_FAILED;

    ZeroMemory(&Render, sizeof(Render));
    ZeroMemory(&Capture, sizeof(Capture));
    ToneFormat(&Format, ValidBits);
    FrameBytes = Wave->nBlockAlign;

    hr = OpenClient(CaptureDevice, Wave, DSPLOOP_CAPTURE_BUFFER, 0, &IID_IAudioCaptureClient, (void **)&Capture.Capture,
                    &Capture.Client, &Capture.Event);
    if (hr == AUDCLNT_E_UNSUPPORTED_FORMAT)
    {
        trace("dsploop%s: the shared-mode engine does not take the format for capture; skipped\n", Label);
        return DSPLOOP_SKIPPED;
    }
    ok(hr == S_OK, "dsploop%s: capture: Initialize 0x%08lx\n", Label, hr);
    if (FAILED(hr))
        goto Done;
    hr = OpenClient(RenderDevice, Wave, DSPLOOP_RENDER_BUFFER,
                    AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM | AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY,
                    &IID_IAudioRenderClient, (void **)&Render.Render, &Render.Client, &Render.Event);
    if (hr == AUDCLNT_E_UNSUPPORTED_FORMAT)
    {
        trace("dsploop%s: the shared-mode engine does not take the format for render; skipped\n", Label);
        CloseClient((IUnknown *)Capture.Capture, Capture.Client, Capture.Event);
        return DSPLOOP_SKIPPED;
    }
    ok(hr == S_OK, "dsploop%s: render: Initialize 0x%08lx\n", Label, hr);
    if (FAILED(hr))
        goto Done;
    hr = IAudioClient_GetBufferSize(Render.Client, &Render.Capacity);
    ok(hr == S_OK && Render.Capacity != 0, "dsploop%s: render: GetBufferSize 0x%08lx, %u frames\n", Label, hr, Render.Capacity);
    if (FAILED(hr) || Render.Capacity == 0)
        goto Done;

    Ring = (BYTE *)calloc(DSPLOOP_CAPTURE_FRAMES, FrameBytes);
    ok(Ring != NULL, "dsploop%s: no room for the capture\n", Label);
    if (!Ring)
        goto Done;

    hr = IAudioClient_Start(Capture.Client);
    ok(hr == S_OK, "dsploop%s: capture: Start 0x%08lx\n", Label, hr);
    if (FAILED(hr))
        goto Done;
    /* the tone fills the render buffer before the stream starts, and no more than the run's frames */
    First = Render.Capacity < DSPLOOP_FRAMES ? Render.Capacity : DSPLOOP_FRAMES;
    hr = IAudioRenderClient_GetBuffer(Render.Render, First, &Data);
    ok(hr == S_OK, "dsploop%s: render: GetBuffer(%u) 0x%08lx\n", Label, First, hr);
    if (FAILED(hr))
        goto Stopped;
    StoreTone(Data, 0, First, ValidBits, FrameBytes);
    hr = IAudioRenderClient_ReleaseBuffer(Render.Render, First, 0);
    ok(hr == S_OK, "dsploop%s: render: ReleaseBuffer(%u) 0x%08lx\n", Label, First, hr);
    if (FAILED(hr))
        goto Stopped;
    Written = First;
    hr = IAudioClient_Start(Render.Client);
    ok(hr == S_OK, "dsploop%s: render: Start 0x%08lx\n", Label, hr);
    if (FAILED(hr))
        goto Stopped;

    Events[0] = Capture.Event;
    Events[1] = Render.Event;
    Started = GetTickCount();
    Deadline = Started + DSPLOOP_RUN_MS;
    while (Captured < DSPLOOP_CAPTURE_FRAMES && (LONG)(GetTickCount() - Deadline) < 0)
    {
        Wait = WaitForMultipleObjects(2, Events, FALSE, 100);
        ok(Wait == WAIT_OBJECT_0 || Wait == WAIT_OBJECT_0 + 1 || Wait == WAIT_TIMEOUT, "dsploop%s: wait %lu\n", Label, Wait);
        if (Wait == WAIT_OBJECT_0)
        {
            /* every packet that is ready goes into the ring, silence included; past the ring's end nothing more is read */
            for (;;)
            {
                if (Captured >= DSPLOOP_CAPTURE_FRAMES)
                    break;
                Next = 0;
                hr = IAudioCaptureClient_GetNextPacketSize(Capture.Capture, &Next);
                if (FAILED(hr) || Next == 0)
                    break;
                hr = IAudioCaptureClient_GetBuffer(Capture.Capture, &Data, &Frames, &Flags, &DevicePosition, &QpcPosition);
                if (FAILED(hr))
                    break;
                Take = Frames < DSPLOOP_CAPTURE_FRAMES - Captured ? Frames : DSPLOOP_CAPTURE_FRAMES - Captured;
                if (Flags & AUDCLNT_BUFFERFLAGS_SILENT)
                    ZeroMemory(Ring + (size_t)Captured * FrameBytes, (size_t)Take * FrameBytes);
                else
                    memcpy(Ring + (size_t)Captured * FrameBytes, Data, (size_t)Take * FrameBytes);
                hr = IAudioCaptureClient_ReleaseBuffer(Capture.Capture, Frames);
                if (FAILED(hr))
                    break;
                /* a packet cut by the ring's end is released whole: its frames past the end are not the run's */
                Captured += Take;
            }
            if (FAILED(hr))
                ok(hr == S_OK, "dsploop%s: capture: 0x%08lx after %u frames\n", Label, hr, Captured);
        }
        else if (Wait == WAIT_OBJECT_0 + 1 && Written < DSPLOOP_FRAMES)
        {
            /* the tone continues into the free frames of the buffer, and no further */
            hr = IAudioClient_GetCurrentPadding(Render.Client, &Padding);
            if (SUCCEEDED(hr))
            {
                Free = Padding < Render.Capacity ? Render.Capacity - Padding : 0;
                if (Free > DSPLOOP_FRAMES - Written)
                    Free = DSPLOOP_FRAMES - Written;
                if (Free != 0)
                {
                    hr = IAudioRenderClient_GetBuffer(Render.Render, Free, &Data);
                    if (SUCCEEDED(hr))
                    {
                        StoreTone(Data, Written, Free, ValidBits, FrameBytes);
                        hr = IAudioRenderClient_ReleaseBuffer(Render.Render, Free, 0);
                        if (SUCCEEDED(hr))
                            Written += Free;
                    }
                }
            }
            if (FAILED(hr))
                ok(hr == S_OK, "dsploop%s: render: 0x%08lx after %u frames\n", Label, hr, Written);
        }
    }
    Elapsed = GetTickCount() - Started;

Stopped:
    IAudioClient_Stop(Render.Client);
    IAudioClient_Stop(Capture.Client);
    trace("dsploop%s: %u frames of the tone rendered, %u frames captured in %lu ms\n", Label, Written, Captured, Elapsed);

    Held = Captured < DSPLOOP_CAPTURE_FRAMES ? Captured : DSPLOOP_CAPTURE_FRAMES;
    DsploopCompare(Ring, Held, DSPLOOP_CHANNELS, Wave->wBitsPerSample, ValidBits, DSPLOOP_FRAMES, DSPLOOP_MAX_OFFSET, &Match);
    if (!Match.Matched)
    {
        if (Match.FirstDifference == DSPLOOP_NO_DIFFERENCE)
            trace("dsploop%s: %u frames captured, too few to hold the tone's first cycle\n", Label, Held);
        else
            trace("dsploop%s: no frame within %u matches the tone's first cycle; from the first frame, first differing sample %u (channel %u): expected %d, captured %d\n",
                  Label, (UINT32)DSPLOOP_MAX_OFFSET, Match.FirstDifference, Match.Channel, Match.Expected, Match.Captured);
        ok(0, "dsploop%s: the tone was not captured (%u frames)\n", Label, Held);
    }
    else
    {
        trace("dsploop%s: %u frames compared, max abs error %I64d, offset %u%s\n", Label, Match.Compared, Match.MaxError, Match.Offset,
              Match.FirstDifference == DSPLOOP_NO_DIFFERENCE ? ", ok" : "");
        if (Match.FirstDifference != DSPLOOP_NO_DIFFERENCE)
            trace("dsploop%s: first differing sample %u after the offset (channel %u): expected %d, captured %d\n",
                  Label, Match.FirstDifference, Match.Channel, Match.Expected, Match.Captured);
        ok(Match.FirstDifference == DSPLOOP_NO_DIFFERENCE && Match.Compared == DSPLOOP_FRAMES,
           "dsploop%s: %u of %u frames compared, max abs error %I64d, first differing sample %u\n",
           Label, Match.Compared, (UINT32)DSPLOOP_FRAMES, Match.MaxError, Match.FirstDifference);
        if (Match.FirstDifference == DSPLOOP_NO_DIFFERENCE && Match.Compared == DSPLOOP_FRAMES)
            Result = DSPLOOP_OK;
    }

Done:
    free(Ring);
    CloseClient((IUnknown *)Render.Render, Render.Client, Render.Event);
    CloseClient((IUnknown *)Capture.Capture, Capture.Client, Capture.Event);
    return Result;
}

START_TEST(dsploop)
{
    /* 16 valid bits is the run the plan asks for; 24 in a 32-bit word and 32 follow when the shared-mode engine takes them */
    static const struct
    {
        WORD ValidBits;
        const char *Label;
    } Runs[] =
    {
        { 16, "" },
        { 24, " (24 valid in 32)" },
        { 32, " (32)" }
    };
    IMMDeviceEnumerator *Enumerator = NULL;
    IMMDevice *Render = NULL, *Capture = NULL;
    HRESULT hr;
    unsigned I;
    int Result;

    hr = CoInitializeEx(NULL, COINIT_MULTITHREADED);
    ok(SUCCEEDED(hr), "CoInitializeEx: 0x%08lx\n", hr);
    if (FAILED(hr))
        return;
    hr = CoCreateInstance(&CLSID_MMDeviceEnumerator, NULL, CLSCTX_ALL, &IID_IMMDeviceEnumerator, (void **)&Enumerator);
    ok(hr == S_OK, "Create enumerator: 0x%08lx\n", hr);
    if (FAILED(hr))
        goto Done;
    hr = FindEndpoint(Enumerator, eRender, &Render);
    if (FAILED(hr))
    {
        apitest_missing("dsploop: no active render endpoint named '%ls'; the DSP's audio child is not present (0x%08lx)\n", DSPLOOP_NAME, hr);
        goto Done;
    }
    hr = FindEndpoint(Enumerator, eCapture, &Capture);
    if (FAILED(hr))
    {
        apitest_missing("dsploop: no active capture endpoint named '%ls'; the DSP's audio child is not present (0x%08lx)\n", DSPLOOP_NAME, hr);
        goto Done;
    }
    for (I = 0; I < ARRAY_SIZE(Runs); I++)
    {
        Result = RunLoop(Render, Capture, Runs[I].ValidBits, Runs[I].Label);
        /* the plan's stop rule: a run that does not match ends the test at its first differing sample; a format the engine refuses is not a mismatch */
        if (Result == DSPLOOP_FAILED)
        {
            trace("dsploop: stopped after the run above, the plan's stop rule\n");
            break;
        }
    }

Done:
    if (Capture)
        IMMDevice_Release(Capture);
    if (Render)
        IMMDevice_Release(Render);
    if (Enumerator)
        IMMDeviceEnumerator_Release(Enumerator);
    CoUninitialize();
}
