/*
 * PROJECT:     LiberNT kernel-mode tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Synthetic replies of ACPI _DSD evaluations, shared by the kmtest and the host test
 * COPYRIGHT:   Copyright 2026 LiberNT contributors
 */

#ifndef _DISCO_CASES_H_
#define _DISCO_CASES_H_

#include <disco.h>
#include "dsp_cases.h"

/* ACPI_EVAL_OUTPUT_BUFFER (signature 'BoeA', length, count) and ACPI_METHOD_ARGUMENT records of 4 bytes plus max(4, data length) */
#define DISCO_REPLY(Length, Count)      DSP_U32(0x41656F42), DSP_U32(Length), DSP_U32(Count)
#define DISCO_ARG_INTEGER(Value)        DSP_U16(DISCO_ARGUMENT_INTEGER), DSP_U16(4), DSP_U32(Value)
#define DISCO_ARG_STRING_3(A, B, C)     DSP_U16(DISCO_ARGUMENT_STRING), DSP_U16(3), (A), (B), (C), 0x00
#define DISCO_ARG_STRING_1(A)           DSP_U16(DISCO_ARGUMENT_STRING), DSP_U16(1), (A), 0x00, 0x00, 0x00
#define DISCO_ARG_STRINGZ_3(A, B, C)    DSP_U16(DISCO_ARGUMENT_STRING), DSP_U16(4), (A), (B), (C), 0x00
#define DISCO_ARG_STRING_5(A, B, C, D, E) DSP_U16(DISCO_ARGUMENT_STRING), DSP_U16(5), (A), (B), (C), (D), (E)
#define DISCO_ARG_BUFFER(Length)        DSP_U16(DISCO_ARGUMENT_BUFFER), DSP_U16(Length)
#define DISCO_ARG_PACKAGE(Length)       DSP_U16(DISCO_ARGUMENT_PACKAGE), DSP_U16(Length)

#define DISCO_DSD_GUID \
    0x14, 0xD8, 0xFF, 0xDA, 0xBA, 0x6E, 0x8C, 0x4D, 0x8A, 0x91, 0xBC, 0x9B, 0xBF, 0x4A, 0xA3, 0x01
#define DISCO_OTHER_GUID \
    0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88, 0x99, 0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF

#define DISCO_DSD_GUID_ARG              DISCO_ARG_BUFFER(16), DISCO_DSD_GUID
#define DISCO_OTHER_GUID_ARG            DISCO_ARG_BUFFER(16), DISCO_OTHER_GUID

/* three properties: "one" = integer 5, "two" = package of the integers 7 and 9, "three" = the string x */
#define DISCO_PROPERTY_ONE              DISCO_ARG_STRING_3('o', 'n', 'e'), DISCO_ARG_INTEGER(5)
#define DISCO_PROPERTY_TWO              DISCO_ARG_STRING_3('t', 'w', 'o'), \
    DISCO_ARG_PACKAGE(16), DISCO_ARG_INTEGER(7), DISCO_ARG_INTEGER(9)
#define DISCO_PROPERTY_THREE            DISCO_ARG_STRING_5('t', 'h', 'r', 'e', 'e'), DISCO_ARG_STRING_1('x')
#define DISCO_THREE_PROPERTY_PACKAGE    DISCO_ARG_PACKAGE(61), \
    DISCO_PROPERTY_ONE, DISCO_PROPERTY_TWO, DISCO_PROPERTY_THREE

typedef struct _DISCO_BEGIN_CASE
{
    const char* Name;
    const unsigned char* Reply;
    unsigned int Size;
    int Status;
    unsigned int PropertyCount;
} DISCO_BEGIN_CASE;

static const unsigned char DiscoThreePropertyReply[] =
{
    DISCO_REPLY(97, 2),
    DISCO_DSD_GUID_ARG,
    DISCO_THREE_PROPERTY_PACKAGE
};

static const unsigned char DiscoSecondGuidReply[] =
{
    DISCO_REPLY(129, 4),
    DISCO_OTHER_GUID_ARG,
    DISCO_ARG_PACKAGE(8), DISCO_ARG_INTEGER(1),
    DISCO_DSD_GUID_ARG,
    DISCO_THREE_PROPERTY_PACKAGE
};

static const unsigned char DiscoOtherGuidOnlyReply[] =
{
    DISCO_REPLY(44, 2),
    DISCO_OTHER_GUID_ARG,
    DISCO_ARG_PACKAGE(8), DISCO_ARG_INTEGER(1)
};

static const unsigned char DiscoGuidThenIntegerReply[] =
{
    DISCO_REPLY(40, 2),
    DISCO_DSD_GUID_ARG,
    DISCO_ARG_INTEGER(3)
};

static const unsigned char DiscoBadSignatureReply[] =
{
    DSP_U32(0x314F6242), DSP_U32(97), DSP_U32(2),
    DISCO_DSD_GUID_ARG,
    DISCO_THREE_PROPERTY_PACKAGE
};

static const unsigned char DiscoLengthPastReplyReply[] =
{
    DISCO_REPLY(98, 2),
    DISCO_DSD_GUID_ARG,
    DISCO_THREE_PROPERTY_PACKAGE
};

static const unsigned char DiscoLengthBelowHeaderReply[] =
{
    DISCO_REPLY(8, 2),
    DISCO_DSD_GUID_ARG,
    DISCO_THREE_PROPERTY_PACKAGE
};

static const unsigned char DiscoCountPastReplyReply[] =
{
    DISCO_REPLY(97, 3),
    DISCO_DSD_GUID_ARG,
    DISCO_THREE_PROPERTY_PACKAGE
};

/* the package claims 45 data bytes but holds two complete pairs and one stray byte */
static const unsigned char DiscoShortPackageReply[] =
{
    DISCO_REPLY(81, 2),
    DISCO_DSD_GUID_ARG,
    DISCO_ARG_PACKAGE(45),
    DISCO_PROPERTY_ONE, DISCO_PROPERTY_TWO, 0x00
};

static const unsigned char DiscoPackagePastReplyReply[] =
{
    DISCO_REPLY(97, 2),
    DISCO_DSD_GUID_ARG,
    DISCO_ARG_PACKAGE(61),
    DISCO_PROPERTY_ONE, DISCO_PROPERTY_TWO
};

static const unsigned char DiscoEmptyPackageReply[] =
{
    DISCO_REPLY(40, 2),
    DISCO_DSD_GUID_ARG,
    DISCO_ARG_PACKAGE(0), DSP_U32(0)
};

static const unsigned char DiscoShortGuidReply[] =
{
    DISCO_REPLY(96, 2),
    DISCO_ARG_BUFFER(15), 0x14, 0xD8, 0xFF, 0xDA, 0xBA, 0x6E, 0x8C, 0x4D, 0x8A, 0x91, 0xBC, 0x9B, 0xBF, 0x4A, 0xA3,
    DISCO_THREE_PROPERTY_PACKAGE
};

static const unsigned char DiscoGuidAsStringReply[] =
{
    DISCO_REPLY(97, 2),
    DSP_U16(DISCO_ARGUMENT_STRING), DSP_U16(16), DISCO_DSD_GUID,
    DISCO_THREE_PROPERTY_PACKAGE
};

/* the package claims ten data bytes where the reply holds eight, and the reply ends there */
static const unsigned char DiscoPackageOverrunReply[] =
{
    DISCO_REPLY(44, 2),
    DISCO_DSD_GUID_ARG,
    DISCO_ARG_PACKAGE(10), DISCO_ARG_INTEGER(1)
};

/* the _DSD GUID carried in a buffer of seventeen bytes */
static const unsigned char DiscoLongGuidReply[] =
{
    DISCO_REPLY(98, 2),
    DISCO_ARG_BUFFER(17), DISCO_DSD_GUID, 0x00,
    DISCO_THREE_PROPERTY_PACKAGE
};

/* "two" appears twice: the first one, the integer 7, wins over the second one, the integer 9 */
static const unsigned char DiscoDuplicateNameReply[] =
{
    DISCO_REPLY(85, 2),
    DISCO_DSD_GUID_ARG,
    DISCO_ARG_PACKAGE(49),
    DISCO_ARG_STRINGZ_3('t', 'w', 'o'), DISCO_ARG_INTEGER(7),
    DISCO_ARG_STRING_3('t', 'w', 'o'), DISCO_ARG_INTEGER(9),
    DISCO_PROPERTY_THREE
};

static const DISCO_BEGIN_CASE DiscoBeginCases[] =
{
    { "three properties", DiscoThreePropertyReply, sizeof(DiscoThreePropertyReply), 0, 3 },
    { "_DSD after another GUID", DiscoSecondGuidReply, sizeof(DiscoSecondGuidReply), 0, 3 },
    { "another GUID only", DiscoOtherGuidOnlyReply, sizeof(DiscoOtherGuidOnlyReply), 5, 0 },
    { "_DSD GUID followed by an integer", DiscoGuidThenIntegerReply, sizeof(DiscoGuidThenIntegerReply), 5, 0 },
    { "bad signature", DiscoBadSignatureReply, sizeof(DiscoBadSignatureReply), 2, 0 },
    { "length past the reply", DiscoLengthPastReplyReply, sizeof(DiscoLengthPastReplyReply), 4, 0 },
    { "length below the header", DiscoLengthBelowHeaderReply, sizeof(DiscoLengthBelowHeaderReply), 3, 0 },
    { "count names one more pair than the reply holds", DiscoCountPastReplyReply, sizeof(DiscoCountPastReplyReply), 0, 3 },
    { "package cut after two complete pairs", DiscoShortPackageReply, sizeof(DiscoShortPackageReply), 0, 2 },
    { "package runs past the reply", DiscoPackagePastReplyReply, sizeof(DiscoPackagePastReplyReply), 4, 0 },
    { "package without properties", DiscoEmptyPackageReply, sizeof(DiscoEmptyPackageReply), 0, 0 },
    { "GUID of fifteen bytes", DiscoShortGuidReply, sizeof(DiscoShortGuidReply), 5, 0 },
    { "GUID carried as a string", DiscoGuidAsStringReply, sizeof(DiscoGuidAsStringReply), 5, 0 },
    { "duplicate property names", DiscoDuplicateNameReply, sizeof(DiscoDuplicateNameReply), 0, 3 },
    { "package claims four bytes past the reply", DiscoPackageOverrunReply, sizeof(DiscoPackageOverrunReply), 4, 0 },
    { "GUID in a buffer of seventeen bytes", DiscoLongGuidReply, sizeof(DiscoLongGuidReply), 5, 0 },
    { "no reply", NULL, 0, 1, 0 }
};

typedef struct _DISCO_PROPERTY_CASE
{
    unsigned int ReplyIndex;
    unsigned int PropertyIndex;
    int Status;
    const char* Name;
    int NameStatus;
    unsigned int ValueType;
    unsigned int ValueLength;
    unsigned int ValueInteger;
    unsigned int ElementCount;
    unsigned int Element0;
    unsigned int Element1;
} DISCO_PROPERTY_CASE;

static const DISCO_PROPERTY_CASE DiscoPropertyCases[] =
{
    { 0, 0, 0, "one", 0, DISCO_ARGUMENT_INTEGER, 4, 5, 0, 0, 0 },
    { 0, 1, 0, "two", 0, DISCO_ARGUMENT_PACKAGE, 16, 0, 2, 7, 9 },
    { 0, 2, 0, "three", 0, DISCO_ARGUMENT_STRING, 1, 0, 0, 0, 0 },
    { 0, 3, 5, NULL, 0, 0, 0, 0, 0, 0, 0 },
    { 0, 0, 0, "onx", 5, DISCO_ARGUMENT_INTEGER, 4, 5, 0, 0, 0 },
    { 1, 2, 0, "three", 0, DISCO_ARGUMENT_STRING, 1, 0, 0, 0, 0 },
    { 8, 1, 0, "two", 0, DISCO_ARGUMENT_PACKAGE, 16, 0, 2, 7, 9 },
    { 8, 2, 5, NULL, 0, 0, 0, 0, 0, 0, 0 },
    { 10, 0, 5, NULL, 0, 0, 0, 0, 0, 0, 0 },
    { 13, 0, 0, "two", 0, DISCO_ARGUMENT_INTEGER, 4, 7, 0, 0, 0 },
    { 13, 1, 0, "two", 0, DISCO_ARGUMENT_INTEGER, 4, 9, 0, 0, 0 }
};

typedef struct _DISCO_FIND_CASE
{
    unsigned int ReplyIndex;
    const char* Name;
    int Status;
    unsigned int ValueType;
    unsigned int ValueInteger;
} DISCO_FIND_CASE;

static const DISCO_FIND_CASE DiscoFindCases[] =
{
    { 0, "one", 0, DISCO_ARGUMENT_INTEGER, 5 },
    { 0, "two", 0, DISCO_ARGUMENT_PACKAGE, 0 },
    { 0, "three", 0, DISCO_ARGUMENT_STRING, 0 },
    { 0, "four", 5, 0, 0 },
    { 0, "", 5, 0, 0 },
    { 0, "on", 5, 0, 0 },
    { 0, "onee", 5, 0, 0 },
    { 1, "three", 0, DISCO_ARGUMENT_STRING, 0 },
    { 8, "three", 5, 0, 0 },
    { 10, "one", 5, 0, 0 },
    { 13, "two", 0, DISCO_ARGUMENT_INTEGER, 7 },
    { 13, "three", 0, DISCO_ARGUMENT_STRING, 0 },
    { 14, "one", 1, 0, 0 }
};

#endif
