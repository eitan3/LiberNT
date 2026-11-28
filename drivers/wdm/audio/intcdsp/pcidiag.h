/*
 * PROJECT:     LiberNT Intel Smart Sound DSP driver
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     The diagnostics of a boot whose DSP window does not answer, as pure values over the PCI bus interface the bus hands out: the controller's config space as dwords, the power-management capability of its capability list, and one write control (the Cache Line Size byte, written, read back and restored) that says whether the path carries a write to this device at all; the driver logs them (hwops.c IntcDspLogDeadWindow) and the host tests check them over a model of the config space
 * COPYRIGHT:   Copyright 2026 LiberNT contributors
 */

#ifndef _DRIVERS_INTCDSP_PCIDIAG_H_
#define _DRIVERS_INTCDSP_PCIDIAG_H_

/* the kernel's types and the PCI bus interface come with the includer (intcdsp.h); fwload.h names the boot's status */
#include <fwload.h>

/*
 * PCI configuration space, the header's bytes and the capability list (the PCI Bus Specification; the
 * kernel's headers name the structures, not these offsets). Cache Line Size is one byte at 0x0C. The
 * Capabilities Pointer at 0x34 names the first capability in its low byte, 0 for none; a capability's
 * header is its ID in byte 0 and the next one's pointer in byte 1; the capabilities lie past the
 * header, each at a dword-aligned offset, and a list is at most 48 long.
 */
#define INTCDSP_PCI_CACHE_LINE_SIZE   0x0C
#define INTCDSP_PCI_CAPABILITIES_PTR  0x34
#define INTCDSP_PCI_CAP_PM            0x01
#define INTCDSP_PCI_CAP_FLOOR         0x40
#define INTCDSP_PCI_CAPS_MAX          48

/*
 * One write control, through the bus interface's own two members: the Cache Line Size byte as read,
 * the value written in its place, what a read answered after the write, and what a read answered
 * after the original was written back. Done when the four transfers went whole; when one did not,
 * the register is left as the last whole write put it.
 */
typedef struct _INTCDSP_PCI_WRITE_CONTROL
{
    UINT8 Original;
    UINT8 Wrote;
    UINT8 Read;
    UINT8 Restored;
    BOOLEAN Done;
} INTCDSP_PCI_WRITE_CONTROL, *PINTCDSP_PCI_WRITE_CONTROL;

/* The power-management capability of the list: the control register is the word at its offset 4, the state in its bits 1:0 (0 is D0, 3 is D3). Pointer is the list's first offset as the pointer at 0x34 names it; Present when the list has the capability */
typedef struct _INTCDSP_PCI_PM
{
    UINT16 Control;
    UINT8 State;
    UINT8 Pointer;
    BOOLEAN Present;
} INTCDSP_PCI_PM, *PINTCDSP_PCI_PM;

/* The block's dwords: the header's four (0x00, 0x04, 0x0C, 0x34) and the vendor-specific ten from 0x40 */
#define INTCDSP_PCI_DIAG_HEADER_COUNT   4
#define INTCDSP_PCI_DIAG_VENDOR_COUNT   10
#define INTCDSP_PCI_DIAG_COUNT          (INTCDSP_PCI_DIAG_HEADER_COUNT + INTCDSP_PCI_DIAG_VENDOR_COUNT)

/* What the block read: each dword with whether the bus answered it whole, the capability, the write control */
typedef struct _INTCDSP_DEAD_WINDOW
{
    UINT32 Config[INTCDSP_PCI_DIAG_COUNT];
    BOOLEAN ConfigRead[INTCDSP_PCI_DIAG_COUNT];
    INTCDSP_PCI_PM Pm;
    INTCDSP_PCI_WRITE_CONTROL Control;
} INTCDSP_DEAD_WINDOW, *PINTCDSP_DEAD_WINDOW;

/* The block's offsets in order */
static
__inline
UINT32
IntcDspPciDiagOffset(
    _In_ UINT32 Index)
{
    switch (Index)
    {
    case 0:
        return 0x00;
    case 1:
        return 0x04;
    case 2:
        return 0x0C;
    case 3:
        return 0x34;
    default:
        return 0x40 + 4 * (Index - INTCDSP_PCI_DIAG_HEADER_COUNT);
    }
}

/* One read of the controller's config space through the PCI bus interface the bus handed out; TRUE when it moved the whole buffer */
static
__inline
BOOLEAN
IntcDspPciRead(
    _In_ PBUS_INTERFACE_STANDARD Pci,
    _In_ ULONG Offset,
    _Out_ PVOID Buffer,
    _In_ ULONG Length)
{
    return Pci->GetBusData != NULL &&
        Pci->GetBusData(Pci->Context, PCI_WHICHSPACE_CONFIG, Buffer, Offset, Length) == Length;
}

/* One write of it, the same two members the DSP's power gating goes through; TRUE when it moved the whole buffer */
static
__inline
BOOLEAN
IntcDspPciWrite(
    _In_ PBUS_INTERFACE_STANDARD Pci,
    _In_ ULONG Offset,
    _In_ const VOID* Buffer,
    _In_ ULONG Length)
{
    return Pci->SetBusData != NULL &&
        Pci->SetBusData(Pci->Context, PCI_WHICHSPACE_CONFIG, (PVOID)Buffer, Offset, Length) == Length;
}

/*
 * The write control: the Cache Line Size byte is read, another value is written in its place (0x08,
 * or 0x10 when it is 0x08 already), read back, the original is written back and read again. It reads
 * as written and the original reads restored: a write reaches this device through the path. It does
 * not read as written: no write lands through it, whatever the power gating's bits did.
 */
static
__inline
VOID
IntcDspPciWriteControl(
    _In_ PBUS_INTERFACE_STANDARD Pci,
    _Out_ PINTCDSP_PCI_WRITE_CONTROL Control)
{
    RtlZeroMemory(Control, sizeof(*Control));
    Control->Wrote = 0x08;
    if (!IntcDspPciRead(Pci, INTCDSP_PCI_CACHE_LINE_SIZE, &Control->Original, sizeof(Control->Original)))
        return;
    if (Control->Original == 0x08)
        Control->Wrote = 0x10;
    if (!IntcDspPciWrite(Pci, INTCDSP_PCI_CACHE_LINE_SIZE, &Control->Wrote, sizeof(Control->Wrote)))
        return;
    if (!IntcDspPciRead(Pci, INTCDSP_PCI_CACHE_LINE_SIZE, &Control->Read, sizeof(Control->Read)))
        return;
    if (!IntcDspPciWrite(Pci, INTCDSP_PCI_CACHE_LINE_SIZE, &Control->Original, sizeof(Control->Original)))
        return;
    if (!IntcDspPciRead(Pci, INTCDSP_PCI_CACHE_LINE_SIZE, &Control->Restored, sizeof(Control->Restored)))
        return;
    Control->Done = TRUE;
}

/*
 * The power-management capability through the list's pointer. No list, a pointer into the header,
 * one that is not dword-aligned, a list longer than the specification allows, or a word the bus does
 * not answer whole leaves the capability absent, the pointer as read.
 */
static
__inline
VOID
IntcDspPciPowerManagement(
    _In_ PBUS_INTERFACE_STANDARD Pci,
    _Out_ PINTCDSP_PCI_PM Pm)
{
    UINT32 Pointer = 0, Header, Control;
    ULONG Seen;

    RtlZeroMemory(Pm, sizeof(*Pm));
    if (!IntcDspPciRead(Pci, INTCDSP_PCI_CAPABILITIES_PTR, &Pointer, sizeof(Pointer)))
        return;
    Pointer &= 0xFF;
    Pm->Pointer = (UINT8)Pointer;
    for (Seen = 0; Pointer != 0 && Seen < INTCDSP_PCI_CAPS_MAX; Seen++)
    {
        if (Pointer < INTCDSP_PCI_CAP_FLOOR || (Pointer & 3) != 0)
            return;
        if (!IntcDspPciRead(Pci, Pointer, &Header, sizeof(Header)))
            return;
        if ((Header & 0xFF) == INTCDSP_PCI_CAP_PM)
        {
            if (!IntcDspPciRead(Pci, Pointer + 4, &Control, sizeof(Control)))
                return;
            Pm->Control = (UINT16)(Control & 0xFFFF);
            Pm->State = (UINT8)(Control & 3);
            Pm->Present = TRUE;
            return;
        }
        Pointer = (Header >> 8) & 0xFF;
    }
}

/*
 * The block's reads, through the PCI bus interface the bus handed out: only for a boot that ended
 * with FwloadDspWindowDead; for any other status nothing is read and the write control, the one
 * write the block makes, is not made. The dwords first, then the capability, then the write control,
 * its byte restored. TRUE when there is something to log.
 */
static
__inline
BOOLEAN
IntcDspDeadWindowCollect(
    _In_ FWLOAD_STATUS Status,
    _In_ PBUS_INTERFACE_STANDARD Pci,
    _Out_ PINTCDSP_DEAD_WINDOW Block)
{
    UINT32 Index;

    RtlZeroMemory(Block, sizeof(*Block));
    if (Status != FwloadDspWindowDead)
        return FALSE;
    for (Index = 0; Index < INTCDSP_PCI_DIAG_COUNT; Index++)
        Block->ConfigRead[Index] = IntcDspPciRead(Pci, IntcDspPciDiagOffset(Index), &Block->Config[Index],
            sizeof(Block->Config[Index]));
    IntcDspPciPowerManagement(Pci, &Block->Pm);
    IntcDspPciWriteControl(Pci, &Block->Control);
    return TRUE;
}

/* A dword of the block as the lines print it: 0x and its eight hex digits, or "unread" for one the bus did not answer whole. Text takes 12 characters and is the answer for a dword that was read */
static
__inline
const CHAR*
IntcDspPciDwordText(
    _In_ UINT32 Value,
    _In_ BOOLEAN Read,
    _Out_ CHAR* Text)
{
    static const CHAR Digits[] = "0123456789abcdef";
    ULONG Index = 0;
    LONG Shift;

    if (!Read)
        return "unread";
    Text[Index++] = '0';
    Text[Index++] = 'x';
    for (Shift = 28; Shift >= 0; Shift -= 4)
        Text[Index++] = Digits[(Value >> Shift) & 0xF];
    Text[Index] = 0;
    return Text;
}

#endif
