/*
 * PROJECT:     LiberNT kernel-mode tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     The topology walker reads a synthetic topology as laid out and never reads past a short or garbled one
 * COPYRIGHT:   Copyright 2026 LiberNT contributors
 */

#include <kmt_test.h>
#include <softplg.h>

#define NDEBUG
#include <debug.h>

#include "softplg_cases.h"

#define SOFTPLG_TEST_TAG 'Splt'

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
ULONG
WrapSize(
    SIZE_T Bytes,
    SIZE_T Which)
{
    return Which == 2 ? 0xFFFFFFFFUL : (ULONG)(0UL - (ULONG)Bytes) + (ULONG)Which;
}

static
VOID
Put32(
    PUCHAR At,
    ULONG Value)
{
    At[0] = (UCHAR)Value;
    At[1] = (UCHAR)(Value >> 8);
    At[2] = (UCHAR)(Value >> 16);
    At[3] = (UCHAR)(Value >> 24);
}

/* A pool block of exactly Length bytes holding the first Length bytes of Source */
static
PUCHAR
ExactCopy(
    const UINT8* Source,
    SIZE_T Length)
{
    PUCHAR Copy = ExAllocatePoolWithTag(NonPagedPool, Length, SOFTPLG_TEST_TAG);

    ok(Copy != NULL, "allocation of %Iu bytes failed\n", Length);
    if (Copy != NULL)
        RtlCopyMemory(Copy, Source, Length);
    return Copy;
}

static
VOID
ExactFree(
    PUCHAR Copy)
{
    if (Copy != NULL)
        ExFreePoolWithTag(Copy, SOFTPLG_TEST_TAG);
}

/* R7 of the branch review: the private size of a control is read only once its whole structure is known to fit, and no private size wraps the sum */
static
VOID
CheckControlBounds(VOID)
{
    /* block, private size field and structure size of the manifest, the first widget, the PCM and the link */
    static const ULONG ElementBlock[] = { 0, 1, 3, 4 };
    static const SIZE_T ElementPriv[] = { 36 + 108, TPLG_WIDGET0_OFFSET + 128, TPLG_PCM_BLOCK_OFFSET + 36 + 908, TPLG_LINK_BLOCK_OFFSET + 36 + 1652 };
    static const SIZE_T ElementBytes[] = { 112, 132, 912, 1656 };
    static const SIZE_T HeadCuts[] = { 0, 1, 207, 208, 209 };
    static const SIZE_T TailCuts[] = { 1, 2, 4, 5 };
    static const SIZE_T HeadCutCount = sizeof(HeadCuts) / sizeof(HeadCuts[0]);
    static const SIZE_T CutCount = sizeof(HeadCuts) / sizeof(HeadCuts[0]) + sizeof(TailCuts) / sizeof(TailCuts[0]);
    SOFTPLG_SUMMARY Summary;
    SOFTPLG_BLOCK Block;
    SOFTPLG_CONTROL Control;
    SOFTPLG_WIDGET Widget;
    SOFTPLG_MANIFEST Manifest;
    SOFTPLG_PCM Pcm;
    SOFTPLG_LINK Link;
    PUCHAR Copy;
    SIZE_T Size, Bytes, Cut, K, E, V, C;
    SOFTPLG_STATUS Status;

    for (K = 0; K < sizeof(ControlKinds) / sizeof(ControlKinds[0]); K++)
    {
        Bytes = ControlKinds[K].Bytes;

        /* the whole structure with no private data is one control that ends where the file ends */
        Size = 36 + Bytes;
        Copy = ExactCopy(ControlKinds[K].Block, Size);
        if (Copy == NULL)
            return;
        ok_eq_int(SoftplgWalk(Copy, Size, NULL, NULL, &Summary), SoftplgOk);
        ok_eq_int(SoftplgReadBlock(Copy, Size, 0, &Block), SoftplgOk);
        ok_eq_int(SoftplgReadControl(Copy, Size, &Block, 0, &Control), SoftplgOk);
        ok_eq_uint(Control.Kind, (ULONG)K + 1);
        ok_eq_size(Control.Private.Offset, Size);
        ok_eq_size(Control.Private.Size, 0);
        for (V = 0; V < 3; V++)
        {
            Put32(Copy + Size - 4, WrapSize(Bytes, V));
            ok_eq_int(SoftplgReadControl(Copy, Size, &Block, 0, &Control), SoftplgTruncated);
        }
        ExactFree(Copy);

        /* cuts inside the structure: before the control header ends and where it ends, then one, two, four and five bytes short of the structure's end, around its private size field */
        for (C = 0; C < CutCount; C++)
        {
            Cut = C < HeadCutCount ? HeadCuts[C] : Bytes - TailCuts[C - HeadCutCount];
            Size = 36 + Cut;
            Copy = ExactCopy(ControlKinds[K].Block, Size);
            if (Copy == NULL)
                return;
            Put32(Copy + 24, (ULONG)Cut);
            ok_eq_int(SoftplgReadBlock(Copy, Size, 0, &Block), SoftplgOk);
            ok_eq_int(SoftplgReadControl(Copy, Size, &Block, 0, &Control), SoftplgTruncated);
            ExactFree(Copy);
        }
    }

    Size = sizeof(TplgSynthetic);
    Copy = ExactCopy(TplgSynthetic, Size);
    if (Copy == NULL)
        return;
    ok_eq_int(SoftplgWalk(Copy, Size, NULL, NULL, &Summary), SoftplgOk);
    ok_eq_int(SoftplgReadBlock(Copy, Size, 1, &Block), SoftplgOk);
    ok_eq_int(SoftplgReadWidgetControl(Copy, Size, &Block, 1, 0, &Control), SoftplgOk);
    ok_eq_int(SoftplgReadWidget(Copy, Size, &Block, 2, &Widget), SoftplgOk);
    ExactFree(Copy);

    /* the widget block ends inside the control of PGA1.0, and the bytes after its end, which belong to the next widget, read as a private size that wraps the sum */
    Copy = ExactCopy(TplgSynthetic, Size);
    if (Copy == NULL)
        return;
    Put32(Copy + TPLG_WIDGET_BLOCK_OFFSET + 24, (ULONG)(TPLG_CONTROL1_OFFSET + 300 - TPLG_WIDGET_BLOCK_OFFSET - 36));
    Put32(Copy + TPLG_CONTROL1_OFFSET + 356, WrapSize(360, 0));
    ok_eq_int(SoftplgReadBlock(Copy, Size, 1, &Block), SoftplgOk);
    ok_eq_int(SoftplgReadWidgetControl(Copy, Size, &Block, 1, 0, &Control), SoftplgTruncated);
    ok_eq_int(SoftplgReadWidget(Copy, Size, &Block, 2, &Widget), SoftplgTruncated);
    ExactFree(Copy);

    for (V = 0; V < 3; V++)
    {
        Copy = ExactCopy(TplgSynthetic, Size);
        if (Copy == NULL)
            return;
        Put32(Copy + TPLG_CONTROL1_OFFSET + 356, WrapSize(360, V));
        ok_eq_int(SoftplgReadBlock(Copy, Size, 1, &Block), SoftplgOk);
        ok_eq_int(SoftplgReadWidgetControl(Copy, Size, &Block, 1, 0, &Control), SoftplgTruncated);
        ok_eq_int(SoftplgReadWidget(Copy, Size, &Block, 2, &Widget), SoftplgTruncated);
        ExactFree(Copy);

        for (E = 0; E < 4; E++)
        {
            Copy = ExactCopy(TplgSynthetic, Size);
            if (Copy == NULL)
                return;
            Put32(Copy + ElementPriv[E], WrapSize(ElementBytes[E], V));
            ok_eq_int(SoftplgReadBlock(Copy, Size, ElementBlock[E], &Block), SoftplgOk);
            switch (E)
            {
                case 0: Status = SoftplgReadManifest(Copy, Size, &Block, &Manifest); break;
                case 1: Status = SoftplgReadWidget(Copy, Size, &Block, 0, &Widget); break;
                case 2: Status = SoftplgReadPcm(Copy, Size, &Block, 0, &Pcm); break;
                default: Status = SoftplgReadLink(Copy, Size, &Block, 0, &Link); break;
            }
            ok_eq_int(Status, SoftplgTruncated);
            ExactFree(Copy);
        }
    }

    /* the same cuts inside the control appended to PGA1.0, through both readers that walk over it */
    for (C = 0; C < CutCount; C++)
    {
        Cut = C < HeadCutCount ? HeadCuts[C] : 360 - TailCuts[C - HeadCutCount];
        Size = TPLG_CONTROL1_OFFSET + Cut;
        Copy = ExactCopy(TplgSynthetic, Size);
        if (Copy == NULL)
            return;
        Put32(Copy + TPLG_WIDGET_BLOCK_OFFSET + 24, (ULONG)(Size - TPLG_WIDGET_BLOCK_OFFSET - 36));
        ok_eq_int(SoftplgReadBlock(Copy, Size, 1, &Block), SoftplgOk);
        ok_eq_int(SoftplgReadWidgetControl(Copy, Size, &Block, 1, 0, &Control), SoftplgTruncated);
        ok_eq_int(SoftplgReadWidget(Copy, Size, &Block, 2, &Widget), SoftplgTruncated);
        ExactFree(Copy);
    }
}

struct _COLLECT
{
    UINT32 Types[8];
    UINT32 Counts[8];
    UINT32 Seen;
    UINT32 StopAfter;
};

static
BOOLEAN
NTAPI
Collect(
    _In_ const SOFTPLG_BLOCK* Block,
    _In_opt_ PVOID Context)
{
    struct _COLLECT* Seen = Context;

    if (Seen->Seen >= 8)
        return FALSE;
    Seen->Types[Seen->Seen] = Block->Type;
    Seen->Counts[Seen->Seen] = Block->Count;
    Seen->Seen++;
    return Seen->StopAfter == 0 || Seen->Seen < Seen->StopAfter;
}

static
VOID
CheckSynthetic(VOID)
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
    ULONG Index;

    RtlZeroMemory(&Seen, sizeof(Seen));
    RtlZeroMemory(&Summary, sizeof(Summary));
    ok_eq_int(SoftplgWalk(TplgSynthetic, sizeof(TplgSynthetic), NULL, NULL, &Summary), SoftplgOk);
    ok_eq_uint(Summary.BlockCount, 5);
    ok_eq_uint(Summary.Abi, 5);
    ok_eq_uint(Summary.VendorBlocks, 0);
    ok_eq_uint(Summary.Counts[SoftplgTypeManifest], 1);
    ok_eq_uint(Summary.Counts[SoftplgTypeWidget], 1);
    ok_eq_uint(Summary.Counts[SoftplgTypeGraph], 1);
    ok_eq_uint(Summary.Counts[SoftplgTypePcm], 1);
    ok_eq_uint(Summary.Counts[SoftplgTypeBackendLink], 1);
    ok_eq_uint(Summary.Counts[SoftplgTypeDai], 0);

    ok_eq_int(SoftplgWalk(TplgSynthetic, sizeof(TplgSynthetic), Collect, &Seen, &Summary), SoftplgOk);
    ok_eq_uint(Seen.Seen, 5);
    for (Index = 0; Index < 5; Index++)
    {
        ok_eq_uint(Seen.Types[Index], Types[Index]);
        ok_eq_uint(Seen.Counts[Index], Counts[Index]);
        ok_eq_int(SoftplgReadBlock(TplgSynthetic, sizeof(TplgSynthetic), Index, &Block), SoftplgOk);
        ok_eq_uint(Block.Type, Types[Index]);
        ok_eq_uint(Block.Count, Counts[Index]);
        ok_eq_uint(Block.PayloadSize, Payloads[Index]);
        ok_eq_uint(Block.Index, Index);
    }
    ok_eq_uint(Block.NextOffset, sizeof(TplgSynthetic));
    ok_eq_int(SoftplgReadBlock(TplgSynthetic, sizeof(TplgSynthetic), 5, &Block), SoftplgNotFound);

    Seen.Seen = 0;
    Seen.StopAfter = 2;
    ok_eq_int(SoftplgWalk(TplgSynthetic, sizeof(TplgSynthetic), Collect, &Seen, &Summary), SoftplgOk);
    ok_eq_uint(Seen.Seen, 2);

    ok_eq_int(SoftplgReadBlock(TplgSynthetic, sizeof(TplgSynthetic), 0, &Block), SoftplgOk);
    ok_eq_int(SoftplgReadManifest(TplgSynthetic, sizeof(TplgSynthetic), &Block, &Manifest), SoftplgOk);
    ok_eq_uint(Manifest.WidgetElements, 3);
    ok_eq_uint(Manifest.GraphElements, 2);
    ok_eq_uint(Manifest.Private.Size, 3);
    ok_eq_int(SoftplgTupleBegin(TplgSynthetic, sizeof(TplgSynthetic), &Manifest.Private, &Iterator), SoftplgOk);
    ok_eq_int(SoftplgTupleNext(&Iterator, &Tuple), SoftplgUnsupported);

    ok_eq_int(SoftplgReadBlock(TplgSynthetic, sizeof(TplgSynthetic), 1, &Block), SoftplgOk);
    ok_eq_int(SoftplgReadWidget(TplgSynthetic, sizeof(TplgSynthetic), &Block, 0, &Widget), SoftplgOk);
    ok_eq_str(Widget.Name, "PCM0P");
    ok_eq_uint(Widget.Id, 11);
    ok_eq_uint(Widget.ControlCount, 0);
    ok_eq_uint(Widget.Private.Size, 28);
    ok_eq_int(SoftplgTupleBegin(TplgSynthetic, sizeof(TplgSynthetic), &Widget.Private, &Iterator), SoftplgOk);
    ok_eq_int(SoftplgTupleNext(&Iterator, &Tuple), SoftplgOk);
    ok_eq_uint(Tuple.Type, SOFTPLG_TUPLE_WORD);
    ok_eq_uint(Tuple.Token, 400);
    ok_eq_uint(Tuple.Value, 2);
    ok_eq_int(SoftplgTupleNext(&Iterator, &Tuple), SoftplgOk);
    ok_eq_uint(Tuple.Token, 401);
    ok_eq_int(SoftplgTupleNext(&Iterator, &Tuple), SoftplgNotFound);

    ok_eq_int(SoftplgReadWidget(TplgSynthetic, sizeof(TplgSynthetic), &Block, 1, &Widget), SoftplgOk);
    ok_eq_str(Widget.Name, "PGA1.0");
    ok_eq_uint(Widget.Id, 4);
    ok_eq_uint(Widget.ControlCount, 1);
    ok_eq_int(SoftplgReadWidgetControl(TplgSynthetic, sizeof(TplgSynthetic), &Block, 1, 0, &Control), SoftplgOk);
    ok_eq_uint(Control.Kind, SoftplgControlMixer);
    ok_eq_str(Control.Name, "PGA1.0");
    ok_eq_uint(Control.Maximum, 100);
    ok_eq_uint(Control.Channels, 2);
    ok_eq_int(SoftplgReadWidgetControl(TplgSynthetic, sizeof(TplgSynthetic), &Block, 1, 1, &Control), SoftplgNotFound);
    ok_eq_int(SoftplgReadWidgetControl(TplgSynthetic, sizeof(TplgSynthetic), &Block, 0, 0, &Control), SoftplgNotFound);

    ok_eq_int(SoftplgReadWidget(TplgSynthetic, sizeof(TplgSynthetic), &Block, 2, &Widget), SoftplgOk);
    ok_eq_str(Widget.Name, "BUF1.0");
    ok_eq_uint(Widget.Private.Size, 12);
    ok_eq_int(SoftplgTupleBegin(TplgSynthetic, sizeof(TplgSynthetic), &Widget.Private, &Iterator), SoftplgOk);
    ok_eq_int(SoftplgTupleNext(&Iterator, &Tuple), SoftplgUnsupported);
    ok_eq_int(SoftplgReadWidget(TplgSynthetic, sizeof(TplgSynthetic), &Block, 3, &Widget), SoftplgNotFound);

    ok_eq_int(SoftplgReadBlock(TplgSynthetic, sizeof(TplgSynthetic), 2, &Block), SoftplgOk);
    ok_eq_int(SoftplgReadGraphEdge(TplgSynthetic, sizeof(TplgSynthetic), &Block, 0, &Edge), SoftplgOk);
    ok_eq_str(Edge.Sink, "PGA1.0");
    ok_eq_str(Edge.Source, "PCM0P");
    ok_eq_int(SoftplgReadGraphEdge(TplgSynthetic, sizeof(TplgSynthetic), &Block, 1, &Edge), SoftplgOk);
    ok_eq_str(Edge.Sink, "BUF1.0");
    ok_eq_int(SoftplgReadGraphEdge(TplgSynthetic, sizeof(TplgSynthetic), &Block, 2, &Edge), SoftplgNotFound);

    ok_eq_int(SoftplgReadBlock(TplgSynthetic, sizeof(TplgSynthetic), 3, &Block), SoftplgOk);
    ok_eq_int(SoftplgReadPcm(TplgSynthetic, sizeof(TplgSynthetic), &Block, 0, &Pcm), SoftplgOk);
    ok_eq_str(Pcm.PcmName, "PCM0P");
    ok_eq_uint(Pcm.Playback, 1);
    ok_eq_uint(Pcm.Capture, 0);
    ok_eq_uint(Pcm.StreamCount, 0);
    ok_eq_uint(Pcm.Private.Size, 60);
    ok_eq_int(SoftplgReadPcmCaps(TplgSynthetic, sizeof(TplgSynthetic), &Block, 0, 0, &Caps), SoftplgOk);
    ok_eq_uint(Caps.RateMin, 48000);
    ok_eq_uint(Caps.ChannelsMax, 2);
    ok_eq_int(SoftplgReadPcmCaps(TplgSynthetic, sizeof(TplgSynthetic), &Block, 0, 2, &Caps), SoftplgNotFound);
    ok_eq_int(SoftplgTupleBegin(TplgSynthetic, sizeof(TplgSynthetic), &Pcm.Private, &Iterator), SoftplgOk);
    ok_eq_int(SoftplgTupleNext(&Iterator, &Tuple), SoftplgOk);
    ok_eq_uint(Tuple.Type, SOFTPLG_TUPLE_STRING);
    ok_eq_str(Tuple.String, "PGA");
    ok_eq_int(SoftplgTupleNext(&Iterator, &Tuple), SoftplgNotFound);

    ok_eq_int(SoftplgReadBlock(TplgSynthetic, sizeof(TplgSynthetic), 4, &Block), SoftplgOk);
    ok_eq_int(SoftplgReadLink(TplgSynthetic, sizeof(TplgSynthetic), &Block, 0, &Link), SoftplgOk);
    ok_eq_str(Link.Name, "NoCodec0");
    ok_eq_uint(Link.HwConfigCount, 1);
    ok_eq_int(SoftplgReadLinkHwConfig(TplgSynthetic, sizeof(TplgSynthetic), &Block, 0, 0, &Config), SoftplgOk);
    ok_eq_uint(Config.Format, 1);
    ok_eq_uint(Config.BclkRate, 1536000);
    ok_eq_uint(Config.FsyncRate, 48000);
    ok_eq_uint(Config.TdmSlots, 2);
    ok_eq_int(SoftplgReadLinkHwConfig(TplgSynthetic, sizeof(TplgSynthetic), &Block, 0, 1, &Config), SoftplgNotFound);
    ok_eq_int(SoftplgReadLink(TplgSynthetic, sizeof(TplgSynthetic), &Block, 1, &Link), SoftplgNotFound);
}

static
VOID
CheckMalformed(VOID)
{
    static unsigned char Copy[4096];
    const SIZE_T Size = sizeof(TplgSynthetic);
    SOFTPLG_SUMMARY Summary;
    SOFTPLG_BLOCK Block;
    SOFTPLG_WIDGET Widget;
    SOFTPLG_CONTROL Control;
    SOFTPLG_GRAPH_EDGE Edge;
    SOFTPLG_PCM Pcm;
    SOFTPLG_TUPLE_ITER Iterator;
    SOFTPLG_TUPLE Tuple;
    SIZE_T Length;
    ULONG Index;

    RtlCopyMemory(Copy, TplgSynthetic, Size);
    Copy[0] = 'X';
    ok_eq_int(SoftplgWalk(Copy, Size, NULL, NULL, &Summary), SoftplgBadSignature);
    RtlCopyMemory(Copy, TplgSynthetic, Size);
    Copy[16] = 32;
    ok_eq_int(SoftplgWalk(Copy, Size, NULL, NULL, &Summary), SoftplgBadHeader);
    RtlCopyMemory(Copy, TplgSynthetic, Size);
    Copy[4] = 4;
    ok_eq_int(SoftplgWalk(Copy, Size, NULL, NULL, &Summary), SoftplgUnsupported);
    RtlCopyMemory(Copy, TplgSynthetic, Size);
    Copy[TPLG_WIDGET_BLOCK_OFFSET + 24] = 0xFF;
    Copy[TPLG_WIDGET_BLOCK_OFFSET + 27] = 0x7F;
    ok_eq_int(SoftplgWalk(Copy, Size, NULL, NULL, &Summary), SoftplgTruncated);
    ok_eq_uint(Summary.BlockCount, 1);

    RtlCopyMemory(Copy, TplgSynthetic, Size);
    Copy[TPLG_WIDGET0_OFFSET] = 136;
    ok_eq_int(SoftplgReadBlock(Copy, Size, 1, &Block), SoftplgOk);
    ok_eq_int(SoftplgReadWidget(Copy, Size, &Block, 0, &Widget), SoftplgUnsupported);

    RtlCopyMemory(Copy, TplgSynthetic, Size);
    Copy[TPLG_CONTROL1_OFFSET + 204] = 0x60;
    ok_eq_int(SoftplgReadBlock(Copy, Size, 1, &Block), SoftplgOk);
    ok_eq_int(SoftplgReadWidget(Copy, Size, &Block, 1, &Widget), SoftplgOk);
    ok_eq_int(SoftplgReadWidgetControl(Copy, Size, &Block, 1, 0, &Control), SoftplgUnsupported);
    ok_eq_int(SoftplgReadWidget(Copy, Size, &Block, 2, &Widget), SoftplgUnsupported);

    RtlCopyMemory(Copy, TplgSynthetic, Size);
    Copy[TPLG_GRAPH_BLOCK_OFFSET + 32] = 3;
    ok_eq_int(SoftplgReadBlock(Copy, Size, 2, &Block), SoftplgOk);
    ok_eq_int(SoftplgReadGraphEdge(Copy, Size, &Block, 1, &Edge), SoftplgOk);
    ok_eq_int(SoftplgReadGraphEdge(Copy, Size, &Block, 2, &Edge), SoftplgTruncated);

    RtlCopyMemory(Copy, TplgSynthetic, Size);
    Copy[TPLG_WIDGET0_OFFSET + 132 + 8] = 9;
    ok_eq_int(SoftplgReadBlock(Copy, Size, 1, &Block), SoftplgOk);
    ok_eq_int(SoftplgReadWidget(Copy, Size, &Block, 0, &Widget), SoftplgOk);
    ok_eq_int(SoftplgTupleBegin(Copy, Size, &Widget.Private, &Iterator), SoftplgOk);
    ok_eq_int(SoftplgTupleNext(&Iterator, &Tuple), SoftplgTruncated);
    Copy[TPLG_WIDGET0_OFFSET + 132 + 4] = 7;
    ok_eq_int(SoftplgTupleBegin(Copy, Size, &Widget.Private, &Iterator), SoftplgOk);
    ok_eq_int(SoftplgTupleNext(&Iterator, &Tuple), SoftplgUnsupported);

    ok_eq_int(SoftplgWalk(NULL, 0, NULL, NULL, &Summary), SoftplgNoData);
    ok_eq_int(SoftplgReadBlock(NULL, 0, 0, &Block), SoftplgNoData);

    for (Length = 0; Length < Size; Length += 3)
    {
        SoftplgWalk(TplgSynthetic, Length, NULL, NULL, &Summary);
        SoftplgReadBlock(TplgSynthetic, Length, 3, &Block);
        SoftplgReadPcm(TplgSynthetic, Length, &Block, 0, &Pcm);
    }
    for (Index = 0; Index < Size; Index += 7)
    {
        RtlCopyMemory(Copy, TplgSynthetic, Size);
        Copy[Index] ^= 0x5A;
        SoftplgWalk(Copy, Size, NULL, NULL, &Summary);
        if (SoftplgReadBlock(Copy, Size, 1, &Block) == SoftplgOk)
            SoftplgReadWidget(Copy, Size, &Block, 1, &Widget);
    }
}

START_TEST(Softplg)
{
    CheckSynthetic();
    CheckMalformed();
    CheckControlBounds();
}
