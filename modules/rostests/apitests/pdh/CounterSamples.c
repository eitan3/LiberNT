/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests the counter types, raw samples and first-sample status of the processor counters
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <apitest.h>
#include <winperf.h>
#include <pdh.h>
#include <pdhmsg.h>

static const struct
{
    PCWSTR Path;
    DWORD Type;
} Counters[] =
{
    { L"\\Processor(_Total)\\% Processor Time", PERF_100NSEC_TIMER_INV },
    { L"\\Processor Information(_Total)\\% Processor Performance", PERF_AVERAGE_BULK },
    { L"\\Processor Information(_Total)\\% Processor Utility", PERF_AVERAGE_BULK },
    { L"\\Processor Information(_Total)\\Processor Frequency", PERF_COUNTER_RAWCOUNT },
};

START_TEST(CounterSamples)
{
    PDH_HCOUNTER Handles[ARRAYSIZE(Counters)];
    PDH_RAW_COUNTER First[ARRAYSIZE(Counters)], Second;
    PDH_FMT_COUNTERVALUE Value;
    PDH_HQUERY Query;
    PDH_STATUS Status;
    FILETIME Now;
    ULONGLONG NowValue;
    BYTE Info[4096];
    DWORD Size, Type, i;

    ok_long(PdhOpenQueryW(NULL, 0, &Query), ERROR_SUCCESS);
    for (i = 0; i < ARRAYSIZE(Counters); i++)
    {
        Handles[i] = NULL;
        ok_long(PdhAddEnglishCounterW(Query, Counters[i].Path, 0, &Handles[i]), ERROR_SUCCESS);
        Size = sizeof(Info);
        ok_long(PdhGetCounterInfoW(Handles[i], FALSE, &Size, (PPDH_COUNTER_INFO_W)Info), ERROR_SUCCESS);
        ok(((PPDH_COUNTER_INFO_W)Info)->dwType == Counters[i].Type, "%ls: type %#lx, expected %#lx\n",
           Counters[i].Path, ((PPDH_COUNTER_INFO_W)Info)->dwType, Counters[i].Type);
    }

    ok_long(PdhCollectQueryData(Query), ERROR_SUCCESS);
    GetSystemTimeAsFileTime(&Now);
    NowValue = ((ULONGLONG)Now.dwHighDateTime << 32) | Now.dwLowDateTime;
    for (i = 0; i < ARRAYSIZE(Counters); i++)
    {
        ok_long(PdhGetRawCounterValue(Handles[i], &Type, &First[i]), ERROR_SUCCESS);
        ok_long(First[i].CStatus, PDH_CSTATUS_VALID_DATA);
        Value.CStatus = 0xdeadbeef;
        Value.largeValue = 0x1234;
        Status = PdhGetFormattedCounterValue(Handles[i], PDH_FMT_DOUBLE | PDH_FMT_NOCAP100, NULL, &Value);
        if (Counters[i].Type == PERF_COUNTER_RAWCOUNT)
        {
            ok_long(Status, ERROR_SUCCESS);
            ok(Value.doubleValue == (double)First[i].FirstValue, "%ls: %f, raw %I64d\n", Counters[i].Path,
               Value.doubleValue, First[i].FirstValue);
        }
        else
        {
            ok(Status == PDH_INVALID_DATA, "%ls: first sample status %#lx\n", Counters[i].Path, Status);
            ok(Value.CStatus == PDH_CSTATUS_INVALID_DATA, "%ls: first sample CStatus %#lx\n", Counters[i].Path,
               Value.CStatus);
            ok(Value.largeValue == 0, "%ls: first sample value %I64x\n", Counters[i].Path, Value.largeValue);
        }
    }
    ok(First[0].SecondValue + 600000000ULL > NowValue && First[0].SecondValue < NowValue + 600000000ULL,
       "%% Processor Time time stamp %I64d, system time %I64u\n", First[0].SecondValue, NowValue);

    Sleep(100);
    ok_long(PdhCollectQueryData(Query), ERROR_SUCCESS);
    for (i = 0; i < ARRAYSIZE(Counters); i++)
    {
        ok_long(PdhGetRawCounterValue(Handles[i], &Type, &Second), ERROR_SUCCESS);
        Status = PdhGetFormattedCounterValue(Handles[i], PDH_FMT_DOUBLE | PDH_FMT_NOCAP100, NULL, &Value);
        ok(Status == ERROR_SUCCESS, "%ls: second sample status %#lx\n", Counters[i].Path, Status);
        ok_long(Value.CStatus, PDH_CSTATUS_VALID_DATA);
        if (Counters[i].Type == PERF_COUNTER_RAWCOUNT)
            continue;
        ok(Second.SecondValue > First[i].SecondValue, "%ls: base %I64d after %I64d\n", Counters[i].Path,
           Second.SecondValue, First[i].SecondValue);
        ok(Second.FirstValue >= First[i].FirstValue, "%ls: value %I64d after %I64d\n", Counters[i].Path,
           Second.FirstValue, First[i].FirstValue);
        ok(Value.doubleValue >= 0.0 && Value.doubleValue <= 1000.0, "%ls: %f\n", Counters[i].Path, Value.doubleValue);
    }

    for (i = 0; i < ARRAYSIZE(Counters); i++)
        ok_long(PdhGetRawCounterValue(Handles[i], &Type, &First[i]), ERROR_SUCCESS);
    ok_long(PdhCollectQueryData(Query), ERROR_SUCCESS);
    for (i = 0; i < ARRAYSIZE(Counters); i++)
    {
        if (Counters[i].Type != PERF_AVERAGE_BULK)
            continue;
        ok_long(PdhGetRawCounterValue(Handles[i], &Type, &Second), ERROR_SUCCESS);
        ok(Second.SecondValue > First[i].SecondValue, "%ls: back-to-back base %I64d after %I64d\n", Counters[i].Path,
           Second.SecondValue, First[i].SecondValue);
        Status = PdhGetFormattedCounterValue(Handles[i], PDH_FMT_DOUBLE | PDH_FMT_NOCAP100, NULL, &Value);
        ok(Status == ERROR_SUCCESS, "%ls: back-to-back sample status %#lx\n", Counters[i].Path, Status);
        ok(Value.doubleValue == Value.doubleValue && Value.doubleValue >= 0.0 && Value.doubleValue <= 1000.0,
           "%ls: back-to-back sample %f\n", Counters[i].Path, Value.doubleValue);
    }
    PdhCloseQuery(Query);
}
