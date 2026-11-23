/*
 * PROJECT:     ReactOS Sound System Helpers
 * LICENSE:     GPL - See COPYING in the top level directory
 */

#ifndef ROS_AUDIO_WAVEFMT_H
#define ROS_AUDIO_WAVEFMT_H

#include <windef.h>
#include <limits.h>
#include <mmsystem.h>
#include <mmreg.h>
#include <ks.h>
#include <ksmedia.h>

typedef struct _ROS_SOUND_LEGACY_WAVE_FORMAT
{
    DWORD Flag;
    DWORD SamplesPerSec;
    WORD BitsPerSample;
    WORD Channels;
} ROS_SOUND_LEGACY_WAVE_FORMAT, *PROS_SOUND_LEGACY_WAVE_FORMAT;

static const ROS_SOUND_LEGACY_WAVE_FORMAT RosSoundLegacyWaveFormats[] =
{
    { WAVE_FORMAT_1M08,  11025,  8, 1 },
    { WAVE_FORMAT_1M16,  11025, 16, 1 },
    { WAVE_FORMAT_1S08,  11025,  8, 2 },
    { WAVE_FORMAT_1S16,  11025, 16, 2 },
    { WAVE_FORMAT_2M08,  22050,  8, 1 },
    { WAVE_FORMAT_2M16,  22050, 16, 1 },
    { WAVE_FORMAT_2S08,  22050,  8, 2 },
    { WAVE_FORMAT_2S16,  22050, 16, 2 },
    { WAVE_FORMAT_4M08,  44100,  8, 1 },
    { WAVE_FORMAT_4M16,  44100, 16, 1 },
    { WAVE_FORMAT_4S08,  44100,  8, 2 },
    { WAVE_FORMAT_4S16,  44100, 16, 2 },
    { WAVE_FORMAT_48M08, 48000,  8, 1 },
    { WAVE_FORMAT_48M16, 48000, 16, 1 },
    { WAVE_FORMAT_48S08, 48000,  8, 2 },
    { WAVE_FORMAT_48S16, 48000, 16, 2 },
    { WAVE_FORMAT_96M08, 96000,  8, 1 },
    { WAVE_FORMAT_96M16, 96000, 16, 1 },
    { WAVE_FORMAT_96S08, 96000,  8, 2 },
    { WAVE_FORMAT_96S16, 96000, 16, 2 },
    { 0, 0, 0, 0 }
};

static inline DWORD
RosSoundWaveFormatFieldsToLegacyFlag(
    _In_ DWORD SamplesPerSec,
    _In_ WORD BitsPerSample,
    _In_ WORD Channels)
{
    const ROS_SOUND_LEGACY_WAVE_FORMAT *Format;

    for (Format = RosSoundLegacyWaveFormats; Format->Flag; Format++)
    {
        if (Format->SamplesPerSec == SamplesPerSec &&
            Format->BitsPerSample == BitsPerSample &&
            Format->Channels == Channels)
        {
            return Format->Flag;
        }
    }

    return 0;
}

static inline BOOL
RosSoundLegacyFlagToWaveFormatFields(
    _In_ DWORD Flag,
    _Out_ DWORD *SamplesPerSec,
    _Out_ WORD *BitsPerSample,
    _Out_ WORD *Channels)
{
    const ROS_SOUND_LEGACY_WAVE_FORMAT *Format;

    for (Format = RosSoundLegacyWaveFormats; Format->Flag; Format++)
    {
        if (Format->Flag == Flag)
        {
            *SamplesPerSec = Format->SamplesPerSec;
            *BitsPerSample = Format->BitsPerSample;
            *Channels = Format->Channels;
            return TRUE;
        }
    }

    return FALSE;
}

static inline DWORD
RosSoundWaveFormatToLegacyFlag(
    _In_ const WAVEFORMATEX *WaveFormat)
{
    if (!WaveFormat || WaveFormat->wFormatTag != WAVE_FORMAT_PCM)
        return 0;

    return RosSoundWaveFormatFieldsToLegacyFlag(WaveFormat->nSamplesPerSec,
                                                WaveFormat->wBitsPerSample,
                                                WaveFormat->nChannels);
}

static inline DWORD
RosSoundAudioRangeToLegacyFlags(
    _In_ DWORD MinimumSampleFrequency,
    _In_ DWORD MaximumSampleFrequency,
    _In_ WORD MinimumBitsPerSample,
    _In_ WORD MaximumBitsPerSample,
    _In_ WORD MaximumChannels)
{
    const ROS_SOUND_LEGACY_WAVE_FORMAT *Format;
    DWORD Flags = 0;

    for (Format = RosSoundLegacyWaveFormats; Format->Flag; Format++)
    {
        if (MinimumSampleFrequency <= Format->SamplesPerSec &&
            MaximumSampleFrequency >= Format->SamplesPerSec &&
            MinimumBitsPerSample <= Format->BitsPerSample &&
            MaximumBitsPerSample >= Format->BitsPerSample &&
            MaximumChannels >= Format->Channels)
        {
            Flags |= Format->Flag;
        }
    }

    return Flags;
}

static inline DWORD
RosSoundChooseBestLegacyFormat(
    _In_ DWORD Flags)
{
    static const DWORD PreferredFormats[] =
    {
        WAVE_FORMAT_48S16,
        WAVE_FORMAT_48M16,
        WAVE_FORMAT_4S16,
        WAVE_FORMAT_4M16,
        WAVE_FORMAT_96S16,
        WAVE_FORMAT_96M16,
        WAVE_FORMAT_2S16,
        WAVE_FORMAT_2M16,
        WAVE_FORMAT_1S16,
        WAVE_FORMAT_1M16,
        WAVE_FORMAT_48S08,
        WAVE_FORMAT_48M08,
        WAVE_FORMAT_4S08,
        WAVE_FORMAT_4M08,
        WAVE_FORMAT_96S08,
        WAVE_FORMAT_96M08,
        WAVE_FORMAT_2S08,
        WAVE_FORMAT_2M08,
        WAVE_FORMAT_1S08,
        WAVE_FORMAT_1M08
    };
    ULONG Index;

    for (Index = 0; Index < sizeof(PreferredFormats) / sizeof(PreferredFormats[0]); Index++)
    {
        if (Flags & PreferredFormats[Index])
            return PreferredFormats[Index];
    }

    return 0;
}

/*
 * The size of the container a PCM sample of ValidBits is carried in on the
 * device: 8, 16 or 32 bits. 20- and 24-bit samples go in a 32-bit container,
 * left-justified with the unused low bits zero, as the HD Audio controller
 * and the audio engine read them; a packed 3-byte container stays a client
 * format the engine converts from.
 */
static inline WORD
RosSoundPcmContainerBits(
    _In_ WORD ValidBits)
{
    if (ValidBits <= 8)
        return 8;
    if (ValidBits <= 16)
        return 16;
    return 32;
}

static inline DWORD
RosSoundChannelMaskFromCount(
    _In_ WORD Channels)
{
    switch (Channels)
    {
        case 1:
            return KSAUDIO_SPEAKER_MONO;
        case 2:
            return KSAUDIO_SPEAKER_STEREO;
        case 4:
            return KSAUDIO_SPEAKER_QUAD;
        case 6:
            return KSAUDIO_SPEAKER_5POINT1;
        case 8:
            return KSAUDIO_SPEAKER_7POINT1;
        default:
            return KSAUDIO_SPEAKER_DIRECTOUT;
    }
}

/*
 * The PCM format a pin's data range describes at its widest: the most
 * channels, the most valid bits in their container, at 48 kHz when the range
 * covers it, else at the range's top rate. Returns FALSE when the range
 * yields no format the WAVEFORMATEX fields can hold.
 */
static inline BOOL
RosSoundAudioRangeToPreferredFormat(
    _In_ const KSDATARANGE_AUDIO *DataRangeAudio,
    _Out_ WAVEFORMATEXTENSIBLE *Format)
{
    ULONG Channels = DataRangeAudio->MaximumChannels;
    ULONG ValidBits = DataRangeAudio->MaximumBitsPerSample;
    ULONG ContainerBits, BlockAlign, SampleRate;

    if (!Channels || Channels > USHRT_MAX || !ValidBits || ValidBits > USHRT_MAX)
        return FALSE;

    ContainerBits = RosSoundPcmContainerBits((WORD)ValidBits);
    BlockAlign = Channels * (ContainerBits / 8);
    if (!BlockAlign || BlockAlign > USHRT_MAX)
        return FALSE;

    if (DataRangeAudio->MinimumSampleFrequency <= 48000 &&
        DataRangeAudio->MaximumSampleFrequency >= 48000)
    {
        SampleRate = 48000;
    }
    else
    {
        SampleRate = DataRangeAudio->MaximumSampleFrequency;
    }

    if (!SampleRate || SampleRate > ULONG_MAX / BlockAlign)
        return FALSE;

    RtlZeroMemory(Format, sizeof(*Format));
    Format->Format.wFormatTag = WAVE_FORMAT_EXTENSIBLE;
    Format->Format.nChannels = (WORD)Channels;
    Format->Format.nSamplesPerSec = SampleRate;
    Format->Format.nAvgBytesPerSec = SampleRate * BlockAlign;
    Format->Format.nBlockAlign = (WORD)BlockAlign;
    Format->Format.wBitsPerSample = (WORD)ContainerBits;
    Format->Format.cbSize = sizeof(*Format) - sizeof(Format->Format);
    Format->Samples.wValidBitsPerSample = (WORD)ValidBits;
    Format->dwChannelMask = RosSoundChannelMaskFromCount((WORD)Channels);
    Format->SubFormat = KSDATAFORMAT_SUBTYPE_PCM;
    return TRUE;
}

#endif /* ROS_AUDIO_WAVEFMT_H */
