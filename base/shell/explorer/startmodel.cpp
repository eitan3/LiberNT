/*
 * PROJECT:     LiberNT Explorer
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Start menu model: program tree, recent apps, launch and power commands
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"
#include <powrprof.h>

#include "startmodel.h"

int __cdecl
SM2CompareItems(const void *a, const void *b)
{
    SM2ITEM *pa = *(SM2ITEM**)a;
    SM2ITEM *pb = *(SM2ITEM**)b;
    if (pa->bFolder != pb->bFolder)
        return pa->bFolder ? 1 : -1;
    return lstrcmpiW(pa->szName, pb->szName);
}

VOID
SM2SortTree(SM2ITEM *pParent)
{
    SIZE_T count = pParent->Children.GetCount();
    if (count > 1)
        qsort(pParent->Children.GetData(), count, sizeof(SM2ITEM*), SM2CompareItems);
    for (SIZE_T i = 0; i < count; i++)
    {
        if (pParent->Children[i]->bFolder)
            SM2SortTree(pParent->Children[i]);
    }
}

SM2ITEM*
SM2FindChild(SM2ITEM *pParent, LPCWSTR pszName, BOOL bFolder)
{
    for (SIZE_T i = 0; i < pParent->Children.GetCount(); i++)
    {
        SM2ITEM *p = pParent->Children[i];
        if (p->bFolder == bFolder && p->szName.CompareNoCase(pszName) == 0)
            return p;
    }
    return NULL;
}

VOID
SM2EnumFolder(LPCWSTR pszDir, SM2ITEM *pParent, BOOL bFilesOnly)
{
    WCHAR szSpec[MAX_PATH];
    WIN32_FIND_DATAW wfd;
    HANDLE hFind;

    StringCchPrintfW(szSpec, _countof(szSpec), L"%s\\*", pszDir);
    hFind = FindFirstFileW(szSpec, &wfd);
    if (hFind == INVALID_HANDLE_VALUE)
        return;

    do
    {
        if (wfd.cFileName[0] == L'.' &&
            (wfd.cFileName[1] == 0 || (wfd.cFileName[1] == L'.' && wfd.cFileName[2] == 0)))
            continue;
        if (wfd.dwFileAttributes & FILE_ATTRIBUTE_HIDDEN)
            continue;

        WCHAR szFull[MAX_PATH];
        StringCchPrintfW(szFull, _countof(szFull), L"%s\\%s", pszDir, wfd.cFileName);

        if (wfd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
        {
            if (bFilesOnly)
                continue;
            SM2ITEM *pFolder = SM2FindChild(pParent, wfd.cFileName, TRUE);
            if (!pFolder)
            {
                pFolder = new SM2ITEM();
                pFolder->bFolder = TRUE;
                pFolder->szName = wfd.cFileName;
                pFolder->szPath = szFull;
                pParent->Children.Add(pFolder);
            }
            SM2EnumFolder(szFull, pFolder, FALSE);
        }
        else
        {
            LPCWSTR pszExt = PathFindExtensionW(wfd.cFileName);
            if (lstrcmpiW(pszExt, L".lnk") != 0 && lstrcmpiW(pszExt, L".url") != 0)
                continue;

            WCHAR szName[MAX_PATH];
            StringCchCopyW(szName, _countof(szName), wfd.cFileName);
            PathRemoveExtensionW(szName);

            if (SM2FindChild(pParent, szName, FALSE))
                continue;

            SM2ITEM *pItem = new SM2ITEM();
            pItem->bFolder = FALSE;
            pItem->szName = szName;
            pItem->szPath = szFull;
            pItem->Created = ((ULONGLONG)wfd.ftCreationTime.dwHighDateTime << 32) |
                             wfd.ftCreationTime.dwLowDateTime;
            pItem->Written = ((ULONGLONG)wfd.ftLastWriteTime.dwHighDateTime << 32) |
                             wfd.ftLastWriteTime.dwLowDateTime;
            pParent->Children.Add(pItem);
        }
    } while (FindNextFileW(hFind, &wfd));

    FindClose(hFind);
}

VOID
SM2BuildTree(SM2ITEM *pRoot)
{
    static const int roots[] =
    {
        CSIDL_STARTMENU, CSIDL_COMMON_STARTMENU,
        CSIDL_PROGRAMS, CSIDL_COMMON_PROGRAMS
    };

    pRoot->Clear();

    for (UINT i = 0; i < _countof(roots); i++)
    {
        WCHAR szPath[MAX_PATH];
        BOOL bFilesOnly = (roots[i] == CSIDL_STARTMENU || roots[i] == CSIDL_COMMON_STARTMENU);
        if (SUCCEEDED(SHGetFolderPathW(NULL, roots[i], NULL, SHGFP_TYPE_CURRENT, szPath)))
            SM2EnumFolder(szPath, pRoot, bFilesOnly);
    }

    SM2SortTree(pRoot);
}

VOID
SM2FlattenTree(SM2ITEM *pParent, CAtlArray<SM2ITEM*> &Flat)
{
    for (SIZE_T i = 0; i < pParent->Children.GetCount(); i++)
    {
        SM2ITEM *p = pParent->Children[i];
        if (p->bFolder)
            SM2FlattenTree(p, Flat);
        else
            Flat.Add(p);
    }
}

VOID
SM2AdoptCaches(const CAtlArray<SM2ITEM*> &Flat, const CAtlArray<SM2ITEM*> &Old)
{
    SIZE_T Next = 0;

    for (SIZE_T i = 0; i < Flat.GetCount() && Old.GetCount(); i++)
    {
        SM2ITEM *pItem = Flat[i];
        SM2ITEM *pOld = NULL;

        for (SIZE_T n = 0; n < Old.GetCount(); n++)
        {
            SIZE_T k = (Next + n) % Old.GetCount();
            if (Old[k]->Written == pItem->Written && !Old[k]->szPath.CompareNoCase(pItem->szPath))
            {
                pOld = Old[k];
                Next = k + 1;
                break;
            }
        }
        if (!pOld)
            continue;

        ATLASSERT(!pItem->bFolder && !pOld->bFolder);
        ATLASSERT(!pItem->hSmallIcon && !pItem->hLargeIcon && !pItem->bTargetTried);
        pItem->szTargetPath = pOld->szTargetPath;
        pItem->bTargetTried = pOld->bTargetTried;
        pItem->bSmallTried = pOld->bSmallTried;
        pItem->hSmallIcon = pOld->hSmallIcon;
        pItem->hLargeIcon = pOld->hLargeIcon;
        pOld->hSmallIcon = NULL;
        pOld->hLargeIcon = NULL;
    }
}

BOOL
SM2NormalizePath(LPCWSTR pszPath, CStringW &Path)
{
    WCHAR szExpanded[MAX_PATH], szFull[MAX_PATH];
    LPCWSTR pszExpanded = pszPath;

    if (!pszPath || !*pszPath)
        return FALSE;

    DWORD cch = ExpandEnvironmentStringsW(pszPath, szExpanded, _countof(szExpanded));
    if (cch && cch <= _countof(szExpanded))
        pszExpanded = szExpanded;

    cch = GetFullPathNameW(pszExpanded, _countof(szFull), szFull, NULL);
    if (cch && cch < _countof(szFull))
        Path = szFull;
    else
        Path = pszExpanded;

    return !Path.IsEmpty();
}

ULONGLONG
SM2GetLastUsed(LPCWSTR pszKey, LPCWSTR pszPath)
{
    ULONGLONG Time = 0;
    DWORD cbData = sizeof(Time), dwType = 0;

    if (SHGetValueW(HKEY_CURRENT_USER, pszKey, pszPath,
                    &dwType, &Time, &cbData) != ERROR_SUCCESS ||
        dwType != REG_QWORD || cbData != sizeof(Time))
    {
        return 0;
    }
    return Time;
}

VOID
SM2SetLastUsed(LPCWSTR pszKey, LPCWSTR pszPath)
{
    FILETIME ft;
    ULARGE_INTEGER Time;

    GetSystemTimeAsFileTime(&ft);
    Time.LowPart = ft.dwLowDateTime;
    Time.HighPart = ft.dwHighDateTime;
    SHSetValueW(HKEY_CURRENT_USER, pszKey, pszPath,
                REG_QWORD, &Time.QuadPart, sizeof(Time.QuadPart));
}

BOOL
SM2GetTargetPath(SM2ITEM *pItem)
{
    if (pItem->bTargetTried)
        return !pItem->szTargetPath.IsEmpty();

    pItem->bTargetTried = TRUE;
    if (lstrcmpiW(PathFindExtensionW(pItem->szPath), L".lnk") != 0)
        return FALSE;

    CComPtr<IShellLinkW> pLink;
    if (FAILED(pLink.CoCreateInstance(CLSID_ShellLink, IID_IShellLinkW)))
        return FALSE;

    CComPtr<IPersistFile> pPersist;
    if (FAILED(pLink->QueryInterface(IID_PPV_ARG(IPersistFile, &pPersist))) ||
        FAILED(pPersist->Load(pItem->szPath, STGM_READ)))
    {
        return FALSE;
    }

    WCHAR szTarget[MAX_PATH] = L"";
    if (FAILED(pLink->GetPath(szTarget, _countof(szTarget), NULL, 0)) ||
        !szTarget[0])
    {
        return FALSE;
    }

    return SM2NormalizePath(szTarget, pItem->szTargetPath);
}

HICON
SM2LoadShellIcon(LPCWSTR pszPath, BOOL bLarge)
{
    SHFILEINFOW sfi;
    ZeroMemory(&sfi, sizeof(sfi));
    SHGetFileInfoW(pszPath, 0, &sfi, sizeof(sfi),
                   SHGFI_ICON | (bLarge ? SHGFI_LARGEICON : SHGFI_SMALLICON));
    return sfi.hIcon;
}

VOID
SM2LaunchItem(SM2ITEM *pItem)
{
    if (!pItem || pItem->bFolder)
        return;

    HINSTANCE hResult = ShellExecuteW(NULL, NULL, pItem->szPath, NULL, NULL, SW_SHOWNORMAL);
    if ((INT_PTR)hResult > 32 && GetAdvancedBool(L"Start_TrackProgs", TRUE))
        SM2SetLastUsed(SM2_RECENT_LINK_KEY, pItem->szPath);
}

DWORD
SM2ExitWindows(UINT uFlags)
{
    HANDLE hToken;
    TOKEN_PRIVILEGES tp;
    TOKEN_PRIVILEGES PreviousState;
    DWORD cbPreviousState = sizeof(PreviousState);
    DWORD dwError;

    if (uFlags == EWX_LOGOFF)
        return ExitWindowsEx(uFlags, SHTDN_REASON_MAJOR_OTHER) ? ERROR_SUCCESS : GetLastError();

    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &hToken))
        return GetLastError();

    tp.PrivilegeCount = 1;
    tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
    if (!LookupPrivilegeValueW(NULL, SE_SHUTDOWN_NAME, &tp.Privileges[0].Luid))
    {
        dwError = GetLastError();
        CloseHandle(hToken);
        return dwError;
    }

    /* A successful return can still mean that the token lacks the privilege. */
    if (!AdjustTokenPrivileges(hToken, FALSE, &tp, sizeof(PreviousState), &PreviousState, &cbPreviousState))
    {
        dwError = GetLastError();
        CloseHandle(hToken);
        return dwError;
    }
    dwError = GetLastError();
    if (dwError != ERROR_SUCCESS)
    {
        CloseHandle(hToken);
        return dwError;
    }

    /* Let Winlogon end the session normally, including application vetoes. */
    dwError = ExitWindowsEx(uFlags, SHTDN_REASON_MAJOR_OTHER) ? ERROR_SUCCESS : GetLastError();

    /* Explorer survives a cancelled shutdown: restore its original privileges. */
    if (!AdjustTokenPrivileges(hToken, FALSE, &PreviousState, 0, NULL, NULL) &&
        dwError == ERROR_SUCCESS)
    {
        dwError = GetLastError();
    }

    CloseHandle(hToken);
    return dwError;
}

DWORD WINAPI
SM2SuspendThread(LPVOID lpParameter)
{
    HANDLE hToken;
    TOKEN_PRIVILEGES tp;

    if (OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &hToken))
    {
        tp.PrivilegeCount = 1;
        tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
        if (LookupPrivilegeValueW(NULL, SE_SHUTDOWN_NAME, &tp.Privileges[0].Luid))
            AdjustTokenPrivileges(hToken, FALSE, &tp, 0, NULL, NULL);
        CloseHandle(hToken);
    }

    SetSuspendState(lpParameter != NULL, FALSE, FALSE);
    return 0;
}

BOOL
SM2IsModernStandby()
{
    SYSTEM_POWER_CAPABILITIES Capabilities;

    return GetPwrCapabilities(&Capabilities) && Capabilities.AoAc;
}

BOOL StartMenu2_IsPowerCommandAllowed(IN INT nCmd)
{
    if (nCmd == SM2F_SLEEP)
        return IsPwrSuspendAllowed() || SM2IsModernStandby();
    if (nCmd == SM2F_HIBERNATE)
        return IsPwrHibernateAllowed();
    return TRUE;
}

DWORD
SM2Suspend(BOOL bHibernate)
{
    HANDLE hThread;

    if (!bHibernate && SM2IsModernStandby())
    {
        DefWindowProcW(GetDesktopWindow(), WM_SYSCOMMAND, SC_MONITORPOWER, 2);
        return ERROR_SUCCESS;
    }

    hThread = CreateThread(NULL, 0, SM2SuspendThread, bHibernate ? (LPVOID)1 : NULL, 0, NULL);
    if (hThread == NULL)
        return GetLastError();

    CloseHandle(hThread);
    return ERROR_SUCCESS;
}


VOID
SM2DoShutdownCmd(HWND hwndOwner, int nCmd)
{
    static BOOL bInProgress = FALSE;
    DWORD dwError = ERROR_SUCCESS;
    UINT nAction;

    /* ExitWindowsEx can pump messages while the shutdown request is pending. */
    if (bInProgress)
        return;

    switch (nCmd)
    {
        case SM2F_SHUTDOWN:
            nAction = IDS_SM2_SHUTDOWN;
            break;
        case SM2F_RESTART:
            nAction = IDS_SM2_RESTART;
            break;
        case SM2F_LOGOFF:
            nAction = IDS_SM2_LOGOFF;
            break;
        case SM2F_LOCK:
            nAction = IDS_SM2_LOCK;
            break;
        case SM2F_SLEEP:
            nAction = IDS_SM2_SLEEP;
            break;
        case SM2F_HIBERNATE:
            nAction = IDS_SM2_HIBERNATE;
            break;
        default:
            return;
    }

    bInProgress = TRUE;
    switch (nCmd)
    {
        case SM2F_SHUTDOWN:
        case SM2F_RESTART:
            if (SHRestricted(REST_NOCLOSE))
                dwError = ERROR_ACCESS_DISABLED_BY_POLICY;
            else
                dwError = SM2ExitWindows(nCmd == SM2F_RESTART ? EWX_REBOOT : EWX_POWEROFF);
            break;
        case SM2F_LOGOFF:
            dwError = SM2ExitWindows(EWX_LOGOFF);
            break;
        case SM2F_LOCK:
            if (!LockWorkStation())
                dwError = GetLastError();
            break;
        case SM2F_SLEEP:
            dwError = SM2Suspend(FALSE);
            break;
        case SM2F_HIBERNATE:
            dwError = SM2Suspend(TRUE);
            break;
    }

    if (dwError != ERROR_SUCCESS && dwError != ERROR_CANCELLED)
    {
        WCHAR szError[512];
        if (!FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, NULL, dwError, 0, szError, _countof(szError), NULL))
            StringCchPrintfW(szError, _countof(szError), SM2Str(IDS_SM2_ERROR), dwError);
        ::MessageBoxW(hwndOwner, szError, SM2Str(nAction), MB_OK | MB_ICONERROR);
    }
    bInProgress = FALSE;
}

VOID
SM2OpenPath(LPCWSTR pszPath)
{
    ShellExecuteW(NULL, L"open", pszPath, NULL, NULL, SW_SHOWNORMAL);
}

VOID
SM2OpenCsidlFolder(int csidl)
{
    WCHAR szPath[MAX_PATH];
    if (SUCCEEDED(SHGetFolderPathW(NULL, csidl, NULL, SHGFP_TYPE_CURRENT, szPath)))
        SM2OpenPath(szPath);
}

VOID
SM2OpenCsidlIdList(int csidl)
{
    SHELLEXECUTEINFOW sei;
    ZeroMemory(&sei, sizeof(sei));
    sei.cbSize = sizeof(sei);
    sei.fMask = SEE_MASK_IDLIST;
    sei.nShow = SW_SHOWNORMAL;
    sei.lpIDList = SHCloneSpecialIDList(NULL, csidl, FALSE);
    if (sei.lpIDList)
    {
        ShellExecuteExW(&sei);
        ILFree((LPITEMIDLIST)sei.lpIDList);
    }
}

VOID
SM2OpenGames(VOID)
{
    static const int roots[] = { CSIDL_COMMON_PROGRAMS, CSIDL_PROGRAMS };
    for (UINT i = 0; i < _countof(roots); i++)
    {
        WCHAR szPath[MAX_PATH];
        if (SUCCEEDED(SHGetFolderPathW(NULL, roots[i], NULL, SHGFP_TYPE_CURRENT, szPath)))
        {
            PathAppendW(szPath, L"Games");
            if (PathIsDirectoryW(szPath))
            {
                SM2OpenPath(szPath);
                return;
            }
        }
    }
    SM2OpenCsidlFolder(CSIDL_PROGRAMS);
}

#ifdef REMOTE_ASSISTANCE
BOOL
SM2NetworkAssistanceEnabled(VOID)
{
    HKEY hKey;
    LONG lError = RegOpenKeyExW(HKEY_LOCAL_MACHINE,
                                L"SYSTEM\\CurrentControlSet\\Control\\NetworkAssistance",
                                0, KEY_QUERY_VALUE, &hKey);
    if (lError == ERROR_SUCCESS)
        RegCloseKey(hKey);
    return lError == ERROR_SUCCESS || lError == ERROR_ACCESS_DENIED;
}
#endif

VOID
SM2ExecRightCmd(int nCmd)
{
    switch (nCmd)
    {
        case SM2C_PROFILE:   SM2OpenCsidlFolder(CSIDL_PROFILE); break;
        case SM2C_DOCUMENTS: SM2OpenCsidlFolder(CSIDL_PERSONAL); break;
        case SM2C_PICTURES:  SM2OpenCsidlFolder(CSIDL_MYPICTURES); break;
        case SM2C_MUSIC:     SM2OpenCsidlFolder(CSIDL_MYMUSIC); break;
        case SM2C_GAMES:     SM2OpenGames(); break;
        case SM2C_COMPUTER:  SM2OpenCsidlIdList(CSIDL_DRIVES); break;
        case SM2C_CONTROL:   ShellExecuteW(NULL, NULL, L"ms-settings:", NULL, NULL, SW_SHOWNORMAL); break;
        case SM2C_PRINTERS:  ShellExecuteW(NULL, NULL, L"ms-settings:devices", NULL, NULL, SW_SHOWNORMAL); break;
#ifdef REMOTE_ASSISTANCE
        case SM2C_ASSIST:    ShellExecuteW(NULL, NULL, L"netassist.exe", NULL, NULL, SW_SHOWNORMAL); break;
#endif
    }
}
