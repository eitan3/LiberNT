/*
 * PROJECT:     LiberNT audio driver libraries
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Fills the code-loader DMA buffer of an HD Audio stream: the payload copy, the buffer descriptor list over the physical segments and the stream parameters
 * COPYRIGHT:   Copyright 2026 LiberNT contributors
 */

/*
 * The buffer descriptor entry (address, length, interrupt-on-completion flag), the optional
 * 4 KiB entry boundary, the cyclic buffer length, the last valid index and the software
 * position in buffer are the HD Audio controller's stream descriptor programming (Intel
 * High Definition Audio Specification, stream descriptor registers SDxCBL, SDxLVI,
 * SDxBDLPU/L, SDxFIFOS and the position based FIFO capability), as Linux v6.12
 * sound/soc/sof/intel/hda-stream.c (hda_setup_bdle, hda_dsp_stream_setup_bdl,
 * hda_dsp_stream_hw_params, hda_dsp_stream_spib_config) and hda-loader.c (hda_cl_prepare)
 * apply them to the code loader. Linux splits entries at 4 KiB boundaries only for
 * controllers with the AZX_DCAPS_4K_BDLE_BOUNDARY quirk (sound/pci/hda/hda_controller.c
 * azx_bus_init); the boundary is therefore a caller setting here, zero for none. All of
 * this was studied under LICENSING.md rule 7; no text is copied.
 */

#include <ntddk.h>
#include "fwload.h"

#define FWLOAD_BDL_ENTRY_BYTES  16
#define FWLOAD_LVI_MAX          0x10000
/* The length field of a descriptor entry is 32 bits */
#define FWLOAD_CL_DMA_ENTRY_BYTES_MAX   0xFFFFFFFFULL

static
SIZE_T
FwloadClDmaWalk(
    _In_reads_(SegmentCount) const FWLOAD_DMA_SEGMENT* Segment,
    SIZE_T SegmentCount,
    SIZE_T PayloadBytes,
    UINT64 EntryBoundary,
    _Inout_opt_ PFWLOAD_CL_DMA Dma)
{
    /* a boundary wider than an address is never crossed */
    const UINT64 Boundary = EntryBoundary <= MAXSIZE_T ? EntryBoundary : 0;
    SIZE_T Index, Offset = 0, Entries = 0;

    for (Index = 0; Index < SegmentCount && Offset < PayloadBytes; Index++)
    {
        UINT64 Address = Segment[Index].Address;
        SIZE_T Done = 0;

        if (Segment[Index].Bytes == 0)
            continue;
        while (Done < Segment[Index].Bytes && Offset < PayloadBytes)
        {
            SIZE_T Chunk = Segment[Index].Bytes - Done;

            if (PayloadBytes - Offset < Chunk)
                Chunk = PayloadBytes - Offset;
            if (Chunk > FWLOAD_CL_DMA_ENTRY_BYTES_MAX)
                Chunk = FWLOAD_CL_DMA_ENTRY_BYTES_MAX;
            if (Boundary != 0)
            {
                SIZE_T Remain = (SIZE_T)Boundary - (SIZE_T)((Address + Done) % Boundary);

                if (Chunk > Remain)
                    Chunk = Remain;
            }
            if (Dma != NULL)
            {
                PFWLOAD_BDL_ENTRY Entry = &Dma->Bdl[Entries];

                Entry->AddressLow = (UINT32)((Address + Done) & 0xFFFFFFFFULL);
                Entry->AddressHigh = (UINT32)((Address + Done) >> 32);
                Entry->Bytes = (UINT32)Chunk;
                Entry->InterruptOnCompletion = 0;
            }
            Done += Chunk;
            Offset += Chunk;
            Entries++;
        }
    }
    if (Offset < PayloadBytes)
        return 0;
    return Entries;
}

SIZE_T
FwloadClDmaEntryCount(
    const FWLOAD_DMA_SEGMENT* Segment,
    SIZE_T SegmentCount,
    SIZE_T PayloadBytes,
    UINT64 EntryBoundary)
{
    return FwloadClDmaWalk(Segment, SegmentCount, PayloadBytes, EntryBoundary, NULL);
}

FWLOAD_STATUS
FwloadClDmaPrepare(
    PFWLOAD_CL_DMA Dma,
    const UINT8* Payload,
    SIZE_T PayloadBytes)
{
    SIZE_T Needed;

    if (Dma == NULL)
        return FwloadBadLength;
    Dma->FragmentCount = 0;
    Dma->Bytes = 0;
    if (Payload == NULL || Dma->Virtual == NULL || Dma->Bdl == NULL ||
        Dma->Segment == NULL || Dma->SegmentCount == 0 || PayloadBytes == 0 ||
        (Dma->EntryBoundary != 0 && (Dma->EntryBoundary & (Dma->EntryBoundary - 1)) != 0))
        return FwloadBadLength;
    if (PayloadBytes > Dma->Capacity)
        return FwloadDmaTooSmall;
    Needed = FwloadClDmaWalk(Dma->Segment, Dma->SegmentCount, PayloadBytes, Dma->EntryBoundary, NULL);
    if (Needed == 0)
        return FwloadDmaTooSmall;
    if (Needed > Dma->BdlCapacity || Needed > FWLOAD_LVI_MAX ||
        Needed * FWLOAD_BDL_ENTRY_BYTES > Dma->BdlBytes)
        return FwloadDmaTooManyFragments;

    RtlCopyMemory(Dma->Virtual, Payload, PayloadBytes);
    Needed = FwloadClDmaWalk(Dma->Segment, Dma->SegmentCount, PayloadBytes, Dma->EntryBoundary, Dma);
    Dma->Bytes = PayloadBytes;
    Dma->FragmentCount = Needed;
    Dma->LastValidIndex = (UINT16)(Needed - 1);
    Dma->StreamTag = Dma->StreamTag ? Dma->StreamTag : FWLOAD_CL_STREAM_TAG;
    Dma->StreamFormat = FWLOAD_CL_STREAM_FORMAT;
    Dma->Spib = (UINT32)PayloadBytes;
    return FwloadOk;
}
