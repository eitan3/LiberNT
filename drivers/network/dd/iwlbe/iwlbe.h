/*
 * PROJECT:     LiberNT Intel BE200 Native 802.11 Miniport
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Driver definitions for the Intel Wi-Fi 7 BE200 PCIe adapter
 * COPYRIGHT:   Copyright 2026 The LiberNT contributors
 */

#pragma once

#include <ntddk.h>
#include <ndis.h>
#include <windot11.h>
#include <nwifi_sae.h>

#include "iwlhw.h"
#include "iwlfw.h"

#define IWL_TAG                         'lwIB'

#define IWL_FIRMWARE_PATH               L"\\SystemRoot\\System32\\drivers\\intel\\iwlwifi-gl-c0-fm-c0-c109.ucode"
#define IWL_FIRMWARE_MAX_SIZE           (8 * 1024 * 1024)

#define IWL_ADDRESS_LENGTH              6
#define IWL_MTU_SIZE                    1500
#define IWL_MAX_80211_FRAME             2346
#define IWL_MAX_BSS                     48
#define IWL_MAX_IE_BYTES                768
#define IWL_HELD_RX_MAX                 8
#define IWL_TX_QUEUE_DEPTH              32
#define IWL_RX_QUEUE_DEPTH              IWL_RX_POSTED
#define IWL_RX_BUFFER_SIZE              IWL_RB_SIZE
#define IWL_LINK_SPEED_BPS              54000000ULL
#define IWL_SCAN_DWELL_MS               120
#define IWL_SCAN_TIMEOUT_MS             10000
#define IWL_AUTH_TIMEOUT_MS             300
#define IWL_ASSOC_TIMEOUT_MS            300
#define IWL_MLME_RETRIES                3
#define IWL_ASSOC_COMEBACK_MAX          8
#define IWL_JOIN_ATTEMPTS               2
#define IWL_SA_QUERY_MAX_TIMEOUT_MS     1024
#define IWL_SAE_TIMEOUT_MS              500
#define IWL_SAE_PARAMS_TIMEOUT_MS       3000
#define IWL_SAE_ROUNDS                  8
#define IWL_BEACON_LOSS_MS              8000
#define IWL_SESSION_DURATION_TU         900

#define IWL_CMD_TIMEOUT_MS              2000
#define IWL_ALIVE_TIMEOUT_MS            2000
#define IWL_INIT_TIMEOUT_MS             2000
#define IWL_NOTIF_MAX                   256
#define IWL_CMD_BUFFERS                 2
#define IWL_CMD_BUFFER_SIZE             4096
#define IWL_RESP_MAX                    2048

#define IWL_RX_RING_SIZE                512
#define IWL_RX_POSTED                   504
#define IWL_RB_PER_CHUNK                16
#define IWL_RB_CHUNKS                   ((IWL_RX_POSTED + IWL_RB_PER_CHUNK - 1) / IWL_RB_PER_CHUNK)

#define IWL_TXQ_SIZE                    128
#define IWL_TXQ_BUFFERS                 32
#define IWL_TXQ_BUFFER_SIZE             2560
#define IWL_STA_ID_AP                   0
#define IWL_MAC_ID                      0
#define IWL_LINK_FW_ID                  0
#define IWL_PHY_ID                      0

#define DOT11_FC0_TYPE_MASK             0x0c
#define DOT11_FC0_TYPE_MGMT             0x00
#define DOT11_FC0_TYPE_CTRL             0x04
#define DOT11_FC0_TYPE_DATA             0x08
#define DOT11_FC0_SUBTYPE_MASK          0xf0
#define DOT11_SUBTYPE_ASSOC_REQ         0x00
#define DOT11_SUBTYPE_ASSOC_RESP        0x10
#define DOT11_SUBTYPE_PROBE_REQ         0x40
#define DOT11_SUBTYPE_PROBE_RESP        0x50
#define DOT11_SUBTYPE_BEACON            0x80
#define DOT11_SUBTYPE_DISASSOC          0xa0
#define DOT11_SUBTYPE_AUTH              0xb0
#define DOT11_SUBTYPE_DEAUTH            0xc0
#define DOT11_SUBTYPE_ACTION            0xd0
#define DOT11_FC1_TODS                  0x01
#define DOT11_FC1_FROMDS                0x02
#define DOT11_FC1_PROTECTED             0x40
#define DOT11_HEADER_LENGTH             24
#define DOT11_QOS_SUBTYPE               0x80
#define DOT11_FCS_LENGTH                4

#define DOT11_IE_SSID                   0
#define DOT11_IE_RATES                  1
#define DOT11_IE_DS_PARAMS              3
#define DOT11_IE_RSN                    48
#define DOT11_IE_TIMEOUT_INTERVAL       56
#define DOT11_IE_EXT_RATES              50
#define DOT11_IE_VENDOR                 221
#define IWL_RATE_MASK_CCK               0x00F
#define IWL_RATE_MASK_OFDM              0xFF0

typedef struct _IWL_DMA
{
    PVOID Va;
    NDIS_PHYSICAL_ADDRESS Pa;
    ULONG Length;
    BOOLEAN Cached;
} IWL_DMA, *PIWL_DMA;

typedef struct _IWL_TXQ
{
    IWL_DMA Tfds;
    IWL_DMA ByteCounts;
    IWL_DMA Buffers;
    USHORT Id;
    USHORT Size;
    USHORT Write;
    USHORT Read;
    BOOLEAN Ready;
} IWL_TXQ, *PIWL_TXQ;

typedef struct _IWL_BSS
{
    UCHAR Bssid[IWL_ADDRESS_LENGTH];
    UCHAR Ssid[DOT11_SSID_MAX_LENGTH];
    ULONG SsidLength;
    ULONG Channel;
    LONG Rssi;
    USHORT Capability;
    USHORT BeaconPeriod;
    ULONGLONG Timestamp;
    ULONGLONG LastSeen;
    ULONG IeLength;
    UCHAR Ies[IWL_MAX_IE_BYTES];
} IWL_BSS, *PIWL_BSS;

typedef enum _IWL_LINK_STATE
{
    IwlLinkIdle,
    IwlLinkAuthenticating,
    IwlLinkAssociating,
    IwlLinkAssociated
} IWL_LINK_STATE;

typedef enum _IWL_COMMAND
{
    IwlCommandNone,
    IwlCommandScan,
    IwlCommandConnect,
    IwlCommandDisconnect,
    IwlCommandReset
} IWL_COMMAND;

typedef struct _IWL_ADAPTER
{
    NDIS_HANDLE MiniportAdapterHandle;
    NDIS_HANDLE DmaHandle;
    NDIS_HANDLE InterruptHandle;
    PUCHAR Registers;
    NDIS_PHYSICAL_ADDRESS RegisterPhysical;
    ULONG RegisterLength;
    BOOLEAN HasInterrupt;
    ULONG HwRev;
    ULONG HwRfId;
    volatile BOOLEAN DeviceEnabled;
    volatile BOOLEAN FirmwareAlive;
    volatile BOOLEAN FirmwareDead;
    volatile LONG PendingCauses;
    ULONG IntMask;

    IWL_FIRMWARE Fw;
    IWL_DMA FwDram[IWL_FW_MAX_SECTIONS * 3];
    ULONG FwDramCount;
    IWL_DMA ImlDma;
    IWL_DMA ContextInfo;
    IWL_DMA PrphInfo;
    IWL_DMA PrphScratch;
    IWL_DMA PnvmDesc;
    IWL_DMA PnvmDram[IWL_PNVM_MAX_CHUNKS];
    ULONG PnvmDramCount;
    ULONG SkuId[3];

    IWL_DMA RxFreeBd;
    IWL_DMA RxUsedBd;
    IWL_DMA RxStatus;
    IWL_DMA RxChunks[IWL_RB_CHUNKS];
    PUCHAR RbVa[IWL_RX_POSTED];
    ULONGLONG RbPa[IWL_RX_POSTED];
    ULONG RxRead;
    ULONG RxWrite;
    BOOLEAN RxSkipFragment;

    IWL_TXQ CmdQueue;
    IWL_DMA CmdBuffers;
    KMUTEX CommandLock;
    KEVENT CommandDone;
    volatile LONG CommandWaiting;
    USHORT CommandSequence;
    PVOID CommandResponse;
    ULONG CommandResponseLength;
    ULONG CommandReturned;

    KEVENT NotifEvent;
    volatile LONG NotifArmed;
    UCHAR NotifGroup;
    UCHAR NotifCommand;
    ULONG NotifLength;
    UCHAR NotifData[IWL_NOTIF_MAX];

    IWL_TXQ MgmtQueue;
    IWL_TXQ DataQueue;
    NDIS_SPIN_LOCK TxLock;
    USHORT TxSequence;
    BOOLEAN StationAdded;
    BOOLEAN LinkActive;
    BOOLEAN MacAdded;
    BOOLEAN PhyAdded;
    UCHAR ValidTxChains;
    UCHAR ValidRxChains;
    UCHAR Channels2G[14];
    ULONG Channels2GCount;
    UCHAR Channels5G[40];
    ULONG Channels5GCount;
    ULONG ScanUid;
    ULONG RxChannel;

    PKTHREAD WorkerThread;
    KEVENT WorkerEvent;
    KEVENT WorkerStopped;
    volatile BOOLEAN WorkerStop;
    BOOLEAN AllBeacons;

    UCHAR CurrentChannel;
    UCHAR PermanentAddress[IWL_ADDRESS_LENGTH];
    UCHAR CurrentAddress[IWL_ADDRESS_LENGTH];
    UCHAR Bssid[IWL_ADDRESS_LENGTH];
    USHORT Aid;
    USHORT BeaconInterval;

    NDIS_SPIN_LOCK Lock;
    NDIS_SPIN_LOCK TxQueueLock;
    BOOLEAN HardwareReady;
    PKTHREAD CommandThread;
    KEVENT CommandEvent;
    KEVENT CommandStopped;
    volatile BOOLEAN CommandStop;
    BOOLEAN ScanQueued;
    BOOLEAN ConnectQueued;
    BOOLEAN DisconnectQueued;
    DOT11_SSID ScanSsid;

    IWL_BSS Bss[IWL_MAX_BSS];
    ULONG BssCount;
    BOOLEAN Scanning;

    ULONG OperationMode;
    DOT11_PHY_TYPE CurrentPhyType;
    ULONG PacketFilter;
    BOOLEAN RadioOn;
    BOOLEAN AutoConfigEnabled;
    ULONG RtsThreshold;
    ULONG FragmentationThreshold;
    DOT11_SSID DesiredSsid;
    BOOLEAN HaveDesiredSsid;
    UCHAR DesiredBssid[IWL_ADDRESS_LENGTH];
    BOOLEAN HaveDesiredBssid;
    DOT11_BSS_TYPE DesiredBssType;
    DOT11_AUTH_ALGORITHM AuthAlgorithm;
    DOT11_CIPHER_ALGORITHM UnicastCipher;
    DOT11_CIPHER_ALGORITHM MulticastCipher;

    IWL_LINK_STATE LinkState;
    UCHAR ConnectedBssid[IWL_ADDRESS_LENGTH];
    ULONG ConnectedChannel;
    USHORT AssociationId;
    LONG ConnectedRssi;
    ULONG TxRateMask;
    ULONGLONG LastBeaconTime;
    KEVENT MlmeEvent;
    KEVENT SaeEvent;
    BOOLEAN SaeParamsValid;
    NWIFI_SAE_PARAMS SaeParams;
    USHORT SaeSequence;
    BOOLEAN SaeH2e;
    ULONG SaeFrameLength;
    UCHAR SaeFrame[NWIFI_SAE_MAX_FRAME];
    UCHAR MlmeExpect;
    USHORT MlmeStatus;
    USHORT MlmeAid;
    ULONG AssocRequestIeLength;
    UCHAR AssocRequestIes[IWL_MAX_IE_BYTES];
    ULONG AssocResponseIeLength;
    UCHAR AssocResponseIes[IWL_MAX_IE_BYTES];
    BOOLEAN PmfActive;
    BOOLEAN SoftwareCrypto;
    BOOLEAN MgmtKeyValid;
    ULONGLONG DataRxPn;
    BOOLEAN GroupKeyValid[4];
    UCHAR GroupKey[4][16];
    ULONGLONG GroupRxPn[4];
    UCHAR MgmtTk[16];
    ULONGLONG MgmtTxPn;
    ULONGLONG MgmtRxPn;
    BOOLEAN SaQueryPending;
    USHORT SaQueryId;
    ULONGLONG SaQueryDeadline;
    BOOLEAN IgtkValid;
    USHORT IgtkKeyId;
    UCHAR Igtk[16];
    ULONGLONG IgtkRxIpn;

    NDIS_HANDLE RxNblPool;
    PNET_BUFFER_LIST TxQueueHead;
    PNET_BUFFER_LIST TxQueueTail;
    volatile BOOLEAN Halting;
    volatile BOOLEAN DataPathRunning;
    volatile LONG RxIndicated;
    volatile LONG TxPending;
    BOOLEAN HoldRx;
    PNET_BUFFER_LIST HeldRxHead;
    PNET_BUFFER_LIST HeldRxTail;
    ULONG HeldRxCount;
    UCHAR TxFrame[IWL_MAX_80211_FRAME + 32];
    UCHAR RxFrame[IWL_RB_SIZE];

    ULONG64 TxOk;
    ULONG64 TxError;
    ULONG64 TxBytes;
    ULONG64 RxOk;
    ULONG64 RxError;
    ULONG64 RxBytes;
} IWL_ADAPTER, *PIWL_ADAPTER;

extern NDIS_OID IwlSupportedOids[];
extern ULONG IwlSupportedOidCount;

FORCEINLINE
ULONG
IwlLe32(
    _In_reads_bytes_(4) const UCHAR *Data)
{
    return Data[0] | ((ULONG)Data[1] << 8) | ((ULONG)Data[2] << 16) | ((ULONG)Data[3] << 24);
}

FORCEINLINE
USHORT
IwlLe16(
    _In_reads_bytes_(2) const UCHAR *Data)
{
    return (USHORT)(Data[0] | (Data[1] << 8));
}

FORCEINLINE
VOID
IwlPutLe16(
    _Out_writes_bytes_(2) PUCHAR Data,
    _In_ ULONG Value)
{
    Data[0] = (UCHAR)Value;
    Data[1] = (UCHAR)(Value >> 8);
}

FORCEINLINE
VOID
IwlPutLe32(
    _Out_writes_bytes_(4) PUCHAR Data,
    _In_ ULONG Value)
{
    Data[0] = (UCHAR)Value;
    Data[1] = (UCHAR)(Value >> 8);
    Data[2] = (UCHAR)(Value >> 16);
    Data[3] = (UCHAR)(Value >> 24);
}

FORCEINLINE
VOID
IwlPutLe64(
    _Out_writes_bytes_(8) PUCHAR Data,
    _In_ ULONGLONG Value)
{
    IwlPutLe32(Data, (ULONG)Value);
    IwlPutLe32(Data + 4, (ULONG)(Value >> 32));
}

FORCEINLINE
ULONG
IwlRead32(
    _In_ PIWL_ADAPTER Adapter,
    _In_ ULONG Offset)
{
    return READ_REGISTER_ULONG((volatile ULONG *)(Adapter->Registers + Offset));
}

FORCEINLINE
VOID
IwlWrite32(
    _In_ PIWL_ADAPTER Adapter,
    _In_ ULONG Offset,
    _In_ ULONG Value)
{
    WRITE_REGISTER_ULONG((volatile ULONG *)(Adapter->Registers + Offset), Value);
}

FORCEINLINE
VOID
IwlSetBits(
    _In_ PIWL_ADAPTER Adapter,
    _In_ ULONG Offset,
    _In_ ULONG Bits)
{
    IwlWrite32(Adapter, Offset, IwlRead32(Adapter, Offset) | Bits);
}

FORCEINLINE
VOID
IwlClearBits(
    _In_ PIWL_ADAPTER Adapter,
    _In_ ULONG Offset,
    _In_ ULONG Bits)
{
    IwlWrite32(Adapter, Offset, IwlRead32(Adapter, Offset) & ~Bits);
}

FORCEINLINE
ULONGLONG
IwlDmaAddress(
    _In_ const IWL_DMA *Dma)
{
    return (ULONGLONG)Dma->Pa.QuadPart;
}

/* iwlbe.c */
PVOID
IwlAllocate(
    _In_ SIZE_T Size);

VOID
IwlFree(
    _In_opt_ PVOID Buffer);

VOID
IwlSleep(
    _In_ ULONG Milliseconds);

VOID
IwlWakeCommand(
    _In_ PIWL_ADAPTER Adapter);

VOID
IwlTxPump(
    _In_ PIWL_ADAPTER Adapter);

/* trans.c */
NDIS_STATUS
IwlMapResources(
    _In_ PIWL_ADAPTER Adapter,
    _In_opt_ PNDIS_RESOURCE_LIST Resources);

VOID
IwlUnmapResources(
    _In_ PIWL_ADAPTER Adapter);

NDIS_STATUS
IwlRegisterInterrupt(
    _In_ PIWL_ADAPTER Adapter);

VOID
IwlDeregisterInterrupt(
    _In_ PIWL_ADAPTER Adapter);

NTSTATUS
IwlStartWorker(
    _In_ PIWL_ADAPTER Adapter);

VOID
IwlStopWorker(
    _In_ PIWL_ADAPTER Adapter);

BOOLEAN
IwlAllocDma(
    _In_ PIWL_ADAPTER Adapter,
    _Out_ PIWL_DMA Dma,
    _In_ ULONG Length,
    _In_ BOOLEAN Cached);

VOID
IwlFreeDma(
    _In_ PIWL_ADAPTER Adapter,
    _Inout_ PIWL_DMA Dma);

ULONG
IwlReadPrph(
    _In_ PIWL_ADAPTER Adapter,
    _In_ ULONG Address);

VOID
IwlWritePrph(
    _In_ PIWL_ADAPTER Adapter,
    _In_ ULONG Address,
    _In_ ULONG Value);

NTSTATUS
IwlStartHardware(
    _In_ PIWL_ADAPTER Adapter);

VOID
IwlStopDevice(
    _In_ PIWL_ADAPTER Adapter);

NTSTATUS
IwlAllocateRings(
    _In_ PIWL_ADAPTER Adapter);

VOID
IwlFreeRings(
    _In_ PIWL_ADAPTER Adapter);

NTSTATUS
IwlKickFirmware(
    _In_ PIWL_ADAPTER Adapter);

VOID
IwlFirmwareAlive(
    _In_ PIWL_ADAPTER Adapter);

NTSTATUS
IwlLoadPnvmDram(
    _In_ PIWL_ADAPTER Adapter,
    _In_reads_(Count) const IWL_SECTION *Chunks,
    _In_ ULONG Count);

VOID
IwlArmNotification(
    _In_ PIWL_ADAPTER Adapter,
    _In_ UCHAR Group,
    _In_ UCHAR Command);

NTSTATUS
IwlWaitNotification(
    _In_ PIWL_ADAPTER Adapter,
    _In_ ULONG TimeoutMs);

NTSTATUS
IwlSendCommand(
    _In_ PIWL_ADAPTER Adapter,
    _In_ UCHAR Group,
    _In_ UCHAR Command,
    _In_reads_bytes_opt_(Length) const VOID *Data,
    _In_ ULONG Length,
    _Out_writes_bytes_opt_(ResponseLength) PVOID Response,
    _In_ ULONG ResponseLength,
    _Out_opt_ PULONG Returned);

NTSTATUS
IwlAllocateTxQueue(
    _In_ PIWL_ADAPTER Adapter,
    _Inout_ PIWL_TXQ Queue,
    _In_ UCHAR Tid);

VOID
IwlReleaseTxQueue(
    _In_ PIWL_ADAPTER Adapter,
    _Inout_ PIWL_TXQ Queue,
    _In_ UCHAR Tid,
    _In_ BOOLEAN TellFirmware);

NTSTATUS
IwlQueueFrame(
    _In_ PIWL_ADAPTER Adapter,
    _Inout_ PIWL_TXQ Queue,
    _In_reads_bytes_(HeaderLength) const UCHAR *Header,
    _In_ ULONG HeaderLength,
    _In_reads_bytes_(PayloadLength) const UCHAR *Payload,
    _In_ ULONG PayloadLength,
    _In_ ULONG RateFlags,
    _In_ ULONG TxFlags);

VOID
IwlReclaimTx(
    _In_ PIWL_ADAPTER Adapter,
    _In_ USHORT QueueId,
    _In_ USHORT Ssn);

/* ucode.c */
NTSTATUS
IwlLoadFirmwareFile(
    _In_ PIWL_ADAPTER Adapter);

NTSTATUS
IwlBootFirmware(
    _In_ PIWL_ADAPTER Adapter);

VOID
IwlReadMacAddress(
    _In_ PIWL_ADAPTER Adapter);

/* mac.c */
NTSTATUS
IwlVifInit(
    _In_ PIWL_ADAPTER Adapter);

VOID
IwlSetChannel(
    _In_ PIWL_ADAPTER Adapter,
    _In_ UCHAR Channel);

VOID
IwlSetRxFilter(
    _In_ PIWL_ADAPTER Adapter,
    _In_ BOOLEAN AllBeacons);

NTSTATUS
IwlVifSetLink(
    _In_ PIWL_ADAPTER Adapter,
    _In_reads_bytes_opt_(IWL_ADDRESS_LENGTH) const UCHAR *Bssid,
    _In_ BOOLEAN Associated,
    _In_ USHORT Aid,
    _In_ USHORT BeaconInterval);

NTSTATUS
IwlVifSetRates(
    _In_ PIWL_ADAPTER Adapter,
    _In_ ULONG RateMask);

NTSTATUS
IwlFirmwareScan(
    _In_ PIWL_ADAPTER Adapter,
    _In_opt_ PDOT11_SSID Ssid);

NTSTATUS
IwlTxFrame(
    _In_ PIWL_ADAPTER Adapter,
    _In_reads_bytes_(Length) const UCHAR *Frame,
    _In_ ULONG Length,
    _In_ BOOLEAN Management);

/* rx.c */
VOID
IwlHandlePacket(
    _In_ PIWL_ADAPTER Adapter,
    _In_reads_bytes_(Length) PUCHAR Packet,
    _In_ ULONG Length);

NTSTATUS
IwlSendNetBuffer(
    _In_ PIWL_ADAPTER Adapter,
    _In_ PNET_BUFFER NetBuffer,
    _In_ BOOLEAN Exempt);

/* mlme.c and oid.c */
ULONG
IwlFrameHeaderLength(
    _In_reads_bytes_(Length) const UCHAR *Frame,
    _In_ ULONG Length);

VOID
IwlClearKeys(
    _In_ PIWL_ADAPTER Adapter);

VOID
IwlMlmeUpdateRssi(
    _In_ PIWL_ADAPTER Adapter,
    _In_reads_bytes_(IWL_ADDRESS_LENGTH) const UCHAR* Transmitter,
    _In_ LONG Rssi);

VOID
IwlMlmeReceive(
    _In_ PIWL_ADAPTER Adapter,
    _In_reads_bytes_(Length) const UCHAR *Frame,
    _In_ ULONG Length,
    _In_ LONG Rssi,
    _In_ BOOLEAN HwDecrypted);

VOID
IwlReceiveData(
    _In_ PIWL_ADAPTER Adapter,
    _In_reads_bytes_(Length) const UCHAR *Frame,
    _In_ ULONG Length,
    _In_ LONG Rssi,
    _In_ ULONG Rate);

VOID
IwlIndicateLinkState(
    _In_ PIWL_ADAPTER Adapter,
    _In_ BOOLEAN Connected);

VOID
IwlIndicateDot11Status(
    _In_ PIWL_ADAPTER Adapter,
    _In_ NDIS_STATUS StatusCode,
    _In_reads_bytes_opt_(BufferSize) PVOID Buffer,
    _In_ ULONG BufferSize);

ULONG
IwlBuildBssList(
    _In_ PIWL_ADAPTER Adapter,
    _Out_writes_bytes_opt_(BufferLength) PUCHAR Buffer,
    _In_ ULONG BufferLength,
    _Out_ PULONG BytesNeeded);

ULONG
IwlChannelToFrequency(
    _In_ ULONG Channel);

VOID
NTAPI
IwlCommandThread(
    _In_ PVOID Context);

NDIS_STATUS
NTAPI
IwlMiniportOidRequest(
    _In_ NDIS_HANDLE MiniportAdapterContext,
    _In_ PNDIS_OID_REQUEST OidRequest);

VOID
NTAPI
IwlMiniportCancelOidRequest(
    _In_ NDIS_HANDLE MiniportAdapterContext,
    _In_ PVOID RequestId);

VOID
NTAPI
IwlMiniportReturnNetBufferLists(
    _In_ NDIS_HANDLE MiniportAdapterContext,
    _In_ PNET_BUFFER_LIST NetBufferLists,
    _In_ ULONG ReturnFlags);
