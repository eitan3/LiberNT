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

/*
 * What a write of the two registers read of them after each of its forms: the
 * dword form (the whole word each), and the byte form it tries once when a bit
 * did not read back as written (the register's low byte, which carries the bit).
 */
typedef struct _HDA_DSP_GATING_READBACK
{
    UINT32 PgctlAfterDword;
    UINT32 CgctlAfterDword;
    UINT32 PgctlAfterByte;
    UINT32 CgctlAfterByte;
    /* the byte form was written: a bit was not as written after the dword form */
    BOOLEAN ByteForm;
} HDA_DSP_GATING_READBACK, *PHDA_DSP_GATING_READBACK;

/* Whether the two bits are as the rule wants them, whatever the registers' other bits are */
static
__inline
BOOLEAN
HdaDspPowerGatingBitsAsWritten(
    _In_ UINT32 AllowGating,
    _In_ UINT32 Pgctl,
    _In_ UINT32 Cgctl)
{
    if (AllowGating)
        return !(Pgctl & HDA_PGCTL_ADSPPGD) && (Cgctl & HDA_CGCTL_ADSPDCGE) != 0;
    return (Pgctl & HDA_PGCTL_ADSPPGD) != 0 && !(Cgctl & HDA_CGCTL_ADSPDCGE);
}

/* One transfer of the controller's PCI config space through the PCI bus interface the bus driver holds (wdm.h BUS_INTERFACE_STANDARD): a read, or a write when Write is set; TRUE when it moved the whole buffer. The members' type is the headers' to name */
static
__inline
BOOLEAN
HdaDspPciTransfer(
    _In_ PBUS_INTERFACE_STANDARD Pci,
    _In_ BOOLEAN Write,
    _In_ ULONG Offset,
    _In_ PVOID Buffer,
    _In_ ULONG Length)
{
    if (Write)
        return Pci->SetBusData(Pci->Context, PCI_WHICHSPACE_CONFIG, Buffer, Offset, Length) == Length;
    return Pci->GetBusData(Pci->Context, PCI_WHICHSPACE_CONFIG, Buffer, Offset, Length) == Length;
}

/*
 * The platform's power gating of the DSP written through the PCI bus interface,
 * the two registers read back after the writes: a bit that does not read as
 * written gets the byte form once (the low byte of each register, in the
 * dword form's order), and one that still does not ends the call with
 * STATUS_UNSUCCESSFUL. A word that does not read or write whole ends the call
 * with STATUS_DEVICE_PROTOCOL_ERROR and nothing more written. What was read
 * is the caller's, for its line.
 */
static
__inline
NTSTATUS
HdaDspPowerGatingWrite(
    _In_ PBUS_INTERFACE_STANDARD Pci,
    _In_ UINT32 AllowGating,
    _Out_ PHDA_DSP_GATING_READBACK Readback)
{
    HDA_DSP_GATING Gating;
    UINT32 Pgctl, Cgctl;
    UINT8 Byte;
    NTSTATUS Status = STATUS_UNSUCCESSFUL;

    RtlZeroMemory(Readback, sizeof(*Readback));
    if (Pci->GetBusData == NULL || Pci->SetBusData == NULL)
        return STATUS_NOT_SUPPORTED;
    if (!HdaDspPciTransfer(Pci, FALSE, HDA_PCIREG_CGCTL, &Gating.Cgctl, sizeof(Gating.Cgctl)) ||
        !HdaDspPciTransfer(Pci, FALSE, HDA_PCIREG_PGCTL, &Gating.Pgctl, sizeof(Gating.Pgctl)))
        return STATUS_DEVICE_PROTOCOL_ERROR;
    HdaDspPowerGatingValues(AllowGating, Gating.Pgctl, Gating.Cgctl, &Gating);
    if (!HdaDspPciTransfer(Pci, TRUE, HDA_PCIREG_CGCTL, &Gating.Cgctl, sizeof(Gating.Cgctl)) ||
        !HdaDspPciTransfer(Pci, TRUE, HDA_PCIREG_PGCTL, &Gating.Pgctl, sizeof(Gating.Pgctl)))
        return STATUS_DEVICE_PROTOCOL_ERROR;
    if (!HdaDspPciTransfer(Pci, FALSE, HDA_PCIREG_PGCTL, &Pgctl, sizeof(Pgctl)) ||
        !HdaDspPciTransfer(Pci, FALSE, HDA_PCIREG_CGCTL, &Cgctl, sizeof(Cgctl)))
        return STATUS_DEVICE_PROTOCOL_ERROR;
    Readback->PgctlAfterDword = Pgctl;
    Readback->CgctlAfterDword = Cgctl;
    if (HdaDspPowerGatingBitsAsWritten(AllowGating, Pgctl, Cgctl))
        return STATUS_SUCCESS;
    Readback->ByteForm = TRUE;
    Byte = AllowGating ? (UINT8)(Cgctl | HDA_CGCTL_ADSPDCGE) : (UINT8)(Cgctl & ~HDA_CGCTL_ADSPDCGE);
    if (!HdaDspPciTransfer(Pci, TRUE, HDA_PCIREG_CGCTL, &Byte, sizeof(Byte)))
        return STATUS_DEVICE_PROTOCOL_ERROR;
    Byte = AllowGating ? (UINT8)(Pgctl & ~HDA_PGCTL_ADSPPGD) : (UINT8)(Pgctl | HDA_PGCTL_ADSPPGD);
    if (!HdaDspPciTransfer(Pci, TRUE, HDA_PCIREG_PGCTL, &Byte, sizeof(Byte)))
        return STATUS_DEVICE_PROTOCOL_ERROR;
    if (!HdaDspPciTransfer(Pci, FALSE, HDA_PCIREG_PGCTL, &Pgctl, sizeof(Pgctl)) ||
        !HdaDspPciTransfer(Pci, FALSE, HDA_PCIREG_CGCTL, &Cgctl, sizeof(Cgctl)))
        return STATUS_DEVICE_PROTOCOL_ERROR;
    Readback->PgctlAfterByte = Pgctl;
    Readback->CgctlAfterByte = Cgctl;
    if (HdaDspPowerGatingBitsAsWritten(AllowGating, Pgctl, Cgctl))
        Status = STATUS_SUCCESS;
    return Status;
}

#endif
