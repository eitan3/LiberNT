/*
 * PROJECT:     LiberNT Multimedia Control Panel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Playback and Recording pages: the devices of a flow, the default one and their levels
 */

#include "mmsys.h"

#define COBJMACROS
#include <objbase.h>
#include <commctrl.h>
#include <mmdeviceapi.h>
#include <libernt/mmdevdefault.h>
#include <initguid.h>
#include <endpointvolume.h>

static const PROPERTYKEY DeviceNameKey =
    { { 0xa45c254e, 0xdf1c, 0x4efd, { 0x80, 0x20, 0x67, 0xd1, 0x46, 0xa8, 0x50, 0xe0 } }, 14 };
static const PROPERTYKEY DeviceDescKey =
    { { 0xa45c254e, 0xdf1c, 0x4efd, { 0x80, 0x20, 0x67, 0xd1, 0x46, 0xa8, 0x50, 0xe0 } }, 2 };
static const PROPERTYKEY InterfaceNameKey =
    { { 0x026e516e, 0xb814, 0x414b, { 0x83, 0xcd, 0x85, 0x6d, 0x6f, 0xef, 0x48, 0x22 } }, 2 };

#define METER_TIMER_ID 1
#define METER_INTERVAL 100
#define MAX_DEVICES    128
/* Posted by the endpoint notification client, which mmdevapi calls on another thread */
#define WM_DEVICELIST_CHANGED (WM_APP + 1)

typedef struct _DEVICE_ROW
{
    LPWSTR Id;
    IAudioMeterInformation *Meter;
    BOOL Default;
    BOOL DefaultComm;
    INT Level;
} DEVICE_ROW;

typedef struct _DEVICE_PAGE
{
    EDataFlow Flow;
    HRESULT ComInit;
    HWND List;
    HIMAGELIST Images;
    UINT Count;
    WCHAR EmptyText[128];
    IMMDeviceEnumerator *Enumerator;
    IMMNotificationClient *Notification;
    DEVICE_ROW Rows[MAX_DEVICES];
} DEVICE_PAGE, *PDEVICE_PAGE;

typedef struct _DEVICE_NOTIFICATION
{
    IMMNotificationClient INotify;
    LONG References;
    HWND Window;
} DEVICE_NOTIFICATION, *PDEVICE_NOTIFICATION;

static VOID
DeviceListChanged(IMMNotificationClient *iface)
{
    PostMessageW(((PDEVICE_NOTIFICATION)iface)->Window, WM_DEVICELIST_CHANGED, 0, 0);
}

static HRESULT WINAPI
DeviceNotification_QueryInterface(IMMNotificationClient *iface, REFIID riid, void **ppv)
{
    PDEVICE_NOTIFICATION Notify = (PDEVICE_NOTIFICATION)iface;

    if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_IMMNotificationClient))
    {
        *ppv = &Notify->INotify;
        InterlockedIncrement(&Notify->References);
        return S_OK;
    }
    *ppv = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI
DeviceNotification_AddRef(IMMNotificationClient *iface)
{
    return InterlockedIncrement(&((PDEVICE_NOTIFICATION)iface)->References);
}

static ULONG WINAPI
DeviceNotification_Release(IMMNotificationClient *iface)
{
    PDEVICE_NOTIFICATION Notify = (PDEVICE_NOTIFICATION)iface;
    ULONG References = InterlockedDecrement(&Notify->References);

    if (!References)
        HeapFree(GetProcessHeap(), 0, Notify);
    return References;
}

static HRESULT WINAPI
DeviceNotification_OnDeviceStateChanged(IMMNotificationClient *iface, LPCWSTR DeviceId, DWORD NewState)
{
    UNREFERENCED_PARAMETER(DeviceId);
    UNREFERENCED_PARAMETER(NewState);
    DeviceListChanged(iface);
    return S_OK;
}

static HRESULT WINAPI
DeviceNotification_OnDeviceAdded(IMMNotificationClient *iface, LPCWSTR DeviceId)
{
    UNREFERENCED_PARAMETER(DeviceId);
    DeviceListChanged(iface);
    return S_OK;
}

static HRESULT WINAPI
DeviceNotification_OnDeviceRemoved(IMMNotificationClient *iface, LPCWSTR DeviceId)
{
    UNREFERENCED_PARAMETER(DeviceId);
    DeviceListChanged(iface);
    return S_OK;
}

static HRESULT WINAPI
DeviceNotification_OnDefaultDeviceChanged(IMMNotificationClient *iface, EDataFlow Flow, ERole Role,
                                                     LPCWSTR DefaultDeviceId)
{
    UNREFERENCED_PARAMETER(Flow);
    UNREFERENCED_PARAMETER(Role);
    UNREFERENCED_PARAMETER(DefaultDeviceId);
    DeviceListChanged(iface);
    return S_OK;
}

static HRESULT WINAPI
DeviceNotification_OnPropertyValueChanged(IMMNotificationClient *iface, LPCWSTR DeviceId,
                                                PROPERTYKEY Key)
{
    UNREFERENCED_PARAMETER(iface);
    UNREFERENCED_PARAMETER(DeviceId);
    UNREFERENCED_PARAMETER(Key);
    return S_OK;
}

static IMMNotificationClientVtbl g_DeviceNotificationVtbl = {
    DeviceNotification_QueryInterface,
    DeviceNotification_AddRef,
    DeviceNotification_Release,
    DeviceNotification_OnDeviceStateChanged,
    DeviceNotification_OnDeviceAdded,
    DeviceNotification_OnDeviceRemoved,
    DeviceNotification_OnDefaultDeviceChanged,
    DeviceNotification_OnPropertyValueChanged
};

static IMMNotificationClient *
CreateDeviceNotification(HWND Window)
{
    PDEVICE_NOTIFICATION Notify = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*Notify));

    if (!Notify)
        return NULL;
    Notify->INotify.lpVtbl = &g_DeviceNotificationVtbl;
    Notify->References = 1;
    Notify->Window = Window;
    return &Notify->INotify;
}

static VOID
ReadString(IPropertyStore *pProps, const PROPERTYKEY *Key, LPWSTR Buffer, SIZE_T Chars)
{
    PROPVARIANT Value;

    PropVariantInit(&Value);
    if (SUCCEEDED(IPropertyStore_GetValue(pProps, Key, &Value)) && Value.vt == VT_LPWSTR && Value.pwszVal)
        StringCchCopyW(Buffer, Chars, Value.pwszVal);
    PropVariantClear(&Value);
}

static VOID
FreeRows(PDEVICE_PAGE Page)
{
    UINT Index;

    for (Index = 0; Index < Page->Count; Index++)
    {
        if (Page->Rows[Index].Meter)
            IAudioMeterInformation_Release(Page->Rows[Index].Meter);
        CoTaskMemFree(Page->Rows[Index].Id);
    }
    Page->Count = 0;
    ListView_DeleteAllItems(Page->List);
}

/* Without a device the list shows why: the DSP message when the machine has no other way to play sound */
static VOID
ShowEmptyNotice(HWND hwndDlg, PDEVICE_PAGE Page)
{
    HWND Notice = GetDlgItem(hwndDlg, IDC_DEVLIST_EMPTY);
    WCHAR Text[256] = L"";
    RECT Rect = { 0, 0, 0, 9 };

    if (Page->Count)
    {
        ShowWindow(Notice, SW_HIDE);
        return;
    }

    if (Page->Flow == eRender && AudioOutputNeedsDsp() &&
        LoadStringW(hApplet, IDS_NO_ENDPOINT_DSP, Text, _countof(Text)))
    {
        Rect.bottom = 45;
    }
    else
    {
        StringCchCopyW(Text, _countof(Text), Page->EmptyText);
    }

    SetDlgItemTextW(hwndDlg, IDC_DEVLIST_EMPTY, Text);
    MapDialogRect(hwndDlg, &Rect);
    SetWindowPos(Notice, NULL, 0, 0, 0, Rect.bottom, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    ShowWindow(Notice, SW_SHOW);
}

/* Windows lists each device with its name, its description and whether it is the default */
static VOID
LoadDevices(HWND hwndDlg, PDEVICE_PAGE Page)
{
    IMMDeviceCollection *pCollection = NULL;
    IMMDevice *pDefault = NULL, *pDefaultComm = NULL;
    LPWSTR DefaultId = NULL, DefaultCommId = NULL;
    WCHAR Line[256], Status[64] = L"";
    UINT Count = 0, Index;
    INT Selected = -1;

    FreeRows(Page);
    if (!Page->Enumerator)
        return;
    if (SUCCEEDED(IMMDeviceEnumerator_GetDefaultAudioEndpoint(Page->Enumerator, Page->Flow, eConsole, &pDefault)))
    {
        IMMDevice_GetId(pDefault, &DefaultId);
        IMMDevice_Release(pDefault);
    }
    if (SUCCEEDED(IMMDeviceEnumerator_GetDefaultAudioEndpoint(Page->Enumerator, Page->Flow, eCommunications, &pDefaultComm)))
    {
        IMMDevice_GetId(pDefaultComm, &DefaultCommId);
        IMMDevice_Release(pDefaultComm);
    }

    if (SUCCEEDED(IMMDeviceEnumerator_EnumAudioEndpoints(Page->Enumerator, Page->Flow, DEVICE_STATE_ACTIVE | DEVICE_STATE_UNPLUGGED, &pCollection)))
        IMMDeviceCollection_GetCount(pCollection, &Count);

    for (Index = 0; Index < Count && Page->Count < MAX_DEVICES; Index++)
    {
        DEVICE_ROW *Row = &Page->Rows[Page->Count];
        IMMDevice *pDevice = NULL;
        IPropertyStore *pProps = NULL;
        DWORD State = DEVICE_STATE_ACTIVE;
        WCHAR Name[128] = L"", Desc[128] = L"", Adapter[128] = L"";
        LVITEMW Item;

        if (FAILED(IMMDeviceCollection_Item(pCollection, Index, &pDevice)))
            continue;
        ZeroMemory(Row, sizeof(*Row));
        if (FAILED(IMMDevice_GetId(pDevice, &Row->Id)))
        {
            IMMDevice_Release(pDevice);
            continue;
        }
        if (SUCCEEDED(IMMDevice_OpenPropertyStore(pDevice, STGM_READ, &pProps)))
        {
            ReadString(pProps, &DeviceDescKey, Desc, _countof(Desc));
            ReadString(pProps, &InterfaceNameKey, Adapter, _countof(Adapter));
            ReadString(pProps, &DeviceNameKey, Name, _countof(Name));
            IPropertyStore_Release(pProps);
        }
        IMMDevice_Activate(pDevice, &IID_IAudioMeterInformation, CLSCTX_INPROC_SERVER, NULL, (void **)&Row->Meter);
        IMMDevice_GetState(pDevice, &State);
        IMMDevice_Release(pDevice);
        Row->Default = DefaultId && !wcscmp(DefaultId, Row->Id);
        Row->DefaultComm = DefaultCommId && !wcscmp(DefaultCommId, Row->Id);

        ZeroMemory(&Item, sizeof(Item));
        Item.mask = LVIF_TEXT | LVIF_IMAGE | LVIF_PARAM;
        Item.iItem = Page->Count;
        Item.iImage = Page->Flow == eRender ? 0 : 1;
        Item.lParam = Page->Count;
        Item.pszText = Desc[0] ? Desc : (Name[0] ? Name : Row->Id);
        ListView_InsertItem(Page->List, &Item);

        StringCchCopyW(Line, _countof(Line), Adapter[0] ? Adapter : Name);
        ListView_SetItemText(Page->List, Page->Count, 1, Line);
        /* A jack with nothing plugged is listed as not connected (LEG-1) */
        LoadStringW(hApplet, State == DEVICE_STATE_UNPLUGGED ? IDS_DEVICE_NOTCONNECTED :
                    Row->Default ? IDS_DEVICE_DEFAULT : Row->DefaultComm ? IDS_DEVICE_DEFAULT_COMM : IDS_DEVICE_READY,
                    Status, _countof(Status));
        ListView_SetItemText(Page->List, Page->Count, 2, Status);
        if (Row->Default)
            Selected = Page->Count;
        Page->Count++;
    }

    if (pCollection)
        IMMDeviceCollection_Release(pCollection);
    if (DefaultId)
        CoTaskMemFree(DefaultId);
    if (DefaultCommId)
        CoTaskMemFree(DefaultCommId);

    ShowEmptyNotice(hwndDlg, Page);
    if (Page->Count)
        ListView_SetItemState(Page->List, Selected >= 0 ? Selected : 0, LVIS_SELECTED | LVIS_FOCUSED,
                              LVIS_SELECTED | LVIS_FOCUSED);
}

static INT
SelectedRow(PDEVICE_PAGE Page)
{
    INT Item = ListView_GetNextItem(Page->List, -1, LVNI_SELECTED);
    LVITEMW Lv;

    if (Item < 0)
        return -1;
    ZeroMemory(&Lv, sizeof(Lv));
    Lv.mask = LVIF_PARAM;
    Lv.iItem = Item;
    if (!ListView_GetItem(Page->List, &Lv) || (UINT)Lv.lParam >= Page->Count)
        return -1;
    return (INT)Lv.lParam;
}

static VOID
UpdateButtons(HWND hwndDlg, PDEVICE_PAGE Page)
{
    INT Row = SelectedRow(Page);

    EnableWindow(GetDlgItem(hwndDlg, IDC_DEVLIST_DEFAULT), Row >= 0 && !Page->Rows[Row].Default);
    EnableWindow(GetDlgItem(hwndDlg, IDC_DEVLIST_DEFAULT_COMM), Row >= 0 && !Page->Rows[Row].DefaultComm);
    EnableWindow(GetDlgItem(hwndDlg, IDC_DEVLIST_PROPERTIES), Row >= 0);
}

/* Windows applies Set Default at once, not on OK; the user is told when the write fails */
static VOID
SetDefault(HWND hwndDlg, PDEVICE_PAGE Page, ERole Role, INT Row)
{
    if (MMDevSetDefaultEndpoint(Page->Flow, Role, Page->Rows[Row].Id) != ERROR_SUCCESS)
    {
        WCHAR Text[256] = L"", Caption[128] = L"";

        LoadStringW(hApplet, IDS_SET_DEFAULT_FAILED, Text, _countof(Text));
        GetWindowTextW(GetParent(hwndDlg), Caption, _countof(Caption));
        MessageBoxW(hwndDlg, Text, Caption, MB_OK | MB_ICONERROR);
    }
    LoadDevices(hwndDlg, Page);
    UpdateButtons(hwndDlg, Page);
}

/* The level bar of the selected device, as the green meter Windows draws next to each device */
static VOID
UpdateMeter(HWND hwndDlg, PDEVICE_PAGE Page)
{
    INT Row = SelectedRow(Page);
    float Peak = 0.0f;

    if (Row >= 0 && Page->Rows[Row].Meter)
        IAudioMeterInformation_GetPeakValue(Page->Rows[Row].Meter, &Peak);
    SendDlgItemMessageW(hwndDlg, IDC_DEVLIST_METER, PBM_SETPOS, (WPARAM)(Peak * 100.0f + 0.5f), 0);
}

static VOID
InitColumns(PDEVICE_PAGE Page)
{
    static const UINT Titles[] = { IDS_DEVICE_COLUMN_NAME, IDS_DEVICE_COLUMN_ADAPTER, IDS_DEVICE_COLUMN_STATUS };
    static const INT Widths[] = { 34, 44, 22 };
    LVCOLUMNW Column;
    WCHAR Title[64] = L"";
    RECT Rect;
    UINT Index;

    GetClientRect(Page->List, &Rect);
    ZeroMemory(&Column, sizeof(Column));
    Column.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM;
    for (Index = 0; Index < _countof(Titles); Index++)
    {
        LoadStringW(hApplet, Titles[Index], Title, _countof(Title));
        Column.pszText = Title;
        Column.cx = (Rect.right - GetSystemMetrics(SM_CXVSCROLL)) * Widths[Index] / 100;
        Column.iSubItem = Index;
        ListView_InsertColumn(Page->List, Index, &Column);
    }
}

static INT_PTR CALLBACK
DeviceListDlgProc(HWND hwndDlg, UINT uMsg, WPARAM wParam, LPARAM lParam, EDataFlow Flow)
{
    PDEVICE_PAGE Page = (PDEVICE_PAGE)GetWindowLongPtrW(hwndDlg, DWLP_USER);
    INT Row;

    switch (uMsg)
    {
        case WM_INITDIALOG:
        {
            INT Size = GetSystemMetrics(SM_CXICON);

            Page = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*Page));
            SetWindowLongPtrW(hwndDlg, DWLP_USER, (LONG_PTR)Page);
            if (!Page)
                return TRUE;
            Page->ComInit = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
            Page->Flow = Flow;
            Page->List = GetDlgItem(hwndDlg, IDC_DEVLIST_LIST);
            if (SUCCEEDED(CoCreateInstance(&CLSID_MMDeviceEnumerator, NULL, CLSCTX_INPROC_SERVER, &IID_IMMDeviceEnumerator,
                                           (void **)&Page->Enumerator)) &&
                (Page->Notification = CreateDeviceNotification(hwndDlg)))
                IMMDeviceEnumerator_RegisterEndpointNotificationCallback(Page->Enumerator, Page->Notification);
            Page->Images = ImageList_Create(Size, Size, ILC_COLOR32 | ILC_MASK, 2, 0);
            ImageList_AddIcon(Page->Images, LoadIconW(hApplet, MAKEINTRESOURCEW(IDI_CPLICON)));
            ImageList_AddIcon(Page->Images, LoadIconW(hApplet, MAKEINTRESOURCEW(IDI_MICROPHONE_ICON)));
            ListView_SetImageList(Page->List, Page->Images, LVSIL_SMALL);
            ListView_SetExtendedListViewStyle(Page->List, LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER);
            InitColumns(Page);
            SendDlgItemMessageW(hwndDlg, IDC_DEVLIST_METER, PBM_SETRANGE32, 0, 100);
            GetDlgItemTextW(hwndDlg, IDC_DEVLIST_EMPTY, Page->EmptyText, _countof(Page->EmptyText));
            LoadDevices(hwndDlg, Page);
            UpdateButtons(hwndDlg, Page);
            SetTimer(hwndDlg, METER_TIMER_ID, METER_INTERVAL, NULL);
            return TRUE;
        }

        case WM_TIMER:
            if (Page && wParam == METER_TIMER_ID)
                UpdateMeter(hwndDlg, Page);
            break;

        case WM_COMMAND:
            if (!Page)
                break;
            Row = SelectedRow(Page);
            if (LOWORD(wParam) == IDC_DEVLIST_DEFAULT && Row >= 0)
                SetDefault(hwndDlg, Page, eConsole, Row);
            else if (LOWORD(wParam) == IDC_DEVLIST_DEFAULT_COMM && Row >= 0)
                SetDefault(hwndDlg, Page, eCommunications, Row);
            else if (LOWORD(wParam) == IDC_DEVLIST_PROPERTIES && Row >= 0)
            {
                ShowEndpointProperties(hwndDlg, Page->Rows[Row].Id);
                LoadDevices(hwndDlg, Page);
                UpdateButtons(hwndDlg, Page);
            }
            break;

        case WM_NOTIFY:
        {
            LPNMHDR Header = (LPNMHDR)lParam;

            if (!Page || Header->idFrom != IDC_DEVLIST_LIST)
                break;
            if (Header->code == LVN_ITEMCHANGED)
                UpdateButtons(hwndDlg, Page);
            else if (Header->code == NM_DBLCLK && (Row = SelectedRow(Page)) >= 0)
            {
                ShowEndpointProperties(hwndDlg, Page->Rows[Row].Id);
                LoadDevices(hwndDlg, Page);
                UpdateButtons(hwndDlg, Page);
            }
            break;
        }

        case WM_DEVICELIST_CHANGED:
            if (Page)
            {
                LoadDevices(hwndDlg, Page);
                UpdateButtons(hwndDlg, Page);
            }
            return TRUE;

        case WM_DESTROY:
            if (!Page)
                break;
            KillTimer(hwndDlg, METER_TIMER_ID);
            FreeRows(Page);
            if (Page->Images)
                ImageList_Destroy(Page->Images);
            if (Page->Notification)
            {
                if (Page->Enumerator)
                    IMMDeviceEnumerator_UnregisterEndpointNotificationCallback(Page->Enumerator, Page->Notification);
                IMMNotificationClient_Release(Page->Notification);
            }
            if (Page->Enumerator)
                IMMDeviceEnumerator_Release(Page->Enumerator);
            if (SUCCEEDED(Page->ComInit))
                CoUninitialize();
            HeapFree(GetProcessHeap(), 0, Page);
            SetWindowLongPtrW(hwndDlg, DWLP_USER, 0);
            break;
    }
    return FALSE;
}

INT_PTR CALLBACK
PlaybackDlgProc(HWND hwndDlg, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    return DeviceListDlgProc(hwndDlg, uMsg, wParam, lParam, eRender);
}

INT_PTR CALLBACK
RecordingDlgProc(HWND hwndDlg, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    return DeviceListDlgProc(hwndDlg, uMsg, wParam, lParam, eCapture);
}
