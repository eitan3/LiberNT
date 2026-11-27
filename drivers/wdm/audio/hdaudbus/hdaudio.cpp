#include "driver.h"

NTSTATUS HDA_WaitForTransfer(
	PFDO_CONTEXT fdoCtx,
	UINT16 codecAddr,
	_In_ ULONG Count,
	_Inout_updates_(Count)
	PHDAUDIO_CODEC_TRANSFER CodecTransfer
) {
	NTSTATUS status = STATUS_SUCCESS;

	SklHdAudBusPrint(DEBUG_LEVEL_VERBOSE, DBG_IOCTL, "%s called (Count: %d)!\n", __func__, Count);

	LARGE_INTEGER Timeout;
	Timeout.QuadPart = -10LL * 1000LL * 1000LL * (LONGLONG)Count;
	ULONG TransferredCount;
	for (;;) {
		status = KeWaitForSingleObject(&fdoCtx->rirb.xferEvent[codecAddr], Executive, KernelMode, FALSE, &Timeout);
		KeClearEvent(&fdoCtx->rirb.xferEvent[codecAddr]);

		SklHdAudBusPrint(DEBUG_LEVEL_VERBOSE, DBG_IOCTL, "%s wait status: 0x%x!\n", __func__, status);

		WdfInterruptAcquireLock(fdoCtx->Interrupt);
		TransferredCount = 0;
		for (ULONG i = 0; i < Count; i++) {
			if (CodecTransfer[i].Input.IsValid) {
				TransferredCount++;
			}
		}

		if (TransferredCount < Count && status == STATUS_TIMEOUT) {
			InterlockedExchange(&fdoCtx->rirb.cmds[codecAddr], 0);
			RtlZeroMemory(&fdoCtx->rirb.xfer[codecAddr], sizeof(fdoCtx->rirb.xfer[codecAddr]));
		}
		WdfInterruptReleaseLock(fdoCtx->Interrupt);

		if (TransferredCount == Count || status == STATUS_TIMEOUT)
			break;
	}

	if (TransferredCount < Count) {
		SklHdAudBusPrint(DEBUG_LEVEL_VERBOSE, DBG_IOCTL, "%s timeout (Count: %d, transferred %d)!\n", __func__, Count, TransferredCount);
		status = STATUS_IO_TIMEOUT;
		goto out;
	}

out:
	SklHdAudBusPrint(DEBUG_LEVEL_VERBOSE, DBG_IOCTL, "%s exit (Count: %d)!\n", __func__, Count);
	return status;
}

void
NTAPI
HDA_AsyncWait(WDFWORKITEM WorkItem) {
	PHDA_ASYNC_CONTEXT workItemContext = HDAAsyncWorkItem_GetContext(WorkItem);

	SklHdAudBusPrint(DEBUG_LEVEL_VERBOSE, DBG_IOCTL, "%s called (Count: %d)!\n", __func__, workItemContext->Count);

	NTSTATUS status = HDA_WaitForTransfer(
		workItemContext->devData->FdoContext,
		(UINT16)workItemContext->devData->CodecIds.CodecAddress,
		workItemContext->Count,
		workItemContext->CodecTransfer
	);
	UNREFERENCED_PARAMETER(status);

	HDAReleaseCodecVerbs(workItemContext->devData->FdoContext,
		(UINT16)workItemContext->devData->CodecIds.CodecAddress);

	workItemContext->Callback(
		workItemContext->CodecTransfer,
		workItemContext->CallbackContext
	);

	PFDO_CONTEXT fdoCtx = workItemContext->devData->FdoContext;
	WdfDeviceResumeIdle(fdoCtx->WdfDevice);
	WdfObjectDelete(WorkItem);

	SklHdAudBusPrint(DEBUG_LEVEL_VERBOSE, DBG_IOCTL, "%s exit (Count: %d)!\n", __func__);
}

NTSTATUS
NTAPI
HDA_TransferCodecVerbs(
	_In_ PVOID _context,
	_In_ ULONG Count,
	_Inout_updates_(Count)
	PHDAUDIO_CODEC_TRANSFER CodecTransfer,
	_In_opt_ PHDAUDIO_TRANSFER_COMPLETE_CALLBACK Callback,
	_In_opt_ PVOID Context
) {
	SklHdAudBusPrint(DEBUG_LEVEL_VERBOSE, DBG_IOCTL, "%s called (Count: %d, Callback? %d)!\n", __func__, Count, Callback != NULL);

	if (!_context)
		return STATUS_NO_SUCH_DEVICE;

	NTSTATUS status = STATUS_SUCCESS;

	PPDO_DEVICE_DATA devData = (PPDO_DEVICE_DATA)_context;
	if (!devData->FdoContext) {
		return STATUS_NO_SUCH_DEVICE;
	}

	PFDO_CONTEXT fdoCtx = devData->FdoContext;
	if (devData->CodecIds.IsGraphicsCodec) {
		if (!fdoCtx->GraphicsCodecConnected) {
			SklHdAudBusPrint(DEBUG_LEVEL_VERBOSE, DBG_IOCTL, "Gfx codec 0x%x disconnected! Not transferring.\n", devData->CodecIds.CodecAddress);
			return STATUS_DEVICE_NOT_CONNECTED;
		}
	}

	status = WdfDeviceStopIdle(devData->FdoContext->WdfDevice, TRUE);
	if (!NT_SUCCESS(status)) {
		return status;
	}

	UINT16 codecAddr = (UINT16)devData->CodecIds.CodecAddress;
	HDAAcquireCodecVerbs(fdoCtx, codecAddr);

	status = SendHDACmds(fdoCtx, Count, CodecTransfer);
	if (!NT_SUCCESS(status)) {
		HDAReleaseCodecVerbs(fdoCtx, codecAddr);
		goto out;
	}

	if (Callback) { //TODO: Do this async
		WDF_WORKITEM_CONFIG workItemConfig;
		WDF_WORKITEM_CONFIG_INIT(&workItemConfig, HDA_AsyncWait);

		WDF_OBJECT_ATTRIBUTES attributes;
		WDFWORKITEM workItem;

		WDF_OBJECT_ATTRIBUTES_INIT(&attributes);
		WDF_OBJECT_ATTRIBUTES_SET_CONTEXT_TYPE(
			&attributes,
			HDA_ASYNC_CONTEXT
		);
		attributes.ParentObject = fdoCtx->WdfDevice;

		status = WdfWorkItemCreate(&workItemConfig, &attributes, &workItem);
		if (!NT_SUCCESS(status)) {
			HDA_WaitForTransfer(fdoCtx, codecAddr, Count, CodecTransfer);
			HDAReleaseCodecVerbs(fdoCtx, codecAddr);
			WdfDeviceResumeIdle(fdoCtx->WdfDevice);
			goto out;
		}

		PHDA_ASYNC_CONTEXT workItemContext = HDAAsyncWorkItem_GetContext(workItem);
		workItemContext->devData = devData;
		workItemContext->Count = Count;
		workItemContext->CodecTransfer = CodecTransfer;
		workItemContext->Callback = Callback;
		workItemContext->CallbackContext = Context;
		WdfWorkItemEnqueue(workItem);
	}
	else {
		status = HDA_WaitForTransfer(fdoCtx, codecAddr, Count, CodecTransfer);
		HDAReleaseCodecVerbs(fdoCtx, codecAddr);
		if (!NT_SUCCESS(status)) {
			goto out;
		}
	}

	status = STATUS_SUCCESS;

out:
	if (!Callback)
		WdfDeviceResumeIdle(fdoCtx->WdfDevice);
	SklHdAudBusPrint(DEBUG_LEVEL_VERBOSE, DBG_IOCTL, "%s exit (Count: %d)!\n", __func__, Count);
	return status;
}

NTSTATUS
NTAPI
HDA_AllocateCaptureDmaEngine(
	_In_ PVOID _context,
	_In_ UCHAR CodecAddress,
	_In_ PHDAUDIO_STREAM_FORMAT StreamFormat,
	_Out_ PHANDLE Handle,
	_Out_ PHDAUDIO_CONVERTER_FORMAT ConverterFormat
) {
	UNREFERENCED_PARAMETER(CodecAddress);

	if (KeGetCurrentIrql() > PASSIVE_LEVEL) {
		return STATUS_UNSUCCESSFUL;
	}

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

		stream->PdoContext = devData;
		stream->running = FALSE;
		stream->streamFormat = *StreamFormat;

		ConverterFormat->ConverterFormat = hdac_format(stream);

		if (Handle)
			*Handle = (HANDLE)stream;

		WdfInterruptReleaseLock(devData->FdoContext->Interrupt);
		return STATUS_SUCCESS;
	}

	WdfInterruptReleaseLock(devData->FdoContext->Interrupt);
	WdfDeviceResumeIdle(devData->FdoContext->WdfDevice);
	return STATUS_INSUFFICIENT_RESOURCES;
}

NTSTATUS
NTAPI
HDA_AllocateRenderDmaEngine(
	_In_ PVOID _context,
	_In_ PHDAUDIO_STREAM_FORMAT StreamFormat,
	_In_ BOOLEAN Stripe,
	_Out_ PHANDLE Handle,
	_Out_ PHDAUDIO_CONVERTER_FORMAT ConverterFormat
) {
	if (KeGetCurrentIrql() > PASSIVE_LEVEL) {
		return STATUS_UNSUCCESSFUL;
	}

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

		stream->stripe = Stripe;
		stream->PdoContext = devData;
		stream->running = FALSE;
		stream->streamFormat = *StreamFormat;

		ConverterFormat->ConverterFormat = hdac_format(stream);

		if (Handle)
			*Handle = (HANDLE)stream;

		WdfInterruptReleaseLock(devData->FdoContext->Interrupt);
		return STATUS_SUCCESS;
	}

	WdfInterruptReleaseLock(devData->FdoContext->Interrupt);
	WdfDeviceResumeIdle(devData->FdoContext->WdfDevice);
	return STATUS_INSUFFICIENT_RESOURCES;
}

NTSTATUS
NTAPI
HDA_ChangeBandwidthAllocation(
	_In_ PVOID _context,
	_In_ HANDLE Handle,
	_In_ PHDAUDIO_STREAM_FORMAT StreamFormat,
	_Out_ PHDAUDIO_CONVERTER_FORMAT ConverterFormat
) {
	if (KeGetCurrentIrql() > PASSIVE_LEVEL) {
		return STATUS_UNSUCCESSFUL;
	}

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

	stream->streamFormat = *StreamFormat;
	ConverterFormat->ConverterFormat = hdac_format(stream);

	hdac_stream_setup(stream);

	WdfInterruptReleaseLock(devData->FdoContext->Interrupt);

	SklHdAudBusPrint(DEBUG_LEVEL_VERBOSE, DBG_IOCTL, "%s called!\n", __func__);

	return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
HDA_FreeDmaEngine(
	_In_ PVOID _context,
	_In_ HANDLE Handle
) {
	if (KeGetCurrentIrql() > PASSIVE_LEVEL) {
		return STATUS_UNSUCCESSFUL;
	}

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

	stream->PdoContext = NULL;
	/* the stream goes back to the pool and forgets the events its owner registered on it, as a
	 * stream of the DSP child does (ADSPFreeStream, N10-01): the codec's client (portcls) unregisters
	 * its pin's notification event at the pin's close, before the stream is freed, so no freed event is
	 * left on the record today, but that is the client's doing and not the bus's; a record that holds
	 * no event whatever its owner did is the bus's own rule, set under the interrupt lock the DPC
	 * reads the events under (hdac_controller.cpp hda_dpc) */
	RtlZeroMemory(stream->registeredEvents, sizeof(stream->registeredEvents));
	InterlockedExchange(&stream->notificationPending, FALSE);
	WdfInterruptReleaseLock(devData->FdoContext->Interrupt);

	WdfDeviceResumeIdle(devData->FdoContext->WdfDevice);

	return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
HDA_SetDmaEngineState(
	_In_ PVOID _context,
	_In_ HDAUDIO_STREAM_STATE StreamState,
	_In_ ULONG NumberOfHandles,
	_In_reads_(NumberOfHandles) PHANDLE Handles
) {
	PPDO_DEVICE_DATA devData = (PPDO_DEVICE_DATA)_context;
	if (!devData->FdoContext) {
		return STATUS_NO_SUCH_DEVICE;
	}

	for (ULONG i = 0; i < NumberOfHandles; i++) {
		PHDAC_STREAM stream = HdaStreamOf(devData, Handles[i]);
		if (stream == NULL) {
			return STATUS_INVALID_HANDLE;
		}

		hdac_stream_set_state(stream, StreamState);
	}

	return STATUS_SUCCESS;
}

VOID
NTAPI
HDA_GetWallClockRegister(
	_In_ PVOID _context,
	_Out_ PULONG* Wallclock
) {
	PPDO_DEVICE_DATA devData = (PPDO_DEVICE_DATA)_context;
	/* the wall clock is a BAR0 register: none is handed out while the controller is not started (R5-02) */
	*Wallclock = NULL;
	if (!HdaControllerStarted(devData->FdoContext)) {
		return;
	}
	*Wallclock = (ULONG *)((devData->FdoContext)->m_BAR0.Base.baseptr + HDA_REG_WALLCLKA);
}

NTSTATUS
NTAPI
HDA_GetLinkPositionRegister(
	_In_ PVOID _context,
	_In_ HANDLE Handle,
	_Out_ PULONG* Position
) {
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

NTSTATUS
NTAPI
HDA_RegisterEventCallback(
	_In_ PVOID _context,
	_In_ PHDAUDIO_UNSOLICITED_RESPONSE_CALLBACK Routine,
	_In_opt_ PVOID Context,
	_Out_ PUCHAR Tag
) {
	if (!_context)
		return STATUS_NO_SUCH_DEVICE;


	PPDO_DEVICE_DATA devData = (PPDO_DEVICE_DATA)_context;
	if (devData->FdoContext) {
		WdfInterruptAcquireLock(devData->FdoContext->Interrupt);
	}

	for (UINT8 i = 0; i < MAX_UNSOLICIT_CALLBACKS; i++) {
		if (devData->unsolitCallbacks[i].inUse)
			continue;

		if (Tag)
			*Tag = i;

		devData->unsolitCallbacks[i].inUse = TRUE;
		devData->unsolitCallbacks[i].Context = Context;
		devData->unsolitCallbacks[i].Routine = Routine;

		if (devData->FdoContext) {
			WdfInterruptReleaseLock(devData->FdoContext->Interrupt);
		}
		return STATUS_SUCCESS;
	}

	if (devData->FdoContext) {
		WdfInterruptReleaseLock(devData->FdoContext->Interrupt);
	}
	return STATUS_INSUFFICIENT_RESOURCES;
}

NTSTATUS
NTAPI
HDA_UnregisterEventCallback(
	_In_ PVOID _context,
	_In_ UCHAR Tag
) {
	if (!_context)
		return STATUS_NO_SUCH_DEVICE;

	PPDO_DEVICE_DATA devData = (PPDO_DEVICE_DATA)_context;
	// U8: the tag indexes the table of callbacks, and is the caller's
	if (Tag >= MAX_UNSOLICIT_CALLBACKS) {
		return STATUS_INVALID_PARAMETER;
	}
	if (!devData->unsolitCallbacks[Tag].inUse) {
		return STATUS_NOT_FOUND;
	}

	if (devData->FdoContext) {
		WdfInterruptAcquireLock(devData->FdoContext->Interrupt);
	}

	devData->unsolitCallbacks[Tag].Routine = NULL;
	devData->unsolitCallbacks[Tag].Context = NULL;
	devData->unsolitCallbacks[Tag].inUse = FALSE;

	if (devData->FdoContext) {
		WdfInterruptReleaseLock(devData->FdoContext->Interrupt);
	}

	return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
HDA_GetDeviceInformation(
	_In_ PVOID _context,
	_Inout_ PHDAUDIO_DEVICE_INFORMATION DeviceInformation
) {
	if (!_context)
		return STATUS_NO_SUCH_DEVICE;

	PPDO_DEVICE_DATA devData = (PPDO_DEVICE_DATA)_context;
	if (!devData->FdoContext) {
		return STATUS_NO_SUCH_DEVICE;
	}

	if (DeviceInformation->Size >= sizeof(HDAUDIO_DEVICE_INFORMATION)) {
		DeviceInformation->CodecsDetected = devData->FdoContext->numCodecs;
		DeviceInformation->DeviceVersion = devData->FdoContext->hwVersion;
		DeviceInformation->DriverVersion = 0x100;
		DeviceInformation->IsStripingSupported = TRUE;
	}
	if (DeviceInformation->Size >= sizeof(HDAUDIO_DEVICE_INFORMATION_V2)) {
		PHDAUDIO_DEVICE_INFORMATION_V2 DeviceInformationV2 = (PHDAUDIO_DEVICE_INFORMATION_V2)DeviceInformation;
		DeviceInformationV2->CtrlRevision = devData->FdoContext->revId;
		DeviceInformationV2->CtrlVendorId = devData->FdoContext->venId;
		DeviceInformationV2->CtrlDeviceId = devData->FdoContext->devId;
	}

	return STATUS_SUCCESS;
}

void
NTAPI
HDA_GetResourceInformation(
	_In_ PVOID _context,
	_Out_ PUCHAR CodecAddress,
	_Out_ PUCHAR FunctionGroupStartNode
) {
	if (!_context)
		return;
	
	PPDO_DEVICE_DATA devData = (PPDO_DEVICE_DATA)_context;
	if (CodecAddress)
		*CodecAddress = (UINT8)devData->CodecIds.CodecAddress;
	if (FunctionGroupStartNode)
		*FunctionGroupStartNode = devData->CodecIds.FunctionGroupStartNode;
}

NTSTATUS
NTAPI
HDA_AllocateDmaBufferWithNotification(
	_In_ PVOID _context,
	_In_ HANDLE Handle,
	_In_ ULONG NotificationCount,
	_In_ SIZE_T RequestedBufferSize,
	_Out_ PMDL* BufferMdl,
	_Out_ PSIZE_T AllocatedBufferSize,
	_Out_ PSIZE_T OffsetFromFirstPage,
	_Out_ PUCHAR StreamId,
	_Out_ PULONG FifoSize
) {
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

NTSTATUS
NTAPI
HDA_FreeDmaBufferWithNotification(
	_In_ PVOID _context,
	_In_ HANDLE Handle,
	_In_ PMDL BufferMdl,
	_In_ SIZE_T BufferSize
) {
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

NTSTATUS
NTAPI
HDA_AllocateDmaBuffer(
	_In_ PVOID _context,
	_In_ HANDLE Handle,
	_In_ SIZE_T RequestedBufferSize,
	_Out_ PMDL* BufferMdl,
	_Out_ PSIZE_T AllocatedBufferSize,
	_Out_ PUCHAR StreamId,
	_Out_ PULONG FifoSize
) {
	SIZE_T OffsetFromFirstPage;
	return HDA_AllocateDmaBufferWithNotification(_context, Handle, 1, RequestedBufferSize, BufferMdl, AllocatedBufferSize, &OffsetFromFirstPage, StreamId, FifoSize);
}

NTSTATUS
NTAPI
HDA_FreeDmaBuffer(
	_In_ PVOID _context,
	_In_ HANDLE Handle
) {
	PPDO_DEVICE_DATA devData = (PPDO_DEVICE_DATA)_context;
	if (!devData->FdoContext) {
		return STATUS_NO_SUCH_DEVICE;
	}

	PHDAC_STREAM stream = HdaStreamOf(devData, Handle);
	if (stream == NULL) {
		return STATUS_INVALID_HANDLE;
	}

	return HDA_FreeDmaBufferWithNotification(_context, Handle, stream->mdlBuf, stream->bufSz);
}

NTSTATUS
NTAPI
HDA_RegisterNotificationEvent(
	_In_ PVOID _context,
	_In_ HANDLE Handle,
	_In_ PKEVENT NotificationEvent
) {
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

NTSTATUS
NTAPI
HDA_UnregisterNotificationEvent(
	_In_ PVOID _context,
	_In_ HANDLE Handle,
	_In_ PKEVENT NotificationEvent
) {
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

NTSTATUS
NTAPI
HDA_RegisterNotificationCallback(
	_In_ PVOID _context,
	_In_ HANDLE Handle,
	PDEVICE_OBJECT Fdo,
	PHDAUDIO_DMA_NOTIFICATION_CALLBACK NotificationCallback,
	PVOID CallbackContext
) {
	PPDO_DEVICE_DATA devData = (PPDO_DEVICE_DATA)_context;
	if (!devData->FdoContext) {
		return STATUS_NO_SUCH_DEVICE;
	}

	PHDAC_STREAM stream = HdaStreamOf(devData, Handle);
	if (stream == NULL) {
		return STATUS_INVALID_HANDLE;
	}

	BOOL registered = FALSE;

	for (int i = 0; i < MAX_NOTIF_EVENTS; i++) {
		if (stream->registeredCallbacks[i].InUse)
			continue;
		stream->registeredCallbacks[i].InUse = TRUE;
		stream->registeredCallbacks[i].Fdo = Fdo;
		stream->registeredCallbacks[i].NotificationCallback = NotificationCallback;
		stream->registeredCallbacks[i].CallbackContext = CallbackContext;

		InterlockedIncrement(&Fdo->ReferenceCount);

		registered = true;
		break;
	}

	return registered ? STATUS_SUCCESS : STATUS_INSUFFICIENT_RESOURCES;
}

NTSTATUS
NTAPI
HDA_UnregisterNotificationCallback(
	_In_ PVOID _context,
	_In_ HANDLE Handle,
	PHDAUDIO_DMA_NOTIFICATION_CALLBACK NotificationCallback,
	PVOID CallbackContext
) {
	PPDO_DEVICE_DATA devData = (PPDO_DEVICE_DATA)_context;
	if (!devData->FdoContext) {
		return STATUS_NO_SUCH_DEVICE;
	}

	PHDAC_STREAM stream = HdaStreamOf(devData, Handle);
	if (stream == NULL) {
		return STATUS_INVALID_HANDLE;
	}

	BOOL registered = FALSE;

	for (int i = 0; i < MAX_NOTIF_EVENTS; i++) {
		// U5: the entry to unregister is one in use whose callback and whose context both match; a free entry is never one
		if (!stream->registeredCallbacks[i].InUse ||
			stream->registeredCallbacks[i].NotificationCallback != NotificationCallback ||
			stream->registeredCallbacks[i].CallbackContext != CallbackContext)
			continue;

		InterlockedDecrement(&stream->registeredCallbacks[i].Fdo->ReferenceCount);

		RtlZeroMemory(&stream->registeredCallbacks[i], sizeof(stream->registeredCallbacks[i]));
		registered = true;
		break;
	}

	return registered ? STATUS_SUCCESS : STATUS_INVALID_PARAMETER;
}

NTSTATUS
NTAPI
HDA_SetupDmaEngineWithBdl(
	_In_ PVOID _context,
	_In_ HANDLE Handle,
	_In_ ULONG BufferLength,
	_In_ ULONG Lvi,
	_In_ PHDAUDIO_BDL_ISR Isr,
	_In_ PVOID Context,
	_Out_ PUCHAR StreamId,
	_Out_ PULONG FifoSize)
{
	if (KeGetCurrentIrql() > PASSIVE_LEVEL) {
		return STATUS_UNSUCCESSFUL;
	}

	PPDO_DEVICE_DATA devData = (PPDO_DEVICE_DATA)_context;
	if (!devData->FdoContext) {
		return STATUS_NO_SUCH_DEVICE;
	}

	PHDAC_STREAM stream = HdaStreamOf(devData, Handle);
	if (stream == NULL) {
		return STATUS_INVALID_HANDLE;
	}

	if (stream->running) {
		return STATUS_INVALID_DEVICE_REQUEST;
	}

	if (!stream->bdl) {
		return STATUS_INVALID_DEVICE_REQUEST;
	}

	if (!stream->dmaBuf) {
		return STATUS_INVALID_DEVICE_REQUEST;
	}

	WdfInterruptAcquireLock(devData->FdoContext->Interrupt);

	stream->bufSz = BufferLength;
	stream->numBlocks = (UINT16)(Lvi + 1);

	RtlZeroMemory(&stream->isr, sizeof(HDAC_ISR_CALLBACK));
	stream->isr.IOC = TRUE;
	stream->isr.IsrCallback = Isr;
	stream->isr.CallbackContext = Context;

	hdac_stream_reset(stream);
	hdac_stream_setup(stream);

	WdfInterruptReleaseLock(devData->FdoContext->Interrupt);

	*StreamId = stream->streamTag;
	*FifoSize = stream->fifoSize;

	return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
HDA_FreeContiguousDmaBuffer(
	_In_ PVOID _context,
	_In_ HANDLE Handle)
{
	if (KeGetCurrentIrql() > PASSIVE_LEVEL) {
		return STATUS_UNSUCCESSFUL;
	}

	PPDO_DEVICE_DATA devData = (PPDO_DEVICE_DATA)_context;
	if (!devData->FdoContext) {
		return STATUS_NO_SUCH_DEVICE;
	}

	PHDAC_STREAM stream = HdaStreamOf(devData, Handle);
	if (stream == NULL) {
		return STATUS_INVALID_HANDLE;
	}

	if (stream->running) {
		return STATUS_INVALID_DEVICE_REQUEST;
	}

	if (!stream->bdl) {
		return STATUS_INVALID_DEVICE_REQUEST;
	}

	if (!stream->dmaBuf) {
		return STATUS_INVALID_DEVICE_REQUEST;
	}

	WdfInterruptAcquireLock(devData->FdoContext->Interrupt);

	stream_write32(stream, SD_BDLPL, 0);
	stream_write32(stream, SD_BDLPU, 0);
	stream_write32(stream, SD_CTL, 0);

	RtlZeroMemory(&stream->isr, sizeof(HDAC_ISR_CALLBACK));

	PVOID dmaBuf = stream->dmaBuf;
	stream->dmaBuf = NULL;

	WdfInterruptReleaseLock(devData->FdoContext->Interrupt);

	MmFreeContiguousMemory(dmaBuf);

	return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
HDA_AllocateContiguousDmaBuffer(
	_In_ PVOID _context,
	_In_ HANDLE Handle,
	_In_ ULONG RequestedBufferSize,
	_Out_ PVOID* DataBuffer,
	_Out_ PHDAUDIO_BUFFER_DESCRIPTOR* BdlBuffer)
{
	if (KeGetCurrentIrql() > PASSIVE_LEVEL) {
		return STATUS_UNSUCCESSFUL;
	}

	PPDO_DEVICE_DATA devData = (PPDO_DEVICE_DATA)_context;
	if (!devData->FdoContext) {
		return STATUS_NO_SUCH_DEVICE;
	}

	PHDAC_STREAM stream = HdaStreamOf(devData, Handle);
	if (stream == NULL) {
		return STATUS_INVALID_HANDLE;
	}

	if (stream->running) {
		return STATUS_INVALID_DEVICE_REQUEST;
	}

	if (stream->dmaBuf) {
		return STATUS_INVALID_DEVICE_REQUEST;
	}

	if (!stream->bdl) {
		return STATUS_INVALID_DEVICE_REQUEST;
	}

    PHYSICAL_ADDRESS maxAddr;
    maxAddr.QuadPart = devData->FdoContext->is64BitOK ? MAXULONG64 : MAXULONG32;

    PVOID dmaBuf = MmAllocateContiguousMemory(RequestedBufferSize, maxAddr);
    if (!dmaBuf) {
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    RtlZeroMemory(dmaBuf, RequestedBufferSize);

	WdfInterruptAcquireLock(devData->FdoContext->Interrupt);

    stream->dmaBuf = dmaBuf;

	WdfInterruptReleaseLock(devData->FdoContext->Interrupt);

	*DataBuffer = stream->dmaBuf;
	*BdlBuffer = (PHDAUDIO_BUFFER_DESCRIPTOR)stream->bdl;

	return STATUS_SUCCESS;
}

HDAUDIO_BUS_INTERFACE_V2 HDA_BusInterfaceV2(PVOID Context) {
	HDAUDIO_BUS_INTERFACE_V2 busInterface;
	RtlZeroMemory(&busInterface, sizeof(HDAUDIO_BUS_INTERFACE_V2));

	busInterface.Size = sizeof(HDAUDIO_BUS_INTERFACE_V2);
	busInterface.Version = 0x0100;
	busInterface.Context = Context;
	busInterface.InterfaceReference = WdfDeviceInterfaceReferenceNoOp;
	busInterface.InterfaceDereference = WdfDeviceInterfaceDereferenceNoOp;
	busInterface.TransferCodecVerbs = HDA_TransferCodecVerbs;
	busInterface.AllocateCaptureDmaEngine = HDA_AllocateCaptureDmaEngine;
	busInterface.AllocateRenderDmaEngine = HDA_AllocateRenderDmaEngine;
	busInterface.ChangeBandwidthAllocation = HDA_ChangeBandwidthAllocation;
	busInterface.AllocateDmaBuffer = HDA_AllocateDmaBuffer;
	busInterface.FreeDmaBuffer = HDA_FreeDmaBuffer;
	busInterface.FreeDmaEngine = HDA_FreeDmaEngine;
	busInterface.SetDmaEngineState = HDA_SetDmaEngineState;
	busInterface.GetWallClockRegister = HDA_GetWallClockRegister;
	busInterface.GetLinkPositionRegister = HDA_GetLinkPositionRegister;
	busInterface.RegisterEventCallback = HDA_RegisterEventCallback;
	busInterface.UnregisterEventCallback = HDA_UnregisterEventCallback;
	busInterface.GetDeviceInformation = HDA_GetDeviceInformation;
	busInterface.GetResourceInformation = HDA_GetResourceInformation;
	busInterface.AllocateDmaBufferWithNotification = HDA_AllocateDmaBufferWithNotification;
	busInterface.FreeDmaBufferWithNotification = HDA_FreeDmaBufferWithNotification;
	busInterface.RegisterNotificationEvent = HDA_RegisterNotificationEvent;
	busInterface.UnregisterNotificationEvent = HDA_UnregisterNotificationEvent;

	return busInterface;
}

HDAUDIO_BUS_INTERFACE_V3 HDA_BusInterfaceV3(PVOID Context) {
	HDAUDIO_BUS_INTERFACE_V3 busInterface;
	RtlZeroMemory(&busInterface, sizeof(HDAUDIO_BUS_INTERFACE_V3));

	busInterface.Size = sizeof(HDAUDIO_BUS_INTERFACE_V3);
	busInterface.Version = 0x0100;
	busInterface.Context = Context;
	busInterface.InterfaceReference = WdfDeviceInterfaceReferenceNoOp;
	busInterface.InterfaceDereference = WdfDeviceInterfaceDereferenceNoOp;
	busInterface.TransferCodecVerbs = HDA_TransferCodecVerbs;
	busInterface.AllocateCaptureDmaEngine = HDA_AllocateCaptureDmaEngine;
	busInterface.AllocateRenderDmaEngine = HDA_AllocateRenderDmaEngine;
	busInterface.ChangeBandwidthAllocation = HDA_ChangeBandwidthAllocation;
	busInterface.AllocateDmaBuffer = HDA_AllocateDmaBuffer;
	busInterface.FreeDmaBuffer = HDA_FreeDmaBuffer;
	busInterface.FreeDmaEngine = HDA_FreeDmaEngine;
	busInterface.SetDmaEngineState = HDA_SetDmaEngineState;
	busInterface.GetWallClockRegister = HDA_GetWallClockRegister;
	busInterface.GetLinkPositionRegister = HDA_GetLinkPositionRegister;
	busInterface.RegisterEventCallback = HDA_RegisterEventCallback;
	busInterface.UnregisterEventCallback = HDA_UnregisterEventCallback;
	busInterface.GetDeviceInformation = HDA_GetDeviceInformation;
	busInterface.GetResourceInformation = HDA_GetResourceInformation;
	busInterface.AllocateDmaBufferWithNotification = HDA_AllocateDmaBufferWithNotification;
	busInterface.FreeDmaBufferWithNotification = HDA_FreeDmaBufferWithNotification;
	busInterface.RegisterNotificationEvent = HDA_RegisterNotificationEvent;
	busInterface.UnregisterNotificationEvent = HDA_UnregisterNotificationEvent;
	busInterface.RegisterNotificationCallback = HDA_RegisterNotificationCallback;
	busInterface.UnregisterNotificationCallback = HDA_UnregisterNotificationCallback;

	return busInterface;
}

HDAUDIO_BUS_INTERFACE HDA_BusInterface(PVOID Context) {
	HDAUDIO_BUS_INTERFACE busInterface;
	RtlZeroMemory(&busInterface, sizeof(HDAUDIO_BUS_INTERFACE));

	busInterface.Size = sizeof(HDAUDIO_BUS_INTERFACE);
	busInterface.Version = 0x0100;
	busInterface.Context = Context;
	busInterface.InterfaceReference = WdfDeviceInterfaceReferenceNoOp;
	busInterface.InterfaceDereference = WdfDeviceInterfaceDereferenceNoOp;
	busInterface.TransferCodecVerbs = HDA_TransferCodecVerbs;
	busInterface.AllocateCaptureDmaEngine = HDA_AllocateCaptureDmaEngine;
	busInterface.AllocateRenderDmaEngine = HDA_AllocateRenderDmaEngine;
	busInterface.ChangeBandwidthAllocation = HDA_ChangeBandwidthAllocation;
	busInterface.AllocateDmaBuffer = HDA_AllocateDmaBuffer;
	busInterface.FreeDmaBuffer = HDA_FreeDmaBuffer;
	busInterface.FreeDmaEngine = HDA_FreeDmaEngine;
	busInterface.SetDmaEngineState = HDA_SetDmaEngineState;
	busInterface.GetWallClockRegister = HDA_GetWallClockRegister;
	busInterface.GetLinkPositionRegister = HDA_GetLinkPositionRegister;
	busInterface.RegisterEventCallback = HDA_RegisterEventCallback;
	busInterface.UnregisterEventCallback = HDA_UnregisterEventCallback;
	busInterface.GetDeviceInformation = HDA_GetDeviceInformation;
	busInterface.GetResourceInformation = HDA_GetResourceInformation;

	return busInterface;
}

HDAUDIO_BUS_INTERFACE_BDL HDA_BusInterfaceBDL(PVOID Context)
{
	HDAUDIO_BUS_INTERFACE_BDL busInterface;
	RtlZeroMemory(&busInterface, sizeof(HDAUDIO_BUS_INTERFACE_BDL));

	busInterface.Size = sizeof(HDAUDIO_BUS_INTERFACE_BDL);
	busInterface.Version = 0x0100;
	busInterface.Context = Context;
	busInterface.InterfaceReference = WdfDeviceInterfaceReferenceNoOp;
	busInterface.InterfaceDereference = WdfDeviceInterfaceDereferenceNoOp;
	busInterface.TransferCodecVerbs = HDA_TransferCodecVerbs;
	busInterface.AllocateCaptureDmaEngine = HDA_AllocateCaptureDmaEngine;
	busInterface.AllocateRenderDmaEngine = HDA_AllocateRenderDmaEngine;
	busInterface.ChangeBandwidthAllocation = HDA_ChangeBandwidthAllocation;
	busInterface.AllocateContiguousDmaBuffer = HDA_AllocateContiguousDmaBuffer;
	busInterface.SetupDmaEngineWithBdl = HDA_SetupDmaEngineWithBdl;
	busInterface.FreeContiguousDmaBuffer = HDA_FreeContiguousDmaBuffer;
	busInterface.FreeDmaEngine = HDA_FreeDmaEngine;
	busInterface.SetDmaEngineState = HDA_SetDmaEngineState;
	busInterface.GetWallClockRegister = HDA_GetWallClockRegister;
	busInterface.GetLinkPositionRegister = HDA_GetLinkPositionRegister;
	busInterface.RegisterEventCallback = HDA_RegisterEventCallback;
	busInterface.UnregisterEventCallback = HDA_UnregisterEventCallback;
	busInterface.GetDeviceInformation = HDA_GetDeviceInformation;
	busInterface.GetResourceInformation = HDA_GetResourceInformation;

	return busInterface;
}
