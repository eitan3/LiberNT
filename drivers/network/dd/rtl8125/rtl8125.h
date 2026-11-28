/*
 * PROJECT:     LiberNT Realtek RTL8125/RTL8126 Ethernet Driver
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Adapter state and internal interfaces of the NDIS 6.20 miniport
 * COPYRIGHT:   Copyright 2026 The LiberNT contributors
 */

#pragma once

#include <ntddk.h>
#include <ndis.h>
#include <ifdef.h>
#include <ipifcons.h>

#define NDEBUG
#include <libernt/debug.h>

#include "rtl8125hw.h"

#ifndef ETH_LENGTH_OF_ADDRESS
#define ETH_LENGTH_OF_ADDRESS 6
#endif

#define RT25_TAG                    '5218'
#define RT25_DRIVER_VERSION         0x0100
#define RT25_MAX_MULTICAST          32
#define RT25_CHUNK_SIZE             0x10000
#define RT25_BUFFERS_PER_CHUNK      (RT25_CHUNK_SIZE / RT25_BUFFER_SIZE)
#define RT25_CHUNKS                 (RT25_RING_SIZE / RT25_BUFFERS_PER_CHUNK)
#define RT25_RING_BYTES             (RT25_RING_SIZE * sizeof(RT25_DESCRIPTOR))

#define RT25_MAC_OPTIONS            (NDIS_MAC_OPTION_TRANSFERS_NOT_PEND | \
                                     NDIS_MAC_OPTION_COPY_LOOKAHEAD_DATA | \
                                     NDIS_MAC_OPTION_NO_LOOPBACK)
#define RT25_SUPPORTED_FILTERS      (NDIS_PACKET_TYPE_DIRECTED | \
                                     NDIS_PACKET_TYPE_MULTICAST | \
                                     NDIS_PACKET_TYPE_ALL_MULTICAST | \
                                     NDIS_PACKET_TYPE_BROADCAST | \
                                     NDIS_PACKET_TYPE_PROMISCUOUS)

#define RT25_OFFLOAD_TX_IP4         0x0001
#define RT25_OFFLOAD_TX_TCP4        0x0002
#define RT25_OFFLOAD_TX_UDP4        0x0004
#define RT25_OFFLOAD_TX_TCP6        0x0008
#define RT25_OFFLOAD_TX_UDP6        0x0010
#define RT25_OFFLOAD_RX_IP4         0x0100
#define RT25_OFFLOAD_RX_TCP4        0x0200
#define RT25_OFFLOAD_RX_UDP4        0x0400
#define RT25_OFFLOAD_RX_TCP6        0x0800
#define RT25_OFFLOAD_RX_UDP6        0x1000
#define RT25_OFFLOAD_RX_ANY         0x1F00
#define RT25_OFFLOAD_ALL            0x1F1F

C_ASSERT(RT25_RING_SIZE % RT25_BUFFERS_PER_CHUNK == 0);

typedef enum _RT25_STATE
{
    Rt25StatePaused,
    Rt25StatePausing,
    Rt25StateRunning
} RT25_STATE;

typedef struct _RT25_CHUNK
{
    PVOID VirtualAddress;
    NDIS_PHYSICAL_ADDRESS PhysicalAddress;
} RT25_CHUNK, *PRT25_CHUNK;

typedef struct _RT25_RX_BUFFER
{
    PUCHAR VirtualAddress;
    ULONG64 PhysicalAddress;
    PMDL Mdl;
    PNET_BUFFER_LIST NetBufferList;
    BOOLEAN Indicated;
} RT25_RX_BUFFER, *PRT25_RX_BUFFER;

typedef struct _RT25_TX_BUFFER
{
    PUCHAR VirtualAddress;
    ULONG64 PhysicalAddress;
    ULONG Length;
    ULONG Options;
} RT25_TX_BUFFER, *PRT25_TX_BUFFER;

typedef struct _RT25_ADAPTER
{
    NDIS_HANDLE MiniportHandle;
    NDIS_HANDLE DmaHandle;
    NDIS_HANDLE InterruptHandle;
    NDIS_HANDLE RxNblPool;

    PUCHAR RegisterBase;
    NDIS_PHYSICAL_ADDRESS RegisterPhysical;
    ULONG RegisterLength;
    BOOLEAN HasInterrupt;
    BOOLEAN MessageInterrupt;

    USHORT DeviceId;
    USHORT ChipId;
    UCHAR ChipFlags;
    ULONG MaxSpeedMbps;
    const char *ChipName;

    NDIS_SPIN_LOCK StateLock;
    NDIS_SPIN_LOCK PhyLock;
    NDIS_SPIN_LOCK RxLock;
    NDIS_SPIN_LOCK TxLock;

    PRT25_DESCRIPTOR RxRing;
    NDIS_PHYSICAL_ADDRESS RxRingPhysical;
    PRT25_DESCRIPTOR TxRing;
    NDIS_PHYSICAL_ADDRESS TxRingPhysical;
    RT25_CHUNK RxChunks[RT25_CHUNKS];
    RT25_CHUNK TxChunks[RT25_CHUNKS];
    RT25_RX_BUFFER RxBuffers[RT25_RING_SIZE];
    RT25_TX_BUFFER TxBuffers[RT25_RING_SIZE];
    ULONG RxNext;
    ULONG TxHead;
    ULONG TxTail;
    ULONG TxFree;
    BOOLEAN DatapathReady;

    volatile LONG InterruptPending;
    volatile LONG RxOutstanding;
    volatile LONG State;
    BOOLEAN HardwareRunning;
    BOOLEAN SurpriseRemoved;

    ULONG Offload;
    BOOLEAN EncapsulationIPv4;
    BOOLEAN EncapsulationIPv6;

    NDIS_DEVICE_POWER_STATE PowerState;
    ULONG EnabledWakePatterns;
    ULONG WakeUpFlags;
    ULONG MagicPatternId;
    BOOLEAN MagicPatternAdded;

    ULONG PacketFilter;
    ULONG Lookahead;
    ULONG MulticastCount;
    UCHAR MulticastList[RT25_MAX_MULTICAST][ETH_LENGTH_OF_ADDRESS];
    UCHAR PermanentMacAddress[ETH_LENGTH_OF_ADDRESS];
    UCHAR CurrentMacAddress[ETH_LENGTH_OF_ADDRESS];

    NDIS_MEDIA_CONNECT_STATE MediaConnectState;
    NDIS_MEDIA_DUPLEX_STATE MediaDuplexState;
    ULONG64 LinkSpeed;
    ULONG LinkSequence;

    ULONG64 TxPackets;
    ULONG64 TxBytes;
    ULONG64 TxErrors;
    ULONG64 RxPackets;
    ULONG64 RxBytes;
    ULONG64 RxErrors;
    ULONG64 RxNoBuffer;
} RT25_ADAPTER, *PRT25_ADAPTER;

FORCEINLINE
UCHAR
Rt25Read8(
    _In_ PRT25_ADAPTER Adapter,
    _In_ ULONG Register)
{
    return READ_REGISTER_UCHAR(Adapter->RegisterBase + Register);
}

FORCEINLINE
USHORT
Rt25Read16(
    _In_ PRT25_ADAPTER Adapter,
    _In_ ULONG Register)
{
    return READ_REGISTER_USHORT((PUSHORT)(Adapter->RegisterBase + Register));
}

FORCEINLINE
ULONG
Rt25Read32(
    _In_ PRT25_ADAPTER Adapter,
    _In_ ULONG Register)
{
    return READ_REGISTER_ULONG((PULONG)(Adapter->RegisterBase + Register));
}

FORCEINLINE
VOID
Rt25Write8(
    _In_ PRT25_ADAPTER Adapter,
    _In_ ULONG Register,
    _In_ UCHAR Value)
{
    WRITE_REGISTER_UCHAR(Adapter->RegisterBase + Register, Value);
}

FORCEINLINE
VOID
Rt25Write16(
    _In_ PRT25_ADAPTER Adapter,
    _In_ ULONG Register,
    _In_ USHORT Value)
{
    WRITE_REGISTER_USHORT((PUSHORT)(Adapter->RegisterBase + Register), Value);
}

FORCEINLINE
VOID
Rt25Write32(
    _In_ PRT25_ADAPTER Adapter,
    _In_ ULONG Register,
    _In_ ULONG Value)
{
    WRITE_REGISTER_ULONG((PULONG)(Adapter->RegisterBase + Register), Value);
}

NDIS_STATUS
Rt25IdentifyChip(
    _In_ PRT25_ADAPTER Adapter);

VOID
Rt25ReadMacAddress(
    _In_ PRT25_ADAPTER Adapter);

NDIS_STATUS
Rt25StartHardware(
    _In_ PRT25_ADAPTER Adapter);

VOID
Rt25StopHardware(
    _In_ PRT25_ADAPTER Adapter);

VOID
Rt25ApplyReceiveFilter(
    _In_ PRT25_ADAPTER Adapter);

BOOLEAN
Rt25RefreshLink(
    _In_ PRT25_ADAPTER Adapter);

VOID
Rt25FillLinkState(
    _In_ PRT25_ADAPTER Adapter,
    _Out_ PNDIS_LINK_STATE LinkState);

VOID
Rt25UpdateLinkState(
    _In_ PRT25_ADAPTER Adapter);

VOID
Rt25EnableInterrupts(
    _In_ PRT25_ADAPTER Adapter);

VOID
Rt25ResumeInterrupts(
    _In_ PRT25_ADAPTER Adapter);

VOID
Rt25QuiesceHardware(
    _In_ PRT25_ADAPTER Adapter);

VOID
Rt25ApplyChecksumConfig(
    _In_ PRT25_ADAPTER Adapter);

VOID
Rt25ArmWake(
    _In_ PRT25_ADAPTER Adapter,
    _In_ BOOLEAN Magic,
    _In_ BOOLEAN LinkChange);

VOID
Rt25DisarmWake(
    _In_ PRT25_ADAPTER Adapter);

VOID
Rt25DisableInterrupts(
    _In_ PRT25_ADAPTER Adapter);

NDIS_STATUS
Rt25AllocateDatapath(
    _In_ PRT25_ADAPTER Adapter);

VOID
Rt25FreeDatapath(
    _In_ PRT25_ADAPTER Adapter);

VOID
Rt25ResetRings(
    _In_ PRT25_ADAPTER Adapter);

BOOLEAN
Rt25ReceivePending(
    _In_ PRT25_ADAPTER Adapter);

VOID
Rt25PollReceive(
    _In_ PRT25_ADAPTER Adapter,
    _In_ ULONG Budget,
    _In_ BOOLEAN DispatchLevel);

VOID
Rt25ReclaimTransmit(
    _In_ PRT25_ADAPTER Adapter);

MINIPORT_SEND_NET_BUFFER_LISTS Rt25SendNetBufferLists;
MINIPORT_RETURN_NET_BUFFER_LISTS Rt25ReturnNetBufferLists;
MINIPORT_CANCEL_SEND Rt25CancelSend;

NDIS_STATUS
Rt25PrepareBus(
    _In_ PRT25_ADAPTER Adapter);

VOID
Rt25FillOffload(
    _In_ ULONG Mask,
    _Out_ PNDIS_OFFLOAD Offload);

VOID
Rt25IndicateOffloadConfig(
    _In_ PRT25_ADAPTER Adapter);

NDIS_STATUS
Rt25SetPower(
    _In_ PRT25_ADAPTER Adapter,
    _In_ NDIS_DEVICE_POWER_STATE PowerState);

VOID
Rt25FillPmCapabilities(
    _Out_ PNDIS_PM_CAPABILITIES Capabilities);

NDIS_STATUS
Rt25RegisterInterrupt(
    _In_ PRT25_ADAPTER Adapter);

VOID
Rt25DeregisterInterrupt(
    _In_ PRT25_ADAPTER Adapter);

MINIPORT_OID_REQUEST Rt25OidRequest;
MINIPORT_CANCEL_OID_REQUEST Rt25CancelOidRequest;

extern const NDIS_OID Rt25SupportedOids[];
extern const ULONG Rt25SupportedOidsSize;
