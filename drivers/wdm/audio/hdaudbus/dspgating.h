/*
 * PROJECT:     LiberNT audio driver libraries
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     The platform's clock and power gating of the DSP, two bits of the controller's PCI config space, as pure values: the bus writes them (adsp.cpp ADSPSetDspPowerGating) and the host tests check the same rule over a model
 * COPYRIGHT:   Copyright 2026 LiberNT contributors
 */

#ifndef _HDAUDBUS_DSPGATING_H_
#define _HDAUDBUS_DSPGATING_H_

/* the kernel's types come with the includer, as policy.h's do */

/* PCI config space (hda_registers.h HDA_PCIREG_TCSEL and INTEL_HDA_CGCTL). Linux sof/intel/hda.h names 0x44 PCI_PGCTL with ADSPPGD at bit 2 and 0x48 PCI_CGCTL with ADSPDCGE at bit 1, and its hda_dsp_ctrl_clock_power_gating takes both off around a firmware boot (hda.c hda_dsp_pre_fw_run and hda_dsp_post_fw_run). Those files were studied for the behaviour under LICENSING.md rule 7: a set ADSPPGD means the DSP is not power-gated, a set ADSPDCGE means its clock may be gated */
#define HDA_PCIREG_PGCTL           0x44
#define HDA_PGCTL_ADSPPGD          (1u << 2)
#define HDA_PCIREG_CGCTL           0x48
#define HDA_CGCTL_ADSPDCGE         (1u << 1)

/* The two registers as a whole 32-bit word each */
typedef struct _HDA_DSP_GATING
{
    UINT32 Pgctl;
    UINT32 Cgctl;
} HDA_DSP_GATING, *PHDA_DSP_GATING;

/*
 * The DSP's power gating allowed or not, from the registers as they are: allowed, the platform
 * may clock and power gate the DSP while it is idle (ADSPDCGE set, ADSPPGD clear); not allowed,
 * the DSP stays powered for a boot (ADSPDCGE clear, ADSPPGD set). Every other bit of both
 * registers is the caller's and stays as it was. The link's own L1 policy (BAR0 EM2 L1SEN),
 * which Linux sets beside them, is not ours: nothing here touches it.
 */
static
__inline
VOID
HdaDspPowerGatingValues(
    _In_ UINT32 AllowGating,
    _In_ UINT32 Pgctl,
    _In_ UINT32 Cgctl,
    _Out_ PHDA_DSP_GATING Gating)
{
    Gating->Cgctl = AllowGating ? (Cgctl | HDA_CGCTL_ADSPDCGE) : (Cgctl & ~HDA_CGCTL_ADSPDCGE);
    Gating->Pgctl = AllowGating ? (Pgctl & ~HDA_PGCTL_ADSPPGD) : (Pgctl | HDA_PGCTL_ADSPPGD);
}

#endif
