/*
 * The layout of a stream's cyclic buffer and the descriptor list over its
 * pages, apart from the allocation and the stream's setup, so that the host
 * test builds the lists the driver does: of hdac_stream.cpp's allocation,
 * and so of sklhdaudbus (CoolStar, BSD-3-Clause; LICENSE.txt in this
 * directory).
 */

#ifndef _SKLHDAUDBUS_HDAC_BDL_H_
#define _SKLHDAUDBUS_HDAC_BDL_H_

#include <ntddk.h>
#include "hda_registers.h"

#ifdef __cplusplus
extern "C" {
#endif

/* One entry of a stream's buffer descriptor list, as fdo.h declared it: a buffer's 64-bit address, its length, and the interrupt on completion in the length's bit 31 */
typedef struct _HDAC_BDLENTRY {
    UINT32 lowAddr;
    UINT32 highAddr;
    UINT32 len;
    UINT32 ioc;
} HDAC_BDLENTRY, *PHDAC_BDLENTRY;

/* Where a stream's cyclic buffer lies in the pages allocated for it */
typedef struct _HDAC_BDL_LAYOUT {
    /* the cyclic buffer's bytes, as the stream's SD_CBL takes them */
    UINT32 BufferBytes;
    /* the bytes of the first of two notifications, 0 when one is asked for: whole frames, as the first multiple of 128 bytes the frame divides */
    UINT32 HalfBytes;
    /* the bytes before the buffer in its pages: none, or the padding that ends the first half on a page boundary */
    UINT32 OffsetBytes;
    /* the bytes the pages hold: the buffer and the padding before it */
    UINT32 AllocBytes;
} HDAC_BDL_LAYOUT, *PHDAC_BDL_LAYOUT;

/*
 * The buffer a stream gets for a request: at most HDA_MAX_BDL_ENTRIES pages;
 * with two notifications asked for, two halves of whole frames around the
 * requested size, and the padding that ends the first half on a page
 * boundary, so that each half's interrupt on completion is the end of a
 * descriptor.
 */
VOID
HdacBdlLayout(
    _In_ ULONG NotificationCount,
    _In_ SIZE_T RequestedBufferSize,
    _In_ UINT32 FrameBytes,
    _Out_ PHDAC_BDL_LAYOUT Layout);

/*
 * The descriptor list of a laid-out buffer over the frames of its pages,
 * the allocation's first page first: one descriptor a page at most, from
 * the padding's end to the buffer's end, and the interrupt on completion at
 * the end of the first half and at the end of the buffer. FALSE when the
 * list would pass HDA_MAX_BDL_ENTRIES, which is then the count written.
 */
BOOLEAN
HdacBdlBuild(
    _Out_writes_(HDA_MAX_BDL_ENTRIES) PHDAC_BDLENTRY Bdl,
    _In_ const PFN_NUMBER* PageFrames,
    _In_ const HDAC_BDL_LAYOUT* Layout,
    _Out_ PUINT16 Entries);

#ifdef __cplusplus
}
#endif

#endif
