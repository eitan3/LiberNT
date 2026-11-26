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
#include <audioclient.h>
#include <devpkey.h>
#include <mmreg.h>
#include <ks.h>
#include <ksmedia.h>
#include <libernt/mmdevdefault.h>

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
/* PKEY_AudioEngine_DeviceFormat: the format the endpoint runs at in shared mode */
static const PROPERTYKEY DeviceFormatKey =
    { { 0xf19f064d, 0x082c, 0x4e27, { 0xbc, 0x73, 0x68, 0x82, 0xa1, 0xbb, 0x8e, 0x4c } }, 0 };
/* PKEY_AudioEngine_OEMFormat: the driver's default format */
static const PROPERTYKEY OemFormatKey =
    { { 0xe4870e26, 0x3cc5, 0x4cd2, { 0xba, 0x46, 0xca, 0x0a, 0x9a, 0x70, 0xed, 0x04 } }, 3 };

#define MAX_ENDPOINT_FORMATS 96

typedef struct _ENDPOINT_PROPS
{
    WCHAR EndpointId[128];
    UINT FormatCount;
    WAVEFORMATEXTENSIBLE Formats[MAX_ENDPOINT_FORMATS];
    INT CurrentFormat;
    INT DefaultFormat;
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

/* A sample of ValidBits carried in a ContainerBits-wide word, as the device takes it */
static VOID
SetPcmFormatValid(WAVEFORMATEXTENSIBLE *Format, DWORD Rate, WORD ContainerBits, WORD ValidBits, WORD Channels)
{
    ZeroMemory(Format, sizeof(*Format));
    Format->Format.wFormatTag = WAVE_FORMAT_EXTENSIBLE;
    Format->Format.nChannels = Channels;
    Format->Format.nSamplesPerSec = Rate;
    Format->Format.wBitsPerSample = ContainerBits;
    Format->Format.nBlockAlign = Channels * ContainerBits / 8;
    Format->Format.nAvgBytesPerSec = Rate * Format->Format.nBlockAlign;
    Format->Format.cbSize = sizeof(*Format) - sizeof(Format->Format);
    Format->Samples.wValidBitsPerSample = ValidBits;
    Format->dwChannelMask = Channels == 1 ? SPEAKER_FRONT_CENTER : SPEAKER_FRONT_LEFT | SPEAKER_FRONT_RIGHT;
    Format->SubFormat = KSDATAFORMAT_SUBTYPE_PCM;
}

static INT
FindFormat(PENDPOINT_PROPS Props, const BYTE *Data, DWORD cbSize)
{
    const WAVEFORMATEX *Format;
    WORD ValidBits;
    UINT Index;

    if (cbSize < sizeof(WAVEFORMATEX))
        return -1;
    Format = (const WAVEFORMATEX *)Data;
    ValidBits = Format->wBitsPerSample;
    if (Format->wFormatTag == WAVE_FORMAT_EXTENSIBLE &&
        cbSize >= sizeof(WAVEFORMATEXTENSIBLE) &&
        Format->cbSize >= sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX))
    {
        ValidBits = ((const WAVEFORMATEXTENSIBLE *)Format)->Samples.wValidBitsPerSample;
    }
    for (Index = 0; Index < Props->FormatCount; Index++)
    {
        if (Props->Formats[Index].Format.nSamplesPerSec == Format->nSamplesPerSec &&
            Props->Formats[Index].Format.wBitsPerSample == Format->wBitsPerSample &&
            Props->Formats[Index].Format.nChannels == Format->nChannels &&
            Props->Formats[Index].Samples.wValidBitsPerSample == ValidBits)
        {
            return (INT)Index;
        }
    }
    return -1;
}

/* The formats the device itself accepts, as Windows 11 offers them under Default Format.
   Depths above 16 bits have no legacy capability flag: the driver is asked for each. */
static VOID
ReadFormats(IMMDevice *pDevice, IPropertyStore *pProps, PENDPOINT_PROPS Props)
{
    static const DWORD Rates[] = { 48000, 44100, 96000, 192000, 176400, 88200, 32000, 22050, 11025, 8000 };
    /* Container and valid bits: 16-bit, 24-in-32, 32-bit, then 20-in-32 for a codec without 24 */
    static const WORD Depths[][2] = { {16, 16}, {32, 24}, {32, 32}, {32, 20} };
    IAudioClient *pClient = NULL;
    PROPVARIANT Value;
    WAVEFORMATEXTENSIBLE UserFormat;
    WORD Channels;
    UINT Rate, Depth;
    BOOL Has24 = FALSE;

    Props->CurrentFormat = Props->DefaultFormat = -1;
    if (FAILED(IMMDevice_Activate(pDevice, &IID_IAudioClient, CLSCTX_INPROC_SERVER, NULL, (void **)&pClient)))
        return;

    for (Channels = 1; Channels <= 2; Channels++)
    {
        for (Rate = 0; Rate < _countof(Rates) && Props->FormatCount < MAX_ENDPOINT_FORMATS; Rate++)
        {
            for (Depth = 0; Depth < _countof(Depths) && Props->FormatCount < MAX_ENDPOINT_FORMATS; Depth++)
            {
                WAVEFORMATEXTENSIBLE *Format = &Props->Formats[Props->FormatCount];

                /* 20-bit is offered only to a codec that has no 24-bit */
                if (Depths[Depth][1] == 20 && Has24)
                    continue;
                SetPcmFormatValid(Format, Rates[Rate], Depths[Depth][0], Depths[Depth][1], Channels);
                if (IAudioClient_IsFormatSupported(pClient, AUDCLNT_SHAREMODE_EXCLUSIVE, &Format->Format, NULL) != S_OK)
                    continue;
                if (Depths[Depth][1] == 24)
                    Has24 = TRUE;
                Props->FormatCount++;
            }
        }
    }
    IAudioClient_Release(pClient);

    /* The user's own choice comes before the device property, as mmdevapi reads them; it is a local, never a blob to free */
    PropVariantInit(&Value);
    if (MMDevGetDeviceFormat(Props->EndpointId, &UserFormat) == ERROR_SUCCESS)
        Props->CurrentFormat = FindFormat(Props, (const BYTE *)&UserFormat, sizeof(UserFormat));
    if (Props->CurrentFormat < 0 && SUCCEEDED(IPropertyStore_GetValue(pProps, &DeviceFormatKey, &Value)))
        Props->CurrentFormat = Value.vt == VT_BLOB ? FindFormat(Props, Value.blob.pBlobData, Value.blob.cbSize) : -1;
    PropVariantClear(&Value);
    if (SUCCEEDED(IPropertyStore_GetValue(pProps, &OemFormatKey, &Value)))
        Props->DefaultFormat = Value.vt == VT_BLOB ? FindFormat(Props, Value.blob.pBlobData, Value.blob.cbSize) : -1;
    PropVariantClear(&Value);
}

/* Stores the chosen format: in the device property when this user may write it, else per user */
static BOOL
SaveFormat(PENDPOINT_PROPS Props, INT Index)
{
    IMMDeviceEnumerator *pEnum = NULL;
    IMMDevice *pDevice = NULL;
    IPropertyStore *pProps = NULL;
    PROPVARIANT Value;
    HRESULT hr = E_FAIL;

    if (Index < 0 || (UINT)Index >= Props->FormatCount)
        return FALSE;
    if (SUCCEEDED(CoCreateInstance(&CLSID_MMDeviceEnumerator, NULL, CLSCTX_INPROC_SERVER,
                                   &IID_IMMDeviceEnumerator, (void **)&pEnum)) &&
        SUCCEEDED(IMMDeviceEnumerator_GetDevice(pEnum, Props->EndpointId, &pDevice)) &&
        SUCCEEDED(IMMDevice_OpenPropertyStore(pDevice, STGM_READWRITE, &pProps)))
    {
        PropVariantInit(&Value);
        Value.vt = VT_BLOB;
        Value.blob.cbSize = sizeof(Props->Formats[Index]);
        Value.blob.pBlobData = (BYTE *)&Props->Formats[Index];
        hr = IPropertyStore_SetValue(pProps, &DeviceFormatKey, &Value);
        /* The device property applies to every user, so the per-user choice must go */
        if (SUCCEEDED(hr) && MMDevSetDeviceFormat(Props->EndpointId, NULL) != ERROR_SUCCESS)
            hr = E_FAIL;
    }
    if (FAILED(hr))
        hr = MMDevSetDeviceFormat(Props->EndpointId, &Props->Formats[Index]) == ERROR_SUCCESS ? S_OK : E_FAIL;
    if (pProps)
        IPropertyStore_Release(pProps);
    if (pDevice)
        IMMDevice_Release(pDevice);
    if (pEnum)
        IMMDeviceEnumerator_Release(pEnum);
    if (SUCCEEDED(hr))
        Props->CurrentFormat = Index;
    return SUCCEEDED(hr);
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
    Props->CurrentFormat = Props->DefaultFormat = -1;
    StringCchCopyW(Props->EndpointId, _countof(Props->EndpointId), pszEndpointId);

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
            ReadFormats(pDevice, pProps, Props);
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
    WCHAR Unknown[64] = L"";

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
    WCHAR Text[128] = L"";

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

static VOID
FormatName(const WAVEFORMATEXTENSIBLE *Format, LPWSTR Buffer, SIZE_T Chars)
{
    WCHAR Template[64], Quality[32] = L"";
    WORD ValidBits = Format->Samples.wValidBitsPerSample;
    UINT QualityId = 0;

    LoadStringW(hApplet, IDS_FORMAT_TEMPLATE, Template, _countof(Template));
    StringCchPrintfW(Buffer, Chars, Template, Format->Format.nChannels, ValidBits,
                     Format->Format.nSamplesPerSec);
    if (ValidBits != Format->Format.wBitsPerSample)
    {
        WCHAR Container[64], Formatted[64];

        LoadStringW(hApplet, IDS_FORMAT_CONTAINER, Container, _countof(Container));
        StringCchPrintfW(Formatted, _countof(Formatted), Container, Format->Format.wBitsPerSample);
        StringCchCatW(Buffer, Chars, Formatted);
    }
    if (ValidBits == 16 && Format->Format.nSamplesPerSec == 44100)
        QualityId = IDS_FORMAT_CD;
    else if (ValidBits == 16 && Format->Format.nSamplesPerSec == 48000)
        QualityId = IDS_FORMAT_DVD;
    else if (ValidBits >= 24)
        QualityId = IDS_FORMAT_STUDIO;
    if (QualityId && LoadStringW(hApplet, QualityId, Quality, _countof(Quality)))
    {
        StringCchCatW(Buffer, Chars, L" ");
        StringCchCatW(Buffer, Chars, Quality);
    }
}

static INT_PTR CALLBACK
EndpointAdvancedDlgProc(HWND hwndDlg, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    PENDPOINT_PROPS Props = (PENDPOINT_PROPS)GetWindowLongPtrW(hwndDlg, DWLP_USER);
    HWND hCombo = GetDlgItem(hwndDlg, IDC_EP_FORMAT);
    WCHAR Text[128];
    UINT Index;

    switch (uMsg)
    {
        case WM_INITDIALOG:
            Props = (PENDPOINT_PROPS)((LPPROPSHEETPAGEW)lParam)->lParam;
            SetWindowLongPtrW(hwndDlg, DWLP_USER, (LONG_PTR)Props);
            for (Index = 0; Index < Props->FormatCount; Index++)
            {
                FormatName(&Props->Formats[Index], Text, _countof(Text));
                SendMessageW(hCombo, CB_ADDSTRING, 0, (LPARAM)Text);
            }
            SendMessageW(hCombo, CB_SETCURSEL, Props->CurrentFormat, 0);
            EnableWindow(hCombo, Props->FormatCount > 0);
            EnableWindow(GetDlgItem(hwndDlg, IDC_EP_RESTORE), Props->DefaultFormat >= 0);
            return TRUE;

        case WM_COMMAND:
            if (!Props)
                break;
            if (LOWORD(wParam) == IDC_EP_FORMAT && HIWORD(wParam) == CBN_SELCHANGE)
                PropSheet_Changed(GetParent(hwndDlg), hwndDlg);
            else if (LOWORD(wParam) == IDC_EP_RESTORE && Props->DefaultFormat >= 0)
            {
                SendMessageW(hCombo, CB_SETCURSEL, Props->DefaultFormat, 0);
                PropSheet_Changed(GetParent(hwndDlg), hwndDlg);
            }
            break;

        case WM_NOTIFY:
            if (Props && ((LPNMHDR)lParam)->code == PSN_APPLY)
            {
                INT Selected = (INT)SendMessageW(hCombo, CB_GETCURSEL, 0, 0);

                if (Selected >= 0 && Selected != Props->CurrentFormat && !SaveFormat(Props, Selected))
                {
                    WCHAR Caption[128];

                    LoadStringW(hApplet, IDS_FORMAT_SAVE_FAILED, Text, _countof(Text));
                    GetWindowTextW(GetParent(hwndDlg), Caption, _countof(Caption));
                    MessageBoxW(hwndDlg, Text, Caption, MB_OK | MB_ICONERROR);
                    SetWindowLongPtrW(hwndDlg, DWLP_MSGRESULT, PSNRET_INVALID);
                    return TRUE;
                }
                SetWindowLongPtrW(hwndDlg, DWLP_MSGRESULT, PSNRET_NOERROR);
                return TRUE;
            }
            break;
    }
    return FALSE;
}

/* Windows 11: Sound control panel, device Properties: General (controller and driver) and Advanced (Default Format) */
VOID
ShowEndpointProperties(HWND hwndOwner, LPCWSTR pszEndpointId)
{
    PROPSHEETHEADERW psh;
    PROPSHEETPAGEW psp[2];
    PENDPOINT_PROPS Props;
    HRESULT hrInit;

    if (!pszEndpointId)
        return;
    Props = HeapAlloc(GetProcessHeap(), 0, sizeof(*Props));
    if (!Props)
        return;

    hrInit = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    ReadEndpoint(pszEndpointId, Props);

    ZeroMemory(psp, sizeof(psp));
    psp[0].dwSize = sizeof(psp[0]);
    psp[0].hInstance = hApplet;
    psp[0].pszTemplate = MAKEINTRESOURCEW(IDD_ENDPOINT_GENERAL);
    psp[0].pfnDlgProc = EndpointGeneralDlgProc;
    psp[0].lParam = (LPARAM)Props;
    psp[1] = psp[0];
    psp[1].pszTemplate = MAKEINTRESOURCEW(IDD_ENDPOINT_ADVANCED);
    psp[1].pfnDlgProc = EndpointAdvancedDlgProc;

    ZeroMemory(&psh, sizeof(psh));
    psh.dwSize = sizeof(psh);
    psh.dwFlags = PSH_PROPSHEETPAGE | PSH_PROPTITLE;
    psh.hwndParent = hwndOwner;
    psh.hInstance = hApplet;
    psh.pszCaption = Props->Name[0] ? Props->Name : pszEndpointId;
    psh.nPages = _countof(psp);
    psh.ppsp = psp;
    PropertySheetW(&psh);

    if (SUCCEEDED(hrInit))
        CoUninitialize();
    HeapFree(GetProcessHeap(), 0, Props);
}
