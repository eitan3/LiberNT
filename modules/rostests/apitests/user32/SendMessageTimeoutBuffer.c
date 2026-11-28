/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test that a timed-out cross-thread send does not let the receiver write into the sender's buffer
 * COPYRIGHT:   Copyright 2026 Eitan Hadar <eitanhadar4@gmail.com>
 */

#include "precomp.h"

#define SLOW_DELAY_MS 250

static HANDLE g_Entered;
static HANDLE g_Done;
static HANDLE g_Ready;
static HWND g_Window;
static HWND g_FastWindow;
static LONG g_Calls;

static LRESULT CALLBACK ReceiverProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    if (msg == WM_GETMINMAXINFO && wParam == 1)
    {
        MINMAXINFO *info = (MINMAXINFO *)lParam;
        InterlockedIncrement(&g_Calls);
        SetEvent(g_Entered);
        Sleep(SLOW_DELAY_MS);
        info->ptMaxSize.x = 1234;
        info->ptMaxSize.y = 5678;
        info->ptReserved.x = 0x7777;
        SetEvent(g_Done);
        return 42;
    }
    if (msg == WM_APP)
    {
        WaitForSingleObject(g_Done, 5000);
        Sleep(100);
        return 0;
    }
    if (msg == WM_GETMINMAXINFO && wParam == 3)
    {
        MINMAXINFO *info = (MINMAXINFO *)lParam;
        if (info->ptMaxSize.x == 1234 || info->ptMaxSize.y == 5678 || info->ptReserved.x == 0x7777)
            return 99;
        return 3;
    }
    if (msg == WM_GETMINMAXINFO && wParam == 2)
    {
        MINMAXINFO *info = (MINMAXINFO *)lParam;
        info->ptMaxSize.x = info->ptMinTrackSize.x + 1;
        info->ptMaxSize.y = 99;
        return 7;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

static DWORD WINAPI ReceiverThread(PVOID param)
{
    MSG message;
    HWND *window = (HWND *)param;

    *window = CreateWindowExW(0, L"SendMessageTimeoutBuffer", L"", WS_POPUP, 0, 0, 10, 10, NULL, NULL,
                               GetModuleHandleW(NULL), NULL);
    SetEvent(g_Ready);
    while (GetMessageW(&message, NULL, 0, 0) > 0)
        DispatchMessageW(&message);
    return 0;
}

static void TimedOutSend(MINMAXINFO *info)
{
    DWORD_PTR result = 0x5555;
    LRESULT ret;

    memset(info, 0x11, sizeof(*info));
    ResetEvent(g_Entered);
    ResetEvent(g_Done);
    SetLastError(0xdeadbeef);
    ret = SendMessageTimeoutW(g_Window, WM_GETMINMAXINFO, 1, (LPARAM)info, SMTO_NORMAL, 50, &result);
    ok(ret == 0, "the slow send times out: %Id\n", (ptrdiff_t)ret);
    ok(GetLastError() == ERROR_TIMEOUT, "the timeout is reported: %lu\n", GetLastError());
    ok(WaitForSingleObject(g_Entered, 1000) == WAIT_OBJECT_0, "the receiver handles the message\n");
}

static void TestTimeoutAfterDispatch(void)
{
    MINMAXINFO info;
    BYTE expected[sizeof(MINMAXINFO)];

    TimedOutSend(&info);
    ok(WaitForSingleObject(g_Done, 5000) == WAIT_OBJECT_0, "the receiver finishes\n");
    Sleep(50);
    memset(expected, 0x11, sizeof(expected));
    ok(!memcmp(&info, expected, sizeof(info)), "the late reply does not reach the sender's buffer\n");
}

static void TestCompletedSend(void)
{
    MINMAXINFO info;
    DWORD_PTR result = 0;
    LRESULT ret;

    memset(&info, 0, sizeof(info));
    info.ptMinTrackSize.x = 41;
    ret = SendMessageTimeoutW(g_Window, WM_GETMINMAXINFO, 2, (LPARAM)&info, SMTO_NORMAL, 5000, &result);
    ok(ret != 0 && result == 7, "a completed send returns the result: %Id %p\n", (ptrdiff_t)ret, (PVOID)result);
    ok(info.ptMaxSize.x == 42 && info.ptMaxSize.y == 99, "the receiver's writes come back: %ld %ld\n",
       info.ptMaxSize.x, info.ptMaxSize.y);
    ok(info.ptMinTrackSize.x == 41, "the receiver saw the sender's input\n");
}

static void TestLateReplyAfterFree(void)
{
    MINMAXINFO slow, fast;
    DWORD_PTR result = 0;
    LRESULT ret;
    int leaks = 0;

    for (int i = 0; i < 10; i++)
    {
        TimedOutSend(&slow);
        PostMessageW(g_FastWindow, WM_APP, 0, 0);
        Sleep(20);
        memset(&fast, 0x22, sizeof(fast));
        ret = SendMessageTimeoutW(g_FastWindow, WM_GETMINMAXINFO, 3, (LPARAM)&fast, SMTO_NORMAL, 5000, &result);
        ok(ret != 0 && (result == 3 || result == 99), "the second send completes: %Id %p\n", (ptrdiff_t)ret,
           (PVOID)result);
        if (result == 99 || fast.ptMaxSize.x == 1234 || fast.ptMaxSize.y == 5678 || fast.ptReserved.x == 0x7777)
            leaks++;
    }
    ok(leaks == 0, "a late reply to a timed-out send never lands in another send's parameters: %d of 10\n", leaks);
}

static void ReuseStack(void)
{
    volatile BYTE scratch[4096];
    for (UINT i = 0; i < sizeof(scratch); i++)
        scratch[i] = (BYTE)i;
}

static void TestStress(void)
{
    MINMAXINFO info;

    for (int i = 0; i < 20; i++)
    {
        TimedOutSend(&info);
        ReuseStack();
        WaitForSingleObject(g_Done, 5000);
    }
    Sleep(SLOW_DELAY_MS);
    ok(g_Calls >= 21, "every slow send reached the receiver: %ld\n", g_Calls);
    TestCompletedSend();
}

START_TEST(SendMessageTimeoutBuffer)
{
    WNDCLASSW wc = { 0 };
    HANDLE thread, fastThread;
    DWORD tid, fastTid;

    wc.lpfnWndProc = ReceiverProc;
    wc.hInstance = GetModuleHandleW(NULL);
    wc.lpszClassName = L"SendMessageTimeoutBuffer";
    ok(RegisterClassW(&wc) != 0, "RegisterClass failed: %lu\n", GetLastError());
    g_Entered = CreateEventW(NULL, TRUE, FALSE, NULL);
    g_Done = CreateEventW(NULL, TRUE, FALSE, NULL);
    g_Ready = CreateEventW(NULL, TRUE, FALSE, NULL);
    thread = CreateThread(NULL, 0, ReceiverThread, &g_Window, 0, &tid);
    ok(thread != NULL, "CreateThread failed: %lu\n", GetLastError());
    if (!thread)
        return;
    WaitForSingleObject(g_Ready, INFINITE);
    ResetEvent(g_Ready);
    fastThread = CreateThread(NULL, 0, ReceiverThread, &g_FastWindow, 0, &fastTid);
    ok(fastThread != NULL, "CreateThread failed: %lu\n", GetLastError());
    if (!fastThread)
        return;
    WaitForSingleObject(g_Ready, INFINITE);
    ok(IsWindow(g_Window) && IsWindow(g_FastWindow), "the receiver windows exist\n");

    TestCompletedSend();
    TestTimeoutAfterDispatch();
    TestStress();
    TestLateReplyAfterFree();

    PostThreadMessageW(tid, WM_QUIT, 0, 0);
    PostThreadMessageW(fastTid, WM_QUIT, 0, 0);
    ok(WaitForSingleObject(thread, 5000) == WAIT_OBJECT_0, "the receiver thread exits\n");
    ok(WaitForSingleObject(fastThread, 5000) == WAIT_OBJECT_0, "the second receiver thread exits\n");
    CloseHandle(thread);
    CloseHandle(fastThread);
    CloseHandle(g_Entered);
    CloseHandle(g_Done);
    CloseHandle(g_Ready);
}
