/*
 * PROJECT:     LiberNT audio driver libraries
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Builds an IO-free model of an IPC4 topology for the DSP driver (the manifest, the pipelines, the widgets with their module ids, formats and configurations, the PCMs with the widgets each one reaches) and checks it against the firmware's limits
 * COPYRIGHT:   Copyright 2026 LiberNT contributors
 */

/* The sources of every number are named in ipc4model.h */

#include <ntddk.h>
#include "ipc4model.h"

/* The manifest's private data: the ABI header's three 16-bit numbers, the 16-bit item count, then items of a type, a size and data */
#define MANIFEST_ABI_MAJOR_OFFSET   0
#define MANIFEST_ABI_MINOR_OFFSET   2
#define MANIFEST_ABI_PATCH_OFFSET   4
#define MANIFEST_ITEM_TYPE_OFFSET   0
#define MANIFEST_ITEM_SIZE_OFFSET   4

/* The six tokens a format needs before it can be sent, and the three that repeat what its configuration word says */
#define FORMAT_REQUIRED_MASK        ((1u << SOFTPLG_FORMAT_RATE) | (1u << SOFTPLG_FORMAT_DEPTH) | (1u << SOFTPLG_FORMAT_CH_MAP) | \
                                     (1u << SOFTPLG_FORMAT_CH_CFG) | (1u << SOFTPLG_FORMAT_INTERLEAVING) | (1u << SOFTPLG_FORMAT_CFG))
#define FORMAT_ECHOED_MASK          ((1u << SOFTPLG_FORMAT_VALID) | (1u << SOFTPLG_FORMAT_CHANNELS) | (1u << SOFTPLG_FORMAT_SAMPLE_TYPE))

typedef struct _MODEL_COUNTS
{
    UINT32 Widgets;
    UINT32 Edges;
    UINT32 Pcms;
    UINT32 Pipelines;
    UINT32 Formats;
    UINT32 Controls;
    BOOLEAN Manifest;
} MODEL_COUNTS;

/* What the tuple walker of one widget counts before it can place the formats: side 0 is the input, side 1 the output */
typedef struct _WIDGET_TUPLES
{
    UINT32 InputFormats;
    UINT32 OutputFormats;
    UINT32 Seen[2][SOFTPLG_TOKEN_FORMAT_COUNT];
    UINT32 BufferSeen[2];
} WIDGET_TUPLES;

static
UINT16
ModelReadU16(
    const UINT8* Data)
{
    return (UINT16)(Data[0] | ((UINT16)Data[1] << 8));
}

static
UINT32
ModelReadU32(
    const UINT8* Data)
{
    return (UINT32)Data[0] | ((UINT32)Data[1] << 8) | ((UINT32)Data[2] << 16) |
        ((UINT32)Data[3] << 24);
}

static
void
ModelCopyText(
    CHAR* Destination,
    SIZE_T Capacity,
    const CHAR* Source)
{
    SIZE_T Index;

    for (Index = 0; Source[Index] != 0 && Index + 1 < Capacity; Index++)
        Destination[Index] = Source[Index];
    Destination[Index] = 0;
}

static
BOOLEAN
ModelTextEqual(
    const CHAR* Left,
    const CHAR* Right)
{
    SIZE_T Index = 0;

    while (Left[Index] != 0 && Left[Index] == Right[Index])
        Index++;
    return Left[Index] == 0 && Right[Index] == 0;
}

static
BOOLEAN
ModelIsCopier(
    SOFTPLG_IPC4_KIND Kind)
{
    return Kind == SoftplgKindHostCopierPlayback || Kind == SoftplgKindHostCopierCapture ||
        Kind == SoftplgKindDaiCopierPlayback || Kind == SoftplgKindDaiCopierCapture || Kind == SoftplgKindModuleCopier;
}

static
BOOLEAN
ModelIsModule(
    SOFTPLG_IPC4_KIND Kind)
{
    return ModelIsCopier(Kind) || Kind == SoftplgKindProcessor;
}

BOOLEAN
SoftplgModelIsGain(
    const SOFTPLG_IPC4_WIDGET* Widget)
{
    static const UINT8 GainUuid[SOFTPLG_UUID_BYTES] = SOFTPLG_GAIN_UUID;
    UINT32 I;

    if (Widget == NULL || Widget->Kind != SoftplgKindProcessor || !Widget->HasUuid)
        return FALSE;
    for (I = 0; I < SOFTPLG_UUID_BYTES; I++)
    {
        if (Widget->Uuid[I] != GainUuid[I])
            return FALSE;
    }
    return TRUE;
}

/* The kind a widget type id names; FALSE for every type the model does not read */
static
BOOLEAN
ModelWidgetKind(
    UINT32 Type,
    SOFTPLG_IPC4_KIND* Kind)
{
    switch (Type)
    {
        case SOFTPLG_WIDGET_INPUT:
        case SOFTPLG_WIDGET_OUTPUT:
        case SOFTPLG_WIDGET_OUT_DRV:
            *Kind = SoftplgKindNone;
            return TRUE;
        case SOFTPLG_WIDGET_SCHEDULER:
            *Kind = SoftplgKindScheduler;
            return TRUE;
        case SOFTPLG_WIDGET_AIF_IN:
            *Kind = SoftplgKindHostCopierPlayback;
            return TRUE;
        case SOFTPLG_WIDGET_AIF_OUT:
            *Kind = SoftplgKindHostCopierCapture;
            return TRUE;
        case SOFTPLG_WIDGET_DAI_IN:
            *Kind = SoftplgKindDaiCopierPlayback;
            return TRUE;
        case SOFTPLG_WIDGET_DAI_OUT:
            *Kind = SoftplgKindDaiCopierCapture;
            return TRUE;
        case SOFTPLG_WIDGET_BUFFER:
            *Kind = SoftplgKindModuleCopier;
            return TRUE;
        case SOFTPLG_WIDGET_MIXER:
        case SOFTPLG_WIDGET_PGA:
        case SOFTPLG_WIDGET_EFFECT:
        case SOFTPLG_WIDGET_SRC:
        case SOFTPLG_WIDGET_ASRC:
            *Kind = SoftplgKindProcessor;
            return TRUE;
        default:
            return FALSE;
    }
}

const char*
SoftplgWidgetKindName(
    SOFTPLG_IPC4_KIND Kind)
{
    switch (Kind)
    {
        case SoftplgKindNone: return "pin";
        case SoftplgKindScheduler: return "scheduler";
        case SoftplgKindHostCopierPlayback: return "host copier playback";
        case SoftplgKindHostCopierCapture: return "host copier capture";
        case SoftplgKindDaiCopierPlayback: return "DAI copier playback";
        case SoftplgKindDaiCopierCapture: return "DAI copier capture";
        case SoftplgKindModuleCopier: return "module copier";
        case SoftplgKindProcessor: return "processor";
        default: return "unknown";
    }
}

SOFTPLG_MODEL_STATUS
SoftplgIpc4ReadManifest(
    const UINT8* Data,
    SIZE_T Size,
    const SOFTPLG_BLOCK* Block,
    SOFTPLG_IPC4_MANIFEST* Manifest)
{
    static const SOFTPLG_IPC4_MANIFEST Empty;
    SOFTPLG_MANIFEST Container;
    const UINT8* Private;
    SIZE_T Offset, Remaining;
    UINT32 Count, Index, Type, ItemSize;
    SOFTPLG_STATUS Status;

    *Manifest = Empty;
    Status = SoftplgReadManifest(Data, Size, Block, &Container);
    if (Status != SoftplgOk)
        return Status == SoftplgTruncated || Status == SoftplgBadHeader ? SoftplgModelManifestTruncated : SoftplgModelBadFile;
    Manifest->Nhlt.Offset = Container.Private.Offset;
    if (Container.Private.Size < SOFTPLG_IPC4_MANIFEST_ABI_BYTES)
        return SoftplgModelManifestTruncated;

    Private = Data + Container.Private.Offset;
    Manifest->Major = ModelReadU16(Private + MANIFEST_ABI_MAJOR_OFFSET);
    Manifest->Minor = ModelReadU16(Private + MANIFEST_ABI_MINOR_OFFSET);
    Manifest->Patch = ModelReadU16(Private + MANIFEST_ABI_PATCH_OFFSET);
    if (Manifest->Major != SOFTPLG_IPC4_ABI_MAJOR || Manifest->Minor != SOFTPLG_IPC4_ABI_MINOR)
        return SoftplgModelAbiMismatch;
    /* the ABI version alone is a whole manifest */
    if (Container.Private.Size == SOFTPLG_IPC4_MANIFEST_ABI_BYTES)
        return SoftplgModelOk;
    if (Container.Private.Size < SOFTPLG_IPC4_MANIFEST_COUNT_OFFSET + sizeof(UINT16))
        return SoftplgModelManifestTruncated;

    Count = ModelReadU16(Private + SOFTPLG_IPC4_MANIFEST_COUNT_OFFSET);
    Offset = SOFTPLG_IPC4_MANIFEST_COUNT_OFFSET + sizeof(UINT16);
    for (Index = 0; Index < Count; Index++)
    {
        Remaining = Container.Private.Size - Offset;
        if (Remaining < SOFTPLG_IPC4_MANIFEST_ITEM_HEADER_BYTES)
            return SoftplgModelManifestTruncated;
        Type = ModelReadU32(Private + Offset + MANIFEST_ITEM_TYPE_OFFSET);
        ItemSize = ModelReadU32(Private + Offset + MANIFEST_ITEM_SIZE_OFFSET);
        if (ItemSize > Remaining - SOFTPLG_IPC4_MANIFEST_ITEM_HEADER_BYTES)
            return SoftplgModelManifestTruncated;
        /* an item of another type is stepped over, as the reference loader does */
        if (Type == SOFTPLG_IPC4_MANIFEST_ITEM_NHLT && Manifest->Nhlt.Size == 0)
        {
            Manifest->Nhlt.Offset = Container.Private.Offset + Offset + SOFTPLG_IPC4_MANIFEST_ITEM_HEADER_BYTES;
            Manifest->Nhlt.Size = ItemSize;
        }
        Offset += SOFTPLG_IPC4_MANIFEST_ITEM_HEADER_BYTES + ItemSize;
    }
    Manifest->ItemCount = Count;
    return SoftplgModelOk;
}

BOOLEAN
SoftplgFormatExpressible(
    UINT32 Rate,
    UINT32 Channels,
    UINT32 ValidBits,
    UINT32 Depth)
{
    /* the rates and depths hdac_format encodes; the container is the one HDAUDIO_SampleContainerBits gives the depth */
    static const UINT32 Rates[] = { 8000, 9600, 11025, 16000, 22050, 32000, 44100, 48000, 88200, 96000, 176400, 192000, 384000 };
    static const UINT32 Depths[] = { 8, 16, 20, 24, 32 };
    const UINT32* Value;

    for (Value = Rates; Value < Rates + sizeof(Rates) / sizeof(Rates[0]) && *Value != Rate; Value++)
        ;
    if (Value == Rates + sizeof(Rates) / sizeof(Rates[0]))
        return FALSE;
    for (Value = Depths; Value < Depths + sizeof(Depths) / sizeof(Depths[0]) && *Value != ValidBits; Value++)
        ;
    if (Value == Depths + sizeof(Depths) / sizeof(Depths[0]))
        return FALSE;
    if (Channels == 0 || Channels > 8)
        return FALSE;
    return Depth == (ValidBits <= 8 ? 8 : ValidBits <= 16 ? 16 : 32);
}

/* The tokens that say yes or no: the files write them as word or as bool tuples */
static
BOOLEAN
ModelTokenIsFlag(
    UINT32 Token)
{
    return Token == SOFTPLG_TOKEN_SCHED_DIRECTION_VALID || Token == SOFTPLG_TOKEN_SCHED_USE_CHAIN_DMA ||
        Token == SOFTPLG_TOKEN_SCHED_DYNAMIC_PIPELINE;
}

/* Keeps one word token of a widget; with no widget, only says whether the token is one the model keeps */
static
BOOLEAN
ModelStoreWord(
    PSOFTPLG_IPC4_WIDGET Widget,
    UINT32 Token,
    UINT32 Value)
{
    switch (Token)
    {
        case SOFTPLG_TOKEN_COMP_CORE_ID:
            if (Widget != NULL)
                Widget->CoreId = Value;
            return TRUE;
        case SOFTPLG_TOKEN_COMP_CPC:
            if (Widget != NULL)
                Widget->Cpc = Value;
            return TRUE;
        case SOFTPLG_TOKEN_COMP_IS_PAGES:
            if (Widget != NULL)
                Widget->IsPages = Value;
            return TRUE;
        case SOFTPLG_TOKEN_COMP_NUM_INPUT_PINS:
            if (Widget != NULL)
                Widget->InputPins = Value;
            return TRUE;
        case SOFTPLG_TOKEN_COMP_NUM_OUTPUT_PINS:
            if (Widget != NULL)
                Widget->OutputPins = Value;
            return TRUE;
        case SOFTPLG_TOKEN_COMP_STACK_BYTES_REQUIREMENT:
            if (Widget != NULL)
                Widget->StackBytes = Value;
            return TRUE;
        case SOFTPLG_TOKEN_COMP_HEAP_BYTES_REQUIREMENT:
            if (Widget != NULL)
                Widget->HeapBytes = Value;
            return TRUE;
        case SOFTPLG_TOKEN_COPIER_NODE_TYPE:
            if (Widget != NULL)
            {
                Widget->NodeType = Value;
                Widget->HasNodeType = TRUE;
            }
            return TRUE;
        case SOFTPLG_TOKEN_COPIER_DEEP_BUFFER_DMA_MS:
            if (Widget != NULL)
                Widget->DeepBufferDmaMs = Value;
            return TRUE;
        case SOFTPLG_TOKEN_DAI_INDEX:
            if (Widget != NULL)
                Widget->DaiIndex = Value;
            return TRUE;
        case SOFTPLG_TOKEN_DAI_DIRECTION:
            if (Widget != NULL)
                Widget->DaiDirection = Value;
            return TRUE;
        case SOFTPLG_TOKEN_GAIN_RAMP_TYPE:
            if (Widget != NULL)
                Widget->GainRampType = Value;
            return TRUE;
        case SOFTPLG_TOKEN_GAIN_RAMP_DURATION:
            if (Widget != NULL)
                Widget->GainRampDuration = Value;
            return TRUE;
        case SOFTPLG_TOKEN_GAIN_VAL:
            if (Widget != NULL)
                Widget->GainVal = Value;
            return TRUE;
        case SOFTPLG_TOKEN_SCHED_PRIORITY:
            if (Widget != NULL)
                Widget->Priority = Value;
            return TRUE;
        case SOFTPLG_TOKEN_SCHED_CORE:
            if (Widget != NULL)
                Widget->SchedCoreId = Value;
            return TRUE;
        case SOFTPLG_TOKEN_SCHED_TIME_DOMAIN:
            if (Widget != NULL)
                Widget->TimeDomain = Value;
            return TRUE;
        case SOFTPLG_TOKEN_SCHED_DYNAMIC_PIPELINE:
            if (Widget != NULL)
                Widget->DynamicPipeline = Value;
            return TRUE;
        case SOFTPLG_TOKEN_SCHED_LP_MODE:
            if (Widget != NULL)
                Widget->LpMode = Value;
            return TRUE;
        case SOFTPLG_TOKEN_SCHED_MEM_USAGE:
            if (Widget != NULL)
                Widget->MemUsage = Value;
            return TRUE;
        case SOFTPLG_TOKEN_SCHED_USE_CHAIN_DMA:
            if (Widget != NULL)
                Widget->UseChainDma = Value;
            return TRUE;
        case SOFTPLG_TOKEN_SCHED_KCPS:
            if (Widget != NULL)
                Widget->Kcps = Value;
            return TRUE;
        case SOFTPLG_TOKEN_SCHED_DIRECTION:
            if (Widget != NULL)
                Widget->Direction = Value;
            return TRUE;
        case SOFTPLG_TOKEN_SCHED_DIRECTION_VALID:
            if (Widget != NULL)
                Widget->DirectionValid = Value;
            return TRUE;
        default:
            return FALSE;
    }
}

/* The side of a format token (0 input, 1 output) and its offset from that side's base */
static
BOOLEAN
ModelFormatToken(
    UINT32 Token,
    UINT32* Side,
    UINT32* Offset)
{
    if (Token >= SOFTPLG_TOKEN_FORMAT_IN_BASE && Token < SOFTPLG_TOKEN_FORMAT_IN_BASE + SOFTPLG_TOKEN_FORMAT_COUNT)
    {
        *Side = 0;
        *Offset = Token - SOFTPLG_TOKEN_FORMAT_IN_BASE;
        return TRUE;
    }
    if (Token >= SOFTPLG_TOKEN_FORMAT_OUT_BASE && Token < SOFTPLG_TOKEN_FORMAT_OUT_BASE + SOFTPLG_TOKEN_FORMAT_COUNT)
    {
        *Side = 1;
        *Offset = Token - SOFTPLG_TOKEN_FORMAT_OUT_BASE;
        return TRUE;
    }
    return FALSE;
}

/* The format slots of a widget: the widget's tuples once more, every format token into the slot its occurrence count names */
static
SOFTPLG_MODEL_STATUS
ModelPlaceFormats(
    const UINT8* Data,
    SIZE_T Size,
    const SOFTPLG_WIDGET* Raw,
    PSOFTPLG_IPC4_WIDGET Widget,
    PSOFTPLG_IPC4_FORMAT Formats,
    const WIDGET_TUPLES* Tuples)
{
    static const UINT32 Empty[2][SOFTPLG_TOKEN_FORMAT_COUNT];
    static const UINT32 EmptyBuffer[2];
    UINT32 Placed[2][SOFTPLG_TOKEN_FORMAT_COUNT];
    UINT32 BufferPlaced[2];
    SOFTPLG_TUPLE_ITER Iterator;
    SOFTPLG_TUPLE Tuple;
    PSOFTPLG_IPC4_FORMAT Format;
    UINT32 Side, Offset;
    SOFTPLG_STATUS Status;

    for (Side = 0; Side < 2; Side++)
    {
        for (Offset = 0; Offset < SOFTPLG_TOKEN_FORMAT_COUNT; Offset++)
            Placed[Side][Offset] = Empty[Side][Offset];
        BufferPlaced[Side] = EmptyBuffer[Side];
    }

    Status = SoftplgTupleBegin(Data, Size, &Raw->Private, &Iterator);
    if (Status != SoftplgOk)
        return SoftplgModelBadFile;
    /* a file may list more formats than the widget offers: the n-th occurrence fills the n-th offered format, and the rest are not read */
    for (;;)
    {
        Status = SoftplgTupleNext(&Iterator, &Tuple);
        if (Status == SoftplgNotFound)
            break;
        if (Status != SoftplgOk)
            return SoftplgModelBadFile;
        if (Tuple.Type != SOFTPLG_TUPLE_WORD)
            continue;
        if (ModelFormatToken(Tuple.Token, &Side, &Offset))
        {
            /* the tokens that only repeat the configuration word are checked once the slots are complete */
            if ((1u << Offset) & FORMAT_ECHOED_MASK)
                continue;
            Format = &Formats[Tuples->InputFormats * Side + Placed[Side][Offset]];
            if (Placed[Side][Offset] < (Side == 0 ? Tuples->InputFormats : Tuples->OutputFormats))
            {
                switch (Offset)
                {
                    case SOFTPLG_FORMAT_RATE: Format->Words[0] = Tuple.Value; break;
                    case SOFTPLG_FORMAT_DEPTH: Format->Words[1] = Tuple.Value; break;
                    case SOFTPLG_FORMAT_CH_MAP: Format->Words[2] = Tuple.Value; break;
                    case SOFTPLG_FORMAT_CH_CFG: Format->Words[3] = Tuple.Value; break;
                    case SOFTPLG_FORMAT_INTERLEAVING: Format->Words[4] = Tuple.Value; break;
                    case SOFTPLG_FORMAT_CFG: Format->Words[5] = Tuple.Value; break;
                    case SOFTPLG_FORMAT_PIN_INDEX: Format->PinIndex = Tuple.Value; break;
                    default: break;
                }
            }
            Placed[Side][Offset]++;
        }
        else if (Tuple.Token == SOFTPLG_TOKEN_FORMAT_IN_IBS || Tuple.Token == SOFTPLG_TOKEN_FORMAT_OUT_OBS)
        {
            Side = Tuple.Token == SOFTPLG_TOKEN_FORMAT_IN_IBS ? 0 : 1;
            Format = &Formats[Tuples->InputFormats * Side + BufferPlaced[Side]];
            if (BufferPlaced[Side] < (Side == 0 ? Tuples->InputFormats : Tuples->OutputFormats))
                Format->BufferBytes = Tuple.Value;
            BufferPlaced[Side]++;
        }
        else if (ModelStoreWord(NULL, Tuple.Token, 0))
        {
            if (Tuple.Type == SOFTPLG_TUPLE_WORD || (Tuple.Type == SOFTPLG_TUPLE_BOOL && ModelTokenIsFlag(Tuple.Token)))
                ModelStoreWord(Widget, Tuple.Token, Tuple.Value);
        }
    }

    /* the echoed tokens must say what the configuration word of their slot says */
    Status = SoftplgTupleBegin(Data, Size, &Raw->Private, &Iterator);
    if (Status != SoftplgOk)
        return SoftplgModelBadFile;
    for (;;)
    {
        Status = SoftplgTupleNext(&Iterator, &Tuple);
        if (Status == SoftplgNotFound)
            break;
        if (Status != SoftplgOk)
            return SoftplgModelBadFile;
        if (Tuple.Type != SOFTPLG_TUPLE_WORD || !ModelFormatToken(Tuple.Token, &Side, &Offset))
            continue;
        if (!((1u << Offset) & FORMAT_ECHOED_MASK))
            continue;
        /* an echoed token the file leaves out for every format is left out here too */
        if (Tuples->Seen[Side][Offset] == 0)
            continue;
        Format = &Formats[Tuples->InputFormats * Side + Placed[Side][Offset]];
        if (Placed[Side][Offset] < (Side == 0 ? Tuples->InputFormats : Tuples->OutputFormats) &&
            ((Offset == SOFTPLG_FORMAT_CHANNELS && Tuple.Value != SOFTPLG_FORMAT_CHANNELS_OF(Format->Words[5])) ||
             (Offset == SOFTPLG_FORMAT_VALID && Tuple.Value != SOFTPLG_FORMAT_VALID_OF(Format->Words[5])) ||
             (Offset == SOFTPLG_FORMAT_SAMPLE_TYPE && Tuple.Value != SOFTPLG_FORMAT_TYPE_OF(Format->Words[5]))))
            return SoftplgModelFormatMismatch;
        Placed[Side][Offset]++;
    }
    for (Side = 0; Side < 2; Side++)
    {
        Format = Formats + (Side == 0 ? 0 : Tuples->InputFormats);
        for (Offset = 0; Offset < (Side == 0 ? Tuples->InputFormats : Tuples->OutputFormats); Offset++)
            Format[Offset].Expressible = SoftplgFormatExpressible(Format[Offset].Words[0],
                SOFTPLG_FORMAT_CHANNELS_OF(Format[Offset].Words[5]), SOFTPLG_FORMAT_VALID_OF(Format[Offset].Words[5]),
                Format[Offset].Words[1]);
    }
    return SoftplgModelOk;
}

/*
 * One widget: its kind, its module and its formats. Formats is NULL in the
 * measure pass, which stops at the counts the model is sized with. The n-th
 * occurrence of a format token belongs to the n-th format of its side, the
 * rule the reference loader reads tuples by.
 */
static
SOFTPLG_MODEL_STATUS
ModelReadWidget(
    const SOFTPLG_MODEL_INPUT* Input,
    const UINT8* Data,
    SIZE_T Size,
    const SOFTPLG_BLOCK* Block,
    UINT32 Index,
    UINT32 FirstFormat,
    PSOFTPLG_IPC4_WIDGET Widget,
    PSOFTPLG_IPC4_FORMAT Formats)
{
    static const SOFTPLG_IPC4_WIDGET Empty;
    static const SOFTPLG_IPC4_FORMAT EmptyFormat;
    static const WIDGET_TUPLES EmptyTuples;
    WIDGET_TUPLES Tuples;
    SOFTPLG_WIDGET Raw;
    SOFTPLG_IPC4_KIND Kind;
    SOFTPLG_TUPLE_ITER Iterator;
    SOFTPLG_TUPLE Tuple;
    UINT32 Side, Offset, Mask, Counts[2];
    /* the tuple is cleared by every step of the walker, so the UUID is kept in its own place */
    UINT8 UuidBytes[SOFTPLG_UUID_BYTES];
    const UINT8* Uuid = NULL;
    BOOLEAN Counted;
    SOFTPLG_STATUS Status;

    Status = SoftplgReadWidget(Data, Size, Block, Index, &Raw);
    if (Status != SoftplgOk)
        return SoftplgModelBadFile;
    if (!ModelWidgetKind(Raw.Id, &Kind))
        return SoftplgModelUnknownWidget;

    *Widget = Empty;
    ModelCopyText(Widget->Name, sizeof(Widget->Name), Raw.Name);
    ModelCopyText(Widget->SName, sizeof(Widget->SName), Raw.SName);
    Widget->Type = Raw.Id;
    Widget->Kind = Kind;
    Widget->PipelineId = Block->Index;
    Widget->Pipeline = SOFTPLG_NO_PIPELINE;
    Widget->Pcm = SOFTPLG_NO_PCM;
    Widget->ModuleId = SOFTPLG_MODULE_NONE;
    Widget->FirstFormat = SOFTPLG_NO_FORMAT;
    Widget->ControlCount = Raw.ControlCount;
    /* a pin carries nothing the DSP instantiates; its tuples are not read */
    if (Kind == SoftplgKindNone)
        return SoftplgModelOk;

    Tuples = EmptyTuples;
    if (Raw.Private.Size != 0)
    {
        Status = SoftplgTupleBegin(Data, Size, &Raw.Private, &Iterator);
        if (Status != SoftplgOk)
            return SoftplgModelBadFile;
        for (;;)
        {
            Status = SoftplgTupleNext(&Iterator, &Tuple);
            if (Status == SoftplgNotFound)
                break;
            if (Status != SoftplgOk)
                return SoftplgModelBadFile;
            Counted = FALSE;
            if (Tuple.Token == SOFTPLG_TOKEN_COMP_UUID)
            {
                if (Tuple.Type != SOFTPLG_TUPLE_UUID)
                    return SoftplgModelTupleType;
                for (Offset = 0; Offset < SOFTPLG_UUID_BYTES; Offset++)
                    UuidBytes[Offset] = Tuple.Uuid[Offset];
                Uuid = UuidBytes;
                continue;
            }
            if (Tuple.Token == SOFTPLG_TOKEN_DAI_TYPE)
            {
                if (Tuple.Type != SOFTPLG_TUPLE_STRING)
                    return SoftplgModelTupleType;
                ModelCopyText(Widget->DaiType, sizeof(Widget->DaiType), Tuple.String);
                continue;
            }
            if (ModelFormatToken(Tuple.Token, &Side, &Offset))
            {
                Counted = TRUE;
                Tuples.Seen[Side][Offset]++;
            }
            else if (Tuple.Token == SOFTPLG_TOKEN_COMP_NUM_INPUT_AUDIO_FORMATS)
            {
                Counted = TRUE;
                Tuples.InputFormats = Tuple.Value;
            }
            else if (Tuple.Token == SOFTPLG_TOKEN_COMP_NUM_OUTPUT_AUDIO_FORMATS)
            {
                Counted = TRUE;
                Tuples.OutputFormats = Tuple.Value;
            }
            else if (Tuple.Token == SOFTPLG_TOKEN_FORMAT_IN_IBS || Tuple.Token == SOFTPLG_TOKEN_FORMAT_OUT_OBS)
            {
                Counted = TRUE;
                Tuples.BufferSeen[Tuple.Token == SOFTPLG_TOKEN_FORMAT_OUT_OBS]++;
            }
            else if (!ModelStoreWord(NULL, Tuple.Token, 0))
                continue;
            /* every token the model keeps is a word, and a flag may come as a bool */
            if (Tuple.Type != SOFTPLG_TUPLE_WORD && !(Tuple.Type == SOFTPLG_TUPLE_BOOL && ModelTokenIsFlag(Tuple.Token)))
                return SoftplgModelTupleType;
            if (!Counted)
                ModelStoreWord(Widget, Tuple.Token, Tuple.Value);
        }
    }

    Widget->InputFormats = Tuples.InputFormats;
    Widget->OutputFormats = Tuples.OutputFormats;
    Widget->FirstFormat = Tuples.InputFormats + Tuples.OutputFormats != 0 ? FirstFormat : SOFTPLG_NO_FORMAT;
    if (ModelIsModule(Kind))
    {
        if (Uuid == NULL)
            return SoftplgModelNoUuid;
        if (Tuples.InputFormats == 0 && Tuples.OutputFormats == 0)
            return SoftplgModelNoFormats;
        Widget->HasUuid = TRUE;
        for (Offset = 0; Offset < SOFTPLG_UUID_BYTES; Offset++)
            Widget->Uuid[Offset] = Uuid[Offset];
        if (Input->Lookup != NULL)
            Widget->ModuleId = Input->Lookup(Input->LookupContext, Widget->Uuid);
        if (Widget->ModuleId == SOFTPLG_MODULE_NONE)
            return SoftplgModelModuleMissing;
    }
    if (Formats == NULL)
        return SoftplgModelOk;

    Counts[0] = Tuples.InputFormats;
    Counts[1] = Tuples.OutputFormats;
    for (Side = 0; Side < 2; Side++)
    {
        if (Counts[Side] == 0)
            continue;
        Mask = 0;
        for (Offset = 0; Offset < SOFTPLG_TOKEN_FORMAT_COUNT; Offset++)
        {
            /* a token comes for every offered format or for none; the file may list more formats than the widget offers, and those are not read */
            if (Tuples.Seen[Side][Offset] != 0 && Tuples.Seen[Side][Offset] < Counts[Side])
                return SoftplgModelFormatCount;
            if (Tuples.Seen[Side][Offset] != 0)
                Mask |= 1u << Offset;
        }
        if ((Mask & FORMAT_REQUIRED_MASK) != FORMAT_REQUIRED_MASK || Tuples.BufferSeen[Side] == 0)
            return SoftplgModelFormatIncomplete;
        if (Tuples.BufferSeen[Side] < Counts[Side])
            return SoftplgModelFormatCount;
    }
    /* the buffer is not zeroed by its owner: a format starts empty, and a token the file leaves out stays empty */
    for (Side = 0; Side < Tuples.InputFormats + Tuples.OutputFormats; Side++)
        Formats[Side] = EmptyFormat;
    return ModelPlaceFormats(Data, Size, &Raw, Widget, Formats, &Tuples);
}

/*
 * One pass over the blocks of the file, each block visited once: the
 * widgets and PCMs as they come (into the model, when there is one), and
 * the counts the model is sized with. Result ends the pass when a block is
 * refused.
 */
typedef struct _MODEL_PASS
{
    const SOFTPLG_MODEL_INPUT* Input;
    SOFTPLG_IPC4_MODEL* Model;
    MODEL_COUNTS* Counts;
    SOFTPLG_IPC4_PROBLEM* Problem;
    UINT32 Widgets;
    UINT32 Formats;
    UINT32 Pcms;
    SOFTPLG_MODEL_STATUS Result;
} MODEL_PASS;

/* One block of a pass; FALSE ends the pass with Result set */
static
BOOLEAN
ModelCollectBlock(
    const SOFTPLG_BLOCK* Block,
    PVOID Context)
{
    MODEL_PASS* Pass = (MODEL_PASS*)Context;
    const SOFTPLG_MODEL_INPUT* Input = Pass->Input;
    SOFTPLG_IPC4_MODEL* Model = Pass->Model;
    SOFTPLG_IPC4_MANIFEST Manifest;
    SOFTPLG_PCM Pcm;
    SOFTPLG_STREAM_CAPS Caps;
    SOFTPLG_IPC4_WIDGET Widget;
    PSOFTPLG_IPC4_WIDGET Placed;
    PSOFTPLG_IPC4_PCM ModelPcm;
    UINT32 N, K;
    SOFTPLG_MODEL_STATUS Result;
    SOFTPLG_STATUS Status;

    switch (Block->Type)
    {
        case SoftplgTypeManifest:
            Result = SoftplgIpc4ReadManifest(Input->Data, Input->Size, Block, &Manifest);
            if (Result != SoftplgModelOk)
            {
                Pass->Result = Result;
                return FALSE;
            }
            Pass->Counts->Manifest = TRUE;
            if (Model != NULL)
            {
                Model->AbiMajor = Manifest.Major;
                Model->AbiMinor = Manifest.Minor;
                Model->AbiPatch = Manifest.Patch;
                Model->ManifestItems = Manifest.ItemCount;
            }
            break;
        case SoftplgTypeWidget:
            for (N = 0; N < Block->Count; N++)
            {
                if (Pass->Widgets >= SOFTPLG_MODEL_WIDGETS_MAX)
                {
                    Pass->Result = SoftplgModelTooLarge;
                    return FALSE;
                }
                /* the measure pass reads every widget into one scratch; the build pass into the model */
                Placed = Model != NULL ? &Model->Widgets[Pass->Widgets] : &Widget;
                Result = ModelReadWidget(Input, Input->Data, Input->Size, Block, N, Pass->Formats, Placed,
                    Model != NULL ? Model->Formats + Pass->Formats : NULL);
                if (Result != SoftplgModelOk)
                {
                    Pass->Problem->Widget = Pass->Widgets;
                    Pass->Result = Result;
                    return FALSE;
                }
                Pass->Widgets++;
                Pass->Formats += Placed->InputFormats + Placed->OutputFormats;
                if (Pass->Formats > SOFTPLG_MODEL_FORMATS_MAX)
                {
                    Pass->Result = SoftplgModelTooLarge;
                    return FALSE;
                }
                if (Placed->Kind == SoftplgKindScheduler)
                    Pass->Counts->Pipelines++;
                Pass->Counts->Controls += Placed->ControlCount;
            }
            break;
        case SoftplgTypeGraph:
            if (Block->Count > SOFTPLG_MODEL_EDGES_MAX - Pass->Counts->Edges)
            {
                Pass->Result = SoftplgModelTooLarge;
                return FALSE;
            }
            Pass->Counts->Edges += Block->Count;
            break;
        case SoftplgTypePcm:
            if (Block->Count > SOFTPLG_MODEL_PCMS_MAX - Pass->Counts->Pcms)
            {
                Pass->Result = SoftplgModelTooLarge;
                return FALSE;
            }
            Pass->Counts->Pcms += Block->Count;
            for (N = 0; N < Block->Count; N++)
            {
                Status = SoftplgReadPcm(Input->Data, Input->Size, Block, N, &Pcm);
                if (Status != SoftplgOk)
                {
                    Pass->Result = SoftplgModelBadFile;
                    return FALSE;
                }
                if (Model != NULL)
                {
                    ModelPcm = &Model->Pcms[Pass->Pcms];
                    ModelCopyText(ModelPcm->Name, sizeof(ModelPcm->Name), Pcm.PcmName);
                    ModelCopyText(ModelPcm->DaiName, sizeof(ModelPcm->DaiName), Pcm.DaiName);
                    ModelPcm->PcmId = Pcm.PcmId;
                    ModelPcm->DaiId = Pcm.DaiId;
                    ModelPcm->Playback = Pcm.Playback;
                    ModelPcm->Capture = Pcm.Capture;
                    ModelPcm->PlaybackStream[0] = 0;
                    ModelPcm->CaptureStream[0] = 0;
                    if (SoftplgReadPcmCaps(Input->Data, Input->Size, Block, N, 0, &Caps) == SoftplgOk)
                        ModelCopyText(ModelPcm->PlaybackStream, sizeof(ModelPcm->PlaybackStream), Caps.Name);
                    if (SoftplgReadPcmCaps(Input->Data, Input->Size, Block, N, 1, &Caps) == SoftplgOk)
                        ModelCopyText(ModelPcm->CaptureStream, sizeof(ModelPcm->CaptureStream), Caps.Name);
                    ModelPcm->PlaybackCopier = SOFTPLG_NO_WIDGET;
                    ModelPcm->CaptureCopier = SOFTPLG_NO_WIDGET;
                    ModelPcm->Reachable = NULL;
                    ModelPcm->Reached = 0;
                    for (K = 0; K < SOFTPLG_PIPELINE_MASK_WORDS; K++)
                        ModelPcm->PipelineMask[K] = 0;
                }
                Pass->Pcms++;
            }
            break;
        case SoftplgTypeMixer:
        case SoftplgTypeBytes:
        case SoftplgTypeEnum:
            Pass->Counts->Controls += Block->Count;
            break;
        default:
            break;
    }
    return TRUE;
}

/*
 * The blocks of the file, each read once: the whole file's headers are
 * checked first, so that a bad header anywhere is a bad file before any
 * block's own problem, then the pass visits every block in the walk's
 * order. The block is never looked up by its index from the first byte
 * again: the n-th lookup cost n header reads, and a file of empty blocks
 * made that quadratic (R2-04).
 */
static
SOFTPLG_MODEL_STATUS
ModelCollect(
    const SOFTPLG_MODEL_INPUT* Input,
    SOFTPLG_IPC4_MODEL* Model,
    MODEL_COUNTS* Counts,
    SOFTPLG_IPC4_PROBLEM* Problem)
{
    static const MODEL_COUNTS Zero;
    SOFTPLG_SUMMARY Summary;
    MODEL_PASS Pass;
    SOFTPLG_STATUS Status;

    *Counts = Zero;
    Problem->Widget = SOFTPLG_NO_WIDGET;
    if (Input->Data == NULL || Input->Size == 0)
        return SoftplgModelNoData;
    Status = SoftplgWalk(Input->Data, Input->Size, NULL, NULL, &Summary);
    if (Status == SoftplgNoData)
        return SoftplgModelNoData;
    if (Status != SoftplgOk)
        return SoftplgModelBadFile;
    Pass.Input = Input;
    Pass.Model = Model;
    Pass.Counts = Counts;
    Pass.Problem = Problem;
    Pass.Widgets = 0;
    Pass.Formats = 0;
    Pass.Pcms = 0;
    Pass.Result = SoftplgModelOk;
    Status = SoftplgWalk(Input->Data, Input->Size, ModelCollectBlock, &Pass, &Summary);
    if (Pass.Result != SoftplgModelOk)
        return Pass.Result;
    if (Status != SoftplgOk)
        return SoftplgModelBadFile;
    if (!Counts->Manifest)
        return SoftplgModelNoManifest;
    Counts->Widgets = Pass.Widgets;
    Counts->Formats = Pass.Formats;
    Counts->Pcms = Pass.Pcms;
    return SoftplgModelOk;
}

/* The bytes of a model with these counts: every array starts at a multiple of eight; the graph's adjacency and the walk's queue are part of the model, sized the same way by both passes */
static
SIZE_T
ModelBytes(
    const MODEL_COUNTS* Counts,
    UINT32* ReachWords)
{
    SIZE_T Bytes;

    *ReachWords = (Counts->Widgets + 31) / 32;
    Bytes = (sizeof(SOFTPLG_IPC4_MODEL) + 7) & ~(SIZE_T)7;
    Bytes += ((SIZE_T)Counts->Widgets * sizeof(SOFTPLG_IPC4_WIDGET) + 7) & ~(SIZE_T)7;
    Bytes += ((SIZE_T)Counts->Edges * sizeof(SOFTPLG_IPC4_EDGE) + 7) & ~(SIZE_T)7;
    Bytes += ((SIZE_T)Counts->Pcms * sizeof(SOFTPLG_IPC4_PCM) + 7) & ~(SIZE_T)7;
    Bytes += ((SIZE_T)Counts->Pipelines * sizeof(SOFTPLG_IPC4_PIPELINE) + 7) & ~(SIZE_T)7;
    Bytes += ((SIZE_T)Counts->Formats * sizeof(SOFTPLG_IPC4_FORMAT) + 7) & ~(SIZE_T)7;
    Bytes += ((SIZE_T)Counts->Pcms * *ReachWords * sizeof(UINT32) + 7) & ~(SIZE_T)7;
    Bytes += ((SIZE_T)Counts->Edges * 2 * sizeof(UINT32) + 7) & ~(SIZE_T)7;
    Bytes += (((SIZE_T)Counts->Widgets + 1) * 2 * sizeof(UINT32) + 7) & ~(SIZE_T)7;
    Bytes += ((SIZE_T)Counts->Widgets * sizeof(UINT32) + 7) & ~(SIZE_T)7;
    return Bytes;
}

/* The model's arrays inside the buffer, in the order ModelBytes counts them */
static
VOID
ModelLayout(
    SOFTPLG_IPC4_MODEL* Model,
    UINT8* Buffer,
    const MODEL_COUNTS* Counts,
    UINT32 ReachWords)
{
    SIZE_T Offset = (sizeof(SOFTPLG_IPC4_MODEL) + 7) & ~(SIZE_T)7;

    Model->Widgets = (PSOFTPLG_IPC4_WIDGET)(Buffer + Offset);
    Offset += ((SIZE_T)Counts->Widgets * sizeof(SOFTPLG_IPC4_WIDGET) + 7) & ~(SIZE_T)7;
    Model->Edges = (PSOFTPLG_IPC4_EDGE)(Buffer + Offset);
    Offset += ((SIZE_T)Counts->Edges * sizeof(SOFTPLG_IPC4_EDGE) + 7) & ~(SIZE_T)7;
    Model->Pcms = (PSOFTPLG_IPC4_PCM)(Buffer + Offset);
    Offset += ((SIZE_T)Counts->Pcms * sizeof(SOFTPLG_IPC4_PCM) + 7) & ~(SIZE_T)7;
    Model->Pipelines = (PSOFTPLG_IPC4_PIPELINE)(Buffer + Offset);
    Offset += ((SIZE_T)Counts->Pipelines * sizeof(SOFTPLG_IPC4_PIPELINE) + 7) & ~(SIZE_T)7;
    Model->Formats = (PSOFTPLG_IPC4_FORMAT)(Buffer + Offset);
    Offset += ((SIZE_T)Counts->Formats * sizeof(SOFTPLG_IPC4_FORMAT) + 7) & ~(SIZE_T)7;
    Model->Reachable = (PUINT32)(Buffer + Offset);
    Offset += ((SIZE_T)Counts->Pcms * ReachWords * sizeof(UINT32) + 7) & ~(SIZE_T)7;
    Model->EdgeOrder = (PUINT32)(Buffer + Offset);
    Offset += ((SIZE_T)Counts->Edges * 2 * sizeof(UINT32) + 7) & ~(SIZE_T)7;
    Model->EdgeFirst = (PUINT32)(Buffer + Offset);
    Offset += ((SIZE_T)Counts->Widgets + 1) * 2 * sizeof(UINT32);
    Model->Queue = (PUINT32)(Buffer + Offset);
    Model->WidgetCount = Counts->Widgets;
    Model->EdgeCount = Counts->Edges;
    Model->PcmCount = Counts->Pcms;
    Model->PipelineCount = Counts->Pipelines;
    Model->FormatCount = Counts->Formats;
    Model->ControlCount = Counts->Controls;
    Model->ReachWords = ReachWords;
}

static
UINT32
ModelFindWidget(
    const SOFTPLG_IPC4_MODEL* Model,
    const CHAR* Name)
{
    UINT32 W;

    for (W = 0; W < Model->WidgetCount; W++)
        if (ModelTextEqual(Model->Widgets[W].Name, Name))
            return W;
    return SOFTPLG_NO_WIDGET;
}

static
BOOLEAN
ModelReached(
    const UINT32* Reachable,
    UINT32 Widget)
{
    return (Reachable[Widget / 32] & (1u << (Widget % 32))) != 0;
}

/*
 * The graph as adjacency, made once: EdgeOrder lists the edge indices
 * grouped by the widget they leave, then by the widget they reach, and
 * EdgeFirst gives where each widget's group starts in its order (one entry
 * more than there are widgets, so a group is First[w] .. First[w + 1]).
 * Queue is the scratch of the count sort and of the walk below.
 */
static
VOID
ModelAdjacency(
    SOFTPLG_IPC4_MODEL* Model)
{
    UINT32* First[] = { Model->EdgeFirst, Model->EdgeFirst + Model->WidgetCount + 1 };
    UINT32* Order[] = { Model->EdgeOrder, Model->EdgeOrder + Model->EdgeCount };
    UINT32 W, E, Side;

    for (Side = 0; Side < 2; Side++)
    {
        for (W = 0; W <= Model->WidgetCount; W++)
            First[Side][W] = 0;
        for (E = 0; E < Model->EdgeCount; E++)
            First[Side][(Side == 0 ? Model->Edges[E].Source : Model->Edges[E].Sink) + 1]++;
        for (W = 1; W <= Model->WidgetCount; W++)
            First[Side][W] += First[Side][W - 1];
        for (W = 0; W < Model->WidgetCount; W++)
            Model->Queue[W] = First[Side][W];
        for (E = 0; E < Model->EdgeCount; E++)
            Order[Side][Model->Queue[Side == 0 ? Model->Edges[E].Source : Model->Edges[E].Sink]++] = E;
    }
}

/*
 * The widgets a stream reaches from the widgets already in the closure:
 * onwards over the edges' sources for playback, backwards over their sinks
 * for capture. One walk: every reached widget leaves the queue once and
 * each of its edges is looked at once, so the work is the graph's own size
 * and not that times the number of widgets (R2-04).
 */
static
VOID
ModelReach(
    const SOFTPLG_IPC4_MODEL* Model,
    UINT32* Closure,
    BOOLEAN Forward)
{
    const UINT32* First = Model->EdgeFirst + (Forward ? 0 : Model->WidgetCount + 1);
    const UINT32* Order = Model->EdgeOrder + (Forward ? 0 : Model->EdgeCount);
    UINT32* Queue = Model->Queue;
    UINT32 Head = 0, Tail = 0, I, W, From, To;

    for (W = 0; W < Model->WidgetCount; W++)
        if (ModelReached(Closure, W))
            Queue[Tail++] = W;
    while (Head < Tail)
    {
        From = Queue[Head++];
        for (I = First[From]; I < First[From + 1]; I++)
        {
            To = Forward ? Model->Edges[Order[I]].Sink : Model->Edges[Order[I]].Source;
            if (ModelReached(Closure, To))
                continue;
            Closure[To / 32] |= 1u << (To % 32);
            Queue[Tail++] = To;
        }
    }
}

/* The widgets a stream reaches from a seed: onwards for playback, backwards for capture; the closure has its own bitmap, so a path the other direction takes is not pulled in */
static
VOID
ModelStream(
    const SOFTPLG_IPC4_MODEL* Model,
    UINT32* Reachable,
    UINT32 Seed,
    BOOLEAN Forward)
{
    UINT32 Closure[SOFTPLG_MODEL_WIDGETS_MAX / 32];
    UINT32 Word;

    if (Seed == SOFTPLG_NO_WIDGET || Model->ReachWords > SOFTPLG_MODEL_WIDGETS_MAX / 32)
        return;
    for (Word = 0; Word < Model->ReachWords; Word++)
        Closure[Word] = 0;
    Closure[Seed / 32] |= 1u << (Seed % 32);
    ModelReach(Model, Closure, Forward);
    for (Word = 0; Word < Model->ReachWords; Word++)
        Reachable[Word] |= Closure[Word];
}

/* One pass over the blocks for the graph: the graph elements as they come, as widget indices; FALSE ends the pass with Result set */
typedef struct _MODEL_GRAPH
{
    const SOFTPLG_MODEL_INPUT* Input;
    SOFTPLG_IPC4_MODEL* Model;
    SOFTPLG_IPC4_PROBLEM* Problem;
    UINT32 Count;
    SOFTPLG_MODEL_STATUS Result;
} MODEL_GRAPH;

static
BOOLEAN
ModelGraphBlock(
    const SOFTPLG_BLOCK* Block,
    PVOID Context)
{
    MODEL_GRAPH* Graph = (MODEL_GRAPH*)Context;
    SOFTPLG_GRAPH_EDGE Edge;
    PSOFTPLG_IPC4_EDGE To;
    UINT32 I;

    if (Block->Type != SoftplgTypeGraph)
        return TRUE;
    for (I = 0; I < Block->Count; I++)
    {
        if (SoftplgReadGraphEdge(Graph->Input->Data, Graph->Input->Size, Block, I, &Edge) != SoftplgOk)
        {
            Graph->Result = SoftplgModelBadFile;
            return FALSE;
        }
        To = &Graph->Model->Edges[Graph->Count];
        To->Source = ModelFindWidget(Graph->Model, Edge.Source);
        To->Sink = ModelFindWidget(Graph->Model, Edge.Sink);
        if (To->Source == SOFTPLG_NO_WIDGET || To->Sink == SOFTPLG_NO_WIDGET)
        {
            Graph->Problem->Widget = To->Source;
            Graph->Result = SoftplgModelBadEdge;
            return FALSE;
        }
        Graph->Count++;
    }
    return TRUE;
}

/*
 * What the walk over the blocks leaves: the pipelines (one scheduler each),
 * the graph elements as widget indices, the copier of every PCM by its
 * stream name, the widgets each PCM reaches, and every module's configuration.
 */
static
SOFTPLG_MODEL_STATUS
ModelFinish(
    const SOFTPLG_MODEL_INPUT* Input,
    SOFTPLG_IPC4_MODEL* Model,
    SOFTPLG_IPC4_PROBLEM* Problem)
{
    SOFTPLG_SUMMARY Summary;
    MODEL_GRAPH Graph;
    PSOFTPLG_IPC4_WIDGET W;
    PSOFTPLG_IPC4_PCM Pcm;
    UINT32 N, P, Count, Word;
    SOFTPLG_MODEL_STATUS Result;

    /* the pipelines, in the order their schedulers come */
    Count = 0;
    for (N = 0; N < Model->WidgetCount; N++)
    {
        W = &Model->Widgets[N];
        if (W->Kind != SoftplgKindScheduler)
            continue;
        for (P = 0; P < Count && Model->Pipelines[P].Id != W->PipelineId; P++)
            ;
        if (P != Count)
        {
            Problem->Widget = N;
            return SoftplgModelDuplicateScheduler;
        }
        /* a PCM's mask holds a bit per pipeline ordinal and has no ninth word: refused here, before any mask is written */
        if (Count >= SOFTPLG_PIPELINE_MASK_WORDS * 32)
        {
            Problem->Widget = N;
            return SoftplgModelTooManyPipelines;
        }
        Model->Pipelines[Count].Id = W->PipelineId;
        Model->Pipelines[Count].Scheduler = N;
        W->Pipeline = Count;
        Count++;
    }
    for (N = 0; N < Model->WidgetCount; N++)
    {
        W = &Model->Widgets[N];
        if (W->Kind == SoftplgKindNone)
            continue;
        for (P = 0; P < Model->PipelineCount && Model->Pipelines[P].Id != W->PipelineId; P++)
            ;
        if (P == Model->PipelineCount)
        {
            Problem->Widget = N;
            return SoftplgModelNoScheduler;
        }
        W->Pipeline = P;
    }

    /* the graph, once every widget has its index: one pass over the blocks, the graph elements as they come, then the adjacency the reach of a stream walks */
    Graph.Input = Input;
    Graph.Model = Model;
    Graph.Problem = Problem;
    Graph.Count = 0;
    Graph.Result = SoftplgModelOk;
    SoftplgWalk(Input->Data, Input->Size, ModelGraphBlock, &Graph, &Summary);
    if (Graph.Result != SoftplgModelOk)
        return Graph.Result;
    if (Graph.Count != Model->EdgeCount)
        return SoftplgModelBadFile;
    ModelAdjacency(Model);

    /* every PCM's copiers by its stream names, then the widgets its stream reaches */
    for (P = 0; P < Model->PcmCount; P++)
    {
        Pcm = &Model->Pcms[P];
        Pcm->Reachable = Model->Reachable + (SIZE_T)P * Model->ReachWords;
        for (Word = 0; Word < Model->ReachWords; Word++)
            Pcm->Reachable[Word] = 0;
    }
    for (N = 0; N < Model->WidgetCount; N++)
    {
        W = &Model->Widgets[N];
        if (W->Kind != SoftplgKindHostCopierPlayback && W->Kind != SoftplgKindHostCopierCapture)
            continue;
        for (P = 0; P < Model->PcmCount; P++)
        {
            Pcm = &Model->Pcms[P];
            if (W->Kind == SoftplgKindHostCopierPlayback)
            {
                if (Pcm->PlaybackCopier == SOFTPLG_NO_WIDGET && ModelTextEqual(Pcm->PlaybackStream, W->SName))
                {
                    Pcm->PlaybackCopier = N;
                    W->Pcm = P;
                }
            }
            else if (Pcm->CaptureCopier == SOFTPLG_NO_WIDGET && ModelTextEqual(Pcm->CaptureStream, W->SName))
            {
                Pcm->CaptureCopier = N;
                W->Pcm = P;
            }
        }
    }
    for (P = 0; P < Model->PcmCount; P++)
    {
        Pcm = &Model->Pcms[P];
        /* playback flows on from the host copier; capture flows into it from before */
        ModelStream(Model, Pcm->Reachable, Pcm->PlaybackCopier, TRUE);
        ModelStream(Model, Pcm->Reachable, Pcm->CaptureCopier, FALSE);
        Pcm->Reached = 0;
        for (N = 0; N < Model->WidgetCount; N++)
        {
            if (!ModelReached(Pcm->Reachable, N))
                continue;
            Pcm->Reached++;
            if (Model->Widgets[N].Pipeline != SOFTPLG_NO_PIPELINE)
                Pcm->PipelineMask[Model->Widgets[N].Pipeline / 32] |= 1u << (Model->Widgets[N].Pipeline % 32);
        }
    }

    /*
     * Every module's configuration of its first input and output formats,
     * the gateway index left as the placeholder. One rule, the gateway
     * buffer's size in one word, and two places that refuse it (FB-05, as
     * R2-10 left it): a copier whose first format's buffer does not fit is
     * refused here, at the build, because its configuration of that format
     * is made now and SoftplgModelConfig answers SoftplgModelGatewayBuffer;
     * a copier whose other formats overflow is the validation's to refuse,
     * over every format (SoftplgModelValidate, ModelGatewayBufferFits).
     * Both name the copier in the driver's log.
     */
    for (N = 0; N < Model->WidgetCount; N++)
    {
        if (!ModelIsModule(Model->Widgets[N].Kind))
            continue;
        Result = SoftplgModelConfig(Model, N, 0, 0, SOFTPLG_STREAM_TAG_NONE, &Model->Widgets[N].Config);
        if (Result != SoftplgModelOk)
        {
            Problem->Widget = N;
            return Result;
        }
    }
    return SoftplgModelOk;
}

SOFTPLG_MODEL_STATUS
SoftplgModelMeasure(
    const SOFTPLG_MODEL_INPUT* Input,
    SIZE_T* Bytes,
    SOFTPLG_IPC4_PROBLEM* Problem)
{
    static const SOFTPLG_IPC4_PROBLEM NoProblem = { SoftplgModelOk, SOFTPLG_NO_WIDGET };
    SOFTPLG_IPC4_PROBLEM Ignored;
    MODEL_COUNTS Counts;
    SOFTPLG_MODEL_STATUS Status;
    UINT32 ReachWords;

    *Bytes = 0;
    if (Problem == NULL)
        Problem = &Ignored;
    *Problem = NoProblem;
    Status = ModelCollect(Input, NULL, &Counts, Problem);
    if (Status != SoftplgModelOk)
    {
        Problem->Status = Status;
        return Status;
    }
    *Bytes = ModelBytes(&Counts, &ReachWords);
    return SoftplgModelOk;
}

SOFTPLG_MODEL_STATUS
SoftplgModelBuild(
    const SOFTPLG_MODEL_INPUT* Input,
    UINT8* Buffer,
    SIZE_T Bytes,
    PSOFTPLG_IPC4_MODEL* Model,
    SOFTPLG_IPC4_PROBLEM* Problem)
{
    static const SOFTPLG_IPC4_MODEL Empty;
    static const SOFTPLG_IPC4_PROBLEM NoProblem = { SoftplgModelOk, SOFTPLG_NO_WIDGET };
    SOFTPLG_IPC4_PROBLEM Ignored;
    MODEL_COUNTS Counts, Again;
    SOFTPLG_MODEL_STATUS Status;
    PSOFTPLG_IPC4_MODEL Built;
    SIZE_T Needed;
    UINT32 ReachWords;

    *Model = NULL;
    if (Problem == NULL)
        Problem = &Ignored;
    *Problem = NoProblem;
    if (Buffer == NULL)
    {
        Problem->Status = SoftplgModelBufferTooSmall;
        return SoftplgModelBufferTooSmall;
    }
    /* the walk is made twice: once for the counts that place the arrays, once to fill them */
    Status = ModelCollect(Input, NULL, &Counts, Problem);
    Needed = Status == SoftplgModelOk ? ModelBytes(&Counts, &ReachWords) : 0;
    if (Status == SoftplgModelOk && Bytes < Needed)
        Status = SoftplgModelBufferTooSmall;
    Built = (PSOFTPLG_IPC4_MODEL)Buffer;
    if (Status == SoftplgModelOk)
    {
        *Built = Empty;
        ModelLayout(Built, Buffer, &Counts, ReachWords);
        Status = ModelCollect(Input, Built, &Again, Problem);
    }
    if (Status == SoftplgModelOk && (Again.Widgets != Counts.Widgets || Again.Edges != Counts.Edges || Again.Pcms != Counts.Pcms ||
        Again.Pipelines != Counts.Pipelines || Again.Formats != Counts.Formats))
        Status = SoftplgModelBadFile;
    if (Status == SoftplgModelOk)
        Status = ModelFinish(Input, Built, Problem);
    Problem->Status = Status;
    if (Status != SoftplgModelOk)
        return Status;
    *Model = Built;
    return SoftplgModelOk;
}

/* The milliseconds of its stream a copier's gateway buffer holds: the deep buffer a playback host copier names above the minimum, else the minimum */
static
UINT32
ModelGatewayMs(
    const SOFTPLG_IPC4_WIDGET* W)
{
    return W->Kind == SoftplgKindHostCopierPlayback && W->DeepBufferDmaMs > SOFTPLG_DMA_BUFFER_MS_MIN ? W->DeepBufferDmaMs : SOFTPLG_DMA_BUFFER_MS_MIN;
}

/* The gateway buffer of a copier over the buffers of the formats its stream takes: a playback copier's input formats, a capture copier's output ones; FALSE when one does not fit the word it is sent in */
static
BOOLEAN
ModelGatewayBufferFits(
    const SOFTPLG_IPC4_MODEL* Model,
    const SOFTPLG_IPC4_WIDGET* W)
{
    UINT32 F, Bytes, Count = W->InputFormats + W->OutputFormats;
    UINT64 Buffer;

    for (F = 0; F < Count; F++)
    {
        Bytes = Model->Formats[W->FirstFormat + F].BufferBytes;
        if (W->Kind == SoftplgKindHostCopierPlayback || W->Kind == SoftplgKindDaiCopierPlayback)
        {
            if (F >= W->InputFormats)
                continue;
            Buffer = (UINT64)ModelGatewayMs(W) * Bytes;
        }
        else if (W->Kind == SoftplgKindHostCopierCapture || W->Kind == SoftplgKindDaiCopierCapture)
        {
            if (F < W->InputFormats)
                continue;
            Buffer = (UINT64)SOFTPLG_DMA_BUFFER_MS_MIN * Bytes;
        }
        else
            return TRUE;
        if (Buffer > 0xFFFFFFFFULL)
            return FALSE;
    }
    return TRUE;
}

SOFTPLG_MODEL_STATUS
SoftplgModelConfig(
    const SOFTPLG_IPC4_MODEL* Model,
    UINT32 Widget,
    UINT32 InputFormat,
    UINT32 OutputFormat,
    UINT32 StreamTag,
    SOFTPLG_IPC4_CONFIG* Config)
{
    static const SOFTPLG_IPC4_CONFIG Empty;
    const SOFTPLG_IPC4_WIDGET* W;
    const SOFTPLG_IPC4_FORMAT* In;
    const SOFTPLG_IPC4_FORMAT* Out;
    const SOFTPLG_IPC4_WIDGET* Scheduler;
    UINT32 I, InputBytes, OutputBytes;
    UINT64 Buffer;

    *Config = Empty;
    if (Model == NULL || Widget >= Model->WidgetCount)
        return SoftplgModelNotFound;
    W = &Model->Widgets[Widget];
    if (!ModelIsModule(W->Kind))
        return SoftplgModelNotFound;
    In = Out = NULL;
    if (W->InputFormats != 0)
    {
        if (InputFormat >= W->InputFormats)
            return SoftplgModelNotFound;
        In = &Model->Formats[W->FirstFormat + InputFormat];
    }
    if (W->OutputFormats != 0)
    {
        if (OutputFormat >= W->OutputFormats)
            return SoftplgModelNotFound;
        Out = &Model->Formats[W->FirstFormat + W->InputFormats + OutputFormat];
    }
    InputBytes = In != NULL ? In->BufferBytes : 0;
    OutputBytes = Out != NULL ? Out->BufferBytes : 0;

    /* the base module configuration */
    Config->Words[0] = W->Cpc;
    Config->Words[1] = InputBytes;
    Config->Words[2] = OutputBytes;
    Config->Words[3] = W->IsPages;
    for (I = 0; I < SOFTPLG_IPC4_AUDIO_FORMAT_WORDS; I++)
        Config->Words[SOFTPLG_IPC4_BASE_CONFIG_WORDS - SOFTPLG_IPC4_AUDIO_FORMAT_WORDS + I] = In != NULL ? In->Words[I] : 0;
    Config->Count = SOFTPLG_IPC4_BASE_CONFIG_WORDS;
    if (SoftplgModelIsGain(W))
    {
        /* the volume entry the topology declares for the widget: every channel, its initial gain, curve and duration */
        Config->Words[SOFTPLG_GAIN_ENTRY_WORD] = 0xFFFFFFFFu;
        Config->Words[SOFTPLG_GAIN_ENTRY_WORD + 1] = W->GainVal;
        Config->Words[SOFTPLG_GAIN_ENTRY_WORD + 2] = W->GainRampType;
        Config->Words[SOFTPLG_GAIN_ENTRY_WORD + 3] = 0;
        Config->Words[SOFTPLG_GAIN_ENTRY_WORD + 4] = W->GainRampDuration;
        Config->Words[SOFTPLG_GAIN_ENTRY_WORD + 5] = 0;
        Config->Count = SOFTPLG_IPC4_BASE_CONFIG_WORDS + SOFTPLG_IPC4_GAIN_CONFIG_WORDS;
        return SoftplgModelOk;
    }
    if (!ModelIsCopier(W->Kind))
        return SoftplgModelOk;

    /* the copier's tail: the output format of pin 0, no feature, and the gateway with its one dword of attributes */
    for (I = 0; I < SOFTPLG_IPC4_AUDIO_FORMAT_WORDS; I++)
        Config->Words[SOFTPLG_COPIER_OUT_FORMAT_WORD + I] = Out != NULL ? Out->Words[I] : 0;
    Config->Words[SOFTPLG_COPIER_FEATURE_MASK_WORD] = 0;
    Config->Words[SOFTPLG_COPIER_CONFIG_LENGTH_WORD] = 1;
    Scheduler = W->Pipeline < Model->PipelineCount ? &Model->Widgets[Model->Pipelines[W->Pipeline].Scheduler] : NULL;
    switch (W->Kind)
    {
        case SoftplgKindHostCopierPlayback:
        case SoftplgKindHostCopierCapture:
            /* the gateway is the host stream: its index is the stream tag minus one */
            Config->Words[SOFTPLG_COPIER_NODE_ID_WORD] = (W->NodeType << SOFTPLG_NODE_TYPE_SHIFT) |
                (StreamTag != SOFTPLG_STREAM_TAG_NONE ? (StreamTag - 1) & SOFTPLG_NODE_INDEX_MASK : SOFTPLG_NODE_INDEX_PLACEHOLDER);
            Config->Words[SOFTPLG_COPIER_ATTRIBUTES_WORD] = Scheduler != NULL ? Scheduler->LpMode & SOFTPLG_ATTRIBUTE_LP_BUFFER : 0;
            break;
        case SoftplgKindDaiCopierPlayback:
        case SoftplgKindDaiCopierCapture:
            /* a DAI gateway takes its index and its data from the machine's NHLT table, which is not this model's */
            Config->Words[SOFTPLG_COPIER_NODE_ID_WORD] = W->NodeType << SOFTPLG_NODE_TYPE_SHIFT;
            break;
        default:
            Config->Words[SOFTPLG_COPIER_NODE_ID_WORD] = SOFTPLG_NODE_INVALID;
            break;
    }
    /* the gateway buffer holds two milliseconds of the copier's stream, or the deep buffer a playback host copier names, in one word: a size the file makes larger than the word is refused, not sent wrapped */
    if (W->Kind == SoftplgKindHostCopierPlayback || W->Kind == SoftplgKindDaiCopierPlayback)
        Buffer = (UINT64)ModelGatewayMs(W) * InputBytes;
    else if (W->Kind == SoftplgKindHostCopierCapture || W->Kind == SoftplgKindDaiCopierCapture)
        Buffer = (UINT64)SOFTPLG_DMA_BUFFER_MS_MIN * OutputBytes;
    else
        Buffer = 0;
    if (Buffer > 0xFFFFFFFFULL)
    {
        *Config = Empty;
        return SoftplgModelGatewayBuffer;
    }
    Config->Words[SOFTPLG_COPIER_DMA_BUFFER_WORD] = (UINT32)Buffer;
    Config->Count = SOFTPLG_IPC4_COPIER_CONFIG_WORDS;
    return SoftplgModelOk;
}

SOFTPLG_MODEL_STATUS
SoftplgModelValidate(
    const SOFTPLG_IPC4_MODEL* Model,
    const SOFTPLG_IPC4_LIMITS* Limits,
    SOFTPLG_IPC4_PROBLEM* Problem)
{
    const SOFTPLG_IPC4_WIDGET* W;
    UINT32 N, F, First, Count, Side;

    Problem->Widget = SOFTPLG_NO_WIDGET;
    Problem->Status = SoftplgModelOk;
    /* pipeline instance ids start at 1, so the count must stay below the firmware's */
    if (Model->PipelineCount >= Limits->MaxPipelines)
    {
        Problem->Status = SoftplgModelTooManyPipelines;
        return Problem->Status;
    }
    for (N = 0; N < Model->WidgetCount; N++)
    {
        W = &Model->Widgets[N];
        if (W->Kind == SoftplgKindHostCopierPlayback || W->Kind == SoftplgKindHostCopierCapture)
        {
            if (!W->HasNodeType || W->NodeType != (W->Kind == SoftplgKindHostCopierPlayback ? SOFTPLG_NODE_HOST_OUTPUT : SOFTPLG_NODE_HOST_INPUT))
            {
                Problem->Widget = N;
                Problem->Status = SoftplgModelNodeType;
                return Problem->Status;
            }
        }
        else if (W->Kind == SoftplgKindDaiCopierPlayback || W->Kind == SoftplgKindDaiCopierCapture)
        {
            Problem->Widget = N;
            Problem->Status = SoftplgModelGatewayData;
            return Problem->Status;
        }
        if (!ModelIsModule(W->Kind))
            continue;
        if (W->Config.Count * sizeof(UINT32) > Limits->MailboxBytes)
        {
            Problem->Widget = N;
            Problem->Status = SoftplgModelConfigTooLarge;
            return Problem->Status;
        }
        /* every gateway buffer the copier can be asked for, over any of its formats, must fit its word */
        if (!ModelGatewayBufferFits(Model, W))
        {
            Problem->Widget = N;
            Problem->Status = SoftplgModelGatewayBuffer;
            return Problem->Status;
        }
        /* the formats a host copier offers the host: the input formats of a playback copier, the output formats of a capture copier */
        if (W->Pcm == SOFTPLG_NO_PCM)
            continue;
        Side = W->Kind == SoftplgKindHostCopierPlayback ? 0 : 1;
        First = W->FirstFormat + (Side == 0 ? 0 : W->InputFormats);
        Count = Side == 0 ? W->InputFormats : W->OutputFormats;
        for (F = 0; F < Count; F++)
            if (Model->Formats[First + F].Expressible)
                break;
        if (F == Count)
        {
            Problem->Widget = N;
            Problem->Status = SoftplgModelFormatInexpressible;
            return Problem->Status;
        }
    }
    return SoftplgModelOk;
}

BOOLEAN
SoftplgModelReachable(
    const SOFTPLG_IPC4_MODEL* Model,
    UINT32 Pcm,
    UINT32 Widget)
{
    if (Model == NULL || Pcm >= Model->PcmCount || Widget >= Model->WidgetCount)
        return FALSE;
    return ModelReached(Model->Pcms[Pcm].Reachable, Widget);
}

const char*
SoftplgModelStatusName(
    SOFTPLG_MODEL_STATUS Status)
{
    switch (Status)
    {
        case SoftplgModelOk: return "ok";
        case SoftplgModelNoData: return "no data";
        case SoftplgModelBadFile: return "not an ALSA topology";
        case SoftplgModelNoManifest: return "no manifest";
        case SoftplgModelAbiMismatch: return "topology ABI mismatch";
        case SoftplgModelManifestTruncated: return "manifest truncated";
        case SoftplgModelTooLarge: return "too large";
        case SoftplgModelBufferTooSmall: return "model buffer too small";
        case SoftplgModelUnknownWidget: return "unknown widget type";
        case SoftplgModelTupleType: return "token of the wrong tuple type";
        case SoftplgModelNoUuid: return "module widget without a UUID";
        case SoftplgModelModuleMissing: return "module not in the firmware image";
        case SoftplgModelNoFormats: return "module widget without an audio format";
        case SoftplgModelFormatCount: return "format tokens do not match the format count";
        case SoftplgModelFormatIncomplete: return "format without its words";
        case SoftplgModelFormatMismatch: return "format tokens disagree";
        case SoftplgModelBadEdge: return "graph element names no widget";
        case SoftplgModelNoScheduler: return "widget without a pipeline scheduler";
        case SoftplgModelDuplicateScheduler: return "two schedulers for one pipeline";
        case SoftplgModelTooManyPipelines: return "more pipelines than the firmware allows";
        case SoftplgModelGatewayData: return "copier needs gateway data";
        case SoftplgModelNodeType: return "host copier of another gateway type";
        case SoftplgModelFormatInexpressible: return "no format the controller can carry";
        case SoftplgModelConfigTooLarge: return "configuration larger than the mailbox";
        case SoftplgModelNotFound: return "not in the model";
        case SoftplgModelGatewayBuffer: return "gateway buffer does not fit its word";
        default: return "unknown";
    }
}
