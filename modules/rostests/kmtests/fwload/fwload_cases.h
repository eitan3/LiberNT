/*
 * PROJECT:     LiberNT kernel-mode tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Shared cases of the firmware loader tests: a small well-formed ACE image, its malformed variants and the ROM error codes
 * COPYRIGHT:   Copyright 2026 LiberNT contributors
 */

#ifndef FWLOAD_CASES_H
#define FWLOAD_CASES_H

/* Layout of the synthetic image (file offsets): $AE1 header and one module entry, then the payload */
#define FWLOAD_CASE_EXT_MAN_BYTES     0x30
#define FWLOAD_CASE_CPD_OFFSET        0x30
#define FWLOAD_CASE_CPD_ENTRIES       (FWLOAD_CASE_CPD_OFFSET + 20)
#define FWLOAD_CASE_CSS_OFFSET        0x8C
#define FWLOAD_CASE_FW_HEADER_OFFSET  0x2030
#define FWLOAD_CASE_MODULE_OFFSET     (FWLOAD_CASE_FW_HEADER_OFFSET + 52)
#define FWLOAD_CASE_CONFIG_OFFSET     (FWLOAD_CASE_MODULE_OFFSET + 116)
#define FWLOAD_CASE_TEXT_OFFSET       0x8030
#define FWLOAD_CASE_DATA_OFFSET       0x9030
#define FWLOAD_CASE_IMAGE_BYTES       0xA030
#define FWLOAD_CASE_PAYLOAD_OFFSET    FWLOAD_CASE_EXT_MAN_BYTES

static const UINT8 FwloadCaseUuid[16] =
{
    0xF3, 0xE4, 0x79, 0x2B, 0x75, 0x46, 0x49, 0xF6, 0x89, 0xDF, 0x3B, 0xC1, 0x94, 0xA9, 0x1A, 0xEB
};

static
VOID
FwloadCasePut32(
    UINT8* Data,
    UINT32 Value)
{
    Data[0] = (UINT8)Value;
    Data[1] = (UINT8)(Value >> 8);
    Data[2] = (UINT8)(Value >> 16);
    Data[3] = (UINT8)(Value >> 24);
}

static
VOID
FwloadCasePut16(
    UINT8* Data,
    UINT16 Value)
{
    Data[0] = (UINT8)Value;
    Data[1] = (UINT8)(Value >> 8);
}

static
VOID
FwloadCaseCopy(
    UINT8* Destination,
    const UINT8* Source,
    SIZE_T Bytes)
{
    SIZE_T Index;

    for (Index = 0; Index < Bytes; Index++)
        Destination[Index] = Source[Index];
}

/* A well-formed one-module image: the same structure as the real sof-arl-s.ri with one BRNGUP-like module and two pages of code */
static
VOID
FwloadCaseBuildImage(
    _Out_writes_bytes_(FWLOAD_CASE_IMAGE_BYTES) UINT8* Image)
{
    UINT32 Index;
    UINT8* At;

    for (Index = 0; Index < FWLOAD_CASE_IMAGE_BYTES; Index++)
        Image[Index] = 0;
    FwloadCasePut32(Image + 0, SOF_RI_EXT_MAN_MAGIC);
    FwloadCasePut32(Image + 4, FWLOAD_CASE_EXT_MAN_BYTES);
    FwloadCasePut16(Image + 8, 1);
    FwloadCasePut16(Image + 10, 0);
    FwloadCasePut32(Image + 12, 1);
    FwloadCasePut32(Image + 16, 32);
    FwloadCaseCopy(Image + 20, FwloadCaseUuid, 16);
    FwloadCasePut16(Image + 36, 2);
    FwloadCasePut16(Image + 38, 5);
    FwloadCasePut32(Image + 44, 0);

    At = Image + FWLOAD_CASE_CPD_OFFSET;
    FwloadCasePut32(At, SOF_RI_CPD_MAGIC);
    FwloadCasePut32(At + 4, 2);
    At[8] = 2;
    At[9] = 1;
    At[10] = 20;
    FwloadCaseCopy(At + 12, (const UINT8*)"ADSP", 4);
    FwloadCasePut32(At + 16, 0);
    At = Image + FWLOAD_CASE_CPD_ENTRIES;
    FwloadCaseCopy(At, (const UINT8*)"ADSP.man", 8);
    FwloadCasePut32(At + 12, 0x5C);
    FwloadCasePut32(At + 16, 0x4B8);
    FwloadCaseCopy(At + 24, (const UINT8*)"ADSP", 4);
    FwloadCasePut32(At + 36, 0x540);
    FwloadCasePut32(At + 40, FWLOAD_CASE_IMAGE_BYTES - FWLOAD_CASE_PAYLOAD_OFFSET - 0x540);

    At = Image + FWLOAD_CASE_CSS_OFFSET;
    FwloadCasePut32(At, SOF_RI_CSS_MODULE_TYPE);
    FwloadCasePut32(At + 4, 225);
    FwloadCasePut32(At + 8, 0x21000);
    FwloadCasePut32(At + 12, 1);
    FwloadCasePut32(At + 16, 0x8086);
    FwloadCasePut32(At + 20, 0x20260101);
    FwloadCasePut32(At + 24, 1);
    FwloadCasePut32(At + 28, SOF_RI_CSS_HEADER_ID);
    FwloadCasePut16(At + 36, 1);

    At = Image + FWLOAD_CASE_FW_HEADER_OFFSET;
    FwloadCasePut32(At, SOF_RI_FW_HDR_MAGIC);
    FwloadCasePut32(At + 4, 52);
    FwloadCaseCopy(At + 8, (const UINT8*)"ADSPFW", 6);
    FwloadCasePut32(At + 16, 2);
    FwloadCasePut32(At + 20, 2);
    FwloadCasePut32(At + 24, 2);
    FwloadCasePut16(At + 28, 2);
    FwloadCasePut16(At + 30, 15);
    FwloadCasePut16(At + 32, 0);
    FwloadCasePut16(At + 34, 1);
    FwloadCasePut32(At + 36, 1);
    FwloadCasePut32(At + 40, 0x100000);
    FwloadCasePut32(At + 44, 0);
    FwloadCasePut32(At + 48, 0x40000);

    At = Image + FWLOAD_CASE_MODULE_OFFSET;
    FwloadCasePut32(At, SOF_RI_MODULE_MAGIC);
    FwloadCaseCopy(At + 4, (const UINT8*)"BRNGUP", 6);
    FwloadCaseCopy(At + 12, FwloadCaseUuid, 16);
    FwloadCasePut32(At + 28, 0x21);
    FwloadCasePut32(At + 64, 0xA1048000);
    FwloadCasePut16(At + 68, 0);
    FwloadCasePut16(At + 70, 1);
    FwloadCasePut32(At + 72, 3);
    FwloadCasePut16(At + 76, 1);
    FwloadCasePut16(At + 78, 1);
    FwloadCasePut32(At + 80, 0x0001001F);
    FwloadCasePut32(At + 84, 0xA1048000);
    FwloadCasePut32(At + 88, 0x8000);
    FwloadCasePut32(At + 92, 0x00010127);
    FwloadCasePut32(At + 96, 0xA1049000);
    FwloadCasePut32(At + 100, 0x9000);

    At = Image + FWLOAD_CASE_CONFIG_OFFSET;
    FwloadCasePut32(At + 16, 0x1000);
    FwloadCasePut32(At + 20, 0x1000);
    FwloadCasePut32(At + 36, 0x100);

    for (Index = 0; Index < 0x2000; Index++)
        Image[FWLOAD_CASE_TEXT_OFFSET + Index] = (UINT8)(Index * 7 + 3);
}

typedef enum _FWLOAD_CASE_FAULT
{
    FwloadCaseFaultExtMagic = 0,
    FwloadCaseFaultExtLenSmall,
    FwloadCaseFaultExtLenLarge,
    FwloadCaseFaultExtVersion,
    FwloadCaseFaultExtEntryLenZero,
    FwloadCaseFaultExtEntryLenShort,
    FwloadCaseFaultExtEntryLenLarge,
    FwloadCaseFaultExtEntryCount,
    FwloadCaseFaultExtEntryCountLow,
    FwloadCaseFaultCpdHeaderLength,
    FwloadCaseFaultCpdEntriesLarge,
    FwloadCaseFaultFwMagic,
    FwloadCaseFaultFwHeaderLenSmall,
    FwloadCaseFaultFwHeaderLenLarge,
    FwloadCaseFaultNoModules,
    FwloadCaseFaultModuleCountLarge,
    FwloadCaseFaultPreloadPages,
    FwloadCaseFaultModuleMagic,
    FwloadCaseFaultConfigRange,
    FwloadCaseFaultConfigRangeNear,
    FwloadCaseFaultSegmentOffset,
    FwloadCaseFaultCssVendor,
    FwloadCaseFaultCount
} FWLOAD_CASE_FAULT;

static const struct
{
    FWLOAD_CASE_FAULT Fault;
    FWLOAD_STATUS Status;
} FwloadCaseFaults[] =
{
    { FwloadCaseFaultExtMagic, FwloadBadSignature },
    { FwloadCaseFaultExtLenSmall, FwloadBadLength },
    { FwloadCaseFaultExtLenLarge, FwloadBadLength },
    { FwloadCaseFaultExtVersion, FwloadUnsupported },
    { FwloadCaseFaultExtEntryLenZero, FwloadBadLength },
    { FwloadCaseFaultExtEntryLenShort, FwloadBadLength },
    { FwloadCaseFaultExtEntryLenLarge, FwloadBadLength },
    { FwloadCaseFaultExtEntryCount, FwloadBadLength },
    { FwloadCaseFaultExtEntryCountLow, FwloadBadLength },
    { FwloadCaseFaultCpdHeaderLength, FwloadBadLength },
    { FwloadCaseFaultCpdEntriesLarge, FwloadOk },
    { FwloadCaseFaultFwMagic, FwloadBadSignature },
    { FwloadCaseFaultFwHeaderLenSmall, FwloadBadLength },
    { FwloadCaseFaultFwHeaderLenLarge, FwloadBadLength },
    { FwloadCaseFaultNoModules, FwloadBadLength },
    { FwloadCaseFaultModuleCountLarge, FwloadBadLength },
    { FwloadCaseFaultPreloadPages, FwloadTruncated },
    { FwloadCaseFaultModuleMagic, FwloadBadSignature },
    { FwloadCaseFaultConfigRange, FwloadBadLength },
    { FwloadCaseFaultConfigRangeNear, FwloadBadLength },
    { FwloadCaseFaultSegmentOffset, FwloadTruncated },
    { FwloadCaseFaultCssVendor, FwloadOk }
};

static
VOID
FwloadCaseApplyFault(
    UINT8* Image,
    FWLOAD_CASE_FAULT Fault)
{
    switch (Fault)
    {
        case FwloadCaseFaultExtMagic: FwloadCasePut32(Image + 0, 0x31454125); break;
        case FwloadCaseFaultExtLenSmall: FwloadCasePut32(Image + 4, 8); break;
        case FwloadCaseFaultExtLenLarge: FwloadCasePut32(Image + 4, FWLOAD_CASE_IMAGE_BYTES + 1); break;
        case FwloadCaseFaultExtVersion: FwloadCasePut16(Image + 8, 2); break;
        case FwloadCaseFaultExtEntryLenZero: FwloadCasePut32(Image + 16, 0); break;
        case FwloadCaseFaultExtEntryLenShort:
            FwloadCasePut32(Image + 16, 16);
            FwloadCasePut32(Image + 4, 0x20);
            break;
        case FwloadCaseFaultExtEntryLenLarge: FwloadCasePut32(Image + 16, 0x100); break;
        case FwloadCaseFaultExtEntryCount: FwloadCasePut32(Image + 12, 2); break;
        case FwloadCaseFaultExtEntryCountLow: FwloadCasePut32(Image + 12, 0); break;
        case FwloadCaseFaultCpdHeaderLength: Image[FWLOAD_CASE_CPD_OFFSET + 10] = 0; break;
        case FwloadCaseFaultCpdEntriesLarge: FwloadCasePut32(Image + FWLOAD_CASE_CPD_OFFSET + 4, 0xFFFFFFFF); break;
        case FwloadCaseFaultFwMagic: FwloadCasePut32(Image + FWLOAD_CASE_FW_HEADER_OFFSET, 0x314D4125); break;
        case FwloadCaseFaultFwHeaderLenSmall: FwloadCasePut32(Image + FWLOAD_CASE_FW_HEADER_OFFSET + 4, 40); break;
        case FwloadCaseFaultFwHeaderLenLarge: FwloadCasePut32(Image + FWLOAD_CASE_FW_HEADER_OFFSET + 4, 0x10000); break;
        case FwloadCaseFaultNoModules: FwloadCasePut32(Image + FWLOAD_CASE_FW_HEADER_OFFSET + 36, 0); break;
        case FwloadCaseFaultModuleCountLarge: FwloadCasePut32(Image + FWLOAD_CASE_FW_HEADER_OFFSET + 36, 1000); break;
        case FwloadCaseFaultPreloadPages: FwloadCasePut32(Image + FWLOAD_CASE_FW_HEADER_OFFSET + 16, 100); break;
        case FwloadCaseFaultModuleMagic: FwloadCasePut32(Image + FWLOAD_CASE_MODULE_OFFSET, 0x454D4125); break;
        case FwloadCaseFaultConfigRange: FwloadCasePut16(Image + FWLOAD_CASE_MODULE_OFFSET + 68, 0xFFFF); break;
        case FwloadCaseFaultConfigRangeNear: FwloadCasePut16(Image + FWLOAD_CASE_MODULE_OFFSET + 68, 800); break;
        case FwloadCaseFaultSegmentOffset: FwloadCasePut32(Image + FWLOAD_CASE_MODULE_OFFSET + 88, 0xA000); break;
        case FwloadCaseFaultCssVendor: FwloadCasePut32(Image + FWLOAD_CASE_CSS_OFFSET + 16, 0x8087); break;
        default: break;
    }
}

/* Every ROM error code of Linux hda.h with the name the loader reports */
static const struct
{
    UINT32 Code;
    const char* Name;
} FwloadCaseRomErrors[] =
{
    { FWLOAD_ROM_CSE_ERROR, "CSE error" },
    { FWLOAD_ROM_CSE_WRONG_RESPONSE, "CSE wrong response" },
    { FWLOAD_ROM_IMR_TO_SMALL, "IMR too small" },
    { FWLOAD_ROM_BASE_FW_NOT_FOUND, "base firmware not found" },
    { FWLOAD_ROM_CSE_VALIDATION_FAILED, "CSE validation failed" },
    { FWLOAD_ROM_IPC_FATAL_ERROR, "IPC fatal error" },
    { FWLOAD_ROM_L2_CACHE_ERROR, "L2 cache error" },
    { FWLOAD_ROM_LOAD_OFFSET_TO_SMALL, "load offset too small" },
    { FWLOAD_ROM_API_PTR_INVALID, "API pointer invalid" },
    { FWLOAD_ROM_BASEFW_INCOMPAT, "base firmware incompatible" },
    { FWLOAD_ROM_UNHANDLED_INTERRUPT, "unhandled interrupt" },
    { FWLOAD_ROM_MEMORY_HOLE_ECC, "memory hole ECC" },
    { FWLOAD_ROM_KERNEL_EXCEPTION, "kernel exception" },
    { FWLOAD_ROM_USER_EXCEPTION, "user exception" },
    { FWLOAD_ROM_UNEXPECTED_RESET, "unexpected reset" },
    { FWLOAD_ROM_NULL_FW_ENTRY, "null firmware entry" }
};

#endif
