/* Host stand-in for the stream format the ADSP bus interface passes by value: the driver code compiled on the host never looks into it */
#include "../shim.h"

#ifndef FWLOAD_STUB_HDAUDIO_H
#define FWLOAD_STUB_HDAUDIO_H

typedef struct _HDAUDIO_STREAM_FORMAT
{
    UINT32 Opaque[2];
} HDAUDIO_STREAM_FORMAT, *PHDAUDIO_STREAM_FORMAT;

#endif
