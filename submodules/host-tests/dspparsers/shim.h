/*
 * PROJECT:     LiberNT host-native tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Environment for the DSP parser tests: the Windows types the parser libraries use
 * COPYRIGHT:   Copyright 2026 LiberNT contributors
 */

#ifndef DSPPARSERS_TEST_SHIM_H
#define DSPPARSERS_TEST_SHIM_H

#include <stdint.h>
#include <stdio.h>
#include <string.h>

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
#define _In_reads_(x)
#define _Out_writes_bytes_(x)
#define _Out_writes_opt_(x)
#define _In_opt_
#define _In_z_
#define _In_opt_z_
#define _Inout_
#define _Out_opt_

#endif
