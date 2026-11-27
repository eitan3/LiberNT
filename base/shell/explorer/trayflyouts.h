#pragma once

VOID TaskPreview_Show(IN HWND hwndOwner, IN const RECT *prcAnchor,
                      IN const HWND *pahWnd, IN UINT cWindows, IN INT_PTR nGroupId);
VOID TaskPreview_ShowHover(IN HWND hwndOwner, IN const RECT *prcAnchor,
                           IN const HWND *pahWnd, IN UINT cWindows, IN INT_PTR nGroupId);
VOID TaskPreview_Hide(VOID);
BOOL TaskPreview_IsVisibleFor(IN INT_PTR nGroupId);
BOOL TaskPreview_IsHover(VOID);

VOID TrayCalendar_Toggle(IN HWND hwndOwner, IN const RECT *prcAnchor);
BOOL TrayCalendar_IsOpen(VOID);
VOID TrayVolume_Toggle(IN HWND hwndOwner, IN const RECT *prcAnchor);
VOID TrayVolume_SetCachedState(IN int nPercent, IN BOOL bMute);
BOOL TrayVolume_HandleAppCommand(IN UINT uAppCmd);
VOID TrayVolume_StartWatch(VOID);
VOID TrayNetwork_StartPortalWatch(VOID);
VOID TrayBrightness_StartWatch(VOID);
HWND TrayMedia_FindSession(BOOL *pbPlaying);
VOID TrayNetwork_Toggle(IN HWND hwndOwner, IN const RECT *prcAnchor);
VOID TrayBluetooth_Toggle(IN HWND hwndOwner, IN const RECT *prcAnchor);
VOID TrayMixer_Open(IN const RECT *prcAnchor);
VOID TrayPower_Toggle(IN HWND hwndOwner, IN const RECT *prcAnchor);

VOID TrayNotifications_Add(IN HWND hWndOwner, IN UINT uID, IN LPCWSTR pszApp,
                           IN LPCWSTR pszTitle, IN LPCWSTR pszText, IN HICON hIcon);
UINT TrayNotifications_GetUnread(VOID);
VOID TrayNotifications_SetSink(IN HWND hwndSink);
BOOL TrayNotifications_IsHidden(VOID);

VOID TrayOverflow_Toggle(IN HWND hwndOwner, IN const RECT *prcAnchor, IN HWND hwndPager);
VOID TrayOverflow_ToggleAll(IN HWND hwndOwner, IN const RECT *prcAnchor);

static inline BOOL TrayIcon_Listed(DWORD dwState, BOOL bVisible, BOOL bAll)
{
    if (dwState & NIS_HIDDEN)
        return FALSE;
    return bAll || !bVisible;
}
static inline VOID TrayFlyout_Position(const RECT *prcBar, const RECT *prcAnchor, const RECT *prcMonitor, SIZE size,
                                       BOOL bCenter, int nGap, POINT *ppt)
{
    const RECT *prcEdge = (prcBar && !IsRectEmpty(prcBar)) ? prcBar : prcAnchor;

    if (bCenter)
        ppt->x = (prcAnchor->left + prcAnchor->right - size.cx) / 2;
    else
        ppt->x = prcMonitor->right - nGap - size.cx;
    if (ppt->x + size.cx > prcMonitor->right - nGap)
        ppt->x = prcMonitor->right - nGap - size.cx;
    if (ppt->x < prcMonitor->left + nGap)
        ppt->x = prcMonitor->left + nGap;

    if (prcEdge->top > (prcMonitor->top + prcMonitor->bottom) / 2)
        ppt->y = prcEdge->top - nGap - size.cy;
    else
        ppt->y = prcEdge->bottom + nGap;
}
BOOL TrayOverflow_IsOpen(VOID);
VOID TrayOverflow_Refresh(VOID);

VOID TrayQuickSettings_Toggle(IN HWND hwndOwner, IN const RECT *prcAnchor);
BOOL TrayQuickSettings_IsOpen(VOID);

VOID TrayScreenshot_Capture(VOID);

VOID TrayFlyouts_Destroy(VOID);
VOID TrayFlyoutsAux_Destroy(VOID);
