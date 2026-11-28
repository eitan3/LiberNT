/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests PdhFormatFromRawValue
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <apitest.h>

#include <windef.h>
#include <winbase.h>
#include <winperf.h>
#include <pdh.h>
#include <pdhmsg.h>

static PDH_RAW_COUNTER Raw(LONGLONG First, LONGLONG Second, DWORD MultiCount)
{
    PDH_RAW_COUNTER Counter;

    ZeroMemory(&Counter, sizeof(Counter));
    Counter.CStatus = PDH_CSTATUS_VALID_DATA;
    Counter.FirstValue = First;
    Counter.SecondValue = Second;
    Counter.MultiCount = MultiCount;
    return Counter;
}

static void TestDouble(DWORD Type, DWORD Format, LONGLONG TimeBase, PDH_RAW_COUNTER New, PDH_RAW_COUNTER *Old,
                       double Expected, int Line)
{
    PDH_FMT_COUNTERVALUE Value;
    PDH_STATUS Status;

    ZeroMemory(&Value, sizeof(Value));
    Value.CStatus = 0xdeadbeef;
    Status = PdhFormatFromRawValue(Type, PDH_FMT_DOUBLE | Format, &TimeBase, &New, Old, &Value);
    ok_(__FILE__, Line)(Status == ERROR_SUCCESS, "type %#lx: status %#lx\n", Type, Status);
    ok_(__FILE__, Line)(Value.CStatus == PDH_CSTATUS_VALID_DATA, "type %#lx: CStatus %#lx\n", Type, Value.CStatus);
    ok_(__FILE__, Line)(Value.doubleValue > Expected - 0.0001 && Value.doubleValue < Expected + 0.0001,
                        "type %#lx: value %f, expected %f\n", Type, Value.doubleValue, Expected);
}

START_TEST(FormatFromRawValue)
{
    PDH_RAW_COUNTER New, Old;
    PDH_FMT_COUNTERVALUE Value;
    LONGLONG TimeBase = 1000;
    PDH_STATUS Status;

    TestDouble(PERF_COUNTER_RAWCOUNT, 0, 1000, Raw(1234, 0, 0), NULL, 1234.0, __LINE__);
    TestDouble(PERF_COUNTER_LARGE_RAWCOUNT, 0, 1000, Raw(5000000000LL, 0, 0), NULL, 5000000000.0, __LINE__);
    TestDouble(PERF_COUNTER_RAWCOUNT, PDH_FMT_1000, 1000, Raw(7, 0, 0), NULL, 7000.0, __LINE__);

    Old = Raw(100, 1000, 0);
    TestDouble(PERF_100NSEC_TIMER, 0, 1000, Raw(700, 2000, 0), &Old, 60.0, __LINE__);
    TestDouble(PERF_100NSEC_TIMER_INV, 0, 1000, Raw(700, 2000, 0), &Old, 40.0, __LINE__);
    TestDouble(PERF_COUNTER_TIMER, 0, 1000, Raw(700, 2000, 0), &Old, 60.0, __LINE__);
    TestDouble(PERF_COUNTER_TIMER_INV, 0, 1000, Raw(700, 2000, 0), &Old, 40.0, __LINE__);
    TestDouble(PERF_100NSEC_TIMER, 0, 1000, Raw(100, 1000, 0), &Old, 0.0, __LINE__);

    Old = Raw(50, 1000, 0);
    TestDouble(PERF_COUNTER_COUNTER, 0, 1000, Raw(100, 3000, 0), &Old, 25.0, __LINE__);
    TestDouble(PERF_COUNTER_BULK_COUNT, 0, 1000, Raw(100, 3000, 0), &Old, 25.0, __LINE__);

    TestDouble(PERF_RAW_FRACTION, 0, 1000, Raw(25, 200, 0), NULL, 12.5, __LINE__);
    TestDouble(PERF_LARGE_RAW_FRACTION, 0, 1000, Raw(300, 200, 0), NULL, 100.0, __LINE__);
    TestDouble(PERF_LARGE_RAW_FRACTION, PDH_FMT_NOCAP100, 1000, Raw(300, 200, 0), NULL, 150.0, __LINE__);

    Old = Raw(500, 10, 0);
    TestDouble(PERF_AVERAGE_BULK, 0, 1000, Raw(1500, 30, 0), &Old, 50.0, __LINE__);
    TestDouble(PERF_AVERAGE_TIMER, 0, 1000, Raw(1500, 30, 0), &Old, 0.05, __LINE__);

    Old = Raw(10, 100, 0);
    TestDouble(PERF_SAMPLE_FRACTION, 0, 1000, Raw(30, 300, 0), &Old, 10.0, __LINE__);

    TestDouble(PERF_ELAPSED_TIME, 0, 100, Raw(1000, 6000, 0), NULL, 50.0, __LINE__);

    New = Raw(1234, 0, 0);
    Value.CStatus = 0xdeadbeef;
    Status = PdhFormatFromRawValue(PERF_COUNTER_RAWCOUNT, PDH_FMT_LONG, &TimeBase, &New, NULL, &Value);
    ok_hex(Status, ERROR_SUCCESS);
    ok_long(Value.longValue, 1234);

    Status = PdhFormatFromRawValue(PERF_COUNTER_LARGE_RAWCOUNT, PDH_FMT_LARGE, &TimeBase, &New, NULL, &Value);
    ok_hex(Status, ERROR_SUCCESS);
    ok(Value.largeValue == 1234, "largeValue %I64d\n", Value.largeValue);

    Status = PdhFormatFromRawValue(PERF_COUNTER_RAWCOUNT, PDH_FMT_LONG, &TimeBase, &New, NULL, NULL);
    ok_hex(Status, PDH_INVALID_ARGUMENT);

    Status = PdhFormatFromRawValue(PERF_COUNTER_RAWCOUNT, PDH_FMT_LONG, &TimeBase, NULL, NULL, &Value);
    ok_hex(Status, PDH_INVALID_ARGUMENT);

    Status = PdhFormatFromRawValue(PERF_COUNTER_RAWCOUNT, 0, &TimeBase, &New, NULL, &Value);
    ok_hex(Status, PDH_INVALID_ARGUMENT);

    Value.CStatus = 0xdeadbeef;
    Status = PdhFormatFromRawValue(PERF_100NSEC_TIMER, PDH_FMT_DOUBLE, &TimeBase, &New, NULL, &Value);
    ok_hex(Status, PDH_CSTATUS_INVALID_DATA);
    ok_hex(Value.CStatus, PDH_CSTATUS_INVALID_DATA);

    Value.CStatus = 0xdeadbeef;
    Status = PdhFormatFromRawValue(PERF_LARGE_RAW_BASE, PDH_FMT_DOUBLE, &TimeBase, &New, NULL, &Value);
    ok_hex(Status, PDH_NO_DATA);
    ok_hex(Value.CStatus, PDH_NO_DATA);
}
