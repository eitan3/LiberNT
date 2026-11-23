/*
 * PROJECT:     LiberNT header
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Microphone access decisions of the user and the machine
 */

#pragma once

/*
 * Windows 11 keeps the microphone choices in the CapabilityAccessManager
 * consent store: "Value" ("Allow" or "Deny") of the microphone key turns
 * access off for everyone (HKLM) or for the user (HKCU), and the same value
 * of its NonPackaged subkey turns it off for desktop apps. Desktop apps are
 * named by their image path with '\' replaced by '#'.
 *
 * Strict mode is a LiberNT addition: when it is on, only desktop apps whose
 * NonPackaged subkey holds Value "Allow" may record.
 */
#define MIC_CONSENT_KEY L"Software\\Microsoft\\Windows\\CurrentVersion\\CapabilityAccessManager\\ConsentStore\\microphone"
#define MIC_CONSENT_APPS_KEY MIC_CONSENT_KEY L"\\NonPackaged"
#define MIC_CONSENT_STRICT_KEY L"Software\\LiberNT\\Privacy\\Microphone"
#define MIC_CONSENT_STRICT_VALUE L"StrictMode"

static __inline VOID
MicConsentAppKeyName(LPCWSTR ImagePath, LPWSTR Name, SIZE_T Chars)
{
    SIZE_T Index;

    for (Index = 0; Index + 1 < Chars && ImagePath[Index]; Index++)
        Name[Index] = ImagePath[Index] == L'\\' ? L'#' : ImagePath[Index];
    if (Chars)
        Name[Index] = UNICODE_NULL;
}

/* TRUE when "Value" under Root\SubKey reads Name ("Allow" or "Deny") */
static __inline BOOL
MicConsentValueIs(HKEY Root, LPCWSTR SubKey, LPCWSTR Name)
{
    WCHAR Value[16];
    DWORD Size = sizeof(Value);

    return RegGetValueW(Root, SubKey, L"Value", RRF_RT_REG_SZ, NULL, Value, &Size) == ERROR_SUCCESS &&
           !lstrcmpiW(Value, Name);
}

static __inline BOOL
MicConsentStrictMode(VOID)
{
    DWORD Value = 0, Size = sizeof(Value);

    return RegGetValueW(HKEY_CURRENT_USER, MIC_CONSENT_STRICT_KEY, MIC_CONSENT_STRICT_VALUE, RRF_RT_REG_DWORD,
                        NULL, &Value, &Size) == ERROR_SUCCESS && Value != 0;
}

static __inline BOOL
MicConsentAppAllowed(LPCWSTR ImagePath)
{
    WCHAR Key[MAX_PATH * 2];
    SIZE_T Length;

    if (FAILED(StringCchCopyW(Key, _countof(Key), MIC_CONSENT_APPS_KEY L"\\")))
        return FALSE;
    Length = wcslen(Key);
    MicConsentAppKeyName(ImagePath, Key + Length, _countof(Key) - Length);
    return MicConsentValueIs(HKEY_CURRENT_USER, Key, L"Allow");
}

/* May the desktop app at ImagePath record from a microphone? */
static __inline BOOL
MicConsentCheck(LPCWSTR ImagePath)
{
    if (MicConsentValueIs(HKEY_LOCAL_MACHINE, MIC_CONSENT_KEY, L"Deny") ||
        MicConsentValueIs(HKEY_CURRENT_USER, MIC_CONSENT_KEY, L"Deny") ||
        MicConsentValueIs(HKEY_CURRENT_USER, MIC_CONSENT_APPS_KEY, L"Deny"))
    {
        return FALSE;
    }
    return !MicConsentStrictMode() || MicConsentAppAllowed(ImagePath);
}
