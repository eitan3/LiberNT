/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests the processor frequency and performance counters of Processor Information
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <apitest.h>
#include <pdh.h>
#include <pdhmsg.h>
#include <powrprof.h>

typedef struct _PROCESSOR_POWER
{
    ULONG Number;
    ULONG MaxMhz;
    ULONG CurrentMhz;
    ULONG MhzLimit;
    ULONG MaxIdleState;
    ULONG CurrentIdleState;
} PROCESSOR_POWER;

static
LONG
ReadCounter(PDH_HQUERY Query, PCWSTR Path, PDH_STATUS *AddStatus)
{
    PDH_HCOUNTER Counter = NULL;
    PDH_FMT_COUNTERVALUE Value;

    *AddStatus = PdhAddEnglishCounterW(Query, Path, 0, &Counter);
    if (*AddStatus != ERROR_SUCCESS)
        return -1;
    if (PdhCollectQueryData(Query) != ERROR_SUCCESS)
        return -2;
    Sleep(50);
    if (PdhCollectQueryData(Query) != ERROR_SUCCESS ||
        PdhGetFormattedCounterValue(Counter, PDH_FMT_LONG | PDH_FMT_NOCAP100, NULL, &Value) != ERROR_SUCCESS)
        return -2;
    return Value.longValue;
}

START_TEST(ProcessorFrequency)
{
    PROCESSOR_POWER Power[256];
    SYSTEM_INFO Info;
    PDH_HQUERY Query;
    PDH_STATUS Status;
    ULONGLONG MaxSum = 0;
    LONG Frequency, Performance;
    DWORD i;

    GetSystemInfo(&Info);
    ok(Info.dwNumberOfProcessors <= ARRAYSIZE(Power), "%lu processors\n", Info.dwNumberOfProcessors);
    if (Info.dwNumberOfProcessors > ARRAYSIZE(Power))
        return;
    ok_long(CallNtPowerInformation(ProcessorInformation, NULL, 0, Power, Info.dwNumberOfProcessors * sizeof(*Power)),
            0);
    for (i = 0; i < Info.dwNumberOfProcessors; i++)
        MaxSum += Power[i].MaxMhz;

    ok_long(PdhOpenQueryW(NULL, 0, &Query), ERROR_SUCCESS);
    Frequency = ReadCounter(Query, L"\\Processor Information(_Total)\\Processor Frequency", &Status);
    ok_long(Status, ERROR_SUCCESS);
    ok_long(Frequency, (LONG)(MaxSum / Info.dwNumberOfProcessors));
    Performance = ReadCounter(Query, L"\\Processor Information(_Total)\\% Processor Performance", &Status);
    ok_long(Status, ERROR_SUCCESS);
    ok(Performance > 0, "%% Processor Performance %ld\n", Performance);
    Performance = ReadCounter(Query, L"\\Processor Information(0,0)\\% Processor Performance", &Status);
    ok_long(Status, ERROR_SUCCESS);
    ok(Performance > 0, "processor 0 %% Processor Performance %ld\n", Performance);
    PdhCloseQuery(Query);
}
