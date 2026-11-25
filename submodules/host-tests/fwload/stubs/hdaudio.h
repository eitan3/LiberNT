/* Host stand-in for the stream format the ADSP bus interface passes by value, with the fields the DSP driver's stream manager reads; the controller's stream format word is never built on the host */
#include "../shim.h"

#ifndef FWLOAD_STUB_HDAUDIO_H
#define FWLOAD_STUB_HDAUDIO_H

typedef struct _HDAUDIO_STREAM_FORMAT
{
    ULONG SampleRate;
    USHORT ValidBitsPerSample;
    USHORT ContainerSize;
    USHORT NumberOfChannels;
} HDAUDIO_STREAM_FORMAT, *PHDAUDIO_STREAM_FORMAT;

/* hdaudio.h HDAUDIO_STREAM_STATE: the states the codec interface's SetDmaEngineState names */
typedef enum _HDAUDIO_STREAM_STATE
{
    ResetState = 0,
    StopState = 1,
    PauseState = 1,
    RunState = 2
} HDAUDIO_STREAM_STATE;

#endif
