/*
 * PROJECT:     LiberNT host-native tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     The Core Audio engine's PCM sample conversion and dither (dll/win32/mmdevapi/pcm_sample.h), the same source the engine compiles: the seed leaves the xorshift's fixed point, a dithered write moves a sample by at most one LSB and is not constant on a constant input, the dither's amount is one LSB at 8, 16, 20, 24 and 32 valid bits (the reference derives it on its own), its sign is balanced (the positive and the negative draws are each half of many, within a tenth: a one-sided dither fails), and a sample at the full scale, or one 32-bit mix-word unit past it, stays at the full scale; under ASan and UBSan
 * COPYRIGHT:   Copyright 2026 LiberNT contributors
 */

#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* The Windows types pcm_sample.h uses */
typedef uint8_t BYTE;
typedef uint16_t WORD;
typedef int32_t INT32;
typedef int64_t INT64;
typedef uint32_t UINT32;
typedef uint64_t UINT64;

#include "pcm_sample.h"

#define SAMPLES 4096

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

/* One sample of an 8 or 16-bit container read back in its own scale */
static
int
Read(
    const BYTE* At,
    WORD Bits)
{
    if (Bits == 8)
        return (int)(int8_t)(At[0] ^ 0x80);
    return (int)(int16_t)((uint16_t)At[0] | ((uint16_t)At[1] << 8));
}

/* One LSB at a depth of ValidBits, in the 32-bit mix word */
#define LSB_OF(ValidBits)   (1ULL << (32 - (ValidBits)))

/*
 * The dither the implementation will add for its next sample, derived on
 * its own: a word of the sequence is a fraction of 2^32, one LSB of the depth
 * is 2^(32 - depth) in the mix word, so the word scaled into the LSB and
 * truncated is (word x 2^(32 - depth)) >> 32, in 64-bit arithmetic. The two
 * words come from a copy of the state. A TPDF dither is their difference.
 */
static
void
DrawDither(
    const pcm_dither* Dither,
    WORD ValidBits,
    INT64* First,
    INT64* Second)
{
    pcm_dither Ahead = *Dither;
    UINT64 Lsb = LSB_OF(ValidBits);

    *First = (INT64)(((UINT64)pcm_dither_next(&Ahead) * Lsb) >> 32);
    *Second = (INT64)(((UINT64)pcm_dither_next(&Ahead) * Lsb) >> 32);
}

/* The two words write_pcm_sample_dithered will draw from this state, in LSBs of the target depth, as the 16-bit tests below compare them */
static
void
StepAhead(
    const pcm_dither* Dither,
    WORD Bits,
    UINT32* First,
    UINT32* Second)
{
    INT64 First64, Second64;

    DrawDither(Dither, Bits, &First64, &Second64);
    *First = (UINT32)First64;
    *Second = (UINT32)Second64;
}

/* A 16 or 32-bit container read back as its signed word; the unused low bits are zero, so a shift by them gives the valid-bit units */
static
INT64
ReadUnits(
    const BYTE* At,
    WORD Container,
    WORD ValidBits)
{
    UINT32 Word = (UINT32)At[0] | ((UINT32)At[1] << 8);

    if (Container == 32)
        Word |= ((UINT32)At[2] << 16) | ((UINT32)At[3] << 24);
    else
        return (INT64)(int16_t)(uint16_t)Word >> (Container - ValidBits);
    return (INT64)(INT32)Word >> (Container - ValidBits);
}

/*
 * The dither's amount at a depth, in a container: every written sample is
 * within one LSB of the plain write, some are not the plain write (at 32
 * valid bits none is: nothing is below one LSB), and each is what the
 * reference says: the sample and the dither derived on its own, narrowed by
 * floor division. The inputs are far from the full scale and from the LSB
 * boundaries, so no saturation and no rounding coincidence helps a wrong
 * dither pass.
 */
static
void
TestDitherAmplitude(
    WORD Container,
    WORD ValidBits)
{
    static const INT32 Inputs[] =
    {
        0x12345678, 0x76543210, -0x23456789, 0x0f0f0f0f, -0x0f0f0f10, 0x00010000, -0x00010000, 0x23450000
    };
    pcm_dither Dither = (pcm_dither)PCM_DITHER_INIT;
    BYTE Plain[4], Dithered[4];
    INT64 Lsb = LSB_OF(ValidBits);
    int Moved = 0, i;

    for (i = 0; i < SAMPLES; ++i)
    {
        INT32 Value = Inputs[i % (int)(sizeof(Inputs) / sizeof(Inputs[0]))];
        INT64 First, Second, DitherMixWords, Sum, Expected, PlainUnits, GotUnits;

        DrawDither(&Dither, ValidBits, &First, &Second);
        /* the two words, and so their difference, are in 32-bit mix-word units (the random word with ValidBits bits dropped), as the sample is; the units read back below are the valid bits' */
        DitherMixWords = First - Second;
        memset(Plain, 0, sizeof(Plain));
        memset(Dithered, 0, sizeof(Dithered));
        write_pcm_sample(Plain, Container, ValidBits, Value);
        write_pcm_sample_dithered(Dithered, Container, ValidBits, Value, &Dither);
        PlainUnits = ReadUnits(Plain, Container, ValidBits);
        GotUnits = ReadUnits(Dithered, Container, ValidBits);
        CHECK(GotUnits - PlainUnits >= -1 && GotUnits - PlainUnits <= 1);
        Moved += GotUnits != PlainUnits;

        Sum = (INT64)Value + DitherMixWords;
        Expected = Sum >= 0 ? Sum / Lsb : -((-Sum + Lsb - 1) / Lsb);
        CHECK_EQ(GotUnits, Expected);
    }
    if (ValidBits < 32)
        CHECK(Moved > 0);
    else
        CHECK_EQ(Moved, 0);
}

/*
 * The dither's sign (R8-03), by hand, as the function applies it: each word
 * is a uniform integer of the 32 - ValidBits bits the random word keeps, M =
 * 2^(32 - ValidBits) values, and the dither is their difference, so it is
 * positive with (M - 1) / 2M and negative with the same. A sample written
 * half an LSB past a valid-bit boundary moves up when the dither is at least
 * half an LSB and down when it is at most minus half: each with 1 / 8 of the
 * draws (the pairs with F - S at least M / 2 are (M / 2)(M / 2 + 1) / 2 of
 * M squared), and it stays with 3 / 4. Over DITHER_DRAWS samples each move is
 * within a tenth of an eighth, so neither sign is missing and neither leans:
 * a dither of one sign fails. Read through the written sample, so it is the
 * dither the function applies, and not the sequence alone, that is measured.
 */
#define DITHER_DRAWS    65536

static
void
TestDitherSignBalance(
    WORD ValidBits)
{
    pcm_dither Dither = (pcm_dither)PCM_DITHER_INIT;
    INT32 Half = (INT32)(LSB_OF(ValidBits) / 2);
    int Up = 0, Down = 0, i;

    for (i = 0; i < DITHER_DRAWS; ++i)
    {
        BYTE Plain[4], Dithered[4];

        memset(Plain, 0, sizeof(Plain));
        memset(Dithered, 0, sizeof(Dithered));
        write_pcm_sample(Plain, 32, ValidBits, Half);
        write_pcm_sample_dithered(Dithered, 32, ValidBits, Half, &Dither);
        if (ReadUnits(Dithered, 32, ValidBits) > ReadUnits(Plain, 32, ValidBits))
            Up++;
        else if (ReadUnits(Dithered, 32, ValidBits) < ReadUnits(Plain, 32, ValidBits))
            Down++;
    }
    /* an eighth of the draws, within a tenth: 1638 to 14745 of 65536, each way */
    CHECK(Up >= DITHER_DRAWS / 40 && Up <= DITHER_DRAWS * 9 / 40);
    CHECK(Down >= DITHER_DRAWS / 40 && Down <= DITHER_DRAWS * 9 / 40);
}
/* The seed is not the xorshift's fixed point, and zero, the state an endpoint's calloc leaves without the seed, is */
static
void
TestSeedLeavesTheFixedPoint(void)
{
    pcm_dither Dither = (pcm_dither)PCM_DITHER_INIT;
    pcm_dither Before = Dither;
    pcm_dither Zero = { 0 };
    UINT32 First, Value;
    int Distinct = 0, i;

    First = pcm_dither_next(&Dither);
    CHECK(First != 0);
    CHECK(Dither.state != Before.state);
    for (i = 1; i < 64; ++i)
    {
        Value = pcm_dither_next(&Dither);
        CHECK(Value != 0);
        if (Value != First)
            Distinct = 1;
    }
    CHECK(Distinct);

    for (i = 0; i < 64; ++i)
        CHECK_EQ(pcm_dither_next(&Zero), 0);
    CHECK_EQ(Zero.state, 0);
}

/* A constant input is written as itself or one LSB away from it on either side, and not always the same: the dither is applied */
static
void
TestConstantInputIsDithered(void)
{
    pcm_dither Dither = (pcm_dither)PCM_DITHER_INIT;
    BYTE Plain[2] = { 0, 0 }, Dithered[2];
    int PlainValue, Value, Same = 0, Moved = 0, i;

    write_pcm_sample(Plain, 16, 16, 0x12345678);
    PlainValue = Read(Plain, 16);
    CHECK_EQ(PlainValue, 0x1234);
    for (i = 0; i < SAMPLES; ++i)
    {
        memset(Dithered, 0, sizeof(Dithered));
        write_pcm_sample_dithered(Dithered, 16, 16, 0x12345678, &Dither);
        Value = Read(Dithered, 16);
        CHECK(Value >= PlainValue - 1 && Value <= PlainValue + 1);
        if (Value == PlainValue)
            ++Same;
        else
            ++Moved;
    }
    CHECK(Same > 0);
    CHECK(Moved > 0);
}

/* A sample at the full scale stays at it whichever way the dither steps, and the dither does step past it */
static
void
TestFullScaleStaysAtFullScale(
    WORD Bits)
{
    pcm_dither Dither = (pcm_dither)PCM_DITHER_INIT;
    BYTE Data[4];
    int Max = (1 << (Bits - 1)) - 1, Min = -(1 << (Bits - 1));
    int PastPositive = 0, PastNegative = 0, i;
    UINT32 First, Second;

    for (i = 0; i < SAMPLES; ++i)
    {
        StepAhead(&Dither, Bits, &First, &Second);
        memset(Data, 0, sizeof(Data));
        write_pcm_sample_dithered(Data, Bits, Bits, 0x7fffffff, &Dither);
        CHECK_EQ(Read(Data, Bits), Max);
        if (First > Second)
            ++PastPositive;

        StepAhead(&Dither, Bits, &First, &Second);
        memset(Data, 0, sizeof(Data));
        write_pcm_sample_dithered(Data, Bits, Bits, (-0x7fffffff - 1), &Dither);
        CHECK_EQ(Read(Data, Bits), Min);
        if (Second > First)
            ++PastNegative;
    }
    CHECK(PastPositive > 0);
    CHECK(PastNegative > 0);
}

/* The sum of the sample and the dither lands exactly one 32-bit mix-word unit past the full scale on both sides: the words are in mix-word units, and one LSB at 16 valid bits is 65536 of them */
static
void
TestOneMixWordPastFullScale(void)
{
    pcm_dither Dither = (pcm_dither)PCM_DITHER_INIT;
    BYTE Data[4];
    int FoundUp = 0, FoundDown = 0, i;
    UINT32 First, Second;

    for (i = 0; i < 1000000 && !(FoundUp && FoundDown); ++i)
    {
        StepAhead(&Dither, 16, &First, &Second);
        if (First - Second == 1 && !FoundUp)
        {
            memset(Data, 0, sizeof(Data));
            write_pcm_sample_dithered(Data, 16, 16, 0x7fffffff, &Dither);
            CHECK_EQ(Read(Data, 16), 0x7fff);
            FoundUp = 1;
        }
        else if (Second - First == 1 && !FoundDown)
        {
            memset(Data, 0, sizeof(Data));
            write_pcm_sample_dithered(Data, 16, 16, (-0x7fffffff - 1), &Dither);
            CHECK_EQ(Read(Data, 16), (int)(int16_t)0x8000);
            FoundDown = 1;
        }
        else
        {
            /* Skip this sample's two words */
            pcm_dither_next(&Dither);
            pcm_dither_next(&Dither);
        }
    }
    CHECK(FoundUp);
    CHECK(FoundDown);
}

int
main(void)
{
    TestSeedLeavesTheFixedPoint();
    TestConstantInputIsDithered();
    TestFullScaleStaysAtFullScale(16);
    TestFullScaleStaysAtFullScale(8);
    TestOneMixWordPastFullScale();
    TestDitherSignBalance(16);
    TestDitherSignBalance(8);
    TestDitherAmplitude(16, 8);
    TestDitherAmplitude(32, 8);
    TestDitherAmplitude(16, 16);
    TestDitherAmplitude(32, 16);
    TestDitherAmplitude(32, 20);
    TestDitherAmplitude(32, 24);
    TestDitherAmplitude(32, 32);
    printf("pcmsample: %d checks, %d failures\n", Checks, Failures);
    return Failures ? 1 : 0;
}
