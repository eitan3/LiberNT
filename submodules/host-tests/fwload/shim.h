/*
 * PROJECT:     LiberNT host-native tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Environment for the firmware loader tests: the Windows types the parser libraries use
 * COPYRIGHT:   Copyright 2026 LiberNT contributors
 */

#ifndef FWLOAD_TEST_SHIM_H
#define FWLOAD_TEST_SHIM_H

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stddef.h>
#include <stdarg.h>

typedef int8_t INT8, *PINT8;
typedef uint8_t UINT8, *PUINT8;
typedef int16_t INT16, *PINT16;
typedef uint16_t UINT16, *PUINT16;
typedef uint32_t UINT32, *PUINT32;
typedef uint64_t UINT64, *PUINT64;
typedef size_t SIZE_T, *PSIZE_T;
typedef char CHAR, *PCHAR;
typedef unsigned char UCHAR, *PUCHAR, BOOLEAN, *PBOOLEAN;
typedef void VOID, *PVOID;
typedef int32_t LONG, *PLONG;
typedef int32_t NTSTATUS;
typedef uint16_t USHORT, *PUSHORT;
typedef uint32_t ULONG, *PULONG;
typedef void* HANDLE;
typedef HANDLE* PHANDLE;

#define MAXSIZE_T              (~(SIZE_T)0)
#define MAXULONG32              0xFFFFFFFFUL
#define MAXULONG64              0xFFFFFFFFFFFFFFFFULL

#define NT_SUCCESS(Status)      ((NTSTATUS)(Status) >= 0)
#define STATUS_SUCCESS          ((NTSTATUS)0x00000000L)
#define STATUS_NO_SUCH_DEVICE   ((NTSTATUS)0xC000000EL)
#define STATUS_INVALID_BUFFER_SIZE ((NTSTATUS)0xC0000023L)
/* the status codes the DSP driver's stream manager answers */
#define STATUS_UNSUCCESSFUL         ((NTSTATUS)0xC0000001L)
#define STATUS_INVALID_HANDLE       ((NTSTATUS)0xC0000008L)
#define STATUS_INVALID_PARAMETER    ((NTSTATUS)0xC000000DL)
#define STATUS_INVALID_DEVICE_REQUEST ((NTSTATUS)0xC0000010L)
#define STATUS_NO_MEMORY            ((NTSTATUS)0xC0000017L)
#define STATUS_OBJECT_NAME_NOT_FOUND ((NTSTATUS)0xC0000034L)
#define STATUS_IO_TIMEOUT           ((NTSTATUS)0xC00000B5L)
#define STATUS_NOT_SUPPORTED        ((NTSTATUS)0xC00000BBL)
#define STATUS_DEVICE_NOT_READY     ((NTSTATUS)0xC00000A3L)
#define STATUS_INSUFFICIENT_RESOURCES ((NTSTATUS)0xC000009AL)
#define STATUS_INVALID_DEVICE_STATE ((NTSTATUS)0xC0000184L)
#define STATUS_DEVICE_PROTOCOL_ERROR ((NTSTATUS)0xC0000186L)
#define STATUS_NOT_FOUND            ((NTSTATUS)0xC0000225L)
#define STATUS_CANCELLED            ((NTSTATUS)0xC0000120L)
#define STATUS_RETRY                ((NTSTATUS)0xC0000227L)

typedef int32_t INT32, *PINT32;
typedef int64_t INT64, *PINT64;
typedef uint64_t ULONGLONG, *PULONGLONG;
typedef int64_t LONGLONG, *PLONGLONG;
typedef ULONG* PULONG;

#define _Must_inspect_result_
#define FIELD_OFFSET(type, field)   offsetof(type, field)
#define RTL_SIZEOF_THROUGH_FIELD(type, field) (FIELD_OFFSET(type, field) + sizeof(((type*)0)->field))

#define RtlZeroMemory(Destination, Length)  memset((Destination), 0, (Length))
#define RtlCopyMemory(Destination, Source, Length) memcpy((Destination), (Source), (Length))

#ifndef TRUE
#define TRUE 1
#endif
#ifndef FALSE
#define FALSE 0
#endif

#define _In_
#define _Out_
#define _In_reads_bytes_(x)
#define _In_reads_bytes_opt_(x)
#define _Out_writes_bytes_(x)
#define _In_opt_
#define _In_z_
#define _In_opt_z_
#define _Inout_
#define _Out_opt_

#endif

#define _In_reads_(x)
#define _In_reads_opt_(x)
#define _Out_writes_(x)
#define _Inout_opt_
#define _Out_writes_bytes_(x)
#define _Out_writes_opt_(x)
#define _In_reads_bytes_(x)
#define _In_range_(a, b)
#define _Printf_format_string_
#define _Deref_out_opt_
