/*
 * PROJECT:     LiberNT Intel Smart Sound DSP driver
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Pure rules of a PCM wave format as the DSP endpoint drivers read it: what a KSDATAFORMAT names (rate, channels, valid bits, the sample's container) and whether it is well formed; shared by intcdsp.sys (the audio interface's format check), dsppcm.sys (its pins) and the host tests
 * COPYRIGHT:   Copyright 2026 LiberNT contributors
 */

#ifndef _DRIVERS_INTCDSP_AUDIOFORMAT_H_
#define _DRIVERS_INTCDSP_AUDIOFORMAT_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <ntddk.h>
#include <wdm.h>
/* the KS types and constants (KSDATAFORMAT, the format GUIDs' STATIC_ values); in C the GUID symbols themselves are declarations only (ks.h DEFINE_GUIDEX), so the constants are compared by value */
#include <portcls.h>
#include "audioif.h"
#include <hdaudio_format.h>

/*
 * The rules are the codec driver's (drivers/wdm/audio/drivers/hdaudio
 * stream.cpp HDAUDIO_ValidateDataFormat), said once for the endpoint
 * drivers: PCM as mmreg.h WAVE_FORMAT_PCM names it, or WAVE_FORMAT_EXTENSIBLE
 * whose subformat is PCM; a well-formed format carries a block alignment
 * of its channels times its container, a byte rate of that alignment times
 * its rate, and either no sample size or the alignment; and a sample's
 * valid bits live in the word HDAUDIO_SampleContainerBits names, so a
 * packed 24-bit stream is refused as the codec refuses it.
 */

/* The three format GUIDs of PCM as ksmedia.h gives their values (STATIC_KSDATAFORMAT_*) */
static const GUID IntelDspAudioTypeAudio = { STATIC_KSDATAFORMAT_TYPE_AUDIO };
static const GUID IntelDspAudioSubtypePcm = { STATIC_KSDATAFORMAT_SUBTYPE_PCM };
static const GUID IntelDspAudioSpecifierWaveFormatEx = { STATIC_KSDATAFORMAT_SPECIFIER_WAVEFORMATEX };

static __inline
BOOLEAN
IntelDspAudioGuidEqual(
    _In_ const GUID* First,
    _In_ const GUID* Second)
{
    return RtlCompareMemory(First, Second, sizeof(GUID)) == sizeof(GUID);
}

static __inline
NTSTATUS
IntelDspAudioFormatParse(
    _In_ const KSDATAFORMAT* DataFormat,
    _Out_ PULONG SampleRate,
    _Out_ PULONG Channels,
    _Out_ PULONG ValidBits,
    _Out_ PULONG ContainerBits)
{
    const KSDATAFORMAT_WAVEFORMATEX* Wave;
    const WAVEFORMATEX* Format;
    ULONG Container, Valid, BlockAlign, ExpectedSize;

    *SampleRate = 0;
    *Channels = 0;
    *ValidBits = 0;
    *ContainerBits = 0;
    if (DataFormat == NULL || DataFormat->FormatSize < sizeof(KSDATAFORMAT_WAVEFORMATEX))
        return STATUS_INVALID_PARAMETER;
    if (!IntelDspAudioGuidEqual(&DataFormat->MajorFormat, &IntelDspAudioTypeAudio) ||
        !IntelDspAudioGuidEqual(&DataFormat->Specifier, &IntelDspAudioSpecifierWaveFormatEx))
        return STATUS_NOT_SUPPORTED;
    if (!IntelDspAudioGuidEqual(&DataFormat->SubFormat, &IntelDspAudioSubtypePcm))
        return STATUS_NOT_SUPPORTED;
    Wave = (const KSDATAFORMAT_WAVEFORMATEX*)DataFormat;
    Format = &Wave->WaveFormatEx;
    if (Format->wFormatTag == WAVE_FORMAT_PCM)
    {
        ExpectedSize = sizeof(KSDATAFORMAT_WAVEFORMATEX);
        if (Format->cbSize != 0)
            return STATUS_INVALID_PARAMETER;
        Valid = Format->wBitsPerSample;
    }
    else if (Format->wFormatTag == WAVE_FORMAT_EXTENSIBLE)
    {
        const WAVEFORMATEXTENSIBLE* Extensible = (const WAVEFORMATEXTENSIBLE*)Format;

        ExpectedSize = sizeof(KSDATAFORMAT) + sizeof(WAVEFORMATEXTENSIBLE);
        if (DataFormat->FormatSize < ExpectedSize || Format->cbSize != sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX))
            return STATUS_INVALID_PARAMETER;
        if (!IntelDspAudioGuidEqual(&Extensible->SubFormat, &IntelDspAudioSubtypePcm))
            return STATUS_NOT_SUPPORTED;
        Valid = Extensible->Samples.wValidBitsPerSample;
    }
    else
    {
        return STATUS_NOT_SUPPORTED;
    }
    Container = Format->wBitsPerSample;
    if (DataFormat->FormatSize != ExpectedSize || Format->nChannels == 0 || Container == 0 || (Container & 7) != 0 ||
        Valid == 0 || Valid > Container)
        return STATUS_INVALID_PARAMETER;
    BlockAlign = (ULONG)Format->nChannels * (Container / 8);
    if (BlockAlign == 0 || BlockAlign > MAXUSHORT || Format->nBlockAlign != BlockAlign ||
        Format->nSamplesPerSec > MAXULONG / BlockAlign || Format->nAvgBytesPerSec != Format->nSamplesPerSec * BlockAlign ||
        (DataFormat->SampleSize != 0 && DataFormat->SampleSize != BlockAlign))
        return STATUS_INVALID_PARAMETER;
    /* the sample's word: a valid depth the controller has no word for is refused, not malformed */
    if (Container != HDAUDIO_SampleContainerBits(Valid))
        return STATUS_NOT_SUPPORTED;
    *SampleRate = Format->nSamplesPerSec;
    *Channels = Format->nChannels;
    *ValidBits = Valid;
    *ContainerBits = Container;
    return STATUS_SUCCESS;
}

/*
 * The index of the endpoint's format a stream format names, by its four
 * numbers; STATUS_NOT_SUPPORTED when the endpoint carries no such format.
 */
static __inline
NTSTATUS
IntelDspAudioFormatFind(
    _In_ const INTELDSP_ENDPOINT* Endpoint,
    ULONG SampleRate,
    ULONG Channels,
    ULONG ValidBits,
    ULONG ContainerBits,
    _Out_ PULONG Index)
{
    ULONG I;

    *Index = 0;
    for (I = 0; I < Endpoint->FormatCount && I < INTELDSP_ENDPOINT_FORMATS_MAX; I++)
    {
        const INTELDSP_FORMAT* Format = &Endpoint->Formats[I];

        if (Format->SampleRate == SampleRate && Format->Channels == Channels && Format->ValidBits == ValidBits &&
            Format->ContainerBits == ContainerBits)
        {
            *Index = I;
            return STATUS_SUCCESS;
        }
    }
    return STATUS_NOT_SUPPORTED;
}

#ifdef __cplusplus
}
#endif

#endif
