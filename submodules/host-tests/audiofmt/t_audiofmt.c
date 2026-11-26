/*
 * PROJECT:     LiberNT host-native tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Bit-exact checks of the shared audio format rules: sample containers, the
 *              preferred format of a data range, and the PCM sample conversion of the audio engine
 * COPYRIGHT:   Copyright 2026 LiberNT contributors
 */

#include <stdio.h>
#include <stdlib.h>

#include "shim.h"

#include <libernt/libs/sound/wavefmt.h>
#include "../../../drivers/wdm/audio/drivers/hdaudio/hdaudio_format.h"
#include "../../../dll/win32/mmdevapi/pcm_sample.h"

static int Failures;

#define CHECK(cond) \
    do { \
        if (!(cond)) { \
            fprintf(stderr, "%s:%d: FAIL: %s\n", __FILE__, __LINE__, #cond); \
            ++Failures; \
        } \
    } while (0)

#define CHECK_EQ(a, b) \
    do { \
        unsigned long long _a = (unsigned long long)(a), _b = (unsigned long long)(b); \
        if (_a != _b) { \
            fprintf(stderr, "%s:%d: FAIL: %s == %s: %llx != %llx\n", __FILE__, __LINE__, #a, #b, _a, _b); \
            ++Failures; \
        } \
    } while (0)

/* INT32 results are compared as 32-bit patterns: negative values must not sign-extend */
#define CHECK_EQ32(a, b) CHECK_EQ((UINT32)(a), (UINT32)(b))

/* A 24-bit codec: 2 channels, 16/24-bit, 44.1..192 kHz */
static void FillRange(KSDATARANGE_AUDIO *Range, ULONG Channels, ULONG MinBits, ULONG MaxBits,
                      ULONG MinRate, ULONG MaxRate)
{
    memset(Range, 0, sizeof(*Range));
    Range->DataRange.FormatSize = sizeof(*Range);
    Range->MaximumChannels = Channels;
    Range->MinimumBitsPerSample = MinBits;
    Range->MaximumBitsPerSample = MaxBits;
    Range->MinimumSampleFrequency = MinRate;
    Range->MaximumSampleFrequency = MaxRate;
}

/* The controller reads 8-, 16- and 32-bit words; 20- and 24-bit samples share the 32-bit word */
static void TestContainerBits(void)
{
    ULONG Bits;

    CHECK_EQ(RosSoundPcmContainerBits(1), 8);
    CHECK_EQ(RosSoundPcmContainerBits(8), 8);
    CHECK_EQ(RosSoundPcmContainerBits(9), 16);
    CHECK_EQ(RosSoundPcmContainerBits(16), 16);
    CHECK_EQ(RosSoundPcmContainerBits(17), 32);
    CHECK_EQ(RosSoundPcmContainerBits(20), 32);
    CHECK_EQ(RosSoundPcmContainerBits(24), 32);
    CHECK_EQ(RosSoundPcmContainerBits(32), 32);

    CHECK_EQ(HDAUDIO_SampleContainerBits(0), 8);
    CHECK_EQ(HDAUDIO_SampleContainerBits(8), 8);
    CHECK_EQ(HDAUDIO_SampleContainerBits(16), 16);
    CHECK_EQ(HDAUDIO_SampleContainerBits(20), 32);
    CHECK_EQ(HDAUDIO_SampleContainerBits(24), 32);
    CHECK_EQ(HDAUDIO_SampleContainerBits(32), 32);
    for (Bits = 1; Bits <= 32; ++Bits)
        CHECK_EQ(RosSoundPcmContainerBits((WORD)Bits), HDAUDIO_SampleContainerBits(Bits));
}

/* The preferred format of a range: most channels, most valid bits in a 32-bit container above 16 */
static void TestPreferredFormat(void)
{
    WAVEFORMATEXTENSIBLE Format;
    KSDATARANGE_AUDIO Range;

    FillRange(&Range, 2, 16, 16, 44100, 96000);
    CHECK(RosSoundAudioRangeToPreferredFormat(&Range, &Format));
    CHECK_EQ(Format.Format.wFormatTag, WAVE_FORMAT_EXTENSIBLE);
    CHECK_EQ(Format.Format.nChannels, 2);
    CHECK_EQ(Format.Format.nSamplesPerSec, 48000);
    CHECK_EQ(Format.Format.wBitsPerSample, 16);
    CHECK_EQ(Format.Format.nBlockAlign, 4);
    CHECK_EQ(Format.Format.nAvgBytesPerSec, 48000 * 4);
    CHECK_EQ(Format.Format.cbSize, sizeof(Format) - sizeof(Format.Format));
    CHECK_EQ(Format.Samples.wValidBitsPerSample, 16);
    CHECK_EQ(Format.dwChannelMask, KSAUDIO_SPEAKER_STEREO);
    CHECK(memcmp(&Format.SubFormat, &KSDATAFORMAT_SUBTYPE_PCM, sizeof(GUID)) == 0);

    FillRange(&Range, 2, 16, 24, 44100, 192000);
    CHECK(RosSoundAudioRangeToPreferredFormat(&Range, &Format));
    CHECK_EQ(Format.Format.wBitsPerSample, 32);
    CHECK_EQ(Format.Samples.wValidBitsPerSample, 24);
    CHECK_EQ(Format.Format.nBlockAlign, 8);
    CHECK_EQ(Format.Format.nAvgBytesPerSec, 48000 * 8);
    CHECK_EQ(Format.Format.nSamplesPerSec, 48000);

    FillRange(&Range, 8, 20, 32, 8000, 48000);
    CHECK(RosSoundAudioRangeToPreferredFormat(&Range, &Format));
    CHECK_EQ(Format.Format.wBitsPerSample, 32);
    CHECK_EQ(Format.Samples.wValidBitsPerSample, 32);
    CHECK_EQ(Format.Format.nChannels, 8);
    CHECK_EQ(Format.Format.nBlockAlign, 32);
    CHECK_EQ(Format.dwChannelMask, KSAUDIO_SPEAKER_7POINT1);

    /* A range that does not cover 48 kHz uses its top rate */
    FillRange(&Range, 2, 16, 24, 88200, 192000);
    CHECK(RosSoundAudioRangeToPreferredFormat(&Range, &Format));
    CHECK_EQ(Format.Format.nSamplesPerSec, 192000);

    /* Mono and 8-bit stay what they are */
    FillRange(&Range, 1, 8, 8, 8000, 48000);
    CHECK(RosSoundAudioRangeToPreferredFormat(&Range, &Format));
    CHECK_EQ(Format.Format.wBitsPerSample, 8);
    CHECK_EQ(Format.Samples.wValidBitsPerSample, 8);
    CHECK_EQ(Format.Format.nChannels, 1);
    CHECK_EQ(Format.dwChannelMask, KSAUDIO_SPEAKER_MONO);

    /* No channels, no bits: no format */
    FillRange(&Range, 0, 16, 16, 44100, 48000);
    CHECK(!RosSoundAudioRangeToPreferredFormat(&Range, &Format));
    FillRange(&Range, 2, 0, 0, 44100, 48000);
    CHECK(!RosSoundAudioRangeToPreferredFormat(&Range, &Format));
    /* A block align the WAVEFORMATEX fields cannot hold */
    FillRange(&Range, 65535, 16, 16, 44100, 48000);
    CHECK(!RosSoundAudioRangeToPreferredFormat(&Range, &Format));
    /* A rate range with no rate */
    FillRange(&Range, 2, 16, 16, 0, 0);
    CHECK(!RosSoundAudioRangeToPreferredFormat(&Range, &Format));
}

/* The container a range states in DataRange.SampleSize (the frame's block align) is kept: per channel it
   must be a PCM container of 8, 16, 24 or 32 bits at least as wide as the valid bits; anything else, and
   zero, derives the container from the valid bits */
static void TestPreferredFormatContainer(void)
{
    WAVEFORMATEXTENSIBLE Format;
    KSDATARANGE_AUDIO Range;

    /* 24 valid bits in 3-byte subslots (a USB audio class range): 24 in 24, block align 6 */
    FillRange(&Range, 2, 24, 24, 44100, 96000);
    Range.DataRange.SampleSize = 6;
    CHECK(RosSoundAudioRangeToPreferredFormat(&Range, &Format));
    CHECK_EQ(Format.Format.wBitsPerSample, 24);
    CHECK_EQ(Format.Samples.wValidBitsPerSample, 24);
    CHECK_EQ(Format.Format.nBlockAlign, 6);
    CHECK_EQ(Format.Format.nAvgBytesPerSec, 48000 * 6);

    /* 16 valid bits in a 4-byte container: 32 in 16, block align 8 */
    FillRange(&Range, 2, 16, 16, 44100, 96000);
    Range.DataRange.SampleSize = 8;
    CHECK(RosSoundAudioRangeToPreferredFormat(&Range, &Format));
    CHECK_EQ(Format.Format.wBitsPerSample, 32);
    CHECK_EQ(Format.Samples.wValidBitsPerSample, 16);
    CHECK_EQ(Format.Format.nBlockAlign, 8);
    CHECK_EQ(Format.Format.nAvgBytesPerSec, 48000 * 8);

    /* 20 valid bits in a 4-byte container, 8 channels */
    FillRange(&Range, 8, 20, 20, 8000, 48000);
    Range.DataRange.SampleSize = 32;
    CHECK(RosSoundAudioRangeToPreferredFormat(&Range, &Format));
    CHECK_EQ(Format.Format.wBitsPerSample, 32);
    CHECK_EQ(Format.Samples.wValidBitsPerSample, 20);
    CHECK_EQ(Format.Format.nBlockAlign, 32);

    /* 8 valid bits in 1-byte samples, stated */
    FillRange(&Range, 2, 8, 8, 8000, 48000);
    Range.DataRange.SampleSize = 2;
    CHECK(RosSoundAudioRangeToPreferredFormat(&Range, &Format));
    CHECK_EQ(Format.Format.wBitsPerSample, 8);
    CHECK_EQ(Format.Samples.wValidBitsPerSample, 8);
    CHECK_EQ(Format.Format.nBlockAlign, 2);

    /* 24 valid bits in a 4-byte container (the HD Audio shape, stated) */
    FillRange(&Range, 2, 16, 24, 44100, 192000);
    Range.DataRange.SampleSize = 8;
    CHECK(RosSoundAudioRangeToPreferredFormat(&Range, &Format));
    CHECK_EQ(Format.Format.wBitsPerSample, 32);
    CHECK_EQ(Format.Samples.wValidBitsPerSample, 24);
    CHECK_EQ(Format.Format.nBlockAlign, 8);

    /* No SampleSize: the valid bits alone, as before */
    FillRange(&Range, 2, 16, 24, 44100, 192000);
    Range.DataRange.SampleSize = 0;
    CHECK(RosSoundAudioRangeToPreferredFormat(&Range, &Format));
    CHECK_EQ(Format.Format.wBitsPerSample, 32);
    CHECK_EQ(Format.Samples.wValidBitsPerSample, 24);
    CHECK_EQ(Format.Format.nBlockAlign, 8);

    /* An inconsistent SampleSize: not a whole number of bytes per channel */
    FillRange(&Range, 2, 16, 24, 44100, 192000);
    Range.DataRange.SampleSize = 5;
    CHECK(RosSoundAudioRangeToPreferredFormat(&Range, &Format));
    CHECK_EQ(Format.Format.wBitsPerSample, 32);
    CHECK_EQ(Format.Samples.wValidBitsPerSample, 24);
    CHECK_EQ(Format.Format.nBlockAlign, 8);

    /* ... a container that is not 8, 16, 24 or 32 bits (6 bytes a channel) */
    FillRange(&Range, 2, 16, 24, 44100, 192000);
    Range.DataRange.SampleSize = 12;
    CHECK(RosSoundAudioRangeToPreferredFormat(&Range, &Format));
    CHECK_EQ(Format.Format.wBitsPerSample, 32);
    CHECK_EQ(Format.Samples.wValidBitsPerSample, 24);
    CHECK_EQ(Format.Format.nBlockAlign, 8);

    /* ... narrower than the valid bits (2 bytes a channel for 24 valid bits) */
    FillRange(&Range, 2, 16, 24, 44100, 192000);
    Range.DataRange.SampleSize = 4;
    CHECK(RosSoundAudioRangeToPreferredFormat(&Range, &Format));
    CHECK_EQ(Format.Format.wBitsPerSample, 32);
    CHECK_EQ(Format.Samples.wValidBitsPerSample, 24);
    CHECK_EQ(Format.Format.nBlockAlign, 8);

    /* ... fewer bytes than channels */
    FillRange(&Range, 2, 16, 16, 44100, 48000);
    Range.DataRange.SampleSize = 1;
    CHECK(RosSoundAudioRangeToPreferredFormat(&Range, &Format));
    CHECK_EQ(Format.Format.wBitsPerSample, 16);
    CHECK_EQ(Format.Samples.wValidBitsPerSample, 16);
    CHECK_EQ(Format.Format.nBlockAlign, 4);

    /* A stated container whose frame does not fit nBlockAlign */
    FillRange(&Range, 65535, 16, 16, 44100, 48000);
    Range.DataRange.SampleSize = 65535 * 2;
    CHECK(!RosSoundAudioRangeToPreferredFormat(&Range, &Format));
}

/* The legacy table has no 20/24/32-bit or 88.2/176.4/192 kHz entry: that is the gate P0 works around */
static void TestLegacyTable(void)
{
    CHECK_EQ(RosSoundWaveFormatFieldsToLegacyFlag(48000, 16, 2), WAVE_FORMAT_48S16);
    CHECK_EQ(RosSoundWaveFormatFieldsToLegacyFlag(44100, 16, 2), WAVE_FORMAT_4S16);
    CHECK_EQ(RosSoundWaveFormatFieldsToLegacyFlag(48000, 24, 2), 0);
    CHECK_EQ(RosSoundWaveFormatFieldsToLegacyFlag(48000, 32, 2), 0);
    CHECK_EQ(RosSoundWaveFormatFieldsToLegacyFlag(88200, 16, 2), 0);
    CHECK_EQ(RosSoundWaveFormatFieldsToLegacyFlag(192000, 16, 2), 0);
    CHECK_EQ(RosSoundChooseBestLegacyFormat(WAVE_FORMAT_48S16 | WAVE_FORMAT_96S16), WAVE_FORMAT_48S16);
    CHECK_EQ(RosSoundChooseBestLegacyFormat(0), 0);
}

/* Samples are carried as 32-bit words spanning full scale; the container holds the top valid bits */
static void TestPcmSamples(void)
{
    BYTE data[4];
    INT32 value;

    /* 16-bit full scale, minimum, zero, -1 LSB */
    value = read_pcm_sample((const BYTE *)"\xff\xff", 16, 16);
    CHECK_EQ32(value, 0xffff0000);
    CHECK_EQ32(read_pcm_sample((const BYTE *)"\x00\x80", 16, 16), 0x80000000);
    CHECK_EQ32(read_pcm_sample((const BYTE *)"\x00\x00", 16, 16), 0);
    CHECK_EQ32(read_pcm_sample((const BYTE *)"\x01\x00", 16, 16), 0x00010000);

    write_pcm_sample(data, 16, 16, 0xffff0000);
    CHECK_EQ(data[0], 0xff);
    CHECK_EQ(data[1], 0xff);
    write_pcm_sample(data, 16, 16, (INT32)0x80000000);
    CHECK_EQ(data[0], 0x00);
    CHECK_EQ(data[1], 0x80);
    write_pcm_sample(data, 16, 16, 0x00010000);
    CHECK_EQ(data[0], 0x01);
    CHECK_EQ(data[1], 0x00);
    write_pcm_sample(data, 16, 16, 0x7fffffff);
    CHECK_EQ(data[0], 0xff);
    CHECK_EQ(data[1], 0x7f);

    /* 24 valid bits in a 32-bit container: top 24 bits carry the sample, the low byte is zero */
    CHECK_EQ32(read_pcm_sample((const BYTE *)"\x00\xff\xff\x7f", 32, 24), 0x7fffff00);
    CHECK_EQ32(read_pcm_sample((const BYTE *)"\x00\x00\x00\x80", 32, 24), 0x80000000);
    CHECK_EQ32(read_pcm_sample((const BYTE *)"\x00\x00\x01\x00", 32, 24), 0x00010000);
    /* The low byte is masked away, never folded into the sample */
    CHECK_EQ32(read_pcm_sample((const BYTE *)"\xff\x00\x01\x00", 32, 24), 0x00010000);
    write_pcm_sample(data, 32, 24, 0x12345600);
    CHECK_EQ(data[0], 0x00);
    CHECK_EQ(data[1], 0x56);
    CHECK_EQ(data[2], 0x34);
    CHECK_EQ(data[3], 0x12);
    write_pcm_sample(data, 32, 24, 0x123456ff);
    CHECK_EQ(data[0], 0x00);
    CHECK_EQ(data[1], 0x56);

    /* 32 valid bits: every bit is the sample */
    CHECK_EQ32(read_pcm_sample((const BYTE *)"\x01\x02\x03\x04", 32, 32), 0x04030201);
    write_pcm_sample(data, 32, 32, 0x04030201);
    CHECK_EQ(data[0], 0x01);
    CHECK_EQ(data[1], 0x02);
    CHECK_EQ(data[2], 0x03);
    CHECK_EQ(data[3], 0x04);

    /* A packed 24-bit client container: top 24 bits of the 32-bit word */
    CHECK_EQ32(read_pcm_sample((const BYTE *)"\x56\x34\x12", 24, 24), 0x12345600);
    write_pcm_sample(data, 24, 24, 0x12345600);
    CHECK_EQ(data[0], 0x56);
    CHECK_EQ(data[1], 0x34);
    CHECK_EQ(data[2], 0x12);

    /* 8-bit is unsigned with the midpoint at 0x80 */
    CHECK_EQ32(read_pcm_sample((const BYTE *)"\x80", 8, 8), 0);
    CHECK_EQ32(read_pcm_sample((const BYTE *)"\xff", 8, 8), 0x7f000000);
    CHECK_EQ32(read_pcm_sample((const BYTE *)"\x00", 8, 8), 0x80000000);
    write_pcm_sample(data, 8, 8, 0);
    CHECK_EQ(data[0], 0x80);

    /* 16 valid bits in a 32-bit container leave the low half zero */
    write_pcm_sample(data, 32, 16, 0xabcd1234);
    CHECK_EQ(data[0], 0x00);
    CHECK_EQ(data[1], 0x00);
    CHECK_EQ(data[2], 0xcd);
    CHECK_EQ(data[3], 0xab);
    CHECK_EQ32(read_pcm_sample((const BYTE *)"\x00\x00\xcd\xab", 32, 16), 0xabcd0000);
    /* Valid bits beyond the container are clamped to the container */
    CHECK_EQ32(read_pcm_sample((const BYTE *)"\x01\x02", 16, 24), 0x02010000);

    /* Mixing saturation */
    CHECK_EQ32(saturate_sample(0x7ffffffeLL + 2), 0x7fffffff);
    CHECK_EQ32(saturate_sample(-0x80000000LL - 1), 0x80000000);
    CHECK_EQ32(saturate_sample(0x12345678), 0x12345678);

    /* Round trip of the mix word at full scale through 24-in-32 */
    CHECK_EQ32(read_pcm_sample((const BYTE *)"\x00\x00\x00\x80", 32, 24), 0x80000000);
    write_pcm_sample(data, 32, 24, (INT32)0x80000000);
    CHECK_EQ(data[3], 0x80);
    CHECK_EQ(data[0], 0);
}

/* Dither when narrowing to 16 bits: a fixed xorshift sequence from the fixed seed, so
   a run is reproducible; the reference words come from the algorithm written out */
static void TestDither(void)
{
    static const WORD Expected16[] = { 0x1233, 0x1234, 0x1233, 0x1233, 0x1233, 0x1233 };
    pcm_dither dither = PCM_DITHER_INIT, again = PCM_DITHER_INIT, untouched = PCM_DITHER_INIT;
    BYTE data[4], first[4];
    unsigned i;

    for (i = 0; i < 6; ++i)
    {
        write_pcm_sample_dithered(data, 16, 16, 0x12340000, &dither);
        CHECK_EQ((WORD)(data[0] | (data[1] << 8)), Expected16[i]);
    }
    /* A negative sample dithers the same way around its own truncated word (0xedca = 0x1234 - 0x1234 + 0xedca) */
    dither = (pcm_dither)PCM_DITHER_INIT;
    for (i = 0; i < 6; ++i)
    {
        write_pcm_sample_dithered(data, 16, 16, (INT32)0xedca0000, &dither);
        CHECK_EQ((WORD)(data[0] | (data[1] << 8)), (WORD)(Expected16[i] + 0xedca - 0x1234));
    }
    /* The same seed and the same samples give the same words, whatever came before */
    dither = (pcm_dither)PCM_DITHER_INIT;
    again = (pcm_dither)PCM_DITHER_INIT;
    for (i = 0; i < 64; ++i)
    {
        write_pcm_sample_dithered(data, 16, 16, (INT32)(0x12340000 + i * 0x10000), &dither);
        write_pcm_sample_dithered(first, 16, 16, (INT32)(0x12340000 + i * 0x10000), &again);
        CHECK(memcmp(data, first, 2) == 0);
    }
    /* The dithered word stays within one LSB of the truncated one */
    dither = (pcm_dither)PCM_DITHER_INIT;
    for (i = 0; i < 4096; ++i)
    {
        INT32 value = (INT32)(0x12340000 + (i * 0x9e37) % 0x10000);
        WORD plain, dithered;

        write_pcm_sample(data, 16, 16, value);
        plain = (WORD)(data[0] | (data[1] << 8));
        write_pcm_sample_dithered(first, 16, 16, value, &dither);
        dithered = (WORD)(first[0] | (first[1] << 8));
        CHECK(dithered + 1 >= plain && dithered <= plain + 1);
    }
    /* 24 and 32 valid bits keep every bit of the mix word: the engine writes them undithered */
    write_pcm_sample(data, 32, 24, 0x12345678);
    CHECK_EQ(data[0], 0x00);
    CHECK_EQ(data[1], 0x56);
    CHECK_EQ(data[2], 0x34);
    CHECK_EQ(data[3], 0x12);
    CHECK_EQ(untouched.state, 0x9e3779b9u);
    /* The generator advances and never falls into the zero state */
    for (i = 0; i < 1000; ++i)
    {
        pcm_dither_next(&untouched);
        CHECK(untouched.state != 0);
    }
    CHECK(untouched.state != 0x9e3779b9u);
}

int main(void)
{
    TestContainerBits();
    TestContainerBits();
    TestPreferredFormat();
    TestPreferredFormatContainer();
    TestLegacyTable();
    TestPcmSamples();
    TestDither();
    if (Failures)
    {
        fprintf(stderr, "audiofmt: %d check(s) failed\n", Failures);
        return 1;
    }
    printf("audiofmt: all checks passed\n");
    return 0;
}
