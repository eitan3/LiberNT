#include "driver.h"
#define ADSP_DECL 1
#include "adsp.h"
#include "dspgating.h"
#include "sof-tplg.h"

NTSTATUS ADSPGetResources(_In_ PVOID _context, _PCI_BAR* hdaBar, _PCI_BAR* adspBar, PVOID *ppcap, PNHLT_INFO nhltInfo, BUS_INTERFACE_STANDARD* pciConfig) {
	if (!_context)
		return STATUS_NO_SUCH_DEVICE;

	PPDO_DEVICE_DATA devData = (PPDO_DEVICE_DATA)_context;
	if (!devData->FdoContext) {
		return STATUS_NO_SUCH_DEVICE;
	}

	if (hdaBar) {
		*hdaBar = devData->FdoContext->m_BAR0;
	}

	if (adspBar) {
		*adspBar = devData->FdoContext->m_BAR4;
	}

	if (ppcap) {
		*ppcap = devData->FdoContext->ppcap;
	}

	if (nhltInfo) {
		nhltInfo->nhlt = NULL;
		nhltInfo->nhltSz = 0;
		if (devData->FdoContext->nhlt) {
			nhltInfo->nhlt = devData->FdoContext->nhlt;
			nhltInfo->nhltSz = devData->FdoContext->nhltSz;
		}
	}

	if (pciConfig) {
		*pciConfig = devData->FdoContext->BusInterface;
	}

	return STATUS_SUCCESS;
}

NTSTATUS ADSPSetPowerState(_In_ PVOID _context, _In_ DEVICE_POWER_STATE powerState) {
	if (!_context)
		return STATUS_NO_SUCH_DEVICE;

	PPDO_DEVICE_DATA devData = (PPDO_DEVICE_DATA)_context;
	if (!devData->FdoContext) {
		return STATUS_NO_SUCH_DEVICE;
	}

	NTSTATUS status = STATUS_SUCCESS;
	if (powerState == PowerDeviceD3) {
		WdfDeviceResumeIdle(devData->FdoContext->WdfDevice);
	} else if (powerState == PowerDeviceD0) {
		status = WdfDeviceStopIdle(devData->FdoContext->WdfDevice, TRUE);
	}
	return status;
}

NTSTATUS ADSPRegisterInterrupt(_In_ PVOID _context, _In_ PADSP_INTERRUPT_CALLBACK callback, _In_ PVOID callbackContext) {
	if (!_context)
		return STATUS_NO_SUCH_DEVICE;

	PPDO_DEVICE_DATA devData = (PPDO_DEVICE_DATA)_context;
	if (!devData->FdoContext) {
		return STATUS_NO_SUCH_DEVICE;
	}

	/* the service routine reads the pair under the interrupt lock (hdac_controller.cpp hda_interrupt): both are stored under it, so the routine never sees one of a new pair with the other of the old (R1-07) */
	WdfInterruptAcquireLock(devData->FdoContext->Interrupt);
	devData->FdoContext->dspInterruptCallback = callback;
	devData->FdoContext->dspInterruptContext = callbackContext;
	WdfInterruptReleaseLock(devData->FdoContext->Interrupt);
	return STATUS_SUCCESS;
}

NTSTATUS ADSPUnregisterInterrupt(_In_ PVOID _context) {
	if (!_context)
		return STATUS_NO_SUCH_DEVICE;

	PPDO_DEVICE_DATA devData = (PPDO_DEVICE_DATA)_context;
	if (!devData->FdoContext) {
		return STATUS_NO_SUCH_DEVICE;
	}

	WdfInterruptAcquireLock(devData->FdoContext->Interrupt);
	devData->FdoContext->dspInterruptCallback = NULL;
	devData->FdoContext->dspInterruptContext = NULL;
	WdfInterruptReleaseLock(devData->FdoContext->Interrupt);
	return STATUS_SUCCESS;
}

NTSTATUS ADSPGetRenderStream(_In_ PVOID _context, HDAUDIO_STREAM_FORMAT StreamFormat, PHANDLE Handle, _Out_ UINT8* streamTag) {
	if (!_context)
		return STATUS_NO_SUCH_DEVICE;

	PPDO_DEVICE_DATA devData = (PPDO_DEVICE_DATA)_context;
	if (!devData->FdoContext) {
		return STATUS_NO_SUCH_DEVICE;
	}

	PFDO_CONTEXT fdoContext = devData->FdoContext;

	NTSTATUS status = WdfDeviceStopIdle(devData->FdoContext->WdfDevice, TRUE);
	if (!NT_SUCCESS(status)) {
		return status;
	}

	WdfInterruptAcquireLock(devData->FdoContext->Interrupt);
	for (UINT32 i = 0; i < fdoContext->playbackStreams; i++) {
		int tag = fdoContext->playbackIndexOff + i;
		PHDAC_STREAM stream = &fdoContext->streams[tag];
		if (stream->PdoContext != NULL) {
			continue;
		}

		stream->stripe = FALSE;
		stream->PdoContext = devData;
		stream->running = FALSE;
		stream->streamFormat = StreamFormat;

		int mask = HDA_PPCTL_PROCEN(stream->idx);
		UINT32 val = 0;
		val = read16(fdoContext->ppcap + HDA_REG_PP_PPCTL) & mask;

		if (!val) {
			hdac_update32(fdoContext->ppcap, HDA_REG_PP_PPCTL, mask, mask);
		}

		if (Handle)
			*Handle = (HANDLE)stream;
		if (streamTag)
			*streamTag = stream->streamTag;

		WdfInterruptReleaseLock(devData->FdoContext->Interrupt);
		return STATUS_SUCCESS;
	}

	WdfInterruptReleaseLock(devData->FdoContext->Interrupt);
	WdfDeviceResumeIdle(devData->FdoContext->WdfDevice);
	return STATUS_INSUFFICIENT_RESOURCES;
}

NTSTATUS ADSPGetCaptureStream(_In_ PVOID _context, HDAUDIO_STREAM_FORMAT StreamFormat, PHANDLE Handle, _Out_ UINT8* streamTag) {
	if (!_context)
		return STATUS_NO_SUCH_DEVICE;

	PPDO_DEVICE_DATA devData = (PPDO_DEVICE_DATA)_context;
	if (!devData->FdoContext) {
		return STATUS_NO_SUCH_DEVICE;
	}

	PFDO_CONTEXT fdoContext = devData->FdoContext;

	NTSTATUS status = WdfDeviceStopIdle(devData->FdoContext->WdfDevice, TRUE);
	if (!NT_SUCCESS(status)) {
		return status;
	}

	WdfInterruptAcquireLock(devData->FdoContext->Interrupt);
	for (UINT32 i = 0; i < fdoContext->captureStreams; i++) {
		int tag = fdoContext->captureIndexOff + i;
		PHDAC_STREAM stream = &fdoContext->streams[tag];
		if (stream->PdoContext != NULL) {
			continue;
		}

		stream->stripe = FALSE;
		stream->PdoContext = devData;
		stream->running = FALSE;
		stream->streamFormat = StreamFormat;

		int mask = HDA_PPCTL_PROCEN(stream->idx);
		UINT32 val = 0;
		val = read16(fdoContext->ppcap + HDA_REG_PP_PPCTL) & mask;

		if (!val) {
			hdac_update32(fdoContext->ppcap, HDA_REG_PP_PPCTL, mask, mask);
		}

		if (Handle)
			*Handle = (HANDLE)stream;
		if (streamTag)
			*streamTag = stream->streamTag;

		WdfInterruptReleaseLock(devData->FdoContext->Interrupt);
		return STATUS_SUCCESS;
	}

	WdfInterruptReleaseLock(devData->FdoContext->Interrupt);
	WdfDeviceResumeIdle(devData->FdoContext->WdfDevice);
	return STATUS_INSUFFICIENT_RESOURCES;
}

NTSTATUS ADSPFreeStream(
	_In_ PVOID _context,
	_In_ HANDLE Handle
) {
	SklHdAudBusPrint(DEBUG_LEVEL_VERBOSE, DBG_IOCTL, "%s called!\n", __func__);

	PPDO_DEVICE_DATA devData = (PPDO_DEVICE_DATA)_context;
	if (!devData->FdoContext) {
		return STATUS_NO_SUCH_DEVICE;
	}

	PHDAC_STREAM stream = HdaStreamOf(devData, Handle);
	if (stream == NULL) {
		return STATUS_INVALID_HANDLE;
	}

	WdfInterruptAcquireLock(devData->FdoContext->Interrupt);

	if (stream->running) {
		WdfInterruptReleaseLock(devData->FdoContext->Interrupt);
		return STATUS_INVALID_DEVICE_REQUEST;
	}

	PFDO_CONTEXT fdoContext = devData->FdoContext;
	int mask = HDA_PPCTL_PROCEN(stream->idx);
	UINT32 val = 0;
	val = read16(fdoContext->ppcap + HDA_REG_PP_PPCTL) & mask;

	if (val) {
		hdac_update32(fdoContext->ppcap, HDA_REG_PP_PPCTL, mask, 0);
	}

	stream->PdoContext = NULL;
	/* the stream goes back to the pool and forgets the events its owner registered on it: an owner
	 * that lets its streams go without unregistering them (intcdsp's fault release, stream.c
	 * IntcDspStreamsLetGo, which drops them by count) would leave a pointer to a freed event on
	 * the record, and the next stream to take it would have the DPC set that event
	 * (hdac_controller.cpp hda_dpc): a freed object, 0xc0000420 in KeSetEvent on the VM */
	RtlZeroMemory(stream->registeredEvents, sizeof(stream->registeredEvents));
	InterlockedExchange(&stream->notificationPending, FALSE);
	WdfInterruptReleaseLock(devData->FdoContext->Interrupt);
	WdfDeviceResumeIdle(devData->FdoContext->WdfDevice);

	return STATUS_SUCCESS;
}

NTSTATUS ADSPPrepareDSP(
	_In_ PVOID _context,
	_In_ HANDLE Handle,
	_In_ unsigned int ByteSize,
	_In_ int NumBlocks,
	_Out_ PVOID* bdlBuf
) {
	PPDO_DEVICE_DATA devData = (PPDO_DEVICE_DATA)_context;
	if (!devData->FdoContext) {
		return STATUS_NO_SUCH_DEVICE;
	}

	PHDAC_STREAM stream = HdaStreamOf(devData, Handle);
	if (stream == NULL) {
		return STATUS_INVALID_HANDLE;
	}

	WdfInterruptAcquireLock(devData->FdoContext->Interrupt);

	if (stream->running) {
		WdfInterruptReleaseLock(devData->FdoContext->Interrupt);
		return STATUS_DEVICE_BUSY;
	}

	WdfInterruptReleaseLock(devData->FdoContext->Interrupt);

	stream->mdlBuf = NULL;
	stream->bufSz = ByteSize;
	stream->numBlocks = (UINT16)NumBlocks;

	hdac_stream_reset(stream);

	/* reset BDL address */
	stream_write32(stream, SD_BDLPL, 0);
	stream_write32(stream, SD_BDLPU, 0);

	hdac_stream_setup(stream);

	if (bdlBuf)
		*bdlBuf = stream->bdl;
	return STATUS_SUCCESS;
}

NTSTATUS ADSPCleanupDSP(_In_ PVOID _context, _In_ HANDLE Handle) {
	PPDO_DEVICE_DATA devData = (PPDO_DEVICE_DATA)_context;
	if (!devData->FdoContext) {
		return STATUS_NO_SUCH_DEVICE;
	}

	PHDAC_STREAM stream = HdaStreamOf(devData, Handle);
	if (stream == NULL) {
		return STATUS_INVALID_HANDLE;
	}

	WdfInterruptAcquireLock(devData->FdoContext->Interrupt);

	stream_write32(stream, SD_BDLPL, 0);
	stream_write32(stream, SD_BDLPU, 0);
	stream_write32(stream, SD_CTL, 0);

	WdfInterruptReleaseLock(devData->FdoContext->Interrupt);

	return STATUS_SUCCESS;
}

void ADSPStartStopDSP(_In_ PVOID _context, _In_ HANDLE Handle, BOOL startStop) {
	PPDO_DEVICE_DATA devData = (PPDO_DEVICE_DATA)_context;
	if (!devData->FdoContext) {
		return;
	}

	PHDAC_STREAM stream = HdaStreamOf(devData, Handle);
	if (stream == NULL) {
		return;
	}

	if (startStop)
		hdac_stream_start(stream);
	else
		hdac_stream_stop(stream);
}

void ADSPEnableSPIB(_In_ PVOID _context, _In_ HANDLE Handle, UINT32 value) {
	PPDO_DEVICE_DATA devData = (PPDO_DEVICE_DATA)_context;
	if (!devData->FdoContext) {
		return;
	}

	PHDAC_STREAM stream = HdaStreamOf(devData, Handle);
	if (stream == NULL) {
		return;
	}

	if (!devData->FdoContext->spbcap) {
		return;
	}

	UINT32 mask = (1 << stream->idx);
	hdac_update32(devData->FdoContext->spbcap, HDA_REG_SPB_SPBFCCTL, mask, mask);

	write32(stream->spib_addr, value);
}

void ADSPDisableSPIB(_In_ PVOID _context, _In_ HANDLE Handle) {
	PPDO_DEVICE_DATA devData = (PPDO_DEVICE_DATA)_context;
	if (!devData->FdoContext) {
		return;
	}

	PHDAC_STREAM stream = HdaStreamOf(devData, Handle);
	if (stream == NULL) {
		return;
	}

	if (!devData->FdoContext->spbcap) {
		return;
	}

	UINT32 mask = (1 << stream->idx);
	hdac_update32(devData->FdoContext->spbcap, HDA_REG_SPB_SPBFCCTL, mask, 0);

	write32(stream->spib_addr, 0);
}

UINT32 ADSPStreamPosition(_In_ PVOID _context, _In_ HANDLE Handle) {
	PPDO_DEVICE_DATA devData = (PPDO_DEVICE_DATA)_context;
	if (!devData->FdoContext) {
		return 0;
	}

	PHDAC_STREAM stream = HdaStreamOf(devData, Handle);
	if (stream == NULL) {
		return 0;
	}

	return *stream->posbuf;
}

/* The lock hda_interrupt runs under: a DSP driver serializes its interrupt register updates against the callback it registered */
void ADSPAcquireInterruptLock(_In_ PVOID _context) {
	PPDO_DEVICE_DATA devData = (PPDO_DEVICE_DATA)_context;

	WdfInterruptAcquireLock(devData->FdoContext->Interrupt);
}

void ADSPReleaseInterruptLock(_In_ PVOID _context) {
	PPDO_DEVICE_DATA devData = (PPDO_DEVICE_DATA)_context;

	WdfInterruptReleaseLock(devData->FdoContext->Interrupt);
}

NTSTATUS ADSPGetPlatformInfo(_In_ PVOID _context, _Out_ PADSP_PLATFORM_INFO platformInfo) {
	if (!_context)
		return STATUS_NO_SUCH_DEVICE;

	PPDO_DEVICE_DATA devData = (PPDO_DEVICE_DATA)_context;
	if (!devData->FdoContext) {
		return STATUS_NO_SUCH_DEVICE;
	}

	platformInfo->Dma64Bit = devData->FdoContext->is64BitOK;
	return STATUS_SUCCESS;
}

/* The machine's DMI strings and the controller's _DSD topology names; a name the firmware fills to its last byte gets its terminator here */
static void copyTopologyName(char* destination, const char* source) {
	size_t length = 0;

	RtlZeroMemory(destination, ADSP_TOPOLOGY_NAME_BYTES);
	while (length < ADSP_TOPOLOGY_NAME_BYTES - 1 && source[length] != 0) {
		destination[length] = source[length];
		length++;
	}
}

NTSTATUS ADSPGetTopologySources(_In_ PVOID _context, _Out_ PADSP_TOPOLOGY_SOURCES sources) {
	if (!_context)
		return STATUS_NO_SUCH_DEVICE;

	PPDO_DEVICE_DATA devData = (PPDO_DEVICE_DATA)_context;
	if (!devData->FdoContext) {
		return STATUS_NO_SUCH_DEVICE;
	}

	RtlZeroMemory(sources, sizeof(*sources));
	if (devData->FdoContext->DmiStatus == DmiOk || devData->FdoContext->DmiStatus == DmiTruncated) {
		sources->DmiValid = TRUE;
		sources->Dmi = devData->FdoContext->Dmi;
	}
	if (devData->FdoContext->sofTplg) {
		const SOF_TPLG* names = (const SOF_TPLG*)devData->FdoContext->sofTplg;
		if (devData->FdoContext->sofTplgSz >= sizeof(*names) && names->magic == SOFTPLG_MAGIC) {
			sources->DsdValid = TRUE;
			copyTopologyName(sources->SpeakerTplg, names->speaker_tplg);
			copyTopologyName(sources->HpTplg, names->hp_tplg);
			copyTopologyName(sources->MicTplg, names->dmic_tplg);
		}
	}
	return STATUS_SUCCESS;
}

/* The DSP stream's buffer, through the codec stream's routine: the pages, the descriptors and the stream's setup */
NTSTATUS ADSPAllocateDmaBuffer(_In_ PVOID _context, _In_ HANDLE Handle, _In_ ULONG NotificationCount, _In_ SIZE_T RequestedBufferSize, _Out_ PMDL* BufferMdl, _Out_ PSIZE_T AllocatedBufferSize, _Out_ PSIZE_T OffsetFromFirstPage, _Out_ PUCHAR StreamId, _Out_ PULONG FifoSize) {
	PPDO_DEVICE_DATA devData = (PPDO_DEVICE_DATA)_context;
	if (!devData->FdoContext) {
		return STATUS_NO_SUCH_DEVICE;
	}

	PHDAC_STREAM stream = HdaStreamOf(devData, Handle);
	if (stream == NULL) {
		return STATUS_INVALID_HANDLE;
	}

	return hdac_stream_allocate_buffer(stream, NotificationCount, RequestedBufferSize, BufferMdl, AllocatedBufferSize,
		OffsetFromFirstPage, StreamId, FifoSize);
}

NTSTATUS ADSPFreeDmaBuffer(_In_ PVOID _context, _In_ HANDLE Handle, _In_ PMDL BufferMdl, _In_ SIZE_T BufferSize) {
	UNREFERENCED_PARAMETER(BufferMdl);
	UNREFERENCED_PARAMETER(BufferSize);

	PPDO_DEVICE_DATA devData = (PPDO_DEVICE_DATA)_context;
	if (!devData->FdoContext) {
		return STATUS_NO_SUCH_DEVICE;
	}

	PHDAC_STREAM stream = HdaStreamOf(devData, Handle);
	if (stream == NULL) {
		return STATUS_INVALID_HANDLE;
	}

	return hdac_stream_free_buffer(stream);
}

/* A stream's buffer taken off the stream and handed to the caller, nothing freed (hdac_stream_detach_buffer): the DSP driver's stream manager holds it for the pin that still maps it (R5-01) */
NTSTATUS ADSPDetachDmaBuffer(_In_ PVOID _context, _In_ HANDLE Handle, _Out_ PMDL* BufferMdl) {
	PPDO_DEVICE_DATA devData = (PPDO_DEVICE_DATA)_context;
	if (!devData->FdoContext) {
		return STATUS_NO_SUCH_DEVICE;
	}

	PHDAC_STREAM stream = HdaStreamOf(devData, Handle);
	if (stream == NULL) {
		return STATUS_INVALID_HANDLE;
	}

	return hdac_stream_detach_buffer(stream, BufferMdl);
}

NTSTATUS ADSPSetDmaEngineState(_In_ PVOID _context, _In_ HANDLE Handle, _In_ HDAUDIO_STREAM_STATE StreamState) {
	PPDO_DEVICE_DATA devData = (PPDO_DEVICE_DATA)_context;
	if (!devData->FdoContext) {
		return STATUS_NO_SUCH_DEVICE;
	}

	PHDAC_STREAM stream = HdaStreamOf(devData, Handle);
	if (stream == NULL) {
		return STATUS_INVALID_HANDLE;
	}

	return hdac_stream_set_state(stream, StreamState);
}

/* The stream's LPIB, for the audio engine to read */
NTSTATUS ADSPGetLinkPositionRegister(_In_ PVOID _context, _In_ HANDLE Handle, _Out_ PULONG* Position) {
	PPDO_DEVICE_DATA devData = (PPDO_DEVICE_DATA)_context;
	if (!devData->FdoContext) {
		return STATUS_NO_SUCH_DEVICE;
	}
	/* the stream's registers sit in BAR0 and its record is the controller's: a controller that is not started answers before the handle is looked at (R5-02) */
	if (!HdaControllerStarted(devData->FdoContext)) {
		return STATUS_DEVICE_NOT_READY;
	}

	PHDAC_STREAM stream = HdaStreamOf(devData, Handle);
	if (stream == NULL) {
		return STATUS_INVALID_HANDLE;
	}

	*Position = hdac_stream_position_register(stream);
	return STATUS_SUCCESS;
}

/* The controller's extended DMA-position register of the stream: Intel's vendor-specific block (hda_registers.h HDA_REG_VS_SDXDPIB_XBASE) */
NTSTATUS ADSPGetPositionRegister(_In_ PVOID _context, _In_ HANDLE Handle, _Out_ PULONG* Position) {
	PPDO_DEVICE_DATA devData = (PPDO_DEVICE_DATA)_context;
	if (!devData->FdoContext) {
		return STATUS_NO_SUCH_DEVICE;
	}
	/* the extended DMA-position registers sit in BAR0 and the stream's record is the controller's: a controller that is not started answers before the handle is looked at (R5-02) */
	if (!HdaControllerStarted(devData->FdoContext)) {
		return STATUS_DEVICE_NOT_READY;
	}

	PHDAC_STREAM stream = HdaStreamOf(devData, Handle);
	if (stream == NULL) {
		return STATUS_INVALID_HANDLE;
	}

	if (devData->FdoContext->venId != VEN_INTEL) {
		*Position = NULL;
		return STATUS_NOT_SUPPORTED;
	}

	*Position = hdac_stream_dma_position_register(stream);
	return STATUS_SUCCESS;
}

void ADSPGetWallClockRegister(_In_ PVOID _context, _Out_ PULONG* Wallclock) {
	PPDO_DEVICE_DATA devData = (PPDO_DEVICE_DATA)_context;
	/* the wall clock is a BAR0 register: none is handed out while the controller is not started (R5-02) */
	*Wallclock = NULL;
	if (!HdaControllerStarted(devData->FdoContext)) {
		return;
	}
	*Wallclock = (ULONG*)(devData->FdoContext->m_BAR0.Base.baseptr + HDA_REG_WALLCLKA);
}

NTSTATUS ADSPRegisterNotificationEvent(_In_ PVOID _context, _In_ HANDLE Handle, _In_ PKEVENT NotificationEvent) {
	PPDO_DEVICE_DATA devData = (PPDO_DEVICE_DATA)_context;
	if (!devData->FdoContext) {
		return STATUS_NO_SUCH_DEVICE;
	}

	PHDAC_STREAM stream = HdaStreamOf(devData, Handle);
	if (stream == NULL) {
		return STATUS_INVALID_HANDLE;
	}

	return hdac_stream_register_event(stream, NotificationEvent) ? STATUS_SUCCESS : STATUS_INSUFFICIENT_RESOURCES;
}

NTSTATUS ADSPUnregisterNotificationEvent(_In_ PVOID _context, _In_ HANDLE Handle, _In_ PKEVENT NotificationEvent) {
	PPDO_DEVICE_DATA devData = (PPDO_DEVICE_DATA)_context;
	if (!devData->FdoContext) {
		return STATUS_NO_SUCH_DEVICE;
	}

	PHDAC_STREAM stream = HdaStreamOf(devData, Handle);
	if (stream == NULL) {
		return STATUS_INVALID_HANDLE;
	}

	return hdac_stream_unregister_event(stream, NotificationEvent) ? STATUS_SUCCESS : STATUS_INVALID_PARAMETER;
}

/* The platform's clock and power gating of the DSP (dspgating.h): a read-modify-write of the two registers through the PCI bus interface the FDO holds, the clock gate first and the power gate last, as Linux's hda_dsp_ctrl_clock_power_gating orders them, each read back after the write (HdaDspPowerGatingWrite): a bit that does not read as written gets the byte form once, and one that still does not ends the call with STATUS_UNSUCCESSFUL, so that a boot stops there. The bus does it itself because the FDO's config space is the bus's to reach, and a word that does not read or write whole ends the call with nothing more written; what was read is kept for the driver's line (ADSPGetDspPowerGatingReadback) */
NTSTATUS ADSPSetDspPowerGating(_In_ PVOID _context, _In_ BOOLEAN AllowGating) {
	PPDO_DEVICE_DATA devData = (PPDO_DEVICE_DATA)_context;
	HDA_DSP_GATING_READBACK Readback;
	NTSTATUS Status;

	if (!_context)
		return STATUS_NO_SUCH_DEVICE;
	if (!devData->FdoContext) {
		return STATUS_NO_SUCH_DEVICE;
	}
	Status = HdaDspPowerGatingWrite(&devData->FdoContext->BusInterface, AllowGating ? 1 : 0, &Readback);
	devData->FdoContext->DspPowerGating = Readback;
	return Status;
}

/* What the last SetDspPowerGating of the controller read of the two registers after each of its forms: the driver's line at a gating that did not change */
VOID ADSPGetDspPowerGatingReadback(_In_ PVOID _context, _Out_ PHDA_DSP_GATING_READBACK Readback) {
	PPDO_DEVICE_DATA devData = (PPDO_DEVICE_DATA)_context;

	RtlZeroMemory(Readback, sizeof(*Readback));
	if (!_context)
		return;
	if (devData->FdoContext)
		*Readback = devData->FdoContext->DspPowerGating;
}

ADSP_BUS_INTERFACE ADSP_BusInterface(PVOID Context) {
	ADSP_BUS_INTERFACE busInterface;
	RtlZeroMemory(&busInterface, sizeof(ADSP_BUS_INTERFACE));

	PPDO_DEVICE_DATA devData = (PPDO_DEVICE_DATA)Context;

	busInterface.Size = sizeof(ADSP_BUS_INTERFACE);
	busInterface.Version = 3;
	busInterface.Context = Context;
	busInterface.InterfaceReference = WdfDeviceInterfaceReferenceNoOp;
	busInterface.InterfaceDereference = WdfDeviceInterfaceDereferenceNoOp;
	busInterface.CtlrDevId = devData->CodecIds.CtlrDevId;
	busInterface.GetResources = ADSPGetResources;
	busInterface.SetDSPPowerState = ADSPSetPowerState;
	busInterface.RegisterInterrupt = ADSPRegisterInterrupt;
	busInterface.UnregisterInterrupt = ADSPUnregisterInterrupt;

	busInterface.GetRenderStream = ADSPGetRenderStream;
	busInterface.GetCaptureStream = ADSPGetCaptureStream;
	busInterface.FreeStream = ADSPFreeStream;
	busInterface.PrepareDSP = ADSPPrepareDSP;
	busInterface.CleanupDSP = ADSPCleanupDSP;
	busInterface.TriggerDSP = ADSPStartStopDSP;
	busInterface.StreamPosition = ADSPStreamPosition;

	busInterface.DSPEnableSPIB = ADSPEnableSPIB;
	busInterface.DSPDisableSPIB = ADSPDisableSPIB;

	busInterface.AcquireInterruptLock = ADSPAcquireInterruptLock;
	busInterface.ReleaseInterruptLock = ADSPReleaseInterruptLock;
	busInterface.GetPlatformInfo = ADSPGetPlatformInfo;
	busInterface.GetTopologySources = ADSPGetTopologySources;
	busInterface.AllocateDmaBufferWithNotification = ADSPAllocateDmaBuffer;
	busInterface.FreeDmaBufferWithNotification = ADSPFreeDmaBuffer;
	busInterface.DetachDmaBuffer = ADSPDetachDmaBuffer;
	busInterface.SetDmaEngineState = ADSPSetDmaEngineState;
	busInterface.GetLinkPositionRegister = ADSPGetLinkPositionRegister;
	busInterface.GetPositionRegister = ADSPGetPositionRegister;
	busInterface.GetWallClockRegister = ADSPGetWallClockRegister;
	busInterface.RegisterNotificationEvent = ADSPRegisterNotificationEvent;
	busInterface.UnregisterNotificationEvent = ADSPUnregisterNotificationEvent;
	busInterface.SetDspPowerGating = ADSPSetDspPowerGating;
	busInterface.GetDspPowerGatingReadback = ADSPGetDspPowerGatingReadback;

	return busInterface;
}
