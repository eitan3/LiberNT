/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     The comparison of the dsploop apitest, free of I/O so the host tests run the same code: a captured stream against the tone from the first frame that matches, within a tolerance in LSBs, and the stop at the first sample beyond it
 * COPYRIGHT:   Copyright 2026 LiberNT contributors
 */

#include <stddef.h>
#include "compare.h"

/*
 * The tone at 16 valid bits: half the full scale, 16384 times the sine of
 * 2 pi k / 48 for the frame k of a cycle, rounded half away from zero. At 24
 * and 32 valid bits the same figures stand for 256 and 65536 times as much.
 */
static const int32_t Tone16[DSPLOOP_TONE_FRAMES] =
{
         0,   2139,   4240,   6270,   8192,   9974,  11585,  12998,
     14189,  15137,  15826,  16244,  16384,  16244,  15826,  15137,
     14189,  12998,  11585,   9974,   8192,   6270,   4240,   2139,
         0,  -2139,  -4240,  -6270,  -8192,  -9974, -11585, -12998,
    -14189, -15137, -15826, -16244, -16384, -16244, -15826, -15137,
    -14189, -12998, -11585,  -9974,  -8192,  -6270,  -4240,  -2139
};

int32_t
DsploopToneSample(
    uint32_t Frame,
    int ValidBits)
{
    /* a multiplication, not a shift: the figures are negative as often as not */
    return Tone16[Frame % DSPLOOP_TONE_FRAMES] * (ValidBits == 24 ? 256 : ValidBits == 32 ? 65536 : 1);
}

int
DsploopTolerance(
    int ValidBits)
{
    return ValidBits == 16 ? 1 : 0;
}

/* The whole decibels from 0 to -96, each the rounded 2^31 x 10^(-dB/20): stream.c IntcDspGainTable */
static const uint32_t GainTable[97] =
{
    0x7FFFFFFF, 0x721482C0, 0x65AC8C2F, 0x5A9DF7AC, 0x50C335D4, 0x47FACCF0,
    0x4026E73D, 0x392CED8E, 0x32F52CFF, 0x2D6A866F, 0x287A26C5, 0x241346F6,
    0x2026F310, 0x1CA7D768, 0x198A1357, 0x16C310E3, 0x144960C5, 0x12149A60,
    0x101D3F2E, 0x0E5CA14C, 0x0CCCCCCD, 0x0B68737A, 0x0A2ADAD2, 0x090FCBF8,
    0x08138562, 0x0732AE18, 0x066A4A53, 0x05B7B15B, 0x05188480, 0x048AA70B,
    0x040C3714, 0x039B8719, 0x0337184E, 0x02DD958A, 0x028DCEBC, 0x0246B4E4,
    0x0207567A, 0x01CEDC3D, 0x019C8651, 0x016FA9BB, 0x0147AE14, 0x01240B8C,
    0x01044915, 0x00E7FACC, 0x00CEC08A, 0x00B8449C, 0x00A43AA2, 0x00925E89,
    0x008273A6, 0x007443E8, 0x00679F1C, 0x005C5A4F, 0x00524F3B, 0x00495BC1,
    0x00416179, 0x003A454A, 0x0033EF0C, 0x002E4939, 0x002940A2, 0x0024C42C,
    0x0020C49C, 0x001D345B, 0x001A074F, 0x001732AE, 0x0014ACDB, 0x00126D43,
    0x00106C43, 0x000EA30E, 0x000D0B91, 0x000BA064, 0x000A5CB6, 0x00093C3B,
    0x00083B20, 0x000755FA, 0x000689BF, 0x0005D3BB, 0x00053181, 0x0004A0EC,
    0x00042010, 0x0003AD38, 0x000346DC, 0x0002EBA3, 0x00029A55, 0x000251DE,
    0x00021149, 0x0001D7BA, 0x0001A46D, 0x000176B5, 0x00014DF5, 0x000129A4,
    0x00010945, 0x0000EC6C, 0x0000D2B6, 0x0000BBCC, 0x0000A760, 0x0000952C,
    0x000084F3
};

uint32_t
DsploopGainWord(
    int32_t Level,
    int Mute)
{
    uint32_t Down, Whole, Part;
    uint64_t Low, High;

    if (Mute)
        return 0;
    if (Level >= 0)
        return DSPLOOP_GAIN_UNITY;
    Down = (uint32_t)-(int64_t)Level;
    if (Down >= (uint32_t)(96 * 65536))
        return GainTable[96];
    Whole = Down / 65536;
    Part = Down % 65536;
    Low = GainTable[Whole];
    if (Part == 0)
        return (uint32_t)Low;
    High = GainTable[Whole + 1];
    return (uint32_t)((Low * (65536 - Part) + High * Part + 32768) / 65536);
}

/* One sample in its container as the gain forwards it: stream.c IntcDspGainApply, the same arithmetic */
static
int32_t
GainApply(
    int32_t Sample,
    int ContainerBits,
    uint32_t Gain)
{
    int64_t Wide, Product;
    int Shift = 32 - ContainerBits;

    if (Gain == DSPLOOP_GAIN_UNITY)
        return Sample;
    Wide = (int64_t)Sample * ((int64_t)1 << Shift);
    Product = ((Wide * (int64_t)Gain) + ((int64_t)1 << 30)) >> 31;
    if (Product > (int64_t)0x7FFFFFFF)
        Product = (int64_t)0x7FFFFFFF;
    else if (Product < -(int64_t)0x80000000)
        Product = -(int64_t)0x80000000;
    return (int32_t)(Product >> Shift);
}

int32_t
DsploopToneSampleAtGain(
    uint32_t Frame,
    int ContainerBits,
    int ValidBits,
    uint32_t Gain)
{
    int32_t Sample = DsploopToneSample(Frame, ValidBits);
    int Raise = ContainerBits - ValidBits;

    /* the tone in its container, the valid bits in the container's top as the tone is stored */
    if (Raise != 0)
        Sample = (int32_t)((int64_t)Sample * ((int64_t)1 << Raise));
    Sample = GainApply(Sample, ContainerBits, Gain);
    /* back in the units of the valid bits: the captured word's upper bits are the valid ones (CapturedSample), an arithmetic shift as there */
    return Raise != 0 ? Sample >> Raise : Sample;
}

int
DsploopToleranceAtGain(
    int ValidBits,
    uint32_t Gain)
{
    return Gain == DSPLOOP_GAIN_UNITY ? DsploopTolerance(ValidBits) : DsploopTolerance(ValidBits) + 1;
}

/* One captured sample in the units of its valid bits: a little-endian word, of which the upper bits are valid when there are 24 of them in 32 */
static
int32_t
CapturedSample(
    const uint8_t* Data,
    int ContainerBits,
    int ValidBits)
{
    uint32_t Word = (uint32_t)Data[0] | ((uint32_t)Data[1] << 8);
    int32_t Value;

    if (ContainerBits == 32)
        Word |= ((uint32_t)Data[2] << 16) | ((uint32_t)Data[3] << 24);
    Value = ContainerBits == 16 ? (int32_t)(int16_t)(uint16_t)Word : (int32_t)Word;
    if (ContainerBits > ValidBits)
        Value >>= ContainerBits - ValidBits;
    return Value;
}

int
DsploopCompare(
    const uint8_t* Captured,
    uint32_t CapturedFrames,
    unsigned Channels,
    int ContainerBits,
    int ValidBits,
    uint32_t Frames,
    uint32_t MaxOffset,
    int Tolerance,
    DSPLOOP_MATCH* Match)
{
    return DsploopCompareAtGain(Captured, CapturedFrames, Channels, ContainerBits, ValidBits, Frames, MaxOffset, Tolerance,
                                DSPLOOP_GAIN_UNITY, Match);
}

int
DsploopCompareAtGain(
    const uint8_t* Captured,
    uint32_t CapturedFrames,
    unsigned Channels,
    int ContainerBits,
    int ValidBits,
    uint32_t Frames,
    uint32_t MaxOffset,
    int Tolerance,
    uint32_t Gain,
    DSPLOOP_MATCH* Match)
{
    uint32_t FrameBytes = Channels * (unsigned)(ContainerBits / 8);
    uint32_t Last, First, Count, F, C;
    /* the offset is searched only in a capture that holds a window */
    int Search = CapturedFrames >= DSPLOOP_WINDOW_FRAMES;

    Match->Matched = 0;
    Match->Offset = 0;
    Match->Compared = 0;
    Match->MaxError = 0;
    Match->FirstDifference = DSPLOOP_NO_DIFFERENCE;
    Match->Channel = 0;
    Match->Expected = 0;
    Match->Captured = 0;
    if (Captured == NULL || Channels == 0 || Frames == 0 || Tolerance < 0 ||
        (ContainerBits != 16 && ContainerBits != 32) ||
        (ValidBits != 16 && ValidBits != 24 && ValidBits != 32) || ValidBits > ContainerBits)
        return 0;

    /* the offset: the first frame where a window is the tone's within the tolerance, up to MaxOffset and up to what the capture holds of a window */
    Last = 0;
    if (Search)
    {
        Last = MaxOffset;
        if (Last > CapturedFrames - DSPLOOP_WINDOW_FRAMES)
            Last = CapturedFrames - DSPLOOP_WINDOW_FRAMES;
    }
    for (First = 0; Search && First <= Last && !Match->Matched; First++)
    {
        Match->Matched = 1;
        for (F = 0; F < DSPLOOP_WINDOW_FRAMES && Match->Matched; F++)
        {
            for (C = 0; C < Channels; C++)
            {
                int32_t Expected = DsploopToneSampleAtGain(F, ContainerBits, ValidBits, Gain);
                int32_t Got = CapturedSample(Captured + (size_t)(First + F) * FrameBytes + C * (unsigned)(ContainerBits / 8), ContainerBits, ValidBits);
                int64_t Error = Got > Expected ? (int64_t)Got - Expected : (int64_t)Expected - Got;

                if (Error > Tolerance)
                {
                    Match->Matched = 0;
                    break;
                }
            }
        }
    }
    if (Match->Matched)
        Match->Offset = First - 1;
    /* no window matched: the frames are compared from the first, for the record */
    else
        Match->Offset = 0;

    Count = Frames;
    if (Count > CapturedFrames - Match->Offset)
        Count = CapturedFrames - Match->Offset;
    Match->Compared = Count;
    for (F = 0; F < Count; F++)
    {
        for (C = 0; C < Channels; C++)
        {
            int32_t Expected = DsploopToneSampleAtGain(F, ContainerBits, ValidBits, Gain);
            int32_t Got = CapturedSample(Captured + (size_t)(Match->Offset + F) * FrameBytes + C * (unsigned)(ContainerBits / 8),
                                         ContainerBits, ValidBits);
            int64_t Error = Got > Expected ? (int64_t)Got - Expected : (int64_t)Expected - Got;

            if (Error > Match->MaxError)
                Match->MaxError = Error;
            if (Error > Tolerance && Match->FirstDifference == DSPLOOP_NO_DIFFERENCE)
            {
                Match->FirstDifference = F;
                Match->Channel = C;
                Match->Expected = Expected;
                Match->Captured = Got;
            }
        }
    }
    return Match->Matched;
}
