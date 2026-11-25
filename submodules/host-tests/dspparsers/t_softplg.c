/*
 * PROJECT:     LiberNT host-native tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     The topology walker reads a synthetic binary and the real sof-bin topologies as the independent walk says, resolves their module UUIDs against the firmware image's module table, and never reads past a short or garbled one
 * COPYRIGHT:   Copyright 2026 LiberNT contributors
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "shim.h"

#include <softplg.h>
#include <fwload.h>

#include "softplg_cases.h"

/* The vendor tuple that names a widget's module (include/uapi/sound/sof/tokens.h SOF_TKN_COMP_UUID, studied under LICENSING.md rule 7) */
#define TEST_COMP_UUID             405

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
    check_names();

    printf("softplg: %d checks, %d failures\n", Checks, Failures);
    return Failures ? 1 : 0;
}
