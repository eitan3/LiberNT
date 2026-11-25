/*
 * PROJECT:     LiberNT host-native tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     A fake hdaudbus DSP child that answers the ADSP bus interface query the way the framework's query handler does, in version 1 or 2 and with the 64-bit DMA capability the case asks for
 * COPYRIGHT:   Copyright 2026 LiberNT contributors
 */

#ifndef FAKE_BUS_H
#define FAKE_BUS_H

#include <adspbus.h>

typedef struct _FAKE_BUS
{
    ADSP_BUS_INTERFACE Interface;
    ADSP_PLATFORM_INFO Info;
    /* what the capability query answers besides the information */
    NTSTATUS InfoStatus;
    UINT32 InfoCalls;
} FAKE_BUS, *PFAKE_BUS;

/* A bus of the given version of the interface; version 2 reports the DMA capability Dma64Bit */
VOID
FakeBusInit(
    _Out_ PFAKE_BUS Bus,
    ULONG Version,
    BOOLEAN Dma64Bit);

/* The query as the framework's handler decides it: the bus's structure and version may not exceed what the caller asks for, and the bus's own size in bytes is what the caller receives */
NTSTATUS
FakeBusQueryInterface(
    _In_ PFAKE_BUS Bus,
    _Out_writes_bytes_(Size) PVOID Interface,
    ULONG Size,
    ULONG Version);

#endif
