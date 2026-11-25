/*
 * PROJECT:     LiberNT kernel-mode tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     A synthetic ALSA SoC topology binary and the facts expected from it, shared by the kmtest and the host test
 * COPYRIGHT:   Copyright 2026 LiberNT contributors
 */

#ifndef _SOFTPLG_CASES_H_
#define _SOFTPLG_CASES_H_

#include <softplg.h>
#include "dsp_cases.h"

#define Z4  0, 0, 0, 0
#define Z8  Z4, Z4
#define Z16 Z8, Z8
#define Z32 Z16, Z16
#define Z64 Z32, Z32
#define Z128 Z64, Z64
#define Z256 Z128, Z128
#define Z512 Z256, Z256

/* Block header: magic "CoSA" read little-endian, ABI 5, version 0, type, header size 36, vendor type 0, payload size, index, count */
#define TPLG_HEADER(Type, Payload, Index, Count) \
    0x43, 0x6F, 0x53, 0x41, DSP_U32(5), DSP_U32(0), DSP_U32(Type), DSP_U32(36), DSP_U32(0), DSP_U32(Payload), \
    DSP_U32(Index), DSP_U32(Count)

/* A name field of 44 bytes: the characters given, then zeros */
#define TPLG_NAME4(A, B, C, D) (A), (B), (C), (D), Z32, Z8
#define TPLG_NAME5(A, B, C, D, E) (A), (B), (C), (D), (E), Z32, Z4, 0, 0, 0
#define TPLG_NAME6(A, B, C, D, E, F) (A), (B), (C), (D), (E), (F), Z32, Z4, 0, 0
#define TPLG_NAME8(A, B, C, D, E, F, G, H) (A), (B), (C), (D), (E), (F), (G), (H), Z32, Z4

/* Widget of 132 bytes: size, id, name, sname, reg, shift, mask, subseq, invert, ignore suspend, event flags, event type, controls, private size */
#define TPLG_WIDGET(Name, Sname, Id, Controls, PrivateSize) \
    DSP_U32(132), DSP_U32(Id), Name, Sname, DSP_U32(0), DSP_U32(0), DSP_U32(0), DSP_U32(0), DSP_U32(0), DSP_U32(0), \
    DSP_U16(0), DSP_U16(0), DSP_U32(Controls), DSP_U32(PrivateSize)

/* Mixer control: header of 204 bytes (size, type, name, access, ops, a TLV of 136 bytes), body of 156 bytes (size 360, min, max, platform max, invert, channels, 8 channels, private size) */
#define TPLG_MIXER(Name, PrivateSize) \
    DSP_U32(204), DSP_U32(SoftplgTypeMixer), Name, DSP_U32(3), Z8, Z4, DSP_U32(0), DSP_U32(0), Z128, \
    DSP_U32(360), DSP_U32(0), DSP_U32(100), DSP_U32(0), DSP_U32(0), DSP_U32(2), Z128, DSP_U32(PrivateSize)

/* Bytes control: the same header, body of 36 bytes (size 240, maximum, mask, base, registers, three operations, private size) */
#define TPLG_BYTES_CONTROL(Name, PrivateSize) \
    DSP_U32(204), DSP_U32(SoftplgTypeBytes), Name, DSP_U32(3), Z8, Z4, DSP_U32(0), DSP_U32(0), Z128, \
    DSP_U32(240), DSP_U32(255), DSP_U32(255), DSP_U32(0), DSP_U32(1), Z8, Z4, DSP_U32(PrivateSize)

/* Enum control: the same header, body of 1560 bytes (size 1764, no channels, 8 channel records of 16 bytes, no items, 16 texts of 44 bytes, 176 values, private size) */
#define TPLG_ENUM_CONTROL(Name, PrivateSize) \
    DSP_U32(204), DSP_U32(SoftplgTypeEnum), Name, DSP_U32(3), Z8, Z4, DSP_U32(0), DSP_U32(0), Z128, \
    DSP_U32(1764), DSP_U32(0), Z128, DSP_U32(0), DSP_U32(0), DSP_U32(0), Z512, Z128, Z64, Z512, Z128, Z64, DSP_U32(PrivateSize)

/* One vendor array of word tuples: size 12 + 8 * Count, type 4, then (token, value) pairs */
#define TPLG_WORDS(Count, ...) DSP_U32(12 + 8 * (Count)), DSP_U32(SOFTPLG_TUPLE_WORD), DSP_U32(Count), __VA_ARGS__
#define TPLG_WORD(Token, Value) DSP_U32(Token), DSP_U32(Value)
/* One vendor array holding one string tuple: size 60, type 1 */
#define TPLG_STRING(Token, Text) DSP_U32(60), DSP_U32(SOFTPLG_TUPLE_STRING), DSP_U32(1), DSP_U32(Token), Text

/* Stream capabilities of 104 bytes: size, name, formats, rates, rate min and max, channels min and max, periods, sizes, significant bits */
#define TPLG_CAPS(Name, Formats, Rates, RateMin, RateMax, Channels) \
    DSP_U32(104), Name, DSP_U64(Formats), DSP_U32(Rates), DSP_U32(RateMin), DSP_U32(RateMax), DSP_U32(Channels), DSP_U32(Channels), \
    DSP_U32(0), DSP_U32(0), DSP_U32(0), DSP_U32(0), DSP_U32(0), DSP_U32(0), DSP_U32(32)

/* Hardware config of 120 bytes: size, id, I2S format, clock flags, rates, two TDM slots of 16 bits, two channels each way */
#define TPLG_HW_CONFIG(Id, Bclk, Fsync) \
    DSP_U32(120), DSP_U32(Id), DSP_U32(1), 0, 0, 0, 0, 0, 0, DSP_U16(0), \
    DSP_U32(19200000), DSP_U32(Bclk), DSP_U32(Fsync), DSP_U32(2), DSP_U32(16), DSP_U32(3), DSP_U32(3), DSP_U32(2), \
    Z32, DSP_U32(2), Z32

/* Block 0, manifest (type 8): element counts control 1, widget 3, graph 2, pcm 1, link 1, dai 0; private size 3 with three raw bytes */
#define TPLG_MANIFEST_PAYLOAD 115
#define TPLG_MANIFEST_BLOCK \
    TPLG_HEADER(SoftplgTypeManifest, TPLG_MANIFEST_PAYLOAD, 0, 1), \
    DSP_U32(112), DSP_U32(1), DSP_U32(3), DSP_U32(2), DSP_U32(1), DSP_U32(1), DSP_U32(0), Z64, Z16, DSP_U32(3), 0x03, 0x16, 0x01

/* Block 1, widgets (type 5), index 1: PCM0P with two word tuples, PGA1.0 with an appended mixer control, BUF1.0 with a SOF blob */
#define TPLG_WIDGET_PAYLOAD 796
#define TPLG_WIDGET_BLOCK \
    TPLG_HEADER(SoftplgTypeWidget, TPLG_WIDGET_PAYLOAD, 1, 3), \
    TPLG_WIDGET(TPLG_NAME5('P', 'C', 'M', '0', 'P'), TPLG_NAME5('P', 'C', 'M', '0', 'P'), 11, 0, 28), \
    TPLG_WORDS(2, TPLG_WORD(400, 2), TPLG_WORD(401, 0)), \
    TPLG_WIDGET(TPLG_NAME6('P', 'G', 'A', '1', '.', '0'), TPLG_NAME6('P', 'G', 'A', '1', '.', '0'), 4, 1, 0), \
    TPLG_MIXER(TPLG_NAME6('P', 'G', 'A', '1', '.', '0'), 0), \
    TPLG_WIDGET(TPLG_NAME6('B', 'U', 'F', '1', '.', '0'), TPLG_NAME5('B', 'U', 'F', '1', '0'), 16, 0, 12), \
    'S', 'O', 'F', 0, Z8

/* Block 2, graph (type 4), index 2: PGA1.0 takes from PCM0P, BUF1.0 takes from PCM0P through PGA1.0 (sink, control, source) */
#define TPLG_GRAPH_PAYLOAD 264
#define TPLG_GRAPH_BLOCK \
    TPLG_HEADER(SoftplgTypeGraph, TPLG_GRAPH_PAYLOAD, 2, 2), \
    TPLG_NAME6('P', 'G', 'A', '1', '.', '0'), TPLG_NAME5('x', 'x', 'x', 'x', 'x'), TPLG_NAME5('P', 'C', 'M', '0', 'P'), \
    TPLG_NAME6('B', 'U', 'F', '1', '.', '0'), TPLG_NAME6('P', 'G', 'A', '1', '.', '0'), TPLG_NAME5('P', 'C', 'M', '0', 'P')

/* Block 3, PCM (type 7), index 3: PCM0P plays with two channels at 48 kHz; one string tuple "PGA" of token 500 in its private data */
#define TPLG_PCM_PAYLOAD 972
#define TPLG_PCM_BLOCK \
    TPLG_HEADER(SoftplgTypePcm, TPLG_PCM_PAYLOAD, 3, 1), \
    DSP_U32(912), TPLG_NAME5('P', 'C', 'M', '0', 'P'), TPLG_NAME5('P', 'C', 'M', '0', 'P'), \
    DSP_U32(0), DSP_U32(0), DSP_U32(1), DSP_U32(0), DSP_U32(0), \
    Z512, Z64, DSP_U32(0), \
    TPLG_CAPS(TPLG_NAME5('P', 'C', 'M', '0', 'P'), 0xFFFFFFFFFFULL, 0x7FF, 48000, 48000, 2), \
    TPLG_CAPS(TPLG_NAME5('P', 'C', 'M', '0', 'C'), 0, 0, 0, 0, 0), \
    DSP_U32(0), DSP_U32(0), DSP_U32(60), \
    TPLG_STRING(500, TPLG_NAME4('P', 'G', 'A', 0))

/* Block 4, backend link (type 10), index 4: NoCodec0 with one hardware config */
#define TPLG_LINK_PAYLOAD 1656
#define TPLG_LINK_BLOCK \
    TPLG_HEADER(SoftplgTypeBackendLink, TPLG_LINK_PAYLOAD, 4, 1), \
    DSP_U32(1656), DSP_U32(0), TPLG_NAME8('N', 'o', 'C', 'o', 'd', 'e', 'c', '0'), TPLG_NAME8('N', 'o', 'C', 'o', 'd', 'e', 'c', '0'), \
    Z512, Z64, DSP_U32(0), \
    TPLG_HW_CONFIG(0, 1536000, 48000), Z512, Z256, Z64, Z8, \
    DSP_U32(1), DSP_U32(0), DSP_U32(0), DSP_U32(0), DSP_U32(0)

static const unsigned char TplgSynthetic[] =
{
    TPLG_MANIFEST_BLOCK,
    TPLG_WIDGET_BLOCK,
    TPLG_GRAPH_BLOCK,
    TPLG_PCM_BLOCK,
    TPLG_LINK_BLOCK
};

/* Blocks 5 to 7: one control of each kind, alone in a block of its own type, without private data */
static const unsigned char TplgControlMixer[] =
{
    TPLG_HEADER(SoftplgTypeMixer, 360, 0, 1), TPLG_MIXER(TPLG_NAME6('P', 'G', 'A', '1', '.', '0'), 0)
};

static const unsigned char TplgControlBytes[] =
{
    TPLG_HEADER(SoftplgTypeBytes, 240, 0, 1), TPLG_BYTES_CONTROL(TPLG_NAME5('B', 'Y', 'T', 'E', 'S'), 0)
};

static const unsigned char TplgControlEnum[] =
{
    TPLG_HEADER(SoftplgTypeEnum, 1764, 0, 1), TPLG_ENUM_CONTROL(TPLG_NAME4('E', 'N', 'U', 'M'), 0)
};

#define TPLG_MANIFEST_BLOCK_SIZE  (36 + TPLG_MANIFEST_PAYLOAD)
#define TPLG_WIDGET_BLOCK_SIZE    (36 + TPLG_WIDGET_PAYLOAD)
#define TPLG_GRAPH_BLOCK_SIZE     (36 + TPLG_GRAPH_PAYLOAD)
#define TPLG_PCM_BLOCK_SIZE       (36 + TPLG_PCM_PAYLOAD)
#define TPLG_LINK_BLOCK_SIZE      (36 + TPLG_LINK_PAYLOAD)

#define TPLG_WIDGET_BLOCK_OFFSET  (TPLG_MANIFEST_BLOCK_SIZE)
#define TPLG_GRAPH_BLOCK_OFFSET   (TPLG_WIDGET_BLOCK_OFFSET + TPLG_WIDGET_BLOCK_SIZE)
#define TPLG_PCM_BLOCK_OFFSET     (TPLG_GRAPH_BLOCK_OFFSET + TPLG_GRAPH_BLOCK_SIZE)
#define TPLG_LINK_BLOCK_OFFSET    (TPLG_PCM_BLOCK_OFFSET + TPLG_PCM_BLOCK_SIZE)

/* The first widget record, its private data, the second widget and the control appended to it */
#define TPLG_WIDGET0_OFFSET       (TPLG_WIDGET_BLOCK_OFFSET + 36)
#define TPLG_WIDGET1_OFFSET       (TPLG_WIDGET0_OFFSET + 160)
#define TPLG_CONTROL1_OFFSET      (TPLG_WIDGET1_OFFSET + 132)
#define TPLG_WIDGET2_OFFSET       (TPLG_CONTROL1_OFFSET + 360)

#endif
