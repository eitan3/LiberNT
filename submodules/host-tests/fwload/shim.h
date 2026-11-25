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

typedef int8_t INT8, *PINT8;
typedef uint8_t UINT8, *PUINT8;
typedef int16_t INT16, *PINT16;
typedef uint16_t UINT16, *PUINT16;
typedef uint32_t UINT32, *PUINT32;
typedef uint64_t UINT64, *PUINT64;
typedef size_t SIZE_T;
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
#define _Inout_
#define _Out_opt_

#endif

#define _In_reads_(x)
#define _In_reads_opt_(x)
#define _Out_writes_(x)
#define _Inout_opt_
#define _Out_writes_bytes_(x)
#define _Out_writes_opt_(x)
