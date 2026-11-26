/*
 * PROJECT:     LiberNT system libraries
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Notification icon shown while an app records from a microphone
 */

#include "precomp.h"

#include <shlwapi.h>
#include <initguid.h>
#include <mmdeviceapi.h>
#include <audiopolicy.h>

static HICON g_hIconMicrophone = NULL;
static BOOL g_bMicrophoneShown = FALSE;
static WCHAR g_szMicrophoneTip[128];
/* One enumerator for the lifetime of the tray, so the poll creates no COM object per tick */
static CComPtr<IMMDeviceEnumerator> g_pMicrophoneEnum;
static HWND g_hMicrophoneTrayWnd = NULL;
static IMMNotificationClient *g_pMicrophoneNotification = NULL;

/* The poll's work is re-decided on the tray window's thread; mmdevapi calls the client on its own */
static
VOID
MicrophoneDevicesChanged(VOID)
{
    if (g_hMicrophoneTrayWnd)
        PostMessageW(g_hMicrophoneTrayWnd, WM_SYSTRAY_AUDIOCHANGED, 0, 0);
}

class CMicrophoneNotification final : public IMMNotificationClient
{
    LONG m_RefCount;

    ~CMicrophoneNotification() {}

public:
    CMicrophoneNotification() : m_RefCount(1) {}

    STDMETHODIMP QueryInterface(REFIID riid, PVOID *ppv)
    {
        if (riid == IID_IUnknown || riid == IID_IMMNotificationClient)
        {
            *ppv = static_cast<IMMNotificationClient *>(this);
            InterlockedIncrement(&m_RefCount);
            return S_OK;
        }
        *ppv = NULL;
        return E_NOINTERFACE;
    }

    STDMETHODIMP_(ULONG) AddRef()
    {
        return InterlockedIncrement(&m_RefCount);
    }

    STDMETHODIMP_(ULONG) Release()
    {
        ULONG Count = InterlockedDecrement(&m_RefCount);

        if (!Count)
            delete this;
        return Count;
    }

    STDMETHODIMP OnDeviceStateChanged(LPCWSTR pwstrDeviceId, DWORD dwNewState)
    {
        UNREFERENCED_PARAMETER(pwstrDeviceId);
        UNREFERENCED_PARAMETER(dwNewState);
        MicrophoneDevicesChanged();
        return S_OK;
    }

    STDMETHODIMP OnDeviceAdded(LPCWSTR pwstrDeviceId)
    {
        UNREFERENCED_PARAMETER(pwstrDeviceId);
        MicrophoneDevicesChanged();
        return S_OK;
    }

    STDMETHODIMP OnDeviceRemoved(LPCWSTR pwstrDeviceId)
    {
        UNREFERENCED_PARAMETER(pwstrDeviceId);
        MicrophoneDevicesChanged();
        return S_OK;
    }

    STDMETHODIMP OnDefaultDeviceChanged(EDataFlow flow, ERole role, LPCWSTR pwstrDefaultDeviceId)
    {
        UNREFERENCED_PARAMETER(flow);
        UNREFERENCED_PARAMETER(role);
        UNREFERENCED_PARAMETER(pwstrDefaultDeviceId);
        return S_OK;
    }

    STDMETHODIMP OnPropertyValueChanged(LPCWSTR pwstrDeviceId, PROPERTYKEY key)
    {
        UNREFERENCED_PARAMETER(pwstrDeviceId);
        UNREFERENCED_PARAMETER(key);
        return S_OK;
    }
};

/* The enumerator the poll needs, made when it is not there yet: at init and again at a poll when the first made none */
static
BOOL
MicrophoneCreateEnumerator(VOID)
{
    CComPtr<IMMDeviceEnumerator> pEnum;
    CMicrophoneNotification *pNotification;

    if (g_pMicrophoneEnum)
        return TRUE;
    if (FAILED(CoCreateInstance(CLSID_MMDeviceEnumerator, NULL, CLSCTX_INPROC_SERVER, IID_IMMDeviceEnumerator, (void **)&pEnum)))
        return FALSE;

    /* Without the client nothing tells of a capture endpoint that arrives, so the poll then never stops */
    pNotification = new CMicrophoneNotification();
    if (pNotification && SUCCEEDED(pEnum->RegisterEndpointNotificationCallback(pNotification)))
        g_pMicrophoneNotification = pNotification;
    else if (pNotification)
        pNotification->Release();
    g_pMicrophoneEnum = pEnum;
    return TRUE;
}

/* Whether the poll has something to do for the microphone: no enumerator or no client yet (the poll
   makes the enumerator, and without the client a new capture endpoint is learned only by polling),
   or a capture endpoint to watch; a machine without one is not polled for the microphone */
BOOL
Microphone_NeedsPoll(VOID)
{
    CComPtr<IMMDeviceCollection> pCollection;
    UINT cDevices = 0;

    if (!g_pMicrophoneEnum || !g_pMicrophoneNotification)
        return TRUE;
    if (FAILED(g_pMicrophoneEnum->EnumAudioEndpoints(eCapture, DEVICE_STATE_ACTIVE, &pCollection)) ||
        FAILED(pCollection->GetCount(&cDevices)))
        return TRUE;
    return cDevices != 0;
}

/* Name of the first app with an active capture session, or an empty string when nobody records */
static BOOL
MicrophoneFindRecordingApp(LPWSTR pszApp, SIZE_T cchApp)
{
    CComPtr<IMMDeviceCollection> pCollection;
    UINT cDevices = 0;

    pszApp[0] = UNICODE_NULL;
    if (!g_pMicrophoneEnum ||
        FAILED(g_pMicrophoneEnum->EnumAudioEndpoints(eCapture, DEVICE_STATE_ACTIVE, &pCollection)) ||
        FAILED(pCollection->GetCount(&cDevices)))
    {
        return FALSE;
    }

    for (UINT i = 0; i < cDevices; i++)
    {
        CComPtr<IMMDevice> pDevice;
        CComPtr<IAudioSessionManager2> pManager;
        CComPtr<IAudioSessionEnumerator> pSessions;
        int cSessions = 0;

        if (FAILED(pCollection->Item(i, &pDevice)) ||
            FAILED(pDevice->Activate(IID_IAudioSessionManager2, CLSCTX_INPROC_SERVER, NULL, (void **)&pManager)) ||
            FAILED(pManager->GetSessionEnumerator(&pSessions)) ||
            FAILED(pSessions->GetCount(&cSessions)))
        {
            continue;
        }

        for (int j = 0; j < cSessions; j++)
        {
            CComPtr<IAudioSessionControl> pControl;
            CComPtr<IAudioSessionControl2> pControl2;
            AudioSessionState state = AudioSessionStateInactive;
            DWORD dwProcessId = 0;

            if (FAILED(pSessions->GetSession(j, &pControl)) || FAILED(pControl->GetState(&state)) ||
                state != AudioSessionStateActive)
            {
                continue;
            }

            if (SUCCEEDED(pControl->QueryInterface(IID_IAudioSessionControl2, (void **)&pControl2)) &&
                SUCCEEDED(pControl2->GetProcessId(&dwProcessId)))
            {
                HANDLE hProcess = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, dwProcessId);
                WCHAR szPath[MAX_PATH];
                DWORD cchPath = _countof(szPath);

                if (hProcess && QueryFullProcessImageNameW(hProcess, 0, szPath, &cchPath))
                    StringCchCopyW(pszApp, cchApp, PathFindFileNameW(szPath));
                if (hProcess)
                    CloseHandle(hProcess);
            }
            return TRUE;
        }
    }
    return FALSE;
}

HRESULT STDMETHODCALLTYPE Microphone_Init(_In_ CSysTray * pSysTray)
{
    TRACE("Microphone_Init\n");

    g_hMicrophoneTrayWnd = pSysTray->GetHWnd();
    MicrophoneCreateEnumerator();
    g_hIconMicrophone = StoLoadTrayIcon(g_hInstance, IDI_MICROPHONE);
    LoadStringW(g_hInstance, IDS_MIC_IN_USE, g_szMicrophoneTip, _countof(g_szMicrophoneTip));
    g_bMicrophoneShown = FALSE;
    return pSysTray->NotifyIcon(NIM_ADD, ID_ICON_MICROPHONE, g_hIconMicrophone, g_szMicrophoneTip, NIS_HIDDEN);
}

HRESULT STDMETHODCALLTYPE Microphone_Update(_In_ CSysTray * pSysTray)
{
    WCHAR szApp[MAX_PATH], szFormat[128];
    BOOL bRecording;

    MicrophoneCreateEnumerator();
    bRecording = MicrophoneFindRecordingApp(szApp, _countof(szApp));

    if (bRecording)
    {
        if (szApp[0] && LoadStringW(g_hInstance, IDS_MIC_IN_USE_BY, szFormat, _countof(szFormat)))
            StringCchPrintfW(g_szMicrophoneTip, _countof(g_szMicrophoneTip), szFormat, szApp);
        else
            LoadStringW(g_hInstance, IDS_MIC_IN_USE, g_szMicrophoneTip, _countof(g_szMicrophoneTip));
    }

    if (!bRecording && !g_bMicrophoneShown)
        return S_OK;
    g_bMicrophoneShown = bRecording;
    return pSysTray->NotifyIcon(NIM_MODIFY, ID_ICON_MICROPHONE, g_hIconMicrophone, g_szMicrophoneTip,
                                bRecording ? 0 : NIS_HIDDEN);
}

HRESULT STDMETHODCALLTYPE Microphone_Shutdown(_In_ CSysTray * pSysTray)
{
    TRACE("Microphone_Shutdown\n");

    g_hMicrophoneTrayWnd = NULL;
    if (g_pMicrophoneEnum && g_pMicrophoneNotification)
        g_pMicrophoneEnum->UnregisterEndpointNotificationCallback(g_pMicrophoneNotification);
    if (g_pMicrophoneNotification)
    {
        g_pMicrophoneNotification->Release();
        g_pMicrophoneNotification = NULL;
    }
    g_pMicrophoneEnum.Release();
    DestroyIcon(g_hIconMicrophone);
    g_hIconMicrophone = NULL;
    return pSysTray->NotifyIcon(NIM_DELETE, ID_ICON_MICROPHONE, NULL, NULL);
}

HRESULT STDMETHODCALLTYPE Microphone_Message(_In_ CSysTray * pSysTray, UINT uMsg, WPARAM wParam, LPARAM lParam, LRESULT &lResult)
{
    if (uMsg == ID_ICON_MICROPHONE && (lParam == WM_LBUTTONUP || lParam == WM_RBUTTONUP))
    {
        /* Windows 11 opens the microphone privacy settings from this icon */
        ShellExecuteW(NULL, NULL, L"ms-settings:privacy-microphone", NULL, NULL, SW_SHOWNORMAL);
        lResult = 0;
        return S_OK;
    }
    return S_FALSE;
}
