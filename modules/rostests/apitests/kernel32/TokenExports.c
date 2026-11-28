/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test the token functions kernel32 and the process-threads API set export
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"

typedef BOOL (WINAPI *PFN_OPEN_PROCESS_TOKEN)(HANDLE, DWORD, PHANDLE);
typedef BOOL (WINAPI *PFN_OPEN_THREAD_TOKEN)(HANDLE, DWORD, BOOL, PHANDLE);
typedef BOOL (WINAPI *PFN_SET_THREAD_TOKEN)(PHANDLE, HANDLE);

static
VOID
TestModule(PCWSTR Name)
{
    HMODULE Module = LoadLibraryW(Name);
    PFN_OPEN_PROCESS_TOKEN OpenProcessTokenFn;
    PFN_OPEN_THREAD_TOKEN OpenThreadTokenFn;
    PFN_SET_THREAD_TOKEN SetThreadTokenFn;
    HANDLE Token = NULL;

    ok(Module != NULL, "%ls not loaded %lu\n", Name, GetLastError());
    if (!Module)
        return;
    OpenProcessTokenFn = (PFN_OPEN_PROCESS_TOKEN)GetProcAddress(Module, "OpenProcessToken");
    OpenThreadTokenFn = (PFN_OPEN_THREAD_TOKEN)GetProcAddress(Module, "OpenThreadToken");
    SetThreadTokenFn = (PFN_SET_THREAD_TOKEN)GetProcAddress(Module, "SetThreadToken");
    ok(OpenProcessTokenFn != NULL, "%ls: no OpenProcessToken\n", Name);
    ok(OpenThreadTokenFn != NULL, "%ls: no OpenThreadToken\n", Name);
    ok(SetThreadTokenFn != NULL, "%ls: no SetThreadToken\n", Name);
    if (OpenProcessTokenFn)
    {
        ok(OpenProcessTokenFn(GetCurrentProcess(), TOKEN_QUERY, &Token), "OpenProcessToken failed %lu\n", GetLastError());
        if (Token)
            CloseHandle(Token);
    }
    if (OpenThreadTokenFn)
    {
        Token = NULL;
        SetLastError(0xdeadbeef);
        ok(!OpenThreadTokenFn(GetCurrentThread(), TOKEN_QUERY, TRUE, &Token), "thread has a token\n");
        ok_long(GetLastError(), ERROR_NO_TOKEN);
    }
    if (SetThreadTokenFn)
        ok(SetThreadTokenFn(NULL, NULL), "SetThreadToken failed %lu\n", GetLastError());
    FreeLibrary(Module);
}

START_TEST(TokenExports)
{
    TestModule(L"kernel32.dll");
    TestModule(L"api-ms-win-core-processthreads-l1-1-1.dll");
}
