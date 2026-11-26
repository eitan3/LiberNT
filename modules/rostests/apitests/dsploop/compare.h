/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     The comparison of the dsploop apitest, free of I/O so the host tests run the same code: a captured stream against the tone from the first frame that matches, and the stop at the first differing sample
 * COPYRIGHT:   Copyright 2026 LiberNT contributors
 */

#ifndef _APITESTS_DSPLOOP_COMPARE_H_
#define _APITESTS_DSPLOOP_COMPARE_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The tone: 1 kHz at 48 kHz is 48 frames a cycle, the same in every channel */
#define DSPLOOP_TONE_FRAMES         48
/* The frames a candidate offset must match before it is the tone's start */
#define DSPLOOP_WINDOW_FRAMES       DSPLOOP_TONE_FRAMES
/* No differing sample */
#define DSPLOOP_NO_DIFFERENCE       0xFFFFFFFFu

/* What a comparison found */
typedef struct _DSPLOOP_MATCH
{
    /* a window matched at Offset, or none did within the range searched */
    int Matched;
    /* the captured frame the tone starts at, 0 when none matched */
    uint32_t Offset;
    /* the frames compared after the offset */
    uint32_t Compared;
    /* the largest distance of a captured sample from the tone's over them */
    int64_t MaxError;
    /* the first differing frame after the offset by more than the tolerance (DSPLOOP_NO_DIFFERENCE when every compared frame is within it), its channel and the two samples */
    uint32_t FirstDifference;
    uint32_t Channel;
    int32_t Expected;
    int32_t Captured;
} DSPLOOP_MATCH;

/* The tone's sample of a frame in the units of ValidBits (16, 24 or 32) */
int32_t
DsploopToneSample(
    uint32_t Frame,
    int ValidBits);

/* The tolerance of a comparison at a depth, in LSBs: the render engine dithers its 16-bit narrowing by one LSB (pcm_sample.h), so a captured 16-bit tone is within one LSB of the rendered one; 24 and 32 valid bits are not dithered and are compared exactly */
int
DsploopTolerance(
    int ValidBits);

/*
 * Compares a captured stream of interleaved little-endian samples, in words
 * of ContainerBits (16 or 32) carrying ValidBits (16, 24 or 32), Channels
 * wide, against the tone's first Frames frames. The offset is the first
 * frame at which a window of DSPLOOP_WINDOW_FRAMES frames is the tone's
 * within Tolerance LSBs, searched up to MaxOffset; from there every frame
 * that was captured is compared, and the first that differs by more than
 * Tolerance LSBs is recorded. Returns Match->Matched: 1 when a window
 * matched, 0 when none did, in which case the frames are compared from the
 * first one for the record.
 */
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
    DSPLOOP_MATCH* Match);

#ifdef __cplusplus
}
#endif

#endif
