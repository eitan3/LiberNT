/*
 * PROJECT:     LiberNT Explorer - Unit-tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Pin the Start menu program tree and path rules shared by the Start menus
 * COPYRIGHT:   Copyright 2026 Eitan Hadar <eitanhadar4@gmail.com>
 */

#include "precomp.h"

#include <apitest.h>

#include "startmodel.h"

HINSTANCE hExplorerInstance;

BOOL GetAdvancedBool(IN LPCWSTR pszValueName, IN BOOL bDefaultValue)
{
    UNREFERENCED_PARAMETER(pszValueName);
    return bDefaultValue;
}

static SM2ITEM *Item(SM2ITEM *pParent, LPCWSTR pszName, BOOL bFolder)
{
    SM2ITEM *p = new SM2ITEM();
    p->szName = pszName;
    p->szPath = CStringW(L"C:\\Start\\") + pszName;
    p->bFolder = bFolder;
    pParent->Children.Add(p);
    return p;
}

static void TestTree()
{
    SM2ITEM root;
    SM2ITEM *tools = Item(&root, L"Tools", TRUE);
    Item(&root, L"zeta", FALSE);
    Item(&root, L"Alpha", FALSE);
    Item(tools, L"Paint", FALSE);
    Item(tools, L"Calc", FALSE);
    SM2ITEM *games = Item(&root, L"Games", TRUE);
    Item(games, L"Chess", FALSE);

    SM2SortTree(&root);
    ok(!wcscmp(root.Children[0]->szName, L"Alpha") && !wcscmp(root.Children[1]->szName, L"zeta"),
       "files sort first, case-insensitively: %ls %ls\n", (LPCWSTR)root.Children[0]->szName,
       (LPCWSTR)root.Children[1]->szName);
    ok(!wcscmp(root.Children[2]->szName, L"Games") && !wcscmp(root.Children[3]->szName, L"Tools"),
       "folders sort after files\n");
    ok(!wcscmp(tools->Children[0]->szName, L"Calc"), "subfolders are sorted too\n");

    ok(SM2FindChild(&root, L"TOOLS", TRUE) == tools, "find is case-insensitive\n");
    ok(SM2FindChild(&root, L"Tools", FALSE) == NULL, "find matches the folder flag\n");
    ok(SM2FindChild(&root, L"Missing", FALSE) == NULL, "missing child\n");

    CAtlArray<SM2ITEM*> flat;
    SM2FlattenTree(&root, flat);
    ok(flat.GetCount() == 5, "flattened %u items\n", (UINT)flat.GetCount());
    BOOL noFolders = TRUE;
    for (SIZE_T i = 0; i < flat.GetCount(); i++)
        noFolders = noFolders && !flat[i]->bFolder;
    ok(noFolders, "flattened list has no folders\n");
    ok(!wcscmp(flat[0]->szName, L"Alpha") && !wcscmp(flat[2]->szName, L"Chess") && !wcscmp(flat[4]->szName, L"Paint"),
       "flattened order follows the tree\n");

    root.Clear();
    ok(root.Children.GetCount() == 0 && !root.bExpanded, "Clear empties the tree\n");
}

static void TestAdoptCaches()
{
    SM2ITEM oldRoot, newRoot;
    SM2ITEM *oldItem = Item(&oldRoot, L"Notepad", FALSE);
    SM2ITEM *newItem = Item(&newRoot, L"Notepad", FALSE);
    SM2ITEM *changed = Item(&newRoot, L"Paint", FALSE);
    SM2ITEM *oldChanged = Item(&oldRoot, L"Paint", FALSE);
    CAtlArray<SM2ITEM*> flatOld, flatNew;

    oldItem->Written = newItem->Written = 42;
    oldItem->szTargetPath = L"C:\\Windows\\notepad.exe";
    oldItem->bTargetTried = TRUE;
    oldChanged->Written = 1;
    oldChanged->szTargetPath = L"C:\\old.exe";
    oldChanged->bTargetTried = TRUE;
    changed->Written = 2;

    SM2FlattenTree(&oldRoot, flatOld);
    SM2FlattenTree(&newRoot, flatNew);
    SM2AdoptCaches(flatNew, flatOld);
    ok(newItem->bTargetTried && !wcscmp(newItem->szTargetPath, L"C:\\Windows\\notepad.exe"),
       "an unchanged item keeps its resolved target\n");
    ok(!changed->bTargetTried && changed->szTargetPath.IsEmpty(), "a rewritten item is resolved again\n");
}

static void TestPaths()
{
    CStringW path;
    WCHAR windir[MAX_PATH];

    ok(!SM2NormalizePath(NULL, path) && !SM2NormalizePath(L"", path), "empty paths are refused\n");
    ok(SM2NormalizePath(L"%SystemRoot%\\System32\\..\\notepad.exe", path), "path normalized\n");
    GetWindowsDirectoryW(windir, _countof(windir));
    CStringW expected = CStringW(windir) + L"\\notepad.exe";
    ok(!path.CompareNoCase(expected), "normalized to %ls, expected %ls\n", (LPCWSTR)path, (LPCWSTR)expected);
}

START_TEST(StartModel)
{
    TestTree();
    TestAdoptCaches();
    TestPaths();
}
