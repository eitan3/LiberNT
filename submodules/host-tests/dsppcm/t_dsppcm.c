/*
 * PROJECT:     LiberNT host-native tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     The DSP endpoint driver's half free of I/O: the data ranges and the format negotiation of a streaming pin over an endpoint's formats (16, 24-in-32 and 32 bits, channel counts, refused rates and a packed 24-bit stream), the descriptor tables' invariants (every connection names an existing pin or node, every exposed property has a handler, the filters pair as the physical connection needs), and each wave pin method as one call into a fake audio interface with its errors propagated; under ASan and UBSan
 * COPYRIGHT:   Copyright 2026 LiberNT contributors
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* the position register is read plainly on the host */
#define DSPPCM_READ_POSITION(Register) (*(Register))

#include <ksendpoint.h>
#include "tables.h"
/* the engine's period-crossing rule over the cursors the position arithmetic reports: the header the render engine and its apitest compile (dll/win32/mmdevapi/reactos_wdmaud.c) */
#include "reactos_wavert.h"

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

/* The handlers of prophnd.cpp, as stand-ins the tables can point at */
NTSTATUS PropertyHandler_JackDescription(PPCPROPERTY_REQUEST Request) { UNREFERENCED_PARAMETER(Request); return STATUS_NOT_IMPLEMENTED; }
NTSTATUS PropertyHandler_Volume(PPCPROPERTY_REQUEST Request) { UNREFERENCED_PARAMETER(Request); return STATUS_NOT_IMPLEMENTED; }
NTSTATUS PropertyHandler_Mute(PPCPROPERTY_REQUEST Request) { UNREFERENCED_PARAMETER(Request); return STATUS_NOT_IMPLEMENTED; }
NTSTATUS PropertyHandler_ChannelConfig(PPCPROPERTY_REQUEST Request) { UNREFERENCED_PARAMETER(Request); return STATUS_NOT_IMPLEMENTED; }
NTSTATUS PropertyHandler_SpeakerGeometry(PPCPROPERTY_REQUEST Request) { UNREFERENCED_PARAMETER(Request); return STATUS_NOT_IMPLEMENTED; }
NTSTATUS EventHandler_Volume(PPCEVENT_REQUEST Request) { UNREFERENCED_PARAMETER(Request); return STATUS_NOT_IMPLEMENTED; }

static
BOOLEAN
SameGuid(
    const GUID* A,
    const GUID* B)
{
    return RtlCompareMemory(A, B, sizeof(GUID)) == sizeof(GUID);
}

/* (a) The format negotiation of a streaming pin over an endpoint's formats */

/* The loopback topology's formats as intcdsp.sys orders them in an endpoint (audioif.c IntcDspAudioFormatOrder): the deepest container first */
static const INTELDSP_FORMAT LoopbackFormats[] =
{
    { 48000, 2, 32, 32 },
    { 48000, 2, 24, 32 },
    { 48000, 2, 16, 16 }
};

static
VOID
FillEndpoint(
    _Out_ PINTELDSP_ENDPOINT Endpoint,
    BOOLEAN Capture,
    _In_reads_(Count) const INTELDSP_FORMAT* Formats,
    ULONG Count)
{
    RtlZeroMemory(Endpoint, sizeof(*Endpoint));
    strcpy(Endpoint->Name, "Loopback");
    Endpoint->Render = !Capture;
    Endpoint->Capture = Capture;
    /* the count is claimed as given, the array holds what fits: a count past the array is what the cases of the bound give the driver */
    Endpoint->FormatCount = Count;
    memcpy(Endpoint->Formats, Formats, (Count < INTELDSP_ENDPOINT_FORMATS_MAX ? Count : INTELDSP_ENDPOINT_FORMATS_MAX) * sizeof(Formats[0]));
    Endpoint->VolumeMinimum = -96L * 65536L;
    Endpoint->VolumeMaximum = 0;
    Endpoint->VolumeStep = 65536L;
}

static
VOID
CheckRange(
    const KSDATARANGE_AUDIO* Range,
    ULONG Channels,
    ULONG Bits,
    ULONG Rate)
{
    CHECK_EQ(Range->DataRange.FormatSize, sizeof(KSDATARANGE_AUDIO));
    CHECK_EQ(Range->DataRange.Flags, 0);
    CHECK_EQ(Range->DataRange.SampleSize, 0);
    CHECK_EQ(Range->DataRange.Reserved, 0);
    CHECK(SameGuid(&Range->DataRange.MajorFormat, &IntelDspAudioTypeAudio));
    CHECK(SameGuid(&Range->DataRange.SubFormat, &IntelDspAudioSubtypePcm));
    CHECK(SameGuid(&Range->DataRange.Specifier, &IntelDspAudioSpecifierWaveFormatEx));
    CHECK_EQ(Range->MaximumChannels, Channels);
    CHECK_EQ(Range->MinimumBitsPerSample, Bits);
    CHECK_EQ(Range->MaximumBitsPerSample, Bits);
    CHECK_EQ(Range->MinimumSampleFrequency, Rate);
    CHECK_EQ(Range->MaximumSampleFrequency, Rate);
}

static
VOID
CheckPinRanges(
    void)
{
    INTELDSP_ENDPOINT Endpoint;
    KSDATARANGE_AUDIO Ranges[DSPPCM_PIN_RANGES_MAX];
    PKSDATARANGE Pointers[DSPPCM_PIN_RANGES_MAX];
    /* several rates, channel counts and words, as intcdsp.sys sorts them (the rate ascending, the channels ascending, the container descending): a range per distinct rate, channels and container */
    static const INTELDSP_FORMAT Sorted[] =
    {
        { 44100, 1, 16, 16 }, { 48000, 2, 32, 32 }, { 48000, 2, 24, 32 }, { 48000, 2, 16, 16 }, { 96000, 2, 32, 32 }
    };
    /* the same formats in no order: the same set of ranges, each where it is first named */
    static const INTELDSP_FORMAT Unsorted[] =
    {
        { 48000, 2, 24, 32 }, { 96000, 2, 32, 32 }, { 44100, 1, 16, 16 }, { 48000, 2, 32, 32 }, { 48000, 2, 16, 16 }
    };
    INTELDSP_FORMAT Forty[40];
    ULONG I, Count;

    /* the loopback: one range for the 32-bit word (24 and 32 valid bits share it), one for the 16-bit */
    FillEndpoint(&Endpoint, FALSE, LoopbackFormats, 3);
    memset(Ranges, 0xA5, sizeof(Ranges));
    memset(Pointers, 0xA5, sizeof(Pointers));
    Count = DsppcmPinRangesBuild(&Endpoint, Ranges, Pointers);
    CHECK_EQ(Count, 2);
    CheckRange(&Ranges[0], 2, 32, 48000);
    CheckRange(&Ranges[1], 2, 16, 48000);
    CHECK_EQ(Pointers[0], (PKSDATARANGE)&Ranges[0]);
    CHECK_EQ(Pointers[1], (PKSDATARANGE)&Ranges[1]);
    /* the ranges and the pointers the count does not name are cleared, so a pin never offers a stale one */
    for (I = Count; I < DSPPCM_PIN_RANGES_MAX; I++)
    {
        CHECK_EQ(Pointers[I], (PKSDATARANGE)NULL);
        CHECK_EQ(Ranges[I].DataRange.FormatSize, 0);
    }
    /* an endpoint is a direction: the capture one builds the same ranges from the same formats */
    FillEndpoint(&Endpoint, TRUE, LoopbackFormats, 3);
    Count = DsppcmPinRangesBuild(&Endpoint, Ranges, Pointers);
    CHECK_EQ(Count, 2);
    CheckRange(&Ranges[0], 2, 32, 48000);
    /* several rates, channel counts and words: a range where any of the three changes */
    FillEndpoint(&Endpoint, FALSE, Sorted, 5);
    Count = DsppcmPinRangesBuild(&Endpoint, Ranges, Pointers);
    CHECK_EQ(Count, 4);
    CheckRange(&Ranges[0], 1, 16, 44100);
    CheckRange(&Ranges[1], 2, 32, 48000);
    CheckRange(&Ranges[2], 2, 16, 48000);
    CheckRange(&Ranges[3], 2, 32, 96000);
    /* the same set from the same formats in no order, each range where it is first named */
    FillEndpoint(&Endpoint, TRUE, Unsorted, 5);
    Count = DsppcmPinRangesBuild(&Endpoint, Ranges, Pointers);
    CHECK_EQ(Count, 4);
    CheckRange(&Ranges[0], 2, 32, 48000);
    CheckRange(&Ranges[1], 2, 32, 96000);
    CheckRange(&Ranges[2], 1, 16, 44100);
    CheckRange(&Ranges[3], 2, 16, 48000);
    /* an endpoint without formats offers none */
    FillEndpoint(&Endpoint, FALSE, LoopbackFormats, 0);
    Count = DsppcmPinRangesBuild(&Endpoint, Ranges, Pointers);
    CHECK_EQ(Count, 0);
    /* forty formats of one word and rate are one range; the pointer array is never overrun */
    for (I = 0; I < 40; I++)
        Forty[I] = LoopbackFormats[0];
    FillEndpoint(&Endpoint, FALSE, Forty, 40);
    Count = DsppcmPinRangesBuild(&Endpoint, Ranges, Pointers);
    CHECK_EQ(Count, 1);
    /* forty rates: at most the array's ranges, and only those the count names are set */
    for (I = 0; I < 40; I++)
    {
        Forty[I] = LoopbackFormats[0];
        Forty[I].SampleRate = 8000 + I * 100;
    }
    FillEndpoint(&Endpoint, FALSE, Forty, 40);
    Count = DsppcmPinRangesBuild(&Endpoint, Ranges, Pointers);
    CHECK(Count >= 1 && Count <= DSPPCM_PIN_RANGES_MAX);
    for (I = Count; I < DSPPCM_PIN_RANGES_MAX; I++)
        CHECK_EQ(Pointers[I], (PKSDATARANGE)NULL);
}

/* A PCM data format of WAVEFORMATEX, well formed, or bent where a case says */
typedef struct _FORMAT_CASE
{
    const char* Name;
    ULONG Rate;
    ULONG Channels;
    ULONG Container;
    ULONG Valid;        /* 0: the container's own */
    BOOLEAN Extensible;
    ULONG FormatSize;   /* 0: the right one */
    ULONG BlockAlign;   /* 0: the right one */
    ULONG ByteRate;     /* 0: the right one */
    ULONG Tag;          /* 0: WAVE_FORMAT_PCM, or the extensible form's */
    ULONG SampleSize;   /* 0: unset */
    const GUID* SubFormat;
    const GUID* MajorFormat;
    NTSTATUS Expected;
    ULONG Index;        /* the endpoint's format, when SUCCESS */
    ULONG CbSize;       /* 0: the form's own */
} FORMAT_CASE;

static
NTSTATUS
BuildFormat(
    _Out_writes_bytes_(Bytes) UCHAR* Buffer,
    ULONG Bytes,
    const FORMAT_CASE* C)
{
    KSDATAFORMAT_WAVEFORMATEX* Wave = (KSDATAFORMAT_WAVEFORMATEX*)Buffer;
    WAVEFORMATEXTENSIBLE* Extensible;
    ULONG Size;

    memset(Buffer, 0, Bytes);
    Size = C->Extensible ? sizeof(KSDATAFORMAT) + sizeof(WAVEFORMATEXTENSIBLE) : sizeof(KSDATAFORMAT_WAVEFORMATEX);
    Wave->DataFormat.FormatSize = C->FormatSize ? C->FormatSize : Size;
    Wave->DataFormat.MajorFormat = C->MajorFormat ? *C->MajorFormat : IntelDspAudioTypeAudio;
    Wave->DataFormat.SubFormat = C->SubFormat ? *C->SubFormat : IntelDspAudioSubtypePcm;
    Wave->DataFormat.Specifier = IntelDspAudioSpecifierWaveFormatEx;
    Wave->DataFormat.SampleSize = C->SampleSize;
    Wave->WaveFormatEx.wFormatTag = C->Extensible ? (C->Tag ? C->Tag : WAVE_FORMAT_EXTENSIBLE) : (C->Tag ? C->Tag : WAVE_FORMAT_PCM);
    Wave->WaveFormatEx.nChannels = (WORD)C->Channels;
    Wave->WaveFormatEx.nSamplesPerSec = C->Rate;
    Wave->WaveFormatEx.wBitsPerSample = (WORD)C->Container;
    Wave->WaveFormatEx.nBlockAlign = (WORD)(C->BlockAlign ? C->BlockAlign : C->Channels * (C->Container / 8));
    Wave->WaveFormatEx.nAvgBytesPerSec = C->ByteRate ? C->ByteRate : C->Rate * Wave->WaveFormatEx.nBlockAlign;
    Wave->WaveFormatEx.cbSize = (WORD)C->CbSize;
    if (C->Extensible)
    {
        Wave->WaveFormatEx.cbSize = C->CbSize ? (WORD)C->CbSize : sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX);
        Extensible = (WAVEFORMATEXTENSIBLE*)&Wave->WaveFormatEx;
        Extensible->Samples.wValidBitsPerSample = (WORD)(C->Valid ? C->Valid : C->Container);
        Extensible->SubFormat = C->SubFormat ? *C->SubFormat : IntelDspAudioSubtypePcm;
    }
    return STATUS_SUCCESS;
}

static
VOID
CheckPinFormatFind(
    void)
{
    /* the subformats and the major format a case refuses as another kind of stream */
    static const GUID FloatSub = { 0x00000003, 0x0000, 0x0010, { 0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71 } };
    static const GUID Music = { 0xE725D360, 0x62CC, 0x11CF, { 0xA5, 0xD6, 0x28, 0xDB, 0x04, 0xC1, 0x00, 0x00 } };
    static const FORMAT_CASE Cases[] =
    {
        { "48 kHz 2 ch 16-bit",                 48000, 2, 16, 0,  FALSE, 0, 0, 0, 0, 0, NULL, NULL, STATUS_SUCCESS, 2 },
        { "48 kHz 2 ch 24 valid in 32",         48000, 2, 32, 24, TRUE,  0, 0, 0, 0, 0, NULL, NULL, STATUS_SUCCESS, 1 },
        { "48 kHz 2 ch 32 in 32",               48000, 2, 32, 0,  TRUE,  0, 0, 0, 0, 0, NULL, NULL, STATUS_SUCCESS, 0 },
        { "48 kHz 2 ch 32 in 32, plain",        48000, 2, 32, 0,  FALSE, 0, 0, 0, 0, 0, NULL, NULL, STATUS_SUCCESS, 0 },
        { "a packed 24-bit stream",             48000, 2, 24, 0,  FALSE, 0, 0, 0, 0, 0, NULL, NULL, STATUS_NOT_SUPPORTED, 0 },
        { "a packed 24-bit stream, extensible", 48000, 2, 24, 24, TRUE,  0, 0, 0, 0, 0, NULL, NULL, STATUS_NOT_SUPPORTED, 0 },
        { "20 valid bits in 32, not offered",   48000, 2, 32, 20, TRUE,  0, 0, 0, 0, 0, NULL, NULL, STATUS_NOT_SUPPORTED, 0 },
        { "8-bit, not offered",                 48000, 2, 8,  0,  FALSE, 0, 0, 0, 0, 0, NULL, NULL, STATUS_NOT_SUPPORTED, 0 },
        { "44.1 kHz, refused",                  44100, 2, 16, 0,  FALSE, 0, 0, 0, 0, 0, NULL, NULL, STATUS_NOT_SUPPORTED, 0 },
        { "96 kHz, refused",                    96000, 2, 32, 0,  FALSE, 0, 0, 0, 0, 0, NULL, NULL, STATUS_NOT_SUPPORTED, 0 },
        { "one channel, refused",               48000, 1, 16, 0,  FALSE, 0, 0, 0, 0, 0, NULL, NULL, STATUS_NOT_SUPPORTED, 0 },
        { "six channels, refused",              48000, 6, 16, 0,  FALSE, 0, 0, 0, 0, 0, NULL, NULL, STATUS_NOT_SUPPORTED, 0 },
        { "16 valid bits in a 24-bit word",     48000, 2, 24, 16, TRUE,  0, 0, 0, 0, 0, NULL, NULL, STATUS_NOT_SUPPORTED, 0 },
        { "valid bits beyond the word",         48000, 2, 16, 24, TRUE,  0, 0, 0, 0, 0, NULL, NULL, STATUS_INVALID_PARAMETER, 0 },
        { "a block alignment off by one",       48000, 2, 16, 0,  FALSE, 0, 5, 0, 0, 0, NULL, NULL, STATUS_INVALID_PARAMETER, 0 },
        { "a byte rate off",                    48000, 2, 16, 0,  FALSE, 0, 0, 191999, 0, 0, NULL, NULL, STATUS_INVALID_PARAMETER, 0 },
        { "a size too small",                   48000, 2, 16, 0,  FALSE, 20, 0, 0, 0, 0, NULL, NULL, STATUS_INVALID_PARAMETER, 0 },
        { "PCM with a cbSize",                  48000, 2, 16, 0,  FALSE, 0, 0, 0, 0, 0, NULL, NULL, STATUS_INVALID_PARAMETER, 0, 22 },
        { "a sample size that disagrees",       48000, 2, 16, 0,  FALSE, 0, 0, 0, 0, 8, NULL, NULL, STATUS_INVALID_PARAMETER, 0 },
        { "no channels",                        48000, 0, 16, 0,  FALSE, 0, 0, 0, 0, 0, NULL, NULL, STATUS_INVALID_PARAMETER, 0 },
        { "float",                              48000, 2, 32, 0,  FALSE, 0, 0, 0, 3, 0, NULL, NULL, STATUS_NOT_SUPPORTED, 0 },
        { "extensible float",                   48000, 2, 32, 0,  TRUE,  0, 0, 0, 0, 0, &FloatSub, NULL, STATUS_NOT_SUPPORTED, 0 },
        { "not audio",                          48000, 2, 16, 0,  FALSE, 0, 0, 0, 0, 0, NULL, &Music, STATUS_NOT_SUPPORTED, 0 },
        { "music",                              48000, 2, 16, 0,  FALSE, 0, 0, 0, 0, 0, &Music, NULL, STATUS_NOT_SUPPORTED, 0 },
        { "extensible with the PCM tag",        48000, 2, 32, 0,  TRUE,  0, 0, 0, 1, 0, NULL, NULL, STATUS_INVALID_PARAMETER, 0 }
    };
    INTELDSP_ENDPOINT Endpoint;
    UCHAR Buffer[128];
    ULONG Index, I;
    NTSTATUS Status;

    for (I = 0; I < sizeof(Cases) / sizeof(Cases[0]); I++)
    {
        FillEndpoint(&Endpoint, (I & 1) != 0, LoopbackFormats, 3);
        Index = 99;
        BuildFormat(Buffer, sizeof(Buffer), &Cases[I]);
        Status = DsppcmPinFormatFind(&Endpoint, (PKSDATAFORMAT)Buffer, &Index);
        if (Status != Cases[I].Expected)
            fprintf(stderr, "case %u (%s): status %lx, expected %lx\n", I, Cases[I].Name, (unsigned long)Status,
                    (unsigned long)Cases[I].Expected);
        CHECK(Status == Cases[I].Expected);
        if (NT_SUCCESS(Cases[I].Expected))
            CHECK_EQ(Index, Cases[I].Index);
    }
    Index = 99;
    CHECK(DsppcmPinFormatFind(&Endpoint, NULL, &Index) == STATUS_INVALID_PARAMETER);
    CHECK_EQ(Index, 99);
    /* the rule itself: the same format with the right numbers for a 24-bit word is refused, and the reason is the word, not a malformed format */
    {
        FORMAT_CASE Packed = { "packed", 48000, 2, 24, 0, FALSE, 0, 0, 0, 0, 0, NULL, NULL, STATUS_NOT_SUPPORTED, 0 };
        FORMAT_CASE Valid24 = { "24 in 32", 48000, 2, 32, 24, TRUE, 0, 0, 0, 0, 0, NULL, NULL, STATUS_SUCCESS, 1 };
        ULONG Rate, Channels, Valid, Container;

        BuildFormat(Buffer, sizeof(Buffer), &Packed);
        Status = IntelDspAudioFormatParse((PKSDATAFORMAT)Buffer, &Rate, &Channels, &Valid, &Container);
        CHECK(Status == STATUS_NOT_SUPPORTED);
        BuildFormat(Buffer, sizeof(Buffer), &Valid24);
        Status = IntelDspAudioFormatParse((PKSDATAFORMAT)Buffer, &Rate, &Channels, &Valid, &Container);
        CHECK(Status == STATUS_SUCCESS);
        CHECK_EQ(Rate, 48000);
        CHECK_EQ(Channels, 2);
        CHECK_EQ(Valid, 24);
        CHECK_EQ(Container, 32);
    }
    /* the lookup by the four numbers, which the interface answers before a stream opens */
    CHECK(IntelDspAudioFormatFind(&Endpoint, 48000, 2, 24, 32, &Index) == STATUS_SUCCESS);
    CHECK_EQ(Index, 1);
    CHECK(IntelDspAudioFormatFind(&Endpoint, 48000, 2, 24, 24, &Index) == STATUS_NOT_SUPPORTED);
}

/* (b) The descriptor tables' invariants */

static
BOOLEAN
KnownHandler(
    PCPFNPROPERTY_HANDLER Handler)
{
    return Handler == PropertyHandler_JackDescription || Handler == PropertyHandler_Volume || Handler == PropertyHandler_Mute ||
           Handler == PropertyHandler_ChannelConfig || Handler == PropertyHandler_SpeakerGeometry;
}

static
VOID
CheckAutomation(
    const char* Name,
    const PCAUTOMATION_TABLE* A)
{
    ULONG I;

    CHECK_EQ(A->PropertyItemSize, sizeof(PCPROPERTY_ITEM));
    CHECK(A->MethodCount == 0);
    CHECK(A->Methods == NULL);
    /* an automation table without events names no event item size (portcls.h DEFINE_PCAUTOMATION_TABLE_PROP) */
    if (A->EventCount == 0)
    {
        CHECK_EQ(A->EventItemSize, 0);
        CHECK(A->Events == NULL);
    }
    else
    {
        CHECK_EQ(A->EventItemSize, sizeof(PCEVENT_ITEM));
    }
    CHECK_EQ(A->Reserved, 0);
    if (A->PropertyCount != 0)
        CHECK(A->Properties != NULL);
    for (I = 0; I < A->PropertyCount; I++)
    {
        /* every exposed property names a set, a verb among get, set and basic support, and a handler of this driver */
        CHECK(A->Properties[I].Set != NULL);
        CHECK((A->Properties[I].Flags & ~(KSPROPERTY_TYPE_GET | KSPROPERTY_TYPE_SET | KSPROPERTY_TYPE_BASICSUPPORT)) == 0);
        CHECK((A->Properties[I].Flags & (KSPROPERTY_TYPE_GET | KSPROPERTY_TYPE_SET)) != 0);
        CHECK(KnownHandler(A->Properties[I].Handler));
    }
    if (A->EventCount != 0)
        CHECK(A->Events != NULL);
    for (I = 0; I < A->EventCount; I++)
    {
        CHECK(A->Events[I].Set != NULL);
        CHECK(A->Events[I].Handler == EventHandler_Volume);
    }
}

static
VOID
CheckBridgePin(
    const char* Name,
    const PCPIN_DESCRIPTOR* Pin,
    KSPIN_DATAFLOW Flow)
{
    CHECK_EQ(Pin->MaxGlobalInstanceCount, 0);
    CHECK_EQ(Pin->MaxFilterInstanceCount, 0);
    CHECK_EQ(Pin->MinFilterInstanceCount, 0);
    CHECK_EQ(Pin->KsPinDescriptor.Communication, KSPIN_COMMUNICATION_NONE);
    CHECK_EQ(Pin->KsPinDescriptor.DataFlow, Flow);
    CHECK_EQ(Pin->KsPinDescriptor.DataRangesCount, 1);
    CHECK(Pin->KsPinDescriptor.DataRanges != NULL && Pin->KsPinDescriptor.DataRanges[0] != NULL);
    /* a failed check counts and goes on: the ranges are read only when there are any */
    if (Pin->KsPinDescriptor.DataRanges == NULL || Pin->KsPinDescriptor.DataRanges[0] == NULL)
        return;
    CHECK_EQ(Pin->KsPinDescriptor.DataRanges[0]->FormatSize, sizeof(KSDATARANGE));
    CHECK(SameGuid(&Pin->KsPinDescriptor.DataRanges[0]->MajorFormat, &IntelDspAudioTypeAudio));
    CHECK(SameGuid(&Pin->KsPinDescriptor.DataRanges[0]->SubFormat, &KSDATAFORMAT_SUBTYPE_ANALOG));
    CHECK(SameGuid(&Pin->KsPinDescriptor.DataRanges[0]->Specifier, &KSDATAFORMAT_SPECIFIER_NONE));
    CHECK(Pin->KsPinDescriptor.Category != NULL);
}

static
VOID
CheckStreamPin(
    const char* Name,
    const PCPIN_DESCRIPTOR* Pin,
    KSPIN_DATAFLOW Flow)
{
    CHECK(Pin->MaxGlobalInstanceCount >= 1);
    CHECK(Pin->MaxFilterInstanceCount >= 1);
    CHECK_EQ(Pin->MinFilterInstanceCount, 0);
    CHECK_EQ(Pin->KsPinDescriptor.Communication, KSPIN_COMMUNICATION_SINK);
    CHECK_EQ(Pin->KsPinDescriptor.DataFlow, Flow);
    /* in the static tables the streaming pin carries no range: the adapter fills the endpoint's (DsppcmPinRangesBuild), and nothing stale is left */
    CHECK_EQ(Pin->KsPinDescriptor.DataRangesCount, 0);
    CHECK(Pin->KsPinDescriptor.DataRanges == NULL);
    CHECK(Pin->KsPinDescriptor.Category != NULL);
    CHECK(Pin->KsPinDescriptor.Name != NULL);
}

static
VOID
CheckFilter(
    const char* Name,
    const PCFILTER_DESCRIPTOR* F)
{
    ULONG I, J, From, To, Sources, Sinks;

    CHECK_EQ(F->Version, 0);
    CHECK_EQ(F->PinSize, sizeof(PCPIN_DESCRIPTOR));
    CHECK_EQ(F->NodeSize, sizeof(PCNODE_DESCRIPTOR));
    CHECK_EQ(F->CategoryCount, 0);
    CHECK(F->Categories == NULL);
    CHECK(F->Pins != NULL);
    CHECK(F->Nodes != NULL);
    CHECK(F->Connections != NULL);
    CHECK(F->PinCount == 2);
    /* a chain: every node between two connections, and the two filter pins at its ends */
    CHECK(F->ConnectionCount == F->NodeCount + 1);
    if (F->AutomationTable != NULL)
        CheckAutomation(Name, F->AutomationTable);
    for (I = 0; I < F->PinCount; I++)
    {
        CHECK(F->Pins[I].KsPinDescriptor.InterfacesCount == 0);
        CHECK(F->Pins[I].KsPinDescriptor.MediumsCount == 0);
        if (F->Pins[I].AutomationTable != NULL)
            CheckAutomation(Name, F->Pins[I].AutomationTable);
    }
    for (I = 0; I < F->NodeCount; I++)
    {
        CHECK(F->Nodes[I].Type != NULL);
        if (F->Nodes[I].AutomationTable != NULL)
            CheckAutomation(Name, F->Nodes[I].AutomationTable);
    }
    /* every connection names an existing pin or node, a filter pin with the connection's flow, a node's output as a source and its input as a sink */
    for (I = 0; I < F->ConnectionCount; I++)
    {
        From = F->Connections[I].FromNode;
        To = F->Connections[I].ToNode;
        if (From == KSFILTER_NODE)
        {
            CHECK(F->Connections[I].FromNodePin < F->PinCount);
            /* the signal enters the filter at a pin data flows into ... */
            CHECK(F->Pins[F->Connections[I].FromNodePin].KsPinDescriptor.DataFlow == KSPIN_DATAFLOW_IN);
        }
        else
        {
            CHECK(From < F->NodeCount);
            CHECK_EQ(F->Connections[I].FromNodePin, 0);
        }
        if (To == KSFILTER_NODE)
        {
            CHECK(F->Connections[I].ToNodePin < F->PinCount);
            /* ... and leaves it at a pin data flows out of (the capture topology's jack is a source the filter's data flows into) */
            CHECK(F->Pins[F->Connections[I].ToNodePin].KsPinDescriptor.DataFlow == KSPIN_DATAFLOW_OUT);
        }
        else
        {
            CHECK(To < F->NodeCount);
            CHECK_EQ(F->Connections[I].ToNodePin, 1);
        }
    }
    /* every node is on the path: one connection leaves it and one enters it */
    for (I = 0; I < F->NodeCount; I++)
    {
        Sources = Sinks = 0;
        for (J = 0; J < F->ConnectionCount; J++)
        {
            Sources += F->Connections[J].FromNode == I;
            Sinks += F->Connections[J].ToNode == I;
        }
        CHECK_EQ(Sources, 1);
        CHECK_EQ(Sinks, 1);
    }
}

/* Whether a filter holds a connection exactly as named */
static
BOOLEAN
HasConnection(
    const PCFILTER_DESCRIPTOR* F,
    ULONG FromNode,
    ULONG FromPin,
    ULONG ToNode,
    ULONG ToPin)
{
    ULONG I;

    for (I = 0; I < F->ConnectionCount; I++)
        if (F->Connections[I].FromNode == FromNode && F->Connections[I].FromNodePin == FromPin &&
            F->Connections[I].ToNode == ToNode && F->Connections[I].ToNodePin == ToPin)
            return TRUE;
    return FALSE;
}

/* The one property of a set and id a filter's node 0 exposes, with its verbs */
static
ULONG
NodePropertyFlags(
    const PCFILTER_DESCRIPTOR* F,
    ULONG Node,
    const GUID* Set,
    ULONG Id)
{
    const PCAUTOMATION_TABLE* A;
    ULONG I;

    if (Node >= F->NodeCount || F->Nodes[Node].AutomationTable == NULL)
        return 0;
    A = F->Nodes[Node].AutomationTable;
    for (I = 0; I < A->PropertyCount; I++)
        if (SameGuid(A->Properties[I].Set, Set) && A->Properties[I].Id == Id)
            return A->Properties[I].Flags;
    return 0;
}

static
ULONG
FilterPropertyFlags(
    const PCFILTER_DESCRIPTOR* F,
    const GUID* Set,
    ULONG Id)
{
    const PCAUTOMATION_TABLE* A;
    ULONG I;

    if (F->AutomationTable == NULL)
        return 0;
    A = F->AutomationTable;
    for (I = 0; I < A->PropertyCount; I++)
        if (SameGuid(A->Properties[I].Set, Set) && A->Properties[I].Id == Id)
            return A->Properties[I].Flags;
    return 0;
}

static
VOID
CheckTables(
    void)
{
    const PCFILTER_DESCRIPTOR* WaveOut = WaveOutFilterDescription;
    const PCFILTER_DESCRIPTOR* WaveIn = WaveInFilterDescription;
    const PCFILTER_DESCRIPTOR* TopoOut = TopoOutFilterDescription;
    const PCFILTER_DESCRIPTOR* TopoIn = TopoInFilterDescription;
    ULONG Flags;

    CHECK_EQ(SIZEOF_ARRAY(WaveOutFilterDescription), 1);
    CHECK_EQ(SIZEOF_ARRAY(WaveInFilterDescription), 1);
    CHECK_EQ(SIZEOF_ARRAY(TopoOutFilterDescription), 1);
    CHECK_EQ(SIZEOF_ARRAY(TopoInFilterDescription), 1);
    CHECK_EQ(SIZEOF_ARRAY(WaveOutPins), 2);
    CHECK_EQ(SIZEOF_ARRAY(WaveInPins), 2);
    CHECK_EQ(SIZEOF_ARRAY(TopoOutPins), 2);
    CHECK_EQ(SIZEOF_ARRAY(TopoInPins), 2);
    /* every pin and node descriptor the filters name is one of the tables' */
    CHECK(WaveOut->Pins == WaveOutPins);
    CHECK(WaveOut->Nodes == DacNode);
    CHECK(WaveOut->Connections == DacConnections);
    CHECK(WaveIn->Pins == WaveInPins);
    CHECK(WaveIn->Nodes == AdcNode);
    CHECK(WaveIn->Connections == AdcConnections);
    CHECK(TopoOut->Pins == TopoOutPins);
    CHECK(TopoOut->Nodes == TopoOutNodes);
    CHECK(TopoOut->Connections == TopoOutConnections);
    CHECK(TopoIn->Pins == TopoInPins);
    CHECK(TopoIn->Nodes == TopoInNodes);
    CHECK(TopoIn->Connections == TopoInConnections);

    CheckFilter("WaveOut", WaveOut);
    CheckFilter("WaveIn", WaveIn);
    CheckFilter("TopoOut", TopoOut);
    CheckFilter("TopoIn", TopoIn);

    /* the render wave filter: pin 0 takes the stream in, pin 1 is the bridge out to the topology; node 0 the DAC with the channel properties */
    CheckStreamPin("WaveOut", &WaveOut->Pins[DSPPCM_WAVE_STREAM_PIN], KSPIN_DATAFLOW_IN);
    CheckBridgePin("WaveOut", &WaveOut->Pins[DSPPCM_WAVE_BRIDGE_PIN], KSPIN_DATAFLOW_OUT);
    CHECK_EQ(WaveOut->NodeCount, 1);
    CHECK(SameGuid(WaveOut->Nodes[0].Type, &KSNODETYPE_DAC));
    Flags = NodePropertyFlags(WaveOut, 0, &KSPROPSETID_Audio, KSPROPERTY_AUDIO_CHANNEL_CONFIG);
    CHECK_EQ(Flags, KSPROPERTY_TYPE_GET | KSPROPERTY_TYPE_SET);
    Flags = NodePropertyFlags(WaveOut, 0, &KSPROPSETID_Audio, KSPROPERTY_AUDIO_STEREO_SPEAKER_GEOMETRY);
    CHECK_EQ(Flags, KSPROPERTY_TYPE_GET | KSPROPERTY_TYPE_SET);
    CHECK(HasConnection(WaveOut, KSFILTER_NODE, 0, 0, 1));
    CHECK(HasConnection(WaveOut, 0, 0, KSFILTER_NODE, 1));

    /* the capture wave filter: the reverse flows, node 0 the ADC without properties, as the codec driver's */
    CheckStreamPin("WaveIn", &WaveIn->Pins[DSPPCM_WAVE_STREAM_PIN], KSPIN_DATAFLOW_OUT);
    CheckBridgePin("WaveIn", &WaveIn->Pins[DSPPCM_WAVE_BRIDGE_PIN], KSPIN_DATAFLOW_IN);
    CHECK_EQ(WaveIn->NodeCount, 1);
    CHECK(SameGuid(WaveIn->Nodes[0].Type, &KSNODETYPE_ADC));
    CHECK(WaveIn->Nodes[0].AutomationTable == NULL);
    CHECK(HasConnection(WaveIn, KSFILTER_NODE, 1, 0, 1));
    CHECK(HasConnection(WaveIn, 0, 0, KSFILTER_NODE, 0));

    /* the render topology: the wave comes in at pin 0 through volume and mute to the speaker at pin 1; both nodes expose their property with get, set and basic support */
    CheckBridgePin("TopoOut", &TopoOut->Pins[DSPPCM_TOPO_WAVE_PIN], KSPIN_DATAFLOW_IN);
    CheckBridgePin("TopoOut", &TopoOut->Pins[DSPPCM_TOPO_JACK_PIN], KSPIN_DATAFLOW_OUT);
    CHECK(SameGuid(TopoOut->Pins[DSPPCM_TOPO_JACK_PIN].KsPinDescriptor.Category, &KSNODETYPE_SPEAKER));
    CHECK_EQ(TopoOut->NodeCount, 2);
    CHECK(SameGuid(TopoOut->Nodes[0].Type, &KSNODETYPE_VOLUME));
    CHECK(SameGuid(TopoOut->Nodes[1].Type, &KSNODETYPE_MUTE));
    CHECK(SameGuid(TopoOut->Nodes[0].Name, &KSAUDFNAME_MASTER_VOLUME));
    CHECK(SameGuid(TopoOut->Nodes[1].Name, &KSAUDFNAME_MASTER_MUTE));
    Flags = NodePropertyFlags(TopoOut, 0, &KSPROPSETID_Audio, KSPROPERTY_AUDIO_VOLUMELEVEL);
    CHECK_EQ(Flags, KSPROPERTY_TYPE_GET | KSPROPERTY_TYPE_SET | KSPROPERTY_TYPE_BASICSUPPORT);
    Flags = NodePropertyFlags(TopoOut, 1, &KSPROPSETID_Audio, KSPROPERTY_AUDIO_MUTE);
    CHECK_EQ(Flags, KSPROPERTY_TYPE_GET | KSPROPERTY_TYPE_SET | KSPROPERTY_TYPE_BASICSUPPORT);
    CHECK(NodePropertyFlags(TopoOut, 1, &KSPROPSETID_Audio, KSPROPERTY_AUDIO_VOLUMELEVEL) == 0);
    CHECK(HasConnection(TopoOut, KSFILTER_NODE, 0, 0, 1));
    CHECK(HasConnection(TopoOut, 0, 0, 1, 1));
    CHECK(HasConnection(TopoOut, 1, 0, KSFILTER_NODE, 1));
    Flags = FilterPropertyFlags(TopoOut, &KSPROPSETID_Jack, KSPROPERTY_JACK_DESCRIPTION);
    CHECK_EQ(Flags, KSPROPERTY_TYPE_GET | KSPROPERTY_TYPE_BASICSUPPORT);

    /* the capture topology: the microphone at pin 1 through volume and mute out to the wave at pin 0, the nodes named as the codec names them */
    CheckBridgePin("TopoIn", &TopoIn->Pins[DSPPCM_TOPO_WAVE_PIN], KSPIN_DATAFLOW_OUT);
    CheckBridgePin("TopoIn", &TopoIn->Pins[DSPPCM_TOPO_JACK_PIN], KSPIN_DATAFLOW_IN);
    CHECK(SameGuid(TopoIn->Pins[DSPPCM_TOPO_JACK_PIN].KsPinDescriptor.Category, &KSNODETYPE_MICROPHONE));
    CHECK_EQ(TopoIn->NodeCount, 2);
    CHECK(SameGuid(TopoIn->Nodes[0].Type, &KSNODETYPE_VOLUME));
    CHECK(SameGuid(TopoIn->Nodes[1].Type, &KSNODETYPE_MUTE));
    Flags = NodePropertyFlags(TopoIn, 0, &KSPROPSETID_Audio, KSPROPERTY_AUDIO_VOLUMELEVEL);
    CHECK_EQ(Flags, KSPROPERTY_TYPE_GET | KSPROPERTY_TYPE_SET | KSPROPERTY_TYPE_BASICSUPPORT);
    Flags = NodePropertyFlags(TopoIn, 1, &KSPROPSETID_Audio, KSPROPERTY_AUDIO_MUTE);
    CHECK_EQ(Flags, KSPROPERTY_TYPE_GET | KSPROPERTY_TYPE_SET | KSPROPERTY_TYPE_BASICSUPPORT);
    CHECK(HasConnection(TopoIn, KSFILTER_NODE, 1, 0, 1));
    CHECK(HasConnection(TopoIn, 0, 0, 1, 1));
    CHECK(HasConnection(TopoIn, 1, 0, KSFILTER_NODE, 0));
    Flags = FilterPropertyFlags(TopoIn, &KSPROPSETID_Jack, KSPROPERTY_JACK_DESCRIPTION);
    CHECK_EQ(Flags, KSPROPERTY_TYPE_GET | KSPROPERTY_TYPE_BASICSUPPORT);

    /* the pairs the physical connection joins: a wave bridge pin flows out where the topology's wave pin flows in (render), and in where it flows out (capture) */
    CHECK(WaveOut->Pins[DSPPCM_WAVE_BRIDGE_PIN].KsPinDescriptor.DataFlow != TopoOut->Pins[DSPPCM_TOPO_WAVE_PIN].KsPinDescriptor.DataFlow);
    CHECK(WaveIn->Pins[DSPPCM_WAVE_BRIDGE_PIN].KsPinDescriptor.DataFlow != TopoIn->Pins[DSPPCM_TOPO_WAVE_PIN].KsPinDescriptor.DataFlow);
    /* the stream pins' categories are the codec driver's: audio for render, the capture name for capture */
    CHECK(SameGuid(WaveOut->Pins[DSPPCM_WAVE_STREAM_PIN].KsPinDescriptor.Category, &KSCATEGORY_AUDIO));
    CHECK(SameGuid(WaveIn->Pins[DSPPCM_WAVE_STREAM_PIN].KsPinDescriptor.Category, &PINNAME_CAPTURE));
}

/* (c) Each wave pin method as one call into the interface */

typedef enum _FAKE_KIND
{
    KGetEndpointCount = 0,
    KGetEndpoint,
    KOpenStream,
    KAllocateBuffer,
    KSetState,
    KGetPositionRegister,
    KGetClockRegister,
    KGetLatency,
    KRegisterNotificationEvent,
    KUnregisterNotificationEvent,
    KFreeBuffer,
    KCloseStream,
    KGetControl,
    KSetControl,
    KKindCount
} FAKE_KIND;

typedef struct _FAKE
{
    INTELDSP_AUDIO_INTERFACE Iface;
    ULONG Calls[KKindCount];
    PVOID Context[KKindCount];
    HANDLE Stream[KKindCount];
    ULONG A[KKindCount], B[KKindCount];
    PVOID P[KKindCount], Q[KKindCount];
    KSSTATE State[KKindCount];
    INTELDSP_CONTROL Control[KKindCount];
    NTSTATUS Result[KKindCount];
    /* what the allocate and the registers hand out */
    PMDL Mdl;
    ULONG Bytes, Offset, Fifo;
    ULONG RegisterValue;
    ULONG* Register;
    KSRTAUDIO_HWLATENCY Latency;
    ULONG EndpointCount;
    INTELDSP_ENDPOINT Endpoint;
} FAKE;

static FAKE* G;

static
NTSTATUS
FakeRecord(
    FAKE_KIND Kind,
    PVOID Context,
    HANDLE Stream)
{
    G->Calls[Kind]++;
    G->Context[Kind] = Context;
    G->Stream[Kind] = Stream;
    return G->Result[Kind];
}

static ULONG FakeTotalCalls(void)
{
    ULONG I, Total = 0;

    for (I = 0; I < KKindCount; I++)
        Total += G->Calls[I];
    return Total;
}

static NTSTATUS FakeGetEndpointCount(PVOID Context, PULONG Count) { if (NT_SUCCESS(FakeRecord(KGetEndpointCount, Context, NULL))) *Count = G->EndpointCount; return G->Result[KGetEndpointCount]; }
static NTSTATUS FakeGetEndpoint(PVOID Context, ULONG Index, PINTELDSP_ENDPOINT Endpoint) { G->A[KGetEndpoint] = Index; if (NT_SUCCESS(FakeRecord(KGetEndpoint, Context, NULL))) *Endpoint = G->Endpoint; return G->Result[KGetEndpoint]; }
static NTSTATUS FakeOpenStream(PVOID Context, ULONG EndpointIndex, BOOLEAN Capture, PKSDATAFORMAT DataFormat, PHANDLE Stream) { G->A[KOpenStream] = EndpointIndex; G->B[KOpenStream] = Capture; G->P[KOpenStream] = DataFormat; if (NT_SUCCESS(FakeRecord(KOpenStream, Context, NULL))) *Stream = (HANDLE)0x1234; return G->Result[KOpenStream]; }
static NTSTATUS FakeAllocateBuffer(PVOID Context, HANDLE Stream, ULONG NotificationCount, ULONG RequestedBytes, PMDL* Mdl, PULONG Bytes, PULONG OffsetFromFirstPage, PULONG FifoSize)
{
    G->A[KAllocateBuffer] = NotificationCount;
    G->B[KAllocateBuffer] = RequestedBytes;
    if (NT_SUCCESS(FakeRecord(KAllocateBuffer, Context, Stream)))
    {
        *Mdl = G->Mdl;
        *Bytes = G->Bytes;
        *OffsetFromFirstPage = G->Offset;
        *FifoSize = G->Fifo;
    }
    return G->Result[KAllocateBuffer];
}
static NTSTATUS FakeSetState(PVOID Context, HANDLE Stream, KSSTATE State) { G->State[KSetState] = State; return FakeRecord(KSetState, Context, Stream); }
static NTSTATUS FakeGetPositionRegister(PVOID Context, HANDLE Stream, PULONG* Position) { if (NT_SUCCESS(FakeRecord(KGetPositionRegister, Context, Stream))) *Position = G->Register; return G->Result[KGetPositionRegister]; }
static NTSTATUS FakeGetClockRegister(PVOID Context, PULONG* Wallclock) { if (NT_SUCCESS(FakeRecord(KGetClockRegister, Context, NULL))) *Wallclock = &G->RegisterValue; return G->Result[KGetClockRegister]; }
static NTSTATUS FakeGetLatency(PVOID Context, HANDLE Stream, PKSRTAUDIO_HWLATENCY Latency) { if (NT_SUCCESS(FakeRecord(KGetLatency, Context, Stream))) *Latency = G->Latency; return G->Result[KGetLatency]; }
static NTSTATUS FakeRegisterNotificationEvent(PVOID Context, HANDLE Stream, PKEVENT Event) { G->P[KRegisterNotificationEvent] = Event; return FakeRecord(KRegisterNotificationEvent, Context, Stream); }
static NTSTATUS FakeUnregisterNotificationEvent(PVOID Context, HANDLE Stream, PKEVENT Event) { G->P[KUnregisterNotificationEvent] = Event; return FakeRecord(KUnregisterNotificationEvent, Context, Stream); }
static NTSTATUS FakeFreeBuffer(PVOID Context, HANDLE Stream, PMDL Mdl, ULONG Bytes) { G->P[KFreeBuffer] = Mdl; G->A[KFreeBuffer] = Bytes; return FakeRecord(KFreeBuffer, Context, Stream); }
static NTSTATUS FakeCloseStream(PVOID Context, HANDLE Stream) { return FakeRecord(KCloseStream, Context, Stream); }
static NTSTATUS FakeGetControl(PVOID Context, ULONG EndpointIndex, INTELDSP_CONTROL Control, PLONG Value) { G->A[KGetControl] = EndpointIndex; G->Control[KGetControl] = Control; if (NT_SUCCESS(FakeRecord(KGetControl, Context, NULL))) *Value = -1234; return G->Result[KGetControl]; }
static NTSTATUS FakeSetControl(PVOID Context, ULONG EndpointIndex, INTELDSP_CONTROL Control, LONG Value) { G->A[KSetControl] = EndpointIndex; G->Control[KSetControl] = Control; G->B[KSetControl] = (ULONG)Value; return FakeRecord(KSetControl, Context, NULL); }

static
VOID
FakeInit(
    FAKE* F)
{
    memset(F, 0, sizeof(*F));
    G = F;
    F->Iface.Size = sizeof(F->Iface);
    F->Iface.Version = INTELDSP_AUDIO_INTERFACE_VERSION;
    F->Iface.Context = (PVOID)0x5A5A5A5Au;
    F->Iface.GetEndpointCount = FakeGetEndpointCount;
    F->Iface.GetEndpoint = FakeGetEndpoint;
    F->Iface.OpenStream = FakeOpenStream;
    F->Iface.AllocateBuffer = FakeAllocateBuffer;
    F->Iface.SetState = FakeSetState;
    F->Iface.GetPositionRegister = FakeGetPositionRegister;
    F->Iface.GetClockRegister = FakeGetClockRegister;
    F->Iface.GetLatency = FakeGetLatency;
    F->Iface.RegisterNotificationEvent = FakeRegisterNotificationEvent;
    F->Iface.UnregisterNotificationEvent = FakeUnregisterNotificationEvent;
    F->Iface.FreeBuffer = FakeFreeBuffer;
    F->Iface.CloseStream = FakeCloseStream;
    F->Iface.GetControl = FakeGetControl;
    F->Iface.SetControl = FakeSetControl;
    F->Mdl = (PMDL)0x1111u;
    F->Bytes = 32768;
    F->Offset = 0;
    F->Fifo = 64;
    F->Register = &F->RegisterValue;
    F->Latency.FifoSize = 64;
    F->Latency.ChipsetDelay = 0;
    F->Latency.CodecDelay = 768;
    F->EndpointCount = 2;
}

/* Every forwarder: one call of its routine and of no other, with the context and the arguments it was given, the status answered whether it succeeds or fails, and the outputs as the interface left them */
static
VOID
CheckForwarding(
    void)
{
    INTELDSP_AUDIO_INTERFACE* I;
    FAKE F;
    KSRTAUDIO_HWREGISTER Register;
    KSRTAUDIO_HWLATENCY Latency;
    KSAUDIO_POSITION Position;
    MEMORY_CACHING_TYPE CacheType;
    PMDL Mdl = NULL;
    HANDLE Stream = (HANDLE)0x2468u;
    ULONG Bytes = 7, Offset = 9, Fifo = 11, Index;
    NTSTATUS Status;
    ULONG S;

    FakeInit(&F);
    I = &F.Iface;

    /* the buffer: the count and the size, the four outputs, and the caching whatever came back */
    Status = DsppcmStreamAllocate(I, Stream, 2, 4096, &Mdl, &Bytes, &Offset, &Fifo, &CacheType);
    CHECK(Status == STATUS_SUCCESS);
    CHECK_EQ(F.Calls[KAllocateBuffer], 1);
    CHECK_EQ(FakeTotalCalls(), 1);
    CHECK(F.Context[KAllocateBuffer] == I->Context);
    CHECK(F.Stream[KAllocateBuffer] == Stream);
    CHECK_EQ(F.A[KAllocateBuffer], 2);
    CHECK_EQ(F.B[KAllocateBuffer], 4096);
    CHECK(Mdl == F.Mdl);
    CHECK_EQ(Bytes, 32768);
    CHECK_EQ(Offset, 0);
    CHECK_EQ(Fifo, 64);
    CHECK_EQ(CacheType, MmWriteCombined);
    FakeInit(&F);
    F.Result[KAllocateBuffer] = STATUS_INSUFFICIENT_RESOURCES;
    Mdl = NULL;
    Bytes = Offset = Fifo = 7;
    Status = DsppcmStreamAllocate(I, Stream, 1, 2048, &Mdl, &Bytes, &Offset, &Fifo, &CacheType);
    CHECK(Status == STATUS_INSUFFICIENT_RESOURCES);
    CHECK_EQ(F.Calls[KAllocateBuffer], 1);
    CHECK_EQ(FakeTotalCalls(), 1);
    CHECK_EQ(Mdl, (PMDL)NULL);
    CHECK_EQ(Bytes, 7);
    CHECK_EQ(CacheType, MmWriteCombined);

    /* the notification events */
    FakeInit(&F);
    Status = DsppcmStreamRegisterNotificationEvent(I, Stream, (PKEVENT)0x77u);
    CHECK(Status == STATUS_SUCCESS);
    CHECK_EQ(F.Calls[KRegisterNotificationEvent], 1);
    CHECK(F.Stream[KRegisterNotificationEvent] == Stream);
    CHECK(F.P[KRegisterNotificationEvent] == (PVOID)0x77u);
    CHECK(F.Context[KRegisterNotificationEvent] == I->Context);
    Status = DsppcmStreamUnregisterNotificationEvent(I, Stream, (PKEVENT)0x78u);
    CHECK(Status == STATUS_SUCCESS);
    CHECK_EQ(F.Calls[KUnregisterNotificationEvent], 1);
    CHECK_EQ(FakeTotalCalls(), 2);
    CHECK(F.P[KUnregisterNotificationEvent] == (PVOID)0x78u);
    FakeInit(&F);
    F.Result[KRegisterNotificationEvent] = STATUS_INVALID_HANDLE;
    CHECK(DsppcmStreamRegisterNotificationEvent(I, Stream, (PKEVENT)0x77u) == STATUS_INVALID_HANDLE);
    CHECK_EQ(F.Calls[KRegisterNotificationEvent], 1);
    FakeInit(&F);
    F.Result[KUnregisterNotificationEvent] = STATUS_INVALID_HANDLE;
    CHECK(DsppcmStreamUnregisterNotificationEvent(I, Stream, (PKEVENT)0x77u) == STATUS_INVALID_HANDLE);

    /* the four states, each forwarded as it is */
    for (S = (ULONG)KSSTATE_STOP; S <= (ULONG)KSSTATE_RUN; S++)
    {
        FakeInit(&F);
        Status = DsppcmStreamSetState(I, Stream, (KSSTATE)S);
        CHECK(Status == STATUS_SUCCESS);
        CHECK_EQ(F.Calls[KSetState], 1);
        CHECK_EQ(FakeTotalCalls(), 1);
        CHECK_EQ((ULONG)F.State[KSetState], S);
        CHECK(F.Stream[KSetState] == Stream);
    }
    FakeInit(&F);
    F.Result[KSetState] = STATUS_DEVICE_NOT_READY;
    CHECK(DsppcmStreamSetState(I, Stream, KSSTATE_RUN) == STATUS_DEVICE_NOT_READY);

    /* the registers: a 32-bit register, and the interface's pointer */
    FakeInit(&F);
    memset(&Register, 0xCC, sizeof(Register));
    Status = DsppcmStreamPositionRegister(I, Stream, &Register);
    CHECK(Status == STATUS_SUCCESS);
    CHECK_EQ(F.Calls[KGetPositionRegister], 1);
    CHECK_EQ(FakeTotalCalls(), 1);
    CHECK_EQ(Register.Width, 32);
    CHECK(Register.Register == (PVOID)&F.RegisterValue);
    FakeInit(&F);
    Status = DsppcmStreamClockRegister(I, &Register);
    CHECK(Status == STATUS_SUCCESS);
    CHECK_EQ(F.Calls[KGetClockRegister], 1);
    CHECK_EQ(FakeTotalCalls(), 1);
    CHECK_EQ(Register.Width, 32);
    CHECK(Register.Register == (PVOID)&F.RegisterValue);
    CHECK(F.Context[KGetClockRegister] == I->Context);
    FakeInit(&F);
    F.Result[KGetPositionRegister] = STATUS_NOT_SUPPORTED;
    CHECK(DsppcmStreamPositionRegister(I, Stream, &Register) == STATUS_NOT_SUPPORTED);
    FakeInit(&F);
    F.Result[KGetClockRegister] = STATUS_NO_SUCH_DEVICE;
    CHECK(DsppcmStreamClockRegister(I, &Register) == STATUS_NO_SUCH_DEVICE);

    /* the latency */
    FakeInit(&F);
    memset(&Latency, 0, sizeof(Latency));
    Status = DsppcmStreamLatency(I, Stream, &Latency);
    CHECK(Status == STATUS_SUCCESS);
    CHECK_EQ(F.Calls[KGetLatency], 1);
    CHECK_EQ(FakeTotalCalls(), 1);
    CHECK_EQ(Latency.FifoSize, 64);
    CHECK_EQ(Latency.CodecDelay, 768);

    /* the buffer's release, with the MDL and the size it was given, and the close */
    FakeInit(&F);
    Status = DsppcmStreamFree(I, Stream, F.Mdl, 32768);
    CHECK(Status == STATUS_SUCCESS);
    CHECK_EQ(F.Calls[KFreeBuffer], 1);
    CHECK_EQ(FakeTotalCalls(), 1);
    CHECK(F.P[KFreeBuffer] == F.Mdl);
    CHECK_EQ(F.A[KFreeBuffer], 32768);
    CHECK(F.Stream[KFreeBuffer] == Stream);
    FakeInit(&F);
    F.Result[KFreeBuffer] = STATUS_INVALID_PARAMETER;
    CHECK(DsppcmStreamFree(I, Stream, F.Mdl, 1) == STATUS_INVALID_PARAMETER);
    FakeInit(&F);
    Status = DsppcmStreamClose(I, Stream);
    CHECK(Status == STATUS_SUCCESS);
    CHECK_EQ(F.Calls[KCloseStream], 1);
    CHECK_EQ(FakeTotalCalls(), 1);
    CHECK(F.Stream[KCloseStream] == Stream);
    FakeInit(&F);
    F.Result[KCloseStream] = STATUS_INVALID_HANDLE;
    CHECK(DsppcmStreamClose(I, Stream) == STATUS_INVALID_HANDLE);

    /* the position: the register modulo the buffer, the capture's read cursor a FIFO behind, the render's write cursor a FIFO ahead */
    FakeInit(&F);
    F.RegisterValue = 100;
    Status = DsppcmStreamPosition(I, Stream, 1000, 16, FALSE, &Position);
    CHECK(Status == STATUS_SUCCESS);
    CHECK_EQ(Position.PlayOffset, 100);
    CHECK_EQ(Position.WriteOffset, 116);
    CHECK_EQ(F.Calls[KGetPositionRegister], 1);
    CHECK_EQ(FakeTotalCalls(), 1);
    F.RegisterValue = 990;
    Status = DsppcmStreamPosition(I, Stream, 1000, 16, FALSE, &Position);
    CHECK_EQ(Position.PlayOffset, 990);
    CHECK_EQ(Position.WriteOffset, 6);
    F.RegisterValue = 100;
    Status = DsppcmStreamPosition(I, Stream, 1000, 16, TRUE, &Position);
    CHECK_EQ(Position.PlayOffset, 84);
    CHECK_EQ(Position.WriteOffset, 84);
    F.RegisterValue = 10;
    Status = DsppcmStreamPosition(I, Stream, 1000, 16, TRUE, &Position);
    CHECK_EQ(Position.PlayOffset, 994);
    F.RegisterValue = 0;
    Status = DsppcmStreamPosition(I, Stream, 1000, 0, FALSE, &Position);
    CHECK_EQ(Position.PlayOffset, 0);
    CHECK_EQ(Position.WriteOffset, 0);
    /* a register beyond the buffer wraps; a stream without a buffer, a missing register and a failed call are refused */
    F.RegisterValue = 123456;
    Status = DsppcmStreamPosition(I, Stream, 1000, 16, FALSE, &Position);
    CHECK_EQ(Position.PlayOffset, 456);
    F.RegisterValue = 500;
    Status = DsppcmStreamPosition(I, Stream, 0, 16, FALSE, &Position);
    CHECK(Status == STATUS_INVALID_PARAMETER);
    /* the six readings before asked the register; this one did not */
    CHECK_EQ(F.Calls[KGetPositionRegister], 6);
    F.Register = NULL;
    Status = DsppcmStreamPosition(I, Stream, 1000, 16, FALSE, &Position);
    CHECK(Status == STATUS_DEVICE_NOT_READY);
    F.Register = &F.RegisterValue;
    F.Result[KGetPositionRegister] = STATUS_INVALID_HANDLE;
    Status = DsppcmStreamPosition(I, Stream, 1000, 16, FALSE, &Position);
    CHECK(Status == STATUS_INVALID_HANDLE);

    /* the interface's own endpoints and the control state: asked once, answered as given */
    FakeInit(&F);
    CHECK(I->GetEndpointCount(I->Context, &Index) == STATUS_SUCCESS);
    CHECK_EQ(Index, 2);
    CHECK_EQ(F.Calls[KGetEndpointCount], 1);
    CHECK(I->GetEndpoint(I->Context, 1, &F.Endpoint) == STATUS_SUCCESS);
    CHECK_EQ(F.A[KGetEndpoint], 1);
    FakeInit(&F);
    {
        LONG Value = 0;

        CHECK(I->GetControl(I->Context, 1, IntcDspControlVolume, &Value) == STATUS_SUCCESS);
        CHECK_EQ(Value, -1234);
        CHECK(F.Control[KGetControl] == IntcDspControlVolume);
        CHECK(I->SetControl(I->Context, 0, IntcDspControlMute, 1) == STATUS_SUCCESS);
        CHECK(F.Control[KSetControl] == IntcDspControlMute);
        CHECK_EQ(F.B[KSetControl], 1);
    }
}

/* (d) The position arithmetic alone across the wraps, and the engine's clock over it */

static
VOID
CheckPositionWrap(
    void)
{
    /* 48 kHz stereo in the 32-bit word, two periods of 4800 frames: the buffer the engine's cyclic scheduling uses */
    const ULONG Buffer = 76800, Fifo = 64;
    KSAUDIO_POSITION Position;
    NTSTATUS Status;

    /* the register counts past the buffer: the reading is the remainder, and the cursors keep their FIFO distance around the buffer's end */
    Status = DsppcmPositionFromValue(Buffer - 1, Buffer, Fifo, FALSE, &Position);
    CHECK(Status == STATUS_SUCCESS);
    CHECK_EQ(Position.PlayOffset, Buffer - 1);
    CHECK_EQ(Position.WriteOffset, Fifo - 1);
    Status = DsppcmPositionFromValue(Buffer, Buffer, Fifo, FALSE, &Position);
    CHECK_EQ(Position.PlayOffset, 0);
    CHECK_EQ(Position.WriteOffset, Fifo);
    /* capture trails the link by a FIFO: the link a FIFO short of the end reads a FIFO short of the end less the FIFO */
    Status = DsppcmPositionFromValue(Buffer - Fifo, Buffer, Fifo, TRUE, &Position);
    CHECK_EQ(Position.PlayOffset, Buffer - 2 * Fifo);
    CHECK_EQ(Position.WriteOffset, Buffer - 2 * Fifo);
    Status = DsppcmPositionFromValue(Buffer - 1, Buffer, Fifo, TRUE, &Position);
    CHECK_EQ(Position.PlayOffset, Buffer - 1 - Fifo);
    /* the register's own wrap at 2^32: 2^32 is not a multiple of the buffer, so the last count and the first of the next turn read as remainders that far apart */
    Status = DsppcmPositionFromValue(0x7FFFFFFFu, Buffer, Fifo, FALSE, &Position);
    CHECK_EQ(Position.PlayOffset, 0x7FFFFFFFu % Buffer);
    Status = DsppcmPositionFromValue(0xFFFFFFFFu, Buffer, Fifo, FALSE, &Position);
    CHECK_EQ(Position.PlayOffset, 0xFFFFFFFFu % Buffer);
    CHECK_EQ(Position.WriteOffset, (0xFFFFFFFFu % Buffer + Fifo) % Buffer);
    Status = DsppcmPositionFromValue(0xFFFFFFFFu + 1u, Buffer, Fifo, FALSE, &Position);
    CHECK_EQ(Position.PlayOffset, 0);
    CHECK_EQ(Position.WriteOffset, Fifo);
    /* a FIFO as large as the buffer, and more, is taken modulo the buffer as the codec driver does; no buffer is refused */
    Status = DsppcmPositionFromValue(1000, Buffer, Buffer, FALSE, &Position);
    CHECK_EQ(Position.WriteOffset, 1000);
    Status = DsppcmPositionFromValue(1000, Buffer, Buffer + 16, TRUE, &Position);
    CHECK_EQ(Position.PlayOffset, (1000 + Buffer - 16) % Buffer);
    CHECK_EQ(Position.WriteOffset, (1000 + Buffer - 16) % Buffer);
    CHECK(DsppcmPositionFromValue(1, 0, Fifo, FALSE, &Position) == STATUS_INVALID_PARAMETER);
    /* the stream method reads the register once and answers the same arithmetic */
    {
        FAKE F;
        INTELDSP_AUDIO_INTERFACE* I;
        KSAUDIO_POSITION Direct;

        FakeInit(&F);
        I = &F.Iface;
        F.RegisterValue = 0xFFFFFFFFu;
        Status = DsppcmStreamPosition(I, (HANDLE)0x11u, Buffer, Fifo, FALSE, &Position);
        CHECK(Status == STATUS_SUCCESS);
        CHECK_EQ(F.Calls[KGetPositionRegister], 1);
        CHECK_EQ(FakeTotalCalls(), 1);
        DsppcmPositionFromValue(0xFFFFFFFFu, Buffer, Fifo, FALSE, &Direct);
        CHECK(Position.PlayOffset == Direct.PlayOffset && Position.WriteOffset == Direct.WriteOffset);
    }
}

/*
 * The engine's clock over the arithmetic: the register moves StepBytes a
 * sample for Samples samples from Start, and the engine adds a period to a
 * 64-bit frame clock whenever the cursors say the playing half changed
 * (dll/win32/mmdevapi/reactos_wdmaud.c refill_cyclic_period, over the rule
 * of reactos_wavert.h); as the engine starts, the first sample's half is
 * taken without counting. The clock is answered in frames; Refused counts
 * the samples the rule refused and Fell the samples at which the clock went
 * backwards.
 */
static
unsigned long long
RunEngineClock(
    ULONG BufferBytes,
    ULONG FrameBytes,
    ULONG Fifo,
    ULONG Start,
    ULONG StepBytes,
    unsigned long Samples,
    BOOLEAN Capture,
    _Out_ PULONG Refused,
    _Out_ PULONG Fell)
{
    struct reactos_wavert_position Progress;
    KSAUDIO_POSITION Position;
    unsigned long long Clock = 0, Last = 0;
    ULONG PeriodFrames = BufferBytes / REACTOS_RT_NOTIFICATION_COUNT / FrameBytes;
    ULONG Value = Start, Previous = 0, S;

    *Refused = 0;
    *Fell = 0;
    for (S = 0; S < Samples; S++)
    {
        CHECK(DsppcmPositionFromValue(Value, BufferBytes, Fifo, Capture, &Position) == STATUS_SUCCESS);
        if (!reactos_wavert_cyclic_position(PeriodFrames, FrameBytes, Previous, Position.PlayOffset, Position.WriteOffset, &Progress))
        {
            (*Refused)++;
            Value += StepBytes;
            continue;
        }
        if (S == 0)
            Previous = Progress.playing;
        else if (Progress.advanced)
        {
            Clock += PeriodFrames;
            if (Clock < Last)
                (*Fell)++;
        }
        Last = Clock;
        Previous = Progress.playing;
        Value += StepBytes;
    }
    return Clock;
}

static
VOID
CheckEngineClock(
    void)
{
    /* two periods of 4800 frames of 8 bytes: one sample a sixteenth of a period, so every boundary is crossed between two samples */
    const ULONG Buffer = 76800, Period = 38400, Step = Period / 16;
    unsigned long long Clock, Ideal;
    unsigned long Samples = 16 * 400;
    ULONG Refused, Fell;

    /* render: four hundred buffers, a period a half, each counted once */
    Clock = RunEngineClock(Buffer, 8, 64, 12345, Step, Samples, FALSE, &Refused, &Fell);
    CHECK_EQ(Refused, 0);
    CHECK_EQ(Fell, 0);
    Ideal = (unsigned long long)Samples * Step / Buffer * REACTOS_RT_NOTIFICATION_COUNT * (Period / 8);
    CHECK_EQ(Clock, Ideal);
    /* capture, the cursors a FIFO behind */
    Clock = RunEngineClock(Buffer, 8, 64, 12345, Step, Samples, TRUE, &Refused, &Fell);
    CHECK_EQ(Refused, 0);
    CHECK_EQ(Fell, 0);
    CHECK_EQ(Clock, Ideal);
    /* the register's wrap at 2^32, inside the run: the clock never falls and stays within one period of the bytes moved */
    Clock = RunEngineClock(Buffer, 8, 64, 0xFFFFFFFFu - 8 * Buffer - 5, Step, Samples, FALSE, &Refused, &Fell);
    CHECK_EQ(Refused, 0);
    CHECK_EQ(Fell, 0);
    Ideal = (unsigned long long)Samples * Step / Buffer * REACTOS_RT_NOTIFICATION_COUNT * (Period / 8);
    CHECK(Clock <= Ideal + Period / 8 && Clock + Period / 8 >= Ideal);
    /* a 16-bit word in two periods of 12288 frames, where 2^32 leaves the cursors in the other half: the same bound */
    Clock = RunEngineClock(98304, 4, 32, 0xFFFFFFFFu - 8 * 98304 - 5, 49152 / 8, Samples, FALSE, &Refused, &Fell);
    CHECK_EQ(Refused, 0);
    CHECK_EQ(Fell, 0);
    Ideal = (unsigned long long)Samples * (49152 / 8) / 98304 * REACTOS_RT_NOTIFICATION_COUNT * 12288;
    CHECK(Clock <= Ideal + 12288 && Clock + 12288 >= Ideal);
}

int
main(
    void)
{
    CheckPinRanges();
    CheckPinFormatFind();
    CheckTables();
    CheckForwarding();
    CheckPositionWrap();
    CheckEngineClock();
    printf("dsppcm: %d checks, %d failures\n", Checks, Failures);
    return Failures != 0;
}
