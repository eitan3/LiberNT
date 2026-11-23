/*
 * PROJECT:     LiberNT HD Audio Bus Driver
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Chooses the audio path a controller offers from the evidence found on it
 * COPYRIGHT:   Copyright 2026 LiberNT contributors
 */

#include <ntddk.h>
#include "policy.h"

DSP_AUDIO_PATH
DspAudioPathDecide(
    _In_ const DSP_AUDIO_PATH_EVIDENCE* Evidence)
{
    BOOLEAN DspEvidence;

    if (Evidence->CodecMask != 0)
    {
        if (Evidence->DspPresent)
            return DspAudioPathDspOptional;
        return DspAudioPathLegacyHdaOnly;
    }

    DspEvidence = Evidence->DspPresent && Evidence->NhltTableValid && Evidence->NonHdaEndpoints != 0;
    if (DspEvidence)
        return DspAudioPathDspRequired;
    return DspAudioPathNone;
}

const char*
DspAudioPathName(
    DSP_AUDIO_PATH Path)
{
    static const char* const Names[] =
    {
        "None", "LegacyHdaOnly", "DspOptional", "DspRequired"
    };

    if ((UINT32)Path >= sizeof(Names) / sizeof(Names[0]))
        return "Unknown";
    return Names[Path];
}
