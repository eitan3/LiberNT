/*
 * PROJECT:     LiberNT Core Audio
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     What two properties of an audio endpoint are answered from, so that the driver's backend and the host test run the same code (FD-01): the form factor (PKEY_AudioEndpoint_FormFactor) from the type the codec driver gave the pin, which it names the pin's subdevices by (the reference string of the device interface the wave pin is connected to), or from the DSP's PCM driver for an endpoint that has no jack; and the physical speakers (PKEY_AudioEndpoint_PhysicalSpeakers) from the endpoint's channel count. A value with no source is not answered
 * COPYRIGHT:   Copyright 2026 LiberNT contributors
 */

#ifndef _MMDEVAPI_ENDPOINT_PROPS_H_
#define _MMDEVAPI_ENDPOINT_PROPS_H_

#include <stddef.h>

/* The includer provides BOOL, DWORD, WORD, WCHAR, TRUE, FALSE, EDataFlow (eRender, eCapture), KSAUDIO_SPEAKER_MONO, KSAUDIO_SPEAKER_STEREO, KSAUDIO_SPEAKER_QUAD, KSAUDIO_SPEAKER_5POINT1, KSAUDIO_SPEAKER_7POINT1 and the EndpointFormFactor members (Speakers, LineLevel, Headphones, Microphone, SPDIF, DigitalAudioDisplayDevice, UnknownFormFactor) */

/* The length of a wide string */
static __inline size_t endpoint_text_length(const WCHAR *text)
{
    size_t length = 0;

    while (text[length])
        length++;
    return length;
}

/* One character as a lower-case letter: the names compared here are ASCII */
static __inline WCHAR endpoint_lower(WCHAR character)
{
    if (character >= L'A' && character <= L'Z')
        return character + (L'a' - L'A');
    return character;
}

/* Whether Text begins with Prefix, without regard to case */
static __inline BOOL endpoint_text_starts(const WCHAR *text, const WCHAR *prefix)
{
    while (*prefix)
    {
        if (endpoint_lower(*text++) != endpoint_lower(*prefix++))
            return FALSE;
    }
    return TRUE;
}

/* Whether two strings are the same, without regard to case */
static __inline BOOL endpoint_text_same(const WCHAR *left, const WCHAR *right)
{
    size_t length = endpoint_text_length(right);

    return endpoint_text_starts(left, right) && !left[length];
}

/*
 * The kinds the codec driver gives a pin by the device field of the pin's
 * configuration default (drivers/wdm/audio/drivers/hdaudio/common.cpp,
 * ChooseSubdeviceName): it names the pin's subdevices after the kind, an
 * ordinal when the codec has more than one of it, and Topo or Wave. The
 * driver builds a render subdevice of the kinds below 0x8 and a capture
 * subdevice of the rest. The form factor each kind is for the flow it belongs
 * to; a kind without a member of the enumeration answers UnknownFormFactor.
 */
struct endpoint_pin_kind
{
    const WCHAR *name;
    DWORD render;
    DWORD capture;
};

static const struct endpoint_pin_kind endpoint_pin_kinds[] =
{
    { L"eLineOut",          LineLevel,                LineLevel              },
    { L"eSpeaker",          Speakers,                 0                      },
    { L"eHeadphone",        Headphones,               0                      },
    { L"eCDIn",             0,                        0                      },
    { L"eSpdifOut",         SPDIF,                    0                      },
    { L"eDigitalOtherOut",  0,                        0                      },
    { L"eModemLineSide",    0,                        0                      },
    { L"eModemHandsetSide", 0,                        0                      },
    { L"eLineIn",           0,                        LineLevel              },
    { L"eAuxIn",            0,                        0                      },
    { L"eMicIn",            0,                        Microphone             },
    { L"eTelephony",        0,                        0                      },
    { L"eSpdifIn",          0,                        0                      },
    { L"eDigitalOtherIn",   0,                        0                      },
    { L"eReserved",         0,                        0                      },
    { L"eOther",            0,                        0                      },
    { L"eHDMIOut",          DigitalAudioDisplayDevice, 0                      },
};

/*
 * Whether Path is the device interface of the DSP's PCM: the audio child
 * intcdsp creates under the bus's child (drivers/wdm/audio/intcdsp/audioif.c,
 * INTELDSP_CHILD_DEVICE_ID), the rule the apitests find an endpoint by
 * (modules/rostests/apitests/include/apitest_dspidentity.h).
 */
static __inline BOOL endpoint_is_dsp_pcm(const WCHAR *path)
{
    static const WCHAR enumerator[] = L"intcdsp";
    static const WCHAR device_id[] = L"pcm&ctlr_ven_8086&ctlr_dev_";
    static const WCHAR controllers[][5] = { L"7f50", L"7e28" };
    size_t i;

    if (endpoint_text_starts(path, L"\\\\?\\"))
        path += 4;
    if (!endpoint_text_starts(path, enumerator))
        return FALSE;
    path += endpoint_text_length(enumerator);
    if (*path != L'#' && *path != L'\\')
        return FALSE;
    path++;
    if (!endpoint_text_starts(path, device_id))
        return FALSE;
    path += endpoint_text_length(device_id);
    for (i = 0; i < sizeof(controllers) / sizeof(controllers[0]); i++)
    {
        if (endpoint_text_starts(path, controllers[i]) &&
            (path[4] == L'#' || path[4] == L'\\' || !path[4]))
            return TRUE;
    }
    return FALSE;
}

/*
 * The form factor of the endpoint whose wave pin is connected to the filter
 * at Path, for its Flow: TRUE and the value when the path tells it. A pin of
 * the codec driver tells the kind it was given; an endpoint of the DSP's PCM
 * has no jack (its jack description says an integrated device of an unknown
 * connection type, drivers/wdm/audio/drivers/dsppcm/prophnd.cpp), so it is
 * of an unknown form factor. Any other path answers nothing.
 */
static __inline BOOL endpoint_form_factor(const WCHAR *path, EDataFlow flow, DWORD *value)
{
    const WCHAR *reference = path, *at;
    size_t i;

    /* the reference string: the last component of the path */
    for (at = path; *at; at++)
    {
        if (*at == L'\\')
            reference = at + 1;
    }

    for (i = 0; i < sizeof(endpoint_pin_kinds) / sizeof(endpoint_pin_kinds[0]); i++)
    {
        const struct endpoint_pin_kind *kind = &endpoint_pin_kinds[i];

        if (!endpoint_text_starts(reference, kind->name))
            continue;
        at = reference + endpoint_text_length(kind->name);
        while (*at >= L'0' && *at <= L'9')
            at++;
        if (!endpoint_text_same(at, L"Topo") && !endpoint_text_same(at, L"Wave"))
            continue;
        *value = flow == eCapture ? kind->capture : kind->render;
        if (!*value)
            *value = UnknownFormFactor;
        return TRUE;
    }

    if (endpoint_is_dsp_pcm(path))
    {
        *value = UnknownFormFactor;
        return TRUE;
    }
    return FALSE;
}

/*
 * The physical speakers of an endpoint of Channels channels: the speaker mask
 * of the channel configuration ksmedia.h names (the same masks the endpoint's
 * format is built with), and nothing for a count that has none.
 */
static __inline BOOL endpoint_physical_speakers(WORD channels, DWORD *mask)
{
    switch (channels)
    {
        case 1:
            *mask = KSAUDIO_SPEAKER_MONO;
            return TRUE;
        case 2:
            *mask = KSAUDIO_SPEAKER_STEREO;
            return TRUE;
        case 4:
            *mask = KSAUDIO_SPEAKER_QUAD;
            return TRUE;
        case 6:
            *mask = KSAUDIO_SPEAKER_5POINT1;
            return TRUE;
        case 8:
            *mask = KSAUDIO_SPEAKER_7POINT1;
            return TRUE;
        default:
            return FALSE;
    }
}

#endif /* _MMDEVAPI_ENDPOINT_PROPS_H_ */
