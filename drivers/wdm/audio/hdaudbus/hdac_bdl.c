#include <ntddk.h>
#include "hdac_bdl.h"

VOID
HdacBdlLayout(
    _In_ ULONG NotificationCount,
    _In_ SIZE_T RequestedBufferSize,
    _In_ UINT32 FrameBytes,
    _Out_ PHDAC_BDL_LAYOUT Layout)
{
    UINT32 Half, Align, MaxHalf;

    Layout->BufferBytes = RequestedBufferSize < (SIZE_T)HDA_MAX_BDL_ENTRIES * PAGE_SIZE ?
        (UINT32)RequestedBufferSize : (UINT32)((SIZE_T)HDA_MAX_BDL_ENTRIES * PAGE_SIZE);
    Layout->HalfBytes = 0;
    Layout->OffsetBytes = 0;
    Layout->AllocBytes = Layout->BufferBytes;
    if (NotificationCount != 2)
        return;
    /* each half is whole frames: the first multiple of 128 bytes that the frame divides */
    Align = 128;
    while (FrameBytes != 0 && Align % FrameBytes != 0)
        Align += 128;
    MaxHalf = (HDA_MAX_BDL_ENTRIES / 2) * PAGE_SIZE / Align * Align;
    Half = RequestedBufferSize / 2 < (SIZE_T)MaxHalf ? (UINT32)(RequestedBufferSize / 2) : MaxHalf;
    Half = (Half + Align - 1) / Align * Align;
    if (Half > MaxHalf)
        Half = MaxHalf;
    Layout->HalfBytes = Half;
    Layout->BufferBytes = Half * 2;
    /* the padding that ends the first half on a page boundary, none when the half is whole pages */
    Layout->OffsetBytes = (PAGE_SIZE - (Half % PAGE_SIZE)) % PAGE_SIZE;
    Layout->AllocBytes = Layout->BufferBytes + Layout->OffsetBytes;
}

/* One descriptor: the rest of the page the buffer has reached, or the rest of what is being described, whichever is shorter; the entry's address is the page's with the offset added, summed in 64 bits before it is split */
static
UINT32
HdacBdlAppend(
    _Out_ PHDAC_BDLENTRY Entry,
    PFN_NUMBER PageFrame,
    _Inout_ PUINT32 Offset,
    _Inout_ PUINT32 Remaining)
{
    ULONGLONG Address;
    UINT32 PageOff, Chunk;

    PageOff = *Offset % PAGE_SIZE;
    Chunk = PageOff == 0 ? PAGE_SIZE : PAGE_SIZE - PageOff;
    if (*Remaining < Chunk)
        Chunk = *Remaining;
    Address = ((ULONGLONG)PageFrame << PAGE_SHIFT) + PageOff;
    Entry->lowAddr = (UINT32)Address;
    Entry->highAddr = (UINT32)(Address >> 32);
    Entry->len = Chunk;
    *Remaining -= Chunk;
    *Offset += Chunk;
    return Chunk;
}

BOOLEAN
HdacBdlBuild(
    _Out_writes_(HDA_MAX_BDL_ENTRIES) PHDAC_BDLENTRY Bdl,
    _In_ const PFN_NUMBER* PageFrames,
    _In_ const HDAC_BDL_LAYOUT* Layout,
    _Out_ PUINT16 Entries)
{
    UINT16 NumBlocks = 0, PageNum;
    UINT32 Size, Offset, Half;

    /* the buffer starts after the padding, which is less than a page: in the allocation's first page */
    PageNum = 0;
    Offset = Layout->OffsetBytes;
    Half = Layout->HalfBytes;
    Size = Layout->BufferBytes;
    while (Half > 0)
    {
        if (NumBlocks >= HDA_MAX_BDL_ENTRIES)
            goto TooMany;
        Size -= HdacBdlAppend(&Bdl[NumBlocks], PageFrames[PageNum], &Offset, &Half);
        //Program interrupt for when buffer is halfway
        Bdl[NumBlocks].ioc = (Half > 0) ? 0 : 1;
        NumBlocks++;
        if ((Offset % PAGE_SIZE) == 0)
            PageNum++; //Only increment page num if we go past page boundary
    }
    while (Size > 0)
    {
        if (NumBlocks >= HDA_MAX_BDL_ENTRIES)
            goto TooMany;
        HdacBdlAppend(&Bdl[NumBlocks], PageFrames[PageNum], &Offset, &Size);
        //Program interrupt for when buffer ends
        Bdl[NumBlocks].ioc = (Size > 0) ? 0 : 1;
        NumBlocks++;
        if ((Offset % PAGE_SIZE) == 0)
            PageNum++; //Only increment page num if we go past page boundary
    }
    *Entries = NumBlocks;
    return TRUE;

TooMany:
    *Entries = HDA_MAX_BDL_ENTRIES;
    return FALSE;
}
