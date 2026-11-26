/*
 * PROJECT:     ReactOS HDAudio Driver
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Which PnP requests end the codec driver's adapter and which leave it for the start that follows: free of I/O, so that the host tests run the codec driver's own rule
 * COPYRIGHT:   Copyright 2026 LiberNT contributors
 */

#ifndef _DRIVERS_HDAUDIO_ADAPTERPNP_H_
#define _DRIVERS_HDAUDIO_ADAPTERPNP_H_

#include <ntddk.h>
#include <wdm.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Whether a PnP request ends the adapter of the device (hdaudio.cpp
 * HDAUDIO_Pnp): the removal and the surprise removal do. A stop leaves the
 * adapter, its subdevices and their registrations in place for the start
 * that follows, which finds the adapter and registers nothing a second
 * time (FA-02, as dsppcm does since R1-06): PortCls passes a stop down
 * without unregistering the subdevices (portcls/irp.cpp), so the miniports'
 * adapter must stay alive as long as they are registered.
 */
static
inline
BOOLEAN
HDAudioPnpReleasesAdapter(
    _In_ UCHAR MinorFunction)
{
    return MinorFunction == IRP_MN_REMOVE_DEVICE || MinorFunction == IRP_MN_SURPRISE_REMOVAL;
}

#ifdef __cplusplus
}
#endif

#endif /* _DRIVERS_HDAUDIO_ADAPTERPNP_H_ */
