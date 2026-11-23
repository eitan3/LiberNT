/*
 * PROJECT:     LiberNT audio driver libraries
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Walks the reply of an ACPI _DSD evaluation into its properties and packages, without I/O
 * COPYRIGHT:   Copyright 2026 LiberNT contributors
 */

#ifndef _AUDIO_DISCO_H_
#define _AUDIO_DISCO_H_

#ifdef __cplusplus
extern "C" {
#endif

/* ACPI_METHOD_ARGUMENT types of the reply of IOCTL_ACPI_EVAL_METHOD_EX */
#define DISCO_ARGUMENT_INTEGER     0
#define DISCO_ARGUMENT_STRING      1
#define DISCO_ARGUMENT_BUFFER      2
#define DISCO_ARGUMENT_PACKAGE     3

#define DISCO_GUID_BYTES           16
#define DISCO_REPLY_HEADER_BYTES   12
#define DISCO_ARGUMENT_HEADER_BYTES 4
#define DISCO_INTEGER_BYTES        4

typedef enum _DISCO_STATUS
{
    DiscoOk = 0,
    DiscoNoData,
    DiscoBadSignature,
    DiscoBadLength,
    DiscoTruncated,
    DiscoNotFound,
    DiscoWrongType
} DISCO_STATUS;

/* One ACPI_METHOD_ARGUMENT of the reply: type, length and where its data is */
typedef struct _DISCO_ARGUMENT
{
    UINT16 Type;
    UINT16 DataLength;
    const UINT8* Data;
} DISCO_ARGUMENT, *PDISCO_ARGUMENT;

/* The property package that follows the _DSD UUID in a reply */
typedef struct _DISCO_DSD
{
    const UINT8* PackageData;
    SIZE_T PackageLength;
    UINT32 PropertyCount;
} DISCO_DSD, *PDISCO_DSD;

/* The UUID of the ACPI _DSD data extension, as the 16 bytes of a BUFFER argument */
const UINT8* DiscoDsdUuid(VOID);

DISCO_STATUS
DiscoDsdBegin(
    _In_reads_bytes_opt_(Size) const UINT8* Reply,
    SIZE_T Size,
    _Out_ DISCO_DSD* Dsd);

DISCO_STATUS
DiscoDsdPropertyAt(
    _In_ const DISCO_DSD* Dsd,
    UINT32 Index,
    _Out_ DISCO_ARGUMENT* Name,
    _Out_ DISCO_ARGUMENT* Value);

DISCO_STATUS
DiscoDsdFind(
    _In_ const DISCO_DSD* Dsd,
    _In_z_ const char* Name,
    _Out_ DISCO_ARGUMENT* Value);

DISCO_STATUS
DiscoArgumentIsName(
    _In_ const DISCO_ARGUMENT* Argument,
    _In_z_ const char* Name);

UINT32
DiscoPackageCount(
    _In_ const DISCO_ARGUMENT* Package);

DISCO_STATUS
DiscoPackageElementAt(
    _In_ const DISCO_ARGUMENT* Package,
    UINT32 Index,
    _Out_ DISCO_ARGUMENT* Element);

DISCO_STATUS
DiscoArgumentU32(
    _In_ const DISCO_ARGUMENT* Argument,
    _Out_ UINT32* Value);

DISCO_STATUS
DiscoArgumentCopy(
    _In_ const DISCO_ARGUMENT* Argument,
    _Out_writes_bytes_(Capacity) UINT8* Buffer,
    SIZE_T Capacity,
    _Out_opt_ SIZE_T* Length);

const char* DiscoStatusName(DISCO_STATUS Status);

#ifdef __cplusplus
}
#endif

#endif
