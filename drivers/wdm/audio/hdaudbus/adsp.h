#ifndef __ADSP_INTERFACE
#define __ADSP_INTERFACE

/* the appended members carry the bus's own types */
#include "policy.h"

//
// The ADSP_BUS_INTERFACE interface GUID
//
// {752A2CAE-3455-4D18-A184-8B34B22632CE}
DEFINE_GUID(GUID_ADSP_BUS_INTERFACE,
    0x752a2cae, 0x3455, 0x4d18, 0xa1, 0x84, 0x8b, 0x34, 0xb2, 0x26, 0x32, 0xce);

typedef struct _NHLT_INFO {
    PVOID nhlt;
    UINT64 nhltSz;
} NHLT_INFO, * PNHLT_INFO;

typedef _Must_inspect_result_ NTSTATUS(*PGET_ADSP_RESOURCES) (_In_ PVOID _context, _Out_ _PCI_BAR* hdaBar, _Out_ _PCI_BAR* adspBar, PVOID* ppcap, PNHLT_INFO nhltInfo, _Out_ BUS_INTERFACE_STANDARD* pciConfig);
typedef _Must_inspect_result_ NTSTATUS(*PDSP_SET_POWER_STATE) (_In_ PVOID _context, _In_ DEVICE_POWER_STATE newPowerState);
typedef _Must_inspect_result_ BOOL(*PADSP_INTERRUPT_CALLBACK)(PVOID context);
typedef _Must_inspect_result_ NTSTATUS(*PREGISTER_ADSP_INTERRUPT) (_In_ PVOID _context, _In_ PADSP_INTERRUPT_CALLBACK callback, _In_ PVOID callbackContext);
typedef _Must_inspect_result_ NTSTATUS(*PUNREGISTER_ADSP_INTERRUPT) (_In_ PVOID _context);
typedef _Must_inspect_result_ NTSTATUS(*PGET_STREAM)(_In_ PVOID _context, HDAUDIO_STREAM_FORMAT StreamFormat, PHANDLE Handle, _Out_ UINT8* streamTag);
typedef _Must_inspect_result_ NTSTATUS(*PFREE_STREAM)(_In_ PVOID _context, _In_ HANDLE Handle);
typedef _Must_inspect_result_ NTSTATUS(*PDSP_PREPARE_STREAM)(_In_ PVOID _context, _In_ HANDLE Handle, _In_ unsigned int ByteSize, _In_ int frags, _Out_ PVOID* bdlBuf);
typedef _Must_inspect_result_ NTSTATUS(*PDSP_CLEANUP_STREAM)(_In_ PVOID _context, _In_ HANDLE Handle);
typedef _Must_inspect_result_ UINT32(*PDSP_STREAM_POSITION)(_In_ PVOID _context, _In_ HANDLE Handle);
typedef void (*PDSP_START_STOP_STREAM)(_In_ PVOID _context, _In_ HANDLE Handle, BOOL startStop);
typedef void (*PDSP_ENABLE_SPIB)(_In_ PVOID _context, _In_ HANDLE Handle, UINT32 value);
typedef void (*PDSP_DISABLE_SPIB)(_In_ PVOID _context, _In_ HANDLE Handle);
/* The bus driver's interrupt spin lock (WdfInterruptAcquireLock), raised to DIRQL; hda_interrupt runs under it while it calls the DSP callback */
typedef void (*PADSP_INTERRUPT_LOCK) (_In_ PVOID _context);
/* What the controller offers a DSP child; version 2 of the interface reports it */
typedef struct _ADSP_PLATFORM_INFO {
	/* The controller addresses 64 bits of DMA address (hda_registers.h HDA_GCAP_64OK) */
	BOOLEAN Dma64Bit;
} ADSP_PLATFORM_INFO, * PADSP_PLATFORM_INFO;
typedef _Must_inspect_result_ NTSTATUS (*PADSP_GET_PLATFORM_INFO) (_In_ PVOID _context, _Out_ PADSP_PLATFORM_INFO platformInfo);
/* The names a topology selection reads: the machine's DMI strings (policy.h) and the controller's _DSD topology names (sof-tplg.h SOF_TPLG); each name is at most 31 characters and terminated */
#define ADSP_TOPOLOGY_NAME_BYTES 32
typedef struct _ADSP_TOPOLOGY_SOURCES {
	/* the DMI strings were read, in full or truncated */
	BOOLEAN DmiValid;
	DSP_DMI_STRINGS Dmi;
	/* the controller's _DSD carried the three topology names */
	BOOLEAN DsdValid;
	char SpeakerTplg[ADSP_TOPOLOGY_NAME_BYTES];
	char HpTplg[ADSP_TOPOLOGY_NAME_BYTES];
	char MicTplg[ADSP_TOPOLOGY_NAME_BYTES];
} ADSP_TOPOLOGY_SOURCES, * PADSP_TOPOLOGY_SOURCES;
typedef _Must_inspect_result_ NTSTATUS (*PADSP_GET_TOPOLOGY_SOURCES) (_In_ PVOID _context, _Out_ PADSP_TOPOLOGY_SOURCES sources);
/* Appended for the DSP driver's PCM streams: the codec interfaces' stream routines (hdaudio.cpp HDA_*) made available to a DSP stream through the one code path of hdac_stream.cpp. AllocateDmaBufferWithNotification gives the stream its pages and descriptors (NotificationCount 1 or 2) and sets it up; FreeDmaBufferWithNotification releases them; SetDmaEngineState runs, pauses or resets the one stream its handle names; GetLinkPositionRegister is the stream's LPIB and GetPositionRegister the controller's extended DMA-position register of it, both for the audio engine to read; GetWallClockRegister the 24 MHz wall clock; RegisterNotificationEvent and UnregisterNotificationEvent the period events the interrupt's DPC sets */
typedef _Must_inspect_result_ NTSTATUS (*PADSP_ALLOCATE_DMA_BUFFER_WITH_NOTIFICATION) (_In_ PVOID _context, _In_ HANDLE Handle, _In_ ULONG NotificationCount, _In_ SIZE_T RequestedBufferSize, _Out_ PMDL* BufferMdl, _Out_ PSIZE_T AllocatedBufferSize, _Out_ PSIZE_T OffsetFromFirstPage, _Out_ PUCHAR StreamId, _Out_ PULONG FifoSize);
typedef _Must_inspect_result_ NTSTATUS (*PADSP_FREE_DMA_BUFFER_WITH_NOTIFICATION) (_In_ PVOID _context, _In_ HANDLE Handle, _In_ PMDL BufferMdl, _In_ SIZE_T BufferSize);
/* A stream's buffer taken off the stream and handed back to the caller, nothing freed (version 3): the DSP driver's stream manager lets a stream go across a power-down or a fault while the pin that owns the buffer still maps it, and the pin's own free returns the pages and the MDL (intcdsp/stream.c, R5-01); the stream must not run and must hold a buffer */
typedef _Must_inspect_result_ NTSTATUS (*PADSP_DETACH_DMA_BUFFER) (_In_ PVOID _context, _In_ HANDLE Handle, _Out_ PMDL* BufferMdl);
typedef _Must_inspect_result_ NTSTATUS (*PADSP_SET_DMA_ENGINE_STATE) (_In_ PVOID _context, _In_ HANDLE Handle, _In_ HDAUDIO_STREAM_STATE StreamState);
typedef _Must_inspect_result_ NTSTATUS (*PADSP_GET_STREAM_POSITION_REGISTER) (_In_ PVOID _context, _In_ HANDLE Handle, _Out_ PULONG* Position);
typedef VOID (*PADSP_GET_WALL_CLOCK_REGISTER) (_In_ PVOID _context, _Out_ PULONG* Wallclock);
typedef _Must_inspect_result_ NTSTATUS (*PADSP_STREAM_NOTIFICATION_EVENT) (_In_ PVOID _context, _In_ HANDLE Handle, _In_ PKEVENT NotificationEvent);

typedef struct _ADSP_BUS_INTERFACE
{
    //
    // First we define the standard INTERFACE structure ...
    //
    USHORT                    Size;
    USHORT                    Version;
    PVOID                     Context;
    PINTERFACE_REFERENCE      InterfaceReference;
    PINTERFACE_DEREFERENCE    InterfaceDereference;

    //
    // Then we expand the structure with the ADSP_BUS_INTERFACE stuff.

    UINT16 CtlrDevId;
    PGET_ADSP_RESOURCES           GetResources;
    PDSP_SET_POWER_STATE          SetDSPPowerState;
    PREGISTER_ADSP_INTERRUPT      RegisterInterrupt;
    PUNREGISTER_ADSP_INTERRUPT    UnregisterInterrupt;
    PGET_STREAM                   GetRenderStream;
    PGET_STREAM                   GetCaptureStream;
    PFREE_STREAM                  FreeStream;
    PDSP_PREPARE_STREAM           PrepareDSP;
    PDSP_CLEANUP_STREAM           CleanupDSP;
    PDSP_START_STOP_STREAM        TriggerDSP;
    PDSP_STREAM_POSITION          StreamPosition;

    PDSP_ENABLE_SPIB              DSPEnableSPIB;
    PDSP_DISABLE_SPIB             DSPDisableSPIB;

    /* Appended for intcdsp: the interrupt lock its interrupt enable read-modify-writes are serialized with */
    PADSP_INTERRUPT_LOCK          AcquireInterruptLock;
    PADSP_INTERRUPT_LOCK          ReleaseInterruptLock;

    /* Appended for version 2: the controller's facts; a driver built for version 1 reads the structure through ReleaseInterruptLock */
    PADSP_GET_PLATFORM_INFO       GetPlatformInfo;

    /* Appended for the DSP driver's topology selection: the machine's DMI strings and the controller's _DSD topology names; a driver built before it reads the structure through GetPlatformInfo */
    PADSP_GET_TOPOLOGY_SOURCES     GetTopologySources;

    /* Appended for the DSP driver's PCM streams; a driver built before them reads the structure through GetTopologySources */
    PADSP_ALLOCATE_DMA_BUFFER_WITH_NOTIFICATION AllocateDmaBufferWithNotification;
    PADSP_FREE_DMA_BUFFER_WITH_NOTIFICATION     FreeDmaBufferWithNotification;
    PADSP_SET_DMA_ENGINE_STATE                  SetDmaEngineState;
    PADSP_GET_STREAM_POSITION_REGISTER          GetLinkPositionRegister;
    PADSP_GET_STREAM_POSITION_REGISTER          GetPositionRegister;
    PADSP_GET_WALL_CLOCK_REGISTER               GetWallClockRegister;
    PADSP_STREAM_NOTIFICATION_EVENT             RegisterNotificationEvent;
    PADSP_STREAM_NOTIFICATION_EVENT             UnregisterNotificationEvent;
    /* Appended for version 3: the buffer detached, not freed; a driver built before it reads the structure through UnregisterNotificationEvent, and one that needs it asks for version 3, which the framework grants only with a bus of that version */
    PADSP_DETACH_DMA_BUFFER                     DetachDmaBuffer;
} ADSP_BUS_INTERFACE, * PADSP_BUS_INTERFACE;

#ifndef ADSP_DECL
ADSP_BUS_INTERFACE ADSP_BusInterface(PVOID Context);
#endif
#endif
