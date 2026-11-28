/*
 * PROJECT:     LiberNT Intel BE200 Native 802.11 Miniport
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Firmware image and PNVM parser definitions
 * COPYRIGHT:   Copyright 2026 The LiberNT contributors
 */

#pragma once

typedef struct _IWL_SECTION
{
    ULONG Offset;
    const UCHAR *Data;
    ULONG Length;
} IWL_SECTION, *PIWL_SECTION;

typedef struct _IWL_FIRMWARE
{
    PUCHAR File;
    ULONG FileLength;
    IWL_SECTION Sections[IWL_FW_MAX_SECTIONS * 3 + 2];
    ULONG SectionCount;
    const UCHAR *Iml;
    ULONG ImlLength;
    const UCHAR *Pnvm;
    ULONG PnvmLength;
    ULONG PhyConfig;
    ULONG CalibFlow;
    ULONG CalibEvent;
    ULONG Capabilities[4];
    ULONG Version;
} IWL_FIRMWARE, *PIWL_FIRMWARE;

NTSTATUS
IwlParseFirmware(
    _Inout_ PIWL_FIRMWARE Fw);

ULONG
IwlParsePnvm(
    _In_reads_bytes_(Length) const UCHAR *Data,
    _In_ ULONG Length,
    _In_reads_(3) const ULONG *SkuId,
    _In_ ULONG MacType,
    _In_ ULONG RfType,
    _Out_writes_(IWL_PNVM_MAX_CHUNKS) PIWL_SECTION Chunks);
