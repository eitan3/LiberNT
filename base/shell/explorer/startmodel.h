/*
 * PROJECT:     LiberNT Explorer
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Start menu model: program tree, recent apps, launch and power commands
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

#define SM2_KEY             L"Software\\ReactOS\\StartMenu2"
#define SM2_RECENT_LINK_KEY L"Software\\ReactOS\\StartMenu2\\RecentApps\\Links"
#define SM2_RECENT_EXE_KEY  L"Software\\ReactOS\\StartMenu2\\RecentApps\\Executables"

enum SM2RCMD
{
    SM2C_PROFILE,
    SM2C_DOCUMENTS,
    SM2C_PICTURES,
    SM2C_MUSIC,
    SM2C_GAMES,
    SM2C_COMPUTER,
    SM2C_CONTROL,
    SM2C_PRINTERS,
#ifdef REMOTE_ASSISTANCE
    SM2C_ASSIST,
#endif
    SM2C_GAP
};

struct SM2ITEM
{
    CStringW szName;
    CStringW szPath;
    CStringW szTargetPath;
    ULONGLONG Created;
    ULONGLONG Written;
    BOOL bFolder;
    BOOL bExpanded;
    HICON hSmallIcon;
    HICON hLargeIcon;
    BOOL bSmallTried;
    BOOL bTargetTried;
    CAtlArray<SM2ITEM*> Children;

    SM2ITEM() : Created(0), Written(0), bFolder(FALSE), bExpanded(FALSE), hSmallIcon(NULL), hLargeIcon(NULL),
                bSmallTried(FALSE), bTargetTried(FALSE) {}

    ~SM2ITEM()
    {
        Clear();
    }

    VOID Clear()
    {
        for (SIZE_T i = 0; i < Children.GetCount(); i++)
            delete Children[i];
        Children.SetCount(0);
        if (hSmallIcon) { DestroyIcon(hSmallIcon); hSmallIcon = NULL; }
        if (hLargeIcon) { DestroyIcon(hLargeIcon); hLargeIcon = NULL; }
        bSmallTried = FALSE;
        bTargetTried = FALSE;
        szTargetPath.Empty();
        bExpanded = FALSE;
    }
};

struct SM2Str
{
    WCHAR sz[128];

    explicit SM2Str(UINT nId)
    {
        if (!LoadStringW(hExplorerInstance, nId, sz, _countof(sz)))
            sz[0] = UNICODE_NULL;
    }

    operator LPCWSTR() const { return sz; }
};

int __cdecl SM2CompareItems(const void *a, const void *b);
VOID SM2SortTree(SM2ITEM *pParent);
SM2ITEM* SM2FindChild(SM2ITEM *pParent, LPCWSTR pszName, BOOL bFolder);
VOID SM2EnumFolder(LPCWSTR pszDir, SM2ITEM *pParent, BOOL bFilesOnly);
VOID SM2BuildTree(SM2ITEM *pRoot);
VOID SM2FlattenTree(SM2ITEM *pParent, CAtlArray<SM2ITEM*> &Flat);
VOID SM2AdoptCaches(const CAtlArray<SM2ITEM*> &Flat, const CAtlArray<SM2ITEM*> &Old);
BOOL SM2NormalizePath(LPCWSTR pszPath, CStringW &Path);
ULONGLONG SM2GetLastUsed(LPCWSTR pszKey, LPCWSTR pszPath);
VOID SM2SetLastUsed(LPCWSTR pszKey, LPCWSTR pszPath);
BOOL SM2GetTargetPath(SM2ITEM *pItem);
HICON SM2LoadShellIcon(LPCWSTR pszPath, BOOL bLarge);
VOID SM2LaunchItem(SM2ITEM *pItem);
DWORD SM2ExitWindows(UINT uFlags);
DWORD WINAPI SM2SuspendThread(LPVOID lpParameter);
BOOL SM2IsModernStandby();
DWORD SM2Suspend(BOOL bHibernate);
VOID SM2DoShutdownCmd(HWND hwndOwner, int nCmd);
VOID SM2OpenPath(LPCWSTR pszPath);
VOID SM2OpenCsidlFolder(int csidl);
VOID SM2OpenCsidlIdList(int csidl);
VOID SM2OpenGames(VOID);
#ifdef REMOTE_ASSISTANCE
BOOL SM2NetworkAssistanceEnabled(VOID);
#endif
VOID SM2ExecRightCmd(int nCmd);
