/*
 * PROJECT:     LiberNT audio driver libraries
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Walks an ALSA SoC topology binary into its blocks, elements, controls and vendor tuples, without I/O
 * COPYRIGHT:   Copyright 2026 LiberNT contributors
 */

/*
 * The container layout re-declared here was studied from Linux
 * include/uapi/sound/asoc.h (the system copy, SPDX-License-Identifier:
 * GPL-2.0 WITH Linux-syscall-note) under LICENSING.md rule 7: every size and
 * offset below is a fact of that ABI (ABI version 5), checked with
 * sizeof/offsetof against the header outside the tree. No text or comment of
 * the header is copied; the names and the words are this project's own.
 */

#ifndef _AUDIO_SOFTPLG_H_
#define _AUDIO_SOFTPLG_H_

#ifdef __cplusplus
extern "C" {
#endif

#define SOFTPLG_MAGIC               0x41536F43
#define SOFTPLG_ABI_VERSION         5
#define SOFTPLG_HEADER_BYTES        36
#define SOFTPLG_NAME_BYTES          44
#define SOFTPLG_TEXT_BYTES          (SOFTPLG_NAME_BYTES + 1)
#define SOFTPLG_STREAMS_MAX         8
#define SOFTPLG_HW_CONFIGS_MAX      8
#define SOFTPLG_CAPS_MAX            2
#define SOFTPLG_UUID_BYTES          16

/* Block types (snd_soc_tplg_hdr.type) */
typedef enum _SOFTPLG_TYPE
{
    SoftplgTypeMixer = 1,
    SoftplgTypeBytes = 2,
    SoftplgTypeEnum = 3,
    SoftplgTypeGraph = 4,
    SoftplgTypeWidget = 5,
    SoftplgTypeDaiLink = 6,
    SoftplgTypePcm = 7,
    SoftplgTypeManifest = 8,
    SoftplgTypeCodecLink = 9,
    SoftplgTypeBackendLink = 10,
    SoftplgTypePdata = 11,
    SoftplgTypeDai = 12,
    SoftplgTypeVendorBase = 1000
} SOFTPLG_TYPE;

#define SOFTPLG_TYPE_COUNT          13

typedef enum _SOFTPLG_STATUS
{
    SoftplgOk = 0,
    SoftplgNoData,
    SoftplgBadSignature,
    SoftplgBadHeader,
    SoftplgTruncated,
    SoftplgNotFound,
    SoftplgUnsupported
} SOFTPLG_STATUS;

/* One block: the 36-byte header (snd_soc_tplg_hdr) and where its payload lies */
typedef struct _SOFTPLG_BLOCK
{
    UINT32 Abi;
    UINT32 Version;
    UINT32 Type;
    UINT32 VendorType;
    UINT32 PayloadSize;
    UINT32 Index;
    UINT32 Count;
    SIZE_T Offset;
    SIZE_T PayloadOffset;
    SIZE_T NextOffset;
} SOFTPLG_BLOCK, *PSOFTPLG_BLOCK;

typedef struct _SOFTPLG_SUMMARY
{
    UINT32 BlockCount;
    UINT32 VendorBlocks;
    UINT32 Abi;
    UINT32 Version;
    UINT32 VendorType;
    UINT32 Counts[SOFTPLG_TYPE_COUNT];
} SOFTPLG_SUMMARY, *PSOFTPLG_SUMMARY;

typedef BOOLEAN (*SOFTPLG_BLOCK_CALLBACK)(
    _In_ const SOFTPLG_BLOCK* Block,
    _In_opt_ PVOID Context);

/*
 * The read of one word of a block header, the one place every walk of the
 * file touches a header. The build has the plain read; the host harness
 * defines its own to count the headers read.
 */
#ifndef SOFTPLG_HEADER_WORD
#define SOFTPLG_HEADER_WORD   SoftplgHeaderWord
#endif

UINT32
SOFTPLG_HEADER_WORD(
    _In_reads_bytes_(Offset + sizeof(UINT32)) const UINT8* Data,
    SIZE_T Offset);

/* Fixed part of an element and the private data that follows it */
typedef struct _SOFTPLG_PRIVATE
{
    SIZE_T Offset;
    SIZE_T Size;
} SOFTPLG_PRIVATE, *PSOFTPLG_PRIVATE;

/* snd_soc_tplg_manifest */
typedef struct _SOFTPLG_MANIFEST
{
    UINT32 ControlElements;
    UINT32 WidgetElements;
    UINT32 GraphElements;
    UINT32 PcmElements;
    UINT32 DaiLinkElements;
    UINT32 DaiElements;
    SOFTPLG_PRIVATE Private;
} SOFTPLG_MANIFEST, *PSOFTPLG_MANIFEST;

/* snd_soc_tplg_ctl_hdr and the three control kinds built on it */
typedef enum _SOFTPLG_CONTROL_KIND
{
    SoftplgControlMixer = SoftplgTypeMixer,
    SoftplgControlBytes = SoftplgTypeBytes,
    SoftplgControlEnum = SoftplgTypeEnum
} SOFTPLG_CONTROL_KIND;

typedef struct _SOFTPLG_CONTROL
{
    SOFTPLG_CONTROL_KIND Kind;
    CHAR Name[SOFTPLG_TEXT_BYTES];
    UINT32 Access;
    UINT32 Minimum;
    UINT32 Maximum;
    UINT32 PlatformMaximum;
    UINT32 Invert;
    UINT32 Channels;
    UINT32 Mask;
    UINT32 Base;
    UINT32 Registers;
    UINT32 Items;
    UINT32 TextCount;
    SOFTPLG_PRIVATE Private;
} SOFTPLG_CONTROL, *PSOFTPLG_CONTROL;

/* snd_soc_tplg_dapm_graph_elem */
typedef struct _SOFTPLG_GRAPH_EDGE
{
    CHAR Sink[SOFTPLG_TEXT_BYTES];
    CHAR Control[SOFTPLG_TEXT_BYTES];
    CHAR Source[SOFTPLG_TEXT_BYTES];
} SOFTPLG_GRAPH_EDGE, *PSOFTPLG_GRAPH_EDGE;

/* snd_soc_tplg_dapm_widget */
typedef struct _SOFTPLG_WIDGET
{
    UINT32 Id;
    CHAR Name[SOFTPLG_TEXT_BYTES];
    CHAR SName[SOFTPLG_TEXT_BYTES];
    UINT32 Register;
    UINT32 Shift;
    UINT32 Mask;
    UINT32 Subseq;
    UINT32 Invert;
    UINT32 IgnoreSuspend;
    UINT16 EventFlags;
    UINT16 EventType;
    UINT32 ControlCount;
    SOFTPLG_PRIVATE Private;
    SIZE_T ControlsOffset;
} SOFTPLG_WIDGET, *PSOFTPLG_WIDGET;

/* snd_soc_tplg_stream */
typedef struct _SOFTPLG_STREAM
{
    CHAR Name[SOFTPLG_TEXT_BYTES];
    UINT64 Format;
    UINT32 Rate;
    UINT32 PeriodBytes;
    UINT32 BufferBytes;
    UINT32 Channels;
} SOFTPLG_STREAM, *PSOFTPLG_STREAM;

/* snd_soc_tplg_stream_caps */
typedef struct _SOFTPLG_STREAM_CAPS
{
    CHAR Name[SOFTPLG_TEXT_BYTES];
    UINT64 Formats;
    UINT32 Rates;
    UINT32 RateMin;
    UINT32 RateMax;
    UINT32 ChannelsMin;
    UINT32 ChannelsMax;
    UINT32 PeriodsMin;
    UINT32 PeriodsMax;
    UINT32 PeriodSizeMin;
    UINT32 PeriodSizeMax;
    UINT32 BufferSizeMin;
    UINT32 BufferSizeMax;
    UINT32 SigBits;
} SOFTPLG_STREAM_CAPS, *PSOFTPLG_STREAM_CAPS;

/* snd_soc_tplg_pcm */
typedef struct _SOFTPLG_PCM
{
    CHAR PcmName[SOFTPLG_TEXT_BYTES];
    CHAR DaiName[SOFTPLG_TEXT_BYTES];
    UINT32 PcmId;
    UINT32 DaiId;
    UINT32 Playback;
    UINT32 Capture;
    UINT32 Compress;
    UINT32 StreamCount;
    UINT32 FlagMask;
    UINT32 Flags;
    SOFTPLG_PRIVATE Private;
} SOFTPLG_PCM, *PSOFTPLG_PCM;

/* snd_soc_tplg_hw_config */
typedef struct _SOFTPLG_HW_CONFIG
{
    UINT32 Id;
    UINT32 Format;
    UINT8 ClockGated;
    UINT8 InvertBclk;
    UINT8 InvertFsync;
    UINT8 BclkProvider;
    UINT8 FsyncProvider;
    UINT8 MclkDirection;
    UINT32 MclkRate;
    UINT32 BclkRate;
    UINT32 FsyncRate;
    UINT32 TdmSlots;
    UINT32 TdmSlotWidth;
    UINT32 TxSlots;
    UINT32 RxSlots;
    UINT32 TxChannels;
    UINT32 RxChannels;
} SOFTPLG_HW_CONFIG, *PSOFTPLG_HW_CONFIG;

/* snd_soc_tplg_link_config, also the codec and backend link blocks */
typedef struct _SOFTPLG_LINK
{
    UINT32 Id;
    CHAR Name[SOFTPLG_TEXT_BYTES];
    CHAR StreamName[SOFTPLG_TEXT_BYTES];
    UINT32 StreamCount;
    UINT32 HwConfigCount;
    UINT32 DefaultHwConfigId;
    UINT32 FlagMask;
    UINT32 Flags;
    SOFTPLG_PRIVATE Private;
} SOFTPLG_LINK, *PSOFTPLG_LINK;

/* snd_soc_tplg_dai */
typedef struct _SOFTPLG_DAI
{
    CHAR DaiName[SOFTPLG_TEXT_BYTES];
    UINT32 DaiId;
    UINT32 Playback;
    UINT32 Capture;
    UINT32 FlagMask;
    UINT32 Flags;
    SOFTPLG_PRIVATE Private;
} SOFTPLG_DAI, *PSOFTPLG_DAI;

/* Vendor tuples (snd_soc_tplg_vendor_array and its elements) */
#define SOFTPLG_TUPLE_UUID          0
#define SOFTPLG_TUPLE_STRING        1
#define SOFTPLG_TUPLE_BOOL          2
#define SOFTPLG_TUPLE_BYTE          3
#define SOFTPLG_TUPLE_WORD          4
#define SOFTPLG_TUPLE_SHORT         5

typedef struct _SOFTPLG_TUPLE_ITER
{
    const UINT8* Data;
    SIZE_T Size;
    SIZE_T Offset;
    SIZE_T Start;
    SIZE_T End;
    SIZE_T ArrayOffset;
    UINT32 Type;
    UINT32 Elements;
    UINT32 Element;
} SOFTPLG_TUPLE_ITER, *PSOFTPLG_TUPLE_ITER;

typedef struct _SOFTPLG_TUPLE
{
    UINT32 Type;
    UINT32 Token;
    UINT32 Value;
    UINT8 Uuid[SOFTPLG_UUID_BYTES];
    CHAR String[SOFTPLG_TEXT_BYTES];
} SOFTPLG_TUPLE, *PSOFTPLG_TUPLE;

SOFTPLG_STATUS
SoftplgWalk(
    _In_reads_bytes_opt_(Size) const UINT8* Data,
    SIZE_T Size,
    _In_opt_ SOFTPLG_BLOCK_CALLBACK Callback,
    _In_opt_ PVOID Context,
    _Out_ SOFTPLG_SUMMARY* Summary);

SOFTPLG_STATUS
SoftplgReadBlock(
    _In_reads_bytes_opt_(Size) const UINT8* Data,
    SIZE_T Size,
    UINT32 Index,
    _Out_ SOFTPLG_BLOCK* Block);

SOFTPLG_STATUS
SoftplgReadManifest(
    _In_reads_bytes_(Size) const UINT8* Data,
    SIZE_T Size,
    _In_ const SOFTPLG_BLOCK* Block,
    _Out_ SOFTPLG_MANIFEST* Manifest);

SOFTPLG_STATUS
SoftplgReadControl(
    _In_reads_bytes_(Size) const UINT8* Data,
    SIZE_T Size,
    _In_ const SOFTPLG_BLOCK* Block,
    UINT32 Index,
    _Out_ SOFTPLG_CONTROL* Control);

SOFTPLG_STATUS
SoftplgReadGraphEdge(
    _In_reads_bytes_(Size) const UINT8* Data,
    SIZE_T Size,
    _In_ const SOFTPLG_BLOCK* Block,
    UINT32 Index,
    _Out_ SOFTPLG_GRAPH_EDGE* Edge);

SOFTPLG_STATUS
SoftplgReadWidget(
    _In_reads_bytes_(Size) const UINT8* Data,
    SIZE_T Size,
    _In_ const SOFTPLG_BLOCK* Block,
    UINT32 Index,
    _Out_ SOFTPLG_WIDGET* Widget);

SOFTPLG_STATUS
SoftplgReadWidgetControl(
    _In_reads_bytes_(Size) const UINT8* Data,
    SIZE_T Size,
    _In_ const SOFTPLG_BLOCK* Block,
    UINT32 WidgetIndex,
    UINT32 ControlIndex,
    _Out_ SOFTPLG_CONTROL* Control);

SOFTPLG_STATUS
SoftplgReadPcm(
    _In_reads_bytes_(Size) const UINT8* Data,
    SIZE_T Size,
    _In_ const SOFTPLG_BLOCK* Block,
    UINT32 Index,
    _Out_ SOFTPLG_PCM* Pcm);

SOFTPLG_STATUS
SoftplgReadPcmStream(
    _In_reads_bytes_(Size) const UINT8* Data,
    SIZE_T Size,
    _In_ const SOFTPLG_BLOCK* Block,
    UINT32 PcmIndex,
    UINT32 StreamIndex,
    _Out_ SOFTPLG_STREAM* Stream);

SOFTPLG_STATUS
SoftplgReadPcmCaps(
    _In_reads_bytes_(Size) const UINT8* Data,
    SIZE_T Size,
    _In_ const SOFTPLG_BLOCK* Block,
    UINT32 PcmIndex,
    UINT32 CapsIndex,
    _Out_ SOFTPLG_STREAM_CAPS* Caps);

SOFTPLG_STATUS
SoftplgReadLink(
    _In_reads_bytes_(Size) const UINT8* Data,
    SIZE_T Size,
    _In_ const SOFTPLG_BLOCK* Block,
    UINT32 Index,
    _Out_ SOFTPLG_LINK* Link);

SOFTPLG_STATUS
SoftplgReadLinkStream(
    _In_reads_bytes_(Size) const UINT8* Data,
    SIZE_T Size,
    _In_ const SOFTPLG_BLOCK* Block,
    UINT32 LinkIndex,
    UINT32 StreamIndex,
    _Out_ SOFTPLG_STREAM* Stream);

SOFTPLG_STATUS
SoftplgReadLinkHwConfig(
    _In_reads_bytes_(Size) const UINT8* Data,
    SIZE_T Size,
    _In_ const SOFTPLG_BLOCK* Block,
    UINT32 LinkIndex,
    UINT32 ConfigIndex,
    _Out_ SOFTPLG_HW_CONFIG* Config);

SOFTPLG_STATUS
SoftplgReadDai(
    _In_reads_bytes_(Size) const UINT8* Data,
    SIZE_T Size,
    _In_ const SOFTPLG_BLOCK* Block,
    UINT32 Index,
    _Out_ SOFTPLG_DAI* Dai);

SOFTPLG_STATUS
SoftplgReadDaiCaps(
    _In_reads_bytes_(Size) const UINT8* Data,
    SIZE_T Size,
    _In_ const SOFTPLG_BLOCK* Block,
    UINT32 DaiIndex,
    UINT32 CapsIndex,
    _Out_ SOFTPLG_STREAM_CAPS* Caps);

SOFTPLG_STATUS
SoftplgTupleBegin(
    _In_reads_bytes_(Size) const UINT8* Data,
    SIZE_T Size,
    _In_ const SOFTPLG_PRIVATE* Private,
    _Out_ SOFTPLG_TUPLE_ITER* Iterator);

SOFTPLG_STATUS
SoftplgTupleNext(
    _Inout_ SOFTPLG_TUPLE_ITER* Iterator,
    _Out_ SOFTPLG_TUPLE* Tuple);

const char* SoftplgStatusName(SOFTPLG_STATUS Status);
const char* SoftplgTypeName(UINT32 Type);

#ifdef __cplusplus
}
#endif

#endif
