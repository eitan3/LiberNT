/*
 * PROJECT:     LiberNT kernel-mode tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Synthetic raw SMBIOS tables for the audio path policy, shared by the kmtest and the host test
 * COPYRIGHT:   Copyright 2026 LiberNT contributors
 */

#ifndef _DMI_CASES_H_
#define _DMI_CASES_H_

#include <policy.h>
#include "dsp_cases.h"

/* MSSmBios_RawSMBiosTables: Used20CallingMethod, SMBIOS 3.3.2, Size, then the structure table of Size bytes */
#define DMI_RAW_HEADER(Size) 0x00, 0x03, 0x03, 0x02, DSP_U32(Size)

/* Structure header: type, formatted length, handle; the end-of-table marker is a header of 4 bytes and an empty string set */
#define DMI_STRUCTURE(Type, Length, Handle) (Type), (Length), DSP_U16(Handle)
#define DMI_END_OF_TABLE 0x7F, 0x04, 0x00, 0x00, 0x00, 0x00

/* System Information string set: 1 VENDORX, 2 PRODUCT 1, 3 VERSION, 4 SERIAL0, 5 SKU0, 6 FAMILY 1 (49 bytes) */
#define DMI_SYSTEM_STRINGS \
    'V', 'E', 'N', 'D', 'O', 'R', 'X', 0, \
    'P', 'R', 'O', 'D', 'U', 'C', 'T', ' ', '1', 0, \
    'V', 'E', 'R', 'S', 'I', 'O', 'N', 0, \
    'S', 'E', 'R', 'I', 'A', 'L', '0', 0, \
    'S', 'K', 'U', '0', 0, \
    'F', 'A', 'M', 'I', 'L', 'Y', ' ', '1', 0, \
    0

/* BIOS Information, 20 formatted bytes, then the strings 1 VENDORX, 2 BIOSV1, 3 01/01/2020: 47 bytes */
#define DMI_BIOS_STRUCTURE \
    DMI_STRUCTURE(0, 0x14, 0), \
    0x01, 0x02, 0x00, 0xE0, 0x03, 0x00, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, \
    'V', 'E', 'N', 'D', 'O', 'R', 'X', 0, \
    'B', 'I', 'O', 'S', 'V', '1', 0, \
    '0', '1', '/', '0', '1', '/', '2', '0', '2', '0', 0, \
    0

/* System Information, 27 formatted bytes: manufacturer 1, product 2, version 3, serial 4, SKU 5, family 6: 76 bytes */
#define DMI_SYSTEM_STRUCTURE(Sku) \
    DMI_STRUCTURE(1, 0x1B, 1), \
    0x01, 0x02, 0x03, 0x04, \
    0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88, 0x99, 0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF, 0x01, \
    0x06, (Sku), 0x06, \
    DMI_SYSTEM_STRINGS

/* Base Board, 9 formatted bytes: manufacturer 1, product 2, version 3, serial 4: 43 bytes */
#define DMI_BASEBOARD_STRUCTURE \
    DMI_STRUCTURE(2, 0x09, 2), \
    0x01, 0x02, 0x03, 0x04, 0x00, \
    'V', 'E', 'N', 'D', 'O', 'R', 'X', 0, \
    'B', 'O', 'A', 'R', 'D', ' ', '1', 0, \
    'B', 'O', 'A', 'R', 'D', 'V', '1', 0, \
    'B', 'S', 'E', 'R', 'I', 'A', 'L', '0', 0, \
    0

/* A real machine's table, structure reproduced with fictional strings: 47 + 76 + 43 + 6 = 172 bytes of structure table */
static const unsigned char DmiTestTable[] =
{
    DMI_RAW_HEADER(172),
    DMI_BIOS_STRUCTURE,
    DMI_SYSTEM_STRUCTURE(5),
    DMI_BASEBOARD_STRUCTURE,
    DMI_END_OF_TABLE
};

/* The system structure with every index unset and an empty string set, then the marker: 35 bytes */
static const unsigned char DmiUnsetTable[] =
{
    DMI_RAW_HEADER(35),
    DMI_STRUCTURE(1, 0x1B, 1),
    0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x06, 0x00, 0x00,
    0x00, 0x00,
    DMI_END_OF_TABLE
};

/* The test table with the SKU index naming a ninth string */
static const unsigned char DmiIndexPastSetTable[] =
{
    DMI_RAW_HEADER(172),
    DMI_BIOS_STRUCTURE,
    DMI_SYSTEM_STRUCTURE(9),
    DMI_BASEBOARD_STRUCTURE,
    DMI_END_OF_TABLE
};

/* A structure whose formatted length is below its own header */
static const unsigned char DmiBadLengthTable[] =
{
    DMI_RAW_HEADER(10),
    0x01, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

/* The system structure with one manufacturer string of 80 bytes: 27 + 82 + 6 = 115 bytes */
#define DMI_X16 'X', 'X', 'X', 'X', 'X', 'X', 'X', 'X', 'X', 'X', 'X', 'X', 'X', 'X', 'X', 'X'
static const unsigned char DmiLongVendorTable[] =
{
    DMI_RAW_HEADER(115),
    DMI_STRUCTURE(1, 0x1B, 1),
    0x01, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x06, 0x00, 0x00,
    DMI_X16, DMI_X16, DMI_X16, DMI_X16, DMI_X16, 0,
    0,
    DMI_END_OF_TABLE
};

/* The system structure whose last string runs into the end of the table without a NUL: 27 + 8 + 9 = 44 bytes */
static const unsigned char DmiUnterminatedTable[] =
{
    DMI_RAW_HEADER(44),
    DMI_STRUCTURE(1, 0x1B, 1),
    0x01, 0x02, 0x03, 0x04,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x06, 0x00, 0x00,
    'V', 'E', 'N', 'D', 'O', 'R', 'X', 0,
    'P', 'R', 'O', 'D', 'U', 'C', 'T', ' ', '1'
};

/* The same bytes with a NUL closing the last string at the very end of the table, which ends the set as well: 45 bytes */
static const unsigned char DmiSingleNulTable[] =
{
    DMI_RAW_HEADER(45),
    DMI_STRUCTURE(1, 0x1B, 1),
    0x01, 0x02, 0x03, 0x04,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x06, 0x00, 0x00,
    'V', 'E', 'N', 'D', 'O', 'R', 'X', 0,
    'P', 'R', 'O', 'D', 'U', 'C', 'T', ' ', '1', 0
};

/* The same structure with the Size field covering it exactly, so the stray byte after it is not part of the table */
static const unsigned char DmiDeclaredShortTable[] =
{
    DMI_RAW_HEADER(36),
    DMI_STRUCTURE(1, 0x1B, 1),
    0x01, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x06, 0x00, 0x00,
    'V', 'E', 'N', 'D', 'O', 'R', 'X', 0,
    0,
    0x7F
};

/* The system structure followed by a stray byte instead of the next structure */
static const unsigned char DmiStrayByteTable[] =
{
    DMI_RAW_HEADER(37),
    DMI_STRUCTURE(1, 0x1B, 1),
    0x01, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x06, 0x00, 0x00,
    'V', 'E', 'N', 'D', 'O', 'R', 'X', 0,
    0,
    0x7F
};

typedef struct _DMI_FIELD_CASE
{
    unsigned int Field;
    const char* Expected;
} DMI_FIELD_CASE;

static const DMI_FIELD_CASE DmiTestFields[] =
{
    { DSP_DMI_SYS_VENDOR, "VENDORX" },
    { DSP_DMI_SYS_PRODUCT, "PRODUCT 1" },
    { DSP_DMI_SYS_VERSION, "VERSION" },
    { DSP_DMI_SYS_SKU, "SKU0" },
    { DSP_DMI_BOARD_VENDOR, "VENDORX" },
    { DSP_DMI_BOARD_NAME, "BOARD 1" },
    { DSP_DMI_BOARD_VERSION, "BOARDV1" },
    { DSP_DMI_BIOS_VERSION, "BIOSV1" }
};

typedef struct _DMI_TABLE_CASE
{
    const char* Name;
    const unsigned char* Raw;
    unsigned int Size;
    int Status;
    unsigned int Present;
    unsigned int Truncated;
} DMI_TABLE_CASE;

static const DMI_TABLE_CASE DmiTableCases[] =
{
    { "test table", DmiTestTable, sizeof(DmiTestTable), 0, 0xFF, 0 },
    { "no table", NULL, 0, 1, 0, 0 },
    { "unset string indexes", DmiUnsetTable, sizeof(DmiUnsetTable), 0, 0, 0 },
    { "SKU index past the string set", DmiIndexPastSetTable, sizeof(DmiIndexPastSetTable), 0, 0xF7, 0 },
    { "formatted length below the header", DmiBadLengthTable, sizeof(DmiBadLengthTable), 2, 0, 0 },
    { "vendor string of 80 bytes", DmiLongVendorTable, sizeof(DmiLongVendorTable), 0, 0x01, 0x01 },
    { "string set never terminated", DmiUnterminatedTable, sizeof(DmiUnterminatedTable), 3, 0, 0 },
    { "string set closed by one NUL at the end of the table", DmiSingleNulTable, sizeof(DmiSingleNulTable), 0, 0x03, 0 },
    { "stray byte after the system structure", DmiStrayByteTable, sizeof(DmiStrayByteTable), 3, 0x01, 0 },
    { "declared size ends before the stray byte", DmiDeclaredShortTable, sizeof(DmiDeclaredShortTable), 0, 0x01, 0 },
    { "header only", DmiTestTable, 8, 3, 0, 0 },
    { "cut below the raw header", DmiTestTable, 7, 3, 0, 0 },
    { "cut inside the BIOS strings", DmiTestTable, 8 + 20 + 10, 3, 0, 0 },
    { "cut inside the system strings", DmiTestTable, 8 + 47 + 27 + 14, 3, 0x80, 0 },
    { "cut after the base board", DmiTestTable, 8 + 47 + 76 + 43, 3, 0xFF, 0 },
    { "declared size past the data", DmiTestTable, sizeof(DmiTestTable) - 1, 3, 0xFF, 0 }
};

/* Quirk tables for the lookup: vendor and product compare without regard to case, a NULL string matches any value */
static const DSP_DMI_QUIRK DspTestQuirks[] =
{
    { "Vendor A", "Product A", DspAudioPathDspRequired },
    { "VENDOR B", NULL, DspAudioPathDspOptional },
    { NULL, "Product C", DspAudioPathLegacyHdaOnly },
    { NULL, NULL, DspAudioPathNone }
};

static const DSP_DMI_QUIRK DspEmptyQuirks[] =
{
    { NULL, NULL, DspAudioPathNone }
};

typedef struct _DMI_LOOKUP_CASE
{
    const char* SysVendor;
    const char* SysProduct;
    int Truncated;
    int Expected;
} DMI_LOOKUP_CASE;

static const DMI_LOOKUP_CASE DmiLookupCases[] =
{
    { "Vendor A", "Product A", 0, 0 },
    { "vendor a", "PRODUCT A", 0, 0 },
    { "Vendor A", "Other", 0, -1 },
    { "vendor b", "anything", 0, 1 },
    { "anyone", "product c", 0, 2 },
    { "Vendor A", "Product A", 1, -1 },
    { NULL, "Product A", 0, -1 },
    { "Vendor A", NULL, 0, -1 },
    { NULL, NULL, 0, -1 },
    { "", "Product A", 0, -1 }
};

#endif
