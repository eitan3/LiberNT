/*
 * PROJECT:     LiberNT audio driver libraries
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Keeps a Sound Open Firmware image resident for its owner: read once through injected operations, parsed with the container reader, kept until released
 * COPYRIGHT:   Copyright 2026 LiberNT contributors
 */

#include <ntddk.h>
#include "fwload.h"

/* The module table of the parsed container: the first parse counts the modules, the second fills the table */
static
FWLOAD_STATUS
FwloadImageParse(
    _Inout_ PFWLOAD_IMAGE Image,
    _In_ const FWLOAD_IMAGE_OPS* Ops,
    _Inout_ PVOID Context)
{
    FWLOAD_STATUS Status;
    UINT32 Count;

    Status = SofRiParse(Image->Data, Image->Bytes, NULL, 0, &Image->Ri);
    if (Status != FwloadOk && Status != FwloadTooManyModules)
        return Status;
    Count = Image->Ri.ModuleCount;
    if (Count == 0 || Count > SOF_RI_MAX_EXT_MAN_MODULES)
        return FwloadTooManyModules;
    Image->Modules = Ops->Allocate(Context, (SIZE_T)Count * sizeof(SOF_RI_MODULE));
    if (Image->Modules == NULL)
        return FwloadOutOfMemory;
    return SofRiParse(Image->Data, Image->Bytes, Image->Modules, Count, &Image->Ri);
}

FWLOAD_STATUS
FwloadImageEnsure(
    _Inout_ PFWLOAD_IMAGE Image,
    _In_ const FWLOAD_IMAGE_OPS* Ops,
    _Inout_ PVOID Context)
{
    FWLOAD_STATUS Status;

    if (Image->Modules != NULL)
        return FwloadOk;
    Status = Ops->Read(Context, &Image->Data, &Image->Bytes);
    if (Status != FwloadOk)
    {
        /* a failed read releases its own; nothing stays behind */
        Image->Data = NULL;
        Image->Bytes = 0;
        return Status;
    }
    Status = FwloadImageParse(Image, Ops, Context);
    if (Status != FwloadOk)
    {
        FwloadImageFree(Image, Ops, Context);
        return Status;
    }
    return FwloadOk;
}

VOID
FwloadImageFree(
    _Inout_ PFWLOAD_IMAGE Image,
    _In_ const FWLOAD_IMAGE_OPS* Ops,
    _Inout_ PVOID Context)
{
    static const FWLOAD_IMAGE Empty;
    SOF_RI_IMAGE Parsed = Image->Ri;

    if (Image->Modules != NULL)
        Ops->Free(Context, Image->Modules);
    if (Image->Data != NULL)
        Ops->Free(Context, (PVOID)Image->Data);
    /* the parsed facts stay for a log line; the pointers into the released bytes do not */
    *Image = Empty;
    Parsed.Data = NULL;
    Parsed.Size = 0;
    Parsed.Modules = NULL;
    Parsed.ModuleCapacity = 0;
    Image->Ri = Parsed;
}
