/*
 * PROJECT:     LiberNT Core Audio
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     PCM sample conversion between the 32-bit mix word and 8/16/24/32-bit containers
 * COPYRIGHT:   Copyright 2026 LiberNT contributors
 */

#pragma once

#include <string.h>

/* A sample is carried between conversions as a 32-bit signed integer spanning
   the full scale; valid_bits of it are significant and sit at the top of the
   container, the unused low bits of the container read as zero and are
   written as zero. */
static __inline INT32
read_pcm_sample(const BYTE *data, WORD bits, WORD valid_bits)
{
    UINT32 raw;
    WORD unused_bits;

    if (!valid_bits || valid_bits > bits)
        valid_bits = bits;
    unused_bits = bits - valid_bits;

    switch (bits)
    {
        case 8:
            raw = data[0] & (0xffu << unused_bits);
            return ((INT32)raw - 128) * 0x1000000;
        case 16:
            raw = data[0] | ((UINT32)data[1] << 8);
            raw &= 0xffffu << unused_bits;
            return (INT32)(raw << 16);
        case 24:
            raw = data[0] | ((UINT32)data[1] << 8) | ((UINT32)data[2] << 16);
            raw &= 0xffffffu << unused_bits;
            return (INT32)(raw << 8);
        case 32:
            memcpy(&raw, data, sizeof(raw));
            raw &= 0xffffffffu << unused_bits;
            return (INT32)raw;
        default:
            return 0;
    }
}

static __inline void
write_pcm_sample(BYTE *data, WORD bits, WORD valid_bits, INT32 value)
{
    UINT32 raw = (UINT32)value;
    WORD byte_index = 0;
    WORD unused_bits;

    if (!valid_bits || valid_bits > bits)
        valid_bits = bits;
    unused_bits = bits - valid_bits;

    switch (bits)
    {
        case 8:
            data[0] = (BYTE)(raw >> 24) ^ 0x80;
            break;
        case 16:
            data[0] = (BYTE)(raw >> 16);
            data[1] = (BYTE)(raw >> 24);
            break;
        case 24:
            data[0] = (BYTE)(raw >> 8);
            data[1] = (BYTE)(raw >> 16);
            data[2] = (BYTE)(raw >> 24);
            break;
        case 32:
            memcpy(data, &raw, sizeof(raw));
            break;
        default:
            return;
    }

    while (unused_bits >= 8)
    {
        data[byte_index++] = 0;
        unused_bits -= 8;
    }
    if (unused_bits)
        data[byte_index] &= (BYTE)(0xffu << unused_bits);
}

static __inline INT32
saturate_sample(INT64 sample)
{
    if (sample > 0x7fffffffLL)
        return 0x7fffffff;
    if (sample < (-0x7fffffffLL - 1))
        return (-0x7fffffff - 1);
    return (INT32)sample;
}

/* Deterministic TPDF dither: a 32-bit xorshift sequence, reproducible run to run */
typedef struct pcm_dither
{
    UINT32 state;
} pcm_dither;

#define PCM_DITHER_INIT { 0x9e3779b9u }

/* The next 32-bit word of a xorshift32 sequence: every output but zero occurs once per period */
static __inline UINT32
pcm_dither_next(pcm_dither *dither)
{
    UINT32 x = dither->state;

    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    dither->state = x;
    return x;
}

/*
 * write_pcm_sample with triangular (TPDF) dither of one least-significant bit at
 * the valid-bit depth, added before the sample is narrowed: it turns the
 * truncation distortion into a fixed noise floor. The dither is a function of
 * the sample sequence and the seed, so a run is reproducible.
 */
static __inline void
write_pcm_sample_dithered(BYTE *data, WORD bits, WORD valid_bits, INT32 value, pcm_dither *dither)
{
    UINT32 first, second;
    WORD shift;

    if (!valid_bits || valid_bits > bits)
        valid_bits = bits;
    shift = (WORD)(32 - valid_bits);
    /* One LSB at the target depth is 2^shift in the 32-bit mix word; two uniform
       words of that size differ to a triangle from -1 to +1 LSB */
    first = pcm_dither_next(dither) >> shift;
    second = pcm_dither_next(dither) >> shift;
    write_pcm_sample(data, bits, valid_bits, saturate_sample((INT64)value + first - second));
}
