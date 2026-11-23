/*
 *
 * PROJECT:         ReactOS Multimedia Control Panel
 * FILE:            dll/cpl/mmsys/audio.c
 * PURPOSE:         ReactOS Multimedia Control Panel
 * PROGRAMMER:      Thomas Weidenmueller <w3seek@reactos.com>
 *                  Johannes Anderwald <janderwald@reactos.com>
 *                  Dmitry Chapyshev <dmitry@reactos.org>
 */

#include "mmsys.h"

#define COBJMACROS
#include <objbase.h>
#include <initguid.h>
#include <mmdeviceapi.h>
#include <reactos/mmdevdefault.h>

/* mmddk.h; mmsys.h already defines the DRVM_MAPPER messages that header would redefine */
#define DRV_QUERYFUNCTIONINSTANCEID (DRV_RESERVED + 17)

static const PROPERTYKEY DeviceFriendlyNameKey =
    { { 0xa45c254e, 0xdf1c, 0x4efd, { 0x80, 0x20, 0x67, 0xd1, 0x46, 0xa8, 0x50, 0xe0 } }, 14 };

typedef struct _GLOBAL_DATA
{
    BOOL bNoAudioOut;
    BOOL bNoAudioIn;
    BOOL bNoMIDIOut;

    BOOL bAudioOutChanged;
    BOOL bAudioInChanged;
    BOOL bMIDIOutChanged;

} GLOBAL_DATA, *PGLOBAL_DATA;

/* Fill a device list with the active endpoints of a flow, each item holding its endpoint ID, and select the default */
static BOOL
FillEndpointList(HWND hCB, EDataFlow Flow)
{
    IMMDeviceEnumerator *pEnum = NULL;
    IMMDeviceCollection *pCollection = NULL;
    IMMDevice *pDefault = NULL;
    LPWSTR pszDefaultId = NULL;
    UINT Count = 0, Index;
    LRESULT Res;
    HRESULT hrInit = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);

    if (FAILED(CoCreateInstance(&CLSID_MMDeviceEnumerator, NULL, CLSCTX_INPROC_SERVER,
                                &IID_IMMDeviceEnumerator, (void **)&pEnum)))
    {
        if (SUCCEEDED(hrInit))
            CoUninitialize();
        return FALSE;
    }

    if (SUCCEEDED(IMMDeviceEnumerator_GetDefaultAudioEndpoint(pEnum, Flow, eConsole, &pDefault)))
    {
        IMMDevice_GetId(pDefault, &pszDefaultId);
        IMMDevice_Release(pDefault);
    }

    if (SUCCEEDED(IMMDeviceEnumerator_EnumAudioEndpoints(pEnum, Flow, DEVICE_STATE_ACTIVE, &pCollection)))
    {
        IMMDeviceCollection_GetCount(pCollection, &Count);
        for (Index = 0; Index < Count; Index++)
        {
            IMMDevice *pDevice = NULL;
            IPropertyStore *pProps = NULL;
            LPWSTR pszId = NULL, pszItemId;
            PROPVARIANT Name;

            if (FAILED(IMMDeviceCollection_Item(pCollection, Index, &pDevice)))
                continue;
            if (FAILED(IMMDevice_GetId(pDevice, &pszId)))
            {
                IMMDevice_Release(pDevice);
                continue;
            }

            PropVariantInit(&Name);
            if (SUCCEEDED(IMMDevice_OpenPropertyStore(pDevice, STGM_READ, &pProps)))
            {
                IPropertyStore_GetValue(pProps, &DeviceFriendlyNameKey, &Name);
                IPropertyStore_Release(pProps);
            }

            Res = SendMessageW(hCB, CB_ADDSTRING, 0,
                               (LPARAM)(Name.vt == VT_LPWSTR && Name.pwszVal ? Name.pwszVal : pszId));
            pszItemId = (Res >= 0) ? HeapAlloc(GetProcessHeap(), 0, (wcslen(pszId) + 1) * sizeof(WCHAR)) : NULL;
            if (pszItemId)
            {
                wcscpy(pszItemId, pszId);
                SendMessageW(hCB, CB_SETITEMDATA, Res, (LPARAM)pszItemId);
                if (pszDefaultId && !wcscmp(pszDefaultId, pszId))
                    SendMessageW(hCB, CB_SETCURSEL, Res, 0);
            }
            else if (Res >= 0)
            {
                SendMessageW(hCB, CB_DELETESTRING, Res, 0);
            }

            PropVariantClear(&Name);
            CoTaskMemFree(pszId);
            IMMDevice_Release(pDevice);
        }
        IMMDeviceCollection_Release(pCollection);
    }

    if (pszDefaultId)
        CoTaskMemFree(pszDefaultId);
    IMMDeviceEnumerator_Release(pEnum);
    if (SUCCEEDED(hrInit))
        CoUninitialize();

    Count = (UINT)SendMessageW(hCB, CB_GETCOUNT, 0, 0);
    if (Count == 0 || Count == (UINT)CB_ERR)
        return FALSE;
    if (SendMessageW(hCB, CB_GETCURSEL, 0, 0) == CB_ERR)
        SendMessageW(hCB, CB_SETCURSEL, 0, 0);
    return TRUE;
}

static VOID
FreeEndpointList(HWND hCB)
{
    LRESULT Count = SendMessageW(hCB, CB_GETCOUNT, 0, 0);
    LRESULT Index;

    for (Index = 0; Index < Count; Index++)
    {
        LRESULT Data = SendMessageW(hCB, CB_GETITEMDATA, Index, 0);
        if (Data != CB_ERR && Data != 0)
            HeapFree(GetProcessHeap(), 0, (LPVOID)Data);
    }
    SendMessageW(hCB, CB_RESETCONTENT, 0, 0);
}

static LPCWSTR
GetSelectedEndpointId(HWND hCB)
{
    LRESULT CurSel = SendMessageW(hCB, CB_GETCURSEL, 0, 0);
    LRESULT Data;

    if (CurSel == CB_ERR)
        return NULL;
    Data = SendMessageW(hCB, CB_GETITEMDATA, CurSel, 0);
    return (Data == CB_ERR) ? NULL : (LPCWSTR)Data;
}

/* The winmm wave device that plays or records through an endpoint, for the mixer of that device */
static UINT
GetWaveDeviceOfEndpoint(LPCWSTR pszId, BOOL bOutput)
{
    WCHAR szId[256];
    UINT Count = bOutput ? waveOutGetNumDevs() : waveInGetNumDevs();
    UINT Index;

    for (Index = 0; pszId && Index < Count; Index++)
    {
        MMRESULT mr = bOutput ?
            waveOutMessage((HWAVEOUT)UlongToHandle(Index), DRV_QUERYFUNCTIONINSTANCEID, (DWORD_PTR)szId, sizeof(szId)) :
            waveInMessage((HWAVEIN)UlongToHandle(Index), DRV_QUERYFUNCTIONINSTANCEID, (DWORD_PTR)szId, sizeof(szId));
        if (mr == MMSYSERR_NOERROR && !wcscmp(szId, pszId))
            return Index;
    }
    return 0;
}

VOID
InitAudioDlg(HWND hwnd, PGLOBAL_DATA pGlobalData)
{
    MIDIOUTCAPSW midiOutCaps;
    WCHAR szNoDevices[256];
    UINT DevsNum;
    UINT uIndex;
    HWND hCB;
    LRESULT Res;

    LoadStringW(hApplet, IDS_NO_DEVICES, szNoDevices, _countof(szNoDevices));

    // Init sound playback devices list
    hCB = GetDlgItem(hwnd, IDC_DEVICE_PLAY_LIST);
    if (!FillEndpointList(hCB, eRender))
    {
        Res = SendMessageW(hCB, CB_ADDSTRING, 0, (LPARAM)szNoDevices);
        SendMessageW(hCB, CB_SETCURSEL, (WPARAM)Res, 0);
        pGlobalData->bNoAudioOut = TRUE;
    }

    // Init sound recording devices list
    hCB = GetDlgItem(hwnd, IDC_DEVICE_REC_LIST);
    if (!FillEndpointList(hCB, eCapture))
    {
        Res = SendMessageW(hCB, CB_ADDSTRING, 0, (LPARAM)szNoDevices);
        SendMessageW(hCB, CB_SETCURSEL, (WPARAM)Res, 0);
        pGlobalData->bNoAudioIn = TRUE;
    }

    // Init MIDI devices list
    hCB = GetDlgItem(hwnd, IDC_DEVICE_MIDI_LIST);

    DevsNum = midiOutGetNumDevs();
    if (DevsNum < 1)
    {
        Res = SendMessageW(hCB, CB_ADDSTRING, 0, (LPARAM)szNoDevices);
        SendMessageW(hCB, CB_SETCURSEL, (WPARAM)Res, 0);
        pGlobalData->bNoMIDIOut = TRUE;
    }
    else
    {
        WCHAR DefaultDevice[MAX_PATH] = {0};
        HKEY hKey;
        DWORD dwSize = sizeof(DefaultDevice);
        UINT DefaultIndex = 0;

        if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Multimedia\\MIDIMap", 0, KEY_READ, &hKey) == ERROR_SUCCESS)
        {
            RegQueryValueExW(hKey, L"szPname", NULL, NULL, (LPBYTE)DefaultDevice, &dwSize);
            DefaultDevice[_countof(DefaultDevice) - 1] = UNICODE_NULL;
            RegCloseKey(hKey);
        }

        for (uIndex = 0; uIndex < DevsNum; uIndex++)
        {
            if (midiOutGetDevCapsW(uIndex, &midiOutCaps, sizeof(midiOutCaps)))
                continue;

            Res = SendMessageW(hCB, CB_ADDSTRING, 0, (LPARAM)midiOutCaps.szPname);

            if (CB_ERR != Res)
            {
                SendMessageW(hCB, CB_SETITEMDATA, Res, (LPARAM)uIndex);
                if (!_wcsicmp(midiOutCaps.szPname, DefaultDevice))
                    DefaultIndex = Res;
            }
        }
        SendMessageW(hCB, CB_SETCURSEL, (WPARAM)DefaultIndex, 0);
    }
}

VOID
UpdateRegistryString(HWND hwnd, INT ctrl, LPCWSTR key, LPCWSTR value)
{
    HWND hwndCombo = GetDlgItem(hwnd, ctrl);
    INT CurSel = SendMessageW(hwndCombo, CB_GETCURSEL, 0, 0);
    UINT TextLen;
    WCHAR SelectedDevice[MAX_PATH] = {0};
    HKEY hKey;

    if (CurSel == CB_ERR)
        return;

    TextLen = SendMessageW(hwndCombo, CB_GETLBTEXTLEN, CurSel, 0) + 1;

    if (TextLen > _countof(SelectedDevice))
        return;

    SendMessageW(hwndCombo, CB_GETLBTEXT, CurSel, (LPARAM)SelectedDevice);

    if (RegCreateKeyExW(HKEY_CURRENT_USER, key, 0, NULL, REG_OPTION_NON_VOLATILE, KEY_WRITE, NULL, &hKey, NULL) != ERROR_SUCCESS)
        return;

    RegSetValueExW(hKey, value, 0, REG_SZ, (BYTE*)SelectedDevice, (wcslen(SelectedDevice) + 1) * sizeof(WCHAR));
    RegCloseKey(hKey);
}

VOID
SaveAudioDlg(HWND hwnd, PGLOBAL_DATA pGlobalData)
{
    LPCWSTR pszId;

    if (pGlobalData->bAudioOutChanged && !pGlobalData->bNoAudioOut)
    {
        pszId = GetSelectedEndpointId(GetDlgItem(hwnd, IDC_DEVICE_PLAY_LIST));
        if (pszId)
            MMDevSetDefaultEndpoint(eRender, eConsole, pszId);
        pGlobalData->bAudioOutChanged = FALSE;
    }

    if (pGlobalData->bAudioInChanged && !pGlobalData->bNoAudioIn)
    {
        pszId = GetSelectedEndpointId(GetDlgItem(hwnd, IDC_DEVICE_REC_LIST));
        if (pszId)
            MMDevSetDefaultEndpoint(eCapture, eConsole, pszId);
        pGlobalData->bAudioInChanged = FALSE;
    }

    if (pGlobalData->bMIDIOutChanged)
    {
        UpdateRegistryString(hwnd,
                             IDC_DEVICE_MIDI_LIST,
                             L"Software\\Microsoft\\Windows\\CurrentVersion\\Multimedia\\MIDIMap",
                             L"szPname");
    }
}

static UINT
GetDevNum(HWND hControl, DWORD Id)
{
    int iCurSel;
    UINT DevNum;

    iCurSel = SendMessageW(hControl, CB_GETCURSEL, 0, 0);

    if (iCurSel == CB_ERR)
        return 0;

    if (Id == MIXER_OBJECTF_WAVEOUT || Id == MIXER_OBJECTF_WAVEIN)
        DevNum = GetWaveDeviceOfEndpoint(GetSelectedEndpointId(hControl), Id == MIXER_OBJECTF_WAVEOUT);
    else
        DevNum = (UINT)SendMessageW(hControl, CB_GETITEMDATA, iCurSel, 0);
    if (DevNum == (UINT) CB_ERR)
        return 0;

    if (mixerGetID((HMIXEROBJ)IntToPtr(DevNum), &DevNum, Id) != MMSYSERR_NOERROR)
        return 0;

    return DevNum;
}

/* Audio property page dialog callback */
INT_PTR CALLBACK
AudioDlgProc(HWND hwndDlg,
             UINT uMsg,
             WPARAM wParam,
             LPARAM lParam)
{
    PGLOBAL_DATA pGlobalData;

    pGlobalData = (PGLOBAL_DATA)GetWindowLongPtrW(hwndDlg, DWLP_USER);

    switch (uMsg)
    {
        case WM_INITDIALOG:
        {
            UINT NumWavOut = waveOutGetNumDevs();

            pGlobalData = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(GLOBAL_DATA));
            SetWindowLongPtrW(hwndDlg, DWLP_USER, (LONG_PTR)pGlobalData);

            if (!pGlobalData)
                break;

            InitAudioDlg(hwndDlg, pGlobalData);

            if (!NumWavOut)
            {
                EnableWindow(GetDlgItem(hwndDlg, IDC_DEVICE_PLAY_LIST),     FALSE);
                EnableWindow(GetDlgItem(hwndDlg, IDC_DEVICE_REC_LIST),      FALSE);
                EnableWindow(GetDlgItem(hwndDlg, IDC_DEVICE_MIDI_LIST),     FALSE);
                EnableWindow(GetDlgItem(hwndDlg, IDC_DEFAULT_DEV_CHECKBOX), FALSE);
                EnableWindow(GetDlgItem(hwndDlg, IDC_VOLUME1_BTN),          FALSE);
                EnableWindow(GetDlgItem(hwndDlg, IDC_ADV2_BTN),             FALSE);
                EnableWindow(GetDlgItem(hwndDlg, IDC_VOLUME2_BTN),          FALSE);
                EnableWindow(GetDlgItem(hwndDlg, IDC_ADV1_BTN),             FALSE);
                EnableWindow(GetDlgItem(hwndDlg, IDC_VOLUME3_BTN),          FALSE);
                EnableWindow(GetDlgItem(hwndDlg, IDC_ADV3_BTN),             FALSE);
            }

            if (pGlobalData->bNoAudioOut)
            {
                EnableWindow(GetDlgItem(hwndDlg, IDC_DEVICE_PLAY_LIST),     FALSE);
                EnableWindow(GetDlgItem(hwndDlg, IDC_VOLUME1_BTN),          FALSE);
                EnableWindow(GetDlgItem(hwndDlg, IDC_ADV2_BTN),             FALSE);
            }

            if (pGlobalData->bNoAudioIn)
            {
                EnableWindow(GetDlgItem(hwndDlg, IDC_DEVICE_REC_LIST),      FALSE);
                EnableWindow(GetDlgItem(hwndDlg, IDC_VOLUME2_BTN),          FALSE);
                EnableWindow(GetDlgItem(hwndDlg, IDC_ADV1_BTN),             FALSE);
            }

            if (pGlobalData->bNoMIDIOut)
            {
                EnableWindow(GetDlgItem(hwndDlg, IDC_DEVICE_MIDI_LIST),     FALSE);
                EnableWindow(GetDlgItem(hwndDlg, IDC_VOLUME3_BTN),          FALSE);
                EnableWindow(GetDlgItem(hwndDlg, IDC_ADV3_BTN),             FALSE);
            }
        }
        break;

        case WM_COMMAND:
        {
            STARTUPINFOW si;
            PROCESS_INFORMATION pi;
            WCHAR szPath[MAX_PATH];

            if (!pGlobalData)
                break;

            switch (LOWORD(wParam))
            {
                case IDC_VOLUME1_BTN:
                {
                    StringCchPrintfW(szPath, _countof(szPath), L"sndvol32.exe -d %d",
                                     GetDevNum(GetDlgItem(hwndDlg, IDC_DEVICE_PLAY_LIST), MIXER_OBJECTF_WAVEOUT));

                    ZeroMemory(&si, sizeof(si));
                    si.cb = sizeof(si);
                    si.dwFlags = STARTF_USESHOWWINDOW;
                    si.wShowWindow = SW_SHOW;

                    if (CreateProcessW(NULL, szPath, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi))
                    {
                        CloseHandle(pi.hProcess);
                        CloseHandle(pi.hThread);
                    }
                }
                break;

                case IDC_ADV2_BTN:
                {

                }
                break;

                case IDC_VOLUME2_BTN:
                {
                    StringCchPrintfW(szPath, _countof(szPath), L"sndvol32.exe -r -d %d",
                                     GetDevNum(GetDlgItem(hwndDlg, IDC_DEVICE_REC_LIST), MIXER_OBJECTF_WAVEIN));

                    ZeroMemory(&si, sizeof(si));
                    si.cb = sizeof(si);
                    si.dwFlags = STARTF_USESHOWWINDOW;
                    si.wShowWindow = SW_SHOW;

                    if (CreateProcessW(NULL, szPath, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi))
                    {
                        CloseHandle(pi.hProcess);
                        CloseHandle(pi.hThread);
                    }
                }
                break;

                case IDC_ADV1_BTN:
                {

                }
                break;

                case IDC_VOLUME3_BTN:
                {
                    StringCchPrintfW(szPath, _countof(szPath), L"sndvol32.exe -d %d",
                                     GetDevNum(GetDlgItem(hwndDlg, IDC_DEVICE_MIDI_LIST), MIXER_OBJECTF_MIDIOUT));

                    ZeroMemory(&si, sizeof(si));
                    si.cb = sizeof(si);
                    si.dwFlags = STARTF_USESHOWWINDOW;
                    si.wShowWindow = SW_SHOW;

                    if (CreateProcessW(NULL, szPath, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi))
                    {
                        CloseHandle(pi.hProcess);
                        CloseHandle(pi.hThread);
                    }
                }
                break;

                case IDC_ADV3_BTN:
                {

                }
                break;

                case IDC_DEVICE_PLAY_LIST:
                {
                    if (HIWORD(wParam) == CBN_SELCHANGE)
                    {
                        PropSheet_Changed(GetParent(hwndDlg), hwndDlg);
                        pGlobalData->bAudioOutChanged = TRUE;
                    }
                }
                break;

                case IDC_DEVICE_REC_LIST:
                {
                    if (HIWORD(wParam) == CBN_SELCHANGE)
                    {
                        PropSheet_Changed(GetParent(hwndDlg), hwndDlg);
                        pGlobalData->bAudioInChanged = TRUE;
                    }
                }
                break;

                case IDC_DEVICE_MIDI_LIST:
                {
                    if (HIWORD(wParam) == CBN_SELCHANGE)
                    {
                        PropSheet_Changed(GetParent(hwndDlg), hwndDlg);
                        pGlobalData->bMIDIOutChanged = TRUE;
                    }
                }
                break;
            }
        }
        break;

        case WM_DESTROY:
            if (!pGlobalData)
                break;

            if (!pGlobalData->bNoAudioOut)
                FreeEndpointList(GetDlgItem(hwndDlg, IDC_DEVICE_PLAY_LIST));
            if (!pGlobalData->bNoAudioIn)
                FreeEndpointList(GetDlgItem(hwndDlg, IDC_DEVICE_REC_LIST));
            HeapFree(GetProcessHeap(), 0, pGlobalData);
            break;

        case WM_NOTIFY:
            if (!pGlobalData)
                break;

            if (((LPNMHDR)lParam)->code == (UINT)PSN_APPLY)
            {
                SaveAudioDlg(hwndDlg, pGlobalData);
            }
            return TRUE;
    }

    return FALSE;
}
