/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test the section offsets MapViewOfFile3 accepts with and without a placeholder
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"

#ifndef MEM_RESERVE_PLACEHOLDER
#define MEM_RESERVE_PLACEHOLDER 0x00040000
#endif
#ifndef MEM_REPLACE_PLACEHOLDER
#define MEM_REPLACE_PLACEHOLDER 0x00004000
#endif
#ifndef MEM_PRESERVE_PLACEHOLDER
#define MEM_PRESERVE_PLACEHOLDER 0x00000002
#endif

typedef PVOID (WINAPI *PFN_VIRTUAL_ALLOC2)(HANDLE, PVOID, SIZE_T, ULONG, ULONG, PVOID, ULONG);
typedef PVOID (WINAPI *PFN_MAP_VIEW_OF_FILE3)(HANDLE, HANDLE, PVOID, ULONG64, SIZE_T, ULONG, ULONG, PVOID, ULONG);
typedef BOOL (WINAPI *PFN_UNMAP_VIEW_OF_FILE2)(HANDLE, PVOID, ULONG);

#define SECTION_SIZE 0x40000

START_TEST(MapViewOfFile3)
{
    HMODULE KernelBase = GetModuleHandleW(L"kernelbase.dll");
    PFN_VIRTUAL_ALLOC2 pVirtualAlloc2;
    PFN_MAP_VIEW_OF_FILE3 pMapViewOfFile3;
    PFN_UNMAP_VIEW_OF_FILE2 pUnmapViewOfFile2;
    HANDLE Section;
    PUCHAR Writer, Placeholder, View;
    ULONG i;

    pVirtualAlloc2 = (PFN_VIRTUAL_ALLOC2)GetProcAddress(KernelBase, "VirtualAlloc2");
    pMapViewOfFile3 = (PFN_MAP_VIEW_OF_FILE3)GetProcAddress(KernelBase, "MapViewOfFile3");
    pUnmapViewOfFile2 = (PFN_UNMAP_VIEW_OF_FILE2)GetProcAddress(KernelBase, "UnmapViewOfFile2");
    if (!pVirtualAlloc2 || !pMapViewOfFile3 || !pUnmapViewOfFile2)
    {
        skip("placeholder functions are missing\n");
        return;
    }

    Section = CreateFileMappingW(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, 0, SECTION_SIZE, NULL);
    ok(Section != NULL, "CreateFileMappingW failed %lu\n", GetLastError());
    if (!Section)
        return;
    Writer = MapViewOfFile(Section, FILE_MAP_WRITE, 0, 0, SECTION_SIZE);
    ok(Writer != NULL, "MapViewOfFile failed %lu\n", GetLastError());
    if (!Writer)
    {
        CloseHandle(Section);
        return;
    }
    for (i = 0; i < SECTION_SIZE / 0x1000; i++)
        Writer[i * 0x1000] = (UCHAR)(0x40 + i);

    SetLastError(0xdeadbeef);
    View = pMapViewOfFile3(Section, GetCurrentProcess(), NULL, 0x3000, 0x2000, 0, PAGE_READONLY, NULL, 0);
    ok(View == NULL, "page-aligned offset without a placeholder mapped at %p\n", View);
    ok_long(GetLastError(), ERROR_MAPPED_ALIGNMENT);
    if (View)
        UnmapViewOfFile(View);

    Placeholder = pVirtualAlloc2(GetCurrentProcess(), NULL, 0x2000, MEM_RESERVE | MEM_RESERVE_PLACEHOLDER,
                                 PAGE_NOACCESS, NULL, 0);
    ok(Placeholder != NULL, "VirtualAlloc2 failed %lu\n", GetLastError());
    if (Placeholder)
    {
        SetLastError(0xdeadbeef);
        View = pMapViewOfFile3(Section, GetCurrentProcess(), Placeholder, 0x800, 0x2000, MEM_REPLACE_PLACEHOLDER,
                               PAGE_READONLY, NULL, 0);
        ok(View == NULL, "unaligned offset replaced the placeholder at %p\n", View);
        ok_long(GetLastError(), ERROR_MAPPED_ALIGNMENT);

        SetLastError(0xdeadbeef);
        View = pMapViewOfFile3(Section, GetCurrentProcess(), Placeholder, 0x3000, 0x2000, MEM_REPLACE_PLACEHOLDER,
                               PAGE_READONLY, NULL, 0);
        ok(View == Placeholder, "view %p, placeholder %p, error %lu\n", View, Placeholder, GetLastError());
        if (View == Placeholder)
        {
            ok(View[0] == 0x43, "first page holds %#x, expected 0x43\n", View[0]);
            ok(View[0x1000] == 0x44, "second page holds %#x, expected 0x44\n", View[0x1000]);
            ok(pUnmapViewOfFile2(GetCurrentProcess(), View, MEM_PRESERVE_PLACEHOLDER),
               "UnmapViewOfFile2 failed %lu\n", GetLastError());
        }
        ok(VirtualFree(Placeholder, 0, MEM_RELEASE), "VirtualFree failed %lu\n", GetLastError());
    }

    UnmapViewOfFile(Writer);
    CloseHandle(Section);
}
