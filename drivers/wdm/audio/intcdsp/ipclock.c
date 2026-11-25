/*
 * PROJECT:     LiberNT Intel Smart Sound DSP driver
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     The request lock of the DSP's IPC transport: a kernel semaphore of one taken with the caller's budget, released by the request that took it
 * COPYRIGHT:   Copyright 2026 LiberNT contributors
 */

#include "ipclock.h"

VOID
IntcDspRequestLockInit(
    PINTCDSP_REQUEST_LOCK Lock)
{
    KeInitializeSemaphore(&Lock->Gate, 1, 1);
}

BOOLEAN
IntcDspRequestLockAcquire(
    PINTCDSP_REQUEST_LOCK Lock,
    UINT32 TimeoutUs)
{
    LARGE_INTEGER Due;
    NTSTATUS Status;

    PAGED_CODE();
    /* a relative wait of TimeoutUs microseconds: negative hundreds of nanoseconds, the zero meaning no wait at all */
    Due.QuadPart = -(LONGLONG)TimeoutUs * 10;
    Status = KeWaitForSingleObject(&Lock->Gate, Executive, KernelMode, FALSE, &Due);
    return Status == STATUS_SUCCESS;
}

VOID
IntcDspRequestLockRelease(
    PINTCDSP_REQUEST_LOCK Lock)
{
    KeReleaseSemaphore(&Lock->Gate, IO_NO_INCREMENT, 1, FALSE);
}
