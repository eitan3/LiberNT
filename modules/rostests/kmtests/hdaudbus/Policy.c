/*
 * PROJECT:     LiberNT kernel-mode tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     The audio path follows the codecs, the DSP capability and the NHLT endpoints
 * COPYRIGHT:   Copyright 2026 LiberNT contributors
 */

#include <kmt_test.h>
#include <policy.h>

#define NDEBUG
#include <debug.h>

#include "dsp_cases.h"

START_TEST(Policy)
{
    DSP_AUDIO_PATH_EVIDENCE Evidence;
    DSP_AUDIO_PATH Path;
    ULONG Index;

    for (Index = 0; Index < sizeof(PolicyCases) / sizeof(PolicyCases[0]); Index++)
    {
        const POLICY_CASE* Case = &PolicyCases[Index];

        RtlZeroMemory(&Evidence, sizeof(Evidence));
        Evidence.CodecMask = Case->CodecMask;
        Evidence.NonHdaEndpoints = Case->NonHdaEndpoints;
        Evidence.DspPresent = Case->DspPresent;
        Evidence.NhltTableValid = Case->NhltTableValid;

        Path = DspAudioPathDecide(&Evidence);
        ok_eq_int(Path, Case->Expected);
        ok_eq_str(DspAudioPathName(Path), Case->ExpectedName);
    }

    ok_eq_uint(DspAudioPathNone, 0);
    ok_eq_str(DspAudioPathName(DspAudioPathNone), "None");
    ok_eq_str(DspAudioPathName(DspAudioPathLegacyHdaOnly), "LegacyHdaOnly");
    ok_eq_str(DspAudioPathName(DspAudioPathDspOptional), "DspOptional");
    ok_eq_str(DspAudioPathName(DspAudioPathDspRequired), "DspRequired");
    ok_eq_str(DspAudioPathName((DSP_AUDIO_PATH)4), "Unknown");
}
