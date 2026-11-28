/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test the error CryptCATAdminEnumCatalogFromHash reports for a hash no catalog lists
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <apitest.h>
#include <wincrypt.h>
#include <wintrust.h>
#include <mscat.h>

START_TEST(CatalogFromHash)
{
    BYTE Hash[64];
    DWORD HashSize = sizeof(Hash);
    HCATADMIN Admin = NULL;
    HCATINFO Info;
    WCHAR Self[MAX_PATH];
    HANDLE File;

    ok(CryptCATAdminAcquireContext(&Admin, NULL, 0), "CryptCATAdminAcquireContext failed %lu\n", GetLastError());
    if (!Admin)
        return;
    GetModuleFileNameW(NULL, Self, MAX_PATH);
    File = CreateFileW(Self, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    ok(File != INVALID_HANDLE_VALUE, "opening the test failed %lu\n", GetLastError());
    if (File != INVALID_HANDLE_VALUE)
    {
        ok(CryptCATAdminCalcHashFromFileHandle(File, &HashSize, Hash, 0), "hashing failed %lu\n", GetLastError());
        ok_long(HashSize, 20);
        CloseHandle(File);
        SetLastError(0xdeadbeef);
        Info = CryptCATAdminEnumCatalogFromHash(Admin, Hash, HashSize, 0, NULL);
        ok(Info == NULL, "the test is listed in catalog %p\n", Info);
        ok_long(GetLastError(), ERROR_NOT_FOUND);
        if (Info)
            CryptCATAdminReleaseCatalogContext(Admin, Info, 0);
    }
    CryptCATAdminReleaseContext(Admin, 0);
}
