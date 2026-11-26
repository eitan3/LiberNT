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
    /* the first differing frame after the offset (DSPLOOP_NO_DIFFERENCE when every compared frame is the tone's), its channel and the two samples */
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

/*
 * Compares a captured stream of interleaved little-endian samples, in words
 * of ContainerBits (16 or 32) carrying ValidBits (16, 24 or 32), Channels
 * wide, against the tone's first Frames frames. The offset is the first
 * frame at which a window of DSPLOOP_WINDOW_FRAMES frames is the tone's,
 * searched up to MaxOffset; from there every frame that was captured is
 * compared, and the first that differs is recorded. Returns Match->Matched:
 * 1 when a window matched, 0 when none did, in which case the frames are
 * compared from the first one for the record.
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
    DSPLOOP_MATCH* Match);

#ifdef __cplusplus
}
#endif

#endif
