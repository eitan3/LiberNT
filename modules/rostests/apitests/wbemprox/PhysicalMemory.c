/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test that Win32_PhysicalMemory reports the SMBIOS memory devices
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "wmitest.h"

#define RSMB_SIGNATURE (('R' << 24) | ('S' << 16) | ('M' << 8) | 'B')

typedef struct _MEMORY_DEVICE
{
    ULONGLONG Capacity;
    UCHAR Type;
} MEMORY_DEVICE;

static
ULONG
ReadMemoryDevices(MEMORY_DEVICE *Devices, ULONG Max)
{
    ULONG Size = GetSystemFirmwareTable(RSMB_SIGNATURE, 0, NULL, 0), Count = 0, TableSize;
    UCHAR *Buffer, *Entry, *End, *Next;

    if (!Size || !(Buffer = HeapAlloc(GetProcessHeap(), 0, Size)))
        return 0;
    GetSystemFirmwareTable(RSMB_SIGNATURE, 0, Buffer, Size);
    TableSize = *(ULONG *)(Buffer + 4);
    Entry = Buffer + 8;
    End = Entry + min(TableSize, Size - 8);
    while (Entry + 4 <= End && Entry[1] >= 4 && Entry[0] != 127)
    {
        for (Next = Entry + Entry[1]; Next + 1 < End && (Next[0] || Next[1]); Next++)
            ;
        if (Entry[0] == 17 && Entry[1] >= 0x13 && Count < Max)
        {
            USHORT DeviceSize = *(USHORT *)(Entry + 0x0c);
            ULONGLONG Bytes = 0;

            if (DeviceSize == 0x7fff && Entry[1] >= 0x20)
                Bytes = (ULONGLONG)(*(ULONG *)(Entry + 0x1c) & 0x7fffffff) << 20;
            else if (DeviceSize != 0 && DeviceSize != 0xffff)
                Bytes = (DeviceSize & 0x8000) ? (ULONGLONG)(DeviceSize & 0x7fff) << 10 : (ULONGLONG)DeviceSize << 20;
            if (Bytes)
            {
                Devices[Count].Capacity = Bytes;
                Devices[Count].Type = Entry[0x12];
                Count++;
            }
        }
        Entry = Next + 2;
    }
    HeapFree(GetProcessHeap(), 0, Buffer);
    return Count;
}

START_TEST(PhysicalMemory)
{
    MEMORY_DEVICE Devices[64];
    ULONG DeviceCount, Instances = 0, Matched = 0;
    ULONGLONG Total = 0, Installed = 0;
    IWbemServices *Services;
    IEnumWbemClassObject *Enum = NULL;
    IWbemClassObject *Object;
    BSTR Language = SysAllocString(L"WQL"), Query = SysAllocString(L"SELECT * FROM Win32_PhysicalMemory");
    HRESULT hr;
    ULONG Got;

    DeviceCount = ReadMemoryDevices(Devices, RTL_NUMBER_OF(Devices));
    if (!DeviceCount)
    {
        skip("no populated SMBIOS memory device\n");
        return;
    }
    ok(GetPhysicallyInstalledSystemMemory(&Installed), "GetPhysicallyInstalledSystemMemory failed %lu\n", GetLastError());

    ok_hex(WmiInitialize(), S_OK);
    Services = WmiConnect(L"ROOT\\CIMV2", &hr);
    ok_hex(hr, S_OK);
    if (!Services)
        return;
    hr = IWbemServices_ExecQuery(Services, Language, Query, WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY, NULL,
                                 &Enum);
    ok_hex(hr, S_OK);
    while (Enum && IEnumWbemClassObject_Next(Enum, WBEM_INFINITE, 1, &Object, &Got) == S_OK && Got)
    {
        VARIANT Value;
        CIMTYPE Type;
        ULONGLONG Capacity = 0;
        LONG SmbiosType = -1;

        VariantInit(&Value);
        if (SUCCEEDED(IWbemClassObject_Get(Object, L"Capacity", 0, &Value, &Type, NULL)))
        {
            ok_long(Type, CIM_UINT64);
            if (V_VT(&Value) == VT_BSTR)
                Capacity = _wcstoui64(V_BSTR(&Value), NULL, 10);
            VariantClear(&Value);
        }
        if (SUCCEEDED(IWbemClassObject_Get(Object, L"SMBIOSMemoryType", 0, &Value, &Type, NULL)))
        {
            ok_long(Type, CIM_UINT32);
            if (V_VT(&Value) == VT_I4)
                SmbiosType = V_I4(&Value);
            VariantClear(&Value);
        }
        if (Instances < DeviceCount)
        {
            ok(Capacity == Devices[Instances].Capacity, "instance %lu capacity %I64u, SMBIOS %I64u\n", Instances, Capacity,
               Devices[Instances].Capacity);
            ok(SmbiosType == Devices[Instances].Type, "instance %lu type %ld, SMBIOS %u\n", Instances, SmbiosType,
               Devices[Instances].Type);
            Matched += (Capacity == Devices[Instances].Capacity);
        }
        Total += Capacity;
        Instances++;
        IWbemClassObject_Release(Object);
    }
    ok_long(Instances, DeviceCount);
    ok_long(Matched, DeviceCount);
    ok(Total == Installed * 1024, "total %I64u, installed %I64u KB\n", Total, Installed);
    if (Enum)
        IEnumWbemClassObject_Release(Enum);
    IWbemServices_Release(Services);
    SysFreeString(Language);
    SysFreeString(Query);
}
