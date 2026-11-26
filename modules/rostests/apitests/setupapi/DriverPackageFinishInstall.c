/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test that the driver database leaves the configurations of classes with a class installer to setup
 */

#include <apitest.h>
#include <apitest_expected.h>
#include <windows.h>
#include <setupapi.h>
#include <regstr.h>
#include <stdio.h>

/* The Net and Media class installers write what the device needs to run (NDIS keywords, the software bus audio devices) */
static const WCHAR *FinishInstallClasses[] =
{
    L"{4d36e972-e325-11ce-bfc1-08002be10318}",
    L"{4d36e96c-e325-11ce-bfc1-08002be10318}",
};

static BOOL
PackageClass(HKEY Package, WCHAR *Class, DWORD Chars)
{
    BYTE Version[64];
    DWORD Size = sizeof(Version), Type;

    /* Bytes 8 to 23 of the package version hold its class GUID */
    if (RegQueryValueExW(Package, L"Version", NULL, &Type, Version, &Size) != ERROR_SUCCESS ||
        Type != REG_BINARY || Size < 24)
        return FALSE;
    return StringFromGUID2((const GUID *)(Version + 8), Class, Chars) != 0;
}

static BOOL
NeedsFinishInstall(const WCHAR *Class)
{
    ULONG Index;

    for (Index = 0; Index < ARRAYSIZE(FinishInstallClasses); Index++)
    {
        if (!_wcsicmp(Class, FinishInstallClasses[Index]))
            return TRUE;
    }
    return FALSE;
}

static VOID
CheckPackage(HKEY Packages, const WCHAR *Name, DWORD *Checked)
{
    WCHAR Class[40], Configuration[256];
    DWORD Index, Length, Flags, Size, Type;
    HKEY Package, Configurations, Key;

    if (RegOpenKeyExW(Packages, Name, 0, KEY_READ, &Package) != ERROR_SUCCESS)
        return;
    if (!PackageClass(Package, Class, ARRAYSIZE(Class)) || !NeedsFinishInstall(Class) ||
        RegOpenKeyExW(Package, L"Configurations", 0, KEY_READ, &Configurations) != ERROR_SUCCESS)
    {
        RegCloseKey(Package);
        return;
    }

    for (Index = 0; ; Index++)
    {
        Length = ARRAYSIZE(Configuration);
        if (RegEnumKeyExW(Configurations, Index, Configuration, &Length, NULL, NULL, NULL, NULL) != ERROR_SUCCESS)
            break;
        if (RegOpenKeyExW(Configurations, Configuration, 0, KEY_READ, &Key) != ERROR_SUCCESS)
            continue;
        Flags = 0;
        Size = sizeof(Flags);
        ok(RegQueryValueExW(Key, L"ConfigFlags", NULL, &Type, (BYTE *)&Flags, &Size) == ERROR_SUCCESS &&
           Type == REG_DWORD, "%ls\\%ls has no ConfigFlags\n", Name, Configuration);
        ok(Flags & CONFIGFLAG_FINISH_INSTALL, "%ls\\%ls (class %ls) has ConfigFlags 0x%lx, expected CONFIGFLAG_FINISH_INSTALL\n",
           Name, Configuration, Class, Flags);
        RegCloseKey(Key);
        (*Checked)++;
    }

    RegCloseKey(Configurations);
    RegCloseKey(Package);
}

START_TEST(DriverPackageFinishInstall)
{
    WCHAR Name[256];
    DWORD Index, Length, Checked = 0;
    HKEY Packages;

    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SYSTEM\\DriverDatabase\\DriverPackages", 0, KEY_READ, &Packages) != ERROR_SUCCESS)
    {
        apitest_missing("No driver database\n");
        return;
    }

    for (Index = 0; ; Index++)
    {
        Length = ARRAYSIZE(Name);
        if (RegEnumKeyExW(Packages, Index, Name, &Length, NULL, NULL, NULL, NULL) != ERROR_SUCCESS)
            break;
        CheckPackage(Packages, Name, &Checked);
    }

    RegCloseKey(Packages);
    ApitestSummary("DriverPackageFinishInstall", Checked, 0, "Net and Media configurations checked");
    ok(Checked > 0, "no Net or Media driver configuration\n");
    trace("%lu Net and Media configurations checked\n", Checked);
}
