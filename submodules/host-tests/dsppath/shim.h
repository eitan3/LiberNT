/*
 * PROJECT:     LiberNT host-native tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Environment for the DSP path tests: the Windows types the parser and the policy headers use
 * COPYRIGHT:   Copyright 2026 LiberNT contributors
 */

#ifndef DSPPATH_TEST_SHIM_H
#define DSPPATH_TEST_SHIM_H

#include <stdint.h>
#include <stdio.h>
#include <string.h>

typedef uint8_t UINT8, *PUINT8;
typedef uint16_t UINT16, *PUINT16;
typedef uint32_t UINT32, *PUINT32;
typedef uint32_t ULONG, *PULONG;
typedef uint64_t UINT64, *PUINT64;
typedef size_t SIZE_T;
typedef uint64_t ULONGLONG, *PULONGLONG;
typedef char CHAR, *PCHAR;
typedef unsigned char UCHAR, *PUCHAR, BOOLEAN, *PBOOLEAN;
typedef void VOID, *PVOID;

/* wdm.h: the page as the kernel's amd64 build has it, and the frame number of one */
typedef SIZE_T ULONG_PTR, *PULONG_PTR;
typedef ULONG_PTR PFN_NUMBER, *PPFN_NUMBER;
#define PAGE_SIZE 0x1000
#define PAGE_SHIFT 12L

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
#define _Out_writes_(x)
#define _In_opt_
#define _In_z_
#define _In_opt_z_
#define _Inout_

#endif
