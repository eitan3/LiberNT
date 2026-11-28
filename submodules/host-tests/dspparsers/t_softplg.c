/*
 * PROJECT:     LiberNT host-native tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     The topology walker reads a synthetic binary and the real sof-bin topologies as the independent walk says, resolves their module UUIDs against the firmware image's module table, and never reads past a short or garbled one; the IPC4 model builds the loopback file and the shipped files as their independent decode says and refuses each malformed input with its status
 * COPYRIGHT:   Copyright 2026 LiberNT contributors
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "shim.h"

#include <softplg.h>
#include <ipc4model.h>
#include <fwload.h>

#include "softplg_cases.h"

/* The vendor tuple that names a widget's module (include/uapi/sound/sof/tokens.h SOF_TKN_COMP_UUID, studied under LICENSING.md rule 7) */
#define TEST_COMP_UUID             405

static int Checks;
static int Failures;

/* The count of block header words the library's walks read, through the read the harness takes over from softplg.h (R2-04) */
static SIZE_T HostHeaderReads;

UINT32
SoftplgHostHeaderWord(
    const UINT8* Data,
    SIZE_T Offset)
{
    HostHeaderReads++;
    return (UINT32)Data[Offset] | ((UINT32)Data[Offset + 1] << 8) | ((UINT32)Data[Offset + 2] << 16) |
        (UINT32)Data[Offset + 3] << 24;
}

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
        unsigned long long _a = (unsigned long long)(a), _b = (unsigned long long)(b); \
        ++Checks; \
        if (_a != _b) { \
            fprintf(stderr, "%s:%d: FAIL: %s == %s: %llx != %llx\n", __FILE__, __LINE__, #a, #b, _a, _b); \
            ++Failures; \
        } \
    } while (0)

#define CHECK_STR(a, b) \
    do { \
        ++Checks; \
        if (strcmp((a), (b)) != 0) { \
            fprintf(stderr, "%s:%d: FAIL: %s == %s: \"%s\" != \"%s\"\n", __FILE__, __LINE__, #a, #b, (a), (b)); \
            ++Failures; \
        } \
    } while (0)

typedef struct _COUNTS
{
    UINT32 Blocks, Widgets, Controls, Edges, Pcms, Streams, Caps, Links, HwConfigs, Dais, Tuples, NotTuples;
} COUNTS;

static
void
ReadTuples(
    const UINT8* Data,
    SIZE_T Size,
    const SOFTPLG_PRIVATE* Private,
    COUNTS* Counts)
{
    SOFTPLG_TUPLE_ITER Iterator;
    SOFTPLG_TUPLE Tuple;
    SOFTPLG_STATUS Status;

    if (Private->Size == 0)
        return;
    Status = SoftplgTupleBegin(Data, Size, Private, &Iterator);
    if (Status != SoftplgOk)
        return;
    for (;;)
    {
        Status = SoftplgTupleNext(&Iterator, &Tuple);
        if (Status == SoftplgNotFound)
            return;
        if (Status == SoftplgUnsupported)
        {
            Counts->NotTuples++;
            return;
        }
        if (Status != SoftplgOk)
            return;
        Counts->Tuples++;
    }
}

/* Reads every element and nested item of every block; returns the softplg status of the walk */
static
int
ReadEverything(
    const UINT8* Data,
    SIZE_T Size,
    COUNTS* Counts)
{
    SOFTPLG_SUMMARY Summary;
    SOFTPLG_BLOCK Block;
    SOFTPLG_MANIFEST Manifest;
    SOFTPLG_WIDGET Widget;
    SOFTPLG_CONTROL Control;
    SOFTPLG_GRAPH_EDGE Edge;
    SOFTPLG_PCM Pcm;
    SOFTPLG_LINK Link;
    SOFTPLG_DAI Dai;
    SOFTPLG_STREAM Stream;
    SOFTPLG_STREAM_CAPS Caps;
    SOFTPLG_HW_CONFIG Config;
    UINT32 B, N, K;

    memset(Counts, 0, sizeof(*Counts));
    if (SoftplgWalk(Data, Size, NULL, NULL, &Summary) != SoftplgOk)
        return 0;
    for (B = 0; B < Summary.BlockCount; B++)
    {
        if (SoftplgReadBlock(Data, Size, B, &Block) != SoftplgOk)
            continue;
        Counts->Blocks++;
        switch (Block.Type)
        {
            case SoftplgTypeManifest:
                if (SoftplgReadManifest(Data, Size, &Block, &Manifest) == SoftplgOk)
                    ReadTuples(Data, Size, &Manifest.Private, Counts);
                break;
            case SoftplgTypeWidget:
                for (N = 0; N < Block.Count; N++)
                {
                    if (SoftplgReadWidget(Data, Size, &Block, N, &Widget) != SoftplgOk)
                        break;
                    Counts->Widgets++;
                    ReadTuples(Data, Size, &Widget.Private, Counts);
                    for (K = 0; K < Widget.ControlCount; K++)
                    {
                        if (SoftplgReadWidgetControl(Data, Size, &Block, N, K, &Control) != SoftplgOk)
                            break;
                        Counts->Controls++;
                        ReadTuples(Data, Size, &Control.Private, Counts);
                    }
                }
                break;
            case SoftplgTypeGraph:
                for (N = 0; N < Block.Count; N++)
                {
                    if (SoftplgReadGraphEdge(Data, Size, &Block, N, &Edge) != SoftplgOk)
                        break;
                    Counts->Edges++;
                }
                break;
            case SoftplgTypeMixer:
            case SoftplgTypeBytes:
            case SoftplgTypeEnum:
                for (N = 0; N < Block.Count; N++)
                {
                    if (SoftplgReadControl(Data, Size, &Block, N, &Control) != SoftplgOk)
                        break;
                    Counts->Controls++;
                    ReadTuples(Data, Size, &Control.Private, Counts);
                }
                break;
            case SoftplgTypePcm:
                for (N = 0; N < Block.Count; N++)
                {
                    if (SoftplgReadPcm(Data, Size, &Block, N, &Pcm) != SoftplgOk)
                        break;
                    Counts->Pcms++;
                    for (K = 0; K < Pcm.StreamCount && K < SOFTPLG_STREAMS_MAX; K++)
                    {
                        if (SoftplgReadPcmStream(Data, Size, &Block, N, K, &Stream) == SoftplgOk)
                            Counts->Streams++;
                    }
                    for (K = 0; K < SOFTPLG_CAPS_MAX; K++)
                    {
                        if (SoftplgReadPcmCaps(Data, Size, &Block, N, K, &Caps) == SoftplgOk)
                            Counts->Caps++;
                    }
                    ReadTuples(Data, Size, &Pcm.Private, Counts);
                }
                break;
            case SoftplgTypeDaiLink:
            case SoftplgTypeBackendLink:
            case SoftplgTypeCodecLink:
                for (N = 0; N < Block.Count; N++)
                {
                    if (SoftplgReadLink(Data, Size, &Block, N, &Link) != SoftplgOk)
                        break;
                    Counts->Links++;
                    for (K = 0; K < Link.StreamCount && K < SOFTPLG_STREAMS_MAX; K++)
                    {
                        if (SoftplgReadLinkStream(Data, Size, &Block, N, K, &Stream) == SoftplgOk)
                            Counts->Streams++;
                    }
                    for (K = 0; K < Link.HwConfigCount && K < SOFTPLG_HW_CONFIGS_MAX; K++)
                    {
                        if (SoftplgReadLinkHwConfig(Data, Size, &Block, N, K, &Config) == SoftplgOk)
                            Counts->HwConfigs++;
                    }
                    ReadTuples(Data, Size, &Link.Private, Counts);
                }
                break;
            case SoftplgTypeDai:
                for (N = 0; N < Block.Count; N++)
                {
                    if (SoftplgReadDai(Data, Size, &Block, N, &Dai) != SoftplgOk)
                        break;
                    Counts->Dais++;
                    for (K = 0; K < SOFTPLG_CAPS_MAX; K++)
                    {
                        if (SoftplgReadDaiCaps(Data, Size, &Block, N, K, &Caps) == SoftplgOk)
                            Counts->Caps++;
                    }
                    ReadTuples(Data, Size, &Dai.Private, Counts);
                }
                break;
            default:
                break;
        }
    }
    return 1;
}

static
void
check_sizes(void)
{
    CHECK_EQ(TPLG_MANIFEST_BLOCK_SIZE, 151);
    CHECK_EQ(TPLG_WIDGET_BLOCK_SIZE, 832);
    CHECK_EQ(TPLG_GRAPH_BLOCK_SIZE, 300);
    CHECK_EQ(TPLG_PCM_BLOCK_SIZE, 1008);
    CHECK_EQ(TPLG_LINK_BLOCK_SIZE, 1692);
    CHECK_EQ(sizeof(TplgSynthetic), 151 + 832 + 300 + 1008 + 1692);
    CHECK_EQ(TPLG_WIDGET_BLOCK_OFFSET, 151);
    CHECK_EQ(TPLG_LINK_BLOCK_OFFSET, 151 + 832 + 300 + 1008);
    CHECK_EQ(TPLG_WIDGET2_OFFSET + 144, TPLG_WIDGET_BLOCK_OFFSET + TPLG_WIDGET_BLOCK_SIZE);
    CHECK_EQ(sizeof(TplgControlMixer), 36 + 360);
    CHECK_EQ(sizeof(TplgControlBytes), 36 + 240);
    CHECK_EQ(sizeof(TplgControlEnum), 36 + 1764);
}

struct _COLLECT
{
    UINT32 Types[8];
    UINT32 Counts[8];
    UINT32 Indexes[8];
    UINT32 Payloads[8];
    UINT32 Seen;
    UINT32 StopAfter;
};

static
BOOLEAN
Collect(
    const SOFTPLG_BLOCK* Block,
    PVOID Context)
{
    struct _COLLECT* Seen = Context;

    if (Seen->Seen >= 8)
        return FALSE;
    Seen->Types[Seen->Seen] = Block->Type;
    Seen->Counts[Seen->Seen] = Block->Count;
    Seen->Indexes[Seen->Seen] = Block->Index;
    Seen->Payloads[Seen->Seen] = Block->PayloadSize;
    Seen->Seen++;
    return Seen->StopAfter == 0 || Seen->Seen < Seen->StopAfter;
}

static
void
check_synthetic(void)
{
    static const UINT32 Types[5] = { 8, 5, 4, 7, 10 };
    static const UINT32 Counts[5] = { 1, 3, 2, 1, 1 };
    static const UINT32 Payloads[5] = { 115, 796, 264, 972, 1656 };
    struct _COLLECT Seen;
    SOFTPLG_SUMMARY Summary;
    SOFTPLG_BLOCK Block;
    SOFTPLG_MANIFEST Manifest;
    SOFTPLG_WIDGET Widget;
    SOFTPLG_CONTROL Control;
    SOFTPLG_GRAPH_EDGE Edge;
    SOFTPLG_PCM Pcm;
    SOFTPLG_STREAM_CAPS Caps;
    SOFTPLG_LINK Link;
    SOFTPLG_HW_CONFIG Config;
    SOFTPLG_TUPLE_ITER Iterator;
    SOFTPLG_TUPLE Tuple;
    SOFTPLG_STREAM Stream;
    UINT32 Index;

    memset(&Summary, 0xAA, sizeof(Summary));
    CHECK_EQ(SoftplgWalk(TplgSynthetic, sizeof(TplgSynthetic), NULL, NULL, &Summary), SoftplgOk);
    CHECK_EQ(Summary.BlockCount, 5);
    CHECK_EQ(Summary.Abi, 5);
    CHECK_EQ(Summary.Version, 0);
    CHECK_EQ(Summary.VendorType, 0);
    CHECK_EQ(Summary.VendorBlocks, 0);
    CHECK_EQ(Summary.Counts[SoftplgTypeManifest], 1);
    CHECK_EQ(Summary.Counts[SoftplgTypeWidget], 1);
    CHECK_EQ(Summary.Counts[SoftplgTypeGraph], 1);
    CHECK_EQ(Summary.Counts[SoftplgTypePcm], 1);
    CHECK_EQ(Summary.Counts[SoftplgTypeBackendLink], 1);
    CHECK_EQ(Summary.Counts[SoftplgTypeDai], 0);

    memset(&Seen, 0, sizeof(Seen));
    CHECK_EQ(SoftplgWalk(TplgSynthetic, sizeof(TplgSynthetic), Collect, &Seen, &Summary), SoftplgOk);
    CHECK_EQ(Seen.Seen, 5);
    for (Index = 0; Index < 5; Index++)
    {
        CHECK_EQ(Seen.Types[Index], Types[Index]);
        CHECK_EQ(Seen.Counts[Index], Counts[Index]);
        CHECK_EQ(Seen.Indexes[Index], Index);
        CHECK_EQ(Seen.Payloads[Index], Payloads[Index]);
        CHECK_EQ(SoftplgReadBlock(TplgSynthetic, sizeof(TplgSynthetic), Index, &Block), SoftplgOk);
        CHECK_EQ(Block.Type, Types[Index]);
        CHECK_EQ(Block.Count, Counts[Index]);
        CHECK_EQ(Block.PayloadSize, Payloads[Index]);
        CHECK_EQ(Block.Index, Index);
        CHECK_EQ(Block.PayloadOffset, Block.Offset + 36);
        CHECK_EQ(Block.NextOffset, Block.PayloadOffset + Block.PayloadSize);
    }
    CHECK_EQ(Block.NextOffset, sizeof(TplgSynthetic));
    CHECK_EQ(SoftplgReadBlock(TplgSynthetic, sizeof(TplgSynthetic), 5, &Block), SoftplgNotFound);

    Seen.Seen = 0;
    Seen.StopAfter = 2;
    CHECK_EQ(SoftplgWalk(TplgSynthetic, sizeof(TplgSynthetic), Collect, &Seen, &Summary), SoftplgOk);
    CHECK_EQ(Seen.Seen, 2);

    CHECK_EQ(SoftplgReadBlock(TplgSynthetic, sizeof(TplgSynthetic), 0, &Block), SoftplgOk);
    CHECK_EQ(SoftplgReadManifest(TplgSynthetic, sizeof(TplgSynthetic), &Block, &Manifest), SoftplgOk);
    CHECK_EQ(Manifest.ControlElements, 1);
    CHECK_EQ(Manifest.WidgetElements, 3);
    CHECK_EQ(Manifest.GraphElements, 2);
    CHECK_EQ(Manifest.PcmElements, 1);
    CHECK_EQ(Manifest.DaiLinkElements, 1);
    CHECK_EQ(Manifest.DaiElements, 0);
    CHECK_EQ(Manifest.Private.Offset, 36 + 112);
    CHECK_EQ(Manifest.Private.Size, 3);
    CHECK_EQ(SoftplgTupleBegin(TplgSynthetic, sizeof(TplgSynthetic), &Manifest.Private, &Iterator), SoftplgOk);
    CHECK_EQ(SoftplgTupleNext(&Iterator, &Tuple), SoftplgUnsupported);

    CHECK_EQ(SoftplgReadBlock(TplgSynthetic, sizeof(TplgSynthetic), 1, &Block), SoftplgOk);
    CHECK_EQ(SoftplgReadWidget(TplgSynthetic, sizeof(TplgSynthetic), &Block, 0, &Widget), SoftplgOk);
    CHECK_STR(Widget.Name, "PCM0P");
    CHECK_STR(Widget.SName, "PCM0P");
    CHECK_EQ(Widget.Id, 11);
    CHECK_EQ(Widget.ControlCount, 0);
    CHECK_EQ(Widget.Private.Size, 28);
    CHECK_EQ(Widget.ControlsOffset, TPLG_WIDGET1_OFFSET);
    CHECK_EQ(SoftplgTupleBegin(TplgSynthetic, sizeof(TplgSynthetic), &Widget.Private, &Iterator), SoftplgOk);
    CHECK_EQ(SoftplgTupleNext(&Iterator, &Tuple), SoftplgOk);
    CHECK_EQ(Tuple.Type, SOFTPLG_TUPLE_WORD);
    CHECK_EQ(Tuple.Token, 400);
    CHECK_EQ(Tuple.Value, 2);
    CHECK_EQ(SoftplgTupleNext(&Iterator, &Tuple), SoftplgOk);
    CHECK_EQ(Tuple.Token, 401);
    CHECK_EQ(Tuple.Value, 0);
    CHECK_EQ(SoftplgTupleNext(&Iterator, &Tuple), SoftplgNotFound);

    CHECK_EQ(SoftplgReadWidget(TplgSynthetic, sizeof(TplgSynthetic), &Block, 1, &Widget), SoftplgOk);
    CHECK_STR(Widget.Name, "PGA1.0");
    CHECK_EQ(Widget.Id, 4);
    CHECK_EQ(Widget.ControlCount, 1);
    CHECK_EQ(Widget.Private.Size, 0);
    CHECK_EQ(SoftplgReadWidgetControl(TplgSynthetic, sizeof(TplgSynthetic), &Block, 1, 0, &Control), SoftplgOk);
    CHECK_EQ(Control.Kind, SoftplgControlMixer);
    CHECK_STR(Control.Name, "PGA1.0");
    CHECK_EQ(Control.Access, 3);
    CHECK_EQ(Control.Minimum, 0);
    CHECK_EQ(Control.Maximum, 100);
    CHECK_EQ(Control.PlatformMaximum, 0);
    CHECK_EQ(Control.Channels, 2);
    CHECK_EQ(Control.Private.Size, 0);
    CHECK_EQ(SoftplgTupleBegin(TplgSynthetic, sizeof(TplgSynthetic), &Control.Private, &Iterator), SoftplgOk);
    CHECK_EQ(SoftplgTupleNext(&Iterator, &Tuple), SoftplgNotFound);
    CHECK_EQ(SoftplgReadWidgetControl(TplgSynthetic, sizeof(TplgSynthetic), &Block, 1, 1, &Control), SoftplgNotFound);
    CHECK_EQ(SoftplgReadWidgetControl(TplgSynthetic, sizeof(TplgSynthetic), &Block, 0, 0, &Control), SoftplgNotFound);

    CHECK_EQ(SoftplgReadWidget(TplgSynthetic, sizeof(TplgSynthetic), &Block, 2, &Widget), SoftplgOk);
    CHECK_STR(Widget.Name, "BUF1.0");
    CHECK_STR(Widget.SName, "BUF10");
    CHECK_EQ(Widget.Id, 16);
    CHECK_EQ(Widget.Private.Size, 12);
    CHECK_EQ(SoftplgTupleBegin(TplgSynthetic, sizeof(TplgSynthetic), &Widget.Private, &Iterator), SoftplgOk);
    CHECK_EQ(SoftplgTupleNext(&Iterator, &Tuple), SoftplgUnsupported);
    CHECK_EQ(SoftplgReadWidget(TplgSynthetic, sizeof(TplgSynthetic), &Block, 3, &Widget), SoftplgNotFound);
    /* a control block read over a widget block: the widget id is no control kind */
    CHECK_EQ(SoftplgReadControl(TplgSynthetic, sizeof(TplgSynthetic), &Block, 0, &Control), SoftplgUnsupported);

    CHECK_EQ(SoftplgReadBlock(TplgSynthetic, sizeof(TplgSynthetic), 2, &Block), SoftplgOk);
    CHECK_EQ(SoftplgReadGraphEdge(TplgSynthetic, sizeof(TplgSynthetic), &Block, 0, &Edge), SoftplgOk);
    CHECK_STR(Edge.Sink, "PGA1.0");
    CHECK_STR(Edge.Control, "xxxxx");
    CHECK_STR(Edge.Source, "PCM0P");
    CHECK_EQ(SoftplgReadGraphEdge(TplgSynthetic, sizeof(TplgSynthetic), &Block, 1, &Edge), SoftplgOk);
    CHECK_STR(Edge.Sink, "BUF1.0");
    CHECK_STR(Edge.Control, "PGA1.0");
    CHECK_STR(Edge.Source, "PCM0P");
    CHECK_EQ(SoftplgReadGraphEdge(TplgSynthetic, sizeof(TplgSynthetic), &Block, 2, &Edge), SoftplgNotFound);

    CHECK_EQ(SoftplgReadBlock(TplgSynthetic, sizeof(TplgSynthetic), 3, &Block), SoftplgOk);
    CHECK_EQ(SoftplgReadPcm(TplgSynthetic, sizeof(TplgSynthetic), &Block, 0, &Pcm), SoftplgOk);
    CHECK_STR(Pcm.PcmName, "PCM0P");
    CHECK_STR(Pcm.DaiName, "PCM0P");
    CHECK_EQ(Pcm.PcmId, 0);
    CHECK_EQ(Pcm.DaiId, 0);
    CHECK_EQ(Pcm.Playback, 1);
    CHECK_EQ(Pcm.Capture, 0);
    CHECK_EQ(Pcm.Compress, 0);
    CHECK_EQ(Pcm.StreamCount, 0);
    CHECK_EQ(Pcm.Private.Size, 60);
    CHECK_EQ(SoftplgReadPcmStream(TplgSynthetic, sizeof(TplgSynthetic), &Block, 0, 0, &Stream), SoftplgNotFound);
    CHECK_EQ(SoftplgReadPcmCaps(TplgSynthetic, sizeof(TplgSynthetic), &Block, 0, 0, &Caps), SoftplgOk);
    CHECK_STR(Caps.Name, "PCM0P");
    CHECK_EQ(Caps.Formats, 0xFFFFFFFFFFULL);
    CHECK_EQ(Caps.Rates, 0x7FF);
    CHECK_EQ(Caps.RateMin, 48000);
    CHECK_EQ(Caps.RateMax, 48000);
    CHECK_EQ(Caps.ChannelsMin, 2);
    CHECK_EQ(Caps.ChannelsMax, 2);
    CHECK_EQ(Caps.SigBits, 32);
    CHECK_EQ(SoftplgReadPcmCaps(TplgSynthetic, sizeof(TplgSynthetic), &Block, 0, 1, &Caps), SoftplgOk);
    CHECK_STR(Caps.Name, "PCM0C");
    CHECK_EQ(Caps.Formats, 0);
    CHECK_EQ(SoftplgTupleBegin(TplgSynthetic, sizeof(TplgSynthetic), &Pcm.Private, &Iterator), SoftplgOk);
    CHECK_EQ(SoftplgTupleNext(&Iterator, &Tuple), SoftplgOk);
    CHECK_EQ(Tuple.Type, SOFTPLG_TUPLE_STRING);
    CHECK_EQ(Tuple.Token, 500);
    CHECK_STR(Tuple.String, "PGA");
    CHECK_EQ(SoftplgTupleNext(&Iterator, &Tuple), SoftplgNotFound);
    CHECK_EQ(SoftplgReadPcmCaps(TplgSynthetic, sizeof(TplgSynthetic), &Block, 0, 2, &Caps), SoftplgNotFound);
    CHECK_EQ(SoftplgReadPcm(TplgSynthetic, sizeof(TplgSynthetic), &Block, 1, &Pcm), SoftplgNotFound);

    CHECK_EQ(SoftplgReadBlock(TplgSynthetic, sizeof(TplgSynthetic), 4, &Block), SoftplgOk);
    CHECK_EQ(SoftplgReadLink(TplgSynthetic, sizeof(TplgSynthetic), &Block, 0, &Link), SoftplgOk);
    CHECK_STR(Link.Name, "NoCodec0");
    CHECK_STR(Link.StreamName, "NoCodec0");
    CHECK_EQ(Link.Id, 0);
    CHECK_EQ(Link.StreamCount, 0);
    CHECK_EQ(Link.HwConfigCount, 1);
    CHECK_EQ(Link.DefaultHwConfigId, 0);
    CHECK_EQ(SoftplgReadLinkStream(TplgSynthetic, sizeof(TplgSynthetic), &Block, 0, 0, &Stream), SoftplgNotFound);
    CHECK_EQ(SoftplgReadLinkHwConfig(TplgSynthetic, sizeof(TplgSynthetic), &Block, 0, 0, &Config), SoftplgOk);
    CHECK_EQ(Config.Id, 0);
    CHECK_EQ(Config.Format, 1);
    CHECK_EQ(Config.MclkRate, 19200000);
    CHECK_EQ(Config.BclkRate, 1536000);
    CHECK_EQ(Config.FsyncRate, 48000);
    CHECK_EQ(Config.TdmSlots, 2);
    CHECK_EQ(Config.TdmSlotWidth, 16);
    CHECK_EQ(Config.TxSlots, 3);
    CHECK_EQ(Config.RxSlots, 3);
    CHECK_EQ(Config.TxChannels, 2);
    CHECK_EQ(Config.RxChannels, 2);
    CHECK_EQ(SoftplgReadLinkHwConfig(TplgSynthetic, sizeof(TplgSynthetic), &Block, 0, 1, &Config), SoftplgNotFound);
    CHECK_EQ(SoftplgReadLink(TplgSynthetic, sizeof(TplgSynthetic), &Block, 1, &Link), SoftplgNotFound);
    CHECK_EQ(SoftplgTupleBegin(TplgSynthetic, sizeof(TplgSynthetic), &Link.Private, &Iterator), SoftplgOk);
    CHECK_EQ(SoftplgTupleNext(&Iterator, &Tuple), SoftplgNotFound);
}

static
void
check_malformed(void)
{
    static unsigned char Copy[4096];
    static const SIZE_T WidgetSizeOffset = TPLG_WIDGET0_OFFSET;
    static const SIZE_T ControlBodySizeOffset = TPLG_CONTROL1_OFFSET + 204;
    SOFTPLG_SUMMARY Summary;
    SOFTPLG_BLOCK Block;
    SOFTPLG_WIDGET Widget;
    SOFTPLG_CONTROL Control;
    SOFTPLG_GRAPH_EDGE Edge;
    SOFTPLG_TUPLE_ITER Iterator;
    SOFTPLG_TUPLE Tuple;
    SOFTPLG_PCM Pcm;
    SIZE_T Size = sizeof(TplgSynthetic);

    memcpy(Copy, TplgSynthetic, Size);
    Copy[0] = 'X';
    CHECK_EQ(SoftplgWalk(Copy, Size, NULL, NULL, &Summary), SoftplgBadSignature);
    CHECK_EQ(SoftplgReadBlock(Copy, Size, 0, &Block), SoftplgBadSignature);
    memcpy(Copy, TplgSynthetic, Size);
    Copy[16] = 32;
    CHECK_EQ(SoftplgWalk(Copy, Size, NULL, NULL, &Summary), SoftplgBadHeader);
    memcpy(Copy, TplgSynthetic, Size);
    Copy[4] = 4;
    CHECK_EQ(SoftplgWalk(Copy, Size, NULL, NULL, &Summary), SoftplgUnsupported);
    memcpy(Copy, TplgSynthetic, Size);
    Copy[TPLG_WIDGET_BLOCK_OFFSET + 24] = 0xFF;
    Copy[TPLG_WIDGET_BLOCK_OFFSET + 27] = 0x7F;
    CHECK_EQ(SoftplgWalk(Copy, Size, NULL, NULL, &Summary), SoftplgTruncated);
    CHECK_EQ(Summary.BlockCount, 1);
    memcpy(Copy, TplgSynthetic, Size);
    Copy[TPLG_WIDGET_BLOCK_OFFSET + 24] = 0xFB;
    CHECK_EQ(SoftplgWalk(Copy, Size, NULL, NULL, &Summary), SoftplgBadHeader);

    memcpy(Copy, TplgSynthetic, Size);
    Copy[WidgetSizeOffset] = 136;
    CHECK_EQ(SoftplgWalk(Copy, Size, NULL, NULL, &Summary), SoftplgOk);
    CHECK_EQ(SoftplgReadBlock(Copy, Size, 1, &Block), SoftplgOk);
    CHECK_EQ(SoftplgReadWidget(Copy, Size, &Block, 0, &Widget), SoftplgUnsupported);

    memcpy(Copy, TplgSynthetic, Size);
    Copy[ControlBodySizeOffset] = 0x60;
    CHECK_EQ(SoftplgReadBlock(Copy, Size, 1, &Block), SoftplgOk);
    CHECK_EQ(SoftplgReadWidget(Copy, Size, &Block, 1, &Widget), SoftplgOk);
    CHECK_EQ(SoftplgReadWidgetControl(Copy, Size, &Block, 1, 0, &Control), SoftplgUnsupported);
    CHECK_EQ(SoftplgReadWidget(Copy, Size, &Block, 2, &Widget), SoftplgUnsupported);

    memcpy(Copy, TplgSynthetic, Size);
    Copy[TPLG_GRAPH_BLOCK_OFFSET + 32] = 3;
    CHECK_EQ(SoftplgReadBlock(Copy, Size, 2, &Block), SoftplgOk);
    CHECK_EQ(SoftplgReadGraphEdge(Copy, Size, &Block, 1, &Edge), SoftplgOk);
    CHECK_EQ(SoftplgReadGraphEdge(Copy, Size, &Block, 2, &Edge), SoftplgTruncated);

    memcpy(Copy, TplgSynthetic, Size);
    Copy[TPLG_WIDGET0_OFFSET + 132 + 8] = 9;
    CHECK_EQ(SoftplgReadBlock(Copy, Size, 1, &Block), SoftplgOk);
    CHECK_EQ(SoftplgReadWidget(Copy, Size, &Block, 0, &Widget), SoftplgOk);
    CHECK_EQ(SoftplgTupleBegin(Copy, Size, &Widget.Private, &Iterator), SoftplgOk);
    CHECK_EQ(SoftplgTupleNext(&Iterator, &Tuple), SoftplgTruncated);
    memcpy(Copy, TplgSynthetic, Size);
    Copy[TPLG_WIDGET0_OFFSET + 132 + 4] = 7;
    CHECK_EQ(SoftplgReadBlock(Copy, Size, 1, &Block), SoftplgOk);
    CHECK_EQ(SoftplgReadWidget(Copy, Size, &Block, 0, &Widget), SoftplgOk);
    CHECK_EQ(SoftplgTupleBegin(Copy, Size, &Widget.Private, &Iterator), SoftplgOk);
    CHECK_EQ(SoftplgTupleNext(&Iterator, &Tuple), SoftplgUnsupported);

    memcpy(Copy, TplgSynthetic, Size);
    Copy[TPLG_PCM_BLOCK_OFFSET + 36 + 908] = 64;
    CHECK_EQ(SoftplgReadBlock(Copy, Size, 3, &Block), SoftplgOk);
    CHECK_EQ(SoftplgReadPcm(Copy, Size, &Block, 0, &Pcm), SoftplgTruncated);
    /* the string array names two elements where its 60 bytes hold one, and the file ends right after it */
    memcpy(Copy, TplgSynthetic, Size);
    Copy[TPLG_PCM_BLOCK_OFFSET + 36 + 912 + 8] = 2;
    CHECK_EQ(SoftplgWalk(Copy, TPLG_LINK_BLOCK_OFFSET, NULL, NULL, &Summary), SoftplgOk);
    CHECK_EQ(SoftplgReadBlock(Copy, TPLG_LINK_BLOCK_OFFSET, 3, &Block), SoftplgOk);
    CHECK_EQ(SoftplgReadPcm(Copy, TPLG_LINK_BLOCK_OFFSET, &Block, 0, &Pcm), SoftplgOk);
    CHECK_EQ(SoftplgTupleBegin(Copy, TPLG_LINK_BLOCK_OFFSET, &Pcm.Private, &Iterator), SoftplgOk);
    CHECK_EQ(SoftplgTupleNext(&Iterator, &Tuple), SoftplgTruncated);
    CHECK_EQ(SoftplgWalk(NULL, 0, NULL, NULL, &Summary), SoftplgNoData);
    CHECK_EQ(SoftplgReadBlock(NULL, 0, 0, &Block), SoftplgNoData);
}

static
UINT32
RandomStep(
    UINT32* State)
{
    UINT32 Value = *State;

    Value ^= Value << 13;
    Value ^= Value >> 17;
    Value ^= Value << 5;
    *State = Value;
    return Value;
}

/* Every reader on a buffer; only the absence of crashes and the status range matter here */
static
void
exercise(
    const UINT8* Data,
    SIZE_T Size)
{
    SOFTPLG_SUMMARY Summary;
    SOFTPLG_BLOCK Block;
    SOFTPLG_WIDGET Widget;
    SOFTPLG_CONTROL Control;
    SOFTPLG_MANIFEST Manifest;
    SOFTPLG_PCM Pcm;
    SOFTPLG_LINK Link;
    SOFTPLG_DAI Dai;
    SOFTPLG_GRAPH_EDGE Edge;
    SOFTPLG_STREAM Stream;
    SOFTPLG_STREAM_CAPS Caps;
    SOFTPLG_HW_CONFIG Config;
    SOFTPLG_TUPLE_ITER Iterator;
    SOFTPLG_TUPLE Tuple;
    UINT32 B, N, K, T;
    int Status;

    Status = SoftplgWalk(Data, Size, NULL, NULL, &Summary);
    CHECK(Status >= SoftplgOk && Status <= SoftplgUnsupported);
    for (B = 0; B < 6; B++)
    {
        if (SoftplgReadBlock(Data, Size, B, &Block) != SoftplgOk)
            continue;
        for (N = 0; N < 3; N++)
        {
            if (SoftplgReadWidget(Data, Size, &Block, N, &Widget) == SoftplgOk)
            {
                for (K = 0; K < 2 && K < Widget.ControlCount + 1; K++)
                    SoftplgReadWidgetControl(Data, Size, &Block, N, K, &Control);
                if (SoftplgTupleBegin(Data, Size, &Widget.Private, &Iterator) == SoftplgOk)
                    for (T = 0; T < 64 && SoftplgTupleNext(&Iterator, &Tuple) == SoftplgOk; T++)
                        CHECK(Tuple.Type <= SOFTPLG_TUPLE_SHORT);
            }
            SoftplgReadControl(Data, Size, &Block, N, &Control);
            SoftplgReadGraphEdge(Data, Size, &Block, N, &Edge);
            if (SoftplgReadPcm(Data, Size, &Block, N, &Pcm) == SoftplgOk)
            {
                SoftplgReadPcmStream(Data, Size, &Block, N, 0, &Stream);
                SoftplgReadPcmCaps(Data, Size, &Block, N, 0, &Caps);
                SoftplgReadPcmCaps(Data, Size, &Block, N, 1, &Caps);
            }
            if (SoftplgReadLink(Data, Size, &Block, N, &Link) == SoftplgOk)
            {
                SoftplgReadLinkStream(Data, Size, &Block, N, 0, &Stream);
                for (K = 0; K < 2; K++)
                    SoftplgReadLinkHwConfig(Data, Size, &Block, N, K, &Config);
            }
            if (SoftplgReadDai(Data, Size, &Block, N, &Dai) == SoftplgOk)
                SoftplgReadDaiCaps(Data, Size, &Block, N, 0, &Caps);
        }
        SoftplgReadManifest(Data, Size, &Block, &Manifest);
    }
}

static
void
check_short_and_garbage_inputs(void)
{
    static const UINT32 Seeds[] = { 0x12345678, 0x9E3779B9, 0xDEADBEEF, 0xCAFEBABE };
    static unsigned char Buffer[4096];
    SIZE_T Size = sizeof(TplgSynthetic), Length;
    unsigned int Seed, Iteration, Index;
    UINT32 State;

    for (Length = 0; Length <= Size; Length++)
        exercise(TplgSynthetic, Length);
    for (Index = 0; Index < Size; Index++)
    {
        memcpy(Buffer, TplgSynthetic, Size);
        Buffer[Index] ^= 0x5A;
        exercise(Buffer, Size);
    }

    for (Seed = 0; Seed < sizeof(Seeds) / sizeof(Seeds[0]); Seed++)
    {
        State = Seeds[Seed];
        for (Iteration = 0; Iteration < 3000; Iteration++)
        {
            Length = RandomStep(&State) % (sizeof(Buffer) + 1);
            for (Index = 0; Index < Length; Index++)
                Buffer[Index] = (unsigned char)RandomStep(&State);
            if (Length >= 36 && (Iteration & 1))
            {
                Buffer[0] = 0x43;
                Buffer[1] = 0x6F;
                Buffer[2] = 0x53;
                Buffer[3] = 0x41;
                Buffer[4] = 5;
                Buffer[16] = 36;
            }
            exercise(Buffer, Length);

            memcpy(Buffer, TplgSynthetic, Size);
            Length = Size;
            for (Index = 0; Index < 4; Index++)
                Buffer[RandomStep(&State) % Size] = (unsigned char)RandomStep(&State);
            exercise(Buffer, Length);
            exercise(Buffer, RandomStep(&State) % (Length + 1));
        }
    }
}

/* Real topologies from sof-bin 2026.09.2 (package sof-firmware, LICENCE.Intel in the fixtures directory), the loopback file this project builds from Sound Open Firmware's topology2 classes (drivers/wdm/audio/intcdsp/firmware/loopback, the same licence for uniformity); the facts come from an independent walk of the same files */
typedef struct _FIXTURE_FACTS
{
    const char* Name;
    unsigned int Size;
    UINT32 Blocks, Widgets, Controls, Edges, Pcms, Links, HwConfigs, Tuples, NotTuples;
    UINT32 Sequence[60];
    const char* Widget0;
    const char* Pcm0;
    const char* Link0;
    const char* Edge0Sink;
    const char* Edge0Source;
} FIXTURE_FACTS;

static const FIXTURE_FACTS Fixtures[] =
{
    { "sof-arl-rt711-l0-2ch.tplg", 65647, 20, 40, 12, 21, 7, 7, 7, 2534, 5,
        { 8, 1, 5, 4, 5, 5, 5, 2, 5, 4, 5, 4, 5, 2, 5, 2, 5, 2, 5, 2, 5, 2, 5, 2, 5, 4, 5, 5, 7, 7, 10, 7, 4, 17, 4, 1, 4, 1, 4, 2, 0, 0 },
        "host-copier.0.playback", "DMIC Raw", "dmic01", "gain.101.1", "dai-copier.DMIC.dmic01.capture" },
    { "sof-arl-dmic-4ch-id2.tplg", 23787, 7, 9, 8, 6, 1, 2, 2, 640, 4,
        { 8, 1, 5, 4, 5, 5, 7, 1, 10, 2, 4, 4, 4, 2, 0, 0 },
        "host-copier.10.capture", "DMIC Raw", "dmic01", "gain.101.1", "dai-copier.DMIC.dmic01.capture" },
    { "sof-adl-nocodec.tplg", 45771, 28, 72, 18, 52, 6, 5, 5, 440, 3,
        { 8, 1, 5, 6, 5, 6, 5, 6, 5, 6, 5, 6, 5, 6, 5, 5, 5, 5, 5, 5, 5, 5, 5, 8, 5, 8, 7, 6, 10, 5, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 3, 4, 3, 4, 3, 4, 3, 4, 6, 4, 6, 0, 0 },
        "MIXER1.0", "DMIC", "NoCodec-3", "MIXER1.0", "BUF7.1" },
    { "sof-sdca-4amp-id2.tplg", 154, 1, 0, 0, 0, 0, 0, 0, 0, 1,
        { 8, 1, 0, 0 },
        "", "", "", "", "" },
    { "sof-hda-generic-ace1-2ch.tplg", 67203, 28, 60, 16, 23, 6, 6, 6, 2480, 8,
        { 8, 1, 5, 1, 5, 6, 5, 9, 5, 4, 5, 6, 5, 2, 5, 2, 5, 2, 5, 2, 5, 1, 5, 4, 5, 5, 5, 4, 5, 2, 5, 2, 5, 2, 5, 2, 5, 2, 5, 2, 7, 6, 10, 6, 4, 14, 4, 1, 4, 4, 4, 1, 4, 2, 4, 1, 0, 0 },
        "iDisp3 Tx", "HDA Analog", "Analog", "dai-copier.HDA.Analog.playback", "drc.2.1" },
    { "sof-ipc4-loopback.tplg", 4286, 4, 4, 2, 2, 1, 0, 0, 149, 1,
        { 8, 1, 5, 4, 7, 1, 4, 2, 0, 0 },
        "host-copier.0.playback", "Loopback", "", "gain.1.1", "host-copier.0.playback" }
};

static unsigned char Fixture[131072];

static
void
check_fixtures(void)
{
    static const UINT32 Seeds[] = { 0x12345678, 0x9E3779B9 };
    CHAR Path[512];
    COUNTS Counts;
    SOFTPLG_SUMMARY Summary;
    SOFTPLG_BLOCK Block;
    SOFTPLG_WIDGET Widget;
    SOFTPLG_PCM Pcm;
    SOFTPLG_LINK Link;
    SOFTPLG_GRAPH_EDGE Edge;
    FILE* File;
    SIZE_T Size, Length, Offset;
    unsigned int F, Index;
    UINT32 State;
    unsigned int Iteration;

    for (F = 0; F < sizeof(Fixtures) / sizeof(Fixtures[0]); F++)
    {
        const FIXTURE_FACTS* Facts = &Fixtures[F];

        snprintf(Path, sizeof(Path), "%s/%s", SOFTPLG_FIXTURE_DIR, Facts->Name);
        File = fopen(Path, "rb");
        CHECK(File != NULL);
        if (File == NULL)
            continue;
        Size = fread(Fixture, 1, sizeof(Fixture), File);
        fclose(File);
        CHECK_EQ(Size, Facts->Size);
        if (Size != Facts->Size)
            continue;

        memset(&Summary, 0, sizeof(Summary));
        CHECK_EQ(SoftplgWalk(Fixture, Size, NULL, NULL, &Summary), SoftplgOk);
        CHECK_EQ(Summary.BlockCount, Facts->Blocks);
        CHECK_EQ(Summary.Abi, 5);
        for (Index = 0; Index * 2 + 1 < 60 && Facts->Sequence[Index * 2] != 0; Index++)
        {
            CHECK_EQ(SoftplgReadBlock(Fixture, Size, Index, &Block), SoftplgOk);
            CHECK_EQ(Block.Type, Facts->Sequence[Index * 2]);
            CHECK_EQ(Block.Count, Facts->Sequence[Index * 2 + 1]);
        }
        CHECK_EQ(SoftplgReadBlock(Fixture, Size, Facts->Blocks, &Block), SoftplgNotFound);

        CHECK(ReadEverything(Fixture, Size, &Counts));
        CHECK_EQ(Counts.Blocks, Facts->Blocks);
        CHECK_EQ(Counts.Widgets, Facts->Widgets);
        CHECK_EQ(Counts.Controls, Facts->Controls);
        CHECK_EQ(Counts.Edges, Facts->Edges);
        CHECK_EQ(Counts.Pcms, Facts->Pcms);
        CHECK_EQ(Counts.Links, Facts->Links);
        CHECK_EQ(Counts.HwConfigs, Facts->HwConfigs);
        CHECK_EQ(Counts.Tuples, Facts->Tuples);
        CHECK_EQ(Counts.NotTuples, Facts->NotTuples);
        if (Failures != 0)
            fprintf(stderr, "  in fixture %s\n", Facts->Name);

        if (Facts->Widget0[0] != 0)
        {
            UINT32 WidgetBlock = 0, PcmBlock = 0, LinkBlock = 0, GraphBlock = 0;

            for (Index = 0; Facts->Sequence[Index * 2] != 0; Index++)
            {
                if (Facts->Sequence[Index * 2] == SoftplgTypeWidget && WidgetBlock == 0)
                    WidgetBlock = Index;
                if (Facts->Sequence[Index * 2] == SoftplgTypePcm && PcmBlock == 0)
                    PcmBlock = Index;
                if (Facts->Sequence[Index * 2] == SoftplgTypeBackendLink && LinkBlock == 0)
                    LinkBlock = Index;
                if (Facts->Sequence[Index * 2] == SoftplgTypeGraph && GraphBlock == 0)
                    GraphBlock = Index;
            }
            CHECK(WidgetBlock != 0 && PcmBlock != 0 && GraphBlock != 0 && (Facts->Links != 0) == (LinkBlock != 0));
            CHECK_EQ(SoftplgReadBlock(Fixture, Size, WidgetBlock, &Block), SoftplgOk);
            CHECK_EQ(Block.Type, SoftplgTypeWidget);
            CHECK_EQ(SoftplgReadWidget(Fixture, Size, &Block, 0, &Widget), SoftplgOk);
            CHECK_STR(Widget.Name, Facts->Widget0);
            CHECK_EQ(SoftplgReadBlock(Fixture, Size, PcmBlock, &Block), SoftplgOk);
            CHECK_EQ(Block.Type, SoftplgTypePcm);
            CHECK_EQ(SoftplgReadPcm(Fixture, Size, &Block, 0, &Pcm), SoftplgOk);
            CHECK_STR(Pcm.PcmName, Facts->Pcm0);
            if (Facts->Links != 0)
            {
                CHECK_EQ(SoftplgReadBlock(Fixture, Size, LinkBlock, &Block), SoftplgOk);
                CHECK_EQ(Block.Type, SoftplgTypeBackendLink);
                CHECK_EQ(SoftplgReadLink(Fixture, Size, &Block, 0, &Link), SoftplgOk);
                CHECK_STR(Link.Name, Facts->Link0);
            }
            CHECK_EQ(SoftplgReadBlock(Fixture, Size, GraphBlock, &Block), SoftplgOk);
            CHECK_EQ(Block.Type, SoftplgTypeGraph);
            CHECK_EQ(SoftplgReadGraphEdge(Fixture, Size, &Block, 0, &Edge), SoftplgOk);
            CHECK_STR(Edge.Sink, Facts->Edge0Sink);
            CHECK_STR(Edge.Source, Facts->Edge0Source);
        }

        for (Length = 0; Length < 400; Length++)
            exercise(Fixture, Length);
        for (Length = 400; Length <= Size; Length += 97)
            exercise(Fixture, Length);
        exercise(Fixture, Size - 1);
        exercise(Fixture, Size);
        for (Offset = 0; Offset < Size; Offset += 251)
        {
            Fixture[Offset] ^= 0x5A;
            exercise(Fixture, Size);
            Fixture[Offset] ^= 0x5A;
        }

        State = Seeds[F % 2];
        for (Iteration = 0; Iteration < 400; Iteration++)
        {
            Size = Facts->Size;
            for (Index = 0; Index < 3; Index++)
                Fixture[RandomStep(&State) % Size] = (unsigned char)RandomStep(&State);
            exercise(Fixture, Size);
            exercise(Fixture, RandomStep(&State) % (Size + 1));
            if (Iteration % 50 == 49)
            {
                File = fopen(Path, "rb");
                if (File != NULL)
                {
                    Size = fread(Fixture, 1, sizeof(Fixture), File);
                    fclose(File);
                }
            }
        }
    }
}

static const struct
{
    const UINT8* Block;
    SIZE_T Bytes;
}
ControlKinds[] =
{
    { TplgControlMixer, 360 },
    { TplgControlBytes, 240 },
    { TplgControlEnum, 1764 }
};

/* A private size whose 32-bit sum with the structure size wraps to 0, to 1, or the largest there is */
static
UINT32
WrapSize(
    SIZE_T Bytes,
    SIZE_T Which)
{
    return Which == 2 ? 0xFFFFFFFFu : (UINT32)(0u - (UINT32)Bytes) + (UINT32)Which;
}

static
void
Put32(
    UINT8* At,
    UINT32 Value)
{
    At[0] = (UINT8)Value;
    At[1] = (UINT8)(Value >> 8);
    At[2] = (UINT8)(Value >> 16);
    At[3] = (UINT8)(Value >> 24);
}

static
void
Put16(
    UINT8* At,
    UINT32 Value)
{
    At[0] = (UINT8)Value;
    At[1] = (UINT8)(Value >> 8);
}

/* A heap block of exactly Length bytes holding the first Length bytes of Source: the sanitizer sees every read past it */
static
UINT8*
ExactCopy(
    const UINT8* Source,
    SIZE_T Length)
{
    UINT8* Copy = (UINT8*)malloc(Length);

    CHECK(Copy != NULL);
    if (Copy == NULL)
        exit(2);
    memcpy(Copy, Source, Length);
    return Copy;
}

/* R7 of the branch review: the private size of a control is read only once its whole structure is known to fit, and no private size wraps the sum */
static
void
check_control_bounds(void)
{
    /* block, private size field and structure size of the manifest, the first widget, the PCM and the link */
    static const UINT32 ElementBlock[] = { 0, 1, 3, 4 };
    static const SIZE_T ElementPriv[] = { 36 + 108, TPLG_WIDGET0_OFFSET + 128, TPLG_PCM_BLOCK_OFFSET + 36 + 908, TPLG_LINK_BLOCK_OFFSET + 36 + 1652 };
    static const SIZE_T ElementBytes[] = { 112, 132, 912, 1656 };
    SOFTPLG_SUMMARY Summary;
    SOFTPLG_BLOCK Block;
    SOFTPLG_CONTROL Control;
    SOFTPLG_WIDGET Widget;
    SOFTPLG_MANIFEST Manifest;
    SOFTPLG_PCM Pcm;
    SOFTPLG_LINK Link;
    UINT8* Copy;
    SIZE_T Size, K, E, V, Cut;
    UINT32 Bytes;

    for (K = 0; K < sizeof(ControlKinds) / sizeof(ControlKinds[0]); K++)
    {
        Bytes = (UINT32)ControlKinds[K].Bytes;

        /* the whole structure with no private data is one control that ends where the file ends */
        Size = 36 + Bytes;
        Copy = ExactCopy(ControlKinds[K].Block, Size);
        CHECK_EQ(SoftplgWalk(Copy, Size, NULL, NULL, &Summary), SoftplgOk);
        CHECK_EQ(SoftplgReadBlock(Copy, Size, 0, &Block), SoftplgOk);
        CHECK_EQ(SoftplgReadControl(Copy, Size, &Block, 0, &Control), SoftplgOk);
        CHECK_EQ(Control.Kind, K + 1);
        CHECK_EQ(Control.Private.Offset, Size);
        CHECK_EQ(Control.Private.Size, 0);
        for (V = 0; V < 3; V++)
        {
            Put32(Copy + Size - 4, WrapSize(Bytes, V));
            CHECK_EQ(SoftplgReadControl(Copy, Size, &Block, 0, &Control), SoftplgTruncated);
        }
        free(Copy);
    }

    Size = sizeof(TplgSynthetic);
    Copy = ExactCopy(TplgSynthetic, Size);
    CHECK_EQ(SoftplgWalk(Copy, Size, NULL, NULL, &Summary), SoftplgOk);
    CHECK_EQ(SoftplgReadBlock(Copy, Size, 1, &Block), SoftplgOk);
    CHECK_EQ(SoftplgReadWidgetControl(Copy, Size, &Block, 1, 0, &Control), SoftplgOk);
    CHECK_EQ(SoftplgReadWidget(Copy, Size, &Block, 2, &Widget), SoftplgOk);
    free(Copy);

    /* the widget block ends inside the control of PGA1.0, and the bytes after its end, which belong to the next widget, read as a private size that wraps the sum */
    Copy = ExactCopy(TplgSynthetic, Size);
    Put32(Copy + TPLG_WIDGET_BLOCK_OFFSET + 24, (UINT32)(TPLG_CONTROL1_OFFSET + 300 - TPLG_WIDGET_BLOCK_OFFSET - 36));
    Put32(Copy + TPLG_CONTROL1_OFFSET + 356, WrapSize(360, 0));
    CHECK_EQ(SoftplgReadBlock(Copy, Size, 1, &Block), SoftplgOk);
    CHECK_EQ(SoftplgReadWidgetControl(Copy, Size, &Block, 1, 0, &Control), SoftplgTruncated);
    CHECK_EQ(SoftplgReadWidget(Copy, Size, &Block, 2, &Widget), SoftplgTruncated);
    free(Copy);

    for (V = 0; V < 3; V++)
    {
        Copy = ExactCopy(TplgSynthetic, Size);
        Put32(Copy + TPLG_CONTROL1_OFFSET + 356, WrapSize(360, V));
        CHECK_EQ(SoftplgReadBlock(Copy, Size, 1, &Block), SoftplgOk);
        CHECK_EQ(SoftplgReadWidgetControl(Copy, Size, &Block, 1, 0, &Control), SoftplgTruncated);
        CHECK_EQ(SoftplgReadWidget(Copy, Size, &Block, 2, &Widget), SoftplgTruncated);
        free(Copy);

        for (E = 0; E < 4; E++)
        {
            Copy = ExactCopy(TplgSynthetic, Size);
            Put32(Copy + ElementPriv[E], WrapSize(ElementBytes[E], V));
            CHECK_EQ(SoftplgReadBlock(Copy, Size, ElementBlock[E], &Block), SoftplgOk);
            CHECK_EQ(E == 0 ? SoftplgReadManifest(Copy, Size, &Block, &Manifest)
                    : E == 1 ? SoftplgReadWidget(Copy, Size, &Block, 0, &Widget)
                    : E == 2 ? SoftplgReadPcm(Copy, Size, &Block, 0, &Pcm)
                    : SoftplgReadLink(Copy, Size, &Block, 0, &Link), SoftplgTruncated);
            free(Copy);
        }
    }

    /* every cut inside a control structure: the file ends there, and the control is refused without a read past the file */
    for (K = 0; K < sizeof(ControlKinds) / sizeof(ControlKinds[0]); K++)
    {
        for (Cut = 0; Cut < ControlKinds[K].Bytes; Cut++)
        {
            Size = 36 + Cut;
            Copy = ExactCopy(ControlKinds[K].Block, Size);
            Put32(Copy + 24, (UINT32)Cut);
            CHECK_EQ(SoftplgReadBlock(Copy, Size, 0, &Block), SoftplgOk);
            CHECK_EQ(SoftplgReadControl(Copy, Size, &Block, 0, &Control), SoftplgTruncated);
            free(Copy);
        }
    }
    for (Cut = 0; Cut < 360; Cut++)
    {
        Size = TPLG_CONTROL1_OFFSET + Cut;
        Copy = ExactCopy(TplgSynthetic, Size);
        Put32(Copy + TPLG_WIDGET_BLOCK_OFFSET + 24, (UINT32)(Size - TPLG_WIDGET_BLOCK_OFFSET - 36));
        CHECK_EQ(SoftplgReadBlock(Copy, Size, 1, &Block), SoftplgOk);
        CHECK_EQ(SoftplgReadWidgetControl(Copy, Size, &Block, 1, 0, &Control), SoftplgTruncated);
        CHECK_EQ(SoftplgReadWidget(Copy, Size, &Block, 2, &Widget), SoftplgTruncated);
        free(Copy);
    }
}

/* A module kind of sof-hda-generic-ace1-2ch.tplg: the last part of the first word of its widgets' names, the module UUID those widgets carry, the module's place in sof-arl-s.ri (the index of its entry in the extended manifest, where Linux takes the module id from), the name the firmware's own module table gives it, and how many widgets carry it. The facts come from an independent decode of both files */
typedef struct _MODULE_FACTS
{
    const char* Suffix;
    UINT8 Uuid[SOFTPLG_UUID_BYTES];
    UINT32 Id;
    const char* Name;
    UINT32 Widgets;
} MODULE_FACTS;

static const MODULE_FACTS AceModules[] =
{
    { "mixin", { 0xB2, 0x6E, 0x65, 0x39, 0x71, 0x3B, 0x49, 0x40, 0x8D, 0x3F, 0xF9, 0x2C, 0xD5, 0xC4, 0x3C, 0x09 }, 3, "MIXIN", 2 },
    { "mixout", { 0x5A, 0x50, 0x56, 0x3C, 0xD7, 0x24, 0x8F, 0x41, 0xBD, 0xDC, 0xC1, 0xF5, 0xA3, 0xAC, 0x2A, 0xE0 }, 4, "MIXOUT", 1 },
    { "copier", { 0x83, 0x0C, 0xA0, 0x9B, 0x12, 0xCA, 0x83, 0x4A, 0x94, 0x3C, 0x1F, 0xA2, 0xE8, 0x2F, 0x9D, 0xDA }, 5, "COPIER", 15 },
    { "gain", { 0xA8, 0xA9, 0xBC, 0x61, 0xD0, 0x18, 0x18, 0x4A, 0x8E, 0x7B, 0x26, 0x39, 0x21, 0x98, 0x04, 0xB7 }, 7, "GAIN", 4 },
    { "eqiir", { 0xE6, 0xC0, 0x50, 0x51, 0xF9, 0x27, 0xC8, 0x4E, 0x83, 0x51, 0xC7, 0x05, 0xB6, 0x42, 0xD1, 0x2F }, 18, "EQIIR", 3 },
    { "eqfir", { 0xE7, 0x0C, 0xA9, 0x43, 0xA5, 0xF3, 0xDF, 0x41, 0xAC, 0x06, 0xBA, 0x98, 0x65, 0x1A, 0xE6, 0xA3 }, 19, "EQFIR", 1 },
    { "drc", { 0xDA, 0xE4, 0x6E, 0xB3, 0x6F, 0x00, 0xF9, 0x47, 0xA0, 0x6D, 0xFE, 0xCB, 0xE2, 0xD8, 0xB6, 0xCE }, 21, "DRC", 2 },
    { "tdfb", { 0x49, 0x17, 0x51, 0xDD, 0xFA, 0xD9, 0x5C, 0x45, 0xB3, 0xA7, 0x13, 0x58, 0x56, 0x93, 0xF1, 0xAF }, 25, "TDFB", 1 }
};

#define ACE_MODULE_COUNT        (sizeof(AceModules) / sizeof(AceModules[0]))

/* A whole file in a heap block of exactly its length */
static
UINT8*
LoadExact(
    const char* Path,
    SIZE_T* Bytes)
{
    FILE* File = fopen(Path, "rb");
    UINT8* Data = NULL;
    long Length = 0;

    *Bytes = 0;
    if (File != NULL)
    {
        fseek(File, 0, SEEK_END);
        Length = ftell(File);
        fseek(File, 0, SEEK_SET);
        if (Length > 0)
            Data = (UINT8*)malloc((SIZE_T)Length);
        if (Data != NULL)
            *Bytes = fread(Data, 1, (SIZE_T)Length, File);
        fclose(File);
    }
    if (Data == NULL || *Bytes != (SIZE_T)Length)
    {
        fprintf(stderr, "t_softplg.c: cannot read %s\n", Path);
        ++Failures;
        free(Data);
        return NULL;
    }
    return Data;
}

/* The module kind a widget's name names, or ACE_MODULE_COUNT: the first word of the name ends with the kind's name, as host-copier, dai-copier and module-copier end in copier */
static
SIZE_T
ModuleKind(
    const char* Name)
{
    const char* Dot = strchr(Name, '.');
    SIZE_T Head = Dot != NULL ? (SIZE_T)(Dot - Name) : strlen(Name), Kind, Length;

    for (Kind = 0; Kind < ACE_MODULE_COUNT; Kind++)
    {
        Length = strlen(AceModules[Kind].Suffix);
        if (Head >= Length && strncmp(Name + Head - Length, AceModules[Kind].Suffix, Length) == 0)
            return Kind;
    }
    return ACE_MODULE_COUNT;
}

static
BOOLEAN
WidgetUuid(
    const UINT8* Data,
    SIZE_T Size,
    const SOFTPLG_WIDGET* Widget,
    UINT8* Uuid)
{
    SOFTPLG_TUPLE_ITER Iterator;
    SOFTPLG_TUPLE Tuple;
    SIZE_T Index;

    if (Widget->Private.Size == 0 || SoftplgTupleBegin(Data, Size, &Widget->Private, &Iterator) != SoftplgOk)
        return FALSE;
    while (SoftplgTupleNext(&Iterator, &Tuple) == SoftplgOk)
    {
        if (Tuple.Type == SOFTPLG_TUPLE_UUID && Tuple.Token == TEST_COMP_UUID)
        {
            for (Index = 0; Index < SOFTPLG_UUID_BYTES; Index++)
                Uuid[Index] = Tuple.Uuid[Index];
            return TRUE;
        }
    }
    return FALSE;
}

/* Edges of sof-hda-generic-ace1-2ch.tplg (sink, source): the analog playback pipeline from the host copier to the link copier, the three HDMI paths and the ends of the two capture paths; each is in the file exactly once */
static const char* const AceEdges[][2] =
{
    { "gain.1.1", "host-copier.0.playback" }, { "mixin.1.1", "gain.1.1" }, { "mixout.2.1", "mixin.1.1" }, { "gain.2.1", "mixout.2.1" },
    { "eqiir.2.1", "gain.2.1" }, { "eqfir.2.1", "eqiir.2.1" }, { "drc.2.1", "eqfir.2.1" }, { "dai-copier.HDA.Analog.playback", "drc.2.1" },
    { "dai-copier.HDA.iDisp1.playback", "host-copier.3.playback" }, { "dai-copier.HDA.iDisp2.playback", "host-copier.4.playback" },
    { "dai-copier.HDA.iDisp3.playback", "host-copier.5.playback" },
    { "eqiir.4.1", "dai-copier.HDA.Analog.capture" }, { "host-copier.0.capture", "module-copier.4.2" },
    { "gain.12.1", "dai-copier.DMIC.dmic01.capture" }, { "host-copier.6.capture", "drc.11.1" }
};

/* Every widget of the fixture that names a module kind carries that module's UUID, and the firmware image's module table has that UUID at the module id the kind resolves to; the widgets that name no module carry none; the graph has the named edges */
static
void
check_ace_fixture(void)
{
    SOF_RI_IMAGE Image;
    PSOF_RI_MODULE Modules;
    SOFTPLG_SUMMARY Summary;
    SOFTPLG_BLOCK Block;
    SOFTPLG_WIDGET Widget;
    SOFTPLG_GRAPH_EDGE Edge;
    UINT8 *Topology, *Firmware, Uuid[SOFTPLG_UUID_BYTES];
    SIZE_T TopologyBytes, FirmwareBytes, B, N, Kind, E, Edges = 0;
    UINT32 M, Resolved[ACE_MODULE_COUNT], Carrying = 0, Plain = 0;
    SIZE_T Found[sizeof(AceEdges) / sizeof(AceEdges[0])];

    memset(Found, 0, sizeof(Found));
    Topology = LoadExact(SOFTPLG_FIXTURE_DIR "/sof-hda-generic-ace1-2ch.tplg", &TopologyBytes);
    Firmware = LoadExact(SOFTPLG_FW_FIXTURE_DIR "/sof-arl-s.ri", &FirmwareBytes);
    CHECK_EQ(FirmwareBytes, 1158164);
    Modules = (PSOF_RI_MODULE)calloc(64, sizeof(SOF_RI_MODULE));
    CHECK(Modules != NULL);
    if (Topology == NULL || Firmware == NULL || Modules == NULL)
        goto Out;

    CHECK_EQ(SofRiParse(Firmware, FirmwareBytes, Modules, 64, &Image), FwloadOk);
    CHECK_EQ(Image.ExtManModuleCount, 32);
    CHECK_EQ(Image.ModuleCount, 32);
    for (M = 0; M < ACE_MODULE_COUNT; M++)
    {
        CHECK(memcmp(Modules[AceModules[M].Id].Uuid, AceModules[M].Uuid, SOFTPLG_UUID_BYTES) == 0);
        CHECK_STR(Modules[AceModules[M].Id].Name, AceModules[M].Name);
    }

    memset(Resolved, 0, sizeof(Resolved));
    CHECK_EQ(SoftplgWalk(Topology, TopologyBytes, NULL, NULL, &Summary), SoftplgOk);
    for (B = 0; B < Summary.BlockCount; B++)
    {
        CHECK_EQ(SoftplgReadBlock(Topology, TopologyBytes, B, &Block), SoftplgOk);
        if (Block.Type != SoftplgTypeWidget)
            continue;
        for (N = 0; N < Block.Count; N++)
        {
            CHECK_EQ(SoftplgReadWidget(Topology, TopologyBytes, &Block, N, &Widget), SoftplgOk);
            Kind = ModuleKind(Widget.Name);
            if (Kind == ACE_MODULE_COUNT)
            {
                CHECK(!WidgetUuid(Topology, TopologyBytes, &Widget, Uuid));
                Plain++;
                continue;
            }
            CHECK(WidgetUuid(Topology, TopologyBytes, &Widget, Uuid));
            CHECK(memcmp(Uuid, AceModules[Kind].Uuid, SOFTPLG_UUID_BYTES) == 0);
            Resolved[Kind]++;
            Carrying++;
        }
    }
    CHECK_EQ(Carrying, 29);
    CHECK_EQ(Plain, 31);
    for (M = 0; M < ACE_MODULE_COUNT; M++)
        CHECK_EQ(Resolved[M], AceModules[M].Widgets);

    for (B = 0; B < Summary.BlockCount; B++)
    {
        CHECK_EQ(SoftplgReadBlock(Topology, TopologyBytes, B, &Block), SoftplgOk);
        if (Block.Type != SoftplgTypeGraph)
            continue;
        for (N = 0; N < Block.Count; N++)
        {
            CHECK_EQ(SoftplgReadGraphEdge(Topology, TopologyBytes, &Block, N, &Edge), SoftplgOk);
            Edges++;
            for (E = 0; E < sizeof(AceEdges) / sizeof(AceEdges[0]); E++)
                if (strcmp(Edge.Sink, AceEdges[E][0]) == 0 && strcmp(Edge.Source, AceEdges[E][1]) == 0)
                {
                    CHECK_EQ(Edge.Control[0], 0);
                    Found[E]++;
                }
        }
    }
    CHECK_EQ(Edges, 23);
    for (E = 0; E < sizeof(AceEdges) / sizeof(AceEdges[0]); E++)
        CHECK_EQ(Found[E], 1);

Out:
    free(Modules);
    free(Firmware);
    free(Topology);
}

/* Topology tokens the loopback case reads: the numbers of the plan's Appendix B, which come from Linux include/uapi/sound/sof/tokens.h studied under LICENSING.md rule 7 */
#define LOOP_NODE_TYPE            1980
#define LOOP_INPUT_FORMATS        415
#define LOOP_OUTPUT_FORMATS       416
#define LOOP_IN_RATE              1900
#define LOOP_IN_DEPTH             1901
#define LOOP_IN_VALID             1902
#define LOOP_IN_IBS               1970
#define LOOP_OUT_RATE              1930
#define LOOP_OUT_DEPTH            1931
#define LOOP_OUT_VALID            1932
#define LOOP_OUT_OBS              1971

/* One audio format as the copier's tuples give it: container depth, valid depth and the buffer size in bytes */
typedef struct _LOOP_FORMAT
{
    UINT32 Depth;
    UINT32 Valid;
    UINT32 Buffer;
} LOOP_FORMAT;

/*
 * The loopback topology this project builds from Sound Open Firmware's topology2 classes
 * (drivers/wdm/audio/intcdsp/firmware/loopback): one pipeline, the host playback copier into a gain
 * into the host capture copier, and a duplex PCM. Both copiers and the gain carry their module's UUID as
 * sof-arl-s.ri's module table knows it (copier at id 5, gain at id 7); the scheduler carries none. The
 * copiers' gateway types are host output 0 and host input 1. The playback copier offers three input
 * formats (16, 24-in-32 and 32 bits at 48 kHz in two channels) and one output format, the capture copier
 * one input and three output formats, as the plan's §4.2 asks. The facts come from the build's own dump
 * of the file and an independent walk of it.
 */
static
void
check_loopback_fixture(void)
{
    static const LOOP_FORMAT PlaybackInputs[3] = { { 16, 16, 192 }, { 32, 24, 384 }, { 32, 32, 384 } };
    static const LOOP_FORMAT CaptureOutputs[3] = { { 32, 24, 384 }, { 32, 32, 384 }, { 16, 16, 192 } };
    static const LOOP_FORMAT ThirtyTwo = { 32, 32, 384 };
    static const char* const LoopEdges[][2] =
    {
        { "gain.1.1", "host-copier.0.playback" }, { "host-copier.0.capture", "gain.1.1" }
    };
    SOF_RI_IMAGE Image;
    PSOF_RI_MODULE Modules;
    SOFTPLG_SUMMARY Summary;
    SOFTPLG_BLOCK Block;
    SOFTPLG_WIDGET Widget;
    SOFTPLG_CONTROL Control;
    SOFTPLG_GRAPH_EDGE Edge;
    SOFTPLG_PCM Pcm;
    SOFTPLG_STREAM_CAPS Caps;
    SOFTPLG_TUPLE_ITER Iterator;
    SOFTPLG_TUPLE Tuple;
    UINT8 *Topology, *Firmware, Uuid[SOFTPLG_UUID_BYTES];
    SIZE_T TopologyBytes, FirmwareBytes, B, N, K, E, Edges = 0, Carrying = 0, Plain = 0;
    SIZE_T Found[sizeof(LoopEdges) / sizeof(LoopEdges[0])];
    UINT32 NodeType = 0xFFFFFFFF, InputFormats = 0, OutputFormats = 0, InputSeen = 0, OutputSeen = 0, Rates = 0;
    LOOP_FORMAT Inputs[3], Outputs[3];

    memset(Found, 0, sizeof(Found));
    Topology = LoadExact(SOFTPLG_FIXTURE_DIR "/sof-ipc4-loopback.tplg", &TopologyBytes);
    Firmware = LoadExact(SOFTPLG_FW_FIXTURE_DIR "/sof-arl-s.ri", &FirmwareBytes);
    Modules = (PSOF_RI_MODULE)calloc(64, sizeof(SOF_RI_MODULE));
    CHECK(Modules != NULL);
    if (Topology == NULL || Firmware == NULL || Modules == NULL)
        goto Out;
    CHECK_EQ(TopologyBytes, 4286);
    CHECK_EQ(SofRiParse(Firmware, FirmwareBytes, Modules, 64, &Image), FwloadOk);

    memset(&Summary, 0, sizeof(Summary));
    CHECK_EQ(SoftplgWalk(Topology, TopologyBytes, NULL, NULL, &Summary), SoftplgOk);
    CHECK_EQ(Summary.Abi, 5);
    CHECK_EQ(Summary.BlockCount, 4);
    CHECK_EQ(Summary.Counts[SoftplgTypeManifest], 1);
    CHECK_EQ(Summary.Counts[SoftplgTypeWidget], 1);
    CHECK_EQ(Summary.Counts[SoftplgTypeMixer], 0);
    CHECK_EQ(Summary.Counts[SoftplgTypeGraph], 1);
    CHECK_EQ(Summary.Counts[SoftplgTypePcm], 1);
    CHECK_EQ(Summary.Counts[SoftplgTypeBackendLink], 0);
    CHECK_EQ(Summary.Counts[SoftplgTypeDai], 0);

    for (B = 0; B < Summary.BlockCount; B++)
    {
        CHECK_EQ(SoftplgReadBlock(Topology, TopologyBytes, B, &Block), SoftplgOk);
        if (Block.Type == SoftplgTypeManifest)
        {
            SOFTPLG_MANIFEST Manifest;
            CHECK_EQ(SoftplgReadManifest(Topology, TopologyBytes, &Block, &Manifest), SoftplgOk);
            CHECK_EQ(Manifest.ControlElements, 2);
            CHECK_EQ(Manifest.WidgetElements, 4);
            CHECK_EQ(Manifest.GraphElements, 2);
            CHECK_EQ(Manifest.PcmElements, 1);
            CHECK_EQ(Manifest.DaiLinkElements, 0);
            CHECK_EQ(Manifest.DaiElements, 0);
            /* the ABI block of the build: 3.29.1, the firmware's ABI at the revision the tree cites */
            CHECK_EQ(Manifest.Private.Size, 6);
            CHECK_EQ(Topology[Manifest.Private.Offset], 3);
            CHECK_EQ(Topology[Manifest.Private.Offset + 2], 29);
            CHECK_EQ(Topology[Manifest.Private.Offset + 4], 1);
            continue;
        }
        if (Block.Type == SoftplgTypeGraph)
        {
            for (N = 0; N < Block.Count; N++)
            {
                CHECK_EQ(SoftplgReadGraphEdge(Topology, TopologyBytes, &Block, N, &Edge), SoftplgOk);
                Edges++;
                for (E = 0; E < sizeof(LoopEdges) / sizeof(LoopEdges[0]); E++)
                    if (strcmp(Edge.Sink, LoopEdges[E][0]) == 0 && strcmp(Edge.Source, LoopEdges[E][1]) == 0)
                    {
                        CHECK_EQ(Edge.Control[0], 0);
                        Found[E]++;
                    }
            }
            continue;
        }
        if (Block.Type == SoftplgTypePcm)
        {
            CHECK_EQ(Block.Count, 1);
            CHECK_EQ(SoftplgReadPcm(Topology, TopologyBytes, &Block, 0, &Pcm), SoftplgOk);
            CHECK_STR(Pcm.PcmName, "Loopback");
            CHECK_STR(Pcm.DaiName, "Loopback");
            CHECK_EQ(Pcm.PcmId, 0);
            CHECK_EQ(Pcm.Playback, 1);
            CHECK_EQ(Pcm.Capture, 1);
            CHECK_EQ(Pcm.Compress, 0);
            for (K = 0; K < 2; K++)
            {
                CHECK_EQ(SoftplgReadPcmCaps(Topology, TopologyBytes, &Block, 0, (UINT32)K, &Caps), SoftplgOk);
                /* S16_LE, S24_LE and S32_LE; 48000; two channels */
                CHECK_EQ(Caps.Formats, 0x444);
                CHECK_EQ(Caps.Rates, 0x80);
                CHECK_EQ(Caps.ChannelsMin, 2);
                CHECK_EQ(Caps.ChannelsMax, 2);
            }
            CHECK_STR(Caps.Name, "Loopback Capture");
            CHECK_EQ(SoftplgReadPcmCaps(Topology, TopologyBytes, &Block, 0, 0, &Caps), SoftplgOk);
            CHECK_STR(Caps.Name, "Loopback Playback");
            continue;
        }
        if (Block.Type != SoftplgTypeWidget)
            continue;
        for (N = 0; N < Block.Count; N++)
        {
            CHECK_EQ(SoftplgReadWidget(Topology, TopologyBytes, &Block, N, &Widget), SoftplgOk);
            NodeType = 0xFFFFFFFF;
            InputFormats = OutputFormats = InputSeen = OutputSeen = Rates = 0;
            memset(Inputs, 0, sizeof(Inputs));
            memset(Outputs, 0, sizeof(Outputs));
            if (SoftplgTupleBegin(Topology, TopologyBytes, &Widget.Private, &Iterator) == SoftplgOk)
            {
                while (SoftplgTupleNext(&Iterator, &Tuple) == SoftplgOk)
                {
                    if (Tuple.Token == LOOP_NODE_TYPE) NodeType = Tuple.Value;
                    else if (Tuple.Token == LOOP_INPUT_FORMATS) InputFormats = Tuple.Value;
                    else if (Tuple.Token == LOOP_OUTPUT_FORMATS) OutputFormats = Tuple.Value;
                    else if (Tuple.Token == LOOP_IN_RATE) CHECK_EQ(Tuple.Value, 48000);
                    /* a format's tuples come as its buffer size, its flags, then rate, container depth and valid depth: the slot advances on the last of them */
                    else if (Tuple.Token == LOOP_IN_IBS && InputSeen < 3) Inputs[InputSeen].Buffer = Tuple.Value;
                    else if (Tuple.Token == LOOP_IN_DEPTH && InputSeen < 3) Inputs[InputSeen].Depth = Tuple.Value;
                    else if (Tuple.Token == LOOP_IN_VALID && InputSeen < 3)
                    {
                        Inputs[InputSeen].Valid = Tuple.Value;
                        InputSeen++;
                    }
                    else if (Tuple.Token == LOOP_OUT_OBS && OutputSeen < 3) Outputs[OutputSeen].Buffer = Tuple.Value;
                    else if (Tuple.Token == LOOP_OUT_DEPTH && OutputSeen < 3) Outputs[OutputSeen].Depth = Tuple.Value;
                    else if (Tuple.Token == LOOP_OUT_VALID && OutputSeen < 3)
                    {
                        Outputs[OutputSeen].Valid = Tuple.Value;
                        OutputSeen++;
                    }
                    else if (Tuple.Token == LOOP_OUT_RATE) Rates++;
                }
            }
            if (strcmp(Widget.Name, "host-copier.0.playback") == 0)
            {
                CHECK(WidgetUuid(Topology, TopologyBytes, &Widget, Uuid));
                CHECK(memcmp(Uuid, AceModules[2].Uuid, SOFTPLG_UUID_BYTES) == 0);
                CHECK(memcmp(Modules[AceModules[2].Id].Uuid, Uuid, SOFTPLG_UUID_BYTES) == 0);
                CHECK_STR(Widget.SName, "Loopback Playback");
                CHECK_EQ(Widget.Id, 11);
                CHECK_EQ(NodeType, 0);
                CHECK_EQ(InputFormats, 3);
                CHECK_EQ(OutputFormats, 1);
                CHECK_EQ(InputSeen, 3);
                CHECK_EQ(OutputSeen, 1);
                CHECK(memcmp(Inputs, PlaybackInputs, sizeof(Inputs)) == 0);
                CHECK(memcmp(Outputs, &ThirtyTwo, sizeof(ThirtyTwo)) == 0);
                CHECK_EQ(Rates, 1);
                Carrying++;
            }
            else if (strcmp(Widget.Name, "host-copier.0.capture") == 0)
            {
                CHECK(WidgetUuid(Topology, TopologyBytes, &Widget, Uuid));
                CHECK(memcmp(Uuid, AceModules[2].Uuid, SOFTPLG_UUID_BYTES) == 0);
                CHECK_STR(Widget.SName, "Loopback Capture");
                CHECK_EQ(Widget.Id, 12);
                CHECK_EQ(NodeType, 1);
                CHECK_EQ(InputFormats, 1);
                CHECK_EQ(OutputFormats, 3);
                CHECK_EQ(InputSeen, 1);
                CHECK_EQ(OutputSeen, 3);
                CHECK(memcmp(Inputs, &ThirtyTwo, sizeof(ThirtyTwo)) == 0);
                CHECK(memcmp(Outputs, CaptureOutputs, sizeof(Outputs)) == 0);
                CHECK_EQ(Rates, 3);
                Carrying++;
            }
            else if (strcmp(Widget.Name, "gain.1.1") == 0)
            {
                CHECK(WidgetUuid(Topology, TopologyBytes, &Widget, Uuid));
                CHECK(memcmp(Uuid, AceModules[3].Uuid, SOFTPLG_UUID_BYTES) == 0);
                CHECK_STR(Modules[AceModules[3].Id].Name, "GAIN");
                CHECK_EQ(Widget.Id, 4);
                CHECK_EQ(Widget.ControlCount, 2);
                CHECK_EQ(SoftplgReadWidgetControl(Topology, TopologyBytes, &Block, (UINT32)N, 0, &Control), SoftplgOk);
                CHECK_STR(Control.Name, "Loopback Volume");
                CHECK_EQ(Control.Minimum, 0);
                CHECK_EQ(Control.Maximum, 45);
                CHECK_EQ(SoftplgReadWidgetControl(Topology, TopologyBytes, &Block, (UINT32)N, 1, &Control), SoftplgOk);
                CHECK_STR(Control.Name, "Loopback Switch");
                CHECK_EQ(Control.Minimum, 0);
                CHECK_EQ(Control.Maximum, 1);
                CHECK_EQ(NodeType, 0xFFFFFFFF);
                CHECK_EQ(InputFormats, 1);
                CHECK_EQ(OutputFormats, 1);
                Carrying++;
            }
            else if (strcmp(Widget.Name, "pipeline.1") == 0)
            {
                CHECK(!WidgetUuid(Topology, TopologyBytes, &Widget, Uuid));
                CHECK_EQ(Widget.Id, 17);
                CHECK_EQ(NodeType, 0xFFFFFFFF);
                Plain++;
            }
            else
                CHECK(FALSE);
        }
    }
    CHECK_EQ(Carrying, 3);
    CHECK_EQ(Plain, 1);
    CHECK_EQ(Edges, 2);
    for (E = 0; E < sizeof(LoopEdges) / sizeof(LoopEdges[0]); E++)
        CHECK_EQ(Found[E], 1);

Out:
    free(Modules);
    free(Firmware);
    free(Topology);
}

/* The IPC4 model over the four fixtures and the malformed inputs of plan section 8 C2 */
#include <ipc4model.h>

static SOF_RI_MODULE ModelModules[64];
static SOF_RI_IMAGE ModelImage;
static SOFTPLG_MODEL_STATUS ModelStatus;
static SOFTPLG_IPC4_PROBLEM ModelProblem;

/* The firmware image's module table, read once for every model test */
static
void
ModelLoadFirmware(
    void)
{
    static BOOLEAN Loaded;
    UINT8* Firmware;
    SIZE_T Bytes;

    if (Loaded)
        return;
    Loaded = TRUE;
    Firmware = LoadExact(SOFTPLG_FW_FIXTURE_DIR "/sof-arl-s.ri", &Bytes);
    CHECK(Firmware != NULL);
    if (Firmware == NULL)
        return;
    CHECK_EQ(SofRiParse(Firmware, Bytes, ModelModules, 64, &ModelImage), FwloadOk);
    free(Firmware);
}

/* The module id of a UUID: its index in the firmware image's module table */
static
UINT32
ModelLookup(
    PVOID Context,
    const UINT8* Uuid)
{
    UINT32 M;

    for (M = 0; M < ModelImage.ModuleCount; M++)
        if (memcmp(ModelModules[M].Uuid, Uuid, SOFTPLG_UUID_BYTES) == 0)
            return M;
    return SOFTPLG_MODULE_NONE;
}

static
PSOFTPLG_IPC4_MODEL
ModelBuild(
    const UINT8* Data,
    SIZE_T Size,
    SIZE_T* Bytes)
{
    SOFTPLG_MODEL_INPUT Input;
    PSOFTPLG_IPC4_MODEL Model = NULL;
    UINT8* Buffer;

    memset(&Input, 0, sizeof(Input));
    Input.Data = Data;
    Input.Size = Size;
    Input.Lookup = ModelLookup;
    *Bytes = 0;
    ModelStatus = SoftplgModelMeasure(&Input, Bytes, &ModelProblem);
    if (ModelStatus != SoftplgModelOk)
    {
        fprintf(stderr, "  the model was not measured: %s\n", SoftplgModelStatusName(ModelStatus));
        return NULL;
    }
    Buffer = (UINT8*)malloc(*Bytes);
    CHECK(Buffer != NULL);
    if (Buffer == NULL)
        exit(2);
    ModelStatus = SoftplgModelBuild(&Input, Buffer, *Bytes, &Model, &ModelProblem);
    if (ModelStatus != SoftplgModelOk)
    {
        fprintf(stderr, "  the model was not built: %s\n", SoftplgModelStatusName(ModelStatus));
        free(Buffer);
        return NULL;
    }
    /* an exact-size heap block, so the sanitizers see every read past the model */
    CHECK(Model == (PSOFTPLG_IPC4_MODEL)Buffer);
    return Model;
}

static
UINT32
ModelWidget(
    const SOFTPLG_IPC4_MODEL* Model,
    const char* Name)
{
    UINT32 W;

    for (W = 0; W < Model->WidgetCount; W++)
        if (strcmp(Model->Widgets[W].Name, Name) == 0)
            return W;
    CHECK(FALSE);
    return SOFTPLG_NO_WIDGET;
}

static
UINT32
ModelPcm(
    const SOFTPLG_IPC4_MODEL* Model,
    const char* Name)
{
    UINT32 P;

    for (P = 0; P < Model->PcmCount; P++)
        if (strcmp(Model->Pcms[P].Name, Name) == 0)
            return P;
    CHECK(FALSE);
    return SOFTPLG_NO_PCM;
}

static
SOFTPLG_MODEL_STATUS
ModelValidate(
    const SOFTPLG_IPC4_MODEL* Model,
    UINT32 MaxPipelines,
    UINT32 MailboxBytes,
    const char** Widget)
{
    SOFTPLG_IPC4_LIMITS Limits;
    SOFTPLG_IPC4_PROBLEM Problem;

    Limits.MaxPipelines = MaxPipelines;
    Limits.MailboxBytes = MailboxBytes;
    *Widget = NULL;
    if (SoftplgModelValidate(Model, &Limits, &Problem) != Problem.Status)
        CHECK(FALSE);
    if (Problem.Widget != SOFTPLG_NO_WIDGET)
        *Widget = Model->Widgets[Problem.Widget].Name;
    return Problem.Status;
}

/* One format as the model keeps it: rate, depth, the configuration word, the pin's buffer */
#define CHECK_FORMAT(F, Rate, Depth, Cfg, Buffer, IsExpressible) \
    do { \
        CHECK_EQ((F)->Words[0], (Rate)); \
        CHECK_EQ((F)->Words[1], (Depth)); \
        CHECK_EQ((F)->Words[2], 0xFFFFFF10u); \
        CHECK_EQ((F)->Words[3], 1); \
        CHECK_EQ((F)->Words[4], 0); \
        CHECK_EQ((F)->Words[5], (Cfg)); \
        CHECK_EQ((F)->BufferBytes, (Buffer)); \
        CHECK_EQ((F)->Expressible, (IsExpressible)); \
    } while (0)

static
void
check_model_expressible(void)
{
    CHECK(SoftplgFormatExpressible(48000, 2, 16, 16));
    CHECK(SoftplgFormatExpressible(48000, 2, 24, 32));
    CHECK(SoftplgFormatExpressible(48000, 2, 32, 32));
    CHECK(SoftplgFormatExpressible(8000, 1, 8, 8));
    CHECK(SoftplgFormatExpressible(384000, 8, 32, 32));
    CHECK(SoftplgFormatExpressible(192000, 2, 20, 32));
    CHECK(SoftplgFormatExpressible(44100, 2, 16, 16));
    CHECK(SoftplgFormatExpressible(9600, 2, 16, 16));
    /* a packed 24-bit stream, a rate the stream format word has no code for, no channel or too many, a depth without a code */
    CHECK(!SoftplgFormatExpressible(48000, 2, 24, 24));
    CHECK(!SoftplgFormatExpressible(48000, 2, 16, 32));
    CHECK(!SoftplgFormatExpressible(64000, 2, 16, 16));
    CHECK(!SoftplgFormatExpressible(24000, 2, 16, 16));
    CHECK(!SoftplgFormatExpressible(48000, 0, 16, 16));
    CHECK(!SoftplgFormatExpressible(48000, 9, 16, 16));
    CHECK(!SoftplgFormatExpressible(48000, 2, 12, 16));
    CHECK(!SoftplgFormatExpressible(48000, 2, 64, 64));
    CHECK(!SoftplgFormatExpressible(0, 2, 16, 16));
}

/*
 * The loopback topology: one pipeline, the host playback copier through the
 * gain to the host capture copier, one duplex PCM. Every number comes from
 * the build's own dump of the file and an independent walk of it.
 */
static
void
check_model_loopback(void)
{
    static const UINT32 PlaybackConfig[SOFTPLG_IPC4_COPIER_CONFIG_WORDS] =
    {
        1647, 192, 384, 1, 48000, 16, 0xFFFFFF10, 1, 0, 0x11002,
        48000, 32, 0xFFFFFF10, 1, 0, 0x12002, 0, 0x0000FF, 384, 1, 0
    };
    static const UINT32 CaptureConfig[SOFTPLG_IPC4_COPIER_CONFIG_WORDS] =
    {
        1647, 384, 384, 1, 48000, 32, 0xFFFFFF10, 1, 0, 0x12002,
        48000, 32, 0xFFFFFF10, 1, 0, 0x11802, 0, 0x0001FF, 768, 1, 0
    };
    static const UINT32 GainConfig[SOFTPLG_IPC4_BASE_CONFIG_WORDS] =
    {
        0, 384, 384, 0, 48000, 32, 0xFFFFFF10, 1, 0, 0x12002
    };
    SOFTPLG_IPC4_MANIFEST Manifest;
    SOFTPLG_MODEL_INPUT Input;
    SOFTPLG_IPC4_CONFIG Config;
    SOFTPLG_BLOCK Block;
    PSOFTPLG_IPC4_MODEL Model;
    PSOFTPLG_IPC4_WIDGET W;
    PSOFTPLG_IPC4_PCM Pcm;
    UINT8* Data;
    SIZE_T Size, Bytes, K;
    const char* Named;
    int N;

    Data = LoadExact(SOFTPLG_FIXTURE_DIR "/sof-ipc4-loopback.tplg", &Size);
    CHECK(Data != NULL);
    if (Data == NULL)
        return;
    ModelLoadFirmware();

    CHECK_EQ(SoftplgReadBlock(Data, Size, 0, &Block), SoftplgOk);
    CHECK_EQ(SoftplgIpc4ReadManifest(Data, Size, &Block, &Manifest), SoftplgModelOk);
    CHECK_EQ(Manifest.Major, 3);
    CHECK_EQ(Manifest.Minor, 29);
    CHECK_EQ(Manifest.Patch, 1);
    CHECK_EQ(Manifest.ItemCount, 0);
    CHECK_EQ(Manifest.Nhlt.Size, 0);

    Model = ModelBuild(Data, Size, &Bytes);
    CHECK(Model != NULL);
    if (Model == NULL)
        goto Out;
    CHECK_EQ(Bytes % 8, 0);
    CHECK_EQ(Model->AbiMajor, 3);
    CHECK_EQ(Model->AbiMinor, 29);
    CHECK_EQ(Model->AbiPatch, 1);
    CHECK_EQ(Model->ManifestItems, 0);
    CHECK_EQ(Model->ControlCount, 2);
    CHECK_EQ(Model->WidgetCount, 4);
    CHECK_EQ(Model->EdgeCount, 2);
    CHECK_EQ(Model->PcmCount, 1);
    CHECK_EQ(Model->PipelineCount, 1);
    CHECK_EQ(Model->FormatCount, 10);
    CHECK_EQ(Model->ReachWords, 1);
    CHECK(strcmp(Model->Widgets[0].Name, "host-copier.0.playback") == 0);
    CHECK(strcmp(Model->Widgets[1].Name, "host-copier.0.capture") == 0);
    CHECK(strcmp(Model->Widgets[2].Name, "gain.1.1") == 0);
    CHECK(strcmp(Model->Widgets[3].Name, "pipeline.1") == 0);

    W = &Model->Widgets[0];
    CHECK_EQ(W->Kind, SoftplgKindHostCopierPlayback);
    CHECK_EQ(W->Type, 11);
    CHECK_STR(W->SName, "Loopback Playback");
    CHECK_EQ(W->PipelineId, 1);
    CHECK_EQ(W->Pipeline, 0);
    CHECK_EQ(W->Pcm, 0);
    CHECK(W->HasUuid);
    CHECK(memcmp(W->Uuid, AceModules[2].Uuid, SOFTPLG_UUID_BYTES) == 0);
    CHECK_EQ(W->ModuleId, 5);
    CHECK_STR(ModelModules[W->ModuleId].Name, "COPIER");
    CHECK(W->HasNodeType);
    CHECK_EQ(W->NodeType, 0);
    CHECK_EQ(W->CoreId, 0);
    CHECK_EQ(W->Cpc, 1647);
    CHECK_EQ(W->IsPages, 1);
    CHECK_EQ(W->InputPins, 0);
    CHECK_EQ(W->OutputPins, 1);
    CHECK_EQ(W->StackBytes, 8192);
    CHECK_EQ(W->HeapBytes, 24576);
    CHECK_EQ(W->InputFormats, 3);
    CHECK_EQ(W->OutputFormats, 1);
    CHECK_EQ(W->FirstFormat, 0);
    CHECK_EQ(W->ControlCount, 0);
    CHECK_FORMAT(&Model->Formats[0], 48000, 16, 0x11002, 192, TRUE);
    CHECK_FORMAT(&Model->Formats[1], 48000, 32, 0x11802, 384, TRUE);
    CHECK_FORMAT(&Model->Formats[2], 48000, 32, 0x12002, 384, TRUE);
    CHECK_FORMAT(&Model->Formats[3], 48000, 32, 0x12002, 384, TRUE);
    CHECK_EQ(W->Config.Count, SOFTPLG_IPC4_COPIER_CONFIG_WORDS);
    for (N = 0; N < SOFTPLG_IPC4_COPIER_CONFIG_WORDS; N++)
        CHECK_EQ(W->Config.Words[N], PlaybackConfig[N]);
    CHECK_EQ(SoftplgModelConfig(Model, 0, 0, 0, 1, &Config), SoftplgModelOk);
    CHECK_EQ(Config.Words[SOFTPLG_COPIER_NODE_ID_WORD], 0x0000);
    CHECK_EQ(SoftplgModelConfig(Model, 0, 0, 0, 5, &Config), SoftplgModelOk);
    CHECK_EQ(Config.Words[SOFTPLG_COPIER_NODE_ID_WORD], 0x0004);
    for (N = 0; N < SOFTPLG_IPC4_COPIER_CONFIG_WORDS; N++)
        if (N != SOFTPLG_COPIER_NODE_ID_WORD)
            CHECK_EQ(Config.Words[N], PlaybackConfig[N]);
    /* the second input format is the 24-in-32 one: its buffer is the base configuration's */
    CHECK_EQ(SoftplgModelConfig(Model, 0, 1, 0, 5, &Config), SoftplgModelOk);
    CHECK_EQ(Config.Words[1], 384);
    CHECK_EQ(Config.Words[4], 48000);
    CHECK_EQ(Config.Words[5], 32);
    CHECK_EQ(Config.Words[9], 0x11802);
    CHECK_EQ(Config.Words[18], 2 * 384);

    W = &Model->Widgets[1];
    CHECK_EQ(W->Kind, SoftplgKindHostCopierCapture);
    CHECK_EQ(W->Type, 12);
    CHECK_STR(W->SName, "Loopback Capture");
    CHECK_EQ(W->PipelineId, 1);
    CHECK_EQ(W->Pcm, 0);
    CHECK_EQ(W->ModuleId, 5);
    CHECK(W->HasNodeType);
    CHECK_EQ(W->NodeType, 1);
    CHECK_EQ(W->Cpc, 1647);
    CHECK_EQ(W->IsPages, 1);
    CHECK_EQ(W->InputPins, 1);
    CHECK_EQ(W->OutputPins, 0);
    CHECK_EQ(W->InputFormats, 1);
    CHECK_EQ(W->OutputFormats, 3);
    CHECK_EQ(W->FirstFormat, 4);
    CHECK_FORMAT(&Model->Formats[4], 48000, 32, 0x12002, 384, TRUE);
    CHECK_FORMAT(&Model->Formats[5], 48000, 32, 0x11802, 384, TRUE);
    CHECK_FORMAT(&Model->Formats[6], 48000, 32, 0x12002, 384, TRUE);
    CHECK_FORMAT(&Model->Formats[7], 48000, 16, 0x11002, 192, TRUE);
    CHECK_EQ(W->Config.Count, SOFTPLG_IPC4_COPIER_CONFIG_WORDS);
    for (N = 0; N < SOFTPLG_IPC4_COPIER_CONFIG_WORDS; N++)
        CHECK_EQ(W->Config.Words[N], CaptureConfig[N]);
    CHECK_EQ(SoftplgModelConfig(Model, 1, 0, 0, 5, &Config), SoftplgModelOk);
    CHECK_EQ(Config.Words[SOFTPLG_COPIER_NODE_ID_WORD], 0x0104);
    /* the gateway buffer of a capture copier is two milliseconds of its output buffer */
    CHECK_EQ(Config.Words[SOFTPLG_COPIER_DMA_BUFFER_WORD], 768);

    W = &Model->Widgets[2];
    CHECK_EQ(W->Kind, SoftplgKindProcessor);
    CHECK_EQ(W->Type, 4);
    CHECK_STR(W->SName, "");
    CHECK_EQ(W->PipelineId, 1);
    CHECK_EQ(W->Pipeline, 0);
    CHECK_EQ(W->Pcm, SOFTPLG_NO_PCM);
    CHECK(memcmp(W->Uuid, AceModules[3].Uuid, SOFTPLG_UUID_BYTES) == 0);
    CHECK_EQ(W->ModuleId, 7);
    CHECK_STR(ModelModules[W->ModuleId].Name, "GAIN");
    CHECK(!W->HasNodeType);
    CHECK_EQ(W->Cpc, 0);
    CHECK_EQ(W->IsPages, 0);
    CHECK_EQ(W->InputPins, 1);
    CHECK_EQ(W->OutputPins, 1);
    CHECK_EQ(W->ControlCount, 2);
    CHECK_EQ(W->InputFormats, 1);
    CHECK_EQ(W->OutputFormats, 1);
    CHECK_EQ(W->FirstFormat, 8);
    CHECK_FORMAT(&Model->Formats[8], 48000, 32, 0x12002, 384, TRUE);
    CHECK_FORMAT(&Model->Formats[9], 48000, 32, 0x12002, 384, TRUE);
    CHECK_EQ(W->GainRampType, 2);
    CHECK_EQ(W->GainRampDuration, 200000);
    CHECK_EQ(W->GainVal, 2147483647);
    /* the gain's configuration ends with its volume entry: every channel, the initial gain, the curve, a reserved word, the duration */
    CHECK_EQ(W->Config.Count, SOFTPLG_IPC4_BASE_CONFIG_WORDS + SOFTPLG_IPC4_GAIN_CONFIG_WORDS);
    for (N = 0; N < SOFTPLG_IPC4_BASE_CONFIG_WORDS; N++)
        CHECK_EQ(W->Config.Words[N], GainConfig[N]);
    CHECK_EQ(W->Config.Words[SOFTPLG_GAIN_ENTRY_WORD], 0xffffffff);
    CHECK_EQ(W->Config.Words[SOFTPLG_GAIN_ENTRY_WORD + 1], 0x7fffffff);
    CHECK_EQ(W->Config.Words[SOFTPLG_GAIN_ENTRY_WORD + 2], 2);
    CHECK_EQ(W->Config.Words[SOFTPLG_GAIN_ENTRY_WORD + 3], 0);
    CHECK_EQ(W->Config.Words[SOFTPLG_GAIN_ENTRY_WORD + 4], 200000);
    CHECK_EQ(W->Config.Words[SOFTPLG_GAIN_ENTRY_WORD + 5], 0);
    for (N = SOFTPLG_IPC4_BASE_CONFIG_WORDS + SOFTPLG_IPC4_GAIN_CONFIG_WORDS; N < SOFTPLG_IPC4_CONFIG_WORDS_MAX; N++)
        CHECK_EQ(W->Config.Words[N], 0);

    W = &Model->Widgets[3];
    CHECK_EQ(W->Kind, SoftplgKindScheduler);
    CHECK_EQ(W->Type, 17);
    CHECK(!W->HasUuid);
    CHECK_EQ(W->ModuleId, SOFTPLG_MODULE_NONE);
    CHECK_EQ(W->PipelineId, 1);
    CHECK_EQ(W->Pipeline, 0);
    CHECK_EQ(W->InputFormats, 0);
    CHECK_EQ(W->OutputFormats, 0);
    CHECK_EQ(W->FirstFormat, SOFTPLG_NO_FORMAT);
    CHECK_EQ(W->Priority, 0);
    CHECK_EQ(W->SchedCoreId, 0);
    CHECK_EQ(W->TimeDomain, 1);
    CHECK_EQ(W->DynamicPipeline, 1);
    CHECK_EQ(W->LpMode, 0);
    CHECK_EQ(W->MemUsage, 0);
    CHECK_EQ(W->UseChainDma, 0);
    CHECK_EQ(W->Kcps, 0);
    CHECK_EQ(W->Direction, 0);
    CHECK_EQ(W->DirectionValid, 1);
    CHECK_EQ(W->Config.Count, 0);
    CHECK_EQ(Model->Pipelines[0].Id, 1);
    CHECK_EQ(Model->Pipelines[0].Scheduler, 3);

    CHECK_EQ(Model->Edges[0].Source, 0);
    CHECK_EQ(Model->Edges[0].Sink, 2);
    CHECK_EQ(Model->Edges[1].Source, 2);
    CHECK_EQ(Model->Edges[1].Sink, 1);

    CHECK_EQ(ModelPcm(Model, "Loopback"), 0);
    Pcm = &Model->Pcms[0];
    CHECK_STR(Pcm->DaiName, "Loopback");
    CHECK_EQ(Pcm->PcmId, 0);
    CHECK_EQ(Pcm->DaiId, 0);
    CHECK_EQ(Pcm->Playback, 1);
    CHECK_EQ(Pcm->Capture, 1);
    CHECK_STR(Pcm->PlaybackStream, "Loopback Playback");
    CHECK_STR(Pcm->CaptureStream, "Loopback Capture");
    CHECK_EQ(Pcm->PlaybackCopier, 0);
    CHECK_EQ(Pcm->CaptureCopier, 1);
    CHECK_EQ(Pcm->Reached, 3);
    CHECK_EQ(Pcm->PipelineMask[0], 1);
    for (K = 1; K < SOFTPLG_PIPELINE_MASK_WORDS; K++)
        CHECK_EQ(Pcm->PipelineMask[K], 0);
    CHECK(SoftplgModelReachable(Model, 0, 0));
    CHECK(SoftplgModelReachable(Model, 0, 1));
    CHECK(SoftplgModelReachable(Model, 0, 2));
    /* the scheduler is not on the stream's path */
    CHECK(!SoftplgModelReachable(Model, 0, 3));
    CHECK(!SoftplgModelReachable(Model, 0, 4));
    CHECK(!SoftplgModelReachable(Model, 1, 0));

    /* the firmware's limits: the model's pipeline count and mailbox take it, a count equal to the pipelines does not */
    CHECK_EQ(ModelValidate(Model, 16, 4096, &Named), SoftplgModelOk);
    CHECK(Named == NULL);
    CHECK_EQ(ModelValidate(Model, 1, 4096, &Named), SoftplgModelTooManyPipelines);
    CHECK(Named == NULL);
    CHECK_EQ(ModelValidate(Model, 2, 4096, &Named), SoftplgModelOk);
    CHECK_EQ(ModelValidate(Model, 16, 40, &Named), SoftplgModelConfigTooLarge);
    CHECK_STR(Named, "host-copier.0.playback");
    CHECK_EQ(ModelValidate(Model, 16, 84, &Named), SoftplgModelOk);

    /* the configuration calls outside the model */
    CHECK_EQ(SoftplgModelConfig(Model, 4, 0, 0, 1, &Config), SoftplgModelNotFound);
    CHECK_EQ(SoftplgModelConfig(Model, 3, 0, 0, 1, &Config), SoftplgModelNotFound);
    CHECK_EQ(SoftplgModelConfig(Model, 0, 3, 0, 1, &Config), SoftplgModelNotFound);
    CHECK_EQ(SoftplgModelConfig(Model, 0, 0, 1, 1, &Config), SoftplgModelNotFound);
    CHECK_EQ(SoftplgModelConfig(Model, 2, 0, 1, 1, &Config), SoftplgModelNotFound);
    CHECK_EQ(SoftplgModelConfig(Model, 2, 1, 0, 1, &Config), SoftplgModelNotFound);
    CHECK_EQ(SoftplgModelConfig(NULL, 0, 0, 0, 1, &Config), SoftplgModelNotFound);

    /* a buffer one byte short of the measure is refused; no buffer at all is */
    memset(&Input, 0, sizeof(Input));
    Input.Data = Data;
    Input.Size = Size;
    Input.Lookup = ModelLookup;
    {
        SOFTPLG_IPC4_MODEL* None = (PSOFTPLG_IPC4_MODEL)Data;

        CHECK_EQ(SoftplgModelBuild(&Input, NULL, Bytes, &None, NULL), SoftplgModelBufferTooSmall);
        CHECK(None == NULL);
        CHECK_EQ(SoftplgModelBuild(&Input, (UINT8*)Model, Bytes - 1, &None, NULL), SoftplgModelBufferTooSmall);
        CHECK(None == NULL);
    }
    Input.Data = NULL;
    Input.Size = 0;
    CHECK_EQ(SoftplgModelMeasure(&Input, &Bytes, &ModelProblem), SoftplgModelNoData);
    CHECK_EQ(ModelProblem.Widget, SOFTPLG_NO_WIDGET);
    CHECK_EQ(Bytes, 0);

    /* FB-03: every status's name against a table of this test's own, the newest status included; the table is of as many rows as there are statuses, so a status added without its row is seen */
    {
        static const struct
        {
            SOFTPLG_MODEL_STATUS Status;
            const char* Name;
        } StatusNames[] = {
            { SoftplgModelOk, "ok" },
            { SoftplgModelNoData, "no data" },
            { SoftplgModelBadFile, "not an ALSA topology" },
            { SoftplgModelNoManifest, "no manifest" },
            { SoftplgModelAbiMismatch, "topology ABI mismatch" },
            { SoftplgModelManifestTruncated, "manifest truncated" },
            { SoftplgModelTooLarge, "too large" },
            { SoftplgModelBufferTooSmall, "model buffer too small" },
            { SoftplgModelUnknownWidget, "unknown widget type" },
            { SoftplgModelTupleType, "token of the wrong tuple type" },
            { SoftplgModelNoUuid, "module widget without a UUID" },
            { SoftplgModelModuleMissing, "module not in the firmware image" },
            { SoftplgModelNoFormats, "module widget without an audio format" },
            { SoftplgModelFormatCount, "format tokens do not match the format count" },
            { SoftplgModelFormatIncomplete, "format without its words" },
            { SoftplgModelFormatMismatch, "format tokens disagree" },
            { SoftplgModelBadEdge, "graph element names no widget" },
            { SoftplgModelNoScheduler, "widget without a pipeline scheduler" },
            { SoftplgModelDuplicateScheduler, "two schedulers for one pipeline" },
            { SoftplgModelTooManyPipelines, "more pipelines than the firmware allows" },
            { SoftplgModelGatewayData, "copier needs gateway data" },
            { SoftplgModelNodeType, "host copier of another gateway type" },
            { SoftplgModelFormatInexpressible, "no format the controller can carry" },
            { SoftplgModelConfigTooLarge, "configuration larger than the mailbox" },
            { SoftplgModelNotFound, "not in the model" },
            { SoftplgModelGatewayBuffer, "gateway buffer does not fit its word" }
        };
        UINT32 N;

        CHECK_EQ((UINT32)(sizeof(StatusNames) / sizeof(StatusNames[0])), (UINT32)SoftplgModelGatewayBuffer + 1);
        for (N = 0; N < sizeof(StatusNames) / sizeof(StatusNames[0]); N++)
            CHECK_STR(SoftplgModelStatusName(StatusNames[N].Status), StatusNames[N].Name);
    }
    CHECK_STR(SoftplgModelStatusName(SoftplgModelOk), "ok");
    CHECK_STR(SoftplgModelStatusName(SoftplgModelGatewayData), "copier needs gateway data");
    CHECK_STR(SoftplgModelStatusName((SOFTPLG_MODEL_STATUS)200), "unknown");
    CHECK_STR(SoftplgWidgetKindName(SoftplgKindScheduler), "scheduler");
    CHECK_STR(SoftplgWidgetKindName((SOFTPLG_IPC4_KIND)200), "unknown");

Out:
    free(Model);
    free(Data);
}

/* A module kind of the shipped files: the first word of the widgets' names, the module id the UUID resolves to, and the firmware's name for it */
typedef struct _MODEL_KIND_FACTS
{
    const char* Prefix;
    UINT32 Id;
    const char* Name;
} MODEL_KIND_FACTS;

static const MODEL_KIND_FACTS ModelKinds[] =
{
    { "mixin", 3, "MIXIN" }, { "mixout", 4, "MIXOUT" }, { "copier", 5, "COPIER" }, { "gain", 7, "GAIN" },
    { "eqiir", 18, "EQIIR" }, { "eqfir", 19, "EQFIR" }, { "drc", 21, "DRC" }, { "tdfb", 25, "TDFB" }
};

/* The node type a copier's name and DAI type give it: host playback 0, host capture 1, HDA link playback 8 and capture 9, DMIC 11, ALH playback 16 and capture 17 */
static
UINT32
ModelCopierNodeType(
    const PSOFTPLG_IPC4_WIDGET W)
{
    SIZE_T Tail = strlen(W->Name) - 8;

    CHECK(Tail > 0 && Tail < SOFTPLG_TEXT_BYTES);
    if (strcmp(W->Name, "alh-copier.SDW0-Playback.0") == 0)
        return 16;
    if (strcmp(W->Name, "alh-copier.SDW0-Capture.0") == 0)
        return 17;
    if (strcmp(W->DaiType, "HDA") == 0)
        return strcmp(W->Name + Tail, "playback") == 0 ? 8 : 9;
    if (strcmp(W->DaiType, "DMIC") == 0)
        return 11;
    return W->Kind == SoftplgKindHostCopierPlayback ? 0 : 1;
}

/* Every module widget of a shipped file: its kind, its module id, the firmware's name, its copier's node type, and no format the controller cannot carry */
static
void
check_model_widgets(
    const PSOFTPLG_IPC4_MODEL Model,
    UINT32 Modules,
    UINT32 Copiers,
    UINT32 Pins)
{
    PSOFTPLG_IPC4_WIDGET W;
    PSOFTPLG_IPC4_FORMAT F;
    SIZE_T K, Head, Length;
    UINT32 N, I, Found, Count = 0, CopierCount = 0, PinCount = 0, ModulesSeen = 0;
    BOOLEAN IsCopier;

    for (N = 0; N < Model->WidgetCount; N++)
    {
        W = &Model->Widgets[N];
        if (W->Kind == SoftplgKindNone)
        {
            PinCount++;
            CHECK(!W->HasUuid);
            CHECK_EQ(W->Pipeline, SOFTPLG_NO_PIPELINE);
            CHECK_EQ(W->ModuleId, SOFTPLG_MODULE_NONE);
            continue;
        }
        if (W->Kind == SoftplgKindScheduler)
        {
            CHECK(!W->HasUuid);
            CHECK_EQ(W->Config.Count, 0);
            CHECK(W->Pipeline < Model->PipelineCount);
            CHECK_EQ(Model->Pipelines[W->Pipeline].Scheduler, N);
            continue;
        }
        IsCopier = W->Kind != SoftplgKindProcessor;
        Head = strlen(W->Name);
        for (K = 0; K < Head && W->Name[K] != '.'; K++)
            ;
        Head = K;
        Found = 0;
        for (I = 0; I < sizeof(ModelKinds) / sizeof(ModelKinds[0]); I++)
        {
            Length = strlen(ModelKinds[I].Prefix);
            if (Head < Length || strncmp(W->Name + Head - Length, ModelKinds[I].Prefix, Length) != 0)
                continue;
            CHECK(W->HasUuid);
            CHECK_EQ(W->ModuleId, ModelKinds[I].Id);
            CHECK(memcmp(ModelModules[W->ModuleId].Uuid, W->Uuid, SOFTPLG_UUID_BYTES) == 0);
            CHECK_STR(ModelModules[W->ModuleId].Name, ModelKinds[I].Name);
            Found++;
        }
        CHECK_EQ(Found, 1);
        ModulesSeen++;
        CHECK(W->InputFormats != 0 || W->OutputFormats != 0);
        CHECK_EQ(W->Config.Count, IsCopier ? SOFTPLG_IPC4_COPIER_CONFIG_WORDS :
                          SoftplgModelIsGain(W) ? SOFTPLG_IPC4_BASE_CONFIG_WORDS + SOFTPLG_IPC4_GAIN_CONFIG_WORDS : SOFTPLG_IPC4_BASE_CONFIG_WORDS);
        for (I = 0; I < W->InputFormats + W->OutputFormats; I++)
        {
            F = &Model->Formats[W->FirstFormat + I];
            CHECK(F->Words[0] == 48000);
            CHECK(F->Words[1] == 16 || F->Words[1] == 32);
            CHECK(SOFTPLG_FORMAT_CHANNELS_OF(F->Words[5]) == 2 || SOFTPLG_FORMAT_CHANNELS_OF(F->Words[5]) == 4);
            CHECK(F->Words[2] == 0xFFFFFF10u || F->Words[2] == 0xFFFF3210u);
            CHECK(F->BufferBytes != 0);
            CHECK(F->Expressible);
            CHECK_EQ(F->PinIndex, 0);
        }
        CHECK(W->Pipeline < Model->PipelineCount);
        if (!IsCopier)
            continue;
        CopierCount++;
        if (W->Kind == SoftplgKindHostCopierPlayback || W->Kind == SoftplgKindHostCopierCapture)
        {
            CHECK(W->HasNodeType);
            CHECK_EQ(W->NodeType, W->Kind == SoftplgKindHostCopierPlayback ? 0 : 1);
            CHECK(W->Pcm != SOFTPLG_NO_PCM);
            CHECK_EQ(W->Config.Words[SOFTPLG_COPIER_NODE_ID_WORD] & ~0xFFu, W->NodeType << 8);
            CHECK_EQ(W->Config.Words[SOFTPLG_COPIER_NODE_ID_WORD] & 0xFF, 0xFF);
            CHECK_EQ(W->Config.Words[SOFTPLG_COPIER_CONFIG_LENGTH_WORD], 1);
        }
        else if (W->Kind == SoftplgKindDaiCopierPlayback || W->Kind == SoftplgKindDaiCopierCapture)
            CHECK_EQ(W->NodeType, ModelCopierNodeType(W));
        else
            CHECK_EQ(W->Config.Words[SOFTPLG_COPIER_NODE_ID_WORD], 0xFFFFFFFF);
        CHECK(W->DaiType[0] == 0 || strcmp(W->DaiType, "HDA") == 0 || strcmp(W->DaiType, "DMIC") == 0 || strcmp(W->DaiType, "ALH") == 0);
        Count++;
    }
    CHECK_EQ(Count, Copiers);
    CHECK_EQ(CopierCount, Copiers);
    CHECK_EQ(PinCount, Pins);
    CHECK_EQ(ModulesSeen, Modules);
}

/* One PCM of the shipped files: its copiers, the widgets its stream reaches and the pipelines they lie in */
typedef struct _MODEL_PCM_FACTS
{
    const char* Name;
    UINT32 Id;
    const char* PlaybackCopier;
    const char* CaptureCopier;
    UINT32 Reached;
    UINT32 Pipelines[SOFTPLG_PIPELINE_MASK_WORDS * 32];
    UINT32 PipelineCount;
} MODEL_PCM_FACTS;

static
void
check_model_pcms(
    const PSOFTPLG_IPC4_MODEL Model,
    const MODEL_PCM_FACTS* Facts,
    UINT32 Count)
{
    SOFTPLG_IPC4_LIMITS Limits;
    SOFTPLG_IPC4_PROBLEM Problem;
    PSOFTPLG_IPC4_PCM Pcm;
    UINT32 P, N, I, Word, Bit, Reached, Pipelines;
    UINT32 Mask[SOFTPLG_PIPELINE_MASK_WORDS];
    BOOLEAN On;

    Limits.MaxPipelines = 256;
    Limits.MailboxBytes = 4096;
    CHECK_EQ(Model->PcmCount, Count);
    for (P = 0; P < Count && P < Model->PcmCount; P++)
    {
        Pcm = &Model->Pcms[P];
        CHECK_STR(Pcm->Name, Facts[P].Name);
        CHECK_STR(Pcm->DaiName, Facts[P].Name);
        CHECK_EQ(Pcm->PcmId, Facts[P].Id);
        CHECK_EQ(Pcm->DaiId, Facts[P].Id);
        CHECK_EQ(Pcm->Playback, Facts[P].PlaybackCopier != NULL);
        CHECK_EQ(Pcm->Capture, Facts[P].CaptureCopier != NULL);
        CHECK_EQ(Pcm->PlaybackCopier, Facts[P].PlaybackCopier != NULL ? ModelWidget(Model, Facts[P].PlaybackCopier) : SOFTPLG_NO_WIDGET);
        CHECK_EQ(Pcm->CaptureCopier, Facts[P].CaptureCopier != NULL ? ModelWidget(Model, Facts[P].CaptureCopier) : SOFTPLG_NO_WIDGET);
        if (Pcm->PlaybackCopier != SOFTPLG_NO_WIDGET)
        {
            CHECK_STR(Pcm->PlaybackStream, Model->Widgets[Pcm->PlaybackCopier].SName);
            CHECK_EQ(Model->Widgets[Pcm->PlaybackCopier].Pcm, P);
        }
        if (Pcm->CaptureCopier != SOFTPLG_NO_WIDGET)
        {
            CHECK_STR(Pcm->CaptureStream, Model->Widgets[Pcm->CaptureCopier].SName);
            CHECK_EQ(Model->Widgets[Pcm->CaptureCopier].Pcm, P);
        }
        /* the widgets the stream reaches, by the model's own claim and by the facts */
        Reached = 0;
        memset(Mask, 0, sizeof(Mask));
        Pipelines = 0;
        for (N = 0; N < Model->WidgetCount; N++)
        {
            On = SoftplgModelReachable(Model, P, N);
            Reached += On ? 1 : 0;
            if (!On)
                continue;
            /* the copiers of other PCMs are never reached */
            if (Model->Widgets[N].Kind == SoftplgKindHostCopierPlayback || Model->Widgets[N].Kind == SoftplgKindHostCopierCapture)
                CHECK(N == Pcm->PlaybackCopier || N == Pcm->CaptureCopier);
            Word = Model->Widgets[N].Pipeline / 32;
            Bit = Model->Widgets[N].Pipeline % 32;
            if (Word < SOFTPLG_PIPELINE_MASK_WORDS)
                Mask[Word] |= 1u << Bit;
        }
        CHECK_EQ(Reached, Facts[P].Reached);
        CHECK_EQ(Pcm->Reached, Facts[P].Reached);
        /* every pipeline the facts name has its bit, and no other has */
        for (I = 0; I < Facts[P].PipelineCount; I++)
        {
            for (N = 0; N < Model->PipelineCount && Model->Pipelines[N].Id != Facts[P].Pipelines[I]; N++)
                ;
            CHECK(N < Model->PipelineCount);
            if (N >= Model->PipelineCount)
                continue;
            CHECK(Pcm->PipelineMask[N / 32] & (1u << (N % 32)));
        }
        for (Word = 0; Word < SOFTPLG_PIPELINE_MASK_WORDS; Word++)
            for (Bit = 0; Bit < 32; Bit++)
                if (Pcm->PipelineMask[Word] & (1u << Bit))
                {
                    Pipelines++;
                    CHECK(Mask[Word] & (1u << Bit));
                }
        CHECK_EQ(Pipelines, Facts[P].PipelineCount);
    }
    /* every host copier of the file serves a PCM of the file */
    for (N = 0; N < Model->WidgetCount; N++)
        if (Model->Widgets[N].Kind == SoftplgKindHostCopierPlayback || Model->Widgets[N].Kind == SoftplgKindHostCopierCapture)
            CHECK(Model->Widgets[N].Pcm != SOFTPLG_NO_PCM);
    CHECK_EQ(SoftplgModelValidate(Model, &Limits, &Problem), Problem.Status);
}

/*
 * sof-hda-generic-ace1-2ch.tplg: every widget kind in one file. The copier
 * that needs gateway data is the first DAI copier, which is where the check
 * stops the file for P3.
 */
static
void
check_model_ace(void)
{
    static const MODEL_PCM_FACTS Pcms[] =
    {
        { "HDA Analog", 0, "host-copier.0.playback", "host-copier.0.capture", 13, { 1, 2, 3, 4 }, 4 },
        { "Deepbuffer HDA Analog", 31, "host-copier.31.playback", NULL, 9, { 2, 15 }, 2 },
        { "DMIC Raw", 6, NULL, "host-copier.6.capture", 7, { 11, 12 }, 2 },
        { "HDMI1", 3, "host-copier.3.playback", NULL, 2, { 50, 51 }, 2 },
        { "HDMI2", 4, "host-copier.4.playback", NULL, 2, { 60, 61 }, 2 },
        { "HDMI3", 5, "host-copier.5.playback", NULL, 2, { 70, 71 }, 2 }
    };
    static const UINT32 PipelineIds[] = { 1, 2, 3, 4, 11, 12, 15, 50, 51, 60, 61, 70, 71 };
    SOFTPLG_IPC4_MANIFEST Manifest;
    SOFTPLG_BLOCK Block;
    PSOFTPLG_IPC4_MODEL Model;
    PSOFTPLG_IPC4_WIDGET W;
    UINT8 *Data, *Nhlt;
    SIZE_T Size, Bytes;
    UINT32 N;
    const char* Named;

    Data = LoadExact(SOFTPLG_FIXTURE_DIR "/sof-hda-generic-ace1-2ch.tplg", &Size);
    CHECK(Data != NULL);
    if (Data == NULL)
        return;
    /* the manifest carries one item, the NHLT table the machine's BIOS does not */
    CHECK_EQ(SoftplgReadBlock(Data, Size, 0, &Block), SoftplgOk);
    CHECK_EQ(SoftplgIpc4ReadManifest(Data, Size, &Block, &Manifest), SoftplgModelOk);
    CHECK_EQ(Manifest.Major, 3);
    CHECK_EQ(Manifest.Minor, 29);
    CHECK_EQ(Manifest.Patch, 1);
    CHECK_EQ(Manifest.ItemCount, 1);
    CHECK_EQ(Manifest.Nhlt.Size, 6211);
    Nhlt = Data + Manifest.Nhlt.Offset;
    CHECK(Nhlt[0] == 'N' && Nhlt[1] == 'H' && Nhlt[2] == 'L' && Nhlt[3] == 'T');
    CHECK_EQ(Nhlt[4] | (Nhlt[5] << 8) | (Nhlt[6] << 16) | ((UINT32)Nhlt[7] << 24), 6211);

    Model = ModelBuild(Data, Size, &Bytes);
    CHECK(Model != NULL);
    if (Model == NULL)
        goto Out;
    CHECK_EQ(Model->ManifestItems, 1);
    CHECK_EQ(Model->ControlCount, 16);
    CHECK_EQ(Model->WidgetCount, 60);
    CHECK_EQ(Model->EdgeCount, 23);
    CHECK_EQ(Model->PcmCount, 6);
    CHECK_EQ(Model->PipelineCount, 13);
    CHECK_EQ(Model->FormatCount, 72);
    CHECK_EQ(Model->ReachWords, 2);
    CHECK_EQ(Model->PcmCount, sizeof(Pcms) / sizeof(Pcms[0]));
    for (N = 0; N < sizeof(PipelineIds) / sizeof(PipelineIds[0]); N++)
        CHECK_EQ(Model->Pipelines[N].Id, PipelineIds[N]);

    check_model_widgets(Model, 29, 15, 18);
    check_model_pcms(Model, Pcms, sizeof(Pcms) / sizeof(Pcms[0]));

    /* the edges the walker test names, as widget indices */
    CHECK_EQ(Model->Edges[0].Source, ModelWidget(Model, "drc.2.1"));
    CHECK_EQ(Model->Edges[0].Sink, ModelWidget(Model, "dai-copier.HDA.Analog.playback"));
    CHECK_EQ(Model->Edges[3].Source, ModelWidget(Model, "host-copier.0.playback"));
    CHECK_EQ(Model->Edges[3].Sink, ModelWidget(Model, "gain.1.1"));
    CHECK_EQ(Model->Edges[7].Source, ModelWidget(Model, "dai-copier.DMIC.dmic01.capture"));
    CHECK_EQ(Model->Edges[7].Sink, ModelWidget(Model, "gain.12.1"));
    CHECK_EQ(Model->Edges[11].Source, ModelWidget(Model, "host-copier.3.playback"));
    CHECK_EQ(Model->Edges[11].Sink, ModelWidget(Model, "dai-copier.HDA.iDisp1.playback"));

    /* the analog capture path runs through a module copier into another pipeline */
    W = &Model->Widgets[ModelWidget(Model, "module-copier.4.2")];
    CHECK_EQ(W->Kind, SoftplgKindModuleCopier);
    CHECK_EQ(W->ModuleId, 5);
    CHECK(!W->HasNodeType);
    CHECK_EQ(W->PipelineId, 4);
    CHECK_STR(W->SName, "Analog");
    CHECK_EQ(W->Config.Words[SOFTPLG_COPIER_NODE_ID_WORD], 0xFFFFFFFF);
    CHECK_EQ(W->Config.Words[SOFTPLG_COPIER_DMA_BUFFER_WORD], 0);
    W = &Model->Widgets[ModelWidget(Model, "dai-copier.DMIC.dmic01.capture")];
    CHECK_EQ(W->Kind, SoftplgKindDaiCopierCapture);
    CHECK_STR(W->DaiType, "DMIC");
    CHECK_EQ(W->DaiIndex, 0);
    CHECK_EQ(W->NodeType, 11);
    CHECK_EQ(W->Pcm, SOFTPLG_NO_PCM);
    W = &Model->Widgets[ModelWidget(Model, "codec0_in")];
    CHECK_EQ(W->Kind, SoftplgKindNone);
    CHECK_EQ(W->Type, 0);
    W = &Model->Widgets[ModelWidget(Model, "Analog CPU Playback")];
    CHECK_EQ(W->Kind, SoftplgKindNone);
    CHECK_EQ(W->Type, 5);
    W = &Model->Widgets[ModelWidget(Model, "pipeline.1")];
    CHECK_EQ(W->Priority, 0);
    CHECK_EQ(W->SchedCoreId, 0);
    CHECK_EQ(W->LpMode, 0);

    /* the first DAI copier needs gateway data */
    CHECK_EQ(ModelValidate(Model, 16, 4096, &Named), SoftplgModelGatewayData);
    CHECK_STR(Named, "dai-copier.HDA.Analog.playback");
    CHECK_EQ(ModelValidate(Model, 13, 4096, &Named), SoftplgModelTooManyPipelines);
    CHECK(Named == NULL);
    CHECK_EQ(ModelValidate(Model, 14, 4096, &Named), SoftplgModelGatewayData);

Out:
    free(Model);
    free(Data);
}

static
void
check_model_dmic(void)
{
    static const MODEL_PCM_FACTS Pcms[] =
    {
        { "DMIC Raw", 10, NULL, "host-copier.10.capture", 7, { 100, 101 }, 2 }
    };
    PSOFTPLG_IPC4_MODEL Model;
    PSOFTPLG_IPC4_WIDGET W;
    UINT8* Data;
    SIZE_T Size, Bytes;
    const char* Named;

    Data = LoadExact(SOFTPLG_FIXTURE_DIR "/sof-arl-dmic-4ch-id2.tplg", &Size);
    CHECK(Data != NULL);
    if (Data == NULL)
        return;
    Model = ModelBuild(Data, Size, &Bytes);
    CHECK(Model != NULL);
    if (Model == NULL)
        goto Out;
    CHECK_EQ(Model->AbiMinor, 29);
    CHECK_EQ(Model->ManifestItems, 1);
    CHECK_EQ(Model->ControlCount, 8);
    CHECK_EQ(Model->WidgetCount, 9);
    CHECK_EQ(Model->EdgeCount, 6);
    CHECK_EQ(Model->PcmCount, 1);
    CHECK_EQ(Model->PipelineCount, 2);
    CHECK_EQ(Model->FormatCount, 16);
    CHECK_EQ(Model->Pipelines[0].Id, 100);
    CHECK_EQ(Model->Pipelines[1].Id, 101);
    check_model_widgets(Model, 7, 3, 0);
    check_model_pcms(Model, Pcms, 1);
    W = &Model->Widgets[ModelWidget(Model, "host-copier.10.capture")];
    CHECK_EQ(W->Kind, SoftplgKindHostCopierCapture);
    CHECK_STR(W->SName, "Gain Capture 13");
    CHECK_EQ(W->InputFormats, 1);
    CHECK_EQ(W->OutputFormats, 1);
    CHECK_EQ(SOFTPLG_FORMAT_CHANNELS_OF(Model->Formats[W->FirstFormat].Words[5]), 4);
    CHECK(SoftplgModelReachable(Model, 0, ModelWidget(Model, "dai-copier.DMIC.dmic01.capture")));
    CHECK(!SoftplgModelReachable(Model, 0, ModelWidget(Model, "pipeline.101")));
    CHECK_EQ(ModelValidate(Model, 16, 4096, &Named), SoftplgModelGatewayData);
    CHECK_STR(Named, "dai-copier.DMIC.dmic01.capture");

Out:
    free(Model);
    free(Data);
}

static
void
check_model_rt711(void)
{
    static const MODEL_PCM_FACTS Pcms[] =
    {
        { "DMIC Raw", 10, NULL, "host-copier.10.capture", 7, { 100, 101 }, 2 },
        { "HDMI1", 5, "host-copier.5.playback", NULL, 2, { 50, 51 }, 2 },
        { "HDMI2", 6, "host-copier.6.playback", NULL, 2, { 60, 61 }, 2 },
        { "HDMI3", 7, "host-copier.7.playback", NULL, 2, { 70, 71 }, 2 },
        { "Jack Out", 0, "host-copier.0.playback", NULL, 7, { 0, 1 }, 2 },
        { "Jack In", 1, NULL, "host-copier.1.capture", 4, { 10, 11 }, 2 },
        { "Deepbuffer Jack Out", 31, "host-copier.31.playback", NULL, 7, { 1, 15 }, 2 }
    };
    PSOFTPLG_IPC4_MODEL Model;
    PSOFTPLG_IPC4_WIDGET W;
    UINT32 P, Ordinal;
    UINT8* Data;
    SIZE_T Size, Bytes;
    const char* Named;

    Data = LoadExact(SOFTPLG_FIXTURE_DIR "/sof-arl-rt711-l0-2ch.tplg", &Size);
    CHECK(Data != NULL);
    if (Data == NULL)
        return;
    Model = ModelBuild(Data, Size, &Bytes);
    CHECK(Model != NULL);
    if (Model == NULL)
        goto Out;
    CHECK_EQ(Model->ManifestItems, 1);
    CHECK_EQ(Model->ControlCount, 12);
    CHECK_EQ(Model->WidgetCount, 40);
    CHECK_EQ(Model->EdgeCount, 21);
    CHECK_EQ(Model->PcmCount, 7);
    CHECK_EQ(Model->PipelineCount, 13);
    CHECK_EQ(Model->FormatCount, 70);
    check_model_widgets(Model, 27, 16, 0);
    check_model_pcms(Model, Pcms, 7);

    /* the jack path crosses from pipeline 0 through a module copier into pipeline 1 */
    P = ModelPcm(Model, "Jack Out");
    CHECK(SoftplgModelReachable(Model, P, ModelWidget(Model, "alh-copier.SDW0-Playback.0")));
    CHECK(SoftplgModelReachable(Model, P, ModelWidget(Model, "mixout.1.1")));
    /* the deep buffer path of the same output does not pull the other PCM's widgets in */
    P = ModelPcm(Model, "Deepbuffer Jack Out");
    CHECK(SoftplgModelReachable(Model, P, ModelWidget(Model, "mixin.15.1")));
    CHECK(!SoftplgModelReachable(Model, P, ModelWidget(Model, "host-copier.0.playback")));
    CHECK(!SoftplgModelReachable(Model, P, ModelWidget(Model, "gain.0.1")));
    P = ModelPcm(Model, "Jack Out");
    CHECK(!SoftplgModelReachable(Model, P, ModelWidget(Model, "mixin.15.1")));
    /* a pipeline with the ordinal 0 */
    Ordinal = Model->Widgets[ModelWidget(Model, "gain.0.1")].Pipeline;
    CHECK_EQ(Ordinal, 0);
    CHECK_EQ(Model->Pipelines[0].Id, 0);
    W = &Model->Widgets[ModelWidget(Model, "alh-copier.SDW0-Playback.0")];
    CHECK_EQ(W->Kind, SoftplgKindDaiCopierPlayback);
    CHECK_STR(W->DaiType, "ALH");
    CHECK_EQ(W->DaiIndex, 0);
    CHECK_EQ(W->NodeType, 16);
    W = &Model->Widgets[ModelWidget(Model, "alh-copier.SDW0-Capture.0")];
    CHECK_EQ(W->Kind, SoftplgKindDaiCopierCapture);
    CHECK_EQ(W->NodeType, 17);
    CHECK_EQ(ModelValidate(Model, 16, 4096, &Named), SoftplgModelGatewayData);
    CHECK_STR(Named, "alh-copier.SDW0-Playback.0");

Out:
    free(Model);
    free(Data);
}

/* The first place the bytes Needle occur in Data from From on, or Size */
static
SIZE_T
ModelFind(
    const UINT8* Data,
    SIZE_T Size,
    const UINT8* Needle,
    SIZE_T NeedleBytes,
    SIZE_T From)
{
    SIZE_T Index;

    for (Index = From; Index + NeedleBytes <= Size; Index++)
        if (memcmp(Data + Index, Needle, NeedleBytes) == 0)
            return Index;
    return Size;
}

/*
 * The malformed inputs of the plan: each one a copy of a real file with the
 * one field changed that makes it wrong, and the status the model answers.
 */
static
void
check_model_malformed(void)
{
    static const UINT8 GainUuid[SOFTPLG_UUID_BYTES] = { 0xA8, 0xA9, 0xBC, 0x61, 0xD0, 0x18, 0x18, 0x4A, 0x8E, 0x7B, 0x26, 0x39, 0x21, 0x98, 0x04, 0xB7 };
    static const UINT8 Rate48k[] = { 0x6C, 0x07, 0x00, 0x00, 0x80, 0xBB, 0x00, 0x00 };
    static const UINT8 Rate64k[] = { 0x6C, 0x07, 0x00, 0x00, 0x00, 0xFA, 0x00, 0x00 };
    static const UINT8 TwoChannels[] = { 0x6F, 0x07, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00 };
    static const UINT8 ThreeChannels[] = { 0x6F, 0x07, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00 };
    static const UINT8 NodeTypeCapture[] = { 0xBC, 0x07, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00 };
    static const UINT8 NodeTypePlayback[] = { 0xBC, 0x07, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
    /* the output rate token (1930) before the value */
    static const UINT8 OutRate48k[] = { 0x8A, 0x07, 0x00, 0x00, 0x80, 0xBB, 0x00, 0x00 };
    static const UINT8 OutRate64k[] = { 0x8A, 0x07, 0x00, 0x00, 0x00, 0xFA, 0x00, 0x00 };
    SOFTPLG_MODEL_INPUT Input;
    SOFTPLG_IPC4_MANIFEST Manifest;
    SOFTPLG_BLOCK Block;
    SOFTPLG_WIDGET Widget;
    PSOFTPLG_IPC4_MODEL Model;
    SOFTPLG_MANIFEST Container;
    UINT8 *Loop, *Ace, *Copy = NULL;
    SIZE_T LoopSize, AceSize, Bytes, At, Private, Offset;
    const char* Named;
    UINT32 K;

    Loop = LoadExact(SOFTPLG_FIXTURE_DIR "/sof-ipc4-loopback.tplg", &LoopSize);
    Ace = LoadExact(SOFTPLG_FIXTURE_DIR "/sof-hda-generic-ace1-2ch.tplg", &AceSize);
    CHECK(Loop != NULL && Ace != NULL);
    if (Loop == NULL || Ace == NULL)
        goto Out;
    ModelLoadFirmware();
    Copy = (UINT8*)malloc(LoopSize > AceSize ? LoopSize : AceSize);
    CHECK(Copy != NULL);
    if (Copy == NULL)
        goto Out;

    /* the ABI header: a major version this model does not read */
    memcpy(Copy, Loop, LoopSize);
    CHECK_EQ(SoftplgReadBlock(Copy, LoopSize, 0, &Block), SoftplgOk);
    CHECK_EQ(SoftplgReadManifest(Copy, LoopSize, &Block, &Container), SoftplgOk);
    Private = Container.Private.Offset;
    CHECK_EQ(Container.Private.Size, SOFTPLG_IPC4_MANIFEST_ABI_BYTES);
    Copy[Private] = 4;
    CHECK_EQ(SoftplgIpc4ReadManifest(Copy, LoopSize, &Block, &Manifest), SoftplgModelAbiMismatch);
    Model = ModelBuild(Copy, LoopSize, &Bytes);
    CHECK(Model == NULL);
    CHECK_EQ(ModelStatus, SoftplgModelAbiMismatch);
    free(Model);
    memcpy(Copy, Loop, LoopSize);
    Copy[Private + 2] = 28;
    CHECK_EQ(SoftplgIpc4ReadManifest(Copy, LoopSize, &Block, &Manifest), SoftplgModelAbiMismatch);
    /* the patch number is free */
    memcpy(Copy, Loop, LoopSize);
    Copy[Private + 4] = 9;
    CHECK_EQ(SoftplgIpc4ReadManifest(Copy, LoopSize, &Block, &Manifest), SoftplgModelOk);
    CHECK_EQ(Manifest.Patch, 9);
    /* a manifest without its whole ABI header */
    Copy[Private + 4] = 1;
    Put32(Copy + Private - 4, 5);
    CHECK_EQ(SoftplgIpc4ReadManifest(Copy, LoopSize, &Block, &Manifest), SoftplgModelManifestTruncated);

    /* a manifest item whose size runs past the end of the manifest's private data */
    memcpy(Copy, Ace, AceSize);
    CHECK_EQ(SoftplgReadBlock(Copy, AceSize, 0, &Block), SoftplgOk);
    CHECK_EQ(SoftplgReadManifest(Copy, AceSize, &Block, &Container), SoftplgOk);
    Private = Container.Private.Offset;
    Put32(Copy + Private + SOFTPLG_IPC4_MANIFEST_COUNT_OFFSET + sizeof(UINT16) + sizeof(UINT32), 7000);
    CHECK_EQ(SoftplgIpc4ReadManifest(Copy, AceSize, &Block, &Manifest), SoftplgModelManifestTruncated);
    Model = ModelBuild(Copy, AceSize, &Bytes);
    CHECK(Model == NULL);
    CHECK_EQ(ModelStatus, SoftplgModelManifestTruncated);
    free(Model);
    /* an item count that names more items than the data holds */
    memcpy(Copy, Ace, AceSize);
    Put16(Copy + Private + SOFTPLG_IPC4_MANIFEST_COUNT_OFFSET, 2);
    CHECK_EQ(SoftplgIpc4ReadManifest(Copy, AceSize, &Block, &Manifest), SoftplgModelManifestTruncated);

    /* a widget of a type the model does not read */
    memcpy(Copy, Loop, LoopSize);
    At = ModelFind(Copy, LoopSize, (const UINT8*)"gain.1.1", 8, 0);
    CHECK(At >= 4);
    Put32(Copy + At - 4, 2);
    Model = ModelBuild(Copy, LoopSize, &Bytes);
    CHECK(Model == NULL);
    CHECK_EQ(ModelStatus, SoftplgModelUnknownWidget);
    CHECK_EQ(ModelProblem.Status, SoftplgModelUnknownWidget);
    CHECK_EQ(ModelProblem.Widget, 2);
    free(Model);

    /* a UUID the firmware image's module table does not have */
    memcpy(Copy, Loop, LoopSize);
    At = ModelFind(Copy, LoopSize, GainUuid, SOFTPLG_UUID_BYTES, 0);
    CHECK(At < LoopSize);
    Copy[At] ^= 1;
    Model = ModelBuild(Copy, LoopSize, &Bytes);
    CHECK(Model == NULL);
    CHECK_EQ(ModelStatus, SoftplgModelModuleMissing);
    CHECK_EQ(ModelProblem.Status, SoftplgModelModuleMissing);
    CHECK_EQ(ModelProblem.Widget, 2);
    free(Model);
    /* with no module table at all, every module is missing */
    memcpy(Copy, Loop, LoopSize);
    memset(&Input, 0, sizeof(Input));
    Input.Data = Copy;
    Input.Size = LoopSize;
    CHECK_EQ(SoftplgModelMeasure(&Input, &Bytes, &ModelProblem), SoftplgModelModuleMissing);
    CHECK_EQ(ModelProblem.Widget, 0);

    /* the UUID tuple coming as a word: the array's type field is twelve bytes before the UUID */
    memcpy(Copy, Loop, LoopSize);
    At = ModelFind(Copy, LoopSize, GainUuid, SOFTPLG_UUID_BYTES, 0);
    CHECK(At >= 16);
    Put32(Copy + At - 16, 12 + 8);
    Put32(Copy + At - 12, SOFTPLG_TUPLE_WORD);
    Model = ModelBuild(Copy, LoopSize, &Bytes);
    CHECK(Model == NULL);
    CHECK_EQ(ModelStatus, SoftplgModelTupleType);
    free(Model);

    /* every rate of the playback copier one the controller cannot carry */
    memcpy(Copy, Loop, LoopSize);
    CHECK_EQ(SoftplgReadBlock(Copy, LoopSize, 1, &Block), SoftplgOk);
    CHECK_EQ(SoftplgReadWidget(Copy, LoopSize, &Block, 0, &Widget), SoftplgOk);
    for (K = 0, Offset = Widget.Private.Offset; K < 3; K++)
    {
        At = ModelFind(Copy, Widget.Private.Offset + Widget.Private.Size, Rate48k, sizeof(Rate48k), Offset);
        CHECK(At < Widget.Private.Offset + Widget.Private.Size);
        memcpy(Copy + At, Rate64k, sizeof(Rate64k));
        Offset = At + sizeof(Rate64k);
    }
    At = ModelFind(Copy, Widget.Private.Offset + Widget.Private.Size, OutRate48k, sizeof(OutRate48k), Widget.Private.Offset);
    CHECK(At < Widget.Private.Offset + Widget.Private.Size);
    memcpy(Copy + At, OutRate64k, sizeof(OutRate64k));
    Model = ModelBuild(Copy, LoopSize, &Bytes);
    CHECK(Model != NULL);
    if (Model != NULL)
    {
        CHECK(Model->Widgets[0].Pcm != SOFTPLG_NO_PCM);
        for (K = 0; K < 3; K++)
            CHECK(!Model->Formats[K].Expressible);
        /* the copier's output slot was patched with it */
        CHECK(!Model->Formats[3].Expressible);
        CHECK(Model->Formats[8].Expressible);
        CHECK_EQ(ModelValidate(Model, 16, 4096, &Named), SoftplgModelFormatInexpressible);
        CHECK_STR(Named, "host-copier.0.playback");
        free(Model);
    }

    /* the channels token disagreeing with the configuration word */
    memcpy(Copy, Loop, LoopSize);
    At = ModelFind(Copy, LoopSize, TwoChannels, sizeof(TwoChannels), Widget.Private.Offset);
    CHECK(At < Widget.Private.Offset + Widget.Private.Size);
    memcpy(Copy + At, ThreeChannels, sizeof(ThreeChannels));
    Model = ModelBuild(Copy, LoopSize, &Bytes);
    CHECK(Model == NULL);
    CHECK_EQ(ModelStatus, SoftplgModelFormatMismatch);
    free(Model);

    /* a host copier of the other gateway type */
    memcpy(Copy, Loop, LoopSize);
    CHECK_EQ(SoftplgReadWidget(Copy, LoopSize, &Block, 1, &Widget), SoftplgOk);
    At = ModelFind(Copy, LoopSize, NodeTypeCapture, sizeof(NodeTypeCapture), Widget.Private.Offset);
    CHECK(At < Widget.Private.Offset + Widget.Private.Size);
    memcpy(Copy + At, NodeTypePlayback, sizeof(NodeTypePlayback));
    Model = ModelBuild(Copy, LoopSize, &Bytes);
    CHECK(Model != NULL);
    if (Model != NULL)
    {
        CHECK_EQ(ModelValidate(Model, 16, 4096, &Named), SoftplgModelNodeType);
        CHECK_STR(Named, "host-copier.0.capture");
        free(Model);
    }

    /* a graph element that names no widget */
    memcpy(Copy, Loop, LoopSize);
    At = ModelFind(Copy, LoopSize, (const UINT8*)"gain.1.1", 8, LoopSize / 2);
    CHECK(At < LoopSize);
    Copy[At + 7] = '2';
    Model = ModelBuild(Copy, LoopSize, &Bytes);
    CHECK(Model == NULL);
    CHECK_EQ(ModelStatus, SoftplgModelBadEdge);
    free(Model);

    /* not a topology at all, and a cut one */
    memcpy(Copy, Loop, LoopSize);
    Copy[0] = 'X';
    CHECK_EQ(ModelBuild(Copy, LoopSize, &Bytes) == NULL, 1);
    CHECK_EQ(ModelStatus, SoftplgModelBadFile);
    CHECK_EQ(ModelBuild(Copy, 100, &Bytes) == NULL, 1);
    CHECK_EQ(ModelProblem.Widget, SOFTPLG_NO_WIDGET);
    CHECK_EQ(ModelStatus, SoftplgModelBadFile);

Out:
    free(Copy);
    free(Ace);
    free(Loop);
}

/*
 * Topologies built for the model's bounds, in the container's own layouts
 * (softplg.c): the manifest; one widget block per pipeline, the block index
 * its pipeline id, each holding its scheduler and the last one a host
 * playback copier; one PCM the copier's stream serves; a graph of one edge;
 * then as many empty blocks of a type the model does not read as asked.
 */
#define GEN_WIDGET_BYTES        132
#define GEN_MANIFEST_BYTES      112
#define GEN_PCM_BYTES           912
#define GEN_PCM_CAPS_OFFSET     692
#define GEN_GRAPH_BYTES         132
#define GEN_FIRST_BUFFER_BYTES  384

/* the copier module of sof-arl-s.ri (AceModules) */
static const UINT8 GenCopierUuid[SOFTPLG_UUID_BYTES] = { 0x83, 0x0C, 0xA0, 0x9B, 0x12, 0xCA, 0x83, 0x4A, 0x94, 0x3C, 0x1F, 0xA2, 0xE8, 0x2F, 0x9D, 0xDA };

typedef struct _GEN_SHAPE
{
    UINT32 Pipelines;
    BOOLEAN Copier;
    /* the copier's input formats: every one BufferBytes; the first takes FirstBufferBytes instead when it is not zero, else 384 */
    UINT32 Formats;
    UINT32 BufferBytes;
    UINT32 FirstBufferBytes;
    /* the copier's deep-buffer token, 0 for none */
    UINT32 DeepBufferMs;
    /* the copier is a host capture copier (aif_out): its Formats are output formats and its stream the PCM's capture one; else a host playback copier (aif_in) */
    BOOLEAN Capture;
    BOOLEAN Graph;
    /* with Graph: a chain, the copier into the last pipeline's scheduler and every scheduler into the one of the pipeline before, the order that made a pass per widget */
    BOOLEAN Chain;
    UINT32 EmptyBlocks;
} GEN_SHAPE;

typedef struct _GEN_TOPOLOGY
{
    UINT8* Data;
    SIZE_T Bytes;
    /* the widget index of the copier, SOFTPLG_NO_WIDGET when there is none */
    UINT32 Copier;
} GEN_TOPOLOGY;

static
void
GenFree(
    GEN_TOPOLOGY* Topology)
{
    free(Topology->Data);
    Topology->Data = NULL;
}

static
SIZE_T
GenText(
    UINT8* At,
    const char* Text)
{
    memset(At, 0, SOFTPLG_NAME_BYTES);
    memcpy(At, Text, strlen(Text));
    return SOFTPLG_NAME_BYTES;
}

static
SIZE_T
GenHeader(
    UINT8* At,
    UINT32 Type,
    UINT32 Index,
    UINT32 Count,
    UINT32 PayloadBytes)
{
    memset(At, 0, SOFTPLG_HEADER_BYTES);
    Put32(At, SOFTPLG_MAGIC);
    Put32(At + 4, SOFTPLG_ABI_VERSION);
    Put32(At + 12, Type);
    Put32(At + 16, SOFTPLG_HEADER_BYTES);
    Put32(At + 24, PayloadBytes);
    Put32(At + 28, Index);
    Put32(At + 32, Count);
    return SOFTPLG_HEADER_BYTES;
}

/* One word tuple */
static
SIZE_T
GenWord(
    UINT8* At,
    UINT32 Token,
    UINT32 Value)
{
    Put32(At, Token);
    Put32(At + 4, Value);
    return 8;
}

/* The words of the copier's private data: the UUID array, then the word array (the formats' tokens, the gateway type, the deep buffer) */
static
SIZE_T
GenCopierWords(
    const GEN_SHAPE* Shape)
{
    return 1 + 2 + (Shape->DeepBufferMs != 0 ? 1 : 0) + Shape->Formats * 10;
}

/* The manifest with the ABI header alone */
static
SIZE_T
GenManifest(
    UINT8* At)
{
    memset(At, 0, GEN_MANIFEST_BYTES + SOFTPLG_IPC4_MANIFEST_ABI_BYTES);
    Put32(At, GEN_MANIFEST_BYTES);
    Put32(At + 108, SOFTPLG_IPC4_MANIFEST_ABI_BYTES);
    Put16(At + GEN_MANIFEST_BYTES, SOFTPLG_IPC4_ABI_MAJOR);
    Put16(At + GEN_MANIFEST_BYTES + 2, SOFTPLG_IPC4_ABI_MINOR);
    return GEN_MANIFEST_BYTES + SOFTPLG_IPC4_MANIFEST_ABI_BYTES;
}

static
SIZE_T
GenScheduler(
    UINT8* At,
    UINT32 Pipeline)
{
    char Name[SOFTPLG_NAME_BYTES];

    memset(At, 0, GEN_WIDGET_BYTES);
    Put32(At, GEN_WIDGET_BYTES);
    Put32(At + 4, SOFTPLG_WIDGET_SCHEDULER);
    snprintf(Name, sizeof(Name), "sched.%u", Pipeline);
    GenText(At + 8, Name);
    return GEN_WIDGET_BYTES;
}

/* The host copier: one format of 48 kHz, 32 valid bits in 32, two channels, interleaved, per format asked; input formats of a playback copier (aif_in), output formats of a capture one (aif_out, Shape->Capture) */
static
SIZE_T
GenCopier(
    UINT8* At,
    const GEN_SHAPE* Shape)
{
    SIZE_T Words = 12 + 8 * GenCopierWords(Shape);
    UINT32 Base = Shape->Capture ? SOFTPLG_TOKEN_FORMAT_OUT_BASE : SOFTPLG_TOKEN_FORMAT_IN_BASE;
    UINT8* Tuple;
    UINT32 F;

    memset(At, 0, GEN_WIDGET_BYTES + 32 + Words);
    Put32(At, GEN_WIDGET_BYTES);
    Put32(At + 4, Shape->Capture ? SOFTPLG_WIDGET_AIF_OUT : SOFTPLG_WIDGET_AIF_IN);
    GenText(At + 8, Shape->Capture ? "host-copier.0.capture" : "host-copier.0.playback");
    GenText(At + 52, "copier-stream");
    Put32(At + 128, (UINT32)(32 + Words));
    /* the UUID array, then the word array */
    Put32(At + GEN_WIDGET_BYTES, 32);
    Put32(At + GEN_WIDGET_BYTES + 4, SOFTPLG_TUPLE_UUID);
    Put32(At + GEN_WIDGET_BYTES + 8, 1);
    Put32(At + GEN_WIDGET_BYTES + 12, SOFTPLG_TOKEN_COMP_UUID);
    memcpy(At + GEN_WIDGET_BYTES + 16, GenCopierUuid, SOFTPLG_UUID_BYTES);
    Tuple = At + GEN_WIDGET_BYTES + 32;
    Put32(Tuple, (UINT32)Words);
    Put32(Tuple + 4, SOFTPLG_TUPLE_WORD);
    Put32(Tuple + 8, GenCopierWords(Shape));
    Tuple += 12;
    Tuple += GenWord(Tuple, SOFTPLG_TOKEN_COMP_NUM_INPUT_AUDIO_FORMATS, Shape->Capture ? 0 : Shape->Formats);
    Tuple += GenWord(Tuple, SOFTPLG_TOKEN_COMP_NUM_OUTPUT_AUDIO_FORMATS, Shape->Capture ? Shape->Formats : 0);
    Tuple += GenWord(Tuple, SOFTPLG_TOKEN_COPIER_NODE_TYPE, Shape->Capture ? SOFTPLG_NODE_HOST_INPUT : SOFTPLG_NODE_HOST_OUTPUT);
    if (Shape->DeepBufferMs != 0)
        Tuple += GenWord(Tuple, SOFTPLG_TOKEN_COPIER_DEEP_BUFFER_DMA_MS, Shape->DeepBufferMs);
    for (F = 0; F < Shape->Formats; F++)
    {
        Tuple += GenWord(Tuple, Base + SOFTPLG_FORMAT_RATE, 48000);
        Tuple += GenWord(Tuple, Base + SOFTPLG_FORMAT_DEPTH, 32);
        Tuple += GenWord(Tuple, Base + SOFTPLG_FORMAT_VALID, 32);
        Tuple += GenWord(Tuple, Base + SOFTPLG_FORMAT_CHANNELS, 2);
        Tuple += GenWord(Tuple, Base + SOFTPLG_FORMAT_CH_MAP, 0xFFFFFF10u);
        Tuple += GenWord(Tuple, Base + SOFTPLG_FORMAT_CH_CFG, 1);
        Tuple += GenWord(Tuple, Base + SOFTPLG_FORMAT_INTERLEAVING, 0);
        Tuple += GenWord(Tuple, Base + SOFTPLG_FORMAT_CFG, 2 | (32 << 8));
        Tuple += GenWord(Tuple, Base + SOFTPLG_FORMAT_SAMPLE_TYPE, 0);
        /* the stream's buffer size: the input buffer size of a playback copier's format, the output one of a capture copier's */
        Tuple += GenWord(Tuple, Shape->Capture ? SOFTPLG_TOKEN_FORMAT_OUT_OBS : SOFTPLG_TOKEN_FORMAT_IN_IBS, F != 0 ? Shape->BufferBytes :
            Shape->FirstBufferBytes != 0 ? Shape->FirstBufferBytes : GEN_FIRST_BUFFER_BYTES);
    }
    return GEN_WIDGET_BYTES + 32 + Words;
}

/* The PCM whose stream is the copier's, the capture one for a capture copier; no other stream: the playback field at 100 and the capture field at 104 (softplg.c PCM_PLAYBACK_OFFSET, PCM_CAPTURE_OFFSET), then the stream count and the caps, a record of 104 bytes (CAPS_BYTES) a stream, the stream's name 4 bytes into it */
#define GEN_PCM_PLAYBACK_OFFSET  100
#define GEN_PCM_CAPTURE_OFFSET   104
#define GEN_CAPS_BYTES           104

static
SIZE_T
GenPcm(
    UINT8* At,
    const GEN_SHAPE* Shape)
{
    memset(At, 0, GEN_PCM_BYTES);
    Put32(At, GEN_PCM_BYTES);
    GenText(At + 4, "PCM0");
    GenText(At + 48, "NoCodec-0");
    Put32(At + (Shape->Capture ? GEN_PCM_CAPTURE_OFFSET : GEN_PCM_PLAYBACK_OFFSET), 1);
    GenText(At + GEN_PCM_CAPS_OFFSET + (Shape->Capture ? GEN_CAPS_BYTES : 0) + 4, "copier-stream");
    return GEN_PCM_BYTES;
}

/* The graph: one edge between the copier and the scheduler of its pipeline, the copier into the scheduler for a playback copier and the scheduler into the copier for a capture one (a link names its sink, then its source), or the chain of every scheduler */
static
SIZE_T
GenGraph(
    UINT8* At,
    const GEN_SHAPE* Shape)
{
    char Name[SOFTPLG_NAME_BYTES];
    UINT32 P;

    memset(At, 0, GEN_GRAPH_BYTES * (Shape->Chain ? Shape->Pipelines : 1));
    snprintf(Name, sizeof(Name), "sched.%u", Shape->Pipelines - 1);
    if (Shape->Capture)
    {
        GenText(At, "host-copier.0.capture");
        GenText(At + 2 * SOFTPLG_NAME_BYTES, Name);
    }
    else
    {
        GenText(At, Name);
        GenText(At + 2 * SOFTPLG_NAME_BYTES, "host-copier.0.playback");
    }
    At += GEN_GRAPH_BYTES;
    /* then every scheduler into the one of the pipeline before, from the far end */
    for (P = 1; P < Shape->Pipelines && Shape->Chain; P++)
    {
        snprintf(Name, sizeof(Name), "sched.%u", Shape->Pipelines - 1 - P);
        GenText(At, Name);
        snprintf(Name, sizeof(Name), "sched.%u", Shape->Pipelines - P);
        GenText(At + 2 * SOFTPLG_NAME_BYTES, Name);
        At += GEN_GRAPH_BYTES;
    }
    return GEN_GRAPH_BYTES * (Shape->Chain ? Shape->Pipelines : 1);
}

static
BOOLEAN
GenBuild(
    GEN_TOPOLOGY* Topology,
    const GEN_SHAPE* Shape)
{
    SIZE_T CopierBytes = Shape->Copier ? GEN_WIDGET_BYTES + 32 + 12 + 8 * GenCopierWords(Shape) : 0;
    SIZE_T Bytes = SOFTPLG_HEADER_BYTES + GEN_MANIFEST_BYTES + SOFTPLG_IPC4_MANIFEST_ABI_BYTES;
    UINT8* At;
    UINT32 P;

    for (P = 0; P < Shape->Pipelines; P++)
        Bytes += SOFTPLG_HEADER_BYTES + GEN_WIDGET_BYTES + (P == Shape->Pipelines - 1 ? CopierBytes : 0);
    Bytes += SOFTPLG_HEADER_BYTES + GEN_PCM_BYTES;
    if (Shape->Graph)
        Bytes += SOFTPLG_HEADER_BYTES + GEN_GRAPH_BYTES * (Shape->Chain ? Shape->Pipelines : 1);
    Bytes += (SIZE_T)Shape->EmptyBlocks * SOFTPLG_HEADER_BYTES;
    memset(Topology, 0, sizeof(*Topology));
    Topology->Copier = SOFTPLG_NO_WIDGET;
    Topology->Data = (UINT8*)malloc(Bytes);
    CHECK(Topology->Data != NULL);
    if (Topology->Data == NULL)
        return FALSE;
    Topology->Bytes = Bytes;
    At = Topology->Data;
    At += GenHeader(At, SoftplgTypeManifest, 0, 1, GEN_MANIFEST_BYTES + SOFTPLG_IPC4_MANIFEST_ABI_BYTES);
    At += GenManifest(At);
    for (P = 0; P < Shape->Pipelines; P++)
    {
        At += GenHeader(At, SoftplgTypeWidget, P, P == Shape->Pipelines - 1 && Shape->Copier ? 2 : 1,
            GEN_WIDGET_BYTES + (P == Shape->Pipelines - 1 ? CopierBytes : 0));
        At += GenScheduler(At, P);
        if (P == Shape->Pipelines - 1 && Shape->Copier)
        {
            Topology->Copier = Shape->Pipelines;
            At += GenCopier(At, Shape);
        }
    }
    At += GenHeader(At, SoftplgTypePcm, 0, 1, GEN_PCM_BYTES);
    At += GenPcm(At, Shape);
    if (Shape->Graph)
    {
        At += GenHeader(At, SoftplgTypeGraph, 0, Shape->Chain ? Shape->Pipelines : 1, GEN_GRAPH_BYTES * (Shape->Chain ? Shape->Pipelines : 1));
        At += GenGraph(At, Shape);
    }
    for (P = 0; P < Shape->EmptyBlocks; P++)
        At += GenHeader(At, SoftplgTypePdata, 0, 0, 0);
    CHECK_EQ((SIZE_T)(At - Topology->Data), Bytes);
    return TRUE;
}

/* R2-01: a topology whose pipeline ordinals reach the 256 bits of a PCM's mask is refused before the mask is written, the scheduler that would be the 257th named; the measure pass still sizes it */
static
void
check_model_pipeline_bound(void)
{
    GEN_TOPOLOGY Topology;
    GEN_SHAPE Shape;
    PSOFTPLG_IPC4_MODEL Model;
    SOFTPLG_MODEL_INPUT Input;
    SIZE_T Bytes;
    const char* Named;

    ModelLoadFirmware();
    memset(&Shape, 0, sizeof(Shape));
    Shape.Pipelines = SOFTPLG_PIPELINE_MASK_WORDS * 32;
    Shape.Copier = TRUE;
    Shape.Formats = 1;
    CHECK(GenBuild(&Topology, &Shape));
    if (Topology.Data == NULL)
        return;
    Model = ModelBuild(Topology.Data, Topology.Bytes, &Bytes);
    CHECK(Model != NULL);
    if (Model != NULL)
    {
        CHECK_EQ(ModelStatus, SoftplgModelOk);
        CHECK_EQ(Model->WidgetCount, 257);
        CHECK_EQ(Model->PipelineCount, 256);
        CHECK_EQ(Model->Pcms[0].PlaybackCopier, Topology.Copier);
        /* the copier sits in the last pipeline: the last bit of the mask's last word */
        CHECK_EQ(Model->Pcms[0].PipelineMask[SOFTPLG_PIPELINE_MASK_WORDS - 1], 0x80000000u);
        CHECK_EQ(Model->Pipelines[255].Id, 255);
        CHECK_EQ(Model->Widgets[Topology.Copier].Pipeline, 255);
        CHECK_EQ(ModelValidate(Model, 257, 4096, &Named), SoftplgModelOk);
        /* the firmware's own count, below the mask's, is still the validator's to say */
        CHECK_EQ(ModelValidate(Model, 256, 4096, &Named), SoftplgModelTooManyPipelines);
        free(Model);
    }
    GenFree(&Topology);

    /* one pipeline more: measured, then refused at the scheduler that would take the ordinal 256 */
    Shape.Pipelines = SOFTPLG_PIPELINE_MASK_WORDS * 32 + 1;
    CHECK(GenBuild(&Topology, &Shape));
    if (Topology.Data == NULL)
        return;
    memset(&Input, 0, sizeof(Input));
    Input.Data = Topology.Data;
    Input.Size = Topology.Bytes;
    Input.Lookup = ModelLookup;
    ModelProblem.Widget = SOFTPLG_NO_WIDGET;
    CHECK_EQ(SoftplgModelMeasure(&Input, &Bytes, &ModelProblem), SoftplgModelOk);
    CHECK(Bytes != 0);
    Model = ModelBuild(Topology.Data, Topology.Bytes, &Bytes);
    CHECK(Model == NULL);
    CHECK_EQ(ModelStatus, SoftplgModelTooManyPipelines);
    CHECK_EQ(ModelProblem.Status, SoftplgModelTooManyPipelines);
    CHECK_EQ(ModelProblem.Widget, 256);
    free(Model);
    GenFree(&Topology);
    CHECK_STR(SoftplgModelStatusName(SoftplgModelTooManyPipelines), "more pipelines than the firmware allows");
}

/* FB-02: the capture side of R2-10's rule. A host capture copier (aif_out) takes output formats, and its gateway buffer is two milliseconds of the stream's: a buffer that does not fit the word is refused by the validation with the copier named, the configuration call says so instead of a wrapped size, and the first output format's own buffer is refused at the build */
static
void
check_model_gateway_buffer_capture(void)
{
    GEN_TOPOLOGY Topology;
    GEN_SHAPE Shape;
    PSOFTPLG_IPC4_MODEL Model;
    SOFTPLG_IPC4_CONFIG Config;
    SIZE_T Bytes;
    const char* Named;

    ModelLoadFirmware();
    memset(&Shape, 0, sizeof(Shape));
    Shape.Pipelines = 1;
    Shape.Copier = TRUE;
    Shape.Capture = TRUE;
    Shape.Graph = TRUE;
    Shape.Formats = 2;
    /* the first output format's buffer is whole, the second one's is not: the model builds, the validation refuses */
    Shape.BufferBytes = 0x80000000u;
    CHECK(GenBuild(&Topology, &Shape));
    if (Topology.Data == NULL)
        return;
    Model = ModelBuild(Topology.Data, Topology.Bytes, &Bytes);
    CHECK(Model != NULL);
    if (Model != NULL)
    {
        CHECK_EQ(ModelStatus, SoftplgModelOk);
        /* the capture copier of PCM0: output formats only, the PCM's capture stream */
        CHECK(Model->Widgets[Topology.Copier].Kind == SoftplgKindHostCopierCapture);
        CHECK_EQ(Model->Widgets[Topology.Copier].InputFormats, 0);
        CHECK_EQ(Model->Widgets[Topology.Copier].OutputFormats, 2);
        CHECK_EQ(Model->Pcms[0].CaptureCopier, Topology.Copier);
        CHECK_EQ(Model->Pcms[0].PlaybackCopier, SOFTPLG_NO_WIDGET);
        CHECK_EQ(SoftplgModelConfig(Model, Topology.Copier, 0, 0, 1, &Config), SoftplgModelOk);
        CHECK_EQ(Config.Count, SOFTPLG_IPC4_COPIER_CONFIG_WORDS);
        CHECK_EQ(Config.Words[SOFTPLG_COPIER_DMA_BUFFER_WORD], SOFTPLG_DMA_BUFFER_MS_MIN * GEN_FIRST_BUFFER_BYTES);
        /* the second output format's buffer, two milliseconds of it, does not fit the word */
        CHECK_EQ(ModelValidate(Model, 16, 4096, &Named), SoftplgModelGatewayBuffer);
        if (Named != NULL)
            CHECK_STR(Named, "host-copier.0.capture");
        CHECK_EQ(SoftplgModelConfig(Model, Topology.Copier, 0, 1, 1, &Config), SoftplgModelGatewayBuffer);
        CHECK_EQ(Config.Count, 0);
        free(Model);
    }
    GenFree(&Topology);

    /* the first output format's own buffer overflows: the model's configuration of it is made at the build, which refuses it */
    Shape.Formats = 1;
    Shape.BufferBytes = 0;
    Shape.FirstBufferBytes = 0x80000000u;
    CHECK(GenBuild(&Topology, &Shape));
    if (Topology.Data == NULL)
        return;
    Model = ModelBuild(Topology.Data, Topology.Bytes, &Bytes);
    CHECK(Model == NULL);
    CHECK_EQ(ModelStatus, SoftplgModelGatewayBuffer);
    CHECK_EQ(ModelProblem.Status, SoftplgModelGatewayBuffer);
    CHECK_EQ(ModelProblem.Widget, Topology.Copier);
    free(Model);
    GenFree(&Topology);

    /* the boundary: a buffer of 0x7FFFFFFF bytes, two milliseconds of it 0xFFFFFFFE, is the largest that fits */
    Shape.FirstBufferBytes = 0x7FFFFFFFu;
    CHECK(GenBuild(&Topology, &Shape));
    if (Topology.Data == NULL)
        return;
    Model = ModelBuild(Topology.Data, Topology.Bytes, &Bytes);
    CHECK(Model != NULL);
    if (Model != NULL)
    {
        CHECK_EQ(ModelValidate(Model, 16, 4096, &Named), SoftplgModelOk);
        CHECK_EQ(SoftplgModelConfig(Model, Topology.Copier, 0, 0, 1, &Config), SoftplgModelOk);
        CHECK_EQ(Config.Words[SOFTPLG_COPIER_DMA_BUFFER_WORD], 0xFFFFFFFEu);
        free(Model);
    }
    GenFree(&Topology);
}

/* R2-10: a copier whose gateway buffer size, two milliseconds of its stream or the deep buffer it names, does not fit the word it is sent in is refused by the validation with the copier named, and the configuration call says so instead of a wrapped size */
static
void
check_model_gateway_buffer(void)
{
    GEN_TOPOLOGY Topology;
    GEN_SHAPE Shape;
    PSOFTPLG_IPC4_MODEL Model;
    SOFTPLG_IPC4_CONFIG Config;
    SIZE_T Bytes;
    const char* Named;

    ModelLoadFirmware();
    memset(&Shape, 0, sizeof(Shape));
    Shape.Pipelines = 1;
    Shape.Copier = TRUE;
    Shape.Graph = TRUE;
    Shape.Formats = 2;
    /* the first format's buffer is whole, the second one's is not: the model builds, the validation refuses */
    Shape.BufferBytes = 0x80000000u;
    CHECK(GenBuild(&Topology, &Shape));
    if (Topology.Data == NULL)
        return;
    Model = ModelBuild(Topology.Data, Topology.Bytes, &Bytes);
    CHECK(Model != NULL);
    if (Model != NULL)
    {
        CHECK_EQ(ModelStatus, SoftplgModelOk);
        CHECK_EQ(SoftplgModelConfig(Model, Topology.Copier, 0, 0, 1, &Config), SoftplgModelOk);
        CHECK_EQ(Config.Count, SOFTPLG_IPC4_COPIER_CONFIG_WORDS);
        CHECK_EQ(Config.Words[SOFTPLG_COPIER_DMA_BUFFER_WORD], SOFTPLG_DMA_BUFFER_MS_MIN * GEN_FIRST_BUFFER_BYTES);
        /* the second format's buffer, two milliseconds of it, does not fit the word */
        CHECK_EQ(ModelValidate(Model, 16, 4096, &Named), SoftplgModelGatewayBuffer);
        if (Named != NULL)
            CHECK_STR(Named, "host-copier.0.playback");
        CHECK_EQ(SoftplgModelConfig(Model, Topology.Copier, 1, 0, 1, &Config), SoftplgModelGatewayBuffer);
        CHECK_EQ(Config.Count, 0);
        free(Model);
    }
    GenFree(&Topology);

    /* the boundary: a buffer of 0x7FFFFFFF bytes, two milliseconds of it 0xFFFFFFFE, is the largest that fits */
    Shape.Formats = 1;
    Shape.BufferBytes = 0;
    Shape.FirstBufferBytes = 0x7FFFFFFFu;
    CHECK(GenBuild(&Topology, &Shape));
    if (Topology.Data == NULL)
        return;
    Model = ModelBuild(Topology.Data, Topology.Bytes, &Bytes);
    CHECK(Model != NULL);
    if (Model != NULL)
    {
        CHECK_EQ(ModelValidate(Model, 16, 4096, &Named), SoftplgModelOk);
        CHECK_EQ(SoftplgModelConfig(Model, Topology.Copier, 0, 0, 1, &Config), SoftplgModelOk);
        CHECK_EQ(Config.Words[SOFTPLG_COPIER_DMA_BUFFER_WORD], 0xFFFFFFFEu);
        free(Model);
    }
    GenFree(&Topology);

    /* the first format's own buffer overflows: the model's configuration of it is made at the build, which refuses it */
    Shape.FirstBufferBytes = 0x80000000u;
    CHECK(GenBuild(&Topology, &Shape));
    if (Topology.Data == NULL)
        return;
    Model = ModelBuild(Topology.Data, Topology.Bytes, &Bytes);
    CHECK(Model == NULL);
    CHECK_EQ(ModelStatus, SoftplgModelGatewayBuffer);
    CHECK_EQ(ModelProblem.Status, SoftplgModelGatewayBuffer);
    CHECK_EQ(ModelProblem.Widget, Topology.Copier);
    free(Model);
    GenFree(&Topology);

    /* a deep buffer of 2^31 milliseconds over 384-byte frames: the product the copier names, not two milliseconds */
    Shape.FirstBufferBytes = 0;
    Shape.DeepBufferMs = 0x80000000u;
    CHECK(GenBuild(&Topology, &Shape));
    if (Topology.Data == NULL)
        return;
    Model = ModelBuild(Topology.Data, Topology.Bytes, &Bytes);
    CHECK(Model == NULL);
    CHECK_EQ(ModelStatus, SoftplgModelGatewayBuffer);
    CHECK_EQ(ModelProblem.Widget, Topology.Copier);
    free(Model);
    GenFree(&Topology);
    /* a deep buffer of 2 ms over 384-byte frames is two milliseconds like any other */
    Shape.DeepBufferMs = SOFTPLG_DMA_BUFFER_MS_MIN;
    CHECK(GenBuild(&Topology, &Shape));
    if (Topology.Data == NULL)
        return;
    Model = ModelBuild(Topology.Data, Topology.Bytes, &Bytes);
    CHECK(Model != NULL);
    if (Model != NULL)
    {
        CHECK_EQ(ModelValidate(Model, 16, 4096, &Named), SoftplgModelOk);
        free(Model);
    }
    GenFree(&Topology);

    /* a deep buffer of 65536 milliseconds over a 384-byte frame fits, over a 65536-byte one does not: the model builds on the first format and the validation refuses the second by the deep buffer */
    Shape.DeepBufferMs = 0x10000;
    Shape.Formats = 2;
    Shape.BufferBytes = 0x10000;
    CHECK(GenBuild(&Topology, &Shape));
    if (Topology.Data == NULL)
        return;
    Model = ModelBuild(Topology.Data, Topology.Bytes, &Bytes);
    CHECK(Model != NULL);
    if (Model != NULL)
    {
        CHECK_EQ(SoftplgModelConfig(Model, Topology.Copier, 0, 0, 1, &Config), SoftplgModelOk);
        CHECK_EQ(Config.Words[SOFTPLG_COPIER_DMA_BUFFER_WORD], 0x10000 * GEN_FIRST_BUFFER_BYTES);
        CHECK_EQ(ModelValidate(Model, 16, 4096, &Named), SoftplgModelGatewayBuffer);
        if (Named != NULL)
            CHECK_STR(Named, "host-copier.0.playback");
        CHECK_EQ(SoftplgModelConfig(Model, Topology.Copier, 1, 0, 1, &Config), SoftplgModelGatewayBuffer);
        free(Model);
    }
    GenFree(&Topology);
    CHECK_STR(SoftplgModelStatusName(SoftplgModelGatewayBuffer), "gateway buffer does not fit its word");
}

/* R2-04: the blocks are read once per pass, not once per lookup; a stream's reach is one walk over the graph, not a pass over the edges per widget */
static
void
check_model_block_walk(void)
{
    static const UINT32 ChainPipelines = 64;
    static const UINT32 EmptyBlocks = 20000;
    GEN_TOPOLOGY Plain, Padded;
    GEN_SHAPE Shape;
    PSOFTPLG_IPC4_MODEL A, B;
    SIZE_T Bytes, Reads;
    UINT32 W, E, Blocks;
    const char* Named;

    ModelLoadFirmware();
    memset(&Shape, 0, sizeof(Shape));
    Shape.Pipelines = ChainPipelines;
    Shape.Copier = TRUE;
    Shape.Formats = 1;
    Shape.Graph = TRUE;
    Shape.Chain = TRUE;
    CHECK(GenBuild(&Plain, &Shape));
    if (Plain.Data == NULL)
        return;
    A = ModelBuild(Plain.Data, Plain.Bytes, &Bytes);
    CHECK(A != NULL);
    if (A != NULL)
    {
        /* the chain, given from its far end: the copier's stream reaches every scheduler and the copier itself */
        CHECK_EQ(A->EdgeCount, ChainPipelines);
        CHECK_EQ(A->Pcms[0].Reached, ChainPipelines + 1);
        for (W = 0; W < SOFTPLG_PIPELINE_MASK_WORDS; W++)
            CHECK_EQ(A->Pcms[0].PipelineMask[W], W < ChainPipelines / 32 ? 0xFFFFFFFFu : 0);
        for (W = 0; W <= ChainPipelines; W++)
            CHECK(SoftplgModelReachable(A, 0, W));
        CHECK_EQ(ModelValidate(A, ChainPipelines + 1, 4096, &Named), SoftplgModelOk);
    }

    /* the same file with twenty thousand empty blocks after it: the same model, for a bounded number of header words */
    Shape.EmptyBlocks = EmptyBlocks;
    CHECK(GenBuild(&Padded, &Shape));
    if (Padded.Data == NULL)
    {
        free(A);
        GenFree(&Plain);
        return;
    }
    HostHeaderReads = 0;
    B = ModelBuild(Padded.Data, Padded.Bytes, &Bytes);
    Reads = HostHeaderReads;
    CHECK(B != NULL);
    if (A != NULL && B != NULL)
    {
        CHECK_EQ(B->WidgetCount, A->WidgetCount);
        CHECK_EQ(B->EdgeCount, A->EdgeCount);
        CHECK_EQ(B->PcmCount, A->PcmCount);
        CHECK_EQ(B->PipelineCount, A->PipelineCount);
        CHECK_EQ(B->FormatCount, A->FormatCount);
        CHECK_EQ(B->ControlCount, A->ControlCount);
        for (W = 0; W < A->WidgetCount; W++)
        {
            CHECK(memcmp(&A->Widgets[W], &B->Widgets[W], sizeof(A->Widgets[W])) == 0);
            CHECK_STR(B->Widgets[W].Name, A->Widgets[W].Name);
        }
        for (E = 0; E < A->EdgeCount; E++)
        {
            CHECK_EQ(B->Edges[E].Source, A->Edges[E].Source);
            CHECK_EQ(B->Edges[E].Sink, A->Edges[E].Sink);
        }
        for (W = 0; W < A->PipelineCount; W++)
        {
            CHECK_EQ(B->Pipelines[W].Id, A->Pipelines[W].Id);
            CHECK_EQ(B->Pipelines[W].Scheduler, A->Pipelines[W].Scheduler);
        }
        /* the PCM records, Reachable aside: it is an address into each model's own buffer */
        for (W = 0; W < A->PcmCount; W++)
        {
            CHECK(memcmp(A->Pcms[W].Name, B->Pcms[W].Name, sizeof(A->Pcms[W].Name)) == 0);
            CHECK(memcmp(A->Pcms[W].DaiName, B->Pcms[W].DaiName, sizeof(A->Pcms[W].DaiName)) == 0);
            CHECK_EQ(A->Pcms[W].PcmId, B->Pcms[W].PcmId);
            CHECK_EQ(A->Pcms[W].DaiId, B->Pcms[W].DaiId);
            CHECK_EQ(A->Pcms[W].Playback, B->Pcms[W].Playback);
            CHECK_EQ(A->Pcms[W].Capture, B->Pcms[W].Capture);
            CHECK(memcmp(A->Pcms[W].PlaybackStream, B->Pcms[W].PlaybackStream, sizeof(A->Pcms[W].PlaybackStream)) == 0);
            CHECK(memcmp(A->Pcms[W].CaptureStream, B->Pcms[W].CaptureStream, sizeof(A->Pcms[W].CaptureStream)) == 0);
            CHECK_EQ(A->Pcms[W].PlaybackCopier, B->Pcms[W].PlaybackCopier);
            CHECK_EQ(A->Pcms[W].CaptureCopier, B->Pcms[W].CaptureCopier);
            CHECK_EQ(A->Pcms[W].Reached, B->Pcms[W].Reached);
            CHECK(memcmp(A->Pcms[W].PipelineMask, B->Pcms[W].PipelineMask, sizeof(A->Pcms[W].PipelineMask)) == 0);
            CHECK(B->Pcms[W].Reachable == B->Reachable + (SIZE_T)W * B->ReachWords);
        }
        CHECK(memcmp(A->Reachable, B->Reachable, A->ReachWords * sizeof(UINT32) * A->PcmCount) == 0);
        CHECK(memcmp(A->Formats, B->Formats, sizeof(A->Formats[0]) * A->FormatCount) == 0);
        CHECK_EQ(ModelValidate(B, ChainPipelines + 1, 4096, &Named), SoftplgModelOk);
    }
    /* five passes over the file (the measure's two, the build's two counts, the graph), eight header words a block each and one more for the first: under sixty-four a block. The lookups by index read every header before the one asked for, once per block, on every pass */
    Blocks = 1 + ChainPipelines + 1 + ChainPipelines + EmptyBlocks;
    CHECK(Reads != 0);
    CHECK(Reads <= (SIZE_T)64 * Blocks);
    free(A);
    free(B);
    GenFree(&Plain);
    GenFree(&Padded);
}

static
void
check_names(void)
{
    CHECK_STR(SoftplgStatusName(SoftplgOk), "ok");
    CHECK_STR(SoftplgStatusName(SoftplgNoData), "no data");
    CHECK_STR(SoftplgStatusName(SoftplgBadSignature), "bad signature");
    CHECK_STR(SoftplgStatusName(SoftplgBadHeader), "bad header");
    CHECK_STR(SoftplgStatusName(SoftplgTruncated), "truncated");
    CHECK_STR(SoftplgStatusName(SoftplgNotFound), "not found");
    CHECK_STR(SoftplgStatusName(SoftplgUnsupported), "unsupported");
    CHECK_STR(SoftplgStatusName((SOFTPLG_STATUS)7), "unknown");
    CHECK_STR(SoftplgTypeName(SoftplgTypeManifest), "MANIFEST");
    CHECK_STR(SoftplgTypeName(SoftplgTypeWidget), "DAPM_WIDGET");
    CHECK_STR(SoftplgTypeName(SoftplgTypePcm), "PCM");
    CHECK_STR(SoftplgTypeName(SoftplgTypeBackendLink), "BACKEND_LINK");
    CHECK_STR(SoftplgTypeName(SoftplgTypeVendorBase), "VENDOR");
}

int main(void)
{
    check_sizes();
    check_synthetic();
    check_malformed();
    check_control_bounds();
    check_short_and_garbage_inputs();
    check_fixtures();
    check_ace_fixture();
    check_loopback_fixture();
    check_model_expressible();
    check_model_loopback();
    check_model_ace();
    check_model_dmic();
    check_model_rt711();
    check_model_malformed();
    check_model_pipeline_bound();
    check_model_gateway_buffer();
    check_model_gateway_buffer_capture();
    check_model_block_walk();
    check_names();

    printf("softplg: %d checks, %d failures\n", Checks, Failures);
    return Failures ? 1 : 0;
}
