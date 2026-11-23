/*
 * PROJECT:     LiberNT Multimedia Control Panel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tells whether a machine without a playback endpoint routes its audio through the Intel Smart Sound DSP
 */

#include "mmsys.h"

#include <cfgmgr32.h>

static
BOOL
ControllerNeedsDsp(VOID)
{
    static const WCHAR Enumerator[] = L"PCI";
    static const WCHAR Required[] = L"DspRequired";
    ULONG Flags = CM_GETIDLIST_FILTER_ENUMERATOR | CM_GETIDLIST_FILTER_PRESENT;
    ULONG Size = 0;
    LPWSTR List, Id;
    BOOL NeedsDsp = FALSE;

    if (CM_Get_Device_ID_List_SizeW(&Size, Enumerator, Flags) != CR_SUCCESS || Size == 0)
        return FALSE;

    List = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, Size * sizeof(WCHAR));
    if (!List)
        return FALSE;

    if (CM_Get_Device_ID_ListW(Enumerator, List, Size, Flags) == CR_SUCCESS)
    {
        for (Id = List; *Id; Id += wcslen(Id) + 1)
        {
            DEVINST DevInst;
            HKEY DeviceKey, SettingsKey;
            WCHAR Path[32] = L"";
            DWORD Length = sizeof(Path) - sizeof(WCHAR), Type;

            if (CM_Locate_DevNodeW(&DevInst, Id, CM_LOCATE_DEVNODE_NORMAL) != CR_SUCCESS ||
                CM_Open_DevNode_Key(DevInst, KEY_QUERY_VALUE, 0, RegDisposition_OpenExisting, &DeviceKey, 0) != CR_SUCCESS)
                continue;

            if (RegOpenKeyExW(DeviceKey, L"Settings", 0, KEY_QUERY_VALUE, &SettingsKey) == ERROR_SUCCESS)
            {
                if (RegQueryValueExW(SettingsKey, L"DspAudioPath", NULL, &Type, (LPBYTE)Path, &Length) != ERROR_SUCCESS ||
                    Type != REG_SZ)
                    Path[0] = L'\0';
                RegCloseKey(SettingsKey);
            }
            RegCloseKey(DeviceKey);

            if (!wcscmp(Path, Required))
            {
                NeedsDsp = TRUE;
                break;
            }
        }
    }

    HeapFree(GetProcessHeap(), 0, List);
    return NeedsDsp;
}

static
BOOL
DspChildExists(VOID)
{
    static const WCHAR Enumerator[] = L"CSAUDIO";
    static const WCHAR DspDevice[] = L"CSAUDIO\\ADSP";
    ULONG Flags = CM_GETIDLIST_FILTER_ENUMERATOR | CM_GETIDLIST_FILTER_PRESENT;
    ULONG Size = 0;
    LPWSTR List, Id;
    BOOL Exists = FALSE;

    if (CM_Get_Device_ID_List_SizeW(&Size, Enumerator, Flags) != CR_SUCCESS || Size == 0)
        return FALSE;

    List = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, Size * sizeof(WCHAR));
    if (!List)
        return FALSE;

    if (CM_Get_Device_ID_ListW(Enumerator, List, Size, Flags) == CR_SUCCESS)
    {
        for (Id = List; *Id; Id += wcslen(Id) + 1)
        {
            if (!_wcsnicmp(Id, DspDevice, CONST_STR_LEN(DspDevice)))
            {
                Exists = TRUE;
                break;
            }
        }
    }

    HeapFree(GetProcessHeap(), 0, List);
    return Exists;
}

BOOL
AudioOutputNeedsDsp(VOID)
{
    return ControllerNeedsDsp() || DspChildExists();
}
