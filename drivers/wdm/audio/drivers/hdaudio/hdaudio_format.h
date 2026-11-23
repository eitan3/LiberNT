/*
 * PROJECT:         ReactOS HDAudio Driver
 * LICENSE:         GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:         Pure sample-container rules, shared by the driver and the host tests
 * COPYRIGHT:       Copyright 2026 LiberNT contributors
 */

#pragma once

/*
 * The size of the word the controller reads a sample of ValidBits from. The
 * stream format word selects it with BITS = 8, 16, 20, 24 or 32, and 20- and
 * 24-bit samples live in the same 32-bit word as 32-bit ones, left-justified
 * with the unused low bits ignored. There is no word size for a 3-byte
 * container: a packed 24-bit stream cannot be described to the hardware.
 */
static __inline ULONG
HDAUDIO_SampleContainerBits(ULONG ValidBits)
{
    if (ValidBits <= 8)
        return 8;
    if (ValidBits <= 16)
        return 16;
    return 32;
}
