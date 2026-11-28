/*
 * PROJECT:     LiberNT Realtek RTL8125/RTL8126 Ethernet Driver
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Descriptor rings, transmit and receive
 * COPYRIGHT:   Copyright 2026 The LiberNT contributors
 */

#include "rtl8125.h"

static
VOID
Rt25SetDescriptorAddress(
    _Inout_ PRT25_DESCRIPTOR Descriptor,
    _In_ ULONG64 Address)
{
    Descriptor->AddressLow = (ULONG)Address;
    Descriptor->AddressHigh = (ULONG)(Address >> 32);
}

static
VOID
Rt25ArmReceive(
    _In_ PRT25_ADAPTER Adapter,
    _In_ ULONG Index)
{
    PRT25_DESCRIPTOR Descriptor = &Adapter->RxRing[Index];

    Rt25SetDescriptorAddress(Descriptor, Adapter->RxBuffers[Index].PhysicalAddress);
    Descriptor->Vlan = 0;
    KeMemoryBarrier();
    Descriptor->Flags = Rt25RxArmFlags(Index, Adapter->BufferSize);
    KeMemoryBarrier();
}

static
PVOID
Rt25RxChecksumInfo(
    _In_ PRT25_ADAPTER Adapter,
    _In_ ULONG DescriptorFlags,
    _In_reads_bytes_(Length) const UCHAR *Frame,
    _In_ ULONG Length)
{
    NDIS_TCP_IP_CHECKSUM_NET_BUFFER_LIST_INFO Info;
    RT25_FRAME_INFO FrameInfo;
    ULONG Result, Enabled = Adapter->Offload;

    Info.Value = NULL;
    if (!(Enabled & RT25_OFFLOAD_RX_ANY))
        return NULL;

    Rt25ParseFrame(Frame, Length, &FrameInfo);
    Result = Rt25RxChecksumResult(DescriptorFlags, &FrameInfo);
    if (FrameInfo.Ip == RT25_IP_V4 && Adapter->EncapsulationIPv4)
    {
        if (!(Enabled & RT25_OFFLOAD_RX_IP4))
            Result &= ~(RT25_RX_OK_IP | RT25_RX_BAD_IP);
        if (!(Enabled & RT25_OFFLOAD_RX_TCP4))
            Result &= ~(RT25_RX_OK_TCP | RT25_RX_BAD_TCP);
        if (!(Enabled & RT25_OFFLOAD_RX_UDP4))
            Result &= ~(RT25_RX_OK_UDP | RT25_RX_BAD_UDP);
    }
    else if (FrameInfo.Ip == RT25_IP_V6 && Adapter->EncapsulationIPv6)
    {
        if (!(Enabled & RT25_OFFLOAD_RX_TCP6))
            Result &= ~(RT25_RX_OK_TCP | RT25_RX_BAD_TCP);
        if (!(Enabled & RT25_OFFLOAD_RX_UDP6))
            Result &= ~(RT25_RX_OK_UDP | RT25_RX_BAD_UDP);
    }
    else
    {
        return NULL;
    }

    Info.Receive.IpChecksumSucceeded = !!(Result & RT25_RX_OK_IP);
    Info.Receive.IpChecksumFailed = !!(Result & RT25_RX_BAD_IP);
    Info.Receive.TcpChecksumSucceeded = !!(Result & RT25_RX_OK_TCP);
    Info.Receive.TcpChecksumFailed = !!(Result & RT25_RX_BAD_TCP);
    Info.Receive.UdpChecksumSucceeded = !!(Result & RT25_RX_OK_UDP);
    Info.Receive.UdpChecksumFailed = !!(Result & RT25_RX_BAD_UDP);
    return Info.Value;
}

static
NDIS_STATUS
Rt25AllocateChunks(
    _In_ PRT25_ADAPTER Adapter,
    _Out_writes_(RT25_MAX_CHUNKS) PRT25_CHUNK Chunks)
{
    ULONG i;

    for (i = 0; i < Adapter->ChunkCount; i++)
    {
        NdisMAllocateSharedMemory(Adapter->MiniportHandle, RT25_CHUNK_SIZE, TRUE,
                                  &Chunks[i].VirtualAddress, &Chunks[i].PhysicalAddress);
        if (!Chunks[i].VirtualAddress)
            return NDIS_STATUS_RESOURCES;

        NdisZeroMemory(Chunks[i].VirtualAddress, RT25_CHUNK_SIZE);
    }

    return NDIS_STATUS_SUCCESS;
}

static
VOID
Rt25FreeChunks(
    _In_ PRT25_ADAPTER Adapter,
    _Inout_updates_(RT25_MAX_CHUNKS) PRT25_CHUNK Chunks)
{
    ULONG i;

    for (i = 0; i < RT25_MAX_CHUNKS; i++)
    {
        if (Chunks[i].VirtualAddress)
        {
            NdisMFreeSharedMemory(Adapter->MiniportHandle, RT25_CHUNK_SIZE, TRUE,
                                  Chunks[i].VirtualAddress, Chunks[i].PhysicalAddress);
            Chunks[i].VirtualAddress = NULL;
        }
    }
}

static
PVOID
Rt25AllocateRing(
    _In_ PRT25_ADAPTER Adapter,
    _Out_ PNDIS_PHYSICAL_ADDRESS Physical)
{
    PVOID Ring = NULL;

    /* Shared memory is page aligned, which covers the 256-byte ring alignment. */
    NdisMAllocateSharedMemory(Adapter->MiniportHandle, RT25_RING_BYTES, FALSE, &Ring, Physical);
    if (Ring)
        NdisZeroMemory(Ring, RT25_RING_BYTES);
    return Ring;
}

NDIS_STATUS
Rt25AllocateDatapath(
    _In_ PRT25_ADAPTER Adapter)
{
    NET_BUFFER_LIST_POOL_PARAMETERS PoolParameters;
    PRT25_RX_BUFFER RxBuffer;
    PRT25_TX_BUFFER TxBuffer;
    PRT25_CHUNK Chunk;
    ULONG i, Offset;

    /* Buffers never straddle a chunk, so each one is physically contiguous. */
    Adapter->ReceiveSize = Rt25ReceiveSize(Adapter->MaxFrame);
    Adapter->BufferSize = Rt25BufferSize(Adapter->MaxFrame);
    Adapter->BuffersPerChunk = RT25_CHUNK_SIZE / Adapter->BufferSize;
    Adapter->ChunkCount = (RT25_RING_SIZE + Adapter->BuffersPerChunk - 1) / Adapter->BuffersPerChunk;

    Adapter->RxRing = Rt25AllocateRing(Adapter, &Adapter->RxRingPhysical);
    Adapter->TxRing = Rt25AllocateRing(Adapter, &Adapter->TxRingPhysical);
    if (!Adapter->RxRing || !Adapter->TxRing)
        return NDIS_STATUS_RESOURCES;

    NdisMAllocateSharedMemory(Adapter->MiniportHandle, RT25_TALLY_BYTES, FALSE,
                              (PVOID *)&Adapter->TallyVirtual, &Adapter->TallyPhysical);
    if (!Adapter->TallyVirtual)
        return NDIS_STATUS_RESOURCES;
    NdisZeroMemory(Adapter->TallyVirtual, RT25_TALLY_BYTES);

    if (Rt25AllocateChunks(Adapter, Adapter->RxChunks) != NDIS_STATUS_SUCCESS ||
        Rt25AllocateChunks(Adapter, Adapter->TxChunks) != NDIS_STATUS_SUCCESS)
    {
        return NDIS_STATUS_RESOURCES;
    }

    NdisZeroMemory(&PoolParameters, sizeof(PoolParameters));
    PoolParameters.Header.Type = NDIS_OBJECT_TYPE_DEFAULT;
    PoolParameters.Header.Revision = NET_BUFFER_LIST_POOL_PARAMETERS_REVISION_1;
    PoolParameters.Header.Size = NDIS_SIZEOF_NET_BUFFER_LIST_POOL_PARAMETERS_REVISION_1;
    PoolParameters.ProtocolId = NDIS_PROTOCOL_ID_DEFAULT;
    PoolParameters.fAllocateNetBuffer = TRUE;
    PoolParameters.ContextSize = 0;
    PoolParameters.PoolTag = RT25_TAG;
    PoolParameters.DataSize = 0;
    Adapter->RxNblPool = NdisAllocateNetBufferListPool(Adapter->MiniportHandle, &PoolParameters);
    if (!Adapter->RxNblPool)
        return NDIS_STATUS_RESOURCES;

    for (i = 0; i < RT25_RING_SIZE; i++)
    {
        Offset = (i % Adapter->BuffersPerChunk) * Adapter->BufferSize;

        Chunk = &Adapter->RxChunks[i / Adapter->BuffersPerChunk];
        RxBuffer = &Adapter->RxBuffers[i];
        RxBuffer->VirtualAddress = (PUCHAR)Chunk->VirtualAddress + Offset;
        RxBuffer->PhysicalAddress = (ULONG64)Chunk->PhysicalAddress.QuadPart + Offset;
        RxBuffer->Mdl = NdisAllocateMdl(Adapter->MiniportHandle, RxBuffer->VirtualAddress, Adapter->BufferSize);
        if (!RxBuffer->Mdl)
            return NDIS_STATUS_RESOURCES;

        RxBuffer->NetBufferList = NdisAllocateNetBufferAndNetBufferList(Adapter->RxNblPool, 0, 0,
                                                                         RxBuffer->Mdl, 0, 0);
        if (!RxBuffer->NetBufferList)
            return NDIS_STATUS_RESOURCES;

        Chunk = &Adapter->TxChunks[i / Adapter->BuffersPerChunk];
        TxBuffer = &Adapter->TxBuffers[i];
        TxBuffer->VirtualAddress = (PUCHAR)Chunk->VirtualAddress + Offset;
        TxBuffer->PhysicalAddress = (ULONG64)Chunk->PhysicalAddress.QuadPart + Offset;
    }

    Rt25ResetRings(Adapter);
    Adapter->DatapathReady = TRUE;
    return NDIS_STATUS_SUCCESS;
}

VOID
Rt25FreeDatapath(
    _In_ PRT25_ADAPTER Adapter)
{
    ULONG i;

    Adapter->DatapathReady = FALSE;

    for (i = 0; i < RT25_RING_SIZE; i++)
    {
        if (Adapter->RxBuffers[i].NetBufferList)
        {
            NdisFreeNetBufferList(Adapter->RxBuffers[i].NetBufferList);
            Adapter->RxBuffers[i].NetBufferList = NULL;
        }

        if (Adapter->RxBuffers[i].Mdl)
        {
            NdisFreeMdl(Adapter->RxBuffers[i].Mdl);
            Adapter->RxBuffers[i].Mdl = NULL;
        }
    }

    if (Adapter->RxNblPool)
    {
        NdisFreeNetBufferListPool(Adapter->RxNblPool);
        Adapter->RxNblPool = NULL;
    }

    Rt25FreeChunks(Adapter, Adapter->RxChunks);
    Rt25FreeChunks(Adapter, Adapter->TxChunks);

    if (Adapter->TallyVirtual)
    {
        NdisMFreeSharedMemory(Adapter->MiniportHandle, RT25_TALLY_BYTES, FALSE,
                              Adapter->TallyVirtual, Adapter->TallyPhysical);
        Adapter->TallyVirtual = NULL;
    }

    if (Adapter->RxRing)
    {
        NdisMFreeSharedMemory(Adapter->MiniportHandle, RT25_RING_BYTES, FALSE,
                              Adapter->RxRing, Adapter->RxRingPhysical);
        Adapter->RxRing = NULL;
    }

    if (Adapter->TxRing)
    {
        NdisMFreeSharedMemory(Adapter->MiniportHandle, RT25_RING_BYTES, FALSE,
                              Adapter->TxRing, Adapter->TxRingPhysical);
        Adapter->TxRing = NULL;
    }
}

VOID
Rt25ResetRings(
    _In_ PRT25_ADAPTER Adapter)
{
    ULONG i;

    for (i = 0; i < RT25_RING_SIZE; i++)
    {
        Adapter->RxBuffers[i].Indicated = FALSE;
        Rt25ArmReceive(Adapter, i);

        Adapter->TxBuffers[i].Length = 0;
        Rt25SetDescriptorAddress(&Adapter->TxRing[i], Adapter->TxBuffers[i].PhysicalAddress);
        Adapter->TxRing[i].Vlan = 0;
        Adapter->TxRing[i].Flags = (i == RT25_RING_SIZE - 1) ? RT25_DESC_EOR : 0;
    }

    KeMemoryBarrier();
    Adapter->RxNext = 0;
    Adapter->TxHead = 0;
    Adapter->TxTail = 0;
    Adapter->TxFree = RT25_RING_SIZE;
    Adapter->RxOutstanding = 0;
}

BOOLEAN
Rt25ReceivePending(
    _In_ PRT25_ADAPTER Adapter)
{
    BOOLEAN Pending;
    ULONG Index;

    if (!Adapter->DatapathReady)
        return FALSE;

    NdisAcquireSpinLock(&Adapter->RxLock);
    Index = Adapter->RxNext;
    KeMemoryBarrier();
    Pending = !Adapter->RxBuffers[Index].Indicated && !(Adapter->RxRing[Index].Flags & RT25_DESC_OWN);
    NdisReleaseSpinLock(&Adapter->RxLock);
    return Pending;
}

VOID
Rt25PollReceive(
    _In_ PRT25_ADAPTER Adapter,
    _In_ ULONG Budget,
    _In_ BOOLEAN DispatchLevel)
{
    PNET_BUFFER_LIST Head = NULL, Tail = NULL, Nbl;
    PRT25_RX_BUFFER RxBuffer;
    PNET_BUFFER NetBuffer;
    ULONG Index, Flags, Length, Count = 0;
    BOOLEAN Indicate;

    if (!Adapter->DatapathReady)
        return;

    /* Rt25Pause reads RxOutstanding under RxLock, so the state is sampled under it too. */
    NdisAcquireSpinLock(&Adapter->RxLock);
    Indicate = Adapter->State == Rt25StateRunning;

    while (Budget-- > 0)
    {
        Index = Adapter->RxNext;
        RxBuffer = &Adapter->RxBuffers[Index];

        /* NDIS still owns this buffer; the ring resumes when it comes back. */
        if (RxBuffer->Indicated)
            break;

        KeMemoryBarrier();
        Flags = Adapter->RxRing[Index].Flags;
        if (Flags & RT25_DESC_OWN)
            break;

        KeMemoryBarrier();
        Adapter->RxNext = Rt25NextIndex(Index);
        Length = Rt25RxFrameLength(Flags, Adapter->ReceiveSize);
        if (Length == 0 || !Indicate)
        {
            if (Length == 0)
                Adapter->RxErrors++;
            Rt25ArmReceive(Adapter, Index);
            continue;
        }

        Nbl = RxBuffer->NetBufferList;
        NetBuffer = NET_BUFFER_LIST_FIRST_NB(Nbl);
        NET_BUFFER_CURRENT_MDL(NetBuffer) = RxBuffer->Mdl;
        NET_BUFFER_CURRENT_MDL_OFFSET(NetBuffer) = 0;
        NET_BUFFER_DATA_OFFSET(NetBuffer) = 0;
        NET_BUFFER_DATA_LENGTH(NetBuffer) = Length;
        NET_BUFFER_LIST_STATUS(Nbl) = NDIS_STATUS_SUCCESS;
        NET_BUFFER_LIST_INFO(Nbl, TcpIpChecksumNetBufferListInfo) =
            Rt25RxChecksumInfo(Adapter, Flags, RxBuffer->VirtualAddress, Length);
        NET_BUFFER_LIST_MINIPORT_RESERVED(Nbl)[0] = (PVOID)(ULONG_PTR)Index;
        NET_BUFFER_LIST_NEXT_NBL(Nbl) = NULL;
        Nbl->SourceHandle = Adapter->MiniportHandle;
        RxBuffer->Indicated = TRUE;

        if (Tail)
            NET_BUFFER_LIST_NEXT_NBL(Tail) = Nbl;
        else
            Head = Nbl;
        Tail = Nbl;
        Count++;
        Adapter->RxPackets++;
        Adapter->RxBytes += Length;
        switch (Rt25CastType(RxBuffer->VirtualAddress))
        {
            case RT25_CAST_MULTICAST:
                Adapter->RxMulticast++;
                break;

            case RT25_CAST_BROADCAST:
                Adapter->RxBroadcast++;
                break;
        }
    }

    if (Count)
        InterlockedExchangeAdd(&Adapter->RxOutstanding, (LONG)Count);

    NdisReleaseSpinLock(&Adapter->RxLock);

    if (Head)
    {
        NdisMIndicateReceiveNetBufferLists(Adapter->MiniportHandle, Head, NDIS_DEFAULT_PORT_NUMBER, Count,
                                           DispatchLevel ? NDIS_RECEIVE_FLAGS_DISPATCH_LEVEL : 0);
    }
}

VOID
NTAPI
Rt25ReturnNetBufferLists(
    _In_ NDIS_HANDLE MiniportAdapterContext,
    _In_ PNET_BUFFER_LIST NetBufferLists,
    _In_ ULONG ReturnFlags)
{
    PRT25_ADAPTER Adapter = MiniportAdapterContext;
    PNET_BUFFER_LIST Nbl, Next;
    ULONG Index;
    LONG Returned = 0;

    UNREFERENCED_PARAMETER(ReturnFlags);

    NdisAcquireSpinLock(&Adapter->RxLock);
    for (Nbl = NetBufferLists; Nbl; Nbl = Next)
    {
        Next = NET_BUFFER_LIST_NEXT_NBL(Nbl);
        NET_BUFFER_LIST_NEXT_NBL(Nbl) = NULL;
        Index = (ULONG)(ULONG_PTR)NET_BUFFER_LIST_MINIPORT_RESERVED(Nbl)[0];
        if (Index >= RT25_RING_SIZE || Adapter->RxBuffers[Index].NetBufferList != Nbl ||
            !Adapter->RxBuffers[Index].Indicated)
        {
            DPRINT1("RTL8125: stray receive NBL %p returned\n", Nbl);
            continue;
        }

        Adapter->RxBuffers[Index].Indicated = FALSE;
        Rt25ArmReceive(Adapter, Index);
        Returned++;
    }
    NdisReleaseSpinLock(&Adapter->RxLock);

    if (Returned && InterlockedExchangeAdd(&Adapter->RxOutstanding, -Returned) == Returned &&
        InterlockedCompareExchange(&Adapter->State, Rt25StatePaused, Rt25StatePausing) == Rt25StatePausing)
    {
        NdisMPauseComplete(Adapter->MiniportHandle);
    }
}

VOID
Rt25ReclaimTransmit(
    _In_ PRT25_ADAPTER Adapter)
{
    ULONG Index, Flags;

    if (!Adapter->DatapathReady)
        return;

    NdisAcquireSpinLock(&Adapter->TxLock);
    while (Adapter->TxFree < RT25_RING_SIZE)
    {
        Index = Adapter->TxTail;
        KeMemoryBarrier();
        Flags = Adapter->TxRing[Index].Flags;
        if (Flags & RT25_DESC_OWN)
            break;

        Adapter->TxPackets++;
        Adapter->TxBytes += Adapter->TxBuffers[Index].Length;
        if (Adapter->TxBuffers[Index].Cast == RT25_CAST_MULTICAST)
            Adapter->TxMulticast++;
        else if (Adapter->TxBuffers[Index].Cast == RT25_CAST_BROADCAST)
            Adapter->TxBroadcast++;
        Adapter->TxBuffers[Index].Length = 0;
        Adapter->TxTail = Rt25NextIndex(Index);
        Adapter->TxFree++;
    }
    NdisReleaseSpinLock(&Adapter->TxLock);
}

static
NDIS_STATUS
Rt25CopyNetBuffer(
    _In_ PNET_BUFFER NetBuffer,
    _Out_writes_bytes_(Capacity) PUCHAR Destination,
    _In_ ULONG Capacity,
    _Out_ PULONG Copied)
{
    ULONG Remaining = NET_BUFFER_DATA_LENGTH(NetBuffer);
    ULONG Offset = NET_BUFFER_CURRENT_MDL_OFFSET(NetBuffer);
    PMDL Mdl = NET_BUFFER_CURRENT_MDL(NetBuffer);
    ULONG MdlLength, Chunk;
    PUCHAR Source;

    *Copied = 0;
    if (Remaining > Capacity)
        return NDIS_STATUS_INVALID_LENGTH;

    while (Remaining)
    {
        if (!Mdl)
            return NDIS_STATUS_INVALID_PACKET;

        MdlLength = MmGetMdlByteCount(Mdl);
        if (Offset >= MdlLength)
        {
            Offset -= MdlLength;
            Mdl = Mdl->Next;
            continue;
        }

        Source = MmGetSystemAddressForMdlSafe(Mdl, NormalPagePriority);
        if (!Source)
            return NDIS_STATUS_RESOURCES;

        Chunk = min(MdlLength - Offset, Remaining);
        NdisMoveMemory(Destination + *Copied, Source + Offset, Chunk);
        *Copied += Chunk;
        Remaining -= Chunk;
        Offset = 0;
        Mdl = Mdl->Next;
    }

    return NDIS_STATUS_SUCCESS;
}

static
ULONG
Rt25TxChecksumRequest(
    _In_ PNET_BUFFER_LIST Nbl)
{
    NDIS_TCP_IP_CHECKSUM_NET_BUFFER_LIST_INFO Info;
    ULONG Requested = 0;

    Info.Value = NET_BUFFER_LIST_INFO(Nbl, TcpIpChecksumNetBufferListInfo);
    if (!Info.Value)
        return 0;

    if (Info.Transmit.IsIPv4 && Info.Transmit.IpHeaderChecksum)
        Requested |= RT25_CSUM_IP;
    if ((Info.Transmit.IsIPv4 || Info.Transmit.IsIPv6) && Info.Transmit.TcpChecksum)
        Requested |= RT25_CSUM_TCP;
    if ((Info.Transmit.IsIPv4 || Info.Transmit.IsIPv6) && Info.Transmit.UdpChecksum)
        Requested |= RT25_CSUM_UDP;
    return Requested;
}

static
ULONG
Rt25TxChecksumOptions(
    _Inout_updates_bytes_(Length) PUCHAR Frame,
    _In_ ULONG Length,
    _In_ ULONG Requested)
{
    RT25_FRAME_INFO Info;
    ULONG Options;

    if (!Requested)
        return 0;

    Rt25ParseFrame(Frame, Length, &Info);
    Options = Rt25TxChecksumFlags(&Info, Requested);

    /* The controller would include the padding of a short frame, so those are summed here. */
    if (Options && Length < RT25_MIN_FRAME)
    {
        Rt25SoftwareChecksum(Frame, &Info, Options);
        Options = 0;
    }

    return Options;
}

static
VOID
Rt25AppendNbl(
    _Inout_ PNET_BUFFER_LIST *Head,
    _Inout_ PNET_BUFFER_LIST *Tail,
    _In_ PNET_BUFFER_LIST Nbl)
{
    NET_BUFFER_LIST_NEXT_NBL(Nbl) = NULL;
    if (*Tail)
        NET_BUFFER_LIST_NEXT_NBL(*Tail) = Nbl;
    else
        *Head = Nbl;
    *Tail = Nbl;
}

VOID
NTAPI
Rt25SendNetBufferLists(
    _In_ NDIS_HANDLE MiniportAdapterContext,
    _In_ PNET_BUFFER_LIST NetBufferLists,
    _In_ NDIS_PORT_NUMBER PortNumber,
    _In_ ULONG SendFlags)
{
    PRT25_ADAPTER Adapter = MiniportAdapterContext;
    PNET_BUFFER_LIST Nbl, Next, Head = NULL, Tail = NULL;
    PNET_BUFFER NetBuffer;
    NDIS_STATUS Status;
    ULONG Count, Index, Length, Padded, Requested;
    BOOLEAN Kick = FALSE;

    UNREFERENCED_PARAMETER(PortNumber);

    Rt25ReclaimTransmit(Adapter);

    for (Nbl = NetBufferLists; Nbl; Nbl = Next)
    {
        Next = NET_BUFFER_LIST_NEXT_NBL(Nbl);
        Status = NDIS_STATUS_SUCCESS;

        if (Adapter->State != Rt25StateRunning)
            Status = NDIS_STATUS_PAUSED;
        else if (Adapter->MediaConnectState != MediaConnectStateConnected)
            Status = NDIS_STATUS_MEDIA_DISCONNECTED;

        Count = 0;
        for (NetBuffer = NET_BUFFER_LIST_FIRST_NB(Nbl);
             Status == NDIS_STATUS_SUCCESS && NetBuffer;
             NetBuffer = NET_BUFFER_NEXT_NB(NetBuffer))
        {
            Length = NET_BUFFER_DATA_LENGTH(NetBuffer);
            if (Length < RT25_ETH_HEADER || Length > Adapter->MaxFrame)
                Status = NDIS_STATUS_INVALID_LENGTH;
            Count++;
        }

        if (Status == NDIS_STATUS_SUCCESS && Count == 0)
            Status = NDIS_STATUS_INVALID_PACKET;

        if (Status == NDIS_STATUS_SUCCESS)
        {
            NdisAcquireSpinLock(&Adapter->TxLock);
            if (Adapter->TxFree < Count)
            {
                Status = NDIS_STATUS_RESOURCES;
            }
            else
            {
                Requested = Rt25TxChecksumRequest(Nbl);
                Index = Adapter->TxHead;
                for (NetBuffer = NET_BUFFER_LIST_FIRST_NB(Nbl);
                     NetBuffer;
                     NetBuffer = NET_BUFFER_NEXT_NB(NetBuffer), Index = Rt25NextIndex(Index))
                {
                    Status = Rt25CopyNetBuffer(NetBuffer, Adapter->TxBuffers[Index].VirtualAddress,
                                               Adapter->BufferSize, &Length);
                    if (Status != NDIS_STATUS_SUCCESS)
                        break;

                    Adapter->TxBuffers[Index].Cast = Rt25CastType(Adapter->TxBuffers[Index].VirtualAddress);
                    Adapter->TxBuffers[Index].Options =
                        Rt25TxChecksumOptions(Adapter->TxBuffers[Index].VirtualAddress, Length, Requested);
                    Padded = Rt25TxPaddedLength(Length);
                    if (Padded > Length)
                        NdisZeroMemory(Adapter->TxBuffers[Index].VirtualAddress + Length, Padded - Length);
                    Adapter->TxBuffers[Index].Length = Padded;
                }

                if (Status == NDIS_STATUS_SUCCESS)
                {
                    Index = Adapter->TxHead;
                    for (; Count > 0; Count--, Index = Rt25NextIndex(Index))
                    {
                        Adapter->TxRing[Index].Vlan = Adapter->TxBuffers[Index].Options;
                        KeMemoryBarrier();
                        Adapter->TxRing[Index].Flags = Rt25TxFlags(Index, Adapter->TxBuffers[Index].Length);
                        Adapter->TxFree--;
                    }

                    Adapter->TxHead = Index;
                    Kick = TRUE;
                }
            }
            NdisReleaseSpinLock(&Adapter->TxLock);
        }

        NET_BUFFER_LIST_STATUS(Nbl) = Status;
        if (Status != NDIS_STATUS_SUCCESS)
            Adapter->TxErrors++;
        Rt25AppendNbl(&Head, &Tail, Nbl);
    }

    if (Kick)
    {
        KeMemoryBarrier();
        Rt25Write16(Adapter, RT25_REG_TPPOLL, RT25_TPPOLL_NPQ);
    }

    if (Head)
    {
        NdisMSendNetBufferListsComplete(Adapter->MiniportHandle, Head,
                                        NDIS_TEST_SEND_AT_DISPATCH_LEVEL(SendFlags) ?
                                        NDIS_SEND_COMPLETE_FLAGS_DISPATCH_LEVEL : 0);
    }
}

VOID
NTAPI
Rt25CancelSend(
    _In_ NDIS_HANDLE MiniportAdapterContext,
    _In_ PVOID CancelId)
{
    UNREFERENCED_PARAMETER(MiniportAdapterContext);
    UNREFERENCED_PARAMETER(CancelId);
}
