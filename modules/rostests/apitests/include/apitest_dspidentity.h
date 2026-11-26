/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     The identity of the DSP's PCM in the device tree for the apitests: the audio child intcdsp creates under the bus's child, INTCDSP\PCM&CTLR_VEN_8086&CTLR_DEV_7F50 or 7E28, read from an instance path or from a KS interface path, so that an endpoint is found by what it is and not by a display name
 * COPYRIGHT:   Copyright 2026 LiberNT contributors
 */

#ifndef _APITEST_DSPIDENTITY_H_
#define _APITEST_DSPIDENTITY_H_

#include <apitest.h>
#include <wchar.h>

/* The enumerator and the device id of the DSP's audio child, as hdaudbus names its own child of the controller (drivers/wdm/audio/intcdsp/audioif.c, INTELDSP_CHILD_DEVICE_ID) */
#define DSPPCM_ENUMERATOR   L"intcdsp"
#define DSPPCM_DEVICE_ID    L"pcm&ctlr_ven_8086&ctlr_dev_"
/* The same for messages */
#define DSPPCM_ID_TEXT      "INTCDSP\\PCM&CTLR_VEN_8086&CTLR_DEV_7F50 or 7E28"

/*
 * Whether Text names the DSP's PCM: a device instance path
 * (INTCDSP\PCM&CTLR_VEN_8086&CTLR_DEV_7F50\00) or a KS interface path
 * (\\?\intcdsp#pcm&ctlr_ven_8086&ctlr_dev_7f50#00#{...}), behind the "{2}."
 * of an audio filter device or not, case-insensitively: the first segment is
 * the enumerator, the second is the device id and ends in one of the two
 * controllers the plan names (7F50, 7E28).
 */
static __inline BOOL
DspPcmIdentity(
    const WCHAR *Text)
{
    static const WCHAR Controllers[][5] = { L"7f50", L"7e28" };
    size_t I;

    if (!wcsncmp(Text, L"{2}.", 4))
        Text += 4;
    if (!wcsncmp(Text, L"\\\\?\\", 4))
        Text += 4;
    if (_wcsnicmp(Text, DSPPCM_ENUMERATOR, ARRAY_SIZE(DSPPCM_ENUMERATOR) - 1))
        return FALSE;
    Text += ARRAY_SIZE(DSPPCM_ENUMERATOR) - 1;
    if (*Text != L'#' && *Text != L'\\')
        return FALSE;
    Text++;
    if (_wcsnicmp(Text, DSPPCM_DEVICE_ID, ARRAY_SIZE(DSPPCM_DEVICE_ID) - 1))
        return FALSE;
    Text += ARRAY_SIZE(DSPPCM_DEVICE_ID) - 1;
    for (I = 0; I < ARRAY_SIZE(Controllers); I++)
    {
        if (!_wcsnicmp(Text, Controllers[I], 4) && (Text[4] == L'#' || Text[4] == L'\\' || !Text[4]))
            return TRUE;
    }
    return FALSE;
}

#endif /* _APITEST_DSPIDENTITY_H_ */
