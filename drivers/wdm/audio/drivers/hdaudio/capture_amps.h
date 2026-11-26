/*
 * PROJECT:     ReactOS HDAudio Driver
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     The selection of the Audio Input converters whose input amplifier makes a jack's capture gain, over the widgets of a function group as plain records: free of I/O, so that the host tests run the codec driver's own rule
 * COPYRIGHT:   Copyright 2026 LiberNT contributors
 */

#ifndef _DRIVERS_HDAUDIO_CAPTURE_AMPS_H_
#define _DRIVERS_HDAUDIO_CAPTURE_AMPS_H_

#include <ntddk.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The widgets a codec's function group holds: its widget ids are bytes */
#define HDA_WIDGETS_MAX             256
/* The widget types of HDA 7.3.4.1 the selection names: an Audio Input converter, and a Pin Complex */
#define HDA_WIDGET_AUDIO_INPUT      0x01
#define HDA_WIDGET_PIN_COMPLEX      0x04

/*
 * One widget as the selection sees it: its id, its widget type, whether it
 * is of the digital domain, and its connection list, the widgets a
 * converter, a mixer or a selector takes its input from, in the codec's
 * order.
 */
typedef struct _HDA_WIDGET
{
    UCHAR Id;
    UCHAR Type;
    UCHAR Digital;
    UCHAR ConnectionCount;
    const USHORT* Connections;
} HDA_WIDGET, *PHDA_WIDGET;

/* One selected converter: the Audio Input converter, and the index of its connection that reaches the pin, the input amplifier the capture gain and mute are (HDA 7.3.4.1) */
typedef struct _HDA_CAPTURE_AMP
{
    UCHAR Converter;
    UCHAR Connection;
} HDA_CAPTURE_AMP, *PHDA_CAPTURE_AMP;

/*
 * The Audio Input converters (widget type 0x01) whose connection, directly
 * or through a selector or a mixer, reaches the jack pin: for each, the
 * first connection of its list from which the pin is reached by following
 * the connection lists, a pin of the converter's own domain. A converter
 * whose list does not reach the pin is not selected; a widget of any other
 * type never is. The pairs are written to Selected in the widgets' order,
 * at most Max; the count written is returned, 0 when none reaches the pin.
 * The walk keeps its own visited marks: nothing of the widgets is changed.
 */
ULONG
HdaCaptureAmpsSelect(
    _In_reads_(WidgetCount) const HDA_WIDGET* Widgets,
    ULONG WidgetCount,
    UCHAR PinId,
    _Out_writes_(Max) PHDA_CAPTURE_AMP Selected,
    ULONG Max);

#ifdef __cplusplus
}
#endif

#endif
