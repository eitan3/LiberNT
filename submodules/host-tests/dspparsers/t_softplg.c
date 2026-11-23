/*
 * PROJECT:     LiberNT host-native tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     The topology walker reads a synthetic binary and the real sof-bin topologies as the independent walk says, and never reads past a short or garbled one
 * COPYRIGHT:   Copyright 2026 LiberNT contributors
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "shim.h"

#include <softplg.h>

#include "softplg_cases.h"

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

/* Real topologies from sof-bin 2026.09.2 (package sof-firmware, LICENCE.Intel in the fixtures directory); the facts come from an independent Python walk of the same files */
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
        "", "", "", "", "" }
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
            CHECK(WidgetBlock != 0 && PcmBlock != 0 && LinkBlock != 0 && GraphBlock != 0);
            CHECK_EQ(SoftplgReadBlock(Fixture, Size, WidgetBlock, &Block), SoftplgOk);
            CHECK_EQ(Block.Type, SoftplgTypeWidget);
            CHECK_EQ(SoftplgReadWidget(Fixture, Size, &Block, 0, &Widget), SoftplgOk);
            CHECK_STR(Widget.Name, Facts->Widget0);
            CHECK_EQ(SoftplgReadBlock(Fixture, Size, PcmBlock, &Block), SoftplgOk);
            CHECK_EQ(Block.Type, SoftplgTypePcm);
            CHECK_EQ(SoftplgReadPcm(Fixture, Size, &Block, 0, &Pcm), SoftplgOk);
            CHECK_STR(Pcm.PcmName, Facts->Pcm0);
            CHECK_EQ(SoftplgReadBlock(Fixture, Size, LinkBlock, &Block), SoftplgOk);
            CHECK_EQ(Block.Type, SoftplgTypeBackendLink);
            CHECK_EQ(SoftplgReadLink(Fixture, Size, &Block, 0, &Link), SoftplgOk);
            CHECK_STR(Link.Name, Facts->Link0);
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
    check_short_and_garbage_inputs();
    check_fixtures();
    check_names();

    printf("softplg: %d checks, %d failures\n", Checks, Failures);
    return Failures ? 1 : 0;
}
