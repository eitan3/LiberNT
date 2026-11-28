/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     The direction the dsploop apitest's walk follows from an endpoint's connector through the device topology's parts (N15-20), free of COM so the host tests run the rule the apitest runs
 * COPYRIGHT:   Copyright 2026 LiberNT contributors
 */

#ifndef _APITESTS_DSPLOOP_WALK_H_
#define _APITESTS_DSPLOOP_WALK_H_

/* The data flow of the endpoint's connector, as IConnector::GetDataFlow answers it */
typedef enum _DSPLOOP_WALK_FLOW
{
    DsploopFlowIn = 0,   /* a render endpoint: the signal enters its connector */
    DsploopFlowOut = 1   /* a capture endpoint: the signal leaves its connector */
} DSPLOOP_WALK_FLOW;

/*
 * Whether a walk from an endpoint's connector lists the parts incoming to a part (1) or outgoing from it (0).
 * IPart::EnumPartsIncoming answers the parts immediately upstream of a part and EnumPartsOutgoing those
 * downstream (devicetopology.h). The connector an endpoint's connector is bridged to on the topology filter
 * is the endpoint's own pin (the jack's): of a render endpoint the volume node is upstream of it (the
 * topology's signal runs from the wave's pin through the DAC and the bridge pin to the topology's pin, the
 * volume and the mute node, then the endpoint's pin), of a capture endpoint downstream (the microphone's
 * pin, the volume, the mute, the bridge pin). E5b's probe: the outgoing lists leave nothing from a render
 * endpoint's pin, so a walk along them finds no node.
 */
static inline
int
DsploopWalkIncoming(
    DSPLOOP_WALK_FLOW Flow)
{
    return Flow == DsploopFlowIn;
}

#endif /* _APITESTS_DSPLOOP_WALK_H_ */
