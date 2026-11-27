/*
 * PROJECT:     ReactOS HDAudio Driver
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     The selection of the Audio Input converters whose input amplifier makes a jack's capture gain: the connection lists followed from a converter's connection to the pin, once per widget
 * COPYRIGHT:   Copyright 2026 LiberNT contributors
 */

#include "capture_amps.h"

/* The widget of an id, its index in the array, HDA_WIDGETS_MAX when there is none */
static
ULONG
HdaWidgetIndex(
    _In_reads_(WidgetCount) const HDA_WIDGET* Widgets,
    ULONG WidgetCount,
    UCHAR Id)
{
    ULONG Index;

    for (Index = 0; Index < WidgetCount; Index++)
        if (Widgets[Index].Id == Id)
            return Index;
    return HDA_WIDGETS_MAX;
}

/*
 * Whether the pin is reached from a widget by following the connection
 * lists, a pin of the given domain: each widget is entered once, by the
 * marks the caller cleared for the walk. The pending widgets are a stack of
 * indexes that holds a widget once: a widget is marked when it is queued
 * (Queued, cleared for each walk), not only when it is entered, so a widget
 * many predecessors name is pushed by the first of them alone and the stack
 * never holds more than the widgets (R5-06); the walk recurses by nothing.
 */
static
BOOLEAN
HdaWidgetReachesPin(
    _In_reads_(WidgetCount) const HDA_WIDGET* Widgets,
    ULONG WidgetCount,
    UCHAR From,
    UCHAR PinId,
    UCHAR Digital,
    _Inout_ UCHAR* Visited)
{
    UCHAR Stack[HDA_WIDGETS_MAX];
    UCHAR Queued[HDA_WIDGETS_MAX];
    ULONG Top = 0, Index, Connection, Next;

    if (From >= WidgetCount)
        return FALSE;
    RtlZeroMemory(Queued, WidgetCount);
    Stack[Top++] = From;
    Queued[From] = 1;
    while (Top != 0)
    {
        Index = Stack[--Top];
        if (Visited[Index])
            continue;
        Visited[Index] = 1;
        if (Widgets[Index].Id == PinId)
            return Widgets[Index].Type == HDA_WIDGET_PIN_COMPLEX && Widgets[Index].Digital == Digital;
        for (Connection = 0; Connection < Widgets[Index].ConnectionCount; Connection++)
        {
            Next = HdaWidgetIndex(Widgets, WidgetCount, (UCHAR)Widgets[Index].Connections[Connection]);
            if (Next < WidgetCount && !Queued[Next])
            {
                Stack[Top++] = (UCHAR)Next;
                Queued[Next] = 1;
            }
        }
    }
    return FALSE;
}

ULONG
HdaCaptureAmpsSelect(
    _In_reads_(WidgetCount) const HDA_WIDGET* Widgets,
    ULONG WidgetCount,
    UCHAR PinId,
    _Out_writes_(Max) PHDA_CAPTURE_AMP Selected,
    ULONG Max)
{
    UCHAR Visited[HDA_WIDGETS_MAX];
    ULONG W, C, Index, Found = 0;

    if (WidgetCount > HDA_WIDGETS_MAX)
        WidgetCount = HDA_WIDGETS_MAX;
    for (W = 0; W < WidgetCount && Found < Max; W++)
    {
        if (Widgets[W].Type != HDA_WIDGET_AUDIO_INPUT)
            continue;
        RtlZeroMemory(Visited, WidgetCount);
        for (C = 0; C < Widgets[W].ConnectionCount; C++)
        {
            Index = HdaWidgetIndex(Widgets, WidgetCount, (UCHAR)Widgets[W].Connections[C]);
            if (Index >= WidgetCount)
                continue;
            if (!HdaWidgetReachesPin(Widgets, WidgetCount, (UCHAR)Index, PinId, Widgets[W].Digital, Visited))
                continue;
            Selected[Found].Converter = Widgets[W].Id;
            Selected[Found].Connection = (UCHAR)C;
            Found++;
            break;
        }
    }
    return Found;
}
