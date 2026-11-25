/*
 * PROJECT:     LiberNT Intel Smart Sound DSP driver
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     The request lock of the DSP's IPC transport as the driver holds it: one request at a time, a second caller waits up to its budget and is told whether it got the lock
 * COPYRIGHT:   Copyright 2026 LiberNT contributors
 */

#ifndef _DRIVERS_INTCDSP_IPCLOCK_H_
#define _DRIVERS_INTCDSP_IPCLOCK_H_

#include <ntddk.h>

/* A semaphore of one, as the WFP transaction lock and the mount manager's remote database lock use one: a request that re-enters on the thread that holds it waits for the budget too, where a kernel mutex would let it through behind itself */
typedef struct _INTCDSP_REQUEST_LOCK
{
    KSEMAPHORE Gate;
} INTCDSP_REQUEST_LOCK, *PINTCDSP_REQUEST_LOCK;

VOID
IntcDspRequestLockInit(
    _Out_ PINTCDSP_REQUEST_LOCK Lock);

/* TRUE when the lock was taken, FALSE when the wait reached TimeoutUs; PASSIVE_LEVEL */
_Must_inspect_result_
BOOLEAN
IntcDspRequestLockAcquire(
    _Inout_ PINTCDSP_REQUEST_LOCK Lock,
    _In_ UINT32 TimeoutUs);

VOID
IntcDspRequestLockRelease(
    _Inout_ PINTCDSP_REQUEST_LOCK Lock);

#endif
