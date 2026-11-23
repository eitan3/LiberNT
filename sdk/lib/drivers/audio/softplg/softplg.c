/*
 * PROJECT:     LiberNT audio driver libraries
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Walks an ALSA SoC topology binary into its blocks, elements, controls and vendor tuples, without I/O
 * COPYRIGHT:   Copyright 2026 LiberNT contributors
 */

#include <ntddk.h>
#include "softplg.h"

/* Sizes and field positions of the ABI version 5 structures named in softplg.h, as studied from include/uapi/sound/asoc.h */

/* snd_soc_tplg_hdr */
#define HDR_ABI_OFFSET              4
#define HDR_VERSION_OFFSET          8
#define HDR_TYPE_OFFSET             12
#define HDR_SIZE_OFFSET             16
#define HDR_VENDOR_TYPE_OFFSET      20
#define HDR_PAYLOAD_SIZE_OFFSET     24
#define HDR_INDEX_OFFSET            28
#define HDR_COUNT_OFFSET            32

/* Every element starts with the size of its structure; the private data that follows it has its own size field */
#define SIZE_FIELD_BYTES            4

/* snd_soc_tplg_ctl_hdr, then the mixer, enum and bytes controls built on it */
#define CTL_BYTES                   204
#define CTL_TYPE_OFFSET             4
#define CTL_NAME_OFFSET             8
#define CTL_ACCESS_OFFSET           52
#define MIXER_MIN_OFFSET            208
#define MIXER_MAX_OFFSET            212
#define MIXER_PLATFORM_MAX_OFFSET   216
#define MIXER_INVERT_OFFSET         220
#define MIXER_CHANNELS_OFFSET       224
#define BYTES_MAX_OFFSET            208
#define BYTES_MASK_OFFSET           212
#define BYTES_BASE_OFFSET           216
#define BYTES_REGS_OFFSET           220
#define ENUM_CHANNELS_OFFSET        208
#define ENUM_ITEMS_OFFSET           340
#define ENUM_MASK_OFFSET            344
#define ENUM_COUNT_OFFSET           348

/* snd_soc_tplg_manifest */
#define MANIFEST_BYTES              112
#define MANIFEST_CONTROLS_OFFSET    4
#define MANIFEST_WIDGETS_OFFSET     8
#define MANIFEST_GRAPH_OFFSET       12
#define MANIFEST_PCM_OFFSET         16
#define MANIFEST_LINKS_OFFSET       20
#define MANIFEST_DAI_OFFSET         24
#define MANIFEST_PRIV_OFFSET        108

/* snd_soc_tplg_dapm_graph_elem */
#define GRAPH_BYTES                 132
#define GRAPH_SINK_OFFSET           0
#define GRAPH_CONTROL_OFFSET        SOFTPLG_NAME_BYTES
#define GRAPH_SOURCE_OFFSET         (2 * SOFTPLG_NAME_BYTES)

/* snd_soc_tplg_dapm_widget */
#define WIDGET_BYTES                132
#define WIDGET_ID_OFFSET            4
#define WIDGET_NAME_OFFSET          8
#define WIDGET_SNAME_OFFSET         52
#define WIDGET_REG_OFFSET           96
#define WIDGET_SHIFT_OFFSET         100
#define WIDGET_MASK_OFFSET          104
#define WIDGET_SUBSEQ_OFFSET        108
#define WIDGET_INVERT_OFFSET        112
#define WIDGET_IGNORE_OFFSET        116
#define WIDGET_EVENT_FLAGS_OFFSET   120
#define WIDGET_EVENT_TYPE_OFFSET    122
#define WIDGET_CONTROLS_OFFSET      124
#define WIDGET_PRIV_OFFSET          128

/* snd_soc_tplg_stream and snd_soc_tplg_stream_caps */
#define STREAM_BYTES                72
#define STREAM_NAME_OFFSET          4
#define STREAM_FORMAT_OFFSET        48
#define STREAM_RATE_OFFSET          56
#define STREAM_PERIOD_OFFSET        60
#define STREAM_BUFFER_OFFSET        64
#define STREAM_CHANNELS_OFFSET      68
#define CAPS_BYTES                  104
#define CAPS_NAME_OFFSET            4
#define CAPS_FORMATS_OFFSET         48
#define CAPS_RATES_OFFSET           56
#define CAPS_RATE_MIN_OFFSET        60
#define CAPS_RATE_MAX_OFFSET        64
#define CAPS_CHANNELS_MIN_OFFSET    68
#define CAPS_CHANNELS_MAX_OFFSET    72
#define CAPS_PERIODS_MIN_OFFSET     76
#define CAPS_PERIODS_MAX_OFFSET     80
#define CAPS_PERIOD_SIZE_MIN_OFFSET 84
#define CAPS_PERIOD_SIZE_MAX_OFFSET 88
#define CAPS_BUFFER_MIN_OFFSET      92
#define CAPS_BUFFER_MAX_OFFSET      96
#define CAPS_SIG_BITS_OFFSET        100

/* snd_soc_tplg_pcm */
#define PCM_BYTES                   912
#define PCM_NAME_OFFSET             4
#define PCM_DAI_NAME_OFFSET         48
#define PCM_ID_OFFSET               92
#define PCM_DAI_ID_OFFSET           96
#define PCM_PLAYBACK_OFFSET         100
#define PCM_CAPTURE_OFFSET          104
#define PCM_COMPRESS_OFFSET         108
#define PCM_STREAMS_OFFSET          112
#define PCM_STREAM_COUNT_OFFSET     688
#define PCM_CAPS_OFFSET             692
#define PCM_FLAG_MASK_OFFSET        900
#define PCM_FLAGS_OFFSET            904
#define PCM_PRIV_OFFSET             908

/* snd_soc_tplg_hw_config */
#define HW_CONFIG_BYTES             120
#define HW_ID_OFFSET                4
#define HW_FORMAT_OFFSET            8
#define HW_CLOCK_GATED_OFFSET       12
#define HW_INVERT_BCLK_OFFSET       13
#define HW_INVERT_FSYNC_OFFSET      14
#define HW_BCLK_PROVIDER_OFFSET     15
#define HW_FSYNC_PROVIDER_OFFSET    16
#define HW_MCLK_DIRECTION_OFFSET    17
#define HW_MCLK_RATE_OFFSET         20
#define HW_BCLK_RATE_OFFSET         24
#define HW_FSYNC_RATE_OFFSET        28
#define HW_TDM_SLOTS_OFFSET         32
#define HW_TDM_SLOT_WIDTH_OFFSET    36
#define HW_TX_SLOTS_OFFSET          40
#define HW_RX_SLOTS_OFFSET          44
#define HW_TX_CHANNELS_OFFSET       48
#define HW_RX_CHANNELS_OFFSET       84

/* snd_soc_tplg_link_config (also the codec and backend link blocks) */
#define LINK_BYTES                  1656
#define LINK_ID_OFFSET              4
#define LINK_NAME_OFFSET            8
#define LINK_STREAM_NAME_OFFSET     52
#define LINK_STREAMS_OFFSET         96
#define LINK_STREAM_COUNT_OFFSET    672
#define LINK_HW_CONFIGS_OFFSET      676
#define LINK_HW_CONFIG_COUNT_OFFSET 1636
#define LINK_DEFAULT_HW_OFFSET      1640
#define LINK_FLAG_MASK_OFFSET       1644
#define LINK_FLAGS_OFFSET           1648
#define LINK_PRIV_OFFSET            1652

/* snd_soc_tplg_dai */
#define DAI_BYTES                   280
#define DAI_NAME_OFFSET             4
#define DAI_ID_OFFSET               48
#define DAI_PLAYBACK_OFFSET         52
#define DAI_CAPTURE_OFFSET          56
#define DAI_CAPS_OFFSET             60
#define DAI_FLAG_MASK_OFFSET        268
#define DAI_FLAGS_OFFSET            272
#define DAI_PRIV_OFFSET             276

/* snd_soc_tplg_vendor_array and its elements */
#define ARRAY_HEADER_BYTES          12
#define ARRAY_SIZE_OFFSET           0
#define ARRAY_TYPE_OFFSET           4
#define ARRAY_COUNT_OFFSET          8
#define TUPLE_TOKEN_OFFSET          0
#define TUPLE_VALUE_OFFSET          4
#define TUPLE_UUID_OFFSET           4
#define TUPLE_STRING_OFFSET         4
#define TUPLE_STRING_BYTES          SOFTPLG_NAME_BYTES

typedef struct _KIND_LAYOUT
{
    UINT32 Bytes;
    UINT32 PrivOffset;
} KIND_LAYOUT;

static const KIND_LAYOUT SoftplgKindLayouts[4] =
{
    { 0, 0 },
    { 360, 356 },
    { 240, 236 },
    { 1764, 1760 }
};

typedef struct _ELEMENT_WALK
{
    const UINT8* Data;
    SIZE_T Size;
    SIZE_T Offset;
    SIZE_T Limit;
} ELEMENT_WALK;

/* An element: where it starts in the file and how many bytes it takes with its private data */
typedef struct _SOFTPLG_ELEMENT
{
    SIZE_T Offset;
    SIZE_T Size;
} SOFTPLG_ELEMENT, *PSOFTPLG_ELEMENT;

static
UINT16
SoftplgReadU16(
    const UINT8* Data)
{
    return (UINT16)(Data[0] | ((UINT16)Data[1] << 8));
}

static
UINT32
SoftplgReadU32(
    const UINT8* Data)
{
    return (UINT32)Data[0] | ((UINT32)Data[1] << 8) | ((UINT32)Data[2] << 16) |
        ((UINT32)Data[3] << 24);
}

static
UINT64
SoftplgReadU64(
    const UINT8* Data)
{
    return (UINT64)SoftplgReadU32(Data) | ((UINT64)SoftplgReadU32(Data + 4) << 32);
}

static
void
SoftplgReadText(
    _Out_writes_bytes_(Capacity) CHAR* Destination,
    SIZE_T Capacity,
    _In_reads_bytes_(Length) const UINT8* Source,
    SIZE_T Length)
{
    SIZE_T Index;

    for (Index = 0; Index < Length && Index + 1 < Capacity; Index++)
        Destination[Index] = (CHAR)Source[Index];
    Destination[Index] = 0;
}

static
BOOLEAN
SoftplgInRange(
    SIZE_T Size,
    SIZE_T Offset,
    SIZE_T Length)
{
    return Offset <= Size && Length <= Size - Offset;
}

static
BOOLEAN
SoftplgRangeFits(
    const ELEMENT_WALK* Walk,
    SIZE_T Offset,
    SIZE_T Length)
{
    return SoftplgInRange(Walk->Size, Offset, Length) && Offset <= Walk->Limit && Length <= Walk->Limit - Offset;
}

/* Steps over one control: the 204-byte control header, then the control structure named by its kind, then its private data */
static
SOFTPLG_STATUS
SoftplgStepControl(
    const ELEMENT_WALK* Walk,
    _Inout_ SIZE_T* Offset,
    _Out_opt_ SOFTPLG_CONTROL* Control)
{
    UINT32 Kind, Bytes, PrivateSize;
    const UINT8* Record;

    if (!SoftplgRangeFits(Walk, *Offset, CTL_BYTES + SIZE_FIELD_BYTES))
        return SoftplgTruncated;
    Record = Walk->Data + *Offset;
    Kind = SoftplgReadU32(Record + CTL_TYPE_OFFSET);
    if (Kind == 0 || Kind > 3)
        return SoftplgUnsupported;
    Bytes = SoftplgReadU32(Record + CTL_BYTES);
    if (Bytes != SoftplgKindLayouts[Kind].Bytes)
        return SoftplgUnsupported;
    PrivateSize = SoftplgReadU32(Record + SoftplgKindLayouts[Kind].PrivOffset);
    if (!SoftplgRangeFits(Walk, *Offset, Bytes + PrivateSize))
        return SoftplgTruncated;

    if (Control != NULL)
    {
        static const SOFTPLG_CONTROL Empty;
        *Control = Empty;
        Control->Kind = (SOFTPLG_CONTROL_KIND)Kind;
        SoftplgReadText(Control->Name, sizeof(Control->Name), Record + CTL_NAME_OFFSET, SOFTPLG_NAME_BYTES);
        Control->Access = SoftplgReadU32(Record + CTL_ACCESS_OFFSET);
        Control->Private.Offset = *Offset + Bytes;
        Control->Private.Size = PrivateSize;
        if (Kind == SoftplgControlMixer)
        {
            Control->Minimum = SoftplgReadU32(Record + MIXER_MIN_OFFSET);
            Control->Maximum = SoftplgReadU32(Record + MIXER_MAX_OFFSET);
            Control->PlatformMaximum = SoftplgReadU32(Record + MIXER_PLATFORM_MAX_OFFSET);
            Control->Invert = SoftplgReadU32(Record + MIXER_INVERT_OFFSET);
            Control->Channels = SoftplgReadU32(Record + MIXER_CHANNELS_OFFSET);
        }
        else if (Kind == SoftplgControlBytes)
        {
            Control->Maximum = SoftplgReadU32(Record + BYTES_MAX_OFFSET);
            Control->Mask = SoftplgReadU32(Record + BYTES_MASK_OFFSET);
            Control->Base = SoftplgReadU32(Record + BYTES_BASE_OFFSET);
            Control->Registers = SoftplgReadU32(Record + BYTES_REGS_OFFSET);
        }
        else
        {
            Control->Channels = SoftplgReadU32(Record + ENUM_CHANNELS_OFFSET);
            Control->Items = SoftplgReadU32(Record + ENUM_ITEMS_OFFSET);
            Control->Mask = SoftplgReadU32(Record + ENUM_MASK_OFFSET);
            Control->TextCount = SoftplgReadU32(Record + ENUM_COUNT_OFFSET);
        }
    }
    *Offset += Bytes + PrivateSize;
    return SoftplgOk;
}

/* Steps to element Index of a block of structures of Bytes bytes whose private data size sits at PrivOffset */
static
SOFTPLG_STATUS
SoftplgLocateElement(
    const ELEMENT_WALK* Walk,
    UINT32 Count,
    UINT32 Index,
    UINT32 Bytes,
    UINT32 PrivOffset,
    BOOLEAN WidgetControls,
    _Out_ SOFTPLG_ELEMENT* Element)
{
    SOFTPLG_STATUS Status;
    SIZE_T Offset = Walk->Offset;
    UINT32 Current, PrivateSize, Controls, Control;

    if (Index >= Count)
        return SoftplgNotFound;
    for (Current = 0; ; Current++)
    {
        if (!SoftplgRangeFits(Walk, Offset, Bytes))
            return SoftplgTruncated;
        if (SoftplgReadU32(Walk->Data + Offset) != Bytes)
            return SoftplgUnsupported;
        PrivateSize = SoftplgReadU32(Walk->Data + Offset + PrivOffset);
        if (!SoftplgRangeFits(Walk, Offset, Bytes + PrivateSize))
            return SoftplgTruncated;
        if (Current == Index)
        {
            Element->Offset = Offset;
            Element->Size = Bytes + PrivateSize;
            return SoftplgOk;
        }

        Offset += Bytes + PrivateSize;
        if (WidgetControls)
        {
            Controls = SoftplgReadU32(Walk->Data + Offset - PrivateSize - Bytes + WIDGET_CONTROLS_OFFSET);
            for (Control = 0; Control < Controls; Control++)
            {
                Status = SoftplgStepControl(Walk, &Offset, NULL);
                if (Status != SoftplgOk)
                    return Status;
            }
        }
    }
}

static
SOFTPLG_STATUS
SoftplgElementWalk(
    _In_reads_bytes_(Size) const UINT8* Data,
    SIZE_T Size,
    _In_ const SOFTPLG_BLOCK* Block,
    UINT32 Index,
    UINT32 Bytes,
    UINT32 PrivOffset,
    BOOLEAN WidgetControls,
    _Out_ ELEMENT_WALK* Walk,
    _Out_ SOFTPLG_ELEMENT* Element)
{
    static const ELEMENT_WALK Empty;
    SOFTPLG_STATUS Status;

    *Walk = Empty;
    if (Block->PayloadOffset > Size || Block->NextOffset > Size || Block->NextOffset < Block->PayloadOffset)
        return SoftplgBadHeader;

    Walk->Data = Data;
    Walk->Size = Size;
    Walk->Offset = Block->PayloadOffset;
    Walk->Limit = Block->NextOffset;
    Status = SoftplgLocateElement(Walk, Block->Count, Index, Bytes, PrivOffset, WidgetControls, Element);
    if (Status != SoftplgOk)
        *Walk = Empty;
    return Status;
}

SOFTPLG_STATUS
SoftplgWalk(
    const UINT8* Data,
    SIZE_T Size,
    SOFTPLG_BLOCK_CALLBACK Callback,
    PVOID Context,
    SOFTPLG_SUMMARY* Summary)
{
    static const SOFTPLG_SUMMARY Empty;
    SOFTPLG_BLOCK Block;
    SIZE_T Offset = 0;

    *Summary = Empty;
    if (Data == NULL || Size == 0)
        return SoftplgNoData;
    while (Offset < Size)
    {
        if (!SoftplgInRange(Size, Offset, SOFTPLG_HEADER_BYTES))
            return SoftplgTruncated;
        if (SoftplgReadU32(Data + Offset) != SOFTPLG_MAGIC)
            return Offset == 0 ? SoftplgBadSignature : SoftplgBadHeader;
        if (SoftplgReadU32(Data + Offset + HDR_SIZE_OFFSET) != SOFTPLG_HEADER_BYTES)
            return SoftplgBadHeader;
        if (Offset == 0 && SoftplgReadU32(Data + HDR_ABI_OFFSET) != SOFTPLG_ABI_VERSION)
            return SoftplgUnsupported;

        Block.Abi = SoftplgReadU32(Data + Offset + HDR_ABI_OFFSET);
        Block.Version = SoftplgReadU32(Data + Offset + HDR_VERSION_OFFSET);
        Block.Type = SoftplgReadU32(Data + Offset + HDR_TYPE_OFFSET);
        Block.VendorType = SoftplgReadU32(Data + Offset + HDR_VENDOR_TYPE_OFFSET);
        Block.PayloadSize = SoftplgReadU32(Data + Offset + HDR_PAYLOAD_SIZE_OFFSET);
        Block.Index = SoftplgReadU32(Data + Offset + HDR_INDEX_OFFSET);
        Block.Count = SoftplgReadU32(Data + Offset + HDR_COUNT_OFFSET);
        Block.Offset = Offset;
        Block.PayloadOffset = Offset + SOFTPLG_HEADER_BYTES;
        Block.NextOffset = Block.PayloadOffset + Block.PayloadSize;
        if (Block.NextOffset > Size || Block.NextOffset < Block.PayloadOffset)
            return SoftplgTruncated;

        if (Summary->BlockCount == 0)
        {
            Summary->Abi = Block.Abi;
            Summary->Version = Block.Version;
            Summary->VendorType = Block.VendorType;
        }
        Summary->BlockCount++;
        if (Block.Type < SOFTPLG_TYPE_COUNT)
            Summary->Counts[Block.Type]++;
        if (Block.VendorType != 0)
            Summary->VendorBlocks++;

        if (Callback != NULL && !Callback(&Block, Context))
            break;
        Offset = Block.NextOffset;
    }
    return SoftplgOk;
}

SOFTPLG_STATUS
SoftplgReadBlock(
    const UINT8* Data,
    SIZE_T Size,
    UINT32 Index,
    SOFTPLG_BLOCK* Block)
{
    static const SOFTPLG_BLOCK Empty;
    SOFTPLG_BLOCK Current;
    SIZE_T Offset = 0;
    UINT32 Seen = 0;

    *Block = Empty;
    if (Data == NULL || Size == 0)
        return SoftplgNoData;
    while (Offset < Size)
    {
        if (!SoftplgInRange(Size, Offset, SOFTPLG_HEADER_BYTES))
            return SoftplgTruncated;
        if (SoftplgReadU32(Data + Offset) != SOFTPLG_MAGIC)
            return Offset == 0 ? SoftplgBadSignature : SoftplgBadHeader;
        Current.Abi = SoftplgReadU32(Data + Offset + HDR_ABI_OFFSET);
        Current.Version = SoftplgReadU32(Data + Offset + HDR_VERSION_OFFSET);
        Current.Type = SoftplgReadU32(Data + Offset + HDR_TYPE_OFFSET);
        Current.VendorType = SoftplgReadU32(Data + Offset + HDR_VENDOR_TYPE_OFFSET);
        Current.PayloadSize = SoftplgReadU32(Data + Offset + HDR_PAYLOAD_SIZE_OFFSET);
        Current.Index = SoftplgReadU32(Data + Offset + HDR_INDEX_OFFSET);
        Current.Count = SoftplgReadU32(Data + Offset + HDR_COUNT_OFFSET);
        Current.Offset = Offset;
        Current.PayloadOffset = Offset + SOFTPLG_HEADER_BYTES;
        Current.NextOffset = Current.PayloadOffset + Current.PayloadSize;
        if (Current.NextOffset > Size || Current.NextOffset < Current.PayloadOffset)
            return SoftplgTruncated;
        if (Seen == Index)
        {
            *Block = Current;
            return SoftplgOk;
        }
        Seen++;
        Offset = Current.NextOffset;
    }
    return SoftplgNotFound;
}

SOFTPLG_STATUS
SoftplgReadManifest(
    const UINT8* Data,
    SIZE_T Size,
    const SOFTPLG_BLOCK* Block,
    SOFTPLG_MANIFEST* Manifest)
{
    static const SOFTPLG_MANIFEST Empty;
    ELEMENT_WALK Walk;
    SOFTPLG_ELEMENT Element;
    SOFTPLG_STATUS Status;
    const UINT8* Record;

    *Manifest = Empty;
    Status = SoftplgElementWalk(Data, Size, Block, 0, MANIFEST_BYTES, MANIFEST_PRIV_OFFSET, FALSE, &Walk, &Element);
    if (Status != SoftplgOk)
        return Status;
    Record = Data + Element.Offset;
    Manifest->ControlElements = SoftplgReadU32(Record + MANIFEST_CONTROLS_OFFSET);
    Manifest->WidgetElements = SoftplgReadU32(Record + MANIFEST_WIDGETS_OFFSET);
    Manifest->GraphElements = SoftplgReadU32(Record + MANIFEST_GRAPH_OFFSET);
    Manifest->PcmElements = SoftplgReadU32(Record + MANIFEST_PCM_OFFSET);
    Manifest->DaiLinkElements = SoftplgReadU32(Record + MANIFEST_LINKS_OFFSET);
    Manifest->DaiElements = SoftplgReadU32(Record + MANIFEST_DAI_OFFSET);
    Manifest->Private.Offset = Element.Offset + MANIFEST_BYTES;
    Manifest->Private.Size = Element.Size - MANIFEST_BYTES;
    return SoftplgOk;
}

SOFTPLG_STATUS
SoftplgReadControl(
    const UINT8* Data,
    SIZE_T Size,
    const SOFTPLG_BLOCK* Block,
    UINT32 Index,
    SOFTPLG_CONTROL* Control)
{
    static const SOFTPLG_CONTROL Empty;
    ELEMENT_WALK Walk;
    SOFTPLG_STATUS Status;
    SIZE_T Offset;
    UINT32 Current;

    *Control = Empty;
    if (Block->PayloadOffset > Size || Block->NextOffset > Size || Block->NextOffset < Block->PayloadOffset)
        return SoftplgBadHeader;
    if (Index >= Block->Count)
        return SoftplgNotFound;

    Walk.Data = Data;
    Walk.Size = Size;
    Walk.Offset = Block->PayloadOffset;
    Walk.Limit = Block->NextOffset;
    Offset = Walk.Offset;
    for (Current = 0; ; Current++)
    {
        Status = SoftplgStepControl(&Walk, &Offset, Current == Index ? Control : NULL);
        if (Status != SoftplgOk)
        {
            *Control = Empty;
            return Status;
        }
        if (Current == Index)
            return SoftplgOk;
    }
}

SOFTPLG_STATUS
SoftplgReadGraphEdge(
    const UINT8* Data,
    SIZE_T Size,
    const SOFTPLG_BLOCK* Block,
    UINT32 Index,
    SOFTPLG_GRAPH_EDGE* Edge)
{
    static const SOFTPLG_GRAPH_EDGE Empty;
    const UINT8* Record;
    SIZE_T Offset;

    *Edge = Empty;
    if (Index >= Block->Count)
        return SoftplgNotFound;
    Offset = Block->PayloadOffset + (SIZE_T)Index * GRAPH_BYTES;
    if (Block->PayloadOffset > Size || Offset + GRAPH_BYTES > Block->NextOffset ||
        !SoftplgInRange(Size, Offset, GRAPH_BYTES))
        return SoftplgTruncated;

    Record = Data + Offset;
    SoftplgReadText(Edge->Sink, sizeof(Edge->Sink), Record + GRAPH_SINK_OFFSET, SOFTPLG_NAME_BYTES);
    SoftplgReadText(Edge->Control, sizeof(Edge->Control), Record + GRAPH_CONTROL_OFFSET, SOFTPLG_NAME_BYTES);
    SoftplgReadText(Edge->Source, sizeof(Edge->Source), Record + GRAPH_SOURCE_OFFSET, SOFTPLG_NAME_BYTES);
    return SoftplgOk;
}

SOFTPLG_STATUS
SoftplgReadWidget(
    const UINT8* Data,
    SIZE_T Size,
    const SOFTPLG_BLOCK* Block,
    UINT32 Index,
    SOFTPLG_WIDGET* Widget)
{
    static const SOFTPLG_WIDGET Empty;
    ELEMENT_WALK Walk;
    SOFTPLG_ELEMENT Element;
    SOFTPLG_STATUS Status;
    const UINT8* Record;

    *Widget = Empty;
    Status = SoftplgElementWalk(Data, Size, Block, Index, WIDGET_BYTES, WIDGET_PRIV_OFFSET, TRUE, &Walk, &Element);
    if (Status != SoftplgOk)
        return Status;
    Record = Data + Element.Offset;
    Widget->Id = SoftplgReadU32(Record + WIDGET_ID_OFFSET);
    SoftplgReadText(Widget->Name, sizeof(Widget->Name), Record + WIDGET_NAME_OFFSET, SOFTPLG_NAME_BYTES);
    SoftplgReadText(Widget->SName, sizeof(Widget->SName), Record + WIDGET_SNAME_OFFSET, SOFTPLG_NAME_BYTES);
    Widget->Register = SoftplgReadU32(Record + WIDGET_REG_OFFSET);
    Widget->Shift = SoftplgReadU32(Record + WIDGET_SHIFT_OFFSET);
    Widget->Mask = SoftplgReadU32(Record + WIDGET_MASK_OFFSET);
    Widget->Subseq = SoftplgReadU32(Record + WIDGET_SUBSEQ_OFFSET);
    Widget->Invert = SoftplgReadU32(Record + WIDGET_INVERT_OFFSET);
    Widget->IgnoreSuspend = SoftplgReadU32(Record + WIDGET_IGNORE_OFFSET);
    Widget->EventFlags = SoftplgReadU16(Record + WIDGET_EVENT_FLAGS_OFFSET);
    Widget->EventType = SoftplgReadU16(Record + WIDGET_EVENT_TYPE_OFFSET);
    Widget->ControlCount = SoftplgReadU32(Record + WIDGET_CONTROLS_OFFSET);
    Widget->Private.Offset = Element.Offset + WIDGET_BYTES;
    Widget->Private.Size = Element.Size - WIDGET_BYTES;
    Widget->ControlsOffset = Element.Offset + Element.Size;
    return SoftplgOk;
}

SOFTPLG_STATUS
SoftplgReadWidgetControl(
    const UINT8* Data,
    SIZE_T Size,
    const SOFTPLG_BLOCK* Block,
    UINT32 WidgetIndex,
    UINT32 ControlIndex,
    SOFTPLG_CONTROL* Control)
{
    static const SOFTPLG_CONTROL Empty;
    SOFTPLG_WIDGET Widget;
    ELEMENT_WALK Walk;
    SOFTPLG_ELEMENT Element;
    SOFTPLG_STATUS Status;
    SIZE_T Offset;
    UINT32 Current;

    *Control = Empty;
    Status = SoftplgElementWalk(Data, Size, Block, WidgetIndex, WIDGET_BYTES, WIDGET_PRIV_OFFSET, TRUE, &Walk, &Element);
    if (Status != SoftplgOk)
        return Status;
    Widget.ControlCount = SoftplgReadU32(Data + Element.Offset + WIDGET_CONTROLS_OFFSET);
    if (ControlIndex >= Widget.ControlCount)
        return SoftplgNotFound;

    Offset = Element.Offset + Element.Size;
    for (Current = 0; ; Current++)
    {
        Status = SoftplgStepControl(&Walk, &Offset, Current == ControlIndex ? Control : NULL);
        if (Status != SoftplgOk)
        {
            *Control = Empty;
            return Status;
        }
        if (Current == ControlIndex)
            return SoftplgOk;
    }
}

static
void
SoftplgReadStream(
    _In_reads_bytes_(STREAM_BYTES) const UINT8* Record,
    _Out_ SOFTPLG_STREAM* Stream)
{
    static const SOFTPLG_STREAM Empty;

    *Stream = Empty;
    SoftplgReadText(Stream->Name, sizeof(Stream->Name), Record + STREAM_NAME_OFFSET, SOFTPLG_NAME_BYTES);
    Stream->Format = SoftplgReadU64(Record + STREAM_FORMAT_OFFSET);
    Stream->Rate = SoftplgReadU32(Record + STREAM_RATE_OFFSET);
    Stream->PeriodBytes = SoftplgReadU32(Record + STREAM_PERIOD_OFFSET);
    Stream->BufferBytes = SoftplgReadU32(Record + STREAM_BUFFER_OFFSET);
    Stream->Channels = SoftplgReadU32(Record + STREAM_CHANNELS_OFFSET);
}

static
void
SoftplgReadCaps(
    _In_reads_bytes_(CAPS_BYTES) const UINT8* Record,
    _Out_ SOFTPLG_STREAM_CAPS* Caps)
{
    static const SOFTPLG_STREAM_CAPS Empty;

    *Caps = Empty;
    SoftplgReadText(Caps->Name, sizeof(Caps->Name), Record + CAPS_NAME_OFFSET, SOFTPLG_NAME_BYTES);
    Caps->Formats = SoftplgReadU64(Record + CAPS_FORMATS_OFFSET);
    Caps->Rates = SoftplgReadU32(Record + CAPS_RATES_OFFSET);
    Caps->RateMin = SoftplgReadU32(Record + CAPS_RATE_MIN_OFFSET);
    Caps->RateMax = SoftplgReadU32(Record + CAPS_RATE_MAX_OFFSET);
    Caps->ChannelsMin = SoftplgReadU32(Record + CAPS_CHANNELS_MIN_OFFSET);
    Caps->ChannelsMax = SoftplgReadU32(Record + CAPS_CHANNELS_MAX_OFFSET);
    Caps->PeriodsMin = SoftplgReadU32(Record + CAPS_PERIODS_MIN_OFFSET);
    Caps->PeriodsMax = SoftplgReadU32(Record + CAPS_PERIODS_MAX_OFFSET);
    Caps->PeriodSizeMin = SoftplgReadU32(Record + CAPS_PERIOD_SIZE_MIN_OFFSET);
    Caps->PeriodSizeMax = SoftplgReadU32(Record + CAPS_PERIOD_SIZE_MAX_OFFSET);
    Caps->BufferSizeMin = SoftplgReadU32(Record + CAPS_BUFFER_MIN_OFFSET);
    Caps->BufferSizeMax = SoftplgReadU32(Record + CAPS_BUFFER_MAX_OFFSET);
    Caps->SigBits = SoftplgReadU32(Record + CAPS_SIG_BITS_OFFSET);
}

SOFTPLG_STATUS
SoftplgReadPcm(
    const UINT8* Data,
    SIZE_T Size,
    const SOFTPLG_BLOCK* Block,
    UINT32 Index,
    SOFTPLG_PCM* Pcm)
{
    static const SOFTPLG_PCM Empty;
    ELEMENT_WALK Walk;
    SOFTPLG_ELEMENT Element;
    SOFTPLG_STATUS Status;
    const UINT8* Record;

    *Pcm = Empty;
    Status = SoftplgElementWalk(Data, Size, Block, Index, PCM_BYTES, PCM_PRIV_OFFSET, FALSE, &Walk, &Element);
    if (Status != SoftplgOk)
        return Status;
    Record = Data + Element.Offset;
    SoftplgReadText(Pcm->PcmName, sizeof(Pcm->PcmName), Record + PCM_NAME_OFFSET, SOFTPLG_NAME_BYTES);
    SoftplgReadText(Pcm->DaiName, sizeof(Pcm->DaiName), Record + PCM_DAI_NAME_OFFSET, SOFTPLG_NAME_BYTES);
    Pcm->PcmId = SoftplgReadU32(Record + PCM_ID_OFFSET);
    Pcm->DaiId = SoftplgReadU32(Record + PCM_DAI_ID_OFFSET);
    Pcm->Playback = SoftplgReadU32(Record + PCM_PLAYBACK_OFFSET);
    Pcm->Capture = SoftplgReadU32(Record + PCM_CAPTURE_OFFSET);
    Pcm->Compress = SoftplgReadU32(Record + PCM_COMPRESS_OFFSET);
    Pcm->StreamCount = SoftplgReadU32(Record + PCM_STREAM_COUNT_OFFSET);
    Pcm->FlagMask = SoftplgReadU32(Record + PCM_FLAG_MASK_OFFSET);
    Pcm->Flags = SoftplgReadU32(Record + PCM_FLAGS_OFFSET);
    Pcm->Private.Offset = Element.Offset + PCM_BYTES;
    Pcm->Private.Size = Element.Size - PCM_BYTES;
    return SoftplgOk;
}

SOFTPLG_STATUS
SoftplgReadPcmStream(
    const UINT8* Data,
    SIZE_T Size,
    const SOFTPLG_BLOCK* Block,
    UINT32 PcmIndex,
    UINT32 StreamIndex,
    SOFTPLG_STREAM* Stream)
{
    static const SOFTPLG_STREAM Empty;
    ELEMENT_WALK Walk;
    SOFTPLG_ELEMENT Element;
    SOFTPLG_STATUS Status;
    SIZE_T Offset;

    *Stream = Empty;
    if (StreamIndex >= SOFTPLG_STREAMS_MAX)
        return SoftplgNotFound;
    Status = SoftplgElementWalk(Data, Size, Block, PcmIndex, PCM_BYTES, PCM_PRIV_OFFSET, FALSE, &Walk, &Element);
    if (Status != SoftplgOk)
        return Status;
    if (StreamIndex >= SoftplgReadU32(Data + Element.Offset + PCM_STREAM_COUNT_OFFSET))
        return SoftplgNotFound;
    Offset = Element.Offset + PCM_STREAMS_OFFSET + (SIZE_T)StreamIndex * STREAM_BYTES;
    if (!SoftplgRangeFits(&Walk, Offset, STREAM_BYTES))
        return SoftplgTruncated;
    SoftplgReadStream(Data + Offset, Stream);
    return SoftplgOk;
}

SOFTPLG_STATUS
SoftplgReadPcmCaps(
    const UINT8* Data,
    SIZE_T Size,
    const SOFTPLG_BLOCK* Block,
    UINT32 PcmIndex,
    UINT32 CapsIndex,
    SOFTPLG_STREAM_CAPS* Caps)
{
    static const SOFTPLG_STREAM_CAPS Empty;
    ELEMENT_WALK Walk;
    SOFTPLG_ELEMENT Element;
    SOFTPLG_STATUS Status;
    SIZE_T Offset;

    *Caps = Empty;
    if (CapsIndex >= SOFTPLG_CAPS_MAX)
        return SoftplgNotFound;
    Status = SoftplgElementWalk(Data, Size, Block, PcmIndex, PCM_BYTES, PCM_PRIV_OFFSET, FALSE, &Walk, &Element);
    if (Status != SoftplgOk)
        return Status;
    Offset = Element.Offset + PCM_CAPS_OFFSET + (SIZE_T)CapsIndex * CAPS_BYTES;
    if (!SoftplgRangeFits(&Walk, Offset, CAPS_BYTES))
        return SoftplgTruncated;
    SoftplgReadCaps(Data + Offset, Caps);
    return SoftplgOk;
}

SOFTPLG_STATUS
SoftplgReadLink(
    const UINT8* Data,
    SIZE_T Size,
    const SOFTPLG_BLOCK* Block,
    UINT32 Index,
    SOFTPLG_LINK* Link)
{
    static const SOFTPLG_LINK Empty;
    ELEMENT_WALK Walk;
    SOFTPLG_ELEMENT Element;
    SOFTPLG_STATUS Status;
    const UINT8* Record;

    *Link = Empty;
    Status = SoftplgElementWalk(Data, Size, Block, Index, LINK_BYTES, LINK_PRIV_OFFSET, FALSE, &Walk, &Element);
    if (Status != SoftplgOk)
        return Status;
    Record = Data + Element.Offset;
    Link->Id = SoftplgReadU32(Record + LINK_ID_OFFSET);
    SoftplgReadText(Link->Name, sizeof(Link->Name), Record + LINK_NAME_OFFSET, SOFTPLG_NAME_BYTES);
    SoftplgReadText(Link->StreamName, sizeof(Link->StreamName), Record + LINK_STREAM_NAME_OFFSET, SOFTPLG_NAME_BYTES);
    Link->StreamCount = SoftplgReadU32(Record + LINK_STREAM_COUNT_OFFSET);
    Link->HwConfigCount = SoftplgReadU32(Record + LINK_HW_CONFIG_COUNT_OFFSET);
    Link->DefaultHwConfigId = SoftplgReadU32(Record + LINK_DEFAULT_HW_OFFSET);
    Link->FlagMask = SoftplgReadU32(Record + LINK_FLAG_MASK_OFFSET);
    Link->Flags = SoftplgReadU32(Record + LINK_FLAGS_OFFSET);
    Link->Private.Offset = Element.Offset + LINK_BYTES;
    Link->Private.Size = Element.Size - LINK_BYTES;
    return SoftplgOk;
}

SOFTPLG_STATUS
SoftplgReadLinkStream(
    const UINT8* Data,
    SIZE_T Size,
    const SOFTPLG_BLOCK* Block,
    UINT32 LinkIndex,
    UINT32 StreamIndex,
    SOFTPLG_STREAM* Stream)
{
    static const SOFTPLG_STREAM Empty;
    ELEMENT_WALK Walk;
    SOFTPLG_ELEMENT Element;
    SOFTPLG_STATUS Status;
    SIZE_T Offset;

    *Stream = Empty;
    if (StreamIndex >= SOFTPLG_STREAMS_MAX)
        return SoftplgNotFound;
    Status = SoftplgElementWalk(Data, Size, Block, LinkIndex, LINK_BYTES, LINK_PRIV_OFFSET, FALSE, &Walk, &Element);
    if (Status != SoftplgOk)
        return Status;
    if (StreamIndex >= SoftplgReadU32(Data + Element.Offset + LINK_STREAM_COUNT_OFFSET))
        return SoftplgNotFound;
    Offset = Element.Offset + LINK_STREAMS_OFFSET + (SIZE_T)StreamIndex * STREAM_BYTES;
    if (!SoftplgRangeFits(&Walk, Offset, STREAM_BYTES))
        return SoftplgTruncated;
    SoftplgReadStream(Data + Offset, Stream);
    return SoftplgOk;
}

SOFTPLG_STATUS
SoftplgReadLinkHwConfig(
    const UINT8* Data,
    SIZE_T Size,
    const SOFTPLG_BLOCK* Block,
    UINT32 LinkIndex,
    UINT32 ConfigIndex,
    SOFTPLG_HW_CONFIG* Config)
{
    static const SOFTPLG_HW_CONFIG Empty;
    ELEMENT_WALK Walk;
    SOFTPLG_ELEMENT Element;
    SOFTPLG_STATUS Status;
    const UINT8* Record;
    SIZE_T Offset;

    *Config = Empty;
    if (ConfigIndex >= SOFTPLG_HW_CONFIGS_MAX)
        return SoftplgNotFound;
    Status = SoftplgElementWalk(Data, Size, Block, LinkIndex, LINK_BYTES, LINK_PRIV_OFFSET, FALSE, &Walk, &Element);
    if (Status != SoftplgOk)
        return Status;
    if (ConfigIndex >= SoftplgReadU32(Data + Element.Offset + LINK_HW_CONFIG_COUNT_OFFSET))
        return SoftplgNotFound;
    Offset = Element.Offset + LINK_HW_CONFIGS_OFFSET + (SIZE_T)ConfigIndex * HW_CONFIG_BYTES;
    if (!SoftplgRangeFits(&Walk, Offset, HW_CONFIG_BYTES))
        return SoftplgTruncated;

    Record = Data + Offset;
    Config->Id = SoftplgReadU32(Record + HW_ID_OFFSET);
    Config->Format = SoftplgReadU32(Record + HW_FORMAT_OFFSET);
    Config->ClockGated = Record[HW_CLOCK_GATED_OFFSET];
    Config->InvertBclk = Record[HW_INVERT_BCLK_OFFSET];
    Config->InvertFsync = Record[HW_INVERT_FSYNC_OFFSET];
    Config->BclkProvider = Record[HW_BCLK_PROVIDER_OFFSET];
    Config->FsyncProvider = Record[HW_FSYNC_PROVIDER_OFFSET];
    Config->MclkDirection = Record[HW_MCLK_DIRECTION_OFFSET];
    Config->MclkRate = SoftplgReadU32(Record + HW_MCLK_RATE_OFFSET);
    Config->BclkRate = SoftplgReadU32(Record + HW_BCLK_RATE_OFFSET);
    Config->FsyncRate = SoftplgReadU32(Record + HW_FSYNC_RATE_OFFSET);
    Config->TdmSlots = SoftplgReadU32(Record + HW_TDM_SLOTS_OFFSET);
    Config->TdmSlotWidth = SoftplgReadU32(Record + HW_TDM_SLOT_WIDTH_OFFSET);
    Config->TxSlots = SoftplgReadU32(Record + HW_TX_SLOTS_OFFSET);
    Config->RxSlots = SoftplgReadU32(Record + HW_RX_SLOTS_OFFSET);
    Config->TxChannels = SoftplgReadU32(Record + HW_TX_CHANNELS_OFFSET);
    Config->RxChannels = SoftplgReadU32(Record + HW_RX_CHANNELS_OFFSET);
    return SoftplgOk;
}

SOFTPLG_STATUS
SoftplgReadDai(
    const UINT8* Data,
    SIZE_T Size,
    const SOFTPLG_BLOCK* Block,
    UINT32 Index,
    SOFTPLG_DAI* Dai)
{
    static const SOFTPLG_DAI Empty;
    ELEMENT_WALK Walk;
    SOFTPLG_ELEMENT Element;
    SOFTPLG_STATUS Status;
    const UINT8* Record;

    *Dai = Empty;
    Status = SoftplgElementWalk(Data, Size, Block, Index, DAI_BYTES, DAI_PRIV_OFFSET, FALSE, &Walk, &Element);
    if (Status != SoftplgOk)
        return Status;
    Record = Data + Element.Offset;
    SoftplgReadText(Dai->DaiName, sizeof(Dai->DaiName), Record + DAI_NAME_OFFSET, SOFTPLG_NAME_BYTES);
    Dai->DaiId = SoftplgReadU32(Record + DAI_ID_OFFSET);
    Dai->Playback = SoftplgReadU32(Record + DAI_PLAYBACK_OFFSET);
    Dai->Capture = SoftplgReadU32(Record + DAI_CAPTURE_OFFSET);
    Dai->FlagMask = SoftplgReadU32(Record + DAI_FLAG_MASK_OFFSET);
    Dai->Flags = SoftplgReadU32(Record + DAI_FLAGS_OFFSET);
    Dai->Private.Offset = Element.Offset + DAI_BYTES;
    Dai->Private.Size = Element.Size - DAI_BYTES;
    return SoftplgOk;
}

SOFTPLG_STATUS
SoftplgReadDaiCaps(
    const UINT8* Data,
    SIZE_T Size,
    const SOFTPLG_BLOCK* Block,
    UINT32 DaiIndex,
    UINT32 CapsIndex,
    SOFTPLG_STREAM_CAPS* Caps)
{
    static const SOFTPLG_STREAM_CAPS Empty;
    ELEMENT_WALK Walk;
    SOFTPLG_ELEMENT Element;
    SOFTPLG_STATUS Status;
    SIZE_T Offset;

    *Caps = Empty;
    if (CapsIndex >= SOFTPLG_CAPS_MAX)
        return SoftplgNotFound;
    Status = SoftplgElementWalk(Data, Size, Block, DaiIndex, DAI_BYTES, DAI_PRIV_OFFSET, FALSE, &Walk, &Element);
    if (Status != SoftplgOk)
        return Status;
    Offset = Element.Offset + DAI_CAPS_OFFSET + (SIZE_T)CapsIndex * CAPS_BYTES;
    if (!SoftplgRangeFits(&Walk, Offset, CAPS_BYTES))
        return SoftplgTruncated;
    SoftplgReadCaps(Data + Offset, Caps);
    return SoftplgOk;
}

SOFTPLG_STATUS
SoftplgTupleBegin(
    const UINT8* Data,
    SIZE_T Size,
    const SOFTPLG_PRIVATE* Private,
    SOFTPLG_TUPLE_ITER* Iterator)
{
    static const SOFTPLG_TUPLE_ITER Empty;

    *Iterator = Empty;
    if (Private->Offset > Size || Private->Size > Size - Private->Offset)
        return SoftplgTruncated;
    Iterator->Data = Data;
    Iterator->Size = Size;
    Iterator->Offset = Private->Offset;
    Iterator->Start = Private->Offset;
    Iterator->End = Private->Offset + Private->Size;
    return SoftplgOk;
}

/* Where the private data of the element begins, which is also where its first array begins */
static
SIZE_T
PrivateStart(
    _In_ const SOFTPLG_TUPLE_ITER* Iterator)
{
    return Iterator->Start;
}

SOFTPLG_STATUS
SoftplgTupleNext(
    SOFTPLG_TUPLE_ITER* Iterator,
    SOFTPLG_TUPLE* Tuple)
{
    static const SOFTPLG_TUPLE Empty;
    static const SIZE_T ElementBytes[6] = { 20, 48, 8, 8, 8, 8 };
    const UINT8* Array;
    const UINT8* Record;
    UINT32 ArrayBytes, Count;
    SIZE_T Index;

    *Tuple = Empty;
    for (;;)
    {
        if (Iterator->Elements != 0 && Iterator->Element < Iterator->Elements)
        {
            Record = Iterator->Data + Iterator->ArrayOffset + ARRAY_HEADER_BYTES +
                Iterator->Element * ElementBytes[Iterator->Type];
            Tuple->Type = Iterator->Type;
            Tuple->Token = SoftplgReadU32(Record + TUPLE_TOKEN_OFFSET);
            if (Iterator->Type == SOFTPLG_TUPLE_UUID)
            {
                for (Index = 0; Index < SOFTPLG_UUID_BYTES; Index++)
                    Tuple->Uuid[Index] = Record[TUPLE_UUID_OFFSET + Index];
            }
            else if (Iterator->Type == SOFTPLG_TUPLE_STRING)
            {
                SoftplgReadText(Tuple->String, sizeof(Tuple->String), Record + TUPLE_STRING_OFFSET, TUPLE_STRING_BYTES);
            }
            else
            {
                Tuple->Value = SoftplgReadU32(Record + TUPLE_VALUE_OFFSET);
            }
            Iterator->Element++;
            return SoftplgOk;
        }

        if (Iterator->Offset >= Iterator->End)
            return SoftplgNotFound;
        if (!SoftplgInRange(Iterator->Size, Iterator->Offset, ARRAY_HEADER_BYTES) ||
            Iterator->End - Iterator->Offset < ARRAY_HEADER_BYTES)
            return Iterator->Offset == PrivateStart(Iterator) ? SoftplgUnsupported : SoftplgTruncated;

        Array = Iterator->Data + Iterator->Offset;
        /* Private data that is not a tuple array: the SOF bytes control configuration blob starts with the text SOF */
        if (Iterator->Offset == PrivateStart(Iterator) && Array[0] == 'S' && Array[1] == 'O' && Array[2] == 'F')
            return SoftplgUnsupported;
        ArrayBytes = SoftplgReadU32(Array + ARRAY_SIZE_OFFSET);
        Iterator->Type = SoftplgReadU32(Array + ARRAY_TYPE_OFFSET);
        Count = SoftplgReadU32(Array + ARRAY_COUNT_OFFSET);
        if (Iterator->Type > SOFTPLG_TUPLE_SHORT)
            return Iterator->Offset == PrivateStart(Iterator) ? SoftplgUnsupported : SoftplgTruncated;
        if (ArrayBytes < ARRAY_HEADER_BYTES || Count > (ArrayBytes - ARRAY_HEADER_BYTES) / ElementBytes[Iterator->Type])
            return SoftplgTruncated;
        if (ArrayBytes > Iterator->End - Iterator->Offset)
            return SoftplgTruncated;

        Iterator->ArrayOffset = Iterator->Offset;
        Iterator->Elements = Count;
        Iterator->Element = 0;
        Iterator->Offset += ArrayBytes;
    }
}

const char* SoftplgStatusName(
    SOFTPLG_STATUS Status)
{
    static const char* const Names[] =
    {
        "ok", "no data", "bad signature", "bad header", "truncated", "not found", "unsupported"
    };

    if ((UINT32)Status >= sizeof(Names) / sizeof(Names[0]))
        return "unknown";
    return Names[Status];
}

const char* SoftplgTypeName(
    UINT32 Type)
{
    static const char* const Names[SOFTPLG_TYPE_COUNT] =
    {
        "NONE", "MIXER", "BYTES", "ENUM", "DAPM_GRAPH", "DAPM_WIDGET", "DAI_LINK", "PCM", "MANIFEST",
        "CODEC_LINK", "BACKEND_LINK", "PDATA", "DAI"
    };

    if (Type >= SOFTPLG_TYPE_COUNT)
        return "VENDOR";
    return Names[Type];
}
