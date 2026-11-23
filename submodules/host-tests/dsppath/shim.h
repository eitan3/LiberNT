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
typedef uint64_t UINT64, *PUINT64;
typedef size_t SIZE_T;
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

#endif
