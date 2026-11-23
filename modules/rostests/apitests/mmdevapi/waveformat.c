/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests that WDMAUD answers whether a driver accepts a wave format
 */

#include <apitest.h>
#include <wingdi.h>
#include <winuser.h>
#include <winreg.h>
#include <winioctl.h>
#include <mmsystem.h>
#include <mmreg.h>
#include <cfgmgr32.h>
#include <setupapi.h>
#include <ks.h>
#include <ksmedia.h>
#include <interface.h>

static const GUID WdmaudCategory = {STATIC_KSCATEGORY_WDMAUD};

static HANDLE OpenWdmaud(void)
{
    SP_DEVICE_INTERFACE_DETAIL_DATA_W *Detail;
    SP_DEVICE_INTERFACE_DATA Interface;
    HANDLE Handle = INVALID_HANDLE_VALUE;
    HDEVINFO Devices;
    DWORD Size;

    Devices = SetupDiGetClassDevsW(&WdmaudCategory, NULL, NULL,
                                   DIGCF_DEVICEINTERFACE | DIGCF_PRESENT);
    if (Devices == INVALID_HANDLE_VALUE)
        return INVALID_HANDLE_VALUE;

    Interface.cbSize = sizeof(Interface);
    Size = sizeof(*Detail) + MAX_PATH * sizeof(WCHAR);
    Detail = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, Size);
    if (Detail &&
        SetupDiEnumDeviceInterfaces(Devices, NULL, &WdmaudCategory, 0, &Interface))
    {
        Detail->cbSize = sizeof(*Detail);
        if (SetupDiGetDeviceInterfaceDetailW(Devices, &Interface, Detail, Size, NULL, NULL))
        {
            WCHAR *Path = Detail->DevicePath;

            if (Path[0] == L'\\' && Path[1] == L'?')
                Path[1] = L'\\';
            Handle = CreateFileW(Path, GENERIC_READ | GENERIC_WRITE,
                                 0, NULL, OPEN_EXISTING, 0, NULL);
        }
    }

    if (Detail)
        HeapFree(GetProcessHeap(), 0, Detail);
    SetupDiDestroyDeviceInfoList(Devices);
    return Handle;
}

static BOOL Ioctl(HANDLE Wdmaud, DWORD Code, WDMAUD_DEVICE_INFO *Info)
{
    DWORD Returned;

    return DeviceIoControl(Wdmaud, Code, Info, sizeof(*Info), Info, sizeof(*Info),
                           &Returned, NULL);
}

static void SetPcmFormat(WAVEFORMATEXTENSIBLE *Format, DWORD Rate, WORD ContainerBits, WORD ValidBits, WORD Channels)
{
    ZeroMemory(Format, sizeof(*Format));
    Format->Format.wFormatTag = WAVE_FORMAT_EXTENSIBLE;
    Format->Format.nChannels = Channels;
    Format->Format.nSamplesPerSec = Rate;
    Format->Format.wBitsPerSample = ContainerBits;
    Format->Format.nBlockAlign = Channels * ContainerBits / 8;
    Format->Format.nAvgBytesPerSec = Rate * Format->Format.nBlockAlign;
    Format->Format.cbSize = sizeof(*Format) - sizeof(Format->Format);
    Format->Samples.wValidBitsPerSample = ValidBits;
    Format->dwChannelMask = Channels == 1 ? SPEAKER_FRONT_CENTER : SPEAKER_FRONT_LEFT | SPEAKER_FRONT_RIGHT;
    Format->SubFormat = KSDATAFORMAT_SUBTYPE_PCM;
}

/* 1 when the driver accepts the format, 0 when it refuses it, -1 when it cannot be asked */
static INT QueryWaveFormat(HANDLE Wdmaud, SOUND_DEVICE_TYPE Type, ULONG Index, const WAVEFORMATEXTENSIBLE *Format)
{
    WDMAUD_DEVICE_INFO Info;

    ZeroMemory(&Info, sizeof(Info));
    Info.DeviceType = Type;
    Info.DeviceIndex = Index;
    Info.u.WaveFormatExtensible = *Format;
    if (!Ioctl(Wdmaud, IOCTL_QUERY_WAVE_FORMAT, &Info))
        return -1;
    return Info.Flags ? 1 : 0;
}

static void TestDevice(HANDLE Wdmaud, SOUND_DEVICE_TYPE Type, ULONG Index)
{
    WAVEFORMATEXTENSIBLE Format, Preferred;
    KSDATARANGE_AUDIO Range;
    WDMAUD_DEVICE_INFO Info;
    INT Answer;
    WORD Channels;
    ULONG Rate;

    ZeroMemory(&Info, sizeof(Info));
    Info.DeviceType = Type;
    Info.DeviceIndex = Index;
    if (!Ioctl(Wdmaud, IOCTL_GETWAVE_DATARANGE, &Info))
    {
        skip("Type %u index %lu: IOCTL_GETWAVE_DATARANGE failed with %lu\n", Type, Index, GetLastError());
        return;
    }
    Range = Info.u.WaveDataRange;
    ok(Range.DataRange.FormatSize == sizeof(KSDATARANGE_AUDIO), "FormatSize %lu\n", Range.DataRange.FormatSize);
    ok(IsEqualGUID(&Range.DataRange.MajorFormat, &KSDATAFORMAT_TYPE_AUDIO), "MajorFormat is not AUDIO\n");
    ok(IsEqualGUID(&Range.DataRange.SubFormat, &KSDATAFORMAT_SUBTYPE_PCM), "SubFormat is not PCM\n");
    ok(Range.MaximumChannels >= 1, "MaximumChannels %lu\n", Range.MaximumChannels);
    ok(Range.MinimumBitsPerSample >= 8 && Range.MaximumBitsPerSample <= 32 &&
       Range.MinimumBitsPerSample <= Range.MaximumBitsPerSample,
       "bits %lu..%lu\n", Range.MinimumBitsPerSample, Range.MaximumBitsPerSample);
    ok(Range.MinimumSampleFrequency >= 8000 && Range.MaximumSampleFrequency <= 384000 &&
       Range.MinimumSampleFrequency <= Range.MaximumSampleFrequency,
       "rates %lu..%lu\n", Range.MinimumSampleFrequency, Range.MaximumSampleFrequency);
    trace("Type %u index %lu: %lu channels, bits %lu..%lu, rates %lu..%lu\n", Type, Index, Range.MaximumChannels,
          Range.MinimumBitsPerSample, Range.MaximumBitsPerSample, Range.MinimumSampleFrequency, Range.MaximumSampleFrequency);

    ZeroMemory(&Info, sizeof(Info));
    Info.DeviceType = Type;
    Info.DeviceIndex = Index;
    ok(Ioctl(Wdmaud, IOCTL_GETPREFERRED_WAVE_FORMAT, &Info), "IOCTL_GETPREFERRED_WAVE_FORMAT failed with %lu\n", GetLastError());
    Preferred = Info.u.WaveFormatExtensible;

    Channels = min(Range.MaximumChannels, 2);
    Rate = Range.MinimumSampleFrequency <= 48000 && Range.MaximumSampleFrequency >= 48000 ? 48000 : Range.MinimumSampleFrequency;

    /* The legacy table format every wave device accepts */
    SetPcmFormat(&Format, Rate, 16, 16, Channels);
    Answer = QueryWaveFormat(Wdmaud, Type, Index, &Format);
    ok(Answer == 1, "Type %u index %lu: 16-bit at %lu Hz refused, answer %d error %lu\n", Type, Index, Rate, Answer, GetLastError());

    /* The driver's own preferred format */
    Answer = QueryWaveFormat(Wdmaud, Type, Index, &Preferred);
    ok(Answer == 1, "Type %u index %lu: the preferred format refused, answer %d error %lu\n", Type, Index, Answer, GetLastError());

    /* A rate outside the advertised range */
    SetPcmFormat(&Format, 12345, 16, 16, Channels);
    Answer = QueryWaveFormat(Wdmaud, Type, Index, &Format);
    ok(Answer == 0, "Type %u index %lu: 12345 Hz accepted, answer %d error %lu\n", Type, Index, Answer, GetLastError());

    /* A channel count outside the advertised range */
    SetPcmFormat(&Format, Rate, 16, 16, (WORD)(Range.MaximumChannels + 1));
    Answer = QueryWaveFormat(Wdmaud, Type, Index, &Format);
    ok(Answer == 0, "Type %u index %lu: %lu channels accepted, answer %d error %lu\n", Type, Index, Range.MaximumChannels + 1, Answer, GetLastError());

    /* A valid-bit count outside the advertised range */
    SetPcmFormat(&Format, Rate, 32, (WORD)(Range.MaximumBitsPerSample == 32 ? 20 : 32), Channels);
    if (Range.MaximumBitsPerSample != 32 || Range.MinimumBitsPerSample > 20)
    {
        Answer = QueryWaveFormat(Wdmaud, Type, Index, &Format);
        ok(Answer == 0, "Type %u index %lu: %u valid bits accepted, answer %d error %lu\n", Type, Index,
           Format.Samples.wValidBitsPerSample, Answer, GetLastError());
    }

    /* 24 valid bits in a 32-bit container: accepted exactly when the range covers 24 bits */
    SetPcmFormat(&Format, Rate, 32, 24, Channels);
    Answer = QueryWaveFormat(Wdmaud, Type, Index, &Format);
    trace("Type %u index %lu: 24 bit in 32-bit container answer %d\n", Type, Index, Answer);
    if (Range.MinimumBitsPerSample <= 24 && Range.MaximumBitsPerSample >= 24)
        ok(Answer == 1, "Type %u index %lu: 24-in-32 refused although the range covers 24 bits, answer %d error %lu\n", Type, Index, Answer, GetLastError());
    else
        ok(Answer == 0, "Type %u index %lu: 24-in-32 accepted although the range is %lu..%lu bits, answer %d\n", Type, Index,
           Range.MinimumBitsPerSample, Range.MaximumBitsPerSample, Answer);

    /* 32 valid bits: accepted exactly when the range covers 32 bits */
    SetPcmFormat(&Format, Rate, 32, 32, Channels);
    Answer = QueryWaveFormat(Wdmaud, Type, Index, &Format);
    trace("Type %u index %lu: 32 bit answer %d\n", Type, Index, Answer);
    if (Range.MaximumBitsPerSample >= 32)
        ok(Answer == 1, "Type %u index %lu: 32-bit refused although the range covers 32 bits, answer %d error %lu\n", Type, Index, Answer, GetLastError());
    else
        ok(Answer == 0, "Type %u index %lu: 32-bit accepted although the range stops at %lu bits, answer %d\n", Type, Index, Range.MaximumBitsPerSample, Answer);

    /* A packed 24-bit container never reaches the hardware: the controller has no 3-byte word */
    SetPcmFormat(&Format, Rate, 24, 24, Channels);
    Answer = QueryWaveFormat(Wdmaud, Type, Index, &Format);
    ok(Answer == 0, "Type %u index %lu: a packed 24-bit container was accepted, answer %d\n", Type, Index, Answer);

    /* 20 valid bits in a 32-bit container: accepted exactly when the range covers 20 bits */
    SetPcmFormat(&Format, Rate, 32, 20, Channels);
    Answer = QueryWaveFormat(Wdmaud, Type, Index, &Format);
    trace("Type %u index %lu: 20 bit in 32-bit container answer %d\n", Type, Index, Answer);
    if (Range.MinimumBitsPerSample <= 20 && Range.MaximumBitsPerSample >= 20)
        ok(Answer == 1, "Type %u index %lu: 20-in-32 refused although the range covers 20 bits, answer %d error %lu\n", Type, Index, Answer, GetLastError());
    else
        ok(Answer == 0, "Type %u index %lu: 20-in-32 accepted although the range is %lu..%lu bits, answer %d\n", Type, Index,
           Range.MinimumBitsPerSample, Range.MaximumBitsPerSample, Answer);

    /* A format the no driver can carry: valid bits beyond the container */
    SetPcmFormat(&Format, Rate, 16, 24, Channels);
    Answer = QueryWaveFormat(Wdmaud, Type, Index, &Format);
    ok(Answer <= 0, "Type %u index %lu: 24 valid bits in a 16-bit container accepted, answer %d\n", Type, Index, Answer);

    /* A malformed request is rejected, not answered */
    ZeroMemory(&Info, sizeof(Info));
    Info.DeviceType = Type;
    Info.DeviceIndex = Index;
    Info.u.WaveFormatEx.wFormatTag = WAVE_FORMAT_PCM;
    Info.u.WaveFormatEx.cbSize = 7;
    ok(!Ioctl(Wdmaud, IOCTL_QUERY_WAVE_FORMAT, &Info), "a malformed format was accepted\n");

    ZeroMemory(&Info, sizeof(Info));
    Info.DeviceType = MIDI_OUT_DEVICE_TYPE;
    Info.DeviceIndex = Index;
    ok(!Ioctl(Wdmaud, IOCTL_QUERY_WAVE_FORMAT, &Info), "a MIDI device answered a wave format query\n");
    ok(!Ioctl(Wdmaud, IOCTL_GETWAVE_DATARANGE, &Info), "a MIDI device answered a wave data range query\n");
}

START_TEST(waveformat)
{
    SOUND_DEVICE_TYPE Type;
    WDMAUD_DEVICE_INFO Info;
    HANDLE Wdmaud;

    Wdmaud = OpenWdmaud();
    if (Wdmaud == INVALID_HANDLE_VALUE)
    {
        skip("No WDMAUD interface\n");
        return;
    }

    for (Type = WAVE_OUT_DEVICE_TYPE; Type <= WAVE_IN_DEVICE_TYPE; Type++)
    {
        ULONG Index, Count;

        ZeroMemory(&Info, sizeof(Info));
        Info.DeviceType = Type;
        if (!Ioctl(Wdmaud, IOCTL_GETNUMDEVS_TYPE, &Info))
        {
            skip("Type %u: IOCTL_GETNUMDEVS_TYPE failed with %lu\n", Type, GetLastError());
            continue;
        }
        Count = min(Info.DeviceCount, 4);
        if (!Count)
            skip("Type %u: no device\n", Type);
        for (Index = 0; Index < Count; Index++)
            TestDevice(Wdmaud, Type, Index);
    }
    CloseHandle(Wdmaud);
}
