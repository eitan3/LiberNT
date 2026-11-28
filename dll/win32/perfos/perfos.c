/*
 * PROJECT:     LiberNT Performance Counters
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     System, Memory and Processor performance objects
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#define WIN32_NO_STATUS
#include <windows.h>
#include <winperf.h>
#include <stdio.h>
#define NTOS_MODE_USER
#include <ndk/exfuncs.h>
#include <ndk/kefuncs.h>
#include <ndk/rtlfuncs.h>
#include <ndk/psfuncs.h>
#include <ndk/pstypes.h>

#define PERFOS_SYSTEM 2
#define PERFOS_MEMORY 4
#define PERFOS_PROCESSOR 238

typedef struct _PERFOS_COUNTER
{
    DWORD Name;
    DWORD Help;
    LONG Scale;
    DWORD Detail;
    DWORD Type;
    DWORD Size;
    DWORD Offset;
} PERFOS_COUNTER;

static const PERFOS_COUNTER SystemCounters[] =
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
#define SYSTEM_DATA_SIZE 96

static const PERFOS_COUNTER MemoryCounters[] =
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
#define MEMORY_DATA_SIZE 232

static const PERFOS_COUNTER ProcessorCounters[] =
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
#define PROCESSOR_DATA_SIZE 120

static LONG OpenCount;

static BOOL
PerfOsWanted(const WCHAR *Query, DWORD Index)
{
    const WCHAR *p = Query;

    if (!Query || !*Query || !_wcsicmp(Query, L"Global"))
        return TRUE;
    if (!_wcsicmp(Query, L"Costly") || !_wcsnicmp(Query, L"Foreign", 7))
        return FALSE;
    while (*p)
    {
        WCHAR *End;
        DWORD Value;

        while (*p == ' ')
            p++;
        if (!*p)
            break;
        Value = wcstoul(p, &End, 10);
        if (End == p)
            return FALSE;
        if (Value == Index)
            return TRUE;
        p = End;
    }
    return FALSE;
}

static SIZE_T
PerfOsDefinitionLength(SIZE_T Counters)
{
    return sizeof(PERF_OBJECT_TYPE) + Counters * sizeof(PERF_COUNTER_DEFINITION);
}

static BYTE *
PerfOsWriteObject(BYTE *Out, DWORD Index, DWORD DefaultCounter, LONG Instances, const PERFOS_COUNTER *Counters,
                  SIZE_T Count, DWORD TotalLength, LARGE_INTEGER Time, LARGE_INTEGER Frequency)
{
    PERF_OBJECT_TYPE *Object = (PERF_OBJECT_TYPE *)Out;
    PERF_COUNTER_DEFINITION *Definition;
    SIZE_T i;

    RtlZeroMemory(Object, sizeof(*Object));
    Object->TotalByteLength = TotalLength;
    Object->DefinitionLength = (DWORD)PerfOsDefinitionLength(Count);
    Object->HeaderLength = sizeof(PERF_OBJECT_TYPE);
    Object->ObjectNameTitleIndex = Index;
    Object->ObjectHelpTitleIndex = Index + 1;
    Object->DetailLevel = PERF_DETAIL_NOVICE;
    Object->NumCounters = (DWORD)Count;
    Object->DefaultCounter = DefaultCounter;
    Object->NumInstances = Instances;
    Object->PerfTime = Time;
    Object->PerfFreq = Frequency;
    Definition = (PERF_COUNTER_DEFINITION *)(Object + 1);
    for (i = 0; i < Count; i++, Definition++)
    {
        RtlZeroMemory(Definition, sizeof(*Definition));
        Definition->ByteLength = sizeof(*Definition);
        Definition->CounterNameTitleIndex = Counters[i].Name;
        Definition->CounterHelpTitleIndex = Counters[i].Help;
        Definition->DefaultScale = Counters[i].Scale;
        Definition->DetailLevel = Counters[i].Detail;
        Definition->CounterType = Counters[i].Type;
        Definition->CounterSize = Counters[i].Size;
        Definition->CounterOffset = Counters[i].Offset;
    }
    return (BYTE *)Definition;
}

#define PUT32(Block, Offset, Value) (*(DWORD *)((BYTE *)(Block) + (Offset)) = (DWORD)(Value))
#define PUT64(Block, Offset, Value) (*(ULONGLONG *)((BYTE *)(Block) + (Offset)) = (ULONGLONG)(Value))

static void
PerfOsCountThreads(ULONG *Processes, ULONG *Threads, ULONG *ReadyThreads)
{
    ULONG Size = 256 * 1024, Needed = 0;
    PSYSTEM_PROCESS_INFORMATION Info = NULL, Entry;
    NTSTATUS Status;

    *Processes = *Threads = *ReadyThreads = 0;
    for (;;)
    {
        Info = HeapAlloc(GetProcessHeap(), 0, Size);
        if (!Info)
            return;
        Status = NtQuerySystemInformation(SystemProcessInformation, Info, Size, &Needed);
        if (Status != STATUS_INFO_LENGTH_MISMATCH)
            break;
        HeapFree(GetProcessHeap(), 0, Info);
        Info = NULL;
        Size = max(Size * 2, Needed + 64 * 1024);
    }
    if (!NT_SUCCESS(Status))
    {
        HeapFree(GetProcessHeap(), 0, Info);
        return;
    }
    for (Entry = Info;;)
    {
        ULONG i;

        (*Processes)++;
        *Threads += Entry->NumberOfThreads;
        for (i = 0; i < Entry->NumberOfThreads; i++)
        {
            if (((PSYSTEM_THREAD_INFORMATION)(Entry + 1))[i].ThreadState == Ready)
                (*ReadyThreads)++;
        }
        if (!Entry->NextEntryOffset)
            break;
        Entry = (PSYSTEM_PROCESS_INFORMATION)((BYTE *)Entry + Entry->NextEntryOffset);
    }
    HeapFree(GetProcessHeap(), 0, Info);
}

static DWORD
PerfOsSystem(BYTE **Cursor, BYTE *End, LARGE_INTEGER Now)
{
    SYSTEM_PERFORMANCE_INFORMATION Perf;
    SYSTEM_EXCEPTION_INFORMATION Exceptions;
    SYSTEM_REGISTRY_QUOTA_INFORMATION Quota;
    SYSTEM_TIMEOFDAY_INFORMATION TimeOfDay;
    LARGE_INTEGER Frequency;
    ULONG Processes, Threads, Ready;
    DWORD Total = (DWORD)(PerfOsDefinitionLength(ARRAYSIZE(SystemCounters)) + SYSTEM_DATA_SIZE);
    BYTE *Block;

    if (End - *Cursor < (SSIZE_T)Total)
        return ERROR_MORE_DATA;
    RtlZeroMemory(&Perf, sizeof(Perf));
    RtlZeroMemory(&Exceptions, sizeof(Exceptions));
    RtlZeroMemory(&Quota, sizeof(Quota));
    RtlZeroMemory(&TimeOfDay, sizeof(TimeOfDay));
    NtQuerySystemInformation(SystemPerformanceInformation, &Perf, sizeof(Perf), NULL);
    NtQuerySystemInformation(SystemExceptionInformation, &Exceptions, sizeof(Exceptions), NULL);
    NtQuerySystemInformation(SystemRegistryQuotaInformation, &Quota, sizeof(Quota), NULL);
    NtQuerySystemInformation(SystemTimeOfDayInformation, &TimeOfDay, sizeof(TimeOfDay), NULL);
    PerfOsCountThreads(&Processes, &Threads, &Ready);

    Frequency.QuadPart = 10000000;
    Block = PerfOsWriteObject(*Cursor, PERFOS_SYSTEM, 8, PERF_NO_INSTANCES, SystemCounters, ARRAYSIZE(SystemCounters),
                              Total, Now, Frequency);
    RtlZeroMemory(Block, SYSTEM_DATA_SIZE);
    PUT32(Block, 0, SYSTEM_DATA_SIZE);
    PUT32(Block, 4, Perf.IoReadOperationCount);
    PUT32(Block, 8, Perf.IoWriteOperationCount);
    PUT32(Block, 12, Perf.IoOtherOperationCount);
    PUT64(Block, 16, Perf.IoReadTransferCount.QuadPart);
    PUT64(Block, 24, Perf.IoWriteTransferCount.QuadPart);
    PUT64(Block, 32, Perf.IoOtherTransferCount.QuadPart);
    PUT32(Block, 40, Perf.ContextSwitches);
    PUT32(Block, 44, Perf.SystemCalls);
    PUT32(Block, 48, Perf.IoReadOperationCount + Perf.IoWriteOperationCount);
    PUT64(Block, 56, TimeOfDay.BootTime.QuadPart);
    PUT32(Block, 64, Ready);
    PUT32(Block, 68, Processes);
    PUT32(Block, 72, Threads);
    PUT32(Block, 76, Exceptions.AlignmentFixupCount);
    PUT32(Block, 80, Exceptions.ExceptionDispatchCount);
    PUT32(Block, 84, Exceptions.FloatingEmulationCount);
    PUT32(Block, 88, Quota.RegistryQuotaUsed);
    PUT32(Block, 92, Quota.RegistryQuotaAllowed);
    *Cursor += Total;
    return ERROR_SUCCESS;
}

static DWORD
PerfOsMemory(BYTE **Cursor, BYTE *End, LARGE_INTEGER Counter, LARGE_INTEGER Frequency)
{
    SYSTEM_PERFORMANCE_INFORMATION Perf;
    SYSTEM_BASIC_INFORMATION Basic;
    SYSTEM_FILECACHE_INFORMATION Cache;
    DWORD Total = (DWORD)(PerfOsDefinitionLength(ARRAYSIZE(MemoryCounters)) + MEMORY_DATA_SIZE);
    ULONGLONG PageSize;
    BYTE *Block;

    if (End - *Cursor < (SSIZE_T)Total)
        return ERROR_MORE_DATA;
    RtlZeroMemory(&Perf, sizeof(Perf));
    RtlZeroMemory(&Basic, sizeof(Basic));
    RtlZeroMemory(&Cache, sizeof(Cache));
    NtQuerySystemInformation(SystemPerformanceInformation, &Perf, sizeof(Perf), NULL);
    NtQuerySystemInformation(SystemBasicInformation, &Basic, sizeof(Basic), NULL);
    NtQuerySystemInformation(SystemFileCacheInformation, &Cache, sizeof(Cache), NULL);
    PageSize = Basic.PageSize ? Basic.PageSize : 4096;

    Block = PerfOsWriteObject(*Cursor, PERFOS_MEMORY, 8, PERF_NO_INSTANCES, MemoryCounters, ARRAYSIZE(MemoryCounters),
                              Total, Counter, Frequency);
    RtlZeroMemory(Block, MEMORY_DATA_SIZE);
    PUT32(Block, 0, MEMORY_DATA_SIZE);
    PUT32(Block, 4, Perf.PageFaultCount);
    PUT64(Block, 8, Perf.AvailablePages * PageSize);
    PUT64(Block, 16, Perf.CommittedPages * PageSize);
    PUT64(Block, 24, Perf.CommitLimit * PageSize);
    PUT32(Block, 32, Perf.CopyOnWriteCount);
    PUT32(Block, 36, Perf.TransitionCount);
    PUT32(Block, 40, Perf.CacheTransitionCount);
    PUT32(Block, 44, Perf.DemandZeroCount);
    PUT32(Block, 48, Perf.PageReadCount + Perf.DirtyPagesWriteCount + Perf.MappedPagesWriteCount);
    PUT32(Block, 52, Perf.PageReadCount);
    PUT32(Block, 56, Perf.PageReadIoCount);
    PUT32(Block, 60, Perf.DirtyPagesWriteCount + Perf.MappedPagesWriteCount);
    PUT64(Block, 64, Perf.PagedPoolPages * PageSize);
    PUT64(Block, 72, Perf.NonPagedPoolPages * PageSize);
    PUT32(Block, 80, Perf.DirtyWriteIoCount + Perf.MappedWriteIoCount);
    PUT32(Block, 84, Perf.PagedPoolAllocs);
    PUT32(Block, 88, Perf.NonPagedPoolAllocs);
    PUT32(Block, 92, Perf.FreeSystemPtes);
    PUT64(Block, 96, Cache.CurrentSize);
    PUT64(Block, 104, Cache.PeakSize);
    PUT64(Block, 112, Perf.ResidentPagedPoolPage * PageSize);
    PUT64(Block, 120, Perf.TotalSystemCodePages * PageSize);
    PUT64(Block, 128, Perf.ResidentSystemCodePage * PageSize);
    PUT64(Block, 136, Perf.TotalSystemDriverPages * PageSize);
    PUT64(Block, 144, Perf.ResidentSystemDriverPage * PageSize);
    PUT64(Block, 152, Perf.ResidentSystemCachePage * PageSize);
    PUT32(Block, 160, Perf.CommittedPages);
    PUT32(Block, 164, Perf.CommitLimit);
    PUT64(Block, 168, Perf.AvailablePages * PageSize / 1024);
    PUT64(Block, 176, Perf.AvailablePages * PageSize / (1024 * 1024));
    /* TODO: page list sizes, standby cache priorities and repurposed transition pages */
    *Cursor += Total;
    return ERROR_SUCCESS;
}

static DWORD
PerfOsInstanceNameSize(DWORD Index, DWORD Processors)
{
    WCHAR Name[16];

    if (Index < Processors)
        _snwprintf(Name, ARRAYSIZE(Name), L"%lu", Index);
    else
        wcscpy(Name, L"_Total");
    return (DWORD)(((wcslen(Name) + 1) * sizeof(WCHAR) + 7) & ~7);
}

static void
PerfOsWriteProcessors(BYTE *Out, DWORD Processors, const SYSTEM_PROCESSOR_PERFORMANCE_INFORMATION *Times,
                      const SYSTEM_INTERRUPT_INFORMATION *Interrupts)
{
    SYSTEM_PROCESSOR_PERFORMANCE_INFORMATION Sum;
    ULONGLONG DpcCount = 0, DpcRate = 0;
    DWORD i;

    RtlZeroMemory(&Sum, sizeof(Sum));
    for (i = 0; i < Processors; i++)
    {
        Sum.IdleTime.QuadPart += Times[i].IdleTime.QuadPart;
        Sum.KernelTime.QuadPart += Times[i].KernelTime.QuadPart;
        Sum.UserTime.QuadPart += Times[i].UserTime.QuadPart;
        Sum.DpcTime.QuadPart += Times[i].DpcTime.QuadPart;
        Sum.InterruptTime.QuadPart += Times[i].InterruptTime.QuadPart;
        Sum.InterruptCount += Times[i].InterruptCount;
        DpcCount += Interrupts[i].DpcCount;
        DpcRate += Interrupts[i].DpcRate;
    }
    for (i = 0; i <= Processors; i++)
    {
        PERF_INSTANCE_DEFINITION *Instance = (PERF_INSTANCE_DEFINITION *)Out;
        const SYSTEM_PROCESSOR_PERFORMANCE_INFORMATION *Time = (i < Processors) ? &Times[i] : &Sum;
        DWORD NameSize = PerfOsInstanceNameSize(i, Processors);
        WCHAR *Name = (WCHAR *)(Instance + 1);
        BYTE *Block;

        RtlZeroMemory(Out, sizeof(*Instance) + NameSize + PROCESSOR_DATA_SIZE);
        if (i < Processors)
            _snwprintf(Name, 8, L"%lu", i);
        else
            wcscpy(Name, L"_Total");
        Instance->ByteLength = sizeof(*Instance) + NameSize;
        Instance->UniqueID = PERF_NO_UNIQUE_ID;
        Instance->NameOffset = sizeof(*Instance);
        Instance->NameLength = (DWORD)((wcslen(Name) + 1) * sizeof(WCHAR));
        Block = Out + Instance->ByteLength;
        PUT32(Block, 0, PROCESSOR_DATA_SIZE);
        PUT64(Block, 8, Time->IdleTime.QuadPart);
        PUT64(Block, 16, Time->UserTime.QuadPart);
        PUT64(Block, 24, Time->KernelTime.QuadPart - Time->IdleTime.QuadPart);
        PUT32(Block, 32, Time->InterruptCount);
        PUT64(Block, 40, Time->DpcTime.QuadPart);
        PUT64(Block, 48, Time->InterruptTime.QuadPart);
        PUT32(Block, 56, i < Processors ? Interrupts[i].DpcCount : DpcCount);
        PUT32(Block, 60, i < Processors ? Interrupts[i].DpcRate : DpcRate);
        PUT64(Block, 64, Time->IdleTime.QuadPart);
        /* TODO: C-state residency and transitions */
        Out += Instance->ByteLength + PROCESSOR_DATA_SIZE;
    }
}

static DWORD
PerfOsProcessor(BYTE **Cursor, BYTE *End, LARGE_INTEGER Counter, LARGE_INTEGER Frequency)
{
    SYSTEM_BASIC_INFORMATION Basic;
    PSYSTEM_PROCESSOR_PERFORMANCE_INFORMATION Times;
    PSYSTEM_INTERRUPT_INFORMATION Interrupts;
    DWORD Total, Processors, i;
    BYTE *Out;

    RtlZeroMemory(&Basic, sizeof(Basic));
    NtQuerySystemInformation(SystemBasicInformation, &Basic, sizeof(Basic), NULL);
    Processors = Basic.NumberOfProcessors ? Basic.NumberOfProcessors : 1;
    Total = (DWORD)PerfOsDefinitionLength(ARRAYSIZE(ProcessorCounters));
    for (i = 0; i <= Processors; i++)
        Total += sizeof(PERF_INSTANCE_DEFINITION) + PerfOsInstanceNameSize(i, Processors) + PROCESSOR_DATA_SIZE;
    if (End - *Cursor < (SSIZE_T)Total)
        return ERROR_MORE_DATA;

    Times = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, Processors * sizeof(*Times));
    Interrupts = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, Processors * sizeof(*Interrupts));
    if (!Times || !Interrupts)
    {
        HeapFree(GetProcessHeap(), 0, Times);
        HeapFree(GetProcessHeap(), 0, Interrupts);
        return ERROR_NOT_ENOUGH_MEMORY;
    }
    NtQuerySystemInformation(SystemProcessorPerformanceInformation, Times, Processors * sizeof(*Times), NULL);
    NtQuerySystemInformation(SystemInterruptInformation, Interrupts, Processors * sizeof(*Interrupts), NULL);

    Out = PerfOsWriteObject(*Cursor, PERFOS_PROCESSOR, 0, (LONG)Processors + 1, ProcessorCounters,
                            ARRAYSIZE(ProcessorCounters), Total, Counter, Frequency);
    PerfOsWriteProcessors(Out, Processors, Times, Interrupts);
    *Cursor += Total;
    HeapFree(GetProcessHeap(), 0, Times);
    HeapFree(GetProcessHeap(), 0, Interrupts);
    return ERROR_SUCCESS;
}

DWORD WINAPI
OpenOSObject(LPWSTR DeviceNames)
{
    InterlockedIncrement(&OpenCount);
    return ERROR_SUCCESS;
}

DWORD WINAPI
CloseOSObject(void)
{
    InterlockedDecrement(&OpenCount);
    return ERROR_SUCCESS;
}

DWORD WINAPI
CollectOSObjectData(LPWSTR ValueName, LPVOID *Data, LPDWORD TotalBytes, LPDWORD NumObjectTypes)
{
    BYTE *Start, *Cursor, *End;
    LARGE_INTEGER Counter, Frequency, Now;
    DWORD Objects = 0, Status = ERROR_SUCCESS;

    if (!Data || !*Data || !TotalBytes || !NumObjectTypes)
        return ERROR_INVALID_PARAMETER;
    Start = Cursor = *Data;
    End = Start + *TotalBytes;
    QueryPerformanceCounter(&Counter);
    QueryPerformanceFrequency(&Frequency);
    NtQuerySystemTime(&Now);

    if (PerfOsWanted(ValueName, PERFOS_SYSTEM))
    {
        Status = PerfOsSystem(&Cursor, End, Now);
        Objects += (Status == ERROR_SUCCESS);
    }
    if (Status == ERROR_SUCCESS && PerfOsWanted(ValueName, PERFOS_MEMORY))
    {
        Status = PerfOsMemory(&Cursor, End, Counter, Frequency);
        Objects += (Status == ERROR_SUCCESS);
    }
    if (Status == ERROR_SUCCESS && PerfOsWanted(ValueName, PERFOS_PROCESSOR))
    {
        Status = PerfOsProcessor(&Cursor, End, Counter, Frequency);
        Objects += (Status == ERROR_SUCCESS);
    }
    if (Status != ERROR_SUCCESS)
    {
        *TotalBytes = 0;
        *NumObjectTypes = 0;
        return Status;
    }
    *TotalBytes = (DWORD)(Cursor - Start);
    *NumObjectTypes = Objects;
    *Data = Cursor;
    return ERROR_SUCCESS;
}

C_ASSERT(sizeof(PERF_INSTANCE_DEFINITION) == 24);
C_ASSERT(sizeof(PERF_COUNTER_DEFINITION) == 40);
C_ASSERT(sizeof(PERF_OBJECT_TYPE) == 64);
