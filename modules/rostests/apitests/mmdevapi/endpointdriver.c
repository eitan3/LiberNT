/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test that an endpoint leads through its topology to the adapter devnode and its driver
 */

#include <apitest.h>

#define COBJMACROS
#include <windows.h>
#include <objbase.h>
#include <mmdeviceapi.h>
#include <cfgmgr32.h>
#include <initguid.h>
#include <devicetopology.h>
#include <devpkey.h>

static const PROPERTYKEY InstanceIdKey =
    { { 0x78c34fc8, 0x104a, 0x4aca, { 0x9e, 0xa4, 0x52, 0x4d, 0x52, 0x99, 0x6e, 0x57 } }, 256 };

static VOID
TestEndpoint(IMMDeviceEnumerator *Enumerator, IMMDevice *Device)
{
    IDeviceTopology *Topology = NULL;
    IConnector *Connector = NULL;
    IMMDevice *Filter = NULL;
    IPropertyStore *Store = NULL;
    LPWSTR FilterId = NULL;
    PROPVARIANT Value;
    DEVINST DevInst;
    DEVPROPTYPE Type;
    WCHAR Version[64];
    ULONG Size = sizeof(Version);
    HRESULT hr;

    hr = IMMDevice_Activate(Device, &IID_IDeviceTopology, CLSCTX_INPROC_SERVER, NULL, (void **)&Topology);
    ok(hr == S_OK, "Activate(IDeviceTopology) returned 0x%08lx\n", hr);
    if (FAILED(hr))
        return;
    hr = IDeviceTopology_GetConnector(Topology, 0, &Connector);
    ok(hr == S_OK, "GetConnector returned 0x%08lx\n", hr);
    if (SUCCEEDED(hr))
    {
        hr = IConnector_GetDeviceIdConnectedTo(Connector, &FilterId);
        ok(hr == S_OK, "GetDeviceIdConnectedTo returned 0x%08lx\n", hr);
        IConnector_Release(Connector);
    }
    IDeviceTopology_Release(Topology);
    if (!FilterId)
        return;

    hr = IMMDeviceEnumerator_GetDevice(Enumerator, FilterId, &Filter);
    ok(hr == S_OK, "GetDevice(%ls) returned 0x%08lx\n", FilterId, hr);
    CoTaskMemFree(FilterId);
    if (FAILED(hr))
        return;

    PropVariantInit(&Value);
    hr = IMMDevice_OpenPropertyStore(Filter, STGM_READ, &Store);
    ok(hr == S_OK, "OpenPropertyStore returned 0x%08lx\n", hr);
    if (SUCCEEDED(hr))
    {
        hr = IPropertyStore_GetValue(Store, &InstanceIdKey, &Value);
        ok(hr == S_OK && Value.vt == VT_LPWSTR, "instance ID: hr 0x%08lx, vt %u\n", hr, Value.vt);
        IPropertyStore_Release(Store);
    }
    IMMDevice_Release(Filter);
    if (Value.vt != VT_LPWSTR)
    {
        PropVariantClear(&Value);
        return;
    }

    ok(CM_Locate_DevNodeW(&DevInst, Value.pwszVal, CM_LOCATE_DEVNODE_NORMAL) == CR_SUCCESS,
       "devnode %ls is not present\n", Value.pwszVal);
    ok(CM_Get_DevNode_PropertyW(DevInst, &DEVPKEY_Device_DriverVersion, &Type, (PBYTE)Version, &Size, 0) == CR_SUCCESS &&
       Type == DEVPROP_TYPE_STRING && Version[0], "devnode %ls has no driver version\n", Value.pwszVal);
    PropVariantClear(&Value);
}

START_TEST(endpointdriver)
{
    IMMDeviceEnumerator *Enumerator = NULL;
    IMMDeviceCollection *Collection = NULL;
    IMMDevice *Device;
    UINT Count = 0, Index;
    HRESULT hr;

    CoInitializeEx(NULL, COINIT_MULTITHREADED);
    hr = CoCreateInstance(&CLSID_MMDeviceEnumerator, NULL, CLSCTX_INPROC_SERVER,
                          &IID_IMMDeviceEnumerator, (void **)&Enumerator);
    ok(hr == S_OK, "CoCreateInstance returned 0x%08lx\n", hr);
    if (SUCCEEDED(hr) &&
        SUCCEEDED(IMMDeviceEnumerator_EnumAudioEndpoints(Enumerator, eAll, DEVICE_STATE_ACTIVE, &Collection)))
    {
        IMMDeviceCollection_GetCount(Collection, &Count);
        if (!Count)
            skip("No active audio endpoint\n");
        for (Index = 0; Index < Count; Index++)
        {
            if (SUCCEEDED(IMMDeviceCollection_Item(Collection, Index, &Device)))
            {
                TestEndpoint(Enumerator, Device);
                IMMDevice_Release(Device);
            }
        }
        IMMDeviceCollection_Release(Collection);
    }
    if (Enumerator)
        IMMDeviceEnumerator_Release(Enumerator);
    CoUninitialize();
}
