/*
 * PROJECT:     LiberNT host-native tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Environment for the DSP endpoint tests: the KS and PortCls data types the endpoint driver's pure half and its descriptor tables name
 * COPYRIGHT:   Copyright 2026 LiberNT contributors
 */

/*
 * Host stand-in for sdk/include/psdk/ks.h, ksmedia.h and sdk/include/ddk/
 * portcls.h, as far as the endpoint driver's C reads them: the structures
 * are declared field for field in the SDK's order, so the positional
 * initializers of drivers/wdm/audio/drivers/dsppcm/tables.h mean the same
 * here as in the kernel build. The GUID symbols stand for the headers' own
 * (in C the headers declare them; their values are ksmedia.h's STATIC_
 * constants), and the property and event ids are ksmedia.h's. A throwaway
 * probe compiled against the kernel's own headers with the build's flags
 * pinned every size and offset below before this file was kept; the probe
 * lives outside the tree.
 */

#ifndef DSPPCM_TEST_PORTCLS_H
#define DSPPCM_TEST_PORTCLS_H

#include "../shim.h"

#ifndef NTAPI
#define NTAPI
#endif
#ifndef IN
#define IN
#endif
#ifndef OUT
#define OUT
#endif
#ifndef OPTIONAL
#define OPTIONAL
#endif

typedef uint16_t WORD, *PWORD;
typedef uint32_t DWORD, *PDWORD;
typedef uint32_t UINT;
typedef int BOOL, *PBOOL;
typedef uint64_t DWORDLONG;
typedef void* PIRP;

#ifndef MAXUSHORT
#define MAXUSHORT 0xffff
#endif
#ifndef MAXULONG
#define MAXULONG 0xffffffffUL
#endif
/* wdm.h READ_REGISTER_ULONG: a plain load on the host */
#ifndef READ_REGISTER_ULONG
#define READ_REGISTER_ULONG(Register) (*(PULONG)(Register))
#endif
/* guiddef.h DEFINE_GUID with initguid.h: the GUID defined where it stands */
#ifndef DEFINE_GUID
#define DEFINE_GUID(name, l, w1, w2, b1, b2, b3, b4, b5, b6, b7, b8) \
    static const GUID name = { l, w1, w2, { b1, b2, b3, b4, b5, b6, b7, b8 } }
#endif
#ifndef STATUS_NOT_IMPLEMENTED
#define STATUS_NOT_IMPLEMENTED ((NTSTATUS)0xC0000002L)
#endif
#ifndef STATUS_BUFFER_TOO_SMALL
#define STATUS_BUFFER_TOO_SMALL ((NTSTATUS)0xC0000023L)
#endif
#ifndef STATUS_BUFFER_OVERFLOW
#define STATUS_BUFFER_OVERFLOW ((NTSTATUS)0x80000005L)
#endif
#ifndef STATUS_NO_MATCH
#define STATUS_NO_MATCH ((NTSTATUS)0xC0000094L)
#endif

#ifndef RtlCompareMemory
static inline ULONG RtlCompareMemory(const void* Source1, const void* Source2, SIZE_T Length)
{
    const UCHAR* A = (const UCHAR*)Source1;
    const UCHAR* B = (const UCHAR*)Source2;
    SIZE_T I;

    for (I = 0; I < Length && A[I] == B[I]; I++)
        ;
    return (ULONG)I;
}
#endif
#ifndef RtlMoveMemory
#define RtlMoveMemory(Destination, Source, Length) memmove((Destination), (Source), (Length))
#endif
#ifndef CONTAINING_RECORD
#define CONTAINING_RECORD(Address, Type, Field) ((Type*)((PVOID)(Address) - FIELD_OFFSET(Type, Field)))
#endif
#ifndef UNREFERENCED_PARAMETER
#define UNREFERENCED_PARAMETER(P) ((void)(P))
#endif

/* ks.h */
#define KSFILTER_NODE   ((ULONG)-1)
#define SIZEOF_ARRAY(a) (sizeof(a) / sizeof((a)[0]))
#define STATICGUIDOF(guid) STATIC_##guid
#define KSPROPERTY_TYPE_GET           0x00000001
#define KSPROPERTY_TYPE_SET           0x00000002
#define KSPROPERTY_TYPE_BASICSUPPORT  0x00000200
#define KSEVENT_TYPE_ENABLE           0x00000001
#define KSEVENT_TYPE_BASICSUPPORT     0x00000200

typedef struct _GUID
{
    uint32_t Data1;
    uint16_t Data2;
    uint16_t Data3;
    uint8_t Data4[8];
} GUID, *PGUID;
typedef const GUID* REFGUID;

typedef enum
{
    KSSTATE_STOP = 0,
    KSSTATE_ACQUIRE,
    KSSTATE_PAUSE,
    KSSTATE_RUN
} KSSTATE;

/* ks.h: a union of the fields and an alignment, so the tables initialize it with a brace of their own around the fields */
typedef union
{
    struct
    {
        ULONG FormatSize;
        ULONG Flags;
        ULONG SampleSize;
        ULONG Reserved;
        GUID MajorFormat;
        GUID SubFormat;
        GUID Specifier;
    };
    LONGLONG Alignment;
} KSDATAFORMAT, *PKSDATAFORMAT, KSDATARANGE, *PKSDATARANGE;

typedef enum
{
    KSPIN_DATAFLOW_IN = 1,
    KSPIN_DATAFLOW_OUT
} KSPIN_DATAFLOW;

typedef enum
{
    KSPIN_COMMUNICATION_NONE,
    KSPIN_COMMUNICATION_SINK,
    KSPIN_COMMUNICATION_SOURCE,
    KSPIN_COMMUNICATION_BOTH,
    KSPIN_COMMUNICATION_DESKTOP
} KSPIN_COMMUNICATION;

typedef struct
{
    ULONG InterfacesCount;
    PVOID Interfaces;
    ULONG MediumsCount;
    PVOID Mediums;
    ULONG DataRangesCount;
    PKSDATARANGE* DataRanges;
    KSPIN_DATAFLOW DataFlow;
    KSPIN_COMMUNICATION Communication;
    const GUID* Category;
    const GUID* Name;
    union
    {
        LONGLONG Reserved;
        struct
        {
            ULONG ConstrainedDataRangesCount;
            PKSDATARANGE* ConstrainedDataRanges;
        };
    };
} KSPIN_DESCRIPTOR, *PKSPIN_DESCRIPTOR;

/* mmreg.h, as ksmedia.h includes it: the wave formats are packed to a byte (ksmedia.h wraps them in #pragma pack(push, 1)) */
#pragma pack(push, 1)
typedef struct
{
    WORD wFormatTag;
    WORD nChannels;
    DWORD nSamplesPerSec;
    DWORD nAvgBytesPerSec;
    WORD nBlockAlign;
    WORD wBitsPerSample;
    WORD cbSize;
} WAVEFORMATEX, *PWAVEFORMATEX;

typedef struct
{
    WAVEFORMATEX Format;
    union
    {
        WORD wValidBitsPerSample;
        WORD wSamplesPerBlock;
        WORD wReserved;
    } Samples;
    DWORD dwChannelMask;
    GUID SubFormat;
} WAVEFORMATEXTENSIBLE, *PWAVEFORMATEXTENSIBLE;

#define WAVE_FORMAT_PCM         1
#define WAVE_FORMAT_EXTENSIBLE  0xfffe

typedef struct
{
    KSDATAFORMAT DataFormat;
    WAVEFORMATEX WaveFormatEx;
} KSDATAFORMAT_WAVEFORMATEX, *PKSDATAFORMAT_WAVEFORMATEX;
#pragma pack(pop)

/* ksmedia.h */
typedef struct
{
    KSDATARANGE DataRange;
    ULONG MaximumChannels;
    ULONG MinimumBitsPerSample;
    ULONG MaximumBitsPerSample;
    ULONG MinimumSampleFrequency;
    ULONG MaximumSampleFrequency;
} KSDATARANGE_AUDIO, *PKSDATARANGE_AUDIO;

typedef struct
{
    PVOID Register;
    ULONG Width;
    ULONGLONG Numerator;
    ULONGLONG Denominator;
    ULONG Accuracy;
} KSRTAUDIO_HWREGISTER, *PKSRTAUDIO_HWREGISTER;

typedef struct
{
    ULONG FifoSize;
    ULONG ChipsetDelay;
    ULONG CodecDelay;
} KSRTAUDIO_HWLATENCY, *PKSRTAUDIO_HWLATENCY;

typedef struct
{
    DWORDLONG PlayOffset;
    DWORDLONG WriteOffset;
} KSAUDIO_POSITION, *PKSAUDIO_POSITION;

/* ksmedia.h KSPROPERTY_AUDIO_* and KSEVENT_AUDIO_CONTROL_CHANGE, by their numbers in the enumerations */
#define KSPROPERTY_AUDIO_CHANNEL_CONFIG            3
#define KSPROPERTY_AUDIO_VOLUMELEVEL               4
#define KSPROPERTY_AUDIO_STEREO_SPEAKER_GEOMETRY   9
#define KSPROPERTY_AUDIO_MUTE                      13
#define KSEVENT_CONTROL_CHANGE                     0
#define KSPROPERTY_JACK_DESCRIPTION                1

/* portcls.h */
typedef struct _PCPROPERTY_REQUEST* PPCPROPERTY_REQUEST;
typedef struct _PCEVENT_REQUEST* PPCEVENT_REQUEST;
typedef NTSTATUS(*PCPFNPROPERTY_HANDLER)(PPCPROPERTY_REQUEST PropertyRequest);
typedef NTSTATUS(*PCPFNEVENT_HANDLER)(PPCEVENT_REQUEST EventRequest);

typedef struct
{
    const GUID* Set;
    ULONG Id;
    ULONG Flags;
    PCPFNPROPERTY_HANDLER Handler;
} PCPROPERTY_ITEM, *PPCPROPERTY_ITEM;

typedef struct
{
    const GUID* Set;
    ULONG Id;
    ULONG Flags;
    PCPFNEVENT_HANDLER Handler;
} PCEVENT_ITEM, *PPCEVENT_ITEM;

typedef struct
{
    const GUID* Set;
    ULONG Id;
    ULONG Flags;
    PVOID Handler;
} PCMETHOD_ITEM, *PPCMETHOD_ITEM;

typedef struct
{
    ULONG PropertyItemSize;
    ULONG PropertyCount;
    const PCPROPERTY_ITEM* Properties;
    ULONG MethodItemSize;
    ULONG MethodCount;
    const PCMETHOD_ITEM* Methods;
    ULONG EventItemSize;
    ULONG EventCount;
    const PCEVENT_ITEM* Events;
    ULONG Reserved;
} PCAUTOMATION_TABLE, *PPCAUTOMATION_TABLE;

typedef struct
{
    ULONG FromNode;
    ULONG FromNodePin;
    ULONG ToNode;
    ULONG ToNodePin;
} PCCONNECTION_DESCRIPTOR, *PPCCONNECTION_DESCRIPTOR;

typedef struct
{
    ULONG MaxGlobalInstanceCount;
    ULONG MaxFilterInstanceCount;
    ULONG MinFilterInstanceCount;
    const PCAUTOMATION_TABLE* AutomationTable;
    KSPIN_DESCRIPTOR KsPinDescriptor;
} PCPIN_DESCRIPTOR, *PPCPIN_DESCRIPTOR;

typedef struct
{
    ULONG Flags;
    const PCAUTOMATION_TABLE* AutomationTable;
    const GUID* Type;
    const GUID* Name;
} PCNODE_DESCRIPTOR, *PPCNODE_DESCRIPTOR;

typedef struct
{
    ULONG Version;
    const PCAUTOMATION_TABLE* AutomationTable;
    ULONG PinSize;
    ULONG PinCount;
    const PCPIN_DESCRIPTOR* Pins;
    ULONG NodeSize;
    ULONG NodeCount;
    const PCNODE_DESCRIPTOR* Nodes;
    ULONG ConnectionCount;
    const PCCONNECTION_DESCRIPTOR* Connections;
    ULONG CategoryCount;
    const GUID* Categories;
} PCFILTER_DESCRIPTOR, *PPCFILTER_DESCRIPTOR;

/* portcls.h DEFINE_PCAUTOMATION_TABLE_PROP, as it is */
#define DEFINE_PCAUTOMATION_TABLE_PROP(AutomationTable, PropertyTable) \
    const PCAUTOMATION_TABLE AutomationTable = {                        \
        sizeof(PropertyTable[0]),                                         \
        SIZEOF_ARRAY(PropertyTable),                                      \
        (const PCPROPERTY_ITEM *) PropertyTable,                          \
        0, 0, NULL,                                                       \
        0, 0, NULL,                                                       \
        0                                                                 \
    }

/* wdm.h MEMORY_CACHING_TYPE */
typedef enum _MEMORY_CACHING_TYPE
{
    MmNonCached = 0,
    MmCached = 1,
    MmWriteCombined = 2
} MEMORY_CACHING_TYPE;

/* The GUID symbols the tables and the data ranges name, with ksmedia.h's values */
static const GUID KSDATAFORMAT_TYPE_AUDIO = { 0x73647561, 0x0000, 0x0010, { 0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71 } };
static const GUID KSDATAFORMAT_TYPE_MUSIC = { 0xE725D360, 0x62CC, 0x11CF, { 0xA5, 0xD6, 0x28, 0xDB, 0x04, 0xC1, 0x00, 0x00 } };
static const GUID KSDATAFORMAT_SUBTYPE_PCM = { 0x00000001, 0x0000, 0x0010, { 0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71 } };
static const GUID KSDATAFORMAT_SUBTYPE_IEEE_FLOAT = { 0x00000003, 0x0000, 0x0010, { 0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71 } };
static const GUID KSDATAFORMAT_SUBTYPE_ANALOG = { 0x6dba3190, 0x67bd, 0x11cf, { 0xa0, 0xf7, 0x00, 0x20, 0xaf, 0xd1, 0x56, 0xe4 } };
static const GUID KSDATAFORMAT_SPECIFIER_NONE = { 0x0F6417D6, 0xC318, 0x11d0, { 0xa4, 0x3f, 0x00, 0xa0, 0xc9, 0x22, 0x31, 0x96 } };
static const GUID KSDATAFORMAT_SPECIFIER_WAVEFORMATEX = { 0x05589f81, 0xc356, 0x11ce, { 0xbf, 0x01, 0x00, 0xaa, 0x00, 0x55, 0x59, 0x5a } };
/* the format GUIDs' constant forms (ksmedia.h STATIC_KSDATAFORMAT_*), as the headers give them: eleven initializer values */
#define STATIC_KSDATAFORMAT_TYPE_AUDIO \
    0x73647561L, 0x0000, 0x0010, {0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71}
#define STATIC_KSDATAFORMAT_SUBTYPE_PCM \
    0x00000001L, 0x0000, 0x0010, {0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71}
#define STATIC_KSDATAFORMAT_SUBTYPE_ANALOG \
    0x6dba3190L, 0x67bd, 0x11cf, {0xa0, 0xf7, 0x00, 0x20, 0xaf, 0xd1, 0x56, 0xe4}
#define STATIC_KSDATAFORMAT_SPECIFIER_NONE \
    0x0F6417D6L, 0xC318, 0x11D0, {0xA4, 0x3F, 0x00, 0xA0, 0xC9, 0x22, 0x31, 0x96}
#define STATIC_KSDATAFORMAT_SPECIFIER_WAVEFORMATEX \
    0x05589f81L, 0xc356, 0x11ce, {0xbf, 0x01, 0x00, 0xaa, 0x00, 0x55, 0x59, 0x5a}
static const GUID KSPROPSETID_Audio = { 0x45BA12D0L, 0x7E1E, 0x11d0, { 0xa7, 0x5c, 0x00, 0xc0, 0x4f, 0xb1, 0x67, 0x50 } };
static const GUID KSPROPSETID_Jack = { 0x4509F757, 0x2D46, 0x42B8, { 0x8c, 0x17, 0x59, 0x86, 0x09, 0x9a, 0x83, 0x6f } };
static const GUID KSEVENTSETID_AudioControlChange = { 0xE85E9698, 0xFA2F, 0x11D1, { 0x95, 0xBD, 0x00, 0xC0, 0x4F, 0xB9, 0x25, 0xD3 } };
static const GUID KSNODETYPE_DAC = { 0x507AE360L, 0xC554, 0x11D0, { 0x8A, 0x2B, 0x00, 0xA0, 0xC9, 0x25, 0x5A, 0xC1 } };
static const GUID KSNODETYPE_ADC = { 0x4D837FE0L, 0xC555, 0x11D0, { 0x8A, 0x2B, 0x00, 0xA0, 0xC9, 0x25, 0x5A, 0xC1 } };
static const GUID KSNODETYPE_VOLUME = { 0x3A5ACC00L, 0xC557, 0x11D0, { 0x8A, 0x2B, 0x00, 0xA0, 0xC9, 0x25, 0x5A, 0xC1 } };
static const GUID KSNODETYPE_MUTE = { 0x02B223C0L, 0xC557, 0x11D0, { 0x8A, 0x2B, 0x00, 0xA0, 0xC9, 0x25, 0x5A, 0xC1 } };
static const GUID KSNODETYPE_SPEAKER = { 0xDFF21BE1L, 0xF70F, 0x11D0, { 0xB9, 0x17, 0x00, 0xA0, 0xC9, 0x22, 0x31, 0x96 } };
static const GUID KSNODETYPE_MICROPHONE = { 0xDFF21BEL, 0xF70F, 0x11D0, { 0xB9, 0x17, 0x00, 0xA0, 0xC9, 0x22, 0x31, 0x96 } };
static const GUID KSCATEGORY_AUDIO = { 0x6994AD04, 0x93EF, 0x11D0, { 0xA3, 0xCC, 0x00, 0xA0, 0xC9, 0x22, 0x31, 0x96 } };
static const GUID PINNAME_CAPTURE = { 0xDFF21BE4L, 0xF70F, 0x11D0, { 0xB9, 0x17, 0x00, 0xA0, 0xC9, 0x22, 0x31, 0x96 } };
static const GUID KSAUDFNAME_MASTER_VOLUME = { 0x185FEDC0L, 0x9905, 0x11D1, { 0x95, 0xA9, 0x00, 0xC0, 0x4F, 0xB9, 0x25, 0xD3 } };
static const GUID KSAUDFNAME_MASTER_MUTE = { 0x185FEDC1L, 0x9905, 0x11D1, { 0x95, 0xA9, 0x00, 0xC0, 0x4F, 0xB9, 0x25, 0xD3 } };
static const GUID KSAUDFNAME_PC_SPEAKER = { 0x185FEDD5L, 0x9905, 0x11D1, { 0x95, 0xA9, 0x00, 0xC0, 0x4F, 0xB9, 0x25, 0xD3 } };
static const GUID KSAUDFNAME_RECORDING_CONTROL = { 0x185FEDDFL, 0x9905, 0x11D1, { 0x95, 0xA9, 0x00, 0xC0, 0x4F, 0xB9, 0x25, 0xD3 } };

#endif
