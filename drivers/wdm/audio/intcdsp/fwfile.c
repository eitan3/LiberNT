/*
 * PROJECT:     LiberNT Intel Smart Sound DSP driver
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Reads the Sound Open Firmware image for the DSP from the driver's firmware directory and parses it with the fwload library
 * COPYRIGHT:   Copyright 2026 LiberNT contributors
 */

#include "intcdsp.h"

static
NTSTATUS
IntcDspReadFile(
    _In_ PCWSTR Path,
    _Out_ PUCHAR* Data,
    _Out_ PSIZE_T Bytes)
{
    FILE_STANDARD_INFORMATION Info;
    OBJECT_ATTRIBUTES Attributes;
    IO_STATUS_BLOCK IoStatus;
    UNICODE_STRING Name;
    HANDLE Handle;
    PUCHAR Buffer;
    NTSTATUS Status;

    *Data = NULL;
    *Bytes = 0;
    RtlInitUnicodeString(&Name, Path);
    InitializeObjectAttributes(&Attributes, &Name, OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE, NULL, NULL);
    Status = ZwCreateFile(&Handle, GENERIC_READ | SYNCHRONIZE, &Attributes, &IoStatus, NULL, FILE_ATTRIBUTE_NORMAL,
                          FILE_SHARE_READ, FILE_OPEN, FILE_SYNCHRONOUS_IO_NONALERT | FILE_NON_DIRECTORY_FILE, NULL, 0);
    if (!NT_SUCCESS(Status))
        return Status;

    Status = ZwQueryInformationFile(Handle, &IoStatus, &Info, sizeof(Info), FileStandardInformation);
    if (!NT_SUCCESS(Status) || Info.EndOfFile.QuadPart <= 0 ||
        (ULONGLONG)Info.EndOfFile.QuadPart > INTCDSP_FIRMWARE_MAX_BYTES)
    {
        ZwClose(Handle);
        return NT_SUCCESS(Status) ? STATUS_INVALID_IMAGE_FORMAT : Status;
    }

    Buffer = ExAllocatePoolWithTag(PagedPool, (SIZE_T)Info.EndOfFile.QuadPart, INTCDSP_POOL_TAG);
    if (!Buffer)
    {
        ZwClose(Handle);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    Status = ZwReadFile(Handle, NULL, NULL, NULL, &IoStatus, Buffer, (ULONG)Info.EndOfFile.QuadPart, NULL, NULL);
    ZwClose(Handle);
    if (!NT_SUCCESS(Status))
    {
        ExFreePoolWithTag(Buffer, INTCDSP_POOL_TAG);
        return Status;
    }
    if (IoStatus.Information != (ULONG_PTR)Info.EndOfFile.QuadPart)
    {
        ExFreePoolWithTag(Buffer, INTCDSP_POOL_TAG);
        return STATUS_END_OF_FILE;
    }

    *Data = Buffer;
    *Bytes = (SIZE_T)Info.EndOfFile.QuadPart;
    return STATUS_SUCCESS;
}

VOID
IntcDspFreeFirmware(
    _Inout_ PINTCDSP_DEVICE Dev)
{
    if (Dev->Modules)
    {
        ExFreePoolWithTag(Dev->Modules, INTCDSP_POOL_TAG);
        Dev->Modules = NULL;
    }
    if (Dev->Image)
    {
        ExFreePoolWithTag(Dev->Image, INTCDSP_POOL_TAG);
        Dev->Image = NULL;
    }
    Dev->ImageBytes = 0;
    RtlZeroMemory(&Dev->Ri, sizeof(Dev->Ri));
}

NTSTATUS
IntcDspLoadFirmware(
    _Inout_ PINTCDSP_DEVICE Dev)
{
    UNICODE_STRING Path;
    FWLOAD_STATUS Status;
    NTSTATUS NtStatus;
    ULONG Count;

    RtlInitUnicodeString(&Path, INTCDSP_FIRMWARE_PATH);
    IntcDspFreeFirmware(Dev);

    NtStatus = IntcDspReadFile(INTCDSP_FIRMWARE_PATH, &Dev->Image, &Dev->ImageBytes);
    if (!NT_SUCCESS(NtStatus))
    {
        IntcDspLog("firmware %wZ: not read, 0x%08lx\n", &Path, NtStatus);
        return NtStatus;
    }

    Status = SofRiParse(Dev->Image, Dev->ImageBytes, NULL, 0, &Dev->Ri);
    if (Status != FwloadOk && Status != FwloadTooManyModules)
        goto Invalid;
    Count = Dev->Ri.ModuleCount;
    if (Count == 0 || Count > SOF_RI_MAX_EXT_MAN_MODULES)
    {
        Status = FwloadTooManyModules;
        goto Invalid;
    }

    Dev->Modules = ExAllocatePoolWithTag(NonPagedPool, Count * sizeof(SOF_RI_MODULE), INTCDSP_POOL_TAG);
    if (!Dev->Modules)
    {
        NtStatus = STATUS_INSUFFICIENT_RESOURCES;
        goto Fail;
    }
    Status = SofRiParse(Dev->Image, Dev->ImageBytes, Dev->Modules, Count, &Dev->Ri);
    if (Status != FwloadOk)
        goto Invalid;

    IntcDspLog("firmware %wZ: %I64u bytes, %s, image '%s' version %u.%u.%u.%u, %u modules, payload 0x%I64x + 0x%I64x, %u preload pages\n",
               &Path, (ULONGLONG)Dev->ImageBytes, FwloadStatusName(FwloadOk), Dev->Ri.Name, Dev->Ri.Major, Dev->Ri.Minor,
               Dev->Ri.Hotfix, Dev->Ri.Build, Dev->Ri.ModuleCount, (ULONGLONG)Dev->Ri.PayloadOffset,
               (ULONGLONG)Dev->Ri.PayloadBytes, Dev->Ri.PreloadPageCount);
    return STATUS_SUCCESS;

Invalid:
    IntcDspLog("firmware %wZ: %s at offset 0x%I64x\n", &Path, FwloadStatusName(Status), (ULONGLONG)Dev->Ri.BadOffset);
    NtStatus = STATUS_INVALID_IMAGE_FORMAT;
Fail:
    IntcDspFreeFirmware(Dev);
    return NtStatus;
}
