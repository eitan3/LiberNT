/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests the System, Memory and Processor performance objects of HKEY_PERFORMANCE_DATA
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"
#include <winperf.h>

typedef struct _PERF_COUNTER_EXPECT
{
    DWORD Name;
    DWORD Help;
    LONG Scale;
    DWORD Detail;
    DWORD Type;
    DWORD Size;
    DWORD Offset;
} PERF_COUNTER_EXPECT;

static const PERF_COUNTER_EXPECT SystemExpect[] =
{
    { 10, 11, 0, PERF_DETAIL_NOVICE, PERF_COUNTER_COUNTER, 4, 4 },
    { 12, 13, 0, PERF_DETAIL_NOVICE, PERF_COUNTER_COUNTER, 4, 8 },
    { 14, 15, 0, PERF_DETAIL_ADVANCED, PERF_COUNTER_COUNTER, 4, 12 },
    { 16, 17, -4, PERF_DETAIL_EXPERT, PERF_COUNTER_BULK_COUNT, 8, 16 },
    { 18, 19, -4, PERF_DETAIL_EXPERT, PERF_COUNTER_BULK_COUNT, 8, 24 },
    { 20, 21, -3, PERF_DETAIL_WIZARD, PERF_COUNTER_BULK_COUNT, 8, 32 },
    { 146, 147, -2, PERF_DETAIL_ADVANCED, PERF_COUNTER_COUNTER, 4, 40 },
    { 150, 151, -1, PERF_DETAIL_ADVANCED, PERF_COUNTER_COUNTER, 4, 44 },
    { 406, 407, 0, PERF_DETAIL_NOVICE, PERF_COUNTER_COUNTER, 4, 48 },
    { 674, 675, -5, PERF_DETAIL_NOVICE, PERF_ELAPSED_TIME, 8, 56 },
    { 44, 45, 1, PERF_DETAIL_WIZARD, PERF_COUNTER_RAWCOUNT, 4, 64 },
    { 248, 249, 1, PERF_DETAIL_WIZARD, PERF_COUNTER_RAWCOUNT, 4, 68 },
    { 250, 251, 1, PERF_DETAIL_WIZARD, PERF_COUNTER_RAWCOUNT, 4, 72 },
    { 686, 687, 0, PERF_DETAIL_WIZARD, PERF_COUNTER_COUNTER, 4, 76 },
    { 688, 689, 0, PERF_DETAIL_WIZARD, PERF_COUNTER_COUNTER, 4, 80 },
    { 690, 691, 0, PERF_DETAIL_WIZARD, PERF_COUNTER_COUNTER, 4, 84 },
    { 1350, 1351, 0, PERF_DETAIL_ADVANCED, PERF_RAW_FRACTION, 4, 88 },
    { 1350, 1351, 0, PERF_DETAIL_ADVANCED, PERF_RAW_BASE, 4, 92 },
};

static const PERF_COUNTER_EXPECT MemoryExpect[] =
{
    { 28, 29, -1, PERF_DETAIL_NOVICE, PERF_COUNTER_COUNTER, 4, 4 },
    { 24, 25, -6, PERF_DETAIL_EXPERT, PERF_COUNTER_LARGE_RAWCOUNT, 8, 8 },
    { 26, 27, -6, PERF_DETAIL_EXPERT, PERF_COUNTER_LARGE_RAWCOUNT, 8, 16 },
    { 30, 31, -6, PERF_DETAIL_WIZARD, PERF_COUNTER_LARGE_RAWCOUNT, 8, 24 },
    { 32, 33, 0, PERF_DETAIL_WIZARD, PERF_COUNTER_COUNTER, 4, 32 },
    { 34, 35, -1, PERF_DETAIL_WIZARD, PERF_COUNTER_COUNTER, 4, 36 },
    { 36, 37, -1, PERF_DETAIL_WIZARD, PERF_COUNTER_COUNTER, 4, 40 },
    { 38, 39, -1, PERF_DETAIL_WIZARD, PERF_COUNTER_COUNTER, 4, 44 },
    { 40, 41, 0, PERF_DETAIL_NOVICE, PERF_COUNTER_COUNTER, 4, 48 },
    { 822, 823, 0, PERF_DETAIL_NOVICE, PERF_COUNTER_COUNTER, 4, 52 },
    { 42, 43, 0, PERF_DETAIL_EXPERT, PERF_COUNTER_COUNTER, 4, 56 },
    { 48, 49, 0, PERF_DETAIL_ADVANCED, PERF_COUNTER_COUNTER, 4, 60 },
    { 56, 57, -5, PERF_DETAIL_ADVANCED, PERF_COUNTER_LARGE_RAWCOUNT, 8, 64 },
    { 58, 59, -5, PERF_DETAIL_ADVANCED, PERF_COUNTER_LARGE_RAWCOUNT, 8, 72 },
    { 50, 51, 0, PERF_DETAIL_EXPERT, PERF_COUNTER_COUNTER, 4, 80 },
    { 60, 61, -2, PERF_DETAIL_WIZARD, PERF_COUNTER_RAWCOUNT, 4, 84 },
    { 64, 65, -2, PERF_DETAIL_WIZARD, PERF_COUNTER_RAWCOUNT, 4, 88 },
    { 678, 679, -2, PERF_DETAIL_WIZARD, PERF_COUNTER_RAWCOUNT, 4, 92 },
    { 818, 819, -5, PERF_DETAIL_ADVANCED, PERF_COUNTER_LARGE_RAWCOUNT, 8, 96 },
    { 820, 821, -5, PERF_DETAIL_ADVANCED, PERF_COUNTER_LARGE_RAWCOUNT, 8, 104 },
    { 66, 63, -5, PERF_DETAIL_ADVANCED, PERF_COUNTER_LARGE_RAWCOUNT, 8, 112 },
    { 68, 69, -5, PERF_DETAIL_ADVANCED, PERF_COUNTER_LARGE_RAWCOUNT, 8, 120 },
    { 70, 71, -5, PERF_DETAIL_ADVANCED, PERF_COUNTER_LARGE_RAWCOUNT, 8, 128 },
    { 72, 73, -5, PERF_DETAIL_ADVANCED, PERF_COUNTER_LARGE_RAWCOUNT, 8, 136 },
    { 74, 75, -5, PERF_DETAIL_ADVANCED, PERF_COUNTER_LARGE_RAWCOUNT, 8, 144 },
    { 76, 77, -5, PERF_DETAIL_ADVANCED, PERF_COUNTER_LARGE_RAWCOUNT, 8, 152 },
    { 1406, 1407, 0, PERF_DETAIL_EXPERT, PERF_RAW_FRACTION, 4, 160 },
    { 1406, 1407, 0, PERF_DETAIL_EXPERT, PERF_RAW_BASE, 4, 164 },
    { 1380, 1381, 0, PERF_DETAIL_EXPERT, PERF_COUNTER_LARGE_RAWCOUNT, 8, 168 },
    { 1382, 1383, 0, PERF_DETAIL_EXPERT, PERF_COUNTER_LARGE_RAWCOUNT, 8, 176 },
    { 824, 825, -1, PERF_DETAIL_WIZARD, PERF_COUNTER_COUNTER, 4, 224 },
    { 1676, 1677, 0, PERF_DETAIL_WIZARD, PERF_COUNTER_LARGE_RAWCOUNT, 8, 184 },
    { 1678, 1679, 0, PERF_DETAIL_WIZARD, PERF_COUNTER_LARGE_RAWCOUNT, 8, 192 },
    { 1680, 1681, 0, PERF_DETAIL_WIZARD, PERF_COUNTER_LARGE_RAWCOUNT, 8, 200 },
    { 1682, 1683, 0, PERF_DETAIL_WIZARD, PERF_COUNTER_LARGE_RAWCOUNT, 8, 208 },
    { 1684, 1685, 0, PERF_DETAIL_WIZARD, PERF_COUNTER_LARGE_RAWCOUNT, 8, 216 },
    { 1686, 1687, -2, PERF_DETAIL_WIZARD, PERF_COUNTER_RAWCOUNT, 4, 228 },
};

static const PERF_COUNTER_EXPECT ProcessorExpect[] =
{
    { 6, 7, 0, PERF_DETAIL_NOVICE, PERF_100NSEC_TIMER_INV, 8, 8 },
    { 142, 143, 0, PERF_DETAIL_ADVANCED, PERF_100NSEC_TIMER, 8, 16 },
    { 144, 145, 0, PERF_DETAIL_ADVANCED, PERF_100NSEC_TIMER, 8, 24 },
    { 148, 149, -2, PERF_DETAIL_NOVICE, PERF_COUNTER_COUNTER, 4, 32 },
    { 696, 339, 0, PERF_DETAIL_ADVANCED, PERF_100NSEC_TIMER, 8, 40 },
    { 698, 397, 0, PERF_DETAIL_ADVANCED, PERF_100NSEC_TIMER, 8, 48 },
    { 1334, 1335, 0, PERF_DETAIL_ADVANCED, PERF_COUNTER_COUNTER, 4, 56 },
    { 1336, 1337, 0, PERF_DETAIL_ADVANCED, PERF_COUNTER_RAWCOUNT, 4, 60 },
    { 1746, 1747, 0, PERF_DETAIL_WIZARD, PERF_100NSEC_TIMER, 8, 64 },
    { 1748, 1749, 0, PERF_DETAIL_WIZARD, PERF_100NSEC_TIMER, 8, 72 },
    { 1750, 1751, 0, PERF_DETAIL_WIZARD, PERF_100NSEC_TIMER, 8, 80 },
    { 1752, 1753, 0, PERF_DETAIL_WIZARD, PERF_100NSEC_TIMER, 8, 88 },
    { 1754, 1755, 0, PERF_DETAIL_WIZARD, PERF_COUNTER_BULK_COUNT, 8, 96 },
    { 1756, 1757, 0, PERF_DETAIL_WIZARD, PERF_COUNTER_BULK_COUNT, 8, 104 },
    { 1758, 1759, 0, PERF_DETAIL_WIZARD, PERF_COUNTER_BULK_COUNT, 8, 112 },
};

static
BOOL
NameTableHas(const WCHAR *Table, DWORD Index, const WCHAR *Name)
{
    const WCHAR *p;

    for (p = Table; *p; )
    {
        DWORD Value = wcstoul(p, NULL, 10);

        p += wcslen(p) + 1;
        if (!*p)
            break;
        if (Value == Index && !wcscmp(p, Name))
            return TRUE;
        p += wcslen(p) + 1;
    }
    return FALSE;
}

static
VOID
CheckObject(const PERF_OBJECT_TYPE *Object, DWORD DataLength, const PERF_COUNTER_EXPECT *Expect, DWORD Count,
            LONG DefaultCounter)
{
    const PERF_COUNTER_DEFINITION *Definition = (const PERF_COUNTER_DEFINITION *)((const BYTE *)Object + Object->HeaderLength);
    DWORD i;

    ok_long(Object->HeaderLength, sizeof(PERF_OBJECT_TYPE));
    ok_long(Object->ObjectHelpTitleIndex, Object->ObjectNameTitleIndex + 1);
    ok_long(Object->NumCounters, Count);
    ok_long(Object->DefaultCounter, DefaultCounter);
    ok_long(Object->DetailLevel, PERF_DETAIL_NOVICE);
    ok_long(Object->DefinitionLength, sizeof(PERF_OBJECT_TYPE) + Count * sizeof(PERF_COUNTER_DEFINITION));
    if (Object->NumCounters != Count)
        return;
    for (i = 0; i < Count; i++, Definition++)
    {
        ok(Definition->ByteLength == sizeof(*Definition) && Definition->CounterNameTitleIndex == Expect[i].Name &&
           Definition->CounterHelpTitleIndex == Expect[i].Help && Definition->DefaultScale == Expect[i].Scale &&
           Definition->DetailLevel == Expect[i].Detail && Definition->CounterType == Expect[i].Type &&
           Definition->CounterSize == Expect[i].Size && Definition->CounterOffset == Expect[i].Offset,
           "object %lu counter %lu: name %lu help %lu scale %ld detail %lu type %08lx size %lu offset %lu\n",
           Object->ObjectNameTitleIndex, i, Definition->CounterNameTitleIndex, Definition->CounterHelpTitleIndex,
           Definition->DefaultScale, Definition->DetailLevel, Definition->CounterType, Definition->CounterSize,
           Definition->CounterOffset);
    }
    if (Object->NumInstances == PERF_NO_INSTANCES)
    {
        const PERF_COUNTER_BLOCK *Block = (const PERF_COUNTER_BLOCK *)((const BYTE *)Object + Object->DefinitionLength);

        ok_long(Block->ByteLength, DataLength);
        ok_long(Object->TotalByteLength, Object->DefinitionLength + DataLength);
    }
}

START_TEST(PerfData)
{
    SYSTEM_INFO Info;
    DWORD Size, Type, Err, i, Found = 0;
    BYTE *Buffer;
    PERF_DATA_BLOCK *Block;
    PERF_OBJECT_TYPE *Object;

    GetSystemInfo(&Info);
    Size = 0;
    Err = RegQueryValueExW(HKEY_PERFORMANCE_DATA, L"Counter 009", NULL, &Type, NULL, &Size);
    ok_long(Err, ERROR_SUCCESS);
    ok_long(Type, REG_MULTI_SZ);
    Buffer = HeapAlloc(GetProcessHeap(), 0, Size + 4096);
    Size += 4096;
    Err = RegQueryValueExW(HKEY_PERFORMANCE_DATA, L"Counter 009", NULL, &Type, Buffer, &Size);
    ok_long(Err, ERROR_SUCCESS);
    if (Err == ERROR_SUCCESS)
    {
        ok(NameTableHas((WCHAR *)Buffer, 2, L"System"), "no System name\n");
        ok(NameTableHas((WCHAR *)Buffer, 4, L"Memory"), "no Memory name\n");
        ok(NameTableHas((WCHAR *)Buffer, 238, L"Processor"), "no Processor name\n");
        ok(NameTableHas((WCHAR *)Buffer, 6, L"% Processor Time"), "no % Processor Time name\n");
        ok(NameTableHas((WCHAR *)Buffer, 674, L"System Up Time"), "no System Up Time name\n");
    }
    HeapFree(GetProcessHeap(), 0, Buffer);

    Size = 1024 * 1024;
    Buffer = HeapAlloc(GetProcessHeap(), 0, Size);
    Err = RegQueryValueExW(HKEY_PERFORMANCE_DATA, L"2 238 4", NULL, &Type, Buffer, &Size);
    RegCloseKey(HKEY_PERFORMANCE_DATA);
    ok_long(Err, ERROR_SUCCESS);
    if (Err != ERROR_SUCCESS)
    {
        HeapFree(GetProcessHeap(), 0, Buffer);
        return;
    }
    Block = (PERF_DATA_BLOCK *)Buffer;
    ok(!memcmp(Block->Signature, L"PERF", 8), "signature\n");
    ok(Block->PerfTime100nSec.QuadPart != 0, "no PerfTime100nSec\n");
    ok(Block->PerfFreq.QuadPart != 0, "no PerfFreq\n");
    ok(Block->NumObjectTypes >= 3, "%lu objects\n", Block->NumObjectTypes);
    Object = (PERF_OBJECT_TYPE *)(Buffer + Block->HeaderLength);
    for (i = 0; i < Block->NumObjectTypes; i++)
    {
        if (Object->ObjectNameTitleIndex == 2)
        {
            Found |= 1;
            CheckObject(Object, 96, SystemExpect, ARRAYSIZE(SystemExpect), 8);
            ok_long(Object->NumInstances, PERF_NO_INSTANCES);
            ok_long(Object->PerfFreq.QuadPart, 10000000);
        }
        else if (Object->ObjectNameTitleIndex == 4)
        {
            Found |= 2;
            CheckObject(Object, 232, MemoryExpect, ARRAYSIZE(MemoryExpect), 8);
            ok_long(Object->NumInstances, PERF_NO_INSTANCES);
        }
        else if (Object->ObjectNameTitleIndex == 238)
        {
            const PERF_INSTANCE_DEFINITION *Instance;
            const WCHAR *Name = NULL;
            LONG n;

            Found |= 4;
            CheckObject(Object, 0, ProcessorExpect, ARRAYSIZE(ProcessorExpect), 0);
            ok_long(Object->NumInstances, Info.dwNumberOfProcessors + 1);
            Instance = (const PERF_INSTANCE_DEFINITION *)((const BYTE *)Object + Object->DefinitionLength);
            for (n = 0; n < Object->NumInstances; n++)
            {
                const PERF_COUNTER_BLOCK *Data = (const PERF_COUNTER_BLOCK *)((const BYTE *)Instance + Instance->ByteLength);

                Name = (const WCHAR *)((const BYTE *)Instance + Instance->NameOffset);
                ok_long(Instance->NameOffset, sizeof(PERF_INSTANCE_DEFINITION));
                ok_long(Instance->UniqueID, PERF_NO_UNIQUE_ID);
                ok_long(Instance->ByteLength, sizeof(PERF_INSTANCE_DEFINITION) + ((Instance->NameLength + 7) & ~7));
                ok_long(Data->ByteLength, 120);
                Instance = (const PERF_INSTANCE_DEFINITION *)((const BYTE *)Data + Data->ByteLength);
            }
            ok(Name && !wcscmp(Name, L"_Total"), "last instance %ls\n", Name);
        }
        Object = (PERF_OBJECT_TYPE *)((BYTE *)Object + Object->TotalByteLength);
    }
    ok_long(Found, 7);
    HeapFree(GetProcessHeap(), 0, Buffer);
}
