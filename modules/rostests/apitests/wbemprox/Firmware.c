/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test the WMI properties that come from the SMBIOS and FADT firmware tables and the page files
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "wmitest.h"

#define RSMB_SIGNATURE (('R' << 24) | ('S' << 16) | ('M' << 8) | 'B')
#define ACPI_SIGNATURE (('A' << 24) | ('C' << 16) | ('P' << 8) | 'I')
#define FACP_SIGNATURE (('P' << 24) | ('C' << 16) | ('A' << 8) | 'F')

static UCHAR *Smbios;
static ULONG SmbiosSize;

static
UCHAR *
FindStructure(UCHAR Type, UCHAR **Strings)
{
    UCHAR *Entry = Smbios + 8, *End = Smbios + 8 + min(*(ULONG *)(Smbios + 4), SmbiosSize - 8), *Next;

    while (Entry + 4 <= End && Entry[1] >= 4)
    {
        for (Next = Entry + Entry[1]; Next + 1 < End && (Next[0] || Next[1]); Next++)
            ;
        if (Entry[0] == Type)
        {
            *Strings = Entry + Entry[1];
            return Entry;
        }
        if (Entry[0] == 127)
            break;
        Entry = Next + 2;
    }
    return NULL;
}

static
void
StructureString(UCHAR Type, UCHAR Offset, WCHAR *Out, ULONG OutChars)
{
    UCHAR *Strings, *Entry = FindStructure(Type, &Strings);
    UCHAR Index;

    Out[0] = 0;
    if (!Entry || Entry[1] <= Offset || !(Index = Entry[Offset]))
        return;
    while (--Index && *Strings)
        Strings += strlen((char *)Strings) + 1;
    if (*Strings)
        MultiByteToWideChar(CP_ACP, 0, (char *)Strings, -1, Out, OutChars);
}

static
IWbemClassObject *
FirstInstance(IWbemServices *Services, const WCHAR *Text)
{
    BSTR Language = SysAllocString(L"WQL"), Query = SysAllocString(Text);
    IEnumWbemClassObject *Enum = NULL;
    IWbemClassObject *Object = NULL;
    ULONG Got = 0;
    HRESULT hr;

    hr = IWbemServices_ExecQuery(Services, Language, Query, WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY, NULL,
                                 &Enum);
    ok_hex(hr, S_OK);
    if (Enum)
    {
        if (IEnumWbemClassObject_Next(Enum, WBEM_INFINITE, 1, &Object, &Got) != S_OK || !Got)
            Object = NULL;
        IEnumWbemClassObject_Release(Enum);
    }
    SysFreeString(Language);
    SysFreeString(Query);
    return Object;
}

static
void
CheckString(IWbemClassObject *Object, const WCHAR *Name, const WCHAR *Expected)
{
    VARIANT Value;
    CIMTYPE Type;
    HRESULT hr;

    VariantInit(&Value);
    hr = IWbemClassObject_Get(Object, Name, 0, &Value, &Type, NULL);
    ok(hr == S_OK, "%ls: hr %#lx\n", Name, hr);
    ok(Type == CIM_STRING, "%ls: type %ld\n", Name, Type);
    if (Expected[0])
    {
        ok(V_VT(&Value) == VT_BSTR, "%ls: vt %u\n", Name, V_VT(&Value));
        if (V_VT(&Value) == VT_BSTR)
            ok(!wcscmp(V_BSTR(&Value), Expected), "%ls: %ls, expected %ls\n", Name, V_BSTR(&Value), Expected);
    }
    else
    {
        ok(V_VT(&Value) == VT_NULL, "%ls: vt %u for an absent string\n", Name, V_VT(&Value));
    }
    VariantClear(&Value);
}

static
LONG
GetLong(IWbemClassObject *Object, const WCHAR *Name, CIMTYPE ExpectedType)
{
    VARIANT Value;
    CIMTYPE Type;
    HRESULT hr;
    LONG Result = -1;

    VariantInit(&Value);
    hr = IWbemClassObject_Get(Object, Name, 0, &Value, &Type, NULL);
    ok(hr == S_OK, "%ls: hr %#lx\n", Name, hr);
    ok(Type == ExpectedType, "%ls: type %ld, expected %ld\n", Name, Type, ExpectedType);
    ok(V_VT(&Value) == VT_I4, "%ls: vt %u\n", Name, V_VT(&Value));
    if (V_VT(&Value) == VT_I4)
        Result = V_I4(&Value);
    VariantClear(&Value);
    return Result;
}

START_TEST(Firmware)
{
    IWbemServices *Services;
    IWbemClassObject *Object;
    WCHAR Expected[256];
    UCHAR *Strings, *Entry;
    ULONG FacpSize;
    HRESULT hr;

    SmbiosSize = GetSystemFirmwareTable(RSMB_SIGNATURE, 0, NULL, 0);
    if (!SmbiosSize || !(Smbios = HeapAlloc(GetProcessHeap(), 0, SmbiosSize)))
    {
        skip("no SMBIOS table\n");
        return;
    }
    GetSystemFirmwareTable(RSMB_SIGNATURE, 0, Smbios, SmbiosSize);

    ok_hex(WmiInitialize(), S_OK);
    Services = WmiConnect(L"ROOT\\CIMV2", &hr);
    ok_hex(hr, S_OK);
    if (!Services)
        return;

    Object = FirstInstance(Services, L"SELECT SystemFamily, SystemSKUNumber, PCSystemType FROM Win32_ComputerSystem");
    ok(Object != NULL, "no Win32_ComputerSystem\n");
    if (Object)
    {
        StructureString(1, 0x1a, Expected, RTL_NUMBER_OF(Expected));
        CheckString(Object, L"SystemFamily", Expected);
        StructureString(1, 0x19, Expected, RTL_NUMBER_OF(Expected));
        CheckString(Object, L"SystemSKUNumber", Expected);
        FacpSize = GetSystemFirmwareTable(ACPI_SIGNATURE, FACP_SIGNATURE, NULL, 0);
        if (FacpSize > 45)
        {
            UCHAR *Buffer = HeapAlloc(GetProcessHeap(), 0, FacpSize);

            if (Buffer)
            {
                GetSystemFirmwareTable(ACPI_SIGNATURE, FACP_SIGNATURE, Buffer, FacpSize);
                ok_long(GetLong(Object, L"PCSystemType", CIM_UINT16), Buffer[45]);
                HeapFree(GetProcessHeap(), 0, Buffer);
            }
        }
        IWbemClassObject_Release(Object);
    }

    Object = FirstInstance(Services, L"SELECT SMBIOSAssetTag FROM Win32_SystemEnclosure");
    ok(Object != NULL, "no Win32_SystemEnclosure\n");
    if (Object)
    {
        StructureString(3, 0x08, Expected, RTL_NUMBER_OF(Expected));
        CheckString(Object, L"SMBIOSAssetTag", Expected);
        IWbemClassObject_Release(Object);
    }

    Object = FirstInstance(Services, L"SELECT MemoryErrorCorrection FROM Win32_PhysicalMemoryArray");
    ok(Object != NULL, "no Win32_PhysicalMemoryArray\n");
    if (Object)
    {
        Entry = FindStructure(16, &Strings);
        if (Entry && Entry[1] > 6)
            ok_long(GetLong(Object, L"MemoryErrorCorrection", CIM_UINT16), Entry[6]);
        IWbemClassObject_Release(Object);
    }

    Object = FirstInstance(Services, L"SELECT AllocatedBaseSize, CurrentUsage, PeakUsage FROM Win32_PageFileUsage");
    if (!Object)
    {
        skip("no page file\n");
    }
    else
    {
        LONG Allocated = GetLong(Object, L"AllocatedBaseSize", CIM_UINT32);
        LONG Current = GetLong(Object, L"CurrentUsage", CIM_UINT32);
        LONG Peak = GetLong(Object, L"PeakUsage", CIM_UINT32);
        VARIANT Value;
        CIMTYPE Type;

        ok(Allocated > 0, "AllocatedBaseSize %ld\n", Allocated);
        ok(Current >= 0 && Current <= Allocated, "CurrentUsage %ld of %ld\n", Current, Allocated);
        ok(Peak >= Current && Peak <= Allocated, "PeakUsage %ld, current %ld of %ld\n", Peak, Current, Allocated);
        VariantInit(&Value);
        hr = IWbemClassObject_Get(Object, L"Name", 0, &Value, &Type, NULL);
        ok_hex(hr, S_OK);
        ok(V_VT(&Value) == VT_BSTR && V_BSTR(&Value)[0] && V_BSTR(&Value)[1] == L':',
           "Name is not a DOS path\n");
        VariantClear(&Value);
        IWbemClassObject_Release(Object);
    }

    IWbemServices_Release(Services);
    HeapFree(GetProcessHeap(), 0, Smbios);
}
