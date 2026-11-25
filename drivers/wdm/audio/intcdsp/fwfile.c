/*
 * PROJECT:     LiberNT Intel Smart Sound DSP driver
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Reads the Sound Open Firmware image for the DSP from the driver's firmware directory once, at start, and keeps it resident for the device through the fwload image holder
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

/* The image holder's operations: the whole file into paged pool, the module table into nonpaged pool, one tag for both */
static
FWLOAD_STATUS
IntcDspReadImage(
    PVOID Context,
    const UINT8** Data,
    SIZE_T* Bytes)
{
    UNICODE_STRING Path;
    PUCHAR Buffer;
    SIZE_T Count;
    NTSTATUS Status;
    PINTCDSP_DEVICE Dev = Context;

    RtlInitUnicodeString(&Path, INTCDSP_FIRMWARE_PATH);
    Status = IntcDspReadFile(INTCDSP_FIRMWARE_PATH, &Buffer, &Count);
    Dev->FirmwareReadStatus = Status;
    if (!NT_SUCCESS(Status))
    {
        IntcDspLog("firmware %wZ: not read, 0x%08lx\n", &Path, Status);
        return Status == STATUS_INSUFFICIENT_RESOURCES ? FwloadOutOfMemory : FwloadNoData;
    }
    *Data = Buffer;
    *Bytes = Count;
    return FwloadOk;
}

static
PVOID
IntcDspAllocateImage(
    PVOID Context,
    SIZE_T Bytes)
{
    UNREFERENCED_PARAMETER(Context);
    return ExAllocatePoolWithTag(NonPagedPool, Bytes, INTCDSP_POOL_TAG);
}

static
VOID
IntcDspFreeImage(
    PVOID Context,
    PVOID Memory)
{
    UNREFERENCED_PARAMETER(Context);
    ExFreePoolWithTag(Memory, INTCDSP_POOL_TAG);
}

static const FWLOAD_IMAGE_OPS IntcDspImageOps =
{
    IntcDspReadImage,
    IntcDspAllocateImage,
    IntcDspFreeImage
};

VOID
IntcDspFreeFirmware(
    _Inout_ PINTCDSP_DEVICE Dev)
{
    FwloadImageFree(&Dev->Image, &IntcDspImageOps, Dev);
    RtlZeroMemory(&Dev->Image.Ri, sizeof(Dev->Image.Ri));
}

NTSTATUS
IntcDspLoadFirmware(
    _Inout_ PINTCDSP_DEVICE Dev)
{
    UNICODE_STRING Path;
    const SOF_RI_IMAGE* Ri = &Dev->Image.Ri;
    BOOLEAN Resident;
    FWLOAD_STATUS Status;

    /* read at the first call that finds no image, at start, and kept for the device: a power-up callback finds it resident and reads nothing */
    Resident = Dev->Image.Data != NULL && Dev->Image.Bytes != 0;
    Status = FwloadImageEnsure(&Dev->Image, &IntcDspImageOps, Dev);
    Dev->FirmwareStatus = Status;
    RtlInitUnicodeString(&Path, INTCDSP_FIRMWARE_PATH);
    if (Status != FwloadOk)
    {
        if (Status != FwloadNoData && Status != FwloadOutOfMemory)
            IntcDspLog("firmware %wZ: %s at offset 0x%I64x\n", &Path, FwloadStatusName(Status), (ULONGLONG)Ri->BadOffset);
        if (Status == FwloadOutOfMemory)
            return STATUS_INSUFFICIENT_RESOURCES;
        return Status == FwloadNoData ? Dev->FirmwareReadStatus : STATUS_INVALID_IMAGE_FORMAT;
    }

    /* the line says whether the bytes were read by this call or were already resident for the device, so that the log of a resume shows no second read (R6) */
    IntcDspLog("firmware %wZ: %I64u bytes, %s, image '%s' version %u.%u.%u.%u, %u modules, payload 0x%I64x + 0x%I64x, %u preload pages\n",
               &Path, (ULONGLONG)Dev->Image.Bytes, Resident ? "resident" : "read", Ri->Name, Ri->Major, Ri->Minor,
               Ri->Hotfix, Ri->Build, Ri->ModuleCount, (ULONGLONG)Ri->PayloadOffset,
               (ULONGLONG)Ri->PayloadBytes, Ri->PreloadPageCount);
    return STATUS_SUCCESS;
}
