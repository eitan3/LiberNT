/*
 * PROJECT:     LiberNT host-native tests
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     The codec driver's selection of the Audio Input converters whose input amplifier makes a jack's capture gain (capture_amps.c): the connection lists of the cases, in the shapes a codec's capture path takes
 * COPYRIGHT:   Copyright 2026 LiberNT contributors
 */

#include <stdio.h>
#include <string.h>

#include "shim.h"

#include "capture_amps.h"
#include "adapterpnp.h"

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

/* The widget ids of the cases: a function group's start node, two Audio Input converters, two Audio Outputs, a mixer, two selectors, three pins */
#define NID_AFG       0x01
#define NID_ADC1      0x02
#define NID_ADC2      0x03
#define NID_DAC1      0x04
#define NID_DAC2      0x05
#define NID_MIXER     0x06
#define NID_SEL1      0x07
#define NID_SEL2      0x08
#define NID_PIN_MIC   0x18
#define NID_PIN_LINE  0x19
#define NID_PIN_SPK   0x14

/* One row: the widgets (NULL ends the list), the jack pin, and the pairs the selection must answer, (0, 0) ending them */
typedef struct _SELECT_CASE
{
    const char* Name;
    UCHAR Pin;
    HDA_WIDGET Widgets[12];
    HDA_CAPTURE_AMP Expected[4];
} SELECT_CASE;

static const SELECT_CASE Cases[] =
{
    /* the jack pin behind the converter directly: the capture path without a selector or a mixer, the shape R1-10 found unanswered */
    { "pin to converter", NID_PIN_MIC,
      { { NID_ADC1, 0x01, 0, 1, NULL }, { NID_PIN_MIC, 0x04, 0, 0, NULL } },
      { { NID_ADC1, 0 } } },
    /* through a selector: the converter takes the selector, the selector takes the pin and the line in */
    { "pin through a selector", NID_PIN_MIC,
      { { NID_ADC1, 0x01, 0, 1, NULL }, { NID_SEL1, 0x03, 0, 2, NULL }, { NID_PIN_MIC, 0x04, 0, 0, NULL }, { NID_PIN_LINE, 0x04, 0, 0, NULL } },
      { { NID_ADC1, 0 } } },
    /* through a mixer: the mixer is a pass-through of the path, and is not the amplifier the capture gain is */
    { "pin through a mixer", NID_PIN_MIC,
      { { NID_ADC1, 0x01, 0, 1, NULL }, { NID_MIXER, 0x02, 0, 1, NULL }, { NID_PIN_MIC, 0x04, 0, 0, NULL } },
      { { NID_ADC1, 0 } } },
    /* two connections: the pin is the second of the converter's list, and the index says so (the input amplifier is the connection's) */
    { "the pin behind the second connection", NID_PIN_MIC,
      { { NID_ADC1, 0x01, 0, 2, NULL }, { NID_PIN_LINE, 0x04, 0, 0, NULL }, { NID_PIN_MIC, 0x04, 0, 0, NULL } },
      { { NID_ADC1, 1 } } },
    /* two converters reach the pin through one selector: both, in the widgets' order */
    { "two converters, one path", NID_PIN_MIC,
      { { NID_ADC2, 0x01, 0, 1, NULL }, { NID_ADC1, 0x01, 0, 1, NULL }, { NID_SEL1, 0x03, 0, 1, NULL }, { NID_PIN_MIC, 0x04, 0, 0, NULL } },
      { { NID_ADC2, 0 }, { NID_ADC1, 0 } } },
    /* a converter whose path leads to another pin only: not this jack's */
    { "another jack's converter", NID_PIN_MIC,
      { { NID_ADC1, 0x01, 0, 1, NULL }, { NID_PIN_LINE, 0x04, 0, 0, NULL }, { NID_ADC2, 0x01, 0, 1, NULL }, { NID_PIN_MIC, 0x04, 0, 0, NULL } },
      { { NID_ADC2, 0 } } },
    /* the render side around it: Audio Outputs and the speaker pin are nothing of a capture path's */
    { "the render widgets beside", NID_PIN_MIC,
      { { NID_DAC1, 0x00, 0, 0, NULL }, { NID_DAC2, 0x00, 0, 0, NULL }, { NID_PIN_SPK, 0x04, 0, 0, NULL }, { NID_ADC1, 0x01, 0, 1, NULL }, { NID_PIN_MIC, 0x04, 0, 0, NULL } },
      { { NID_ADC1, 0 } } },
    /* a loop between the selector and the mixer: the walk ends, and the pin behind the loop is reached */
    { "a loop on the path", NID_PIN_MIC,
      { { NID_ADC1, 0x01, 0, 1, NULL }, { NID_SEL1, 0x03, 0, 2, NULL }, { NID_MIXER, 0x02, 0, 2, NULL }, { NID_PIN_MIC, 0x04, 0, 0, NULL } },
      { { NID_ADC1, 0 } } },
    /* the digital domain: a digital converter does not take an analog pin for its path */
    { "a digital converter, an analog pin", NID_PIN_MIC,
      { { NID_ADC1, 0x01, 1, 1, NULL }, { NID_SEL1, 0x03, 1, 1, NULL }, { NID_PIN_MIC, 0x04, 0, 0, NULL } },
      { { 0, 0 } } },
    /* nothing reaches the pin: no pair */
    { "no path", NID_PIN_MIC,
      { { NID_ADC1, 0x01, 0, 1, NULL }, { NID_PIN_LINE, 0x04, 0, 0, NULL } },
      { { 0, 0 } } },
    /* a converter without a connection list */
    { "a converter without connections", NID_PIN_MIC,
      { { NID_ADC1, 0x01, 0, 0, NULL }, { NID_PIN_MIC, 0x04, 0, 0, NULL } },
      { { 0, 0 } } },
    /* a connection to a widget the group does not hold is skipped, the next one reaches the pin */
    { "a connection to nothing", NID_PIN_MIC,
      { { NID_ADC1, 0x01, 0, 2, NULL }, { NID_PIN_MIC, 0x04, 0, 0, NULL } },
      { { NID_ADC1, 1 } } },
};

/* The connection lists of the rows, by the widget they belong to: the rows above name the widgets, these name what each takes its input from */
static const USHORT ListAdc1Sel[] = { NID_SEL1 };
static const USHORT ListAdc1Mix[] = { NID_MIXER };
static const USHORT ListAdc1Pin[] = { NID_PIN_MIC };
static const USHORT ListAdc1LinePin[] = { NID_PIN_LINE, NID_PIN_MIC };
static const USHORT ListAdc1Line[] = { NID_PIN_LINE };
static const USHORT ListAdc1Nothing[] = { 0x30, NID_PIN_MIC };
static const USHORT ListAdc2Sel[] = { NID_SEL1 };
static const USHORT ListAdc2Pin[] = { NID_PIN_MIC };
static const USHORT ListSel1MicLine[] = { NID_PIN_MIC, NID_PIN_LINE };
static const USHORT ListSel1Mic[] = { NID_PIN_MIC };
static const USHORT ListMixPin[] = { NID_PIN_MIC };
static const USHORT ListMixSel1Pin[] = { NID_SEL1, NID_PIN_MIC };

/* The connection list of each widget of a row, in the row's order */
static const USHORT* const Lists[][12] =
{
    { ListAdc1Pin, NULL },
    { ListAdc1Sel, ListSel1MicLine, NULL, NULL },
    { ListAdc1Mix, ListMixPin, NULL },
    { ListAdc1LinePin, NULL, NULL },
    { ListAdc2Sel, ListAdc1Sel, ListSel1Mic, NULL },
    { ListAdc1Line, NULL, ListAdc2Pin, NULL },
    { NULL, NULL, NULL, ListAdc1Pin, NULL },
    { ListAdc1Sel, ListSel1MicLine, ListMixSel1Pin, NULL },
    { ListAdc1Sel, ListSel1Mic, NULL },
    { ListAdc1Line, NULL },
    { NULL, NULL },
    { ListAdc1Nothing, NULL },
};

static
void
check_select(
    const SELECT_CASE* Case,
    const USHORT* const* Lists)
{
    HDA_WIDGET Widgets[12];
    HDA_CAPTURE_AMP Selected[8];
    ULONG Count, I, J;

    memcpy(Widgets, Case->Widgets, sizeof(Widgets));
    for (I = 0; I < 12 && Widgets[I].Id != 0; I++)
        Widgets[I].Connections = Lists[I];
    Count = (ULONG)I;
    memset(Selected, 0xAA, sizeof(Selected));
    I = HdaCaptureAmpsSelect(Widgets, Count, Case->Pin, Selected, 8);
    for (J = 0; Case->Expected[J].Converter != 0; J++)
        ;
    if (I != J)
        fprintf(stderr, "  in the row '%s': %u pairs, %u expected\n", Case->Name, (unsigned int)I, (unsigned int)J);
    CHECK_EQ(I, J);
    for (I = 0; I < J && I < 8; I++)
    {
        if (Selected[I].Converter != Case->Expected[I].Converter || Selected[I].Connection != Case->Expected[I].Connection)
            fprintf(stderr, "  in the row '%s': pair %u: converter %u/%u connection %u/%u\n", Case->Name, (unsigned int)I,
                    Selected[I].Converter, Case->Expected[I].Converter, Selected[I].Connection, Case->Expected[I].Connection);
        CHECK_EQ(Selected[I].Converter, Case->Expected[I].Converter);
        CHECK_EQ(Selected[I].Connection, Case->Expected[I].Connection);
    }
}

/* A PnP request ends the codec driver's adapter on the removal and the surprise removal only; a stop keeps it, with its registered subdevices, for the start that follows (FA-02) */
static
void
CheckPnpRelease(
    void)
{
    CHECK(HDAudioPnpReleasesAdapter(IRP_MN_REMOVE_DEVICE));
    CHECK(HDAudioPnpReleasesAdapter(IRP_MN_SURPRISE_REMOVAL));
    CHECK(!HDAudioPnpReleasesAdapter(IRP_MN_STOP_DEVICE));
    CHECK(!HDAudioPnpReleasesAdapter(IRP_MN_START_DEVICE));
    CHECK(!HDAudioPnpReleasesAdapter(IRP_MN_QUERY_REMOVE_DEVICE));
    CHECK(!HDAudioPnpReleasesAdapter(IRP_MN_QUERY_REMOVE_DEVICE + 0x10));
    CHECK(!HDAudioPnpReleasesAdapter(0xFF));
}

int main(void)
{
    HDA_WIDGET Widgets[HDA_WIDGETS_MAX];
    HDA_CAPTURE_AMP Selected[HDA_WIDGETS_MAX];
    static const USHORT Pin[] = { 0 };
    ULONG I, Count;

    CHECK_EQ(sizeof(Cases) / sizeof(Cases[0]), sizeof(Lists) / sizeof(Lists[0]));
    for (I = 0; I < sizeof(Cases) / sizeof(Cases[0]); I++)
    {
        int FailuresBefore = Failures;

        check_select(&Cases[I], Lists[I]);
        if (Failures != FailuresBefore)
            fprintf(stderr, "  (row %u)\n", (unsigned int)I);
    }

    /* the bound: every converter of a full group reaches the pin (id 0, the one id the 255 converters leave), and a Max below the count is not passed */
    memset(Widgets, 0, sizeof(Widgets));
    Widgets[0].Id = 0;
    Widgets[0].Type = 0x04;
    for (I = 1; I < HDA_WIDGETS_MAX; I++)
    {
        Widgets[I].Id = (UCHAR)I;
        Widgets[I].Type = 0x01;
        Widgets[I].ConnectionCount = 1;
        Widgets[I].Connections = Pin;
    }
    /* the first widget is the pin and is not a converter: 255 converters */
    memset(Selected, 0xAA, sizeof(Selected));
    Count = HdaCaptureAmpsSelect(Widgets, HDA_WIDGETS_MAX, 0, Selected, HDA_WIDGETS_MAX);
    CHECK_EQ(Count, HDA_WIDGETS_MAX - 1);
    CHECK_EQ(Selected[0].Converter, 0x01);
    CHECK_EQ(Selected[HDA_WIDGETS_MAX - 2].Converter, 0xFF);
    CHECK_EQ(Selected[HDA_WIDGETS_MAX - 1].Converter, 0xAA);
    memset(Selected, 0xAA, sizeof(Selected));
    Count = HdaCaptureAmpsSelect(Widgets, HDA_WIDGETS_MAX, 0, Selected, 4);
    CHECK_EQ(Count, 4);
    CHECK_EQ(Selected[4].Converter, 0xAA);
    /* nothing asked, nothing answered; no widgets, none */
    CHECK_EQ(HdaCaptureAmpsSelect(Widgets, HDA_WIDGETS_MAX, 0, Selected, 0), 0);
    CHECK_EQ(HdaCaptureAmpsSelect(Widgets, 0, 0, Selected, 8), 0);
    CHECK_EQ(HdaCaptureAmpsSelect(NULL, 0, 0, Selected, 8), 0);
    /* a count past the array's own bound is cut to it */
    Count = HdaCaptureAmpsSelect(Widgets, HDA_WIDGETS_MAX + 100, 0, Selected, HDA_WIDGETS_MAX);
    CHECK_EQ(Count, HDA_WIDGETS_MAX - 1);

    CheckPnpRelease();

    printf("hdaudio: %d checks, %d failures\n", Checks, Failures);
    return Failures ? 1 : 0;
}
