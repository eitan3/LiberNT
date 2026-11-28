/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test that a WQL property list returns the key properties of the class
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "wmitest.h"

static
BOOL
NamesContain(SAFEARRAY *Names, const WCHAR *Name)
{
    LONG Lower = 0, Upper = -1, i;
    BOOL Found = FALSE;

    SafeArrayGetLBound(Names, 1, &Lower);
    SafeArrayGetUBound(Names, 1, &Upper);
    for (i = Lower; i <= Upper && !Found; i++)
    {
        BSTR Item = NULL;

        if (SUCCEEDED(SafeArrayGetElement(Names, &i, &Item)) && Item)
        {
            Found = !_wcsicmp(Item, Name);
            SysFreeString(Item);
        }
    }
    return Found;
}

START_TEST(Projection)
{
    IWbemServices *Services;
    IEnumWbemClassObject *Enum = NULL;
    IWbemClassObject *Object;
    BSTR Language = SysAllocString(L"WQL"), Query = SysAllocString(L"SELECT FreeSpace FROM Win32_LogicalDisk");
    ULONG Got, Instances = 0;
    HRESULT hr;

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
        SAFEARRAY *Names = NULL;
        VARIANT Value;
        CIMTYPE Type;

        VariantInit(&Value);
        hr = IWbemClassObject_Get(Object, L"DeviceID", 0, &Value, &Type, NULL);
        ok_hex(hr, S_OK);
        ok_long(V_VT(&Value), VT_BSTR);
        ok_long(Type, CIM_STRING);
        if (V_VT(&Value) == VT_BSTR)
            ok(V_BSTR(&Value) && V_BSTR(&Value)[0], "empty DeviceID\n");
        VariantClear(&Value);

        hr = IWbemClassObject_Get(Object, L"FreeSpace", 0, &Value, &Type, NULL);
        ok_hex(hr, S_OK);
        VariantClear(&Value);

        hr = IWbemClassObject_Get(Object, L"VolumeName", 0, &Value, &Type, NULL);
        ok_hex(hr, WBEM_E_NOT_FOUND);
        VariantClear(&Value);

        hr = IWbemClassObject_GetNames(Object, NULL, WBEM_FLAG_NONSYSTEM_ONLY, NULL, &Names);
        ok_hex(hr, S_OK);
        if (Names)
        {
            ok(NamesContain(Names, L"DeviceID"), "DeviceID not listed\n");
            ok(NamesContain(Names, L"FreeSpace"), "FreeSpace not listed\n");
            ok(!NamesContain(Names, L"VolumeName"), "VolumeName listed\n");
            SafeArrayDestroy(Names);
        }
        Instances++;
        IWbemClassObject_Release(Object);
    }
    ok(Instances > 0, "no logical disk\n");
    if (Enum)
        IEnumWbemClassObject_Release(Enum);
    IWbemServices_Release(Services);
    SysFreeString(Language);
    SysFreeString(Query);
}
