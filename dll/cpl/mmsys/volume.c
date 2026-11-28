/*
 * PROJECT:         ReactOS Multimedia Control Panel
 * FILE:            dll/cpl/mmsys/volume.c
 * PURPOSE:         ReactOS Multimedia Control Panel
 * PROGRAMMER:      Thomas Weidenmueller <w3seek@reactos.com>
 *                  Johannes Anderwald <janderwald@reactos.com>
 *                  Dmitry Chapyshev <dmitry.chapyshev@reactos.org>
 */

#define COBJMACROS
#include "mmsys.h"

#include <shellapi.h>
#include <objbase.h>
#include <initguid.h>
#include <mmdeviceapi.h>
#include <endpointvolume.h>

/*
 * The Volume tab is the default render endpoint's, the one the tray
 * controls (N14-01): its endpoint volume, the slider the level in
 * 1/VOLUME_MAX of the scalar the tray moves in 1/100, the checkbox its
 * mute. Not mixer device 0's, which on a machine with more than one audio
 * device is whichever the kernel mixer listed first.
 */

/* posted by the control-change callback when the level or the mute moved elsewhere (the tray, the hot keys) */
#define WM_VOLUME_CHANGED   (WM_APP + 1)

static const PROPERTYKEY DeviceFriendlyNameKey =
    { { 0xa45c254e, 0xdf1c, 0x4efd, { 0x80, 0x20, 0x67, 0xd1, 0x46, 0xa8, 0x50, 0xe0 } }, 14 };

typedef struct _VOLUME_CALLBACK
{
    IAudioEndpointVolumeCallback INotify;
    LONG References;
    HWND Window;
} VOLUME_CALLBACK, *PVOLUME_CALLBACK;

typedef struct _GLOBAL_DATA
{
    BOOL bComInit;
    IMMDeviceEnumerator *Enumerator;
    IMMDevice *Device;
    IAudioEndpointVolume *Volume;
    IAudioEndpointVolumeCallback *Callback;

    HICON hIconMuted;
    HICON hIconUnMuted;
    HICON hIconNoHW;
    HICON hIconSpeakImg;

    BOOL muteVal;

    /* the level and the mute the page found, put back on Cancel */
    float initialLevel;
    BOOL initialMute;

} GLOBAL_DATA, *PGLOBAL_DATA;

static
HRESULT WINAPI
VolumeCallback_QueryInterface(IAudioEndpointVolumeCallback *iface, REFIID riid, void **ppv)
{
    PVOLUME_CALLBACK Notify = (PVOLUME_CALLBACK)iface;

    if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_IAudioEndpointVolumeCallback))
    {
        *ppv = &Notify->INotify;
        InterlockedIncrement(&Notify->References);
        return S_OK;
    }
    *ppv = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI
VolumeCallback_AddRef(IAudioEndpointVolumeCallback *iface)
{
    return InterlockedIncrement(&((PVOLUME_CALLBACK)iface)->References);
}

static ULONG WINAPI
VolumeCallback_Release(IAudioEndpointVolumeCallback *iface)
{
    PVOLUME_CALLBACK Notify = (PVOLUME_CALLBACK)iface;
    ULONG References = InterlockedDecrement(&Notify->References);

    if (!References)
        HeapFree(GetProcessHeap(), 0, Notify);
    return References;
}

/* the notification's own values are not trusted for the page: the page reads the endpoint again on its own thread */
static HRESULT WINAPI
VolumeCallback_OnNotify(IAudioEndpointVolumeCallback *iface, PAUDIO_VOLUME_NOTIFICATION_DATA pNotify)
{
    UNREFERENCED_PARAMETER(pNotify);
    PostMessageW(((PVOLUME_CALLBACK)iface)->Window, WM_VOLUME_CHANGED, 0, 0);
    return S_OK;
}

static IAudioEndpointVolumeCallbackVtbl g_VolumeCallbackVtbl = {
    VolumeCallback_QueryInterface,
    VolumeCallback_AddRef,
    VolumeCallback_Release,
    VolumeCallback_OnNotify
};

static
IAudioEndpointVolumeCallback *
CreateVolumeCallback(HWND Window)
{
    PVOLUME_CALLBACK Notify = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*Notify));

    if (!Notify)
        return NULL;
    Notify->INotify.lpVtbl = &g_VolumeCallbackVtbl;
    Notify->References = 1;
    Notify->Window = Window;
    return &Notify->INotify;
}

static
VOID
UpdateMuteControls(
    HWND hwndDlg,
    PGLOBAL_DATA pGlobalData)
{
    SendDlgItemMessageW(hwndDlg, IDC_MUTE_CHECKBOX, BM_SETCHECK, (WPARAM)(pGlobalData->muteVal ? BST_CHECKED : BST_UNCHECKED), 0);
    SendDlgItemMessageW(hwndDlg, IDC_MUTE_ICON, STM_SETIMAGE, IMAGE_ICON,
                        (LPARAM)(pGlobalData->muteVal ? pGlobalData->hIconMuted : pGlobalData->hIconUnMuted));
}

static
VOID
GetMuteState(PGLOBAL_DATA pGlobalData)
{
    BOOL bMute = FALSE;

    if (SUCCEEDED(IAudioEndpointVolume_GetMute(pGlobalData->Volume, &bMute)))
        pGlobalData->muteVal = bMute;
}

static
VOID
SwitchMuteState(PGLOBAL_DATA pGlobalData)
{
    if (SUCCEEDED(IAudioEndpointVolume_SetMute(pGlobalData->Volume, !pGlobalData->muteVal, NULL)))
        GetMuteState(pGlobalData);
}

/* the level the page reads now, as the slider's position */
static
DWORD
VolumePosition(PGLOBAL_DATA pGlobalData)
{
    float fLevel = 0.0f;

    if (FAILED(IAudioEndpointVolume_GetMasterVolumeLevelScalar(pGlobalData->Volume, &fLevel)))
        return VOLUME_MIN;
    return (DWORD)(fLevel * (float)VOLUME_MAX + 0.5f);
}

static
VOID
SetVolumeValue(PGLOBAL_DATA pGlobalData,
               DWORD dwPosition)
{
    IAudioEndpointVolume_SetMasterVolumeLevelScalar(pGlobalData->Volume, (float)dwPosition / (float)VOLUME_MAX, NULL);
}

static
VOID
RestoreVolumeValue(PGLOBAL_DATA pGlobalData)
{
    IAudioEndpointVolume_SetMasterVolumeLevelScalar(pGlobalData->Volume, pGlobalData->initialLevel, NULL);
    IAudioEndpointVolume_SetMute(pGlobalData->Volume, pGlobalData->initialMute, NULL);
}

static
VOID
CloseEndpoint(PGLOBAL_DATA pGlobalData)
{
    if (pGlobalData->Callback)
    {
        IAudioEndpointVolume_UnregisterControlChangeNotify(pGlobalData->Volume, pGlobalData->Callback);
        IAudioEndpointVolumeCallback_Release(pGlobalData->Callback);
        pGlobalData->Callback = NULL;
    }
    if (pGlobalData->Volume)
    {
        IAudioEndpointVolume_Release(pGlobalData->Volume);
        pGlobalData->Volume = NULL;
    }
    if (pGlobalData->Device)
    {
        IMMDevice_Release(pGlobalData->Device);
        pGlobalData->Device = NULL;
    }
    if (pGlobalData->Enumerator)
    {
        IMMDeviceEnumerator_Release(pGlobalData->Enumerator);
        pGlobalData->Enumerator = NULL;
    }
}

/* opens the default render endpoint and its volume, and registers the callback; the device's friendly name, or its id, into Name */
static
BOOL
OpenDefaultEndpoint(
    PGLOBAL_DATA pGlobalData,
    HWND hwndDlg,
    LPWSTR Name,
    SIZE_T Chars)
{
    IPropertyStore *pProps = NULL;
    PROPVARIANT FriendlyName;
    LPWSTR pszId = NULL;

    Name[0] = L'\0';
    if (FAILED(CoCreateInstance(&CLSID_MMDeviceEnumerator, NULL, CLSCTX_INPROC_SERVER,
                                &IID_IMMDeviceEnumerator, (void **)&pGlobalData->Enumerator)))
    {
        return FALSE;
    }

    /* the role the tray asks of the default render device (trayflyouts.cpp), the applet's own as the second */
    if (FAILED(IMMDeviceEnumerator_GetDefaultAudioEndpoint(pGlobalData->Enumerator, eRender, eMultimedia, &pGlobalData->Device)) &&
        FAILED(IMMDeviceEnumerator_GetDefaultAudioEndpoint(pGlobalData->Enumerator, eRender, eConsole, &pGlobalData->Device)))
    {
        return FALSE;
    }

    if (FAILED(IMMDevice_Activate(pGlobalData->Device, &IID_IAudioEndpointVolume, CLSCTX_INPROC_SERVER, NULL,
                                  (void **)&pGlobalData->Volume)))
    {
        return FALSE;
    }

    pGlobalData->Callback = CreateVolumeCallback(hwndDlg);
    if (pGlobalData->Callback == NULL ||
        FAILED(IAudioEndpointVolume_RegisterControlChangeNotify(pGlobalData->Volume, pGlobalData->Callback)))
    {
        return FALSE;
    }

    PropVariantInit(&FriendlyName);
    if (SUCCEEDED(IMMDevice_OpenPropertyStore(pGlobalData->Device, STGM_READ, &pProps)))
    {
        IPropertyStore_GetValue(pProps, &DeviceFriendlyNameKey, &FriendlyName);
        IPropertyStore_Release(pProps);
    }
    if (FriendlyName.vt == VT_LPWSTR && FriendlyName.pwszVal)
        StringCchCopyW(Name, Chars, FriendlyName.pwszVal);
    PropVariantClear(&FriendlyName);
    if (Name[0] == L'\0' && SUCCEEDED(IMMDevice_GetId(pGlobalData->Device, &pszId)))
    {
        StringCchCopyW(Name, Chars, pszId);
        CoTaskMemFree(pszId);
    }
    return TRUE;
}

static
VOID
SetSystrayVolumeIconState(BOOL bEnabled)
{
    HWND hwndTaskBar;

    hwndTaskBar = FindWindowW(L"SystemTray_Main", NULL);
    if (hwndTaskBar == NULL)
        return;

    SendMessageW(hwndTaskBar, WM_USER + 220, 4, bEnabled);
}

static
BOOL
GetSystrayVolumeIconState(VOID)
{
    HWND hwndTaskBar;

    hwndTaskBar = FindWindowW(L"SystemTray_Main", NULL);
    if (hwndTaskBar == NULL)
    {
        return FALSE;
    }

    return (BOOL)SendMessageW(hwndTaskBar, WM_USER + 221, 4, 0);
}

VOID
InitVolumeControls(HWND hwndDlg, PGLOBAL_DATA pGlobalData)
{
    WCHAR szDeviceName[256], szNoDevices[256];
    BOOL bOpened = FALSE;

    LoadStringW(hApplet, IDS_NO_DEVICES, szNoDevices, _countof(szNoDevices));

    SendDlgItemMessageW(hwndDlg, IDC_VOLUME_TRACKBAR, TBM_SETRANGE, (WPARAM)TRUE, (LPARAM)MAKELONG(VOLUME_MIN, VOLUME_MAX));
    SendDlgItemMessageW(hwndDlg, IDC_VOLUME_TRACKBAR, TBM_SETTICFREQ, VOLUME_TICFREQ, 0);
    SendDlgItemMessageW(hwndDlg, IDC_VOLUME_TRACKBAR, TBM_SETPAGESIZE, 0, VOLUME_PAGESIZE);

    pGlobalData->bComInit = SUCCEEDED(CoInitializeEx(NULL, COINIT_APARTMENTTHREADED));
    if (pGlobalData->bComInit)
        bOpened = OpenDefaultEndpoint(pGlobalData, hwndDlg, szDeviceName, _countof(szDeviceName));
    if (bOpened)
    {
        CheckDlgButton(hwndDlg,
                       IDC_ICON_IN_TASKBAR,
                       GetSystrayVolumeIconState() ? BST_CHECKED : BST_UNCHECKED);

        GetMuteState(pGlobalData);
        UpdateMuteControls(hwndDlg, pGlobalData);
        pGlobalData->initialMute = pGlobalData->muteVal;
        if (SUCCEEDED(IAudioEndpointVolume_GetMasterVolumeLevelScalar(pGlobalData->Volume, &pGlobalData->initialLevel)))
        {
            SendDlgItemMessageW(hwndDlg, IDC_DEVICE_NAME, WM_SETTEXT, 0, (LPARAM)szDeviceName);
            SendDlgItemMessageW(hwndDlg, IDC_VOLUME_TRACKBAR, TBM_SETPOS, (WPARAM)TRUE, (LPARAM)VolumePosition(pGlobalData));
            return;
        }
    }

    /* no default render endpoint, or its volume would not open or read: nothing to move */
    CloseEndpoint(pGlobalData);
    if (pGlobalData->bComInit)
    {
        CoUninitialize();
        pGlobalData->bComInit = FALSE;
    }
    EnableWindow(GetDlgItem(hwndDlg, IDC_VOLUME_TRACKBAR), FALSE);
    EnableWindow(GetDlgItem(hwndDlg, IDC_VOLUME_LOW),      FALSE);
    EnableWindow(GetDlgItem(hwndDlg, IDC_VOLUME_HIGH),     FALSE);
    EnableWindow(GetDlgItem(hwndDlg, IDC_MUTE_CHECKBOX),   FALSE);
    EnableWindow(GetDlgItem(hwndDlg, IDC_ICON_IN_TASKBAR), FALSE);
    EnableWindow(GetDlgItem(hwndDlg, IDC_ADVANCED_BTN),    FALSE);
    EnableWindow(GetDlgItem(hwndDlg, IDC_SPEAKER_VOL_BTN), FALSE);
    EnableWindow(GetDlgItem(hwndDlg, IDC_ADVANCED2_BTN),   FALSE);
    SendDlgItemMessageW(hwndDlg, IDC_MUTE_ICON, STM_SETIMAGE, IMAGE_ICON, (LPARAM)pGlobalData->hIconNoHW);
    SetDlgItemTextW(hwndDlg, IDC_DEVICE_NAME, szNoDevices);
}

VOID
SaveData(HWND hwndDlg)
{
    BOOL bShowIcon;

    bShowIcon = (IsDlgButtonChecked(hwndDlg, IDC_ICON_IN_TASKBAR) == BST_CHECKED);

    SetSystrayVolumeIconState(bShowIcon);
}

VOID
LaunchSoundControl(HWND hwndDlg)
{
    if ((INT_PTR)ShellExecuteW(NULL, L"open", L"sndvol32.exe", NULL, NULL, SW_SHOWNORMAL) > 32)
        return;
    MessageBoxW(hwndDlg, L"Cannot run sndvol32.exe", NULL, MB_OK);
}

/* Volume property page dialog callback */
//static INT_PTR CALLBACK
INT_PTR CALLBACK
VolumeDlgProc(HWND hwndDlg,
              UINT uMsg,
              WPARAM wParam,
              LPARAM lParam)
{
    static const INT speakImgSize[] = {72, 72};
    PGLOBAL_DATA pGlobalData;

    pGlobalData = (PGLOBAL_DATA)GetWindowLongPtrW(hwndDlg, DWLP_USER);

    switch (uMsg)
    {
        case WM_VOLUME_CHANGED:
        {
            if (!pGlobalData->Volume)
                break;
            GetMuteState(pGlobalData);
            UpdateMuteControls(hwndDlg, pGlobalData);
            SendDlgItemMessageW(hwndDlg, IDC_VOLUME_TRACKBAR, TBM_SETPOS, (WPARAM)TRUE, (LPARAM)VolumePosition(pGlobalData));
            break;
        }
        case WM_INITDIALOG:
        {
            pGlobalData = (PGLOBAL_DATA)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(GLOBAL_DATA));
            SetWindowLongPtrW(hwndDlg, DWLP_USER, (LONG_PTR)pGlobalData);

            pGlobalData->hIconUnMuted = LoadImageW(hApplet, MAKEINTRESOURCEW(IDI_CPLICON), IMAGE_ICON, 32, 32, LR_DEFAULTCOLOR);
            pGlobalData->hIconMuted = LoadImageW(hApplet, MAKEINTRESOURCEW(IDI_MUTED_ICON), IMAGE_ICON, 32, 32, LR_DEFAULTCOLOR);
            pGlobalData->hIconNoHW = LoadImageW(hApplet, MAKEINTRESOURCEW(IDI_NO_HW), IMAGE_ICON, 32, 32, LR_DEFAULTCOLOR);
            pGlobalData->hIconSpeakImg = LoadImageW(hApplet,
                                                    MAKEINTRESOURCEW(IDI_SPEAKIMG),
                                                    IMAGE_ICON,
                                                    speakImgSize[0],
                                                    speakImgSize[1],
                                                    LR_DEFAULTCOLOR);

            InitVolumeControls(hwndDlg, pGlobalData);
            break;
        }

        case WM_DRAWITEM:
        {
            LPDRAWITEMSTRUCT lpDrawItem;
            lpDrawItem = (LPDRAWITEMSTRUCT)lParam;
            if (lpDrawItem->CtlID == IDC_SPEAKIMG)
            {
                LONG left;

                /* Position image in centre of dialog */
                left = (lpDrawItem->rcItem.right - speakImgSize[0]) / 2;

                DrawIconEx(lpDrawItem->hDC,
                           left,
                           lpDrawItem->rcItem.top,
                           pGlobalData->hIconSpeakImg,
                           speakImgSize[0],
                           speakImgSize[1],
                           0,
                           NULL,
                           DI_NORMAL);
            }
            break;
        }

        case WM_COMMAND:
        {
            switch (LOWORD(wParam))
            {
                case IDC_MUTE_CHECKBOX:
                    if (HIWORD(wParam) == BN_CLICKED)
                    {
                        SwitchMuteState(pGlobalData);
                        UpdateMuteControls(hwndDlg, pGlobalData);

                        PropSheet_Changed(GetParent(hwndDlg), hwndDlg);
                    }
                    break;

                case IDC_ICON_IN_TASKBAR:
                    if (HIWORD(wParam) == BN_CLICKED)
                    {
                        PropSheet_Changed(GetParent(hwndDlg), hwndDlg);
                    }
                    break;

                case IDC_ADVANCED_BTN:
                    LaunchSoundControl(hwndDlg);
                    break;

                case IDC_SPEAKER_VOL_BTN:
                    SpeakerVolume(hwndDlg);
                    break;
            }
            break;
        }

        case WM_HSCROLL:
        {
            HWND hVolumeTrackbar = GetDlgItem(hwndDlg, IDC_VOLUME_TRACKBAR);
            if (hVolumeTrackbar == (HWND)lParam)
            {
                switch (LOWORD(wParam))
                {
                    case TB_THUMBPOSITION:
                        break;

                    case TB_ENDTRACK:
                        PlaySoundW((LPCWSTR)SND_ALIAS_SYSTEMDEFAULT, NULL, SND_ALIAS_ID | SND_ASYNC);
                        break;

                    default:
                        SetVolumeValue(pGlobalData,
                                       (DWORD)SendDlgItemMessageW(hwndDlg, IDC_VOLUME_TRACKBAR, TBM_GETPOS, 0, 0));
                        break;
                }
            }
            break;
        }

        case WM_DESTROY:
            if (pGlobalData)
            {
                CloseEndpoint(pGlobalData);
                if (pGlobalData->bComInit)
                    CoUninitialize();

                if (pGlobalData->hIconSpeakImg)
                    DestroyIcon(pGlobalData->hIconSpeakImg);

                if (pGlobalData->hIconNoHW)
                    DestroyIcon(pGlobalData->hIconNoHW);

                if (pGlobalData->hIconMuted)
                    DestroyIcon(pGlobalData->hIconMuted);

                if (pGlobalData->hIconUnMuted)
                    DestroyIcon(pGlobalData->hIconUnMuted);

                HeapFree(GetProcessHeap(), 0, pGlobalData);
            }
            break;

        case WM_NOTIFY:
            switch (((LPNMHDR)lParam)->code)
            {
                case PSN_APPLY:
                    SaveData(hwndDlg);
                    break;

                case PSN_RESET:
                    if (pGlobalData->Volume)
                        RestoreVolumeValue(pGlobalData);
                    break;
            }
            return TRUE;
    }

    return FALSE;
}
