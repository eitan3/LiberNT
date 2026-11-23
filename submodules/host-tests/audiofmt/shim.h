/*
 * PROJECT:     LiberNT host-native tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Environment for the audio format rule tests: the Windows types the shared headers use
 * COPYRIGHT:   Copyright 2026 LiberNT contributors
 */

#ifndef AUDIOFMT_TEST_SHIM_H
#define AUDIOFMT_TEST_SHIM_H

#include <stdint.h>
#include <string.h>

#define _In_
#define _Out_

typedef uint8_t BYTE, *PBYTE, UCHAR;
typedef uint16_t WORD, USHORT;
typedef uint32_t DWORD, ULONG, *PULONG;
typedef int32_t INT32, LONG;
typedef uint32_t UINT32;
typedef int64_t INT64, LONGLONG;
typedef uint64_t UINT64, ULONGLONG;
typedef uint32_t UINT;
typedef int BOOL;
typedef void *PVOID;

#ifndef TRUE
#define TRUE 1
#endif
#ifndef FALSE
#define FALSE 0
#endif

#define MAXUSHORT 0xffff
#define MAXULONG  0xffffffffUL

static inline void *RtlZeroMemory(void *dst, size_t n) { return memset(dst, 0, n); }
static inline void *RtlCopyMemory(void *dst, const void *src, size_t n) { return memcpy(dst, src, n); }

/* mmsystem.h */
#define WAVE_FORMAT_PCM        1
#define WAVE_FORMAT_IEEE_FLOAT 3
#define WAVE_FORMAT_1M08       0x00000001
#define WAVE_FORMAT_1S08       0x00000002
#define WAVE_FORMAT_1M16       0x00000004
#define WAVE_FORMAT_1S16       0x00000008
#define WAVE_FORMAT_2M08       0x00000010
#define WAVE_FORMAT_2S08       0x00000020
#define WAVE_FORMAT_2M16       0x00000040
#define WAVE_FORMAT_2S16       0x00000080
#define WAVE_FORMAT_4M08       0x00000100
#define WAVE_FORMAT_4S08       0x00000200
#define WAVE_FORMAT_4M16       0x00000400
#define WAVE_FORMAT_4S16       0x00000800
#define WAVE_FORMAT_48M08      0x00001000
#define WAVE_FORMAT_48S08      0x00002000
#define WAVE_FORMAT_48M16      0x00004000
#define WAVE_FORMAT_48S16      0x00008000
#define WAVE_FORMAT_96M08      0x00010000
#define WAVE_FORMAT_96S08      0x00020000
#define WAVE_FORMAT_96M16      0x00040000
#define WAVE_FORMAT_96S16      0x00080000

/* mmreg.h */
#define WAVE_FORMAT_EXTENSIBLE 0xfffe

/* ksmedia.h */
#define SPEAKER_FRONT_LEFT              0x1
#define SPEAKER_FRONT_RIGHT             0x2
#define SPEAKER_FRONT_CENTER            0x4
#define SPEAKER_LOW_FREQUENCY           0x8
#define SPEAKER_BACK_LEFT               0x10
#define SPEAKER_BACK_RIGHT              0x20
#define SPEAKER_FRONT_LEFT_OF_CENTER    0x40
#define SPEAKER_FRONT_RIGHT_OF_CENTER   0x80
#define KSAUDIO_SPEAKER_DIRECTOUT       0
#define KSAUDIO_SPEAKER_MONO            (SPEAKER_FRONT_CENTER)
#define KSAUDIO_SPEAKER_STEREO          (SPEAKER_FRONT_LEFT | SPEAKER_FRONT_RIGHT)
#define KSAUDIO_SPEAKER_QUAD            (SPEAKER_FRONT_LEFT | SPEAKER_FRONT_RIGHT | \
                                         SPEAKER_BACK_LEFT  | SPEAKER_BACK_RIGHT)
#define KSAUDIO_SPEAKER_5POINT1         (SPEAKER_FRONT_LEFT | SPEAKER_FRONT_RIGHT | \
                                         SPEAKER_FRONT_CENTER | SPEAKER_LOW_FREQUENCY | \
                                         SPEAKER_BACK_LEFT  | SPEAKER_BACK_RIGHT)
#define KSAUDIO_SPEAKER_7POINT1         (SPEAKER_FRONT_LEFT | SPEAKER_FRONT_RIGHT | \
                                         SPEAKER_FRONT_CENTER | SPEAKER_LOW_FREQUENCY | \
                                         SPEAKER_BACK_LEFT | SPEAKER_BACK_RIGHT | \
                                         SPEAKER_FRONT_LEFT_OF_CENTER | SPEAKER_FRONT_RIGHT_OF_CENTER)

#pragma pack(push, 1)
typedef struct _GUID
{
    uint32_t Data1;
    uint16_t Data2;
    uint16_t Data3;
    uint8_t Data4[8];
} GUID;

/* {00000001-0000-0010-8000-00aa00389b71} */
static const GUID KSDATAFORMAT_SUBTYPE_PCM =
    { 0x00000001, 0x0000, 0x0010, { 0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71 } };

typedef struct _WAVEFORMATEX
{
    WORD wFormatTag;
    WORD nChannels;
    DWORD nSamplesPerSec;
    DWORD nAvgBytesPerSec;
    WORD nBlockAlign;
    WORD wBitsPerSample;
    WORD cbSize;
} WAVEFORMATEX, *PWAVEFORMATEX;

typedef struct
{
    WORD wValidBitsPerSample;
    WORD wSamplesPerBlock;
    DWORD dwChannelMask;
} WAVEFORMATEXTENSIBLE_SAMPLE;

typedef struct _WAVEFORMATEXTENSIBLE
{
    WAVEFORMATEX Format;
    union
    {
        WORD wValidBitsPerSample;
        WORD wSamplesPerBlock;
        DWORD dwChannelMask;
    } Samples;
    DWORD dwChannelMask;
    GUID SubFormat;
} WAVEFORMATEXTENSIBLE, *PWAVEFORMATEXTENSIBLE;

typedef struct _KSDATARANGE
{
    ULONG FormatSize;
    ULONG Flags;
    ULONG SampleSize;
    ULONG Reserved;
    GUID MajorFormat;
    GUID SubFormat;
    GUID Specifier;
} KSDATARANGE;

typedef struct
{
    KSDATARANGE DataRange;
    ULONG MaximumChannels;
    ULONG MinimumBitsPerSample;
    ULONG MaximumBitsPerSample;
    ULONG MinimumSampleFrequency;
    ULONG MaximumSampleFrequency;
} KSDATARANGE_AUDIO;
#pragma pack(pop)

#endif /* AUDIOFMT_TEST_SHIM_H */
