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
                int32_t Expected = DsploopToneSample(F, ValidBits);
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
            int32_t Expected = DsploopToneSample(F, ValidBits);
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
