/*
 * PROJECT:     LiberNT audio driver libraries
 * LICENSE:     GPL-3.0-or-later for this file (https://spdx.org/licenses/GPL-3.0-or-later); the definitions ported from Sound Open Firmware below are BSD-3-Clause, Intel's notice kept below (LICENSING.md §2)
 * PURPOSE:     Reads the Sound Open Firmware container (.ri) of an ACE platform: the $AE1 extended manifest, the partition directory, the signature header and the $AM1 firmware header with its modules
 * COPYRIGHT:   Copyright 2026 LiberNT contributors
 */

/*
 * The container layout read here is ported from the Sound Open Firmware project
 * (https://github.com/thesofproject/sof) at fcebbbf22d7c08159fbb8486d5f3687da4f167c3:
 * tools/rimage/src/include/rimage/cavs/cavs_ext_manifest.h, tools/rimage/src/include/rimage/sof/user/manifest.h,
 * tools/rimage/src/include/rimage/cse.h and tools/rimage/src/include/rimage/css.h. The host-side reading
 * order (payload offset = extended manifest length, firmware header at payload + 0x2000, configuration
 * entries after the module table) follows Linux v6.12 sound/soc/sof/ipc4-loader.c
 * (sof_ipc4_fw_parse_ext_man), studied under LICENSING.md rule 7. Every definition below carries its
 * own comment; nothing of the studied files is copied.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2016, 2017, 2021 Intel Corporation. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 * this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 * this list of conditions and the following disclaimer in the documentation
 * and/or other materials provided with the distribution.
 *
 * 3. Neither the name of the copyright holder nor the names of its
 * contributors may be used to endorse or promote products derived from this
 * software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include <ntddk.h>
#include "fwload.h"

/* sof_man_segment_flags.r */
#define SOF_RI_SEG_CONTENTS    0
#define SOF_RI_SEG_ALLOC       1
#define SOF_RI_SEG_LOAD        2
#define SOF_RI_SEG_READONLY    3
#define SOF_RI_SEG_CODE        4
#define SOF_RI_SEG_DATA        5
#define SOF_RI_SEG_TYPE_SHIFT  8
#define SOF_RI_SEG_TYPE_MASK   0xF
#define SOF_RI_SEG_PAGES_SHIFT 16
#define SOF_RI_SEG_PAGES_MASK  0xFFFF

/* sof_man_module_type.r */
#define SOF_RI_MOD_LOAD_TYPE_MASK   0xF
#define SOF_RI_MOD_AUTO_START       0x10
#define SOF_RI_MOD_DOMAIN_LL        0x20
#define SOF_RI_MOD_DOMAIN_DP        0x40
#define SOF_RI_MOD_LIB_CODE         0x80

/* fw_ext_man_cavs_header: id, len, version_major, version_minor, num_module_entries */
#define SOF_RI_EXT_MAN_LEN          4
#define SOF_RI_EXT_MAN_VER_MAJOR    8
#define SOF_RI_EXT_MAN_VER_MINOR    10
#define SOF_RI_EXT_MAN_MODULE_COUNT 12
#define SOF_RI_EXT_MAN_VERSION_SUPPORTED 1

/* CsePartitionDirHeader_v2_5 and CsePartitionDirEntry */
#define SOF_RI_CPD_ENTRIES          4
#define SOF_RI_CPD_HEADER_VERSION   8
#define SOF_RI_CPD_HEADER_LENGTH    10
#define SOF_RI_CPD_NAME             12
#define SOF_RI_CPD_ENTRY_NAME       0
#define SOF_RI_CPD_ENTRY_OFFSET     12
#define SOF_RI_CPD_ENTRY_LENGTH     16

/* css_header_v2_5, as far as the host reads it */
#define SOF_RI_CSS_HEADER_LEN       4
#define SOF_RI_CSS_VENDOR           16
#define SOF_RI_CSS_DATE             20
#define SOF_RI_CSS_ID               28
#define SOF_RI_CSS_VERSION          36
#define SOF_RI_CSS_SVN              44
#define SOF_RI_CSS_BYTES_READ       48

/* sof_man_fw_header */
#define SOF_RI_FW_HDR_LEN           4
#define SOF_RI_FW_HDR_NAME          8
#define SOF_RI_FW_HDR_PRELOAD       16
#define SOF_RI_FW_HDR_FLAGS         20
#define SOF_RI_FW_HDR_FEATURES      24
#define SOF_RI_FW_HDR_MAJOR         28
#define SOF_RI_FW_HDR_MINOR         30
#define SOF_RI_FW_HDR_HOTFIX        32
#define SOF_RI_FW_HDR_BUILD         34
#define SOF_RI_FW_HDR_MODULE_COUNT  36
#define SOF_RI_FW_HDR_HW_BUF_BASE   40
#define SOF_RI_FW_HDR_HW_BUF_LENGTH 44
#define SOF_RI_FW_HDR_LOAD_OFFSET   48

/* sof_man_module */
#define SOF_RI_MOD_ID           0
#define SOF_RI_MOD_NAME         4
#define SOF_RI_MOD_UUID         12
#define SOF_RI_MOD_TYPE         28
#define SOF_RI_MOD_ENTRY        64
#define SOF_RI_MOD_CFG_OFFSET   68
#define SOF_RI_MOD_CFG_COUNT    70
#define SOF_RI_MOD_AFFINITY     72
#define SOF_RI_MOD_INST_MAX     76
#define SOF_RI_MOD_INST_BSS     78
#define SOF_RI_MOD_SEGMENT      80
#define SOF_RI_MOD_SEGMENT_BYTES 12
#define SOF_RI_SEG_FLAGS        0
#define SOF_RI_SEG_VBASE        4
#define SOF_RI_SEG_FILE_OFFSET  8

/* sof_man_mod_config */
#define SOF_RI_CFG_PAR          0
#define SOF_RI_CFG_IS_BYTES     16
#define SOF_RI_CFG_CPS          20
#define SOF_RI_CFG_IBS          24
#define SOF_RI_CFG_OBS          28
#define SOF_RI_CFG_FLAGS        32
#define SOF_RI_CFG_CPC          36
#define SOF_RI_CFG_OBLS         40

static
UINT32
SofRiRead32(
    const UINT8* Data,
    SIZE_T Offset)
{
    return (UINT32)Data[Offset] | ((UINT32)Data[Offset + 1] << 8) |
        ((UINT32)Data[Offset + 2] << 16) | ((UINT32)Data[Offset + 3] << 24);
}

static
UINT16
SofRiRead16(
    const UINT8* Data,
    SIZE_T Offset)
{
    return (UINT16)((UINT16)Data[Offset] | ((UINT16)Data[Offset + 1] << 8));
}

static
VOID
SofRiCopyText(
    _Out_writes_bytes_(Capacity) CHAR* Destination,
    SIZE_T Capacity,
    const UINT8* Source,
    SIZE_T Length)
{
    SIZE_T Index;

    for (Index = 0; Index < Length && Index + 1 < Capacity && Source[Index] != 0; Index++)
        Destination[Index] = (CHAR)Source[Index];
    Destination[Index] = 0;
}

static
BOOLEAN
SofRiNameIs(
    const UINT8* Name,
    SIZE_T Length,
    const char* Expected)
{
    SIZE_T Index;

    for (Index = 0; Index < Length; Index++)
    {
        if (Name[Index] != (UINT8)Expected[Index])
            return FALSE;
        if (Expected[Index] == 0)
            return TRUE;
    }
    return Expected[Index] == 0;
}

/* CsePartitionDirHeader_v2_5 at the payload start; the ROM validates it, the host only records it */
static
FWLOAD_STATUS
SofRiParsePartitionDirectory(
    const UINT8* Data,
    SIZE_T Size,
    SIZE_T Payload,
    _Out_ PSOF_RI_IMAGE Image)
{
    SIZE_T Header, Entry, End;
    UINT32 Count, Index, HeaderLength;

    Header = Payload;
    Count = SofRiRead32(Data, Header + SOF_RI_CPD_ENTRIES);
    HeaderLength = Data[Header + SOF_RI_CPD_HEADER_LENGTH];
    Image->PartitionEntryCount = Count;
    SofRiCopyText(Image->PartitionDirectoryName, sizeof(Image->PartitionDirectoryName),
        Data + Header + SOF_RI_CPD_NAME, 4);
    if (HeaderLength < SOF_RI_CPD_HEADER_BYTES_MIN || HeaderLength > Image->PayloadBytes)
    {
        Image->BadOffset = Header + SOF_RI_CPD_HEADER_LENGTH;
        return FwloadBadLength;
    }

    End = Header + HeaderLength;
    for (Index = 0; Index < Count && Index < SOF_RI_MAX_PARTITIONS; Index++)
    {
        Entry = End + (SIZE_T)Index * SOF_RI_CPD_ENTRY_BYTES;
        if (Entry + SOF_RI_CPD_ENTRY_BYTES > Size)
        {
            Image->BadOffset = Entry;
            return FwloadTruncated;
        }
        SofRiCopyText(Image->Partition[Index].Name, sizeof(Image->Partition[Index].Name),
            Data + Entry + SOF_RI_CPD_ENTRY_NAME, 12);
        Image->Partition[Index].Offset = SofRiRead32(Data, Entry + SOF_RI_CPD_ENTRY_OFFSET);
        Image->Partition[Index].Length = SofRiRead32(Data, Entry + SOF_RI_CPD_ENTRY_LENGTH);
        Image->PartitionCount++;
    }

    /* css_header_v2_5 at the start of the .man partition */
    for (Index = 0; Index < Image->PartitionCount; Index++)
    {
        const SOF_RI_PARTITION* Part = &Image->Partition[Index];

        if (!SofRiNameIs((const UINT8*)Part->Name, 12, "ADSP.man"))
            continue;
        Entry = Header + Part->Offset;
        if (Part->Offset < HeaderLength || Entry + SOF_RI_CSS_BYTES_READ > Size)
            continue;
        if (SofRiRead32(Data, Entry) != SOF_RI_CSS_MODULE_TYPE ||
            SofRiRead32(Data, Entry + SOF_RI_CSS_ID) != SOF_RI_CSS_HEADER_ID)
            continue;
        Image->HasSignature = TRUE;
        Image->SignatureVendor = SofRiRead32(Data, Entry + SOF_RI_CSS_VENDOR);
        Image->SignatureDate = SofRiRead32(Data, Entry + SOF_RI_CSS_DATE);
        Image->SignatureMajor = SofRiRead16(Data, Entry + SOF_RI_CSS_VERSION);
        Image->SignatureMinor = SofRiRead16(Data, Entry + SOF_RI_CSS_VERSION + 2);
        Image->SignatureHotfix = SofRiRead16(Data, Entry + SOF_RI_CSS_VERSION + 4);
        Image->SignatureBuild = SofRiRead16(Data, Entry + SOF_RI_CSS_VERSION + 6);
        Image->SignatureSvn = SofRiRead32(Data, Entry + SOF_RI_CSS_SVN);
    }
    return FwloadOk;
}

/* sof_man_module with its configuration at index cfg_offset of the sof_man_mod_config table that follows the module array */
static
FWLOAD_STATUS
SofRiParseModules(
    const UINT8* Data,
    SIZE_T Size,
    SIZE_T Table,
    _Out_writes_opt_(ModuleCapacity) PSOF_RI_MODULE Modules,
    UINT32 ModuleCapacity,
    _Inout_ PSOF_RI_IMAGE Image)
{
    UINT32 Index, Segment;
    SIZE_T Configs, Config, End, Remaining, Used = 0;

    End = Table + (SIZE_T)Image->ModuleCount * SOF_RI_MODULE_BYTES;
    if (End > Size)
    {
        Image->BadOffset = Table;
        return FwloadTruncated;
    }
    Configs = End;
    Remaining = Size - End;
    Image->ConfigOffset = (UINT32)Configs;
    if (ModuleCapacity < Image->ModuleCount)
    {
        Image->BadOffset = Table;
        return FwloadTooManyModules;
    }

    for (Index = 0; Index < Image->ModuleCount; Index++)
    {
        SIZE_T Entry = Table + (SIZE_T)Index * SOF_RI_MODULE_BYTES;
        PSOF_RI_MODULE Module = &Modules[Index];

        if (SofRiRead32(Data, Entry + SOF_RI_MOD_ID) != SOF_RI_MODULE_MAGIC)
        {
            Image->BadOffset = Entry;
            return FwloadBadSignature;
        }
        SofRiCopyText(Module->Name, sizeof(Module->Name), Data + Entry + SOF_RI_MOD_NAME, 8);
        for (Segment = 0; Segment < 16; Segment++)
            Module->Uuid[Segment] = Data[Entry + SOF_RI_MOD_UUID + Segment];
        Module->Type = SofRiRead32(Data, Entry + SOF_RI_MOD_TYPE);
        Module->LoadType = Module->Type & SOF_RI_MOD_LOAD_TYPE_MASK;
        Module->EntryPoint = SofRiRead32(Data, Entry + SOF_RI_MOD_ENTRY);
        Module->CfgOffset = SofRiRead16(Data, Entry + SOF_RI_MOD_CFG_OFFSET);
        Module->CfgCount = SofRiRead16(Data, Entry + SOF_RI_MOD_CFG_COUNT);
        Module->AffinityMask = SofRiRead32(Data, Entry + SOF_RI_MOD_AFFINITY);
        Module->InstanceMaxCount = SofRiRead16(Data, Entry + SOF_RI_MOD_INST_MAX);
        Module->InstanceBssSize = SofRiRead16(Data, Entry + SOF_RI_MOD_INST_BSS);

        for (Segment = 0; Segment < 3; Segment++)
        {
            SIZE_T At = Entry + SOF_RI_MOD_SEGMENT + (SIZE_T)Segment * SOF_RI_MOD_SEGMENT_BYTES;
            PSOF_RI_SEGMENT Info = &Module->Segment[Segment];
            UINT32 Flags = SofRiRead32(Data, At + SOF_RI_SEG_FLAGS);

            Info->Flags = Flags;
            Info->VBaseAddress = SofRiRead32(Data, At + SOF_RI_SEG_VBASE);
            Info->FileOffset = SofRiRead32(Data, At + SOF_RI_SEG_FILE_OFFSET);
            Info->Pages = (Flags >> SOF_RI_SEG_PAGES_SHIFT) & SOF_RI_SEG_PAGES_MASK;
            Info->Type = (Flags >> SOF_RI_SEG_TYPE_SHIFT) & SOF_RI_SEG_TYPE_MASK;
            Info->Load = (Flags & (1 << SOF_RI_SEG_LOAD)) ? TRUE : FALSE;
            Info->Alloc = (Flags & (1 << SOF_RI_SEG_ALLOC)) ? TRUE : FALSE;
            if (Info->Pages == 0 || !Info->Load)
                continue;
            if ((SIZE_T)Info->FileOffset + (SIZE_T)Info->Pages * SOF_RI_PAGE_BYTES > Image->PayloadBytes)
            {
                Image->BadOffset = At;
                return FwloadTruncated;
            }
            if ((SIZE_T)Info->FileOffset + (SIZE_T)Info->Pages * SOF_RI_PAGE_BYTES > Used)
                Used = (SIZE_T)Info->FileOffset + (SIZE_T)Info->Pages * SOF_RI_PAGE_BYTES;
        }

        /* the configuration entries of a module start at CfgOffset in the table after the module array */
        if (Module->CfgCount == 0)
            continue;
        if ((SIZE_T)(Module->CfgOffset + Module->CfgCount) * SOF_RI_MODULE_CONFIG_BYTES > Remaining)
        {
            Image->BadOffset = Entry + SOF_RI_MOD_CFG_OFFSET;
            return FwloadBadLength;
        }
        Config = Configs + (SIZE_T)Module->CfgOffset * SOF_RI_MODULE_CONFIG_BYTES;
        for (Segment = 0; Segment < 4; Segment++)
            Module->Par[Segment] = SofRiRead32(Data, Config + SOF_RI_CFG_PAR + 4 * Segment);
        Module->IsBytes = SofRiRead32(Data, Config + SOF_RI_CFG_IS_BYTES);
        Module->Cps = SofRiRead32(Data, Config + SOF_RI_CFG_CPS);
        Module->Ibs = SofRiRead32(Data, Config + SOF_RI_CFG_IBS);
        Module->Obs = SofRiRead32(Data, Config + SOF_RI_CFG_OBS);
        Module->ModuleFlags = SofRiRead32(Data, Config + SOF_RI_CFG_FLAGS);
        Module->Cpc = SofRiRead32(Data, Config + SOF_RI_CFG_CPC);
        Module->Obls = SofRiRead32(Data, Config + SOF_RI_CFG_OBLS);
        Module->HasConfig = TRUE;
    }
    Image->UsedBytes = Used;
    Image->Modules = Modules;
    Image->ModuleCapacity = ModuleCapacity;
    return FwloadOk;
}

FWLOAD_STATUS
SofRiParse(
    const UINT8* Data,
    SIZE_T Size,
    PSOF_RI_MODULE Modules,
    UINT32 ModuleCapacity,
    PSOF_RI_IMAGE Image)
{
    static const SOF_RI_IMAGE Empty;
    FWLOAD_STATUS Status;
    SIZE_T Offset, Header, End;
    UINT32 Length, Count, HeaderLength;

    *Image = Empty;
    if (Data == NULL || Size == 0)
        return FwloadNoData;
    if (Size < SOF_RI_EXT_MAN_HEADER_BYTES)
    {
        Image->BadOffset = 0;
        return FwloadTruncated;
    }
    if (SofRiRead32(Data, 0) != SOF_RI_EXT_MAN_MAGIC)
    {
        Image->BadOffset = 0;
        return FwloadBadSignature;
    }
    Length = SofRiRead32(Data, SOF_RI_EXT_MAN_LEN);
    Image->ExtManVersionMajor = SofRiRead16(Data, SOF_RI_EXT_MAN_VER_MAJOR);
    Image->ExtManVersionMinor = SofRiRead16(Data, SOF_RI_EXT_MAN_VER_MINOR);
    Image->ExtManModuleCount = SofRiRead32(Data, SOF_RI_EXT_MAN_MODULE_COUNT);
    if (Length < SOF_RI_EXT_MAN_HEADER_BYTES || Length > Size)
    {
        Image->BadOffset = SOF_RI_EXT_MAN_LEN;
        return FwloadBadLength;
    }
    if (Image->ExtManVersionMajor != SOF_RI_EXT_MAN_VERSION_SUPPORTED)
    {
        Image->BadOffset = SOF_RI_EXT_MAN_VER_MAJOR;
        return FwloadUnsupported;
    }

    /* fw_ext_mod_config_header.ext_module_config_length is the length of the whole entry */
    Offset = SOF_RI_EXT_MAN_HEADER_BYTES;
    Count = 0;
    while (Offset < Length)
    {
        UINT32 Entry;

        if (Offset + 4 > Length)
        {
            Image->BadOffset = Offset;
            return FwloadTruncated;
        }
        Entry = SofRiRead32(Data, Offset);
        if (Entry < SOF_RI_EXT_MODULE_HEADER_BYTES || Entry > Length - Offset)
        {
            Image->BadOffset = Offset;
            return FwloadBadLength;
        }
        Offset += Entry;
        Count++;
    }
    if (Count != Image->ExtManModuleCount)
    {
        Image->BadOffset = SOF_RI_EXT_MAN_MODULE_COUNT;
        return FwloadBadLength;
    }

    Image->PayloadOffset = Length;
    Image->PayloadBytes = Size - Length;
    Image->Data = Data;
    Image->Size = Size;
    Image->Modules = NULL;
    Image->ModuleCapacity = 0;

    if (Image->PayloadBytes >= SOF_RI_CPD_HEADER_BYTES_MIN &&
        SofRiRead32(Data, Length) == SOF_RI_CPD_MAGIC)
    {
        Image->HasPartitionDirectory = TRUE;
        Status = SofRiParsePartitionDirectory(Data, Size, Length, Image);
        if (Status != FwloadOk)
            return Status;
    }

    Header = Length + SOF_RI_FW_HDR_OFFSET;
    Image->HeaderOffset = (UINT32)Header;
    if (Header + SOF_RI_FW_HDR_BYTES > Size)
    {
        Image->BadOffset = Header;
        return FwloadTruncated;
    }
    if (SofRiRead32(Data, Header) != SOF_RI_FW_HDR_MAGIC)
    {
        Image->BadOffset = Header;
        return FwloadBadSignature;
    }
    HeaderLength = SofRiRead32(Data, Header + SOF_RI_FW_HDR_LEN);
    if (HeaderLength < SOF_RI_FW_HDR_BYTES)
    {
        Image->BadOffset = Header + SOF_RI_FW_HDR_LEN;
        return FwloadBadLength;
    }
    SofRiCopyText(Image->Name, sizeof(Image->Name), Data + Header + SOF_RI_FW_HDR_NAME, 8);
    Image->PreloadPageCount = SofRiRead32(Data, Header + SOF_RI_FW_HDR_PRELOAD);
    Image->ImageFlags = SofRiRead32(Data, Header + SOF_RI_FW_HDR_FLAGS);
    Image->FeatureMask = SofRiRead32(Data, Header + SOF_RI_FW_HDR_FEATURES);
    Image->Major = SofRiRead16(Data, Header + SOF_RI_FW_HDR_MAJOR);
    Image->Minor = SofRiRead16(Data, Header + SOF_RI_FW_HDR_MINOR);
    Image->Hotfix = SofRiRead16(Data, Header + SOF_RI_FW_HDR_HOTFIX);
    Image->Build = SofRiRead16(Data, Header + SOF_RI_FW_HDR_BUILD);
    Image->ModuleCount = SofRiRead32(Data, Header + SOF_RI_FW_HDR_MODULE_COUNT);
    Image->HwBufferBase = SofRiRead32(Data, Header + SOF_RI_FW_HDR_HW_BUF_BASE);
    Image->HwBufferLength = SofRiRead32(Data, Header + SOF_RI_FW_HDR_HW_BUF_LENGTH);
    Image->LoadOffset = SofRiRead32(Data, Header + SOF_RI_FW_HDR_LOAD_OFFSET);
    if (Image->ModuleCount == 0 ||
        Header + HeaderLength > Size ||
        (SIZE_T)Image->ModuleCount > (Size - Header - HeaderLength) / SOF_RI_MODULE_BYTES)
    {
        Image->BadOffset = Header + SOF_RI_FW_HDR_MODULE_COUNT;
        return FwloadBadLength;
    }
    End = (SIZE_T)Image->PreloadPageCount * SOF_RI_PAGE_BYTES;
    if (End > Image->PayloadBytes)
    {
        Image->BadOffset = Header + SOF_RI_FW_HDR_PRELOAD;
        return FwloadTruncated;
    }
    return SofRiParseModules(Data, Size, Header + HeaderLength, Modules, ModuleCapacity, Image);
}
