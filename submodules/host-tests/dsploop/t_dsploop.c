/*
 * PROJECT:     LiberNT host-native tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     The dsploop apitest's comparison (modules/rostests/apitests/dsploop/compare.c), the same source the apitest compiles: the offset from the first matching frame, the whole tone compared after it, the first differing sample recorded and the stop rule's shape, in the three formats; the tolerance of one LSB at 16 valid bits, where the render engine's dither moves the frames, and none at 24 and 32; under ASan and UBSan
 * COPYRIGHT:   Copyright 2026 LiberNT contributors
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "compare.h"
#include "walk.h"

/* The run's parameters, as the apitest has them (dsploop.c): one second of tone, a second of latency at most, the capture holding both */
#define DSPLOOP_FRAMES           48000
#define DSPLOOP_MAX_OFFSET       48000
#define DSPLOOP_CAPTURE_FRAMES   (DSPLOOP_FRAMES + DSPLOOP_MAX_OFFSET)

static int Checks;
static int Failures;

#define CHECK(cond) \
    do { \
        ++Checks; \
        if (!(cond)) { \
            fprintf(stderr, "%s:%d: FAIL: %s\n", __FILE__, __LINE__, #cond); \
            ++Failures; \
        } \
    } while (0)

#define CHECK_EQ(a, b) \
    do { \
        long long _a = (long long)(a), _b = (long long)(b); \
        ++Checks; \
        if (_a != _b) { \
            fprintf(stderr, "%s:%d: FAIL: %s == %s: %lld != %lld\n", __FILE__, __LINE__, #a, #b, _a, _b); \
            ++Failures; \
        } \
    } while (0)

/* One sample into a capture as the stream carries it: a little-endian word, the valid bits at the top of a 32-bit word when there are 24 */
static
void
StoreSample(
    uint8_t* At,
    int ContainerBits,
    int ValidBits,
    int32_t Sample)
{
    uint32_t Word = (uint32_t)(ValidBits == 24 ? Sample * 256 : Sample);

    At[0] = (uint8_t)Word;
    At[1] = (uint8_t)(Word >> 8);
    if (ContainerBits == 32)
    {
        At[2] = (uint8_t)(Word >> 16);
        At[3] = (uint8_t)(Word >> 24);
    }
}

/*
 * A capture of Count frames: silence for Lead frames, then Tone frames of
 * the tone from its frame Phase, silence after them. The caller frees it.
 */
static
uint8_t*
BuildCapture(
    unsigned Channels,
    int ContainerBits,
    int ValidBits,
    uint32_t Lead,
    uint32_t Tone,
    uint32_t Count,
    uint32_t Phase)
{
    uint32_t SampleBytes = (uint32_t)(ContainerBits / 8), FrameBytes = Channels * SampleBytes;
    uint8_t* Data = (uint8_t*)calloc(Count, FrameBytes);
    uint32_t N, C;

    for (N = 0; Data && N < Tone && Lead + N < Count; N++)
        for (C = 0; C < Channels; C++)
            StoreSample(Data + (Lead + N) * FrameBytes + C * SampleBytes, ContainerBits, ValidBits, DsploopToneSample(N + Phase, ValidBits));
    return Data;
}

/* One sample of a capture, as the comparison reads it */
static
int32_t
ReadSample(
    const uint8_t* Data,
    unsigned Channels,
    int ContainerBits,
    int ValidBits,
    uint32_t Frame,
    unsigned Channel)
{
    uint32_t SampleBytes = (uint32_t)(ContainerBits / 8);
    const uint8_t* At = Data + Frame * Channels * SampleBytes + Channel * SampleBytes;
    uint32_t Word = (uint32_t)At[0] | ((uint32_t)At[1] << 8);
    int32_t Value;

    if (ContainerBits == 32)
        Word |= ((uint32_t)At[2] << 16) | ((uint32_t)At[3] << 24);
    Value = ContainerBits == 16 ? (int32_t)(int16_t)(uint16_t)Word : (int32_t)Word;
    if (ContainerBits > ValidBits)
        Value >>= ContainerBits - ValidBits;
    return Value;
}

static
void
WriteSample(
    uint8_t* Data,
    unsigned Channels,
    int ContainerBits,
    int ValidBits,
    uint32_t Frame,
    unsigned Channel,
    int32_t Sample)
{
    StoreSample(Data + (Frame * Channels + Channel) * (uint32_t)(ContainerBits / 8), ContainerBits, ValidBits, Sample);
}

/* The tone's table as the apitest states it: half the full scale at 16 valid bits, one cycle of 48 frames */
static
void
CheckTone(
    void)
{
    static const int32_t Cycle[DSPLOOP_TONE_FRAMES] =
    {
             0,   2139,   4240,   6270,   8192,   9974,  11585,  12998,
         14189,  15137,  15826,  16244,  16384,  16244,  15826,  15137,
         14189,  12998,  11585,   9974,   8192,   6270,   4240,   2139,
             0,  -2139,  -4240,  -6270,  -8192,  -9974, -11585, -12998,
        -14189, -15137, -15826, -16244, -16384, -16244, -15826, -15137,
        -14189, -12998, -11585,  -9974,  -8192,  -6270,  -4240,  -2139
    };
    uint32_t K;

    for (K = 0; K < DSPLOOP_TONE_FRAMES * 3; K++)
    {
        CHECK_EQ(DsploopToneSample(K, 16), Cycle[K % DSPLOOP_TONE_FRAMES]);
        CHECK_EQ(DsploopToneSample(K, 24), Cycle[K % DSPLOOP_TONE_FRAMES] * 256);
        CHECK_EQ(DsploopToneSample(K, 32), Cycle[K % DSPLOOP_TONE_FRAMES] * 65536);
    }
}

/* The loop as the VM run should see it, in each format: a latency of lead frames, the whole tone, then silence */
static
void
CheckExact(
    void)
{
    static const struct
    {
        int ContainerBits, ValidBits;
    } Formats[] =
    {
        { 16, 16 }, { 32, 24 }, { 32, 32 }
    };
    static const uint32_t Leads[] = { 0, 7, 1000, 4800, DSPLOOP_MAX_OFFSET };
    DSPLOOP_MATCH Match;
    uint8_t* Data;
    unsigned F, L;

    for (F = 0; F < sizeof(Formats) / sizeof(Formats[0]); F++)
    {
        for (L = 0; L < sizeof(Leads) / sizeof(Leads[0]); L++)
        {
            Data = BuildCapture(2, Formats[F].ContainerBits, Formats[F].ValidBits, Leads[L], DSPLOOP_FRAMES, DSPLOOP_CAPTURE_FRAMES, 0);
            CHECK(Data != NULL);
            if (!Data)
                continue;
            memset(&Match, 0xCC, sizeof(Match));
            CHECK(DsploopCompare(Data, DSPLOOP_CAPTURE_FRAMES, 2, Formats[F].ContainerBits, Formats[F].ValidBits, DSPLOOP_FRAMES, DSPLOOP_MAX_OFFSET, 0, &Match) == 1);
            CHECK_EQ(Match.Matched, 1);
            CHECK_EQ(Match.Offset, Leads[L]);
            CHECK_EQ(Match.Compared, DSPLOOP_FRAMES);
            CHECK_EQ(Match.MaxError, 0);
            CHECK_EQ(Match.FirstDifference, DSPLOOP_NO_DIFFERENCE);
            CHECK_EQ(Match.Channel, 0);
            free(Data);
        }
    }
}

/*
 * The tolerance at 16 valid bits: the render engine dithers its 16-bit
 * narrowing, so about half the frames come one LSB below or above the tone's
 * sample. An exact comparison finds no window and stops at the first frame;
 * with one LSB every frame compares and none is a difference, the largest
 * error being one LSB; a frame two LSBs away is still the first beyond it.
 * At 24 and 32 valid bits the tolerance is 0 and one LSB is a difference.
 */
static
void
CheckTolerance(
    void)
{
    static const int Steps[] = { -1, 1, 0, -1, 0, 1, -1, 0 };
    DSPLOOP_MATCH Match;
    uint8_t* Data;
    uint32_t N;
    unsigned C;

    /* every frame but the isolated unmodified ones one LSB away from the tone, as the dither leaves it */
    Data = BuildCapture(2, 16, 16, 0, DSPLOOP_FRAMES, DSPLOOP_CAPTURE_FRAMES, 0);
    CHECK(Data != NULL);
    if (Data)
    {
        for (N = 0; N < DSPLOOP_FRAMES; N++)
            for (C = 0; C < 2; C++)
                WriteSample(Data, 2, 16, 16, N, C, ReadSample(Data, 2, 16, 16, N, C) + Steps[N % 8]);

        /* exact: no window is the tone's; the frames from the first are compared and the first differs */
        memset(&Match, 0xCC, sizeof(Match));
        CHECK(DsploopCompare(Data, DSPLOOP_CAPTURE_FRAMES, 2, 16, 16, DSPLOOP_FRAMES, DSPLOOP_MAX_OFFSET, 0, &Match) == 0);
        CHECK_EQ(Match.FirstDifference, 0);

        /* one LSB: the window matches at 0, every frame compares, the largest error is one LSB */
        memset(&Match, 0xCC, sizeof(Match));
        CHECK(DsploopCompare(Data, DSPLOOP_CAPTURE_FRAMES, 2, 16, 16, DSPLOOP_FRAMES, DSPLOOP_MAX_OFFSET, 1, &Match) == 1);
        CHECK_EQ(Match.Offset, 0);
        CHECK_EQ(Match.Compared, DSPLOOP_FRAMES);
        CHECK_EQ(Match.MaxError, 1);
        CHECK_EQ(Match.FirstDifference, DSPLOOP_NO_DIFFERENCE);

        /* the tolerance the apitest passes is that one */
        CHECK_EQ(DsploopTolerance(16), 1);
        memset(&Match, 0xCC, sizeof(Match));
        CHECK(DsploopCompare(Data, DSPLOOP_CAPTURE_FRAMES, 2, 16, 16, DSPLOOP_FRAMES, DSPLOOP_MAX_OFFSET, DsploopTolerance(16), &Match) == 1);
        CHECK_EQ(Match.FirstDifference, DSPLOOP_NO_DIFFERENCE);

        /* a frame two LSBs away is the first beyond the tolerance, with its channel */
        WriteSample(Data, 2, 16, 16, 500, 1, DsploopToneSample(500, 16) - 2);
        memset(&Match, 0xCC, sizeof(Match));
        CHECK(DsploopCompare(Data, DSPLOOP_CAPTURE_FRAMES, 2, 16, 16, DSPLOOP_FRAMES, DSPLOOP_MAX_OFFSET, 1, &Match) == 1);
        CHECK_EQ(Match.FirstDifference, 500);
        CHECK_EQ(Match.Channel, 1);
        CHECK_EQ(Match.MaxError, 2);
        free(Data);
    }

    /* 24 and 32 valid bits: the tolerance is 0 and one LSB is a difference */
    CHECK_EQ(DsploopTolerance(24), 0);
    CHECK_EQ(DsploopTolerance(32), 0);
    Data = BuildCapture(2, 32, 24, 0, DSPLOOP_FRAMES, DSPLOOP_CAPTURE_FRAMES, 0);
    CHECK(Data != NULL);
    if (Data)
    {
        WriteSample(Data, 2, 32, 24, 500, 0, ReadSample(Data, 2, 32, 24, 500, 0) + 1);
        memset(&Match, 0xCC, sizeof(Match));
        CHECK(DsploopCompare(Data, DSPLOOP_CAPTURE_FRAMES, 2, 32, 24, DSPLOOP_FRAMES, DSPLOOP_MAX_OFFSET, DsploopTolerance(24), &Match) == 1);
        CHECK_EQ(Match.FirstDifference, 500);
        free(Data);
    }
}

/* What stops a run: a damaged sample after the offset is the first differing sample, with its channel and the two figures */
static
void
CheckDamaged(
    void)
{
    DSPLOOP_MATCH Match;
    uint8_t* Data;
    uint32_t Lead = 1234, At = 777;
    int32_t Expected;

    Data = BuildCapture(2, 16, 16, Lead, DSPLOOP_FRAMES, DSPLOOP_CAPTURE_FRAMES, 0);
    CHECK(Data != NULL);
    if (!Data)
        return;
    /* channel 1 of the frame: three above the tone */
    Expected = DsploopToneSample(At, 16);
    WriteSample(Data, 2, 16, 16, Lead + At, 1, Expected + 3);
    memset(&Match, 0xCC, sizeof(Match));
    CHECK(DsploopCompare(Data, DSPLOOP_CAPTURE_FRAMES, 2, 16, 16, DSPLOOP_FRAMES, DSPLOOP_MAX_OFFSET, 0, &Match) == 1);
    CHECK_EQ(Match.Offset, Lead);
    CHECK_EQ(Match.FirstDifference, At);
    CHECK_EQ(Match.Channel, 1);
    CHECK_EQ(Match.Expected, Expected);
    CHECK_EQ(Match.Captured, Expected + 3);
    CHECK_EQ(Match.MaxError, 3);
    /* a larger damage earlier in another channel wins both records */
    WriteSample(Data, 2, 16, 16, Lead + 100, 0, -DsploopToneSample(100, 16));
    memset(&Match, 0xCC, sizeof(Match));
    DsploopCompare(Data, DSPLOOP_CAPTURE_FRAMES, 2, 16, 16, DSPLOOP_FRAMES, DSPLOOP_MAX_OFFSET, 0, &Match);
    CHECK_EQ(Match.FirstDifference, 100);
    CHECK_EQ(Match.Channel, 0);
    CHECK_EQ(Match.Expected, DsploopToneSample(100, 16));
    CHECK_EQ(Match.Captured, -DsploopToneSample(100, 16));
    CHECK_EQ(Match.MaxError, 2 * (int64_t)DsploopToneSample(100, 16));
    free(Data);
    /* 24 valid bits in a 32-bit word: the low byte is outside the comparison, a change of the valid bits is not */
    Data = BuildCapture(2, 32, 24, 10, DSPLOOP_FRAMES, DSPLOOP_CAPTURE_FRAMES, 0);
    CHECK(Data != NULL);
    if (!Data)
        return;
    Data[(10 + 5) * 8 + 0] = 0xA5;
    Data[(10 + 5) * 8 + 4] = 0x5A;
    memset(&Match, 0xCC, sizeof(Match));
    CHECK(DsploopCompare(Data, DSPLOOP_CAPTURE_FRAMES, 2, 32, 24, DSPLOOP_FRAMES, DSPLOOP_MAX_OFFSET, 0, &Match) == 1);
    CHECK_EQ(Match.Offset, 10);
    CHECK_EQ(Match.MaxError, 0);
    WriteSample(Data, 2, 32, 24, 10 + 100, 0, DsploopToneSample(100, 24) - 1);
    memset(&Match, 0xCC, sizeof(Match));
    DsploopCompare(Data, DSPLOOP_CAPTURE_FRAMES, 2, 32, 24, DSPLOOP_FRAMES, DSPLOOP_MAX_OFFSET, 0, &Match);
    CHECK_EQ(Match.Offset, 10);
    CHECK_EQ(Match.FirstDifference, 100);
    CHECK_EQ(Match.MaxError, 1);
    free(Data);
    /*
     * The limit of "from the first matching frame": a damaged sample inside
     * the tone's first cycle fails the window at the tone's start, and the
     * next cycle's start matches, so the offset moves a cycle on past the
     * damage and the line reports the silence after the tone as the first
     * difference, a cycle early.
     */
    Data = BuildCapture(2, 16, 16, 10, DSPLOOP_FRAMES, DSPLOOP_CAPTURE_FRAMES, 0);
    CHECK(Data != NULL);
    if (Data)
    {
        WriteSample(Data, 2, 16, 16, 10 + 5, 0, DsploopToneSample(5, 16) + 1);
        memset(&Match, 0xCC, sizeof(Match));
        CHECK(DsploopCompare(Data, DSPLOOP_CAPTURE_FRAMES, 2, 16, 16, DSPLOOP_FRAMES, DSPLOOP_MAX_OFFSET, 0, &Match) == 1);
        CHECK_EQ(Match.Offset, 58);
        CHECK_EQ(Match.Compared, DSPLOOP_FRAMES);
        /* the tone ends 48 frames into the compared range's last cycle: the first of its frames that is not silence is the second */
        CHECK_EQ(Match.FirstDifference, DSPLOOP_FRAMES - DSPLOOP_WINDOW_FRAMES + 1);
        CHECK_EQ(Match.Captured, 0);
        CHECK_EQ(Match.MaxError, 16384);
        free(Data);
    }
}

/*
 * No match: the frames are compared from the first for the record. The
 * latency past the range searched, a capture that is not the tone, a
 * capture shorter than a window, and arguments the comparison refuses.
 */
static
void
CheckNoMatch(
    void)
{
    DSPLOOP_MATCH Match;
    uint8_t* Data;

    /* a latency past the range: silence where the tone's first cycle should be */
    Data = BuildCapture(2, 16, 16, DSPLOOP_MAX_OFFSET + DSPLOOP_WINDOW_FRAMES, DSPLOOP_FRAMES, DSPLOOP_CAPTURE_FRAMES + 2 * DSPLOOP_WINDOW_FRAMES, 0);
    CHECK(Data != NULL);
    if (Data)
    {
        memset(&Match, 0xCC, sizeof(Match));
        CHECK(DsploopCompare(Data, DSPLOOP_CAPTURE_FRAMES + 2 * DSPLOOP_WINDOW_FRAMES, 2, 16, 16, DSPLOOP_FRAMES, DSPLOOP_MAX_OFFSET, 0, &Match) == 0);
        CHECK_EQ(Match.Matched, 0);
        CHECK_EQ(Match.Offset, 0);
        CHECK_EQ(Match.Compared, DSPLOOP_FRAMES);
        /* frame 0 is the tone's frame 0 in silence; frame 1 is where they part */
        CHECK_EQ(Match.FirstDifference, 1);
        CHECK_EQ(Match.Channel, 0);
        CHECK_EQ(Match.Expected, DsploopToneSample(1, 16));
        CHECK_EQ(Match.Captured, 0);
        CHECK_EQ(Match.MaxError, 16384);
        free(Data);
    }
    /* every sample one above the tone, an offset the loop must not have: no window matches, and the first frame's silence is where they part */
    Data = BuildCapture(2, 16, 16, 0, DSPLOOP_FRAMES, DSPLOOP_CAPTURE_FRAMES, 0);
    CHECK(Data != NULL);
    if (Data)
    {
        uint32_t N;
        unsigned C;

        for (N = 0; N < DSPLOOP_FRAMES; N++)
            for (C = 0; C < 2; C++)
                WriteSample(Data, 2, 16, 16, N, C, ReadSample(Data, 2, 16, 16, N, C) + 1);
        memset(&Match, 0xCC, sizeof(Match));
        CHECK(DsploopCompare(Data, DSPLOOP_CAPTURE_FRAMES, 2, 16, 16, DSPLOOP_FRAMES, DSPLOOP_MAX_OFFSET, 0, &Match) == 0);
        CHECK_EQ(Match.Matched, 0);
        CHECK_EQ(Match.FirstDifference, 0);
        CHECK_EQ(Match.Channel, 0);
        CHECK_EQ(Match.Expected, 0);
        CHECK_EQ(Match.Captured, 1);
        CHECK_EQ(Match.MaxError, 1);
        free(Data);
    }
    /* a half cycle of the tone missed: the periodic tone matches where its cycle starts again */
    Data = BuildCapture(2, 16, 16, 0, DSPLOOP_FRAMES + 24, DSPLOOP_FRAMES + 24, 24);
    CHECK(Data != NULL);
    if (Data)
    {
        memset(&Match, 0xCC, sizeof(Match));
        CHECK(DsploopCompare(Data, DSPLOOP_FRAMES + 24, 2, 16, 16, DSPLOOP_FRAMES, DSPLOOP_MAX_OFFSET, 0, &Match) == 1);
        CHECK_EQ(Match.Offset, 24);
        CHECK_EQ(Match.Compared, DSPLOOP_FRAMES);
        CHECK_EQ(Match.MaxError, 0);
        free(Data);
    }
    /* shorter than a window: nothing is searched, what there is compared from the first frame, and nothing read past the end */
    Data = BuildCapture(2, 16, 16, 0, 40, 40, 0);
    CHECK(Data != NULL);
    if (Data)
    {
        memset(&Match, 0xCC, sizeof(Match));
        CHECK(DsploopCompare(Data, 40, 2, 16, 16, DSPLOOP_FRAMES, DSPLOOP_MAX_OFFSET, 0, &Match) == 0);
        CHECK_EQ(Match.Matched, 0);
        CHECK_EQ(Match.Compared, 40);
        CHECK_EQ(Match.FirstDifference, DSPLOOP_NO_DIFFERENCE);
        free(Data);
    }
    /* silence only, two windows long and no match in it: the search stops where the capture stops holding a window */
    Data = BuildCapture(2, 16, 16, 0, 0, 96, 0);
    CHECK(Data != NULL);
    if (Data)
    {
        memset(&Match, 0xCC, sizeof(Match));
        CHECK(DsploopCompare(Data, 96, 2, 16, 16, DSPLOOP_FRAMES, DSPLOOP_MAX_OFFSET, 0, &Match) == 0);
        CHECK_EQ(Match.Matched, 0);
        CHECK_EQ(Match.Compared, 96);
        CHECK_EQ(Match.FirstDifference, 1);
        CHECK_EQ(Match.MaxError, 16384);
        free(Data);
    }
    /* a range searched past the capture stops at what the capture holds of a window */
    Data = BuildCapture(2, 16, 16, 0, 48000, 96, 0);
    CHECK(Data != NULL);
    if (Data)
    {
        memset(&Match, 0xCC, sizeof(Match));
        CHECK(DsploopCompare(Data, 96, 2, 16, 16, DSPLOOP_FRAMES, DSPLOOP_MAX_OFFSET, 0, &Match) == 1);
        CHECK_EQ(Match.Offset, 0);
        CHECK_EQ(Match.Compared, 96);
        free(Data);
    }
    /* a capture that ends before the tone does: the frames compared are what it holds after the offset, all of them the tone's */
    Data = BuildCapture(2, 16, 16, 1000, DSPLOOP_FRAMES, DSPLOOP_FRAMES + 500, 0);
    CHECK(Data != NULL);
    if (Data)
    {
        memset(&Match, 0xCC, sizeof(Match));
        CHECK(DsploopCompare(Data, DSPLOOP_FRAMES + 500, 2, 16, 16, DSPLOOP_FRAMES, DSPLOOP_MAX_OFFSET, 0, &Match) == 1);
        CHECK_EQ(Match.Offset, 1000);
        CHECK_EQ(Match.Compared, DSPLOOP_FRAMES - 500);
        CHECK_EQ(Match.MaxError, 0);
        free(Data);
    }
    /* refused: no buffer, no channel, no frames, a word that is not 16 or 32 bits, valid bits beyond the word or of no count */
    Data = BuildCapture(2, 16, 16, 0, DSPLOOP_FRAMES, DSPLOOP_FRAMES, 0);
    CHECK(Data != NULL);
    if (Data)
    {
        memset(&Match, 0xCC, sizeof(Match));
        CHECK(DsploopCompare(NULL, DSPLOOP_FRAMES, 2, 16, 16, DSPLOOP_FRAMES, DSPLOOP_MAX_OFFSET, 0, &Match) == 0);
        CHECK_EQ(Match.Matched, 0);
        CHECK_EQ(Match.FirstDifference, DSPLOOP_NO_DIFFERENCE);
        CHECK_EQ(Match.Compared, 0);
        CHECK(DsploopCompare(Data, DSPLOOP_FRAMES, 0, 16, 16, DSPLOOP_FRAMES, DSPLOOP_MAX_OFFSET, 0, &Match) == 0);
        CHECK(DsploopCompare(Data, DSPLOOP_FRAMES, 2, 16, 16, 0, DSPLOOP_MAX_OFFSET, 0, &Match) == 0);
        CHECK(DsploopCompare(Data, DSPLOOP_FRAMES, 2, 24, 24, DSPLOOP_FRAMES, DSPLOOP_MAX_OFFSET, 0, &Match) == 0);
        CHECK(DsploopCompare(Data, DSPLOOP_FRAMES, 2, 16, 24, DSPLOOP_FRAMES, DSPLOOP_MAX_OFFSET, 0, &Match) == 0);
        CHECK(DsploopCompare(Data, DSPLOOP_FRAMES, 2, 32, 20, DSPLOOP_FRAMES, DSPLOOP_MAX_OFFSET, 0, &Match) == 0);
        free(Data);
    }
}

/*
 * The tone repeats every 48 frames, so a capture that missed the tone's
 * first frames matches where its cycle starts again: the offset is the
 * first frame whose window is the tone's from its first frame, and the
 * comparison is exact from there.
 */
static
void
CheckMidCycle(
    void)
{
    DSPLOOP_MATCH Match;
    uint8_t* Data;

    /* the capture begins at the tone's frame 12: the window matches 36 frames in, and the tone runs to the capture's end from there */
    Data = BuildCapture(2, 16, 16, 0, DSPLOOP_FRAMES + 36, DSPLOOP_FRAMES + 36, 12);
    CHECK(Data != NULL);
    if (Data)
    {
        memset(&Match, 0xCC, sizeof(Match));
        CHECK(DsploopCompare(Data, DSPLOOP_FRAMES + 36, 2, 16, 16, DSPLOOP_FRAMES, DSPLOOP_MAX_OFFSET, 0, &Match) == 1);
        CHECK_EQ(Match.Offset, 36);
        CHECK_EQ(Match.MaxError, 0);
        free(Data);
    }
    /* one channel count and six: the tone is in every channel, and a channel that is not the tone's fails the window */
    Data = BuildCapture(6, 32, 32, 3, DSPLOOP_FRAMES, DSPLOOP_CAPTURE_FRAMES, 0);
    CHECK(Data != NULL);
    if (Data)
    {
        memset(&Match, 0xCC, sizeof(Match));
        CHECK(DsploopCompare(Data, DSPLOOP_CAPTURE_FRAMES, 6, 32, 32, DSPLOOP_FRAMES, DSPLOOP_MAX_OFFSET, 0, &Match) == 1);
        CHECK_EQ(Match.Offset, 3);
        CHECK_EQ(Match.Compared, DSPLOOP_FRAMES);
        WriteSample(Data, 6, 32, 32, 3 + 500, 5, DsploopToneSample(500, 32) + 65536);
        memset(&Match, 0xCC, sizeof(Match));
        DsploopCompare(Data, DSPLOOP_CAPTURE_FRAMES, 6, 32, 32, DSPLOOP_FRAMES, DSPLOOP_MAX_OFFSET, 0, &Match);
        CHECK_EQ(Match.FirstDifference, 500);
        CHECK_EQ(Match.Channel, 5);
        CHECK_EQ(Match.MaxError, 65536);
        free(Data);
    }
    /* a channel that is silence from the start: no window matches, the first differing sample is in it */
    Data = BuildCapture(2, 16, 16, 0, DSPLOOP_FRAMES, DSPLOOP_CAPTURE_FRAMES, 0);
    CHECK(Data != NULL);
    if (Data)
    {
        uint32_t N;

        for (N = 0; N < DSPLOOP_CAPTURE_FRAMES; N++)
            WriteSample(Data, 2, 16, 16, N, 1, 0);
        memset(&Match, 0xCC, sizeof(Match));
        CHECK(DsploopCompare(Data, DSPLOOP_CAPTURE_FRAMES, 2, 16, 16, DSPLOOP_FRAMES, DSPLOOP_MAX_OFFSET, 0, &Match) == 0);
        CHECK_EQ(Match.FirstDifference, 1);
        CHECK_EQ(Match.Channel, 1);
        free(Data);
    }
}

/*
 * The gain (N14-02): the word of a level is the driver's table's, the tone
 * as the gain forwards it is the driver's arithmetic (stream.c
 * IntcDspGainApply, the plan's -6 dB figure), and a capture at a level
 * compares against the scaled tone where the unscaled one finds no match.
 */
static
void
CheckGain(
    void)
{
    /* container and valid bits of the three depths */
    static const int Depths[3][2] = { {16, 16}, {32, 24}, {32, 32} };
    const uint32_t Minus6 = 0x4026E73Du, Half = 0x40000000u;
    uint8_t* Data;
    DSPLOOP_MATCH Match;
    uint32_t F, N;
    int D;

    /* the table: unity at 0 and above, the whole decibels the table's words, -96 dB and below the last word, muted 0 */
    CHECK_EQ(DsploopGainWord(0, 0), DSPLOOP_GAIN_UNITY);
    CHECK_EQ(DsploopGainWord(100 * 65536, 0), DSPLOOP_GAIN_UNITY);
    CHECK_EQ(DsploopGainWord(-6 * 65536, 0), Minus6);
    CHECK_EQ(DsploopGainWord(-1 * 65536, 0), 0x721482C0);
    CHECK_EQ(DsploopGainWord(-96 * 65536, 0), 0x84F3);
    CHECK_EQ(DsploopGainWord(-200 * 65536, 0), 0x84F3);
    CHECK_EQ(DsploopGainWord(-6 * 65536, 1), 0);
    /* between two decibels: the straight line between their words, -5.5 dB halfway between the -5 and the -6 ones, the mean rounded up; a hair from -6 the -6 word less a sixteenth five hundred and thirty fifths of the way */
    CHECK_EQ(DsploopGainWord(-5 * 65536, 0), 0x47FACCF0);
    CHECK_EQ(DsploopGainWord(-5 * 65536 - 32768, 0), (int32_t)(((int64_t)0x47FACCF0 + Minus6 + 1) / 2));
    CHECK_EQ(DsploopGainWord(-6 * 65536 + 1, 0), (int32_t)(((int64_t)0x47FACCF0 + (int64_t)Minus6 * 65535 + 32768) / 65536));
    /* N14-17 (ruling (a)): the mapping is the node's. The endpoint's answer maps the scalar last set to decibels in user mode (the tray's 92 % is -96 + 0.92 x 96 = -7.68 dB), while the node's step is the driver's (1 dB) and the stream runs at the step nearest the request, -8.00 dB: dsploop scales its expectation by the node's level, the whole decibel's word, and the scalar's word stands about 4 % above it (the run that matched no frame) */
    {
        float ScalarDb = -96.0f + 0.92f * 96.0f;
        int32_t ScalarLevel = (int32_t)floorf(ScalarDb * 65536.0f + 0.5f);
        int64_t Node = DsploopGainWord(-8 * 65536, 0), Scalar = DsploopGainWord(ScalarLevel, 0);
        CHECK(fabs((double)Node / 2147483648.0 - pow(10.0, -8.0 / 20.0)) <= 1.0 / 2147483648.0);
        CHECK(Scalar > Node && Scalar * 100 / Node >= 103 && Scalar * 100 / Node <= 104);
    }
    /* the word falls with the level, and the whole decibels are 2^31 x 10^(-dB/20) within a unit */
    for (F = 0; F < 96; F++)
    {
        double Linear = pow(10.0, -(double)F / 20.0);

        CHECK(DsploopGainWord(-(int32_t)(F * 65536), 0) > DsploopGainWord(-(int32_t)((F + 1) * 65536), 0));
        CHECK(fabs((double)DsploopGainWord(-(int32_t)(F * 65536), 0) / 2147483648.0 - Linear) <= 1.0 / 2147483648.0);
    }
    /* unity is the sample itself at every depth, mute is silence */
    for (D = 0; D < 3; D++)
        for (F = 0; F < 48; F++)
        {
            CHECK_EQ(DsploopToneSampleAtGain(F, Depths[D][0], Depths[D][1], DSPLOOP_GAIN_UNITY), DsploopToneSample(F, Depths[D][1]));
            CHECK_EQ(DsploopToneSampleAtGain(F, Depths[D][0], Depths[D][1], 0), 0);
        }
    /* an exact half in Q1.31: the whole halves, and the odd ones the shift's floor: 2139 to 1069, -2139 to -1070; at 24 valid in 32 and at 32 the halves are whole */
    CHECK_EQ(DsploopToneSampleAtGain(12, 16, 16, Half), 8192);
    CHECK_EQ(DsploopToneSampleAtGain(1, 16, 16, Half), 1069);
    CHECK_EQ(DsploopToneSampleAtGain(25, 16, 16, Half), -1070);
    CHECK_EQ(DsploopToneSampleAtGain(12, 32, 24, Half), DsploopToneSample(12, 24) / 2);
    CHECK_EQ(DsploopToneSampleAtGain(12, 32, 32, Half), DsploopToneSample(12, 32) / 2);
    /* the driver's arithmetic at the plan's figure, every depth and frame: the container's whole times the word, rounded, over 2^31, back in the container */
    for (D = 0; D < 3; D++)
    {
        int Shift = 32 - Depths[D][0], Raise = Depths[D][0] - Depths[D][1];

        for (F = 0; F < 48; F++)
        {
            int64_t Sample = (int64_t)DsploopToneSample(F, Depths[D][1]) * ((int64_t)1 << Raise);
            double Wide = (double)Sample * (double)((int64_t)1 << Shift);
            double Product = floor((Wide * 1076291389.0 + 1073741824.0) / 2147483648.0);
            int64_t Expected = (int64_t)floor(Product / (double)((int64_t)1 << Shift)) >> Raise;

            CHECK_EQ(DsploopToneSampleAtGain(F, Depths[D][0], Depths[D][1], Minus6), Expected);
        }
    }
    /* the tolerance: the tone's own at unity, one unit more under a gain */
    CHECK_EQ(DsploopToleranceAtGain(16, DSPLOOP_GAIN_UNITY), 1);
    CHECK_EQ(DsploopToleranceAtGain(24, DSPLOOP_GAIN_UNITY), 0);
    CHECK_EQ(DsploopToleranceAtGain(16, Minus6), 2);
    CHECK_EQ(DsploopToleranceAtGain(32, Minus6), 1);
    CHECK_EQ(DsploopToleranceAtGain(16, 0), 2);
    /* a capture at -6 dB in 16 bits: compared against the scaled tone it matches from the first frame, every sample exact; the unscaled tone finds no window in it */
    Data = (uint8_t*)calloc(200, 4);
    CHECK(Data != NULL);
    if (Data)
    {
        for (N = 0; N < 100; N++)
            for (D = 0; D < 2; D++)
                StoreSample(Data + N * 4 + D * 2, 16, 16, DsploopToneSampleAtGain(N, 16, 16, Minus6));
        DsploopCompareAtGain(Data, 200, 2, 16, 16, 100, 48000, 2, Minus6, &Match);
        CHECK(Match.Matched && Match.Offset == 0 && Match.Compared == 100 && Match.FirstDifference == DSPLOOP_NO_DIFFERENCE);
        CHECK_EQ(Match.MaxError, 0);
        DsploopCompare(Data, 200, 2, 16, 16, 100, 10, 2, &Match);
        CHECK(!Match.Matched);
        /* one sample off by three units past the first window is found at its frame, its expected the scaled tone's, the offset kept */
        WriteSample(Data, 2, 16, 16, 60, 1, DsploopToneSampleAtGain(60, 16, 16, Minus6) + 3);
        DsploopCompareAtGain(Data, 200, 2, 16, 16, 100, 48000, 2, Minus6, &Match);
        CHECK(Match.Matched && Match.Offset == 0 && Match.FirstDifference == 60 && Match.Channel == 1);
        CHECK_EQ(Match.Expected, DsploopToneSampleAtGain(60, 16, 16, Minus6));
        free(Data);
    }
    /* the same at 24 valid in 32 */
    Data = (uint8_t*)calloc(200, 8);
    CHECK(Data != NULL);
    if (Data)
    {
        for (N = 0; N < 100; N++)
            for (D = 0; D < 2; D++)
                StoreSample(Data + N * 8 + D * 4, 32, 24, DsploopToneSampleAtGain(N, 32, 24, Minus6));
        DsploopCompareAtGain(Data, 200, 2, 32, 24, 100, 48000, 1, Minus6, &Match);
        CHECK(Match.Matched && Match.Offset == 0 && Match.Compared == 100 && Match.FirstDifference == DSPLOOP_NO_DIFFERENCE);
        CHECK_EQ(Match.MaxError, 0);
        DsploopCompare(Data, 200, 2, 32, 24, 100, 10, 0, &Match);
        CHECK(!Match.Matched);
        free(Data);
    }
}

/*
 * The walk's direction (walk.h, N15-20): the parts upstream of a render endpoint's connector, downstream of a capture endpoint's, and the two flows as IConnector::GetDataFlow answers them
 */
static
void
CheckWalkDirection(
    void)
{
    CHECK_EQ(DsploopFlowIn, 0);
    CHECK_EQ(DsploopFlowOut, 1);
    CHECK_EQ(DsploopWalkIncoming(DsploopFlowIn), 1);
    CHECK_EQ(DsploopWalkIncoming(DsploopFlowOut), 0);
}

int
main(
    void)
{
    CheckTone();
    CheckExact();
    CheckTolerance();
    CheckDamaged();
    CheckNoMatch();
    CheckMidCycle();
    CheckGain();
    CheckWalkDirection();
    printf("dsploop: %d checks, %d failures\n", Checks, Failures);
    return Failures != 0;
}
