/*
 * PROJECT:     LiberNT audio driver libraries
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Builds an IO-free model of an IPC4 topology for the DSP driver (the manifest, the pipelines, the widgets with their module ids, formats and configurations, the PCMs with the widgets each one reaches) and checks it against the firmware's limits
 * COPYRIGHT:   Copyright 2026 LiberNT contributors
 */

/*
 * Every number below is a fact of an ABI, studied under LICENSING.md rule 7
 * (read to learn the layout, declared here in this project's words, no text
 * copied):
 *
 *  - the widget type ids of a topology: Linux include/uapi/sound/asoc.h
 *    SND_SOC_TPLG_DAPM_* (GPL-2.0 WITH Linux-syscall-note), read in the real
 *    topology files of sof-bin 2026.09.2;
 *  - the token numbers: Linux include/uapi/sound/sof/tokens.h SOF_TKN_*
 *    ((GPL-2.0 WITH Linux-syscall-note) OR BSD-3-Clause), the 6.18.60 system
 *    copy and, for the tokens it lacks, the 7.3.5 one;
 *  - the manifest's ABI header and its items: Linux v6.12
 *    sound/soc/sof/ipc4-topology.c, function sof_ipc4_parse_manifest, studied
 *    for its behaviour, and the shipped topology files;
 *  - the audio format, the base module configuration, the copier
 *    configuration and the gateway node id: Sound Open Firmware
 *    (https://github.com/thesofproject/sof) at fcebbbf22d7c08159fbb8486d5f3687da4f167c3,
 *    src/include/module/ipc4/base-config.h, src/audio/copier/copier.h and
 *    src/include/ipc4/gateway.h (BSD-3-Clause, Copyright Intel); how the
 *    gateway index, the attributes and the DMA buffer size are filled: the
 *    same Linux file's function sof_ipc4_prepare_copier_module, studied for
 *    its behaviour;
 *  - the formats the HD Audio controller can carry: this tree's
 *    drivers/wdm/audio/hdaudbus/hdac_stream.cpp, function hdac_format, and
 *    drivers/wdm/audio/drivers/hdaudio/hdaudio_format.h,
 *    HDAUDIO_SampleContainerBits.
 */

#ifndef _AUDIO_SOFTPLG_IPC4MODEL_H_
#define _AUDIO_SOFTPLG_IPC4MODEL_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "softplg.h"

/* The topology ABI this model reads; the patch number is free */
#define SOFTPLG_IPC4_ABI_MAJOR                 3
#define SOFTPLG_IPC4_ABI_MINOR                 29

/* The manifest's private data: three 16-bit numbers (major, minor, patch); when more bytes follow, a 16-bit item count, then items of a type, a size and that many bytes */
#define SOFTPLG_IPC4_MANIFEST_ABI_BYTES        6
#define SOFTPLG_IPC4_MANIFEST_COUNT_OFFSET     6
#define SOFTPLG_IPC4_MANIFEST_ITEM_HEADER_BYTES 8
#define SOFTPLG_IPC4_MANIFEST_ITEM_NHLT        1

/* Widget type ids (snd_soc_tplg_dapm_widget.id) */
#define SOFTPLG_WIDGET_INPUT                   0
#define SOFTPLG_WIDGET_OUTPUT                  1
#define SOFTPLG_WIDGET_MIXER                   3
#define SOFTPLG_WIDGET_PGA                     4
#define SOFTPLG_WIDGET_OUT_DRV                 5
#define SOFTPLG_WIDGET_AIF_IN                  11
#define SOFTPLG_WIDGET_AIF_OUT                 12
#define SOFTPLG_WIDGET_DAI_IN                  13
#define SOFTPLG_WIDGET_DAI_OUT                 14
#define SOFTPLG_WIDGET_BUFFER                  16
#define SOFTPLG_WIDGET_SCHEDULER               17
#define SOFTPLG_WIDGET_EFFECT                  18
#define SOFTPLG_WIDGET_SRC                     20
#define SOFTPLG_WIDGET_ASRC                    21

typedef enum _SOFTPLG_IPC4_KIND
{
    SoftplgKindNone = 0,
    /* a scheduler: the pipeline itself, no module instance */
    SoftplgKindScheduler,
    /* a host copier reading a host playback stream (aif_in) */
    SoftplgKindHostCopierPlayback,
    /* a host copier writing a host capture stream (aif_out) */
    SoftplgKindHostCopierCapture,
    SoftplgKindDaiCopierPlayback,
    SoftplgKindDaiCopierCapture,
    /* a copier between two pipelines (a buffer) */
    SoftplgKindModuleCopier,
    /* a mixer, PGA, effect, SRC or ASRC module */
    SoftplgKindProcessor
} SOFTPLG_IPC4_KIND;

/* Topology tokens (SOF_TKN_*) */
#define SOFTPLG_TOKEN_DAI_TYPE                 154
#define SOFTPLG_TOKEN_DAI_INDEX                155
#define SOFTPLG_TOKEN_DAI_DIRECTION            156
#define SOFTPLG_TOKEN_SCHED_PRIORITY           201
#define SOFTPLG_TOKEN_SCHED_CORE               203
#define SOFTPLG_TOKEN_SCHED_TIME_DOMAIN        205
#define SOFTPLG_TOKEN_SCHED_DYNAMIC_PIPELINE   206
#define SOFTPLG_TOKEN_SCHED_LP_MODE            207
#define SOFTPLG_TOKEN_SCHED_MEM_USAGE          208
#define SOFTPLG_TOKEN_SCHED_USE_CHAIN_DMA      209
#define SOFTPLG_TOKEN_SCHED_KCPS               210
#define SOFTPLG_TOKEN_SCHED_DIRECTION          211
#define SOFTPLG_TOKEN_SCHED_DIRECTION_VALID    212
#define SOFTPLG_TOKEN_GAIN_RAMP_TYPE           260
#define SOFTPLG_TOKEN_GAIN_RAMP_DURATION       261
#define SOFTPLG_TOKEN_GAIN_VAL                 262
#define SOFTPLG_TOKEN_COMP_CORE_ID             404
#define SOFTPLG_TOKEN_COMP_UUID                405
#define SOFTPLG_TOKEN_COMP_CPC                 406
#define SOFTPLG_TOKEN_COMP_IS_PAGES            409
#define SOFTPLG_TOKEN_COMP_NUM_INPUT_PINS      411
#define SOFTPLG_TOKEN_COMP_NUM_OUTPUT_PINS     412
#define SOFTPLG_TOKEN_COMP_NUM_INPUT_AUDIO_FORMATS  415
#define SOFTPLG_TOKEN_COMP_NUM_OUTPUT_AUDIO_FORMATS 416
#define SOFTPLG_TOKEN_COMP_STACK_BYTES_REQUIREMENT  420
#define SOFTPLG_TOKEN_COMP_HEAP_BYTES_REQUIREMENT   421
/* The ten tokens of one audio format, counted from the base of its side, and the buffer size of the pin it describes */
#define SOFTPLG_TOKEN_FORMAT_IN_BASE           1900
#define SOFTPLG_TOKEN_FORMAT_OUT_BASE          1930
#define SOFTPLG_TOKEN_FORMAT_COUNT             10
#define SOFTPLG_TOKEN_FORMAT_IN_IBS            1970
#define SOFTPLG_TOKEN_FORMAT_OUT_OBS           1971
#define SOFTPLG_TOKEN_COPIER_NODE_TYPE         1980
#define SOFTPLG_TOKEN_COPIER_DEEP_BUFFER_DMA_MS 1981

/* Offsets of a format's tokens from its base */
#define SOFTPLG_FORMAT_RATE                    0
#define SOFTPLG_FORMAT_DEPTH                   1
#define SOFTPLG_FORMAT_VALID                   2
#define SOFTPLG_FORMAT_CHANNELS                3
#define SOFTPLG_FORMAT_CH_MAP                  4
#define SOFTPLG_FORMAT_CH_CFG                  5
#define SOFTPLG_FORMAT_INTERLEAVING            6
#define SOFTPLG_FORMAT_CFG                     7
#define SOFTPLG_FORMAT_SAMPLE_TYPE             8
#define SOFTPLG_FORMAT_PIN_INDEX               9

/* Gateway node types of the copiers P3 instantiates (SOF gateway.h); any other one needs gateway data the firmware takes from an NHLT table, which is not P3's */
#define SOFTPLG_NODE_HOST_OUTPUT               0
#define SOFTPLG_NODE_HOST_INPUT                1
#define SOFTPLG_NODE_INVALID                   0xFFFFFFFF

/* ipc4_audio_format in six words: sampling frequency, container depth, channel map, channel configuration, interleaving style, then the configuration word (channels in 7:0, valid bits in 15:8, sample type in 23:16) */
#define SOFTPLG_IPC4_AUDIO_FORMAT_WORDS        6
/* What SOFTPLG_IPC4_FORMAT.Words holds, in the order the topology gives its audio-format tuples (SoftplgFormatTuples) and the message carries them: the sampling rate, the container depth, the channel map, the channel configuration, the interleaving style, and the format configuration, whose channels are SOFTPLG_FORMAT_CHANNELS_OF it, its valid bits SOFTPLG_FORMAT_VALID_OF it and its sample type SOFTPLG_FORMAT_TYPE_OF it */
#define SOFTPLG_FORMAT_WORD_RATE              0
#define SOFTPLG_FORMAT_WORD_DEPTH             1
#define SOFTPLG_FORMAT_WORD_CFG               5
#define SOFTPLG_FORMAT_CHANNELS_OF(Word)       ((Word) & 0xFF)
#define SOFTPLG_FORMAT_VALID_OF(Word)          (((Word) >> 8) & 0xFF)
#define SOFTPLG_FORMAT_TYPE_OF(Word)           (((Word) >> 16) & 0xFF)
/* ipc4_base_module_cfg: the four counts, then the input audio format */
#define SOFTPLG_IPC4_BASE_CONFIG_WORDS         10
/* ipc4_copier_module_cfg: the base configuration, the output format of pin 0, the feature mask, then the gateway configuration with one dword of attributes */
#define SOFTPLG_IPC4_COPIER_CONFIG_WORDS       21
#define SOFTPLG_IPC4_CONFIG_WORDS_MAX          SOFTPLG_IPC4_COPIER_CONFIG_WORDS
#define SOFTPLG_COPIER_OUT_FORMAT_WORD         10
#define SOFTPLG_COPIER_FEATURE_MASK_WORD       16
#define SOFTPLG_COPIER_NODE_ID_WORD            17
#define SOFTPLG_COPIER_DMA_BUFFER_WORD         18
#define SOFTPLG_COPIER_CONFIG_LENGTH_WORD      19
#define SOFTPLG_COPIER_ATTRIBUTES_WORD         20
#define SOFTPLG_NODE_TYPE_SHIFT                8
#define SOFTPLG_NODE_INDEX_MASK                0xFF
#define SOFTPLG_ATTRIBUTE_LP_BUFFER            0x1
/* The gateway DMA buffer holds at least this many milliseconds of the copier's buffer (Linux v6.12 SOF_IPC4_MIN_DMA_BUFFER_SIZE) */
#define SOFTPLG_DMA_BUFFER_MS_MIN              2
/* No stream tag yet: the gateway index is left at a value no stream has */
#define SOFTPLG_STREAM_TAG_NONE                0
#define SOFTPLG_NODE_INDEX_PLACEHOLDER         0xFF

#define SOFTPLG_MODULE_NONE                    0xFFFFFFFF
#define SOFTPLG_NO_WIDGET                      0xFFFFFFFF
#define SOFTPLG_NO_PCM                         0xFFFFFFFF
#define SOFTPLG_NO_PIPELINE                    0xFFFFFFFF
#define SOFTPLG_NO_FORMAT                      0xFFFFFFFF

/* A topology above any of these is not read further */
#define SOFTPLG_MODEL_WIDGETS_MAX              2048
#define SOFTPLG_MODEL_EDGES_MAX                8192
#define SOFTPLG_MODEL_PCMS_MAX                 256
#define SOFTPLG_MODEL_FORMATS_MAX              16384

/* Pipelines are told apart by their ordinal, so 256 at most */
#define SOFTPLG_PIPELINE_MASK_WORDS            8

typedef struct _SOFTPLG_IPC4_MANIFEST
{
    UINT16 Major;
    UINT16 Minor;
    UINT16 Patch;
    UINT32 ItemCount;
    /* the NHLT table the manifest carries, when it carries one: where it lies in the file and how long it is (Size 0 otherwise) */
    SOFTPLG_PRIVATE Nhlt;
} SOFTPLG_IPC4_MANIFEST, *PSOFTPLG_IPC4_MANIFEST;

/* One audio format of one pin: the six words the firmware takes and the pin's buffer size */
typedef struct _SOFTPLG_IPC4_FORMAT
{
    UINT32 Words[SOFTPLG_IPC4_AUDIO_FORMAT_WORDS];
    UINT32 BufferBytes;
    UINT32 PinIndex;
    /* whether the HD Audio controller can carry this format on a host stream */
    BOOLEAN Expressible;
} SOFTPLG_IPC4_FORMAT, *PSOFTPLG_IPC4_FORMAT;

/* What an init-instance message carries for a widget */
typedef struct _SOFTPLG_IPC4_CONFIG
{
    UINT32 Words[SOFTPLG_IPC4_CONFIG_WORDS_MAX];
    UINT32 Count;
} SOFTPLG_IPC4_CONFIG, *PSOFTPLG_IPC4_CONFIG;

typedef struct _SOFTPLG_IPC4_WIDGET
{
    CHAR Name[SOFTPLG_TEXT_BYTES];
    CHAR SName[SOFTPLG_TEXT_BYTES];
    UINT32 Type;
    SOFTPLG_IPC4_KIND Kind;
    /* the header index of the widget block that carries the widget: the topology's pipeline number */
    UINT32 PipelineId;
    /* the ordinal of that pipeline among the model's pipelines */
    UINT32 Pipeline;
    /* the PCM a host copier serves, by its stream name */
    UINT32 Pcm;
    BOOLEAN HasUuid;
    UINT8 Uuid[SOFTPLG_UUID_BYTES];
    /* the index of the UUID in the firmware image's module table, the module id of every message to the instance */
    UINT32 ModuleId;
    UINT32 CoreId;
    UINT32 Cpc;
    UINT32 IsPages;
    UINT32 InputPins;
    UINT32 OutputPins;
    UINT32 StackBytes;
    UINT32 HeapBytes;
    UINT32 ControlCount;
    /* input formats first, output formats after them */
    UINT32 InputFormats;
    UINT32 OutputFormats;
    UINT32 FirstFormat;
    BOOLEAN HasNodeType;
    UINT32 NodeType;
    UINT32 DeepBufferDmaMs;
    CHAR DaiType[SOFTPLG_TEXT_BYTES];
    UINT32 DaiIndex;
    UINT32 DaiDirection;
    UINT32 GainRampType;
    UINT32 GainRampDuration;
    UINT32 GainVal;
    /* the scheduler's values */
    UINT32 Priority;
    UINT32 SchedCoreId;
    UINT32 TimeDomain;
    UINT32 DynamicPipeline;
    UINT32 LpMode;
    UINT32 MemUsage;
    UINT32 UseChainDma;
    UINT32 Kcps;
    UINT32 Direction;
    UINT32 DirectionValid;
    /* the configuration of the first input and output formats; a host copier's gateway index is the placeholder until a stream tag is known */
    SOFTPLG_IPC4_CONFIG Config;
} SOFTPLG_IPC4_WIDGET, *PSOFTPLG_IPC4_WIDGET;

/* A graph element: the widgets its source and its sink name */
typedef struct _SOFTPLG_IPC4_EDGE
{
    UINT32 Source;
    UINT32 Sink;
} SOFTPLG_IPC4_EDGE, *PSOFTPLG_IPC4_EDGE;

typedef struct _SOFTPLG_IPC4_PIPELINE
{
    UINT32 Id;
    /* the scheduler widget of the pipeline, whose values are the create-pipeline message's */
    UINT32 Scheduler;
} SOFTPLG_IPC4_PIPELINE, *PSOFTPLG_IPC4_PIPELINE;

typedef struct _SOFTPLG_IPC4_PCM
{
    CHAR Name[SOFTPLG_TEXT_BYTES];
    CHAR DaiName[SOFTPLG_TEXT_BYTES];
    UINT32 PcmId;
    UINT32 DaiId;
    UINT32 Playback;
    UINT32 Capture;
    CHAR PlaybackStream[SOFTPLG_TEXT_BYTES];
    CHAR CaptureStream[SOFTPLG_TEXT_BYTES];
    UINT32 PlaybackCopier;
    UINT32 CaptureCopier;
    /* the widgets the stream of this PCM reaches, one bit per widget */
    UINT32* Reachable;
    UINT32 Reached;
    UINT32 PipelineMask[SOFTPLG_PIPELINE_MASK_WORDS];
} SOFTPLG_IPC4_PCM, *PSOFTPLG_IPC4_PCM;

typedef struct _SOFTPLG_IPC4_MODEL
{
    UINT16 AbiMajor;
    UINT16 AbiMinor;
    UINT16 AbiPatch;
    UINT32 ManifestItems;
    UINT32 ControlCount;
    UINT32 WidgetCount;
    UINT32 EdgeCount;
    UINT32 PcmCount;
    UINT32 PipelineCount;
    UINT32 FormatCount;
    UINT32 ReachWords;
    PSOFTPLG_IPC4_WIDGET Widgets;
    PSOFTPLG_IPC4_EDGE Edges;
    PSOFTPLG_IPC4_PCM Pcms;
    PSOFTPLG_IPC4_PIPELINE Pipelines;
    PSOFTPLG_IPC4_FORMAT Formats;
    /* PcmCount bitmaps of ReachWords words each */
    PUINT32 Reachable;
} SOFTPLG_IPC4_MODEL, *PSOFTPLG_IPC4_MODEL;

typedef struct _SOFTPLG_IPC4_LIMITS
{
    /* the firmware's pipeline count (its configuration reply); pipeline instance ids start at 1 and stay below it */
    UINT32 MaxPipelines;
    /* the firmware's downlink mailbox in bytes: an init-instance configuration must fit it */
    UINT32 MailboxBytes;
} SOFTPLG_IPC4_LIMITS, *PSOFTPLG_IPC4_LIMITS;

/* The module id of a UUID in the firmware image's module table, or SOFTPLG_MODULE_NONE */
typedef UINT32 (*SOFTPLG_MODULE_LOOKUP)(
    _In_opt_ PVOID Context,
    _In_reads_bytes_(SOFTPLG_UUID_BYTES) const UINT8* Uuid);

typedef struct _SOFTPLG_MODEL_INPUT
{
    _In_reads_bytes_(Size) const UINT8* Data;
    SIZE_T Size;
    _In_opt_ SOFTPLG_MODULE_LOOKUP Lookup;
    _In_opt_ PVOID LookupContext;
} SOFTPLG_MODEL_INPUT, *PSOFTPLG_MODEL_INPUT;

typedef enum _SOFTPLG_MODEL_STATUS
{
    SoftplgModelOk = 0,
    SoftplgModelNoData,
    /* the container reader refused the file */
    SoftplgModelBadFile,
    /* the file has no manifest block */
    SoftplgModelNoManifest,
    /* the manifest's ABI is not the one this model reads */
    SoftplgModelAbiMismatch,
    /* a manifest item's size runs past the manifest's private data */
    SoftplgModelManifestTruncated,
    /* more widgets, edges, PCMs or formats than the model holds */
    SoftplgModelTooLarge,
    SoftplgModelBufferTooSmall,
    SoftplgModelUnknownWidget,
    /* a token the model reads comes as another tuple type than its number says */
    SoftplgModelTupleType,
    /* a module widget without a UUID tuple */
    SoftplgModelNoUuid,
    /* the UUID is not in the firmware image's module table */
    SoftplgModelModuleMissing,
    /* a module widget without any audio format */
    SoftplgModelNoFormats,
    /* a format token occurs neither as often as there are formats nor not at all */
    SoftplgModelFormatCount,
    /* a format without its six words */
    SoftplgModelFormatIncomplete,
    /* a format's channels, valid bits or sample type tokens disagree with its configuration word */
    SoftplgModelFormatMismatch,
    /* a graph element names no widget */
    SoftplgModelBadEdge,
    SoftplgModelNoScheduler,
    SoftplgModelDuplicateScheduler,
    SoftplgModelTooManyPipelines,
    SoftplgModelGatewayData,
    SoftplgModelNodeType,
    SoftplgModelFormatInexpressible,
    SoftplgModelConfigTooLarge,
    /* a widget or format index the model does not have */
    SoftplgModelNotFound,
    /* a copier whose gateway buffer size, two milliseconds of its stream or the deep buffer it names, does not fit the word it is sent in */
    SoftplgModelGatewayBuffer
} SOFTPLG_MODEL_STATUS;

/* The first problem a check finds, and the widget it names */
typedef struct _SOFTPLG_IPC4_PROBLEM
{
    SOFTPLG_MODEL_STATUS Status;
    UINT32 Widget;
} SOFTPLG_IPC4_PROBLEM, *PSOFTPLG_IPC4_PROBLEM;

/* The manifest's ABI header and items */
SOFTPLG_MODEL_STATUS
SoftplgIpc4ReadManifest(
    _In_reads_bytes_(Size) const UINT8* Data,
    SIZE_T Size,
    _In_ const SOFTPLG_BLOCK* Block,
    _Out_ SOFTPLG_IPC4_MANIFEST* Manifest);

/* The bytes the model of this topology needs; when it refuses the file, Problem names the widget it refused at (optional) */
SOFTPLG_MODEL_STATUS
SoftplgModelMeasure(
    _In_ const SOFTPLG_MODEL_INPUT* Input,
    _Out_ SIZE_T* Bytes,
    _Out_opt_ SOFTPLG_IPC4_PROBLEM* Problem);

/* The model, in a buffer of at least the bytes the measure pass named; when it refuses the file, Problem names the widget it refused at (optional) */
SOFTPLG_MODEL_STATUS
SoftplgModelBuild(
    _In_ const SOFTPLG_MODEL_INPUT* Input,
    _Out_writes_bytes_(Bytes) UINT8* Buffer,
    SIZE_T Bytes,
    _Out_ PSOFTPLG_IPC4_MODEL* Model,
    _Out_opt_ SOFTPLG_IPC4_PROBLEM* Problem);

/* Whether the firmware can run what the model describes: the pipelines fit its count, every copier's gateway is a host gateway, every host copier has a format the controller can carry, every gateway buffer size fits its word and every configuration fits the mailbox */
SOFTPLG_MODEL_STATUS
SoftplgModelValidate(
    _In_ const SOFTPLG_IPC4_MODEL* Model,
    _In_ const SOFTPLG_IPC4_LIMITS* Limits,
    _Out_ SOFTPLG_IPC4_PROBLEM* Problem);

/* A widget's init-instance configuration for the given input and output formats; a host copier's gateway index is the stream tag minus one, or the placeholder without a tag; a gateway buffer size that does not fit its word is SoftplgModelGatewayBuffer and nothing is written */
SOFTPLG_MODEL_STATUS
SoftplgModelConfig(
    _In_ const SOFTPLG_IPC4_MODEL* Model,
    UINT32 Widget,
    UINT32 InputFormat,
    UINT32 OutputFormat,
    UINT32 StreamTag,
    _Out_ SOFTPLG_IPC4_CONFIG* Config);

/* Whether the stream of a PCM reaches a widget */
BOOLEAN
SoftplgModelReachable(
    _In_ const SOFTPLG_IPC4_MODEL* Model,
    UINT32 Pcm,
    UINT32 Widget);

/* Whether the HD Audio controller can carry a format on a host stream: one of the rates, channel counts and valid-bit depths its stream format word expresses, in the container the tree's sample rule gives that depth */
BOOLEAN
SoftplgFormatExpressible(
    UINT32 Rate,
    UINT32 Channels,
    UINT32 ValidBits,
    UINT32 Depth);

const char* SoftplgModelStatusName(SOFTPLG_MODEL_STATUS Status);
const char* SoftplgWidgetKindName(SOFTPLG_IPC4_KIND Kind);

#ifdef __cplusplus
}
#endif

#endif
