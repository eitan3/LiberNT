/*
 * PROJECT:     LiberNT Multimedia Control Panel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Properties of a playback or recording device: its jack, controller and driver
 */

#include "mmsys.h"

#define COBJMACROS
#include <winnls.h>
#include <objbase.h>
#include <prsht.h>
#include <mmdeviceapi.h>
#include <cfgmgr32.h>
#include <initguid.h>
#include <devicetopology.h>
#include <devpkey.h>

int WINAPI
DevicePropertiesW(HWND hWndParent,
                  LPCWSTR lpMachineName,
                  LPCWSTR lpDeviceID,
                  BOOL bShowDevMgr);

/* DEVPKEY_Device_InstanceId as the filter device's property store reports it */
static const PROPERTYKEY InstanceIdKey =
    { { 0x78c34fc8, 0x104a, 0x4aca, { 0x9e, 0xa4, 0x52, 0x4d, 0x52, 0x99, 0x6e, 0x57 } }, 256 };
static const PROPERTYKEY EndpointNameKey =
    { { 0xa45c254e, 0xdf1c, 0x4efd, { 0x80, 0x20, 0x67, 0xd1, 0x46, 0xa8, 0x50, 0xe0 } }, 14 };
static const PROPERTYKEY EndpointFormFactorKey =
    { { 0x1da5d803, 0xd492, 0x4edd, { 0x8c, 0x23, 0xe0, 0xc0, 0xff, 0xee, 0x7f, 0x0e } }, 0 };

typedef struct _ENDPOINT_PROPS
{
    WCHAR Name[128];
    WCHAR Jack[128];
    WCHAR Controller[MAX_PATH];
    WCHAR Manufacturer[128];
    WCHAR DriverProvider[128];
    WCHAR DriverVersion[64];
    WCHAR DriverDate[64];
    WCHAR InstanceId[MAX_DEVICE_ID_LEN];
    UINT FormFactor;
} ENDPOINT_PROPS, *PENDPOINT_PROPS;

static VOID
ReadStringProperty(IPropertyStore *pProps, const PROPERTYKEY *Key, LPWSTR Buffer, SIZE_T Chars)
{
    PROPVARIANT Value;

    PropVariantInit(&Value);
    if (SUCCEEDED(IPropertyStore_GetValue(pProps, Key, &Value)) && Value.vt == VT_LPWSTR && Value.pwszVal)
        StringCchCopyW(Buffer, Chars, Value.pwszVal);
    PropVariantClear(&Value);
}

/*
 * The endpoint's connector is wired to a pin of the adapter's KS filter; the filter's
 * device ID opens a property store holding the adapter's device instance ID.
 */
static VOID
ReadControllerInstanceId(IMMDeviceEnumerator *pEnum, IMMDevice *pDevice, PENDPOINT_PROPS Props)
{
    IDeviceTopology *pTopology = NULL;
    IConnector *pConnector = NULL;
    LPWSTR pszFilterId = NULL;
    IMMDevice *pFilter = NULL;
    IPropertyStore *pProps = NULL;

    if (SUCCEEDED(IMMDevice_Activate(pDevice, &IID_IDeviceTopology, CLSCTX_INPROC_SERVER, NULL, (void **)&pTopology)) &&
        SUCCEEDED(IDeviceTopology_GetConnector(pTopology, 0, &pConnector)) &&
        SUCCEEDED(IConnector_GetDeviceIdConnectedTo(pConnector, &pszFilterId)) &&
        SUCCEEDED(IMMDeviceEnumerator_GetDevice(pEnum, pszFilterId, &pFilter)) &&
        SUCCEEDED(IMMDevice_OpenPropertyStore(pFilter, STGM_READ, &pProps)))
    {
        ReadStringProperty(pProps, &InstanceIdKey, Props->InstanceId, _countof(Props->InstanceId));
    }

    if (pProps)
        IPropertyStore_Release(pProps);
    if (pFilter)
        IMMDevice_Release(pFilter);
    if (pszFilterId)
        CoTaskMemFree(pszFilterId);
    if (pConnector)
        IConnector_Release(pConnector);
    if (pTopology)
        IDeviceTopology_Release(pTopology);
}

static VOID
ReadDevNodeString(DEVINST DevInst, const DEVPROPKEY *Key, LPWSTR Buffer, SIZE_T Chars)
{
    DEVPROPTYPE Type;
    ULONG Size = (ULONG)(Chars * sizeof(WCHAR));

    if (CM_Get_DevNode_PropertyW(DevInst, Key, &Type, (PBYTE)Buffer, &Size, 0) != CR_SUCCESS ||
        Type != DEVPROP_TYPE_STRING)
    {
        Buffer[0] = UNICODE_NULL;
    }
}

static VOID
ReadDriver(PENDPOINT_PROPS Props)
{
    DEVINST DevInst;
    DEVPROPTYPE Type;
    FILETIME Date;
    SYSTEMTIME SystemTime;
    ULONG Size = sizeof(Date);

    if (!Props->InstanceId[0] || CM_Locate_DevNodeW(&DevInst, Props->InstanceId, CM_LOCATE_DEVNODE_NORMAL) != CR_SUCCESS)
        return;

    ReadDevNodeString(DevInst, &DEVPKEY_Device_FriendlyName, Props->Controller, _countof(Props->Controller));
    if (!Props->Controller[0])
        ReadDevNodeString(DevInst, &DEVPKEY_Device_DeviceDesc, Props->Controller, _countof(Props->Controller));
    ReadDevNodeString(DevInst, &DEVPKEY_Device_Manufacturer, Props->Manufacturer, _countof(Props->Manufacturer));
    ReadDevNodeString(DevInst, &DEVPKEY_Device_DriverProvider, Props->DriverProvider, _countof(Props->DriverProvider));
    ReadDevNodeString(DevInst, &DEVPKEY_Device_DriverVersion, Props->DriverVersion, _countof(Props->DriverVersion));

    if (CM_Get_DevNode_PropertyW(DevInst, &DEVPKEY_Device_DriverDate, &Type, (PBYTE)&Date, &Size, 0) == CR_SUCCESS &&
        Type == DEVPROP_TYPE_FILETIME && FileTimeToSystemTime(&Date, &SystemTime))
    {
        GetDateFormatW(LOCALE_USER_DEFAULT, DATE_SHORTDATE, &SystemTime, NULL, Props->DriverDate, _countof(Props->DriverDate));
    }
}

static VOID
ReadEndpoint(LPCWSTR pszEndpointId, PENDPOINT_PROPS Props)
{
    IMMDeviceEnumerator *pEnum = NULL;
    IMMDevice *pDevice = NULL;
    IPropertyStore *pProps = NULL;
    PROPVARIANT Value;

    ZeroMemory(Props, sizeof(*Props));
    Props->FormFactor = UnknownFormFactor;

    if (FAILED(CoCreateInstance(&CLSID_MMDeviceEnumerator, NULL, CLSCTX_INPROC_SERVER,
                                &IID_IMMDeviceEnumerator, (void **)&pEnum)))
        return;

    if (SUCCEEDED(IMMDeviceEnumerator_GetDevice(pEnum, pszEndpointId, &pDevice)))
    {
        if (SUCCEEDED(IMMDevice_OpenPropertyStore(pDevice, STGM_READ, &pProps)))
        {
            ReadStringProperty(pProps, &EndpointNameKey, Props->Name, _countof(Props->Name));
            PropVariantInit(&Value);
            if (SUCCEEDED(IPropertyStore_GetValue(pProps, &EndpointFormFactorKey, &Value)) && Value.vt == VT_UI4)
                Props->FormFactor = Value.ulVal;
            PropVariantClear(&Value);
            IPropertyStore_Release(pProps);
        }
        ReadControllerInstanceId(pEnum, pDevice, Props);
        IMMDevice_Release(pDevice);
    }
    IMMDeviceEnumerator_Release(pEnum);

    ReadDriver(Props);
}

static UINT
FormFactorString(UINT FormFactor)
{
    switch (FormFactor)
    {
        case Speakers: return IDS_FORMFACTOR_SPEAKERS;
        case LineLevel: return IDS_FORMFACTOR_LINE;
        case Headphones: return IDS_FORMFACTOR_HEADPHONES;
        case Microphone: return IDS_FORMFACTOR_MICROPHONE;
        case Headset: return IDS_FORMFACTOR_HEADSET;
        case Handset: return IDS_FORMFACTOR_HANDSET;
        case SPDIF: return IDS_FORMFACTOR_SPDIF;
        case DigitalAudioDisplayDevice: return IDS_FORMFACTOR_HDMI;
        default: return IDS_FORMFACTOR_UNKNOWN;
    }
}

static VOID
SetTextOrUnknown(HWND hwndDlg, INT Id, LPCWSTR Text)
{
    WCHAR Unknown[64];

    if (Text && Text[0])
    {
        SetDlgItemTextW(hwndDlg, Id, Text);
        return;
    }
    LoadStringW(hApplet, IDS_UNKNOWN_VALUE, Unknown, _countof(Unknown));
    SetDlgItemTextW(hwndDlg, Id, Unknown);
}

static INT_PTR CALLBACK
EndpointGeneralDlgProc(HWND hwndDlg, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    PENDPOINT_PROPS Props = (PENDPOINT_PROPS)GetWindowLongPtrW(hwndDlg, DWLP_USER);
    WCHAR Text[128];

    switch (uMsg)
    {
        case WM_INITDIALOG:
            Props = (PENDPOINT_PROPS)((LPPROPSHEETPAGEW)lParam)->lParam;
            SetWindowLongPtrW(hwndDlg, DWLP_USER, (LONG_PTR)Props);
            SetTextOrUnknown(hwndDlg, IDC_EP_NAME, Props->Name);
            LoadStringW(hApplet, FormFactorString(Props->FormFactor), Text, _countof(Text));
            SetDlgItemTextW(hwndDlg, IDC_EP_JACK, Text);
            SetTextOrUnknown(hwndDlg, IDC_EP_CONTROLLER, Props->Controller);
            SetTextOrUnknown(hwndDlg, IDC_EP_MANUFACTURER, Props->Manufacturer);
            SetTextOrUnknown(hwndDlg, IDC_EP_PROVIDER, Props->DriverProvider);
            SetTextOrUnknown(hwndDlg, IDC_EP_VERSION, Props->DriverVersion);
            SetTextOrUnknown(hwndDlg, IDC_EP_DATE, Props->DriverDate);
            EnableWindow(GetDlgItem(hwndDlg, IDC_EP_PROPERTIES), Props->InstanceId[0] != UNICODE_NULL);
            return TRUE;

        case WM_COMMAND:
            if (LOWORD(wParam) == IDC_EP_PROPERTIES && Props && Props->InstanceId[0])
                DevicePropertiesW(hwndDlg, NULL, Props->InstanceId, FALSE);
            break;
    }
    return FALSE;
}

/* Windows 11: Sound control panel, device Properties, General tab with the controller and its driver */
VOID
ShowEndpointProperties(HWND hwndOwner, LPCWSTR pszEndpointId)
{
    PROPSHEETHEADERW psh;
    PROPSHEETPAGEW psp;
    PENDPOINT_PROPS Props;
    HRESULT hrInit;

    if (!pszEndpointId)
        return;
    Props = HeapAlloc(GetProcessHeap(), 0, sizeof(*Props));
    if (!Props)
        return;

    hrInit = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    ReadEndpoint(pszEndpointId, Props);

    ZeroMemory(&psp, sizeof(psp));
    psp.dwSize = sizeof(psp);
    psp.hInstance = hApplet;
    psp.pszTemplate = MAKEINTRESOURCEW(IDD_ENDPOINT_GENERAL);
    psp.pfnDlgProc = EndpointGeneralDlgProc;
    psp.lParam = (LPARAM)Props;

    ZeroMemory(&psh, sizeof(psh));
    psh.dwSize = sizeof(psh);
    psh.dwFlags = PSH_PROPSHEETPAGE | PSH_PROPTITLE | PSH_NOAPPLYNOW;
    psh.hwndParent = hwndOwner;
    psh.hInstance = hApplet;
    psh.pszCaption = Props->Name[0] ? Props->Name : pszEndpointId;
    psh.nPages = 1;
    psh.ppsp = &psp;
    PropertySheetW(&psh);

    if (SUCCEEDED(hrInit))
        CoUninitialize();
    HeapFree(GetProcessHeap(), 0, Props);
}
