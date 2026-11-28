/*
 * Copyright 2009 Maarten Lankhorst
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA
 */

#include <stdarg.h>
#include <wchar.h>

#define COBJMACROS
#include "windef.h"
#include "winbase.h"
#include "winnls.h"
#include "winreg.h"
#include "wine/debug.h"
#include "wine/list.h"

#ifndef __REACTOS__
#include "initguid.h"
#else
#include "cguid.h"
#endif
#include "ole2.h"
#include "mmdeviceapi.h"
#include "dshow.h"
#include "dsound.h"
#include "audioclient.h"
#include "endpointvolume.h"
#include "audiopolicy.h"
#include "spatialaudioclient.h"

#include "mmdevapi_private.h"
#include "devenum_notif.h"
#include "devpkey.h"
#ifdef __REACTOS__
#include <libernt/mmdevdefault.h>
#include "ks.h"
#endif

WINE_DEFAULT_DEBUG_CHANNEL(mmdevapi);

#ifndef __REACTOS__
DEFINE_GUID(GUID_NULL,0,0,0,0,0,0,0,0,0,0,0);
#endif

static HKEY key_render;
static HKEY key_capture;

typedef struct MMDevPropStoreImpl
{
    IPropertyStore IPropertyStore_iface;
    LONG ref;
    MMDevice *parent;
    DWORD access;
} MMDevPropStore;

typedef struct MMDevEnumImpl
{
    IMMDeviceEnumerator IMMDeviceEnumerator_iface;
    LONG ref;
} MMDevEnumImpl;

static MMDevice *MMDevice_def_rec, *MMDevice_def_play;
static const IMMDeviceEnumeratorVtbl MMDevEnumVtbl;
static const IMMDeviceCollectionVtbl MMDevColVtbl;
static const IMMDeviceVtbl MMDeviceVtbl;
static const IPropertyStoreVtbl MMDevPropVtbl;
static const IMMEndpointVtbl MMEndpointVtbl;

static MMDevEnumImpl enumerator;
static struct list device_list = LIST_INIT(device_list);

typedef struct MMDevColImpl
{
    IMMDeviceCollection IMMDeviceCollection_iface;
    LONG ref;
    IMMDevice **devices;
    UINT devices_count;
} MMDevColImpl;

typedef struct IPropertyBagImpl {
    IPropertyBag IPropertyBag_iface;
    GUID devguid;
} IPropertyBagImpl;

static const IPropertyBagVtbl PB_Vtbl;

typedef struct IConnectorImpl {
    IConnector IConnector_iface;
    LONG ref;
} IConnectorImpl;

typedef struct IDeviceTopologyImpl {
    IDeviceTopology IDeviceTopology_iface;
    LONG ref;
} IDeviceTopologyImpl;

static HRESULT MMDevPropStore_Create(MMDevice *This, DWORD access, IPropertyStore **ppv);

static inline MMDevPropStore *impl_from_IPropertyStore(IPropertyStore *iface)
{
    return CONTAINING_RECORD(iface, MMDevPropStore, IPropertyStore_iface);
}

static inline MMDevEnumImpl *impl_from_IMMDeviceEnumerator(IMMDeviceEnumerator *iface)
{
    return CONTAINING_RECORD(iface, MMDevEnumImpl, IMMDeviceEnumerator_iface);
}

static inline MMDevColImpl *impl_from_IMMDeviceCollection(IMMDeviceCollection *iface)
{
    return CONTAINING_RECORD(iface, MMDevColImpl, IMMDeviceCollection_iface);
}

static inline IPropertyBagImpl *impl_from_IPropertyBag(IPropertyBag *iface)
{
    return CONTAINING_RECORD(iface, IPropertyBagImpl, IPropertyBag_iface);
}

#ifdef __REACTOS__
/* The three device notifications of IMMNotificationClient, one per verdict of the diff, sent to the registered clients as notify_clients sends the default's (R7-01) */
static void notify_device(enum notif_change change, const MMDevice *device);
static void notify_clients(EDataFlow flow, ERole role, const WCHAR *id);
static HRESULT WINAPI MMDevEnum_GetDevice(IMMDeviceEnumerator *iface, const WCHAR *name, IMMDevice **device);
static inline MMDevice *impl_from_IMMDevice(IMMDevice *iface);
static CRITICAL_SECTION g_notif_lock;
#endif

#ifndef __REACTOS__
static HRESULT DeviceTopology_Create(IMMDevice *device, IDeviceTopology **ppv);

static inline IConnectorImpl *impl_from_IConnector(IConnector *iface)
{
    return CONTAINING_RECORD(iface, IConnectorImpl, IConnector_iface);
}

static inline IDeviceTopologyImpl *impl_from_IDeviceTopology(IDeviceTopology *iface)
{
    return CONTAINING_RECORD(iface, IDeviceTopologyImpl, IDeviceTopology_iface);
}
#endif

static const WCHAR propkey_formatW[] = L"{%08X-%04X-%04X-%02X%02X-%02X%02X%02X%02X%02X%02X},%d";

struct device
{
    struct list entry;
    GUID        guid;
    EDataFlow   flow;
    char        name[];
};

static struct list devices_cache = LIST_INIT( devices_cache );

static void add_device_to_cache( const GUID *guid, const char *name, EDataFlow flow )
{
    struct device *dev;

    if (!(dev = malloc( offsetof( struct device, name[strlen(name) + 1] )))) return;
    dev->guid = *guid;
    dev->flow = flow;
    strcpy( dev->name, name );
    list_add_tail( &devices_cache, &dev->entry );
}

static struct device *find_device_in_cache( const GUID *guid )
{
    struct device *dev;

    LIST_FOR_EACH_ENTRY( dev, &devices_cache, struct device, entry )
        if (IsEqualGUID( guid, &dev->guid )) return dev;
    return NULL;
}

/* A device name keeps its GUID, so the registry is read once per process for it */
static struct device *find_device_in_cache_by_name( const char *name, EDataFlow flow )
{
    struct device *dev;

    LIST_FOR_EACH_ENTRY( dev, &devices_cache, struct device, entry )
        if (dev->flow == flow && !strcmp( dev->name, name )) return dev;
    return NULL;
}

BOOL get_device_name_from_guid( const GUID *guid, char **name, EDataFlow *flow )
{
    struct device *dev;
    WCHAR key_name[MAX_PATH];
    DWORD index = 0;
    HKEY key;

    if ((dev = find_device_in_cache( guid )))
    {
        *name = strdup(dev->name);
        *flow = dev->flow;
        return TRUE;
    }

    swprintf( key_name, ARRAY_SIZE(key_name), L"Software\\Wine\\Drivers\\%s\\devices", drvs.module_name );
    if (RegOpenKeyExW( HKEY_CURRENT_USER, key_name, 0, KEY_READ | KEY_WOW64_64KEY, &key )) return FALSE;

    for (;;)
    {
        DWORD size, type;
        LSTATUS status;
        GUID reg_guid;
        HKEY dev_key;

        size = ARRAY_SIZE(key_name);
        if (RegEnumKeyExW( key, index++, key_name, &size, NULL, NULL, NULL, NULL )) break;
        if (RegOpenKeyExW( key, key_name, 0, KEY_READ | KEY_WOW64_64KEY, &dev_key )) continue;
        size = sizeof(reg_guid);
        status = RegQueryValueExW( dev_key, L"guid", 0, &type, (BYTE *)&reg_guid, &size );
        RegCloseKey(dev_key);
        if (status || type != REG_BINARY || size != sizeof(reg_guid)) continue;
        if (!IsEqualGUID( &reg_guid, guid )) continue;
        if (key_name[0] == '0') *flow = eRender;
        else if (key_name[0] == '1') *flow = eCapture;
        else continue;

        RegCloseKey( key );
        TRACE( "Found matching device key %s for %s\n", wine_dbgstr_w(key_name), debugstr_guid(guid) );
        size = WideCharToMultiByte( CP_UNIXCP, 0, key_name + 2, -1, NULL, 0, NULL, NULL );
        if (!(*name = malloc( size ))) return FALSE;
        WideCharToMultiByte( CP_UNIXCP, 0, key_name + 2, -1, *name, size, NULL, NULL );
        add_device_to_cache( guid, *name, *flow );
        return TRUE;
    }
    RegCloseKey( key );
    WARN( "No matching device in registry for %s\n", debugstr_guid(guid) );
    return FALSE;
}

static void hash_device_guid_bytes( ULONGLONG hashes[2], const void *data, SIZE_T size )
{
    const BYTE *bytes = data;

    while (size--)
    {
        hashes[0] ^= *bytes;
        hashes[0] *= 1099511628211ULL;
        hashes[1] ^= *bytes++;
        hashes[1] *= 14029467366897019727ULL;
        hashes[1] ^= hashes[1] >> 29;
    }
}

static void get_fallback_device_guid( EDataFlow flow, const char *dev_name, GUID *guid )
{
    ULONGLONG hashes[2] = {14695981039346656037ULL, 10723151780598845931ULL};

    hash_device_guid_bytes( hashes, drvs.module_name,
                            (wcslen( drvs.module_name ) + 1) * sizeof(WCHAR) );
    hash_device_guid_bytes( hashes, &flow, sizeof(flow) );
    hash_device_guid_bytes( hashes, dev_name, strlen( dev_name ) + 1 );
    memcpy( guid, hashes, sizeof(*guid) );

    /* Mark the stable, name-derived value as a custom UUID. */
    guid->Data3 = (guid->Data3 & 0x0fff) | 0x8000;
    guid->Data4[0] = (guid->Data4[0] & 0x3f) | 0x80;
}

static void get_device_guid( EDataFlow flow, const char *dev_name, GUID *guid )
{
    WCHAR name[512];
    DWORD type, size;
    HKEY key = NULL;
    LSTATUS status;
    int len;
    struct device *cached;

    if ((cached = find_device_in_cache_by_name( dev_name, flow )))
    {
        *guid = cached->guid;
        return;
    }

    /* Low-integrity audio clients may only read the endpoint registry.  Keep
     * a process-independent identity even when no value can be persisted. */
    get_fallback_device_guid( flow, dev_name, guid );

    len = swprintf( name, ARRAY_SIZE(name), L"Software\\Wine\\Drivers\\%s\\devices\\%u,",
                    drvs.module_name, flow == eCapture );
    if (len < 0 || len >= ARRAY_SIZE(name) ||
        !MultiByteToWideChar( CP_UNIXCP, 0, dev_name, -1, name + len, ARRAY_SIZE(name) - len ))
        goto done;

    status = RegOpenKeyExW( HKEY_CURRENT_USER, name, 0,
                            KEY_READ | KEY_WOW64_64KEY, &key );
    if (!status)
    {
        size = sizeof(*guid);
        status = RegQueryValueExW( key, L"guid", 0, &type, (BYTE *)guid, &size );
        RegCloseKey( key );
        key = NULL;
        if (!status && type == REG_BINARY && size == sizeof(*guid))
            goto done;

        get_fallback_device_guid( flow, dev_name, guid );
    }

    status = RegCreateKeyExW( HKEY_CURRENT_USER, name, 0, NULL, 0,
                              KEY_READ | KEY_WRITE | KEY_WOW64_64KEY, NULL, &key, NULL);
    if (!status)
    {
        GUID persistent_guid;

        size = sizeof(persistent_guid);
        status = RegQueryValueExW( key, L"guid", 0, &type, (BYTE *)&persistent_guid, &size );
        if (!status && type == REG_BINARY && size == sizeof(persistent_guid))
            *guid = persistent_guid;
        else
            RegSetValueExW( key, L"guid", 0, REG_BINARY, (BYTE *)guid, sizeof(*guid) );

        RegCloseKey( key );
    }
    else if (status != ERROR_ACCESS_DENIED)
    {
        WARN( "Failed to open or create key %s: %lu\n", debugstr_w(name), status );
    }

done:
    if (!find_device_in_cache( guid )) add_device_to_cache( guid, dev_name, flow );
}

static HRESULT MMDevPropStore_OpenPropKey(const GUID *guid, DWORD flow, REGSAM access, HKEY *propkey)
{
    WCHAR buffer[39];
    LONG ret;
    HKEY key, root = flow == eRender ? key_render : key_capture;

    if (!root)
        return E_FAIL;

    StringFromGUID2(guid, buffer, 39);
    if ((ret = RegOpenKeyExW(root, buffer, 0, access | KEY_WOW64_64KEY, &key)) != ERROR_SUCCESS)
    {
        WARN("Opening key %s failed with %lu\n", debugstr_w(buffer), ret);
        return E_FAIL;
    }
    ret = RegOpenKeyExW(key, L"Properties", 0, access | KEY_WOW64_64KEY, propkey);
    RegCloseKey(key);
    if (ret != ERROR_SUCCESS)
    {
        WARN("Opening key Properties failed with %lu\n", ret);
        return E_FAIL;
    }
    return S_OK;
}

static HRESULT MMDevice_GetPropValue(const GUID *devguid, DWORD flow, REFPROPERTYKEY key, PROPVARIANT *pv)
{
    WCHAR buffer[80];
    const GUID *id = &key->fmtid;
    DWORD type, size;
    HRESULT hr = S_OK;
    HKEY regkey;
    LONG ret;

    hr = MMDevPropStore_OpenPropKey(devguid, flow, KEY_READ, &regkey);
    if (FAILED(hr))
        return hr;
    wsprintfW( buffer, propkey_formatW, id->Data1, id->Data2, id->Data3,
               id->Data4[0], id->Data4[1], id->Data4[2], id->Data4[3],
               id->Data4[4], id->Data4[5], id->Data4[6], id->Data4[7], key->pid );
    ret = RegGetValueW(regkey, NULL, buffer, RRF_RT_ANY, &type, NULL, &size);
    if (ret != ERROR_SUCCESS)
    {
        WARN("Reading %s returned %ld\n", debugstr_w(buffer), ret);
        RegCloseKey(regkey);
        pv->vt = VT_EMPTY;
        return S_OK;
    }

    switch (type)
    {
        case REG_SZ:
        {
            pv->vt = VT_LPWSTR;
            pv->pwszVal = CoTaskMemAlloc(size);
            if (!pv->pwszVal)
                hr = E_OUTOFMEMORY;
            else
                RegGetValueW(regkey, NULL, buffer, RRF_RT_REG_SZ, NULL, (BYTE*)pv->pwszVal, &size);
            break;
        }
        case REG_DWORD:
        {
            pv->vt = VT_UI4;
            RegGetValueW(regkey, NULL, buffer, RRF_RT_REG_DWORD, NULL, (BYTE*)&pv->ulVal, &size);
            break;
        }
        case REG_BINARY:
        {
            pv->vt = VT_BLOB;
            pv->blob.cbSize = size;
            pv->blob.pBlobData = CoTaskMemAlloc(size);
            if (!pv->blob.pBlobData)
                hr = E_OUTOFMEMORY;
            else
                RegGetValueW(regkey, NULL, buffer, RRF_RT_REG_BINARY, NULL, (BYTE*)pv->blob.pBlobData, &size);
            break;
        }
        default:
            ERR("Unknown/unhandled type: %lu\n", type);
            PropVariantClear(pv);
            break;
    }
    RegCloseKey(regkey);
    return hr;
}

static HRESULT MMDevice_SetPropValue(const GUID *devguid, DWORD flow, REFPROPERTYKEY key, REFPROPVARIANT pv)
{
    WCHAR buffer[80];
    const GUID *id = &key->fmtid;
    HRESULT hr;
    HKEY regkey;
    LONG ret;

    hr = MMDevPropStore_OpenPropKey(devguid, flow, KEY_READ | KEY_WRITE, &regkey);
    if (FAILED(hr))
        return hr;
    wsprintfW( buffer, propkey_formatW, id->Data1, id->Data2, id->Data3,
               id->Data4[0], id->Data4[1], id->Data4[2], id->Data4[3],
               id->Data4[4], id->Data4[5], id->Data4[6], id->Data4[7], key->pid );
    switch (pv->vt)
    {
        case VT_EMPTY:
        {
            ret = RegDeleteValueW(regkey, buffer);
            if (ret == ERROR_FILE_NOT_FOUND)
                ret = ERROR_SUCCESS;
            break;
        }
        case VT_UI4:
        {
            ret = RegSetValueExW(regkey, buffer, 0, REG_DWORD, (const BYTE*)&pv->ulVal, sizeof(DWORD));
            break;
        }
        case VT_BLOB:
        {
            ret = RegSetValueExW(regkey, buffer, 0, REG_BINARY, pv->blob.pBlobData, pv->blob.cbSize);
            TRACE("Blob %p %lu\n", pv->blob.pBlobData, pv->blob.cbSize);

            break;
        }
        case VT_LPWSTR:
        {
            ret = RegSetValueExW(regkey, buffer, 0, REG_SZ, (const BYTE*)pv->pwszVal, sizeof(WCHAR)*(1+lstrlenW(pv->pwszVal)));
            break;
        }
        default:
            ret = 0;
            FIXME("Unhandled type %u\n", pv->vt);
            hr = E_INVALIDARG;
            break;
    }
    RegCloseKey(regkey);
    TRACE("Writing %s returned %lu\n", debugstr_w(buffer), ret);
    if (SUCCEEDED(hr) && ret)
        hr = HRESULT_FROM_WIN32(ret);
    return hr;
}

static HRESULT set_driver_prop_value(GUID *id, const EDataFlow flow, const PROPERTYKEY *prop)
{
    struct get_prop_value_params params;
    PROPVARIANT pv;
    char *dev_name;
    unsigned int size = 0;

    TRACE("%s, (%s,%lu)\n", wine_dbgstr_guid(id), wine_dbgstr_guid(&prop->fmtid), prop->pid);

    if (!get_device_name_from_guid(id, &dev_name, &params.flow))
        return E_FAIL;

    params.device      = dev_name;
    params.guid        = id;
    params.prop        = prop;
    params.value       = &pv;
    params.buffer      = NULL;
    params.buffer_size = &size;

    while (1) {
#ifdef __REACTOS__
        reactos_mmdevapi_call(get_prop_value, &params);
#else
        __wine_unix_call(drvs.module_unixlib, get_prop_value, &params);
#endif

        if (params.result != E_NOT_SUFFICIENT_BUFFER)
            break;

        CoTaskMemFree(params.buffer);
        params.buffer = CoTaskMemAlloc(*params.buffer_size);
        if (!params.buffer) {
            free(dev_name);
            return E_OUTOFMEMORY;
        }
    }

    if (FAILED(params.result))
        CoTaskMemFree(params.buffer);

    free(dev_name);

    if (SUCCEEDED(params.result)) {
        MMDevice_SetPropValue(id, flow, prop, &pv);
        PropVariantClear(&pv);
    }

    return params.result;
}

struct product_name_overrides
{
    const WCHAR *id;
    const WCHAR *product;
};

static const struct product_name_overrides product_name_overrides[] =
{
    /* Sony controllers */
    { .id = L"VID_054C&PID_0CE6", .product = L"Wireless Controller" },
    { .id = L"VID_054C&PID_0DF2", .product = L"Wireless Controller" },
};

static const WCHAR *find_product_name_override(const WCHAR *device_id)
{
    const WCHAR *match_id = wcschr( device_id, '\\' ) + 1;
    DWORD i;

    for (i = 0; i < ARRAY_SIZE(product_name_overrides); ++i)
        if (!wcsnicmp( product_name_overrides[i].id, match_id, 17 ))
            return product_name_overrides[i].product;

    return NULL;
}

/* Creates or updates the state of a device
 * If GUID is null, a random guid will be assigned
 * and the device will be created
 */
/* The endpoint's id as IMMDevice_GetId hands it out */
static void MMDevice_FormatId(const MMDevice *This, WCHAR *str)
{
    const GUID *id = &This->devguid;

    wsprintfW(str, L"{0.0.%u.00000000}.{%08X-%04X-%04X-%02X%02X-%02X%02X%02X%02X%02X%02X}",
              This->flow, id->Data1, id->Data2, id->Data3,
              id->Data4[0], id->Data4[1], id->Data4[2], id->Data4[3],
              id->Data4[4], id->Data4[5], id->Data4[6], id->Data4[7]);
}

static MMDevice *MMDevice_Create(const WCHAR *name, GUID *id, EDataFlow flow, DWORD state, BOOL setdefault)
{
    HKEY key, root;
    MMDevice *device, *cur = NULL;
    WCHAR guidstr[39];

    static const PROPERTYKEY deviceinterface_key = {
        {0x233164c8, 0x1b2c, 0x4c7d, {0xbc, 0x68, 0xb6, 0x71, 0x68, 0x7a, 0x25, 0x67}}, 1
    };

    static const PROPERTYKEY devicepath_key = {
        {0xb3f8fa53, 0x0004, 0x438e, {0x90, 0x03, 0x51, 0xa4, 0x6e, 0x13, 0x9b, 0xfc}}, 2
    };

    LIST_FOR_EACH_ENTRY(device, &device_list, MMDevice, entry)
    {
        if (device->flow == flow && IsEqualGUID(&device->devguid, id)){
            cur = device;
            break;
        }
    }

    if(!cur){
        /* No device found, allocate new one */
        cur = calloc(1, sizeof(*cur));
        if (!cur)
            return NULL;

        cur->IMMDevice_iface.lpVtbl = &MMDeviceVtbl;
        cur->IMMEndpoint_iface.lpVtbl = &MMEndpointVtbl;

        list_add_tail(&device_list, &cur->entry);
    }else if(cur->ref > 0)
        WARN("Modifying an MMDevice with postitive reference count!\n");

    free(cur->drv_id);
    cur->drv_id = wcsdup(name);

    cur->flow = flow;
    cur->state = state;
    cur->devguid = *id;
    cur->named = TRUE;

    StringFromGUID2(&cur->devguid, guidstr, ARRAY_SIZE(guidstr));

    if (flow == eRender)
        root = key_render;
    else
        root = key_capture;

    if (root && RegCreateKeyExW(root, guidstr, 0, NULL, 0,
                               KEY_WRITE | KEY_READ | KEY_WOW64_64KEY,
                               NULL, &key, NULL) == ERROR_SUCCESS)
    {
        HKEY keyprop;
        RegSetValueExW(key, L"DeviceState", 0, REG_DWORD, (const BYTE*)&state, sizeof(DWORD));
        if (!RegCreateKeyExW(key, L"Properties", 0, NULL, 0, KEY_WRITE|KEY_READ|KEY_WOW64_64KEY, NULL, &keyprop, NULL))
        {
            PROPVARIANT pv;
            WCHAR *type;
            DWORD len;

            pv.vt = VT_LPWSTR;
            pv.pwszVal = cur->drv_id;

            if (SUCCEEDED(set_driver_prop_value(id, flow, &devicepath_key))) {
                PROPVARIANT pv2;

                PropVariantInit(&pv2);

                if (SUCCEEDED(MMDevice_GetPropValue(id, flow, &devicepath_key, &pv2)) && pv2.vt == VT_LPWSTR) {
                    const WCHAR *override;
                    if ((override = find_product_name_override(pv2.pwszVal)) != NULL)
                        pv.pwszVal = (WCHAR*) override;
                }

                PropVariantClear(&pv2);
            }

            MMDevice_SetPropValue(id, flow, (const PROPERTYKEY*)&DEVPKEY_DeviceInterface_FriendlyName, &pv);

            pv.pwszVal = type = (WCHAR *)(flow == eCapture ? L"Microphone" : L"Speakers");
            MMDevice_SetPropValue(id, flow, (const PROPERTYKEY*)&DEVPKEY_Device_DeviceDesc, &pv);

            {
                MMDevice *other;
                unsigned int dup = 0;
                LIST_FOR_EACH_ENTRY(other, &device_list, MMDevice, entry)
                {
                    if (other == cur)
                        break;
                    if (other->flow == flow && other->drv_id && !wcscmp(other->drv_id, cur->drv_id) &&
                        other->state != DEVICE_STATE_NOTPRESENT)
                        dup++;
                }
                len = (wcslen(type) + wcslen(cur->drv_id) + wcslen(L" (99- )") + 1);
                pv.vt = VT_LPWSTR;
                pv.pwszVal = CoTaskMemAlloc(len * sizeof(WCHAR));
                if (dup)
                    swprintf(pv.pwszVal, len, L"%ls (%u- %ls)", type, dup + 1, cur->drv_id);
                else
                    swprintf(pv.pwszVal, len, L"%ls (%ls)", type, cur->drv_id);
                MMDevice_SetPropValue(id, flow, (const PROPERTYKEY*)&DEVPKEY_Device_FriendlyName, &pv);
                CoTaskMemFree(pv.pwszVal);
            }

            pv.pwszVal = guidstr;
            MMDevice_SetPropValue(id, flow, &deviceinterface_key, &pv);

            if (FAILED(set_driver_prop_value(id, flow, &PKEY_AudioEndpoint_FormFactor)))
            {
                pv.vt = VT_UI4;
                pv.ulVal = (flow == eCapture) ? Microphone : Speakers;

                MMDevice_SetPropValue(id, flow, &PKEY_AudioEndpoint_FormFactor, &pv);
            }

            if (flow != eCapture)
            {
                PROPVARIANT pv2;

                PropVariantInit(&pv2);

                /* make read-write by not overwriting if already set */
                if (FAILED(MMDevice_GetPropValue(id, flow, &PKEY_AudioEndpoint_PhysicalSpeakers, &pv2)) || pv2.vt != VT_UI4)
                    set_driver_prop_value(id, flow, &PKEY_AudioEndpoint_PhysicalSpeakers);

                PropVariantClear(&pv2);
            }

            pv.vt = VT_LPWSTR;
            pv.pwszVal = drvs.module_name;

            MMDevice_SetPropValue(id, flow, (const PROPERTYKEY*)&DEVPKEY_Device_Driver, &pv);

            RegCloseKey(keyprop);
        }
        RegCloseKey(key);
    }

    if (setdefault)
    {
        if (flow == eRender)
            MMDevice_def_play = cur;
        else
            MMDevice_def_rec = cur;
    }
    return cur;
}

static LONG open_audio_registry_key(HKEY parent, const WCHAR *name, HKEY *key)
{
    LONG ret;

    *key = NULL;
    ret = RegCreateKeyExW(parent, name, 0, NULL, 0,
                          KEY_READ | KEY_WRITE | KEY_WOW64_64KEY,
                          NULL, key, NULL);
    if (ret != ERROR_SUCCESS)
        ret = RegOpenKeyExW(parent, name, 0, KEY_READ | KEY_WOW64_64KEY, key);
    return ret;
}

HRESULT load_devices_from_reg(void)
{
    DWORD i = 0;
    HKEY root = NULL, cur;
    LONG ret;
    DWORD curflow;

    key_capture = key_render = NULL;
    ret = open_audio_registry_key(HKEY_LOCAL_MACHINE,
            L"Software\\Microsoft\\Windows\\CurrentVersion\\MMDevices\\Audio", &root);
    if (ret == ERROR_SUCCESS)
        ret = open_audio_registry_key(root, L"Capture", &key_capture);
    if (ret == ERROR_SUCCESS)
        ret = open_audio_registry_key(root, L"Render", &key_render);
    if (root)
        RegCloseKey(root);
    if (ret != ERROR_SUCCESS)
    {
        if (key_capture)
            RegCloseKey(key_capture);
        if (key_render)
            RegCloseKey(key_render);
        key_render = key_capture = NULL;
        WARN("Couldn't open MMDevice registry: %lu\n", ret);
        return E_FAIL;
    }

    cur = key_capture;
    curflow = eCapture;

    do {
        WCHAR guidvalue[39];
        GUID guid;
        DWORD len;
        PROPVARIANT pv = { VT_EMPTY };

        len = ARRAY_SIZE(guidvalue);
        ret = RegEnumKeyExW(cur, i++, guidvalue, &len, NULL, NULL, NULL, NULL);
        if (ret == ERROR_NO_MORE_ITEMS)
        {
            if (cur == key_capture)
            {
                cur = key_render;
                curflow = eRender;
                i = 0;
                continue;
            }
            break;
        }
        if (ret != ERROR_SUCCESS)
            continue;
        if (SUCCEEDED(CLSIDFromString(guidvalue, &guid))
            && SUCCEEDED(MMDevice_GetPropValue(&guid, curflow, (const PROPERTYKEY*)&DEVPKEY_Device_FriendlyName, &pv))
            && pv.vt == VT_LPWSTR)
        {
            MMDevice_Create(pv.pwszVal, &guid, curflow,
                    DEVICE_STATE_NOTPRESENT, FALSE);
            CoTaskMemFree(pv.pwszVal);
        }
    } while (1);

    return S_OK;
}

#ifdef __REACTOS__
static BOOL is_pcm_blob(const PROPVARIANT *pv)
{
    const WAVEFORMATEXTENSIBLE *ext = (const WAVEFORMATEXTENSIBLE *)pv->blob.pBlobData;

    if (pv->vt != VT_BLOB || pv->blob.cbSize < sizeof(WAVEFORMATEX))
        return FALSE;
    if (ext->Format.wFormatTag == WAVE_FORMAT_PCM)
        return TRUE;
    return ext->Format.wFormatTag == WAVE_FORMAT_EXTENSIBLE && pv->blob.cbSize >= sizeof(*ext) &&
           IsEqualGUID(&ext->SubFormat, &KSDATAFORMAT_SUBTYPE_PCM);
}

/*
 * The format the user chose for an endpoint: the per-user value a standard
 * user can write (MMDevGetDeviceFormat) comes before the device property
 * (PKEY_AudioEngine_DeviceFormat), which needs administrator rights.
 */
BOOL reactos_user_device_format(EDataFlow flow, const char *device, WAVEFORMATEXTENSIBLE *fmt)
{
    PROPVARIANT pv = { VT_EMPTY };
    WCHAR guidstr[39], endpoint_id[64];
    GUID guid;
    BOOL ret = FALSE;

    get_device_guid(flow, device, &guid);
    StringFromGUID2(&guid, guidstr, ARRAY_SIZE(guidstr));
    swprintf(endpoint_id, ARRAY_SIZE(endpoint_id), L"{0.0.%u.00000000}.%ls", (unsigned)flow, guidstr);
    if (MMDevGetDeviceFormat(endpoint_id, fmt) == ERROR_SUCCESS)
        return TRUE;
    if (SUCCEEDED(MMDevice_GetPropValue(&guid, flow, &PKEY_AudioEngine_DeviceFormat, &pv)) &&
        is_pcm_blob(&pv))
    {
        ZeroMemory(fmt, sizeof(*fmt));
        memcpy(fmt, pv.blob.pBlobData, min(pv.blob.cbSize, sizeof(*fmt)));
        ret = TRUE;
    }
    PropVariantClear(&pv);
    return ret;
}

/*
 * Windows keeps the driver's default format in PKEY_AudioEngine_OEMFormat and
 * the format the endpoint runs at in PKEY_AudioEngine_DeviceFormat, which the
 * user changes on the device's Advanced page: only set it when it holds no PCM
 * format (missing, or the float mix format earlier builds stored there).
 */
static HRESULT set_format(MMDevice *dev)
{
    WAVEFORMATEXTENSIBLE fmt;
    PROPVARIANT pv = { VT_EMPTY }, current;
    char *name;
    EDataFlow flow;
    BOOL found;

    if (!get_device_name_from_guid(&dev->devguid, &name, &flow))
        return E_FAIL;
    found = reactos_driver_device_format(name, flow, &fmt);
    free(name);
    if (!found)
        return E_FAIL;

    pv.vt = VT_BLOB;
    pv.blob.cbSize = sizeof(fmt);
    pv.blob.pBlobData = (BYTE *)&fmt;
    MMDevice_SetPropValue(&dev->devguid, dev->flow, &PKEY_AudioEngine_OEMFormat, &pv);

    PropVariantInit(&current);
    if (SUCCEEDED(MMDevice_GetPropValue(&dev->devguid, dev->flow, &PKEY_AudioEngine_DeviceFormat, &current)) &&
        !is_pcm_blob(&current))
    {
        MMDevice_SetPropValue(&dev->devguid, dev->flow, &PKEY_AudioEngine_DeviceFormat, &pv);
    }
    PropVariantClear(&current);
    return S_OK;
}
#else
static HRESULT set_format(MMDevice *dev)
{
    HRESULT hr;
    IAudioClient *client;
    WAVEFORMATEX *fmt;
    PROPVARIANT pv = { VT_EMPTY };

    hr = AudioClient_Create(&dev->devguid, &dev->IMMDevice_iface, &client);
    if(FAILED(hr))
        return hr;

    hr = IAudioClient_GetMixFormat(client, &fmt);
    if(FAILED(hr)){
        IAudioClient_Release(client);
        return hr;
    }

    IAudioClient_Release(client);

    if (fmt->wFormatTag == WAVE_FORMAT_EXTENSIBLE &&
        fmt->cbSize >= sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX))
    {
        WAVEFORMATEXTENSIBLE *fmtex = (WAVEFORMATEXTENSIBLE *)fmt;
        if (IsEqualGUID(&fmtex->SubFormat, &KSDATAFORMAT_SUBTYPE_IEEE_FLOAT))
        {
            fmtex->SubFormat = KSDATAFORMAT_SUBTYPE_PCM;
            fmtex->Format.wBitsPerSample = 16;
            fmtex->Samples.wValidBitsPerSample = 16;
            fmtex->Format.nBlockAlign = fmtex->Format.nChannels * (fmtex->Format.wBitsPerSample / 8);
            fmtex->Format.nAvgBytesPerSec = fmtex->Format.nSamplesPerSec * fmtex->Format.nBlockAlign;
        }
    }

    pv.vt = VT_BLOB;
    pv.blob.cbSize = sizeof(WAVEFORMATEX) + fmt->cbSize;
    pv.blob.pBlobData = (BYTE*)fmt;
    MMDevice_SetPropValue(&dev->devguid, dev->flow,
            &PKEY_AudioEngine_DeviceFormat, &pv);
    MMDevice_SetPropValue(&dev->devguid, dev->flow,
            &PKEY_AudioEngine_OEMFormat, &pv);
    CoTaskMemFree(fmt);

    return S_OK;
}
#endif

HRESULT load_driver_devices(EDataFlow flow)
{
    struct get_endpoint_ids_params params;
    UINT i;

    params.flow = flow;
    params.size = 1024;
    params.endpoints = NULL;
    do {
        free(params.endpoints);
        params.endpoints = malloc(params.size);
#ifdef __REACTOS__
        reactos_mmdevapi_call(get_endpoint_ids, &params);
#else
        __wine_unix_call(drvs.module_unixlib, get_endpoint_ids, &params);
#endif
    } while (params.result == HRESULT_FROM_WIN32(ERROR_INSUFFICIENT_BUFFER));

    if (FAILED(params.result))
        goto end;

    if (flow == eRender)
        MMDevice_def_play = NULL;
    else
        MMDevice_def_rec = NULL;

    for (i = 0; i < params.num; i++) {
        GUID guid;
        MMDevice *dev;
        DWORD state;
        const WCHAR *name = (WCHAR *)((char *)params.endpoints + params.endpoints[i].name);
        const char *dev_name = (char *)params.endpoints + params.endpoints[i].device;

        get_device_guid( flow, dev_name, &guid );

#ifdef __REACTOS__
        state = params.endpoints[i].state;
#else
        state = DEVICE_STATE_ACTIVE;
#endif
        dev = MMDevice_Create(name, &guid, flow, state, params.default_idx == i);
        if (dev)
            set_format(dev);
    }

end:
    free(params.endpoints);

    return params.result;
}

#ifdef __REACTOS__
/* The refresh of the device list, one at a time: a client asking and the jack watcher may both ask the driver. The lock is recursive: a client told of a change under it may ask again */
static CRITICAL_SECTION g_refresh_lock;
static CRITICAL_SECTION_DEBUG g_refresh_lock_debug =
{
    0, 0, &g_refresh_lock,
    { &g_refresh_lock_debug.ProcessLocksList, &g_refresh_lock_debug.ProcessLocksList },
      0, 0, { (DWORD_PTR)(__FILE__ ": g_refresh_lock") }
};
static CRITICAL_SECTION g_refresh_lock = { &g_refresh_lock_debug, -1, 0, 0, 0, 0 };

/* The devices of the flows as the answer left them, in the list's order: the diff's Next (devenum_notif.h), with the list as it was, for the state a change had */
struct refresh_iter
{
    struct list *at;
    EDataFlow first, last;
    const struct notif_before *before;
    UINT count;
};

static BOOLEAN refresh_next(void *context, struct notif_after *after)
{
    struct refresh_iter *iter = context;
    MMDevice *device;

    while (iter->at != &device_list)
    {
        device = LIST_ENTRY(iter->at, MMDevice, entry);
        iter->at = iter->at->next;
        if (device->flow < iter->first || device->flow > iter->last)
            continue;
        after->id = &device->devguid;
        after->state = device->state;
        after->named = device->named;
        after->device = device;
        return TRUE;
    }
    return FALSE;
}

/* The diff's verdict on a device told to the clients: a removed one is not present from now on, and a changed state is told in the log with its name (LEG-1) */
static void refresh_tell(enum notif_change change, struct notif_after *after, void *context)
{
    struct refresh_iter *iter = context;
    MMDevice *device = after->device;

    if (change == NOTIF_STATE && iter->before)
    {
        WCHAR id[56];

        MMDevice_FormatId(device, id);
        WARN("endpoint state: %s (%s) %#lx -> %#lx\n", wine_dbgstr_w(id), wine_dbgstr_w(device->drv_id),
             notif_before_state(iter->before, iter->count, after->id), after->state);
    }

    if (change == NOTIF_REMOVED)
        device->state = DEVICE_STATE_NOTPRESENT;
    notify_device(change, device);
}

/* The devices of one flow in the list's order, as they are now */
struct flow_iter
{
    struct list *at;
    EDataFlow flow;
};

static BOOLEAN flow_next(void *context, struct notif_after *after)
{
    struct flow_iter *iter = context;
    MMDevice *device;

    while (iter->at != &device_list)
    {
        device = LIST_ENTRY(iter->at, MMDevice, entry);
        iter->at = iter->at->next;
        if (device->flow != iter->flow)
            continue;
        after->id = &device->devguid;
        after->state = device->state;
        after->named = TRUE;
        after->device = device;
        return TRUE;
    }
    return FALSE;
}

/* The device an endpoint id names, as GetDevice finds it, without a reference kept */
static MMDevice *device_from_id(const WCHAR *id)
{
    IMMDevice *device;
    MMDevice *found;

    if (FAILED(MMDevEnum_GetDevice(&enumerator.IMMDeviceEnumerator_iface, id, &device)))
        return NULL;
    found = impl_from_IMMDevice(device);
    IMMDevice_Release(device);
    return found;
}

/*
 * The device that is a flow's default for a role (LEG-1): what the user chose
 * for the role, then what the user chose for the flow, then the driver's
 * default, the first of them that is active, else the first active device of
 * the flow, as GetDefaultAudioEndpoint hands it out. NULL when no device of
 * the flow is active.
 */
static MMDevice *pick_default_device(EDataFlow flow, ERole role)
{
    WCHAR reg_key[256], def_id[64];
    GUID preferred[3];
    struct flow_iter iter;
    MMDevice *device, *driver_default;
    DWORD size;
    UINT count = 0, i;
    HKEY key;

    if (!drvs.module_name[0])
        return NULL;

    lstrcpyW(reg_key, drv_keyW);
    lstrcatW(reg_key, L"\\");
    lstrcatW(reg_key, drvs.module_name);

    if (RegOpenKeyW(HKEY_CURRENT_USER, reg_key, &key) == ERROR_SUCCESS)
    {
        const WCHAR *names[2];
        UINT names_count = 0;

        if (role == eCommunications)
            names[names_count++] = flow == eRender ? L"DefaultVoiceOutput" : L"DefaultVoiceInput";
        names[names_count++] = flow == eRender ? L"DefaultOutput" : L"DefaultInput";

        for (i = 0; i < names_count; i++)
        {
            size = sizeof(def_id);
            if (RegQueryValueExW(key, names[i], 0, NULL, (BYTE *)def_id, &size) != ERROR_SUCCESS)
                continue;
            def_id[ARRAY_SIZE(def_id) - 1] = 0;
            if ((device = device_from_id(def_id)))
                preferred[count++] = device->devguid;
        }
        RegCloseKey(key);
    }

    driver_default = flow == eRender ? MMDevice_def_play : MMDevice_def_rec;
    if (driver_default)
        preferred[count++] = driver_default->devguid;

    iter.at = device_list.next;
    iter.flow = flow;
    return notif_default_pick(flow_next, &iter, preferred, count);
}

/* The roles a default is told for: eMultimedia follows eConsole, as notify_clients tells it */
static const ERole default_roles[2] = {eConsole, eCommunications};

/* The defaults of two flows and two roles, by flow and by role, as they were or are */
struct default_snapshot
{
    GUID id[2][2];
    BOOLEAN has[2][2];
};

static void default_snapshot_take(struct default_snapshot *snapshot, EDataFlow first, EDataFlow last)
{
    MMDevice *device;
    UINT flow, role;

    for (flow = first; flow <= last; flow++)
        for (role = 0; role < 2; role++)
        {
            device = pick_default_device(flow, default_roles[role]);
            snapshot->has[flow][role] = device != NULL;
            if (device)
                snapshot->id[flow][role] = device->devguid;
        }
}

/* The defaults the answer moved are told to the clients (LEG-1), with the device that is the default now, or NULL when none is; the caller holds g_notif_lock */
static void default_snapshot_tell(const struct default_snapshot *snapshot, EDataFlow first, EDataFlow last)
{
    MMDevice *device;
    WCHAR id[56];
    UINT flow, role;

    for (flow = first; flow <= last; flow++)
        for (role = 0; role < 2; role++)
        {
            device = pick_default_device(flow, default_roles[role]);
            if ((device != NULL) == snapshot->has[flow][role] &&
                (!device || IsEqualGUID(&device->devguid, &snapshot->id[flow][role])))
                continue;
            if (device)
                MMDevice_FormatId(device, id);
            WARN("endpoint state: the default of flow %u role %u is now %s\n", flow, default_roles[role],
                 device ? wine_dbgstr_w(id) : "(none)");
            notify_clients(flow, default_roles[role], device ? id : NULL);
        }
}

/*
 * The driver asked again for a flow's endpoints, and what its answer changed
 * told to the registered clients (R7-01): the list as it was is taken down
 * first and no device is named; load_driver_devices names what the answer
 * holds, with its state. A device the answer no longer names is not present
 * and was removed; one it names that was not there, or was not present, was
 * added; one whose state changed has the new state. An unchanged answer tells
 * nothing. A default the answer moved, as a jack that was unplugged or plugged
 * moves it (LEG-1), is told too. The clients are told under g_notif_lock, as
 * the default's change is. The diff itself is devenum_notif.h's, where the host
 * test runs it, and so is the default's rule.
 */
static void reactos_refresh_devices(EDataFlow flow)
{
    struct notif_before *before;
    struct default_snapshot snapshot;
    MMDevice *device;
    EDataFlow first, last;
    struct refresh_iter iter;
    UINT count = 0, i;

    if (!drvs.module && !reactos_ensure_driver())
        return;

    if (flow == eRender || flow == eCapture)
        first = last = flow;
    else
    {
        first = eRender;
        last = eCapture;
    }

    EnterCriticalSection(&g_refresh_lock);

    default_snapshot_take(&snapshot, first, last);

    LIST_FOR_EACH_ENTRY(device, &device_list, MMDevice, entry)
    {
        if (device->flow < first || device->flow > last)
            continue;
        count++;
        device->named = FALSE;
    }
    before = malloc(count * sizeof(*before));
    i = 0;
    LIST_FOR_EACH_ENTRY(device, &device_list, MMDevice, entry)
    {
        if (device->flow < first || device->flow > last)
            continue;
        if (before)
        {
            before[i].id = device->devguid;
            before[i].state = device->state;
        }
        i++;
    }

    load_driver_devices(first);
    if (last != first)
        load_driver_devices(last);

    /* without the list as it was, nothing can be told to have changed */
    if (before)
    {
        EnterCriticalSection(&g_notif_lock);

        iter.at = device_list.next;
        iter.first = first;
        iter.last = last;
        iter.before = before;
        iter.count = count;
        notif_diff_run(before, count, refresh_next, refresh_tell, &iter);
        default_snapshot_tell(&snapshot, first, last);

        LeaveCriticalSection(&g_notif_lock);
        free(before);
    }
    LeaveCriticalSection(&g_refresh_lock);
}

/*
 * The jack watcher (LEG-1). The HDAudio driver raises
 * KSEVENT_PINCAPS_JACKINFOCHANGE on an endpoint's filter when a jack's
 * presence changes. The watcher enables it, with an event of its own, on the
 * wave filter of every endpoint that takes it, waits for any of the events,
 * and asks the driver for the endpoints again: reactos_refresh_devices tells
 * the registered clients the states and the defaults that changed, as a
 * client's own enumeration would. The events are enabled again after each
 * answer, since the endpoints it names may differ, and after a timeout of the
 * bounded wait when the endpoints changed; with nothing to wait for it looks
 * again later.
 */
#define JACK_WATCH_MAX       32
#define JACK_WATCH_IDLE_MS   30000

struct jack_watch
{
    HANDLE filters[JACK_WATCH_MAX];
    HANDLE events[JACK_WATCH_MAX];
    UINT count;
    /* the endpoints the events were armed for */
    GUID endpoints[JACK_WATCH_MAX];
    UINT endpoint_count;
};

/* The endpoints there are now, at most JACK_WATCH_MAX */
static UINT jack_endpoints(GUID *endpoints)
{
    MMDevice *device;
    UINT count = 0;

    EnterCriticalSection(&g_refresh_lock);
    LIST_FOR_EACH_ENTRY(device, &device_list, MMDevice, entry)
    {
        if (count < JACK_WATCH_MAX)
            endpoints[count++] = device->devguid;
    }
    LeaveCriticalSection(&g_refresh_lock);
    return count;
}

/* Whether the endpoints are not the set the events were armed for: an endpoint that came since is armed when the watcher arms again */
static BOOL jack_watch_stale(const struct jack_watch *watch)
{
    GUID endpoints[JACK_WATCH_MAX];
    UINT count, i;

    count = jack_endpoints(endpoints);
    if (count != watch->endpoint_count)
        return TRUE;
    for (i = 0; i < count; i++)
        if (!IsEqualGUID(&endpoints[i], &watch->endpoints[i]))
            return TRUE;
    return FALSE;
}

static void jack_watch_disarm(struct jack_watch *watch)
{
    UINT i;

    for (i = 0; i < watch->count; i++)
    {
        /* an event a file enabled goes with the file's closing */
        CloseHandle(watch->filters[i]);
        CloseHandle(watch->events[i]);
    }
    watch->count = 0;
}

static void jack_watch_arm(struct jack_watch *watch)
{
    static const GUID pin_caps_change = {STATIC_KSEVENTSETID_PinCapsChange};
    KSEVENT event;
    UINT count, i;

    jack_watch_disarm(watch);
    count = jack_endpoints(watch->endpoints);
    watch->endpoint_count = count;

    event.Set = pin_caps_change;
    event.Id = KSEVENT_PINCAPS_JACKINFOCHANGE;
    event.Flags = KSEVENT_TYPE_ENABLE;

    for (i = 0; i < count; i++)
    {
        KSEVENTDATA data;
        WCHAR *path;
        HANDLE filter, signal;
        DWORD bytes;
        ULONG pin;

        if (FAILED(reactos_wave_connector(&watch->endpoints[i], &path, &pin)))
            continue;
        filter = CreateFileW(path, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
        free(path);
        if (filter == INVALID_HANDLE_VALUE)
            continue;
        if (!(signal = CreateEventW(NULL, FALSE, FALSE, NULL)))
        {
            CloseHandle(filter);
            continue;
        }

        ZeroMemory(&data, sizeof(data));
        data.NotificationType = KSEVENTF_EVENT_HANDLE;
        data.EventHandle.Event = signal;
        if (DeviceIoControl(filter, IOCTL_KS_ENABLE_EVENT, &event, sizeof(event), &data, sizeof(data), &bytes, NULL))
        {
            watch->filters[watch->count] = filter;
            watch->events[watch->count] = signal;
            watch->count++;
        }
        else
        {
            CloseHandle(filter);
            CloseHandle(signal);
        }
    }
}

static DWORD WINAPI jack_thread_proc(void *user)
{
    struct jack_watch watch;
    HANDLE handles[JACK_WATCH_MAX];
    UINT i;
    DWORD woken;

    UNREFERENCED_PARAMETER(user);
    SetThreadDescription(GetCurrentThread(), L"libernt_mmdevapi_jack");
    watch.count = 0;
    watch.endpoint_count = 0;

    for (;;)
    {
        jack_watch_arm(&watch);
        for (;;)
        {
            if (!watch.count)
            {
                Sleep(JACK_WATCH_IDLE_MS);
                break;
            }
            for (i = 0; i < watch.count; i++)
                handles[i] = watch.events[i];
            /* a bounded wait: an endpoint that came since the events were armed is armed in time */
            woken = WaitForMultipleObjects(watch.count, handles, FALSE, JACK_WATCH_IDLE_MS);
            if (woken == WAIT_TIMEOUT)
            {
                if (jack_watch_stale(&watch))
                    break;
                continue;
            }
            if (woken == WAIT_FAILED)
            {
                jack_watch_disarm(&watch);
                Sleep(JACK_WATCH_IDLE_MS);
                break;
            }
            TRACE("jack change told by endpoint %lu: asking the driver again\n", woken);
            reactos_refresh_devices(eAll);
            break;
        }
    }
    return 0;
}
#endif

static void MMDevice_Destroy(MMDevice *This)
{
    TRACE("Freeing %s\n", debugstr_w(This->drv_id));
    list_remove(&This->entry);
    free(This->drv_id);
    free(This);
}

static inline MMDevice *impl_from_IMMDevice(IMMDevice *iface)
{
    return CONTAINING_RECORD(iface, MMDevice, IMMDevice_iface);
}

static HRESULT WINAPI MMDevice_QueryInterface(IMMDevice *iface, REFIID riid, void **ppv)
{
    MMDevice *This = impl_from_IMMDevice(iface);
    TRACE("(%p)->(%s,%p)\n", iface, debugstr_guid(riid), ppv);

    if (!ppv)
        return E_POINTER;
    *ppv = NULL;
    if (IsEqualIID(riid, &IID_IUnknown)
        || IsEqualIID(riid, &IID_IMMDevice))
        *ppv = &This->IMMDevice_iface;
    else if (IsEqualIID(riid, &IID_IMMEndpoint))
        *ppv = &This->IMMEndpoint_iface;
    if (*ppv)
    {
        IUnknown_AddRef((IUnknown*)*ppv);
        return S_OK;
    }
    WARN("Unknown interface %s\n", debugstr_guid(riid));
    return E_NOINTERFACE;
}

static ULONG WINAPI MMDevice_AddRef(IMMDevice *iface)
{
    MMDevice *This = impl_from_IMMDevice(iface);
    LONG ref;

    ref = InterlockedIncrement(&This->ref);
    TRACE("Refcount now %li\n", ref);
    return ref;
}

static ULONG WINAPI MMDevice_Release(IMMDevice *iface)
{
    MMDevice *This = impl_from_IMMDevice(iface);
    LONG ref;

    ref = InterlockedDecrement(&This->ref);
    TRACE("Refcount now %li\n", ref);
    return ref;
}

static HRESULT WINAPI MMDevice_Activate(IMMDevice *iface, REFIID riid, DWORD clsctx, PROPVARIANT *params, void **ppv)
{
    HRESULT hr = E_NOINTERFACE;
    MMDevice *This = impl_from_IMMDevice(iface);
    BOOL creates_stream;

    TRACE("(%p)->(%s, %lx, %p, %p)\n", iface, debugstr_guid(riid), clsctx, params, ppv);

    if (!ppv)
        return E_POINTER;
    *ppv = NULL;

    creates_stream = IsEqualIID(riid, &IID_IAudioClient) ||
            IsEqualIID(riid, &IID_IAudioClient2) ||
            IsEqualIID(riid, &IID_IAudioClient3) ||
            IsEqualIID(riid, &IID_IBaseFilter) ||
            IsEqualIID(riid, &IID_IDirectSound) ||
            IsEqualIID(riid, &IID_IDirectSound8) ||
            IsEqualIID(riid, &IID_IDirectSoundCapture) ||
            IsEqualIID(riid, &IID_ISpatialAudioClient);
    if (creates_stream && This->state != DEVICE_STATE_ACTIVE)
        return AUDCLNT_E_DEVICE_INVALIDATED;

    if (IsEqualIID(riid, &IID_IAudioClient) ||
            IsEqualIID(riid, &IID_IAudioClient2) ||
            IsEqualIID(riid, &IID_IAudioClient3)){
        hr = AudioClient_Create(&This->devguid, iface, (IAudioClient**)ppv);
    }else if (IsEqualIID(riid, &IID_IAudioEndpointVolume) ||
            IsEqualIID(riid, &IID_IAudioEndpointVolumeEx))
        hr = AudioEndpointVolume_Create(This, (IAudioEndpointVolumeEx**)ppv);
    else if (IsEqualIID(riid, &IID_IAudioMeterInformation))
        hr = AudioEndpointMeter_Create(iface, ppv);
    else if (IsEqualIID(riid, &IID_IAudioSessionManager)
             || IsEqualIID(riid, &IID_IAudioSessionManager2))
    {
        hr = AudioSessionManager_Create(iface, (IAudioSessionManager2**)ppv);
    }
    else if (IsEqualIID(riid, &IID_IBaseFilter))
    {
        if (This->flow == eRender)
            hr = CoCreateInstance(&CLSID_DSoundRender, NULL, clsctx, riid, ppv);
        else
            ERR("Not supported for recording?\n");
        if (SUCCEEDED(hr))
        {
            IPersistPropertyBag *ppb;
            hr = IUnknown_QueryInterface((IUnknown*)*ppv, &IID_IPersistPropertyBag, (void **)&ppb);
            if (SUCCEEDED(hr))
            {
                /* ::Load cannot assume the interface stays alive after the function returns,
                 * so just create the interface on the stack, saves a lot of complicated code */
                IPropertyBagImpl bag = { { &PB_Vtbl } };
                bag.devguid = This->devguid;
                hr = IPersistPropertyBag_Load(ppb, &bag.IPropertyBag_iface, NULL);
                IPersistPropertyBag_Release(ppb);
                if (FAILED(hr))
                    IBaseFilter_Release((IBaseFilter*)*ppv);
            }
            else
            {
                FIXME("Wine doesn't support IPersistPropertyBag on DSoundRender yet, ignoring..\n");
                hr = S_OK;
            }
        }
    }
    else if (IsEqualIID(riid, &IID_IDeviceTopology))
    {
#ifdef __REACTOS__
        hr = reactos_device_topology_create(This, (IDeviceTopology**)ppv);
#else
        hr = DeviceTopology_Create(iface, (IDeviceTopology**)ppv);
#endif
    }
    else if (IsEqualIID(riid, &IID_IDirectSound)
             || IsEqualIID(riid, &IID_IDirectSound8))
    {
        if (This->flow == eRender)
            hr = CoCreateInstance(&CLSID_DirectSound8, NULL, clsctx, riid, ppv);
        if (SUCCEEDED(hr))
        {
            hr = IDirectSound_Initialize((IDirectSound*)*ppv, &This->devguid);
            if (FAILED(hr))
                IDirectSound_Release((IDirectSound*)*ppv);
        }
    }
    else if (IsEqualIID(riid, &IID_IDirectSoundCapture))
    {
        if (This->flow == eCapture)
            hr = CoCreateInstance(&CLSID_DirectSoundCapture8, NULL, clsctx, riid, ppv);
        if (SUCCEEDED(hr))
        {
            hr = IDirectSoundCapture_Initialize((IDirectSoundCapture*)*ppv, &This->devguid);
            if (FAILED(hr))
                IDirectSoundCapture_Release((IDirectSoundCapture*)*ppv);
        }
    }
    else if (IsEqualIID(riid, &IID_ISpatialAudioClient))
    {
        hr = SpatialAudioClient_Create(iface, (ISpatialAudioClient**)ppv);
    }
    else
        ERR("Invalid/unknown iid %s\n", debugstr_guid(riid));

    if (FAILED(hr))
        *ppv = NULL;

    TRACE("Returning %08lx\n", hr);
    return hr;
}

static HRESULT WINAPI MMDevice_OpenPropertyStore(IMMDevice *iface, DWORD access, IPropertyStore **ppv)
{
    MMDevice *This = impl_from_IMMDevice(iface);
    TRACE("(%p)->(%lx,%p)\n", This, access, ppv);

    if (!ppv)
        return E_POINTER;
    return MMDevPropStore_Create(This, access, ppv);
}

static HRESULT WINAPI MMDevice_GetId(IMMDevice *iface, WCHAR **itemid)
{
    MMDevice *This = impl_from_IMMDevice(iface);
    WCHAR *str;

    TRACE("(%p)->(%p)\n", This, itemid);
    if (!itemid)
        return E_POINTER;
    *itemid = str = CoTaskMemAlloc(56 * sizeof(WCHAR));
    if (!str)
        return E_OUTOFMEMORY;
    MMDevice_FormatId(This, str);
    TRACE("returning %s\n", wine_dbgstr_w(str));
    return S_OK;
}

static HRESULT WINAPI MMDevice_GetState(IMMDevice *iface, DWORD *state)
{
    MMDevice *This = impl_from_IMMDevice(iface);
    TRACE("(%p)->(%p)\n", iface, state);

    if (!state)
        return E_POINTER;
    *state = This->state;
    return S_OK;
}

static const IMMDeviceVtbl MMDeviceVtbl =
{
    MMDevice_QueryInterface,
    MMDevice_AddRef,
    MMDevice_Release,
    MMDevice_Activate,
    MMDevice_OpenPropertyStore,
    MMDevice_GetId,
    MMDevice_GetState
};

static inline MMDevice *impl_from_IMMEndpoint(IMMEndpoint *iface)
{
    return CONTAINING_RECORD(iface, MMDevice, IMMEndpoint_iface);
}

static HRESULT WINAPI MMEndpoint_QueryInterface(IMMEndpoint *iface, REFIID riid, void **ppv)
{
    MMDevice *This = impl_from_IMMEndpoint(iface);
    TRACE("(%p)->(%s, %p)\n", This, debugstr_guid(riid), ppv);
    return IMMDevice_QueryInterface(&This->IMMDevice_iface, riid, ppv);
}

static ULONG WINAPI MMEndpoint_AddRef(IMMEndpoint *iface)
{
    MMDevice *This = impl_from_IMMEndpoint(iface);
    TRACE("(%p)\n", This);
    return IMMDevice_AddRef(&This->IMMDevice_iface);
}

static ULONG WINAPI MMEndpoint_Release(IMMEndpoint *iface)
{
    MMDevice *This = impl_from_IMMEndpoint(iface);
    TRACE("(%p)\n", This);
    return IMMDevice_Release(&This->IMMDevice_iface);
}

static HRESULT WINAPI MMEndpoint_GetDataFlow(IMMEndpoint *iface, EDataFlow *flow)
{
    MMDevice *This = impl_from_IMMEndpoint(iface);
    TRACE("(%p)->(%p)\n", This, flow);
    if (!flow)
        return E_POINTER;
    *flow = This->flow;
    return S_OK;
}

static const IMMEndpointVtbl MMEndpointVtbl =
{
    MMEndpoint_QueryInterface,
    MMEndpoint_AddRef,
    MMEndpoint_Release,
    MMEndpoint_GetDataFlow
};

static HRESULT MMDevCol_Create(IMMDeviceCollection **ppv, EDataFlow flow, DWORD state)
{
    MMDevColImpl *This;
    MMDevice *cur;
    UINT i = 0;

    This = malloc(sizeof(*This));
    *ppv = NULL;
    if (!This)
        return E_OUTOFMEMORY;
    This->IMMDeviceCollection_iface.lpVtbl = &MMDevColVtbl;
    This->ref = 1;
    This->devices = NULL;
    This->devices_count = 0;
    *ppv = &This->IMMDeviceCollection_iface;

    LIST_FOR_EACH_ENTRY(cur, &device_list, MMDevice, entry)
    {
        if ((cur->flow == flow || flow == eAll) && (cur->state & state))
            This->devices_count++;
    }

    if (This->devices_count)
    {
        This->devices = malloc(This->devices_count * sizeof(IMMDevice *));
        if (!This->devices_count)
            return E_OUTOFMEMORY;

        LIST_FOR_EACH_ENTRY(cur, &device_list, MMDevice, entry)
        {
            if ((cur->flow == flow || flow == eAll) && (cur->state & state))
            {
                This->devices[i] = &cur->IMMDevice_iface;
                IMMDevice_AddRef(This->devices[i]);
                i++;
            }
        }
    }

    return S_OK;
}

static void MMDevCol_Destroy(MMDevColImpl *This)
{
    UINT i;
    for (i = 0; i < This->devices_count; i++)
        IMMDevice_Release(This->devices[i]);

    free(This->devices);
    free(This);
}

static HRESULT WINAPI MMDevCol_QueryInterface(IMMDeviceCollection *iface, REFIID riid, void **ppv)
{
    MMDevColImpl *This = impl_from_IMMDeviceCollection(iface);
    TRACE("(%p)->(%s, %p)\n", This, debugstr_guid(riid), ppv);

    if (!ppv)
        return E_POINTER;
    if (IsEqualIID(riid, &IID_IUnknown)
        || IsEqualIID(riid, &IID_IMMDeviceCollection))
        *ppv = &This->IMMDeviceCollection_iface;
    else
        *ppv = NULL;
    if (!*ppv)
        return E_NOINTERFACE;
    IUnknown_AddRef((IUnknown*)*ppv);
    return S_OK;
}

static ULONG WINAPI MMDevCol_AddRef(IMMDeviceCollection *iface)
{
    MMDevColImpl *This = impl_from_IMMDeviceCollection(iface);
    LONG ref = InterlockedIncrement(&This->ref);
    TRACE("Refcount now %li\n", ref);
    return ref;
}

static ULONG WINAPI MMDevCol_Release(IMMDeviceCollection *iface)
{
    MMDevColImpl *This = impl_from_IMMDeviceCollection(iface);
    LONG ref = InterlockedDecrement(&This->ref);
    TRACE("Refcount now %li\n", ref);
    if (!ref)
        MMDevCol_Destroy(This);
    return ref;
}

static HRESULT WINAPI MMDevCol_GetCount(IMMDeviceCollection *iface, UINT *numdevs)
{
    MMDevColImpl *This = impl_from_IMMDeviceCollection(iface);

    TRACE("(%p)->(%p)\n", This, numdevs);
    if (!numdevs)
        return E_POINTER;

    *numdevs = This->devices_count;
    return S_OK;
}

static HRESULT WINAPI MMDevCol_Item(IMMDeviceCollection *iface, UINT n, IMMDevice **dev)
{
    MMDevColImpl *This = impl_from_IMMDeviceCollection(iface);

    TRACE("(%p)->(%u, %p)\n", This, n, dev);
    if (!dev)
        return E_POINTER;

    if (n >= This->devices_count)
    {
        WARN("Could not obtain item %u\n", n);
        *dev = NULL;
        return E_INVALIDARG;
    }

    *dev = This->devices[n];
    IMMDevice_AddRef(*dev);
    return S_OK;
}

static const IMMDeviceCollectionVtbl MMDevColVtbl =
{
    MMDevCol_QueryInterface,
    MMDevCol_AddRef,
    MMDevCol_Release,
    MMDevCol_GetCount,
    MMDevCol_Item
};

HRESULT MMDevEnum_Create(REFIID riid, void **ppv)
{
    return IMMDeviceEnumerator_QueryInterface(&enumerator.IMMDeviceEnumerator_iface, riid, ppv);
}

void MMDevEnum_Free(void)
{
    MMDevice *device, *next;
    struct device *dev, *dev_next;

    LIST_FOR_EACH_ENTRY_SAFE(device, next, &device_list, MMDevice, entry)
        MMDevice_Destroy(device);
    if (key_render)
        RegCloseKey(key_render);
    if (key_capture)
        RegCloseKey(key_capture);
    LIST_FOR_EACH_ENTRY_SAFE(dev, dev_next, &devices_cache, struct device, entry)
        free( dev );
}

static HRESULT WINAPI MMDevEnum_QueryInterface(IMMDeviceEnumerator *iface, REFIID riid, void **ppv)
{
    MMDevEnumImpl *This = impl_from_IMMDeviceEnumerator(iface);
    TRACE("(%p)->(%s, %p)\n", This, debugstr_guid(riid), ppv);

    if (!ppv)
        return E_POINTER;
    if (IsEqualIID(riid, &IID_IUnknown)
        || IsEqualIID(riid, &IID_IMMDeviceEnumerator))
        *ppv = &This->IMMDeviceEnumerator_iface;
    else
        *ppv = NULL;
    if (!*ppv)
        return E_NOINTERFACE;
    IUnknown_AddRef((IUnknown*)*ppv);
    return S_OK;
}

static ULONG WINAPI MMDevEnum_AddRef(IMMDeviceEnumerator *iface)
{
    MMDevEnumImpl *This = impl_from_IMMDeviceEnumerator(iface);
    LONG ref = InterlockedIncrement(&This->ref);
    TRACE("Refcount now %li\n", ref);
    return ref;
}

static ULONG WINAPI MMDevEnum_Release(IMMDeviceEnumerator *iface)
{
    MMDevEnumImpl *This = impl_from_IMMDeviceEnumerator(iface);
    LONG ref = InterlockedDecrement(&This->ref);
    TRACE("Refcount now %li\n", ref);
    return ref;
}

static HRESULT WINAPI MMDevEnum_EnumAudioEndpoints(IMMDeviceEnumerator *iface, EDataFlow flow, DWORD mask, IMMDeviceCollection **devices)
{
    MMDevEnumImpl *This = impl_from_IMMDeviceEnumerator(iface);
    TRACE("(%p)->(%u,%lu,%p)\n", This, flow, mask, devices);
    if (!devices)
        return E_POINTER;
    *devices = NULL;
    if (flow >= EDataFlow_enum_count)
        return E_INVALIDARG;
    if (mask & ~DEVICE_STATEMASK_ALL)
        return E_INVALIDARG;
#ifdef __REACTOS__
    reactos_refresh_devices(flow);
#endif
    return MMDevCol_Create(devices, flow, mask);
}

static HRESULT WINAPI MMDevEnum_GetDefaultAudioEndpoint(IMMDeviceEnumerator *iface, EDataFlow flow, ERole role, IMMDevice **device)
{
    MMDevEnumImpl *This = impl_from_IMMDeviceEnumerator(iface);
#ifndef __REACTOS__
    WCHAR reg_key[256];
    HKEY key;
    HRESULT hr;
#endif

    TRACE("(%p)->(%u,%u,%p)\n", This, flow, role, device);

    if (!device)
        return E_POINTER;

    if((flow != eRender && flow != eCapture) ||
            (role != eConsole && role != eMultimedia && role != eCommunications)){
        WARN("Unknown flow (%u) or role (%u)\n", flow, role);
        return E_INVALIDARG;
    }

    *device = NULL;

#ifdef __REACTOS__
    reactos_refresh_devices(flow);

    /* the first active of the user's choices and of the driver's default, else the first active one (LEG-1) */
    {
        MMDevice *impl = pick_default_device(flow, role);
        HRESULT hr = notif_default_result(impl);

        if (FAILED(hr))
            return hr;
        *device = &impl->IMMDevice_iface;
        IMMDevice_AddRef(*device);
        return S_OK;
    }
#else
    if(!drvs.module_name[0])
        return E_NOTFOUND;

    lstrcpyW(reg_key, drv_keyW);
    lstrcatW(reg_key, L"\\");
    lstrcatW(reg_key, drvs.module_name);

    if(RegOpenKeyW(HKEY_CURRENT_USER, reg_key, &key) == ERROR_SUCCESS){
        const WCHAR *reg_x_name, *reg_vx_name;
        WCHAR def_id[256];
        DWORD size = sizeof(def_id), state;

        if(flow == eRender){
            reg_x_name = L"DefaultOutput";
            reg_vx_name = L"DefaultVoiceOutput";
        }else{
            reg_x_name = L"DefaultInput";
            reg_vx_name = L"DefaultVoiceInput";
        }

        if(role == eCommunications &&
                RegQueryValueExW(key, reg_vx_name, 0, NULL,
                    (BYTE*)def_id, &size) == ERROR_SUCCESS){
            hr = IMMDeviceEnumerator_GetDevice(iface, def_id, device);
            if(SUCCEEDED(hr)){
                if(SUCCEEDED(IMMDevice_GetState(*device, &state)) &&
                        state == DEVICE_STATE_ACTIVE){
                    RegCloseKey(key);
                    return S_OK;
                }
            }

            TRACE("Unable to find voice device %s\n", wine_dbgstr_w(def_id));
        }

        if(RegQueryValueExW(key, reg_x_name, 0, NULL,
                    (BYTE*)def_id, &size) == ERROR_SUCCESS){
            hr = IMMDeviceEnumerator_GetDevice(iface, def_id, device);
            if(SUCCEEDED(hr)){
                if(SUCCEEDED(IMMDevice_GetState(*device, &state)) &&
                        state == DEVICE_STATE_ACTIVE){
                    RegCloseKey(key);
                    return S_OK;
                }
            }

            TRACE("Unable to find device %s\n", wine_dbgstr_w(def_id));
        }

        RegCloseKey(key);
    }

    if (flow == eRender)
    {
        if (!MMDevice_def_play)
            return E_NOTFOUND;
        *device = &MMDevice_def_play->IMMDevice_iface;
    }
    else
    {
        if (!MMDevice_def_rec)
            return E_NOTFOUND;
        *device = &MMDevice_def_rec->IMMDevice_iface;
    }

    if (!*device)
        return E_NOTFOUND;
    IMMDevice_AddRef(*device);
    return S_OK;
#endif
}

static HRESULT WINAPI MMDevEnum_GetDevice(IMMDeviceEnumerator *iface, const WCHAR *name, IMMDevice **device)
{
    MMDevEnumImpl *This = impl_from_IMMDeviceEnumerator(iface);
    MMDevice *impl;
    IMMDevice *dev = NULL;

    TRACE("(%p)->(%s,%p)\n", This, debugstr_w(name), device);

    if(!name || !device)
        return E_POINTER;

#ifdef __REACTOS__
    if (!wcsncmp(name, L"{2}.", 4))
        return reactos_filter_device_get(name, device);
#endif

    LIST_FOR_EACH_ENTRY(impl, &device_list, MMDevice, entry)
    {
        HRESULT hr;
        WCHAR *str;
        dev = &impl->IMMDevice_iface;
        hr = IMMDevice_GetId(dev, &str);
        if (FAILED(hr))
        {
            WARN("GetId failed: %08lx\n", hr);
            continue;
        }

        if (str && !lstrcmpW(str, name))
        {
            CoTaskMemFree(str);
            IMMDevice_AddRef(dev);
            *device = dev;
            return S_OK;
        }
        CoTaskMemFree(str);
    }
    TRACE("Could not find device %s\n", debugstr_w(name));
    return E_INVALIDARG;
}

/* The registered clients and their walks: devenum_notif.h's bookkeeping, every entry under g_notif_lock */
static struct notif_clients g_notif_clients = NOTIF_CLIENTS_INIT(g_notif_clients);
static HANDLE g_notif_thread;
/* The thread that waits for the jacks' changes (LEG-1), started with the notification thread */
static HANDLE g_jack_thread;

static CRITICAL_SECTION g_notif_lock;
static CRITICAL_SECTION_DEBUG g_notif_lock_debug =
{
    0, 0, &g_notif_lock,
    { &g_notif_lock_debug.ProcessLocksList, &g_notif_lock_debug.ProcessLocksList },
      0, 0, { (DWORD_PTR)(__FILE__ ": g_notif_lock") }
};
static CRITICAL_SECTION g_notif_lock = { &g_notif_lock_debug, -1, 0, 0, 0, 0 };

/* One change told to a client: the default's (flow, role and id), or a device's (which of the three, its id and its state) */
struct notif_change_args
{
    EDataFlow flow;
    ERole role;
    enum notif_change change;
    const WCHAR *id;
    DWORD state;
};

static void call_client(void *client, void *context)
{
    struct notif_change_args *args = context;
    IMMNotificationClient *iface = client;

    switch (args->change)
    {
        case NOTIF_ADDED:
            IMMNotificationClient_OnDeviceAdded(iface, args->id);
            break;
        case NOTIF_REMOVED:
            IMMNotificationClient_OnDeviceRemoved(iface, args->id);
            break;
        case NOTIF_STATE:
            IMMNotificationClient_OnDeviceStateChanged(iface, args->id, args->state);
            break;
        default:
            IMMNotificationClient_OnDefaultDeviceChanged(iface, args->flow, args->role, args->id);
    }
}

static void notify_clients(EDataFlow flow, ERole role, const WCHAR *id)
{
    struct notif_change_args args;

    args.change = NOTIF_DEFAULT;
    args.flow = flow;
    args.role = role;
    args.id = id;
    notif_clients_walk(&g_notif_clients, call_client, &args);

    /* Windows 7 treats changes to eConsole as changes to eMultimedia */
    if(role == eConsole)
        notify_clients(flow, eMultimedia, id);
}

/* The three device notifications, to every registered client as the default's goes (R7-01); the caller holds g_notif_lock */
static void notify_device(enum notif_change change, const MMDevice *device)
{
    struct notif_change_args args;
    WCHAR id[56];

    MMDevice_FormatId(device, id);
    args.change = change;
    args.id = id;
    args.state = device->state;
    notif_clients_walk(&g_notif_clients, call_client, &args);
}

static BOOL notify_if_changed(EDataFlow flow, ERole role, HKEY key,
                              const WCHAR *val_name, WCHAR *old_val, IMMDevice *def_dev)
{
    WCHAR new_val[64], *id;
    DWORD size;
    HRESULT hr;

    size = sizeof(new_val);
    if(RegQueryValueExW(key, val_name, 0, NULL,
                (BYTE*)new_val, &size) != ERROR_SUCCESS){
        if(old_val[0] != 0){
            /* set by user -> system default */
            if(def_dev){
                hr = IMMDevice_GetId(def_dev, &id);
                if(FAILED(hr)){
                    ERR("GetId failed: %08lx\n", hr);
                    return FALSE;
                }
            }else
                id = NULL;

            notify_clients(flow, role, id);
            old_val[0] = 0;
            CoTaskMemFree(id);

            return TRUE;
        }

        /* system default -> system default, noop */
        return FALSE;
    }

    if(!lstrcmpW(old_val, new_val)){
        /* set by user -> same value */
        return FALSE;
    }

    if(new_val[0] != 0){
        /* set by user -> different value */
        notify_clients(flow, role, new_val);
        memcpy(old_val, new_val, sizeof(new_val));
        return TRUE;
    }

    /* set by user -> system default */
    if(def_dev){
        hr = IMMDevice_GetId(def_dev, &id);
        if(FAILED(hr)){
            ERR("GetId failed: %08lx\n", hr);
            return FALSE;
        }
    }else
        id = NULL;

    notify_clients(flow, role, id);
    old_val[0] = 0;
    CoTaskMemFree(id);

    return TRUE;
}

static DWORD WINAPI notif_thread_proc(void *user)
{
    HKEY key;
    LSTATUS status;
    WCHAR reg_key[256];
    WCHAR out_name[64], vout_name[64], in_name[64], vin_name[64];
    DWORD size;

    SetThreadDescription(GetCurrentThread(), L"wine_mmdevapi_notification");

    lstrcpyW(reg_key, drv_keyW);
    lstrcatW(reg_key, L"\\");
    lstrcatW(reg_key, drvs.module_name);

    status = RegCreateKeyExW(HKEY_CURRENT_USER, reg_key, 0, NULL, 0, MAXIMUM_ALLOWED, NULL, &key, NULL);
    if(status != ERROR_SUCCESS){
        ERR("RegCreateKeyEx failed: %ld\n", status);
        return 1;
    }

    size = sizeof(out_name);
    if(RegQueryValueExW(key, L"DefaultOutput", 0, NULL, (BYTE*)out_name, &size) != ERROR_SUCCESS)
        out_name[0] = 0;

    size = sizeof(vout_name);
    if(RegQueryValueExW(key, L"DefaultVoiceOutput", 0, NULL, (BYTE*)vout_name, &size) != ERROR_SUCCESS)
        vout_name[0] = 0;

    size = sizeof(in_name);
    if(RegQueryValueExW(key, L"DefaultInput", 0, NULL, (BYTE*)in_name, &size) != ERROR_SUCCESS)
        in_name[0] = 0;

    size = sizeof(vin_name);
    if(RegQueryValueExW(key, L"DefaultVoiceInput", 0, NULL, (BYTE*)vin_name, &size) != ERROR_SUCCESS)
        vin_name[0] = 0;

    while(1){
        status = RegNotifyChangeKeyValue(key, FALSE, REG_NOTIFY_CHANGE_LAST_SET, NULL, FALSE);
        if(status != ERROR_SUCCESS){
            ERR("RegNotifyChangeKeyValue failed: %ld\n", status);
            RegCloseKey(key);
            g_notif_thread = NULL;
            return 1;
        }

        EnterCriticalSection(&g_notif_lock);

        notify_if_changed(eRender, eConsole, key, L"DefaultOutput",
                out_name, MMDevice_def_play ? &MMDevice_def_play->IMMDevice_iface : NULL);
        notify_if_changed(eRender, eCommunications, key, L"DefaultVoiceOutput",
                vout_name, MMDevice_def_play ? &MMDevice_def_play->IMMDevice_iface : NULL);
        notify_if_changed(eCapture, eConsole, key, L"DefaultInput",
                in_name, MMDevice_def_rec ? &MMDevice_def_rec->IMMDevice_iface : NULL);
        notify_if_changed(eCapture, eCommunications, key, L"DefaultVoiceInput",
                vin_name, MMDevice_def_rec ? &MMDevice_def_rec->IMMDevice_iface : NULL);

        LeaveCriticalSection(&g_notif_lock);
    }

    RegCloseKey(key);

    g_notif_thread = NULL;

    return 0;
}

static HRESULT WINAPI MMDevEnum_RegisterEndpointNotificationCallback(IMMDeviceEnumerator *iface, IMMNotificationClient *client)
{
    MMDevEnumImpl *This = impl_from_IMMDeviceEnumerator(iface);

    TRACE("(%p)->(%p)\n", This, client);

    if(!client)
        return E_POINTER;

    EnterCriticalSection(&g_notif_lock);

    if (notif_clients_register(&g_notif_clients, client))
    {
        if(!g_notif_thread){
            g_notif_thread = CreateThread(NULL, 0, notif_thread_proc, NULL, 0, NULL);
            if(!g_notif_thread)
                ERR("CreateThread failed: %lu\n", GetLastError());
        }
        /* a jack's change wakes the refresh that tells the clients (LEG-1) */
        if (!g_jack_thread)
            g_jack_thread = CreateThread(NULL, 0, jack_thread_proc, NULL, 0, NULL);

        LeaveCriticalSection(&g_notif_lock);

        return S_OK;
    }

    LeaveCriticalSection(&g_notif_lock);

    return E_OUTOFMEMORY;
}

static HRESULT WINAPI MMDevEnum_UnregisterEndpointNotificationCallback(IMMDeviceEnumerator *iface, IMMNotificationClient *client)
{
    MMDevEnumImpl *This = impl_from_IMMDeviceEnumerator(iface);
    BOOLEAN found;

    TRACE("(%p)->(%p)\n", This, client);

    if(!client)
        return E_POINTER;

    EnterCriticalSection(&g_notif_lock);

    found = notif_clients_unregister(&g_notif_clients, client);

    LeaveCriticalSection(&g_notif_lock);

    return found ? S_OK : E_NOTFOUND;
}

static const IMMDeviceEnumeratorVtbl MMDevEnumVtbl =
{
    MMDevEnum_QueryInterface,
    MMDevEnum_AddRef,
    MMDevEnum_Release,
    MMDevEnum_EnumAudioEndpoints,
    MMDevEnum_GetDefaultAudioEndpoint,
    MMDevEnum_GetDevice,
    MMDevEnum_RegisterEndpointNotificationCallback,
    MMDevEnum_UnregisterEndpointNotificationCallback
};

static MMDevEnumImpl enumerator =
{
    {&MMDevEnumVtbl},
    1,
};

static HRESULT MMDevPropStore_Create(MMDevice *parent, DWORD access, IPropertyStore **ppv)
{
    MMDevPropStore *This;
    if (access != STGM_READ
        && access != STGM_WRITE
        && access != STGM_READWRITE)
    {
        WARN("Invalid access %08lx\n", access);
        return E_INVALIDARG;
    }
    This = malloc(sizeof(*This));
    *ppv = &This->IPropertyStore_iface;
    if (!This)
        return E_OUTOFMEMORY;
    This->IPropertyStore_iface.lpVtbl = &MMDevPropVtbl;
    This->ref = 1;
    This->parent = parent;
    This->access = access;
    return S_OK;
}

static void MMDevPropStore_Destroy(MMDevPropStore *This)
{
    free(This);
}

static HRESULT WINAPI MMDevPropStore_QueryInterface(IPropertyStore *iface, REFIID riid, void **ppv)
{
    MMDevPropStore *This = impl_from_IPropertyStore(iface);
    TRACE("(%p)->(%s, %p)\n", This, debugstr_guid(riid), ppv);

    if (!ppv)
        return E_POINTER;
    if (IsEqualIID(riid, &IID_IUnknown)
        || IsEqualIID(riid, &IID_IPropertyStore))
        *ppv = &This->IPropertyStore_iface;
    else
        *ppv = NULL;
    if (!*ppv)
        return E_NOINTERFACE;
    IUnknown_AddRef((IUnknown*)*ppv);
    return S_OK;
}

static ULONG WINAPI MMDevPropStore_AddRef(IPropertyStore *iface)
{
    MMDevPropStore *This = impl_from_IPropertyStore(iface);
    LONG ref = InterlockedIncrement(&This->ref);
    TRACE("Refcount now %li\n", ref);
    return ref;
}

static ULONG WINAPI MMDevPropStore_Release(IPropertyStore *iface)
{
    MMDevPropStore *This = impl_from_IPropertyStore(iface);
    LONG ref = InterlockedDecrement(&This->ref);
    TRACE("Refcount now %li\n", ref);
    if (!ref)
        MMDevPropStore_Destroy(This);
    return ref;
}

static HRESULT WINAPI MMDevPropStore_GetCount(IPropertyStore *iface, DWORD *nprops)
{
    MMDevPropStore *This = impl_from_IPropertyStore(iface);
    WCHAR buffer[50];
    DWORD i = 0;
    HKEY propkey;
    HRESULT hr;

    TRACE("(%p)->(%p)\n", iface, nprops);
    if (!nprops)
        return E_POINTER;
    hr = MMDevPropStore_OpenPropKey(&This->parent->devguid, This->parent->flow,
                                    KEY_READ, &propkey);
    if (FAILED(hr))
        return hr;
    *nprops = 0;
    do {
        DWORD len = ARRAY_SIZE(buffer);
        if (RegEnumValueW(propkey, i, buffer, &len, NULL, NULL, NULL, NULL) != ERROR_SUCCESS)
            break;
        i++;
    } while (1);
    RegCloseKey(propkey);
    TRACE("Returning %li\n", i);
    *nprops = i;
    return S_OK;
}

static HRESULT WINAPI MMDevPropStore_GetAt(IPropertyStore *iface, DWORD prop, PROPERTYKEY *key)
{
    MMDevPropStore *This = impl_from_IPropertyStore(iface);
    WCHAR buffer[50];
    DWORD len = ARRAY_SIZE(buffer);
    HRESULT hr;
    HKEY propkey;

    TRACE("(%p)->(%lu,%p)\n", iface, prop, key);
    if (!key)
        return E_POINTER;

    hr = MMDevPropStore_OpenPropKey(&This->parent->devguid, This->parent->flow,
                                    KEY_READ, &propkey);
    if (FAILED(hr))
        return hr;

    if (RegEnumValueW(propkey, prop, buffer, &len, NULL, NULL, NULL, NULL) != ERROR_SUCCESS
        || len <= 39)
    {
        WARN("GetAt %lu failed\n", prop);
        return E_INVALIDARG;
    }
    RegCloseKey(propkey);
    buffer[38] = 0;
    CLSIDFromString(buffer, &key->fmtid);
    key->pid = wcstol(&buffer[39], NULL, 10);
    return S_OK;
}

static HRESULT WINAPI MMDevPropStore_GetValue(IPropertyStore *iface, REFPROPERTYKEY key, PROPVARIANT *pv)
{
    MMDevPropStore *This = impl_from_IPropertyStore(iface);
    HRESULT hres;
    TRACE("(%p)->(\"%s,%lu\", %p)\n", This, key ? debugstr_guid(&key->fmtid) : NULL, key ? key->pid : 0, pv);

    if (!key || !pv)
        return E_POINTER;
    if (This->access != STGM_READ
        && This->access != STGM_READWRITE)
        return STG_E_ACCESSDENIED;

    /* Special case */
    if (IsEqualPropertyKey(*key, PKEY_AudioEndpoint_GUID))
    {
        pv->vt = VT_LPWSTR;
        pv->pwszVal = CoTaskMemAlloc(39 * sizeof(WCHAR));
        if (!pv->pwszVal)
            return E_OUTOFMEMORY;
        StringFromGUID2(&This->parent->devguid, pv->pwszVal, 39);
        return S_OK;
    }

    hres = MMDevice_GetPropValue(&This->parent->devguid, This->parent->flow, key, pv);
    if (FAILED(hres))
        return hres;

    if (WARN_ON(mmdevapi))
    {
        if ((IsEqualPropertyKey(*key, DEVPKEY_Device_FriendlyName) ||
             IsEqualPropertyKey(*key, DEVPKEY_DeviceInterface_FriendlyName) ||
             IsEqualPropertyKey(*key, DEVPKEY_Device_DeviceDesc)) &&
            pv->vt == VT_LPWSTR && wcslen(pv->pwszVal) > 62)
            WARN("Returned name exceeds length limit of some broken apps/libs, might crash: %s\n", debugstr_w(pv->pwszVal));
    }
    return hres;
}

static HRESULT WINAPI MMDevPropStore_SetValue(IPropertyStore *iface, REFPROPERTYKEY key, REFPROPVARIANT pv)
{
    MMDevPropStore *This = impl_from_IPropertyStore(iface);
    TRACE("(%p)->(\"%s,%lu\", %p)\n", This, key ? debugstr_guid(&key->fmtid) : NULL, key ? key->pid : 0, pv);

    if (!key || !pv)
        return E_POINTER;

    if (This->access != STGM_WRITE
        && This->access != STGM_READWRITE)
        return STG_E_ACCESSDENIED;
    return MMDevice_SetPropValue(&This->parent->devguid, This->parent->flow, key, pv);
}

static HRESULT WINAPI MMDevPropStore_Commit(IPropertyStore *iface)
{
    MMDevPropStore *This = impl_from_IPropertyStore(iface);
    TRACE("(%p)\n", iface);

    if (This->access != STGM_WRITE
        && This->access != STGM_READWRITE)
        return STG_E_ACCESSDENIED;

    /* Does nothing - for mmdevapi, the propstore values are written on SetValue,
     * not on Commit. */

    return S_OK;
}

static const IPropertyStoreVtbl MMDevPropVtbl =
{
    MMDevPropStore_QueryInterface,
    MMDevPropStore_AddRef,
    MMDevPropStore_Release,
    MMDevPropStore_GetCount,
    MMDevPropStore_GetAt,
    MMDevPropStore_GetValue,
    MMDevPropStore_SetValue,
    MMDevPropStore_Commit
};


/* Property bag for IBaseFilter activation */
static HRESULT WINAPI PB_QueryInterface(IPropertyBag *iface, REFIID riid, void **ppv)
{
    ERR("Should not be called\n");
    *ppv = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI PB_AddRef(IPropertyBag *iface)
{
    ERR("Should not be called\n");
    return 2;
}

static ULONG WINAPI PB_Release(IPropertyBag *iface)
{
    ERR("Should not be called\n");
    return 1;
}

static HRESULT WINAPI PB_Read(IPropertyBag *iface, LPCOLESTR name, VARIANT *var, IErrorLog *log)
{
    IPropertyBagImpl *This = impl_from_IPropertyBag(iface);
    TRACE("Trying to read %s, type %u\n", debugstr_w(name), var->vt);
    if (!lstrcmpW(name, L"DSGuid"))
    {
        WCHAR guidstr[39];
        StringFromGUID2(&This->devguid, guidstr,ARRAY_SIZE(guidstr));
        var->vt = VT_BSTR;
        var->bstrVal = SysAllocString(guidstr);
        return S_OK;
    }
    ERR("Unknown property '%s' queried\n", debugstr_w(name));
    return E_FAIL;
}

static HRESULT WINAPI PB_Write(IPropertyBag *iface, LPCOLESTR name, VARIANT *var)
{
    ERR("Should not be called\n");
    return E_FAIL;
}

static const IPropertyBagVtbl PB_Vtbl =
{
    PB_QueryInterface,
    PB_AddRef,
    PB_Release,
    PB_Read,
    PB_Write
};

#ifndef __REACTOS__
static HRESULT WINAPI Connector_QueryInterface(IConnector *iface, REFIID riid, void **ppv)
{
    IConnectorImpl *This = impl_from_IConnector(iface);
    TRACE("(%p)->(%s, %p)\n", This, debugstr_guid(riid), ppv);

    if (!ppv)
        return E_POINTER;

    if (IsEqualIID(riid, &IID_IUnknown) ||
        IsEqualIID(riid, &IID_IConnector))
        *ppv = &This->IConnector_iface;
    else {
        *ppv = NULL;
        return E_NOINTERFACE;
    }

    IUnknown_AddRef((IUnknown *)*ppv);

    return S_OK;
}

static ULONG WINAPI Connector_AddRef(IConnector *iface)
{
    IConnectorImpl *This = impl_from_IConnector(iface);
    ULONG ref = InterlockedIncrement(&This->ref);
    TRACE("(%p) new ref %lu\n", This, ref);
    return ref;
}

static ULONG WINAPI Connector_Release(IConnector *iface)
{
    IConnectorImpl *This = impl_from_IConnector(iface);
    ULONG ref = InterlockedDecrement(&This->ref);
    TRACE("(%p) new ref %lu\n", This, ref);

    if (!ref)
        HeapFree(GetProcessHeap(), 0, This);

    return ref;
}

static HRESULT WINAPI Connector_GetType(
    IConnector *This,
    ConnectorType *pType)
{
    FIXME("(%p) - partial stub\n", This);
    *pType = Physical_Internal;
    return S_OK;
}

static HRESULT WINAPI Connector_GetDataFlow(
    IConnector *This,
    DataFlow *pFlow)
{
    FIXME("(%p) - stub\n", This);
    return E_NOTIMPL;
}

static HRESULT WINAPI Connector_ConnectTo(
    IConnector *This,
    IConnector *pConnectTo)
{
    FIXME("(%p) - stub\n", This);
    return E_NOTIMPL;
}

static HRESULT WINAPI Connector_Disconnect(
    IConnector *This)
{
    FIXME("(%p) - stub\n", This);
    return E_NOTIMPL;
}

static HRESULT WINAPI Connector_IsConnected(
    IConnector *This,
    BOOL *pbConnected)
{
    FIXME("(%p) - stub\n", This);
    return E_NOTIMPL;
}

static HRESULT WINAPI Connector_GetConnectedTo(
    IConnector *This,
    IConnector **ppConTo)
{
    FIXME("(%p) - stub\n", This);
    return E_NOTIMPL;
}

static HRESULT WINAPI Connector_GetConnectorIdConnectedTo(
    IConnector *This,
    LPWSTR *ppwstrConnectorId)
{
    FIXME("(%p) - stub\n", This);
    return E_NOTIMPL;
}

static HRESULT WINAPI Connector_GetDeviceIdConnectedTo(
    IConnector *This,
    LPWSTR *ppwstrDeviceId)
{
    FIXME("(%p) - stub\n", This);
    return E_NOTIMPL;
}

static const IConnectorVtbl Connector_Vtbl =
{
    Connector_QueryInterface,
    Connector_AddRef,
    Connector_Release,
    Connector_GetType,
    Connector_GetDataFlow,
    Connector_ConnectTo,
    Connector_Disconnect,
    Connector_IsConnected,
    Connector_GetConnectedTo,
    Connector_GetConnectorIdConnectedTo,
    Connector_GetDeviceIdConnectedTo,
};

HRESULT Connector_Create(IConnector **ppv)
{
    IConnectorImpl *This;

    This = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*This));
    if (!This)
        return E_OUTOFMEMORY;

    This->IConnector_iface.lpVtbl = &Connector_Vtbl;
    This->ref = 1;

    *ppv = &This->IConnector_iface;

    return S_OK;
}

static HRESULT WINAPI DT_QueryInterface(IDeviceTopology *iface, REFIID riid, void **ppv)
{
    IDeviceTopologyImpl *This = impl_from_IDeviceTopology(iface);
    TRACE("(%p)->(%s, %p)\n", This, debugstr_guid(riid), ppv);

    if (!ppv)
        return E_POINTER;

    if (IsEqualIID(riid, &IID_IUnknown) ||
        IsEqualIID(riid, &IID_IDeviceTopology))
        *ppv = &This->IDeviceTopology_iface;
    else {
        *ppv = NULL;
        return E_NOINTERFACE;
    }

    IUnknown_AddRef((IUnknown *)*ppv);

    return S_OK;
}

static ULONG WINAPI DT_AddRef(IDeviceTopology *iface)
{
    IDeviceTopologyImpl *This = impl_from_IDeviceTopology(iface);
    ULONG ref = InterlockedIncrement(&This->ref);
    TRACE("(%p) new ref %lu\n", This, ref);
    return ref;
}

static ULONG WINAPI DT_Release(IDeviceTopology *iface)
{
    IDeviceTopologyImpl *This = impl_from_IDeviceTopology(iface);
    ULONG ref = InterlockedDecrement(&This->ref);
    TRACE("(%p) new ref %lu\n", This, ref);

    if (!ref)
        HeapFree(GetProcessHeap(), 0, This);

    return ref;
}

static HRESULT WINAPI DT_GetConnectorCount(IDeviceTopology *This,
                                           UINT *pCount)
{
    FIXME("(%p)->(%p) - partial stub\n", This, pCount);

    if (!pCount)
        return E_POINTER;

    *pCount = 1;
    return S_OK;
}

static HRESULT WINAPI DT_GetConnector(IDeviceTopology *This,
                                      UINT nIndex,
                                      IConnector **ppConnector)
{
    FIXME("(%p)->(%u, %p) - partial stub\n", This, nIndex, ppConnector);

    if (nIndex == 0)
    {
        return Connector_Create(ppConnector);
    }

    return E_INVALIDARG;
}

static HRESULT WINAPI DT_GetSubunitCount(IDeviceTopology *This,
                                         UINT *pCount)
{
    FIXME("(%p)->(%p) - stub\n", This, pCount);
    return E_NOTIMPL;
}

static HRESULT WINAPI DT_GetSubunit(IDeviceTopology *This,
                                    UINT nIndex,
                                    ISubUnit **ppConnector)
{
    FIXME("(%p)->(%u, %p) - stub\n", This, nIndex, ppConnector);
    return E_NOTIMPL;
}

static HRESULT WINAPI DT_GetPartById(IDeviceTopology *This,
                                     UINT nId,
                                     IPart **ppPart)
{
    FIXME("(%p)->(%u, %p) - stub\n", This, nId, ppPart);
    return E_NOTIMPL;
}

static HRESULT WINAPI DT_GetDeviceId(IDeviceTopology *This,
                                     LPWSTR *ppwstrDeviceId)
{
    FIXME("(%p)->(%p) - stub\n", This, ppwstrDeviceId);
    return E_NOTIMPL;
}

static HRESULT WINAPI DT_GetSignalPath(IDeviceTopology *This,
                                       IPart *pIPartFrom,
                                       IPart *pIPartTo,
                                       BOOL bRejectMixedPaths,
                                       IPartsList **ppParts)
{
    FIXME("(%p)->(%p, %p, %s, %p) - stub\n",
          This, pIPartFrom, pIPartTo, bRejectMixedPaths ? "TRUE" : "FALSE", ppParts);
    return E_NOTIMPL;
}

static const IDeviceTopologyVtbl DeviceTopology_Vtbl =
{
    DT_QueryInterface,
    DT_AddRef,
    DT_Release,
    DT_GetConnectorCount,
    DT_GetConnector,
    DT_GetSubunitCount,
    DT_GetSubunit,
    DT_GetPartById,
    DT_GetDeviceId,
    DT_GetSignalPath,
};

static HRESULT DeviceTopology_Create(IMMDevice *device, IDeviceTopology **ppv)
{
    IDeviceTopologyImpl *This;

    This = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*This));
    if (!This)
        return E_OUTOFMEMORY;

    This->IDeviceTopology_iface.lpVtbl = &DeviceTopology_Vtbl;
    This->ref = 1;

    *ppv = &This->IDeviceTopology_iface;

    return S_OK;
}
#endif
