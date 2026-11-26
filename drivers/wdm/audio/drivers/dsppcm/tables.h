/*
 * PROJECT:     LiberNT Intel Smart Sound DSP endpoint driver
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Filter descriptors: the KS topology of an endpoint of the DSP, the codec driver's shape with a converter node no hardware has behind it (plan §6.4)
 * COPYRIGHT:   Copyright 2026 LiberNT contributors
 */

/*
 * Written in the subset of C and C++ both read, so the host tests compile
 * this header over a mirror of the descriptor types (submodules/host-tests/
 * dsppcm): positional initializers only.
 *
 * The shape is drivers/wdm/audio/drivers/hdaudio/tables.h: a topology
 * filter per endpoint, source pin -> volume -> mute -> sink pin, and a wave
 * filter per endpoint, source pin -> converter -> sink pin, the two joined
 * by a physical connection the adapter registers. The names and categories
 * of the bridge pins are the codec driver's (a speaker for render, a
 * microphone for capture); the endpoint's own name is its subdevice's and
 * its interface's friendly name.
 */

#ifndef _DSPPCM_TABLES_H_
#define _DSPPCM_TABLES_H_

/* The handlers of prophnd.cpp */
NTSTATUS NTAPI PropertyHandler_JackDescription(IN PPCPROPERTY_REQUEST PropertyRequest);
NTSTATUS NTAPI PropertyHandler_Volume(IN PPCPROPERTY_REQUEST PropertyRequest);
NTSTATUS NTAPI PropertyHandler_Mute(IN PPCPROPERTY_REQUEST PropertyRequest);
NTSTATUS NTAPI PropertyHandler_ChannelConfig(IN PPCPROPERTY_REQUEST PropertyRequest);
NTSTATUS NTAPI PropertyHandler_SpeakerGeometry(IN PPCPROPERTY_REQUEST PropertyRequest);
NTSTATUS NTAPI EventHandler_Volume(IN PPCEVENT_REQUEST EventRequest);

static PCPROPERTY_ITEM WaveProperty[] =
{
    {
        &KSPROPSETID_Audio,
        KSPROPERTY_AUDIO_CHANNEL_CONFIG,
        KSPROPERTY_TYPE_GET | KSPROPERTY_TYPE_SET,
        PropertyHandler_ChannelConfig
    },
    {
        &KSPROPSETID_Audio,
        KSPROPERTY_AUDIO_STEREO_SPEAKER_GEOMETRY,
        KSPROPERTY_TYPE_GET | KSPROPERTY_TYPE_SET,
        PropertyHandler_SpeakerGeometry
    }
};

DEFINE_PCAUTOMATION_TABLE_PROP(WaveAutomationTable, WaveProperty);

static PCPROPERTY_ITEM VolumeProperty[] =
{
    {
        &KSPROPSETID_Audio,
        KSPROPERTY_AUDIO_VOLUMELEVEL,
        KSPROPERTY_TYPE_GET | KSPROPERTY_TYPE_SET | KSPROPERTY_TYPE_BASICSUPPORT,
        PropertyHandler_Volume
    }
};

static PCPROPERTY_ITEM MuteProperty[] =
{
    {
        &KSPROPSETID_Audio,
        KSPROPERTY_AUDIO_MUTE,
        KSPROPERTY_TYPE_GET | KSPROPERTY_TYPE_SET | KSPROPERTY_TYPE_BASICSUPPORT,
        PropertyHandler_Mute
    }
};

static PCEVENT_ITEM VolumeEvent[] =
{
    {
        &KSEVENTSETID_AudioControlChange,
        KSEVENT_CONTROL_CHANGE,
        KSEVENT_TYPE_ENABLE | KSEVENT_TYPE_BASICSUPPORT,
        EventHandler_Volume
    }
};

static PCAUTOMATION_TABLE VolumeAutomationTable[] =
{
    {
        sizeof(PCPROPERTY_ITEM),
        1,
        VolumeProperty,
        sizeof(PCMETHOD_ITEM),
        0,
        NULL,
        sizeof(PCEVENT_ITEM),
        1,
        VolumeEvent,
        0
    }
};

static PCAUTOMATION_TABLE MuteAutomationTable[] =
{
    {
        sizeof(PCPROPERTY_ITEM),
        1,
        MuteProperty,
        sizeof(PCMETHOD_ITEM),
        0,
        NULL,
        sizeof(PCEVENT_ITEM),
        1,
        VolumeEvent,
        0
    }
};

static PCPROPERTY_ITEM TopologyProperty[] =
{
    {
        &KSPROPSETID_Jack,
        KSPROPERTY_JACK_DESCRIPTION,
        KSPROPERTY_TYPE_GET | KSPROPERTY_TYPE_BASICSUPPORT,
        PropertyHandler_JackDescription
    }
};

DEFINE_PCAUTOMATION_TABLE_PROP(TopologyAutomationTable, TopologyProperty);

/* Node 0 of each wave filter: the converter the wave data crosses, DAC for render and ADC for capture; no hardware converter is behind it */
static PCNODE_DESCRIPTOR DacNode[] = {{0, &WaveAutomationTable, &KSNODETYPE_DAC, NULL}};

static PCNODE_DESCRIPTOR AdcNode[] = {{0, NULL, &KSNODETYPE_ADC, NULL}};

/* Nodes 0 and 1 of the capture topology, named as the codec driver names them */
static PCNODE_DESCRIPTOR TopoInNodes[] = {{0, VolumeAutomationTable, &KSNODETYPE_VOLUME, &KSNODETYPE_MICROPHONE},
                                          {0, MuteAutomationTable, &KSNODETYPE_MUTE, &KSNODETYPE_MICROPHONE}};

/* Nodes 0 and 1 of the render topology */
static PCNODE_DESCRIPTOR TopoOutNodes[] = {{0, VolumeAutomationTable, &KSNODETYPE_VOLUME, &KSAUDFNAME_MASTER_VOLUME},
                                           {0, MuteAutomationTable, &KSNODETYPE_MUTE, &KSAUDFNAME_MASTER_MUTE}};

static PCCONNECTION_DESCRIPTOR DacConnections[] = {{KSFILTER_NODE, 0, 0, 1}, {0, 0, KSFILTER_NODE, 1}};

static PCCONNECTION_DESCRIPTOR AdcConnections[] = {{KSFILTER_NODE, 1, 0, 1}, {0, 0, KSFILTER_NODE, 0}};

static PCCONNECTION_DESCRIPTOR TopoInConnections[] = {{KSFILTER_NODE, 1, 0, 1}, {1, 0, KSFILTER_NODE, 0}, {0, 0, 1, 1}};

static PCCONNECTION_DESCRIPTOR TopoOutConnections[] = {{KSFILTER_NODE, 0, 0, 1}, {1, 0, KSFILTER_NODE, 1}, {0, 0, 1, 1}};

static KSDATARANGE DataRangeBridge[] =
{
   {{
      sizeof(KSDATARANGE),
      0, 0, 0,
      {STATICGUIDOF(KSDATAFORMAT_TYPE_AUDIO)},
      {STATICGUIDOF(KSDATAFORMAT_SUBTYPE_ANALOG)},
      {STATICGUIDOF(KSDATAFORMAT_SPECIFIER_NONE)}
   }}
};

static PKSDATARANGE DataRangesBridge[] =
{
    DataRangeBridge
};

/* The pins of the capture wave filter: pin 0 streams and gets its data ranges per endpoint (DSPPCM_PIN_RANGES_MAX at most); pin 1 is the bridge to the topology */
static PCPIN_DESCRIPTOR WaveInPins[] =
{
    {
        1,1,0,
        NULL,
        {
            0,
            NULL,
            0,
            NULL,
            0,
            NULL,
            KSPIN_DATAFLOW_OUT,
            KSPIN_COMMUNICATION_SINK,
            &PINNAME_CAPTURE,
            &KSAUDFNAME_RECORDING_CONTROL,
            {0}
        }
    },
    {
        0,0,0,
        NULL,
        {
            0,
            NULL,
            0,
            NULL,
            1,
            DataRangesBridge,
            KSPIN_DATAFLOW_IN,
            KSPIN_COMMUNICATION_NONE,
            &KSCATEGORY_AUDIO,
            NULL,
            {0}
        }
    }
};

/* The pins of the render wave filter, the same shape with the flows reversed */
static PCPIN_DESCRIPTOR WaveOutPins[] =
{
    {
        1,1,0,
        NULL,
        {
            0,
            NULL,
            0,
            NULL,
            0,
            NULL,
            KSPIN_DATAFLOW_IN,
            KSPIN_COMMUNICATION_SINK,
            &KSCATEGORY_AUDIO,
            &KSAUDFNAME_PC_SPEAKER,
            {0}
        }
    },
    {
        0,0,0,
        NULL,
        {
            0,
            NULL,
            0,
            NULL,
            1,
            DataRangesBridge,
            KSPIN_DATAFLOW_OUT,
            KSPIN_COMMUNICATION_NONE,
            &KSCATEGORY_AUDIO,
            NULL,
            {0}
        }
    }
};

static PCPIN_DESCRIPTOR TopoInPins[] =
{
    {
        0,0,0,
        NULL,
        {
            0,
            NULL,
            0,
            NULL,
            1,
            DataRangesBridge,
            KSPIN_DATAFLOW_OUT,
            KSPIN_COMMUNICATION_NONE,
            &KSCATEGORY_AUDIO,
            NULL,
            {0}
        }
    },
    {
        0,0,0,
        NULL,
        {
            0,
            NULL,
            0,
            NULL,
            1,
            DataRangesBridge,
            KSPIN_DATAFLOW_IN,
            KSPIN_COMMUNICATION_NONE,
            &KSNODETYPE_MICROPHONE,
            &KSNODETYPE_MICROPHONE,
            {0}
        }
    }
};

static PCPIN_DESCRIPTOR TopoOutPins[] =
{
    {
        0,0,0,
        NULL,
        {
            0,
            NULL,
            0,
            NULL,
            1,
            DataRangesBridge,
            KSPIN_DATAFLOW_IN,
            KSPIN_COMMUNICATION_NONE,
            &KSCATEGORY_AUDIO,
            NULL,
            {0}
        }
    },
    {
        0,0,0,
        NULL,
        {
            0,
            NULL,
            0,
            NULL,
            1,
            DataRangesBridge,
            KSPIN_DATAFLOW_OUT,
            KSPIN_COMMUNICATION_NONE,
            &KSNODETYPE_SPEAKER,
            &KSAUDFNAME_PC_SPEAKER,
            {0}
        }
    }
};

/* The four filters: an endpoint's adapter copies the wave filter it needs and fills its streaming pin's data ranges */
static PCFILTER_DESCRIPTOR WaveInFilterDescription[] =
{
    {
        0,
        NULL,
        sizeof(PCPIN_DESCRIPTOR),
        2,
        WaveInPins,
        sizeof(PCNODE_DESCRIPTOR),
        1,
        AdcNode,
        2,
        AdcConnections,
        0,
        NULL
    }
};

static PCFILTER_DESCRIPTOR WaveOutFilterDescription[] =
{
    {
        0,
        NULL,
        sizeof(PCPIN_DESCRIPTOR),
        2,
        WaveOutPins,
        sizeof(PCNODE_DESCRIPTOR),
        1,
        DacNode,
        2,
        DacConnections,
        0,
        NULL
    }
};

static PCFILTER_DESCRIPTOR TopoInFilterDescription[] =
{
    {
        0,
        &TopologyAutomationTable,
        sizeof(PCPIN_DESCRIPTOR),
        2,
        TopoInPins,
        sizeof(PCNODE_DESCRIPTOR),
        2,
        TopoInNodes,
        3,
        TopoInConnections,
        0,
        NULL
    }
};

static PCFILTER_DESCRIPTOR TopoOutFilterDescription[] =
{
    {
        0,
        &TopologyAutomationTable,
        sizeof(PCPIN_DESCRIPTOR),
        2,
        TopoOutPins,
        sizeof(PCNODE_DESCRIPTOR),
        2,
        TopoOutNodes,
        3,
        TopoOutConnections,
        0,
        NULL
    }
};

/* The index of the streaming pin of every wave filter, and of the bridge pin; the topology's pins are the reverse */
#define DSPPCM_WAVE_STREAM_PIN     0
#define DSPPCM_WAVE_BRIDGE_PIN     1
#define DSPPCM_TOPO_WAVE_PIN       0
#define DSPPCM_TOPO_JACK_PIN       1

#endif
