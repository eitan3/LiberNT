/*
 * PROJECT:     LiberNT HD Audio Bus Driver
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Chooses the audio path a controller offers from the evidence found on it
 * COPYRIGHT:   Copyright 2026 LiberNT contributors
 */

#ifndef _SKLHDAUDBUS_POLICY_H_
#define _SKLHDAUDBUS_POLICY_H_

#ifdef __cplusplus
extern "C" {
#endif

typedef enum _DSP_AUDIO_PATH
{
    DspAudioPathNone = 0,
    DspAudioPathLegacyHdaOnly,
    DspAudioPathDspOptional,
    DspAudioPathDspRequired
} DSP_AUDIO_PATH;

typedef struct _DSP_AUDIO_PATH_EVIDENCE
{
    UINT16 CodecMask;
    UINT16 NonHdaEndpoints;
    BOOLEAN DspPresent;
    BOOLEAN NhltTableValid;
} DSP_AUDIO_PATH_EVIDENCE, *PDSP_AUDIO_PATH_EVIDENCE;

DSP_AUDIO_PATH DspAudioPathDecide(_In_ const DSP_AUDIO_PATH_EVIDENCE* Evidence);
const char* DspAudioPathName(DSP_AUDIO_PATH Path);

#ifdef __cplusplus
}
#endif

#endif
