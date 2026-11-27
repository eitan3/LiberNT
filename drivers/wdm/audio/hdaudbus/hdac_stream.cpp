#include "driver.h"

void hdac_stream_start(PHDAC_STREAM stream) {
	hda_read32(stream->FdoContext, WALLCLK);

	/* enable SIE */
	hda_update32(stream->FdoContext, INTCTL, 1 << stream->idx, 1 << stream->idx);

	/* set stripe control */
	if (stream->stripe) {
		UINT8 stripe_ctl = 1;
		stream_update8(stream, SD_CTL_3B, SD_CTL_STRIPE_MASK,
			stripe_ctl);
	}

	/* set DMA start and interrupt mask */
	stream_update8(stream, SD_CTL, 0, SD_CTL_DMA_START | SD_INT_MASK);
	stream->running = TRUE;
}

void hdac_stream_clear(PHDAC_STREAM stream) {
	stream_update8(stream, SD_CTL, SD_CTL_DMA_START | SD_INT_MASK, 0);
	stream_write8(stream, SD_STS, SD_INT_MASK); /* to be sure */
	if (stream->stripe)
		stream_update8(stream, SD_CTL_3B, SD_CTL_STRIPE_MASK, 0);

	stream->running = FALSE;
}

void hdac_stream_stop(PHDAC_STREAM stream) {
	hdac_stream_clear(stream);

	/* disable SIE */
	hda_update32(stream->FdoContext, INTCTL, 1 << stream->idx, 0);
}

void hdac_stream_reset(PHDAC_STREAM stream) {
	hdac_stream_clear(stream);

	stream_update8(stream, SD_CTL, 0, SD_CTL_STREAM_RESET);
	KeStallExecutionProcessor(3);
	int timeout = 300;

	UCHAR val;
	do {
		val = stream_read8(stream, SD_CTL) & SD_CTL_STREAM_RESET;
		if (val)
			break;
	} while (--timeout);

	val &= ~SD_CTL_STREAM_RESET;
	stream_write8(stream, SD_CTL, val);
	KeStallExecutionProcessor(3);

	timeout = 300;
	/* waiting for hardware to report that the stream is out of reset */
	do {
		val = stream_read8(stream, SD_CTL) & SD_CTL_STREAM_RESET;
		if (!val)
			break;
	} while (--timeout);

	if (stream->posbuf)
		*stream->posbuf = 0;
}

UINT16 hdac_format(PHDAC_STREAM stream) {
	UINT16 format = 0;

	switch (stream->streamFormat.SampleRate) {
	case 8000:
		format = HDA_RATE(48, 1, 6);
		break;
	case 9600:
		format = HDA_RATE(48, 1, 5);
		break;
	case 11025:
		format = HDA_RATE(44, 1, 4);
		break;
	case 16000:
		format = HDA_RATE(48, 1, 3);
		break;
	case 22050:
		format = HDA_RATE(44, 1, 2);
		break;
	case 32000:
		format = HDA_RATE(48, 2, 3);
		break;
	case 44100:
		format = HDA_RATE(44, 1, 1);
		break;
	case 48000:
		format = HDA_RATE(48, 1, 1);
		break;
	case 88200:
		format = HDA_RATE(44, 2, 1);
		break;
	case 96000:
		format = HDA_RATE(48, 2, 1);
		break;
	case 176400:
		format = HDA_RATE(44, 4, 1);
		break;
	case 192000:
		format = HDA_RATE(48, 4, 1);
		break;
	case 384000:
		format = HDA_RATE(48, 8, 1);
		break;
	}

	{
		UINT16 channels = stream->streamFormat.NumberOfChannels;
		if (channels == 0 || channels > 8)
			return 0;
		format |= channels - 1;

		switch (stream->streamFormat.ValidBitsPerSample) {
		case 8:
			format |= AC_FMT_BITS_8;
			break;
		case 16:
			format |= AC_FMT_BITS_16;
			break;
		case 20:
			format |= AC_FMT_BITS_20;
			break;
		case 24:
			format |= AC_FMT_BITS_24;
			break;
		case 32:
			format |= AC_FMT_BITS_32;
			break;
		}
	}
	return format;
}

void hdac_stream_setup(PHDAC_STREAM stream) {
	/* make sure the run bit is zero for SD */
	hdac_stream_clear(stream);

	UINT val;
	/* program the stream_tag */
	val = stream_read32(stream, SD_CTL);
	val = (val & ~SD_CTL_STREAM_TAG_MASK) |
		(stream->streamTag << SD_CTL_STREAM_TAG_SHIFT);
	stream_write32(stream, SD_CTL, val);

	/* program the length of samples in cyclic buffer */
	stream_write32(stream, SD_CBL, stream->bufSz);

	/* program the stream format */
	/* this value needs to be the same as the one programmed */
	UINT16 format = hdac_format(stream);
	stream_write16(stream, SD_FORMAT, format);

	/* program the stream LVI (last valid index) of the BDL */
	stream_write16(stream, SD_LVI, stream->numBlocks - 1);

	/* program the BDL address */
	/* lower BDL address */
	PHYSICAL_ADDRESS bdlAddr = MmGetPhysicalAddress(stream->bdl);
	stream_write32(stream, SD_BDLPL, bdlAddr.LowPart);
	/* upper BDL address */
	stream_write32(stream, SD_BDLPU, bdlAddr.HighPart);

	//Enable position buffer
	if (!(hda_read32(stream->FdoContext, DPLBASE) & HDA_DPLBASE_ENABLE)){
		PHYSICAL_ADDRESS posbufAddr = MmGetPhysicalAddress(stream->FdoContext->posbuf);
		hda_write32(stream->FdoContext, DPLBASE, posbufAddr.LowPart | HDA_DPLBASE_ENABLE);
	}

	/* set the interrupt enable bits in the descriptor control register */
	stream_update32(stream, SD_CTL, 0, SD_INT_MASK);

	stream->fifoSize = stream_read16(stream, SD_FIFOSIZE) + 1;
}

/*
 * Pages for a cyclic buffer, the descriptor list over them and the stream
 * set up for it: one descriptor per page, the interrupt on completion at the
 * end of the buffer, or of each of its two halves when two notifications are
 * asked for (the halves start on a 128-byte frame boundary). The stream is
 * reset and set up, and stays stopped.
 */
NTSTATUS hdac_stream_allocate_buffer(PHDAC_STREAM stream, ULONG notificationCount, SIZE_T requestedBufferSize, PMDL* bufferMdl, SIZE_T* allocatedBufferSize, SIZE_T* offsetFromFirstPage, UCHAR* streamId, PULONG fifoSize) {
	if (KeGetCurrentIrql() > PASSIVE_LEVEL) {
		return STATUS_UNSUCCESSFUL;
	}

	if (stream->running) {
		return STATUS_INVALID_DEVICE_REQUEST;
	}

	if (stream->mdlBuf) {
		return STATUS_INVALID_DEVICE_REQUEST;
	}

	PHYSICAL_ADDRESS zeroAddr;
	zeroAddr.QuadPart = 0;
	PHYSICAL_ADDRESS maxAddr;
	maxAddr.QuadPart = stream->FdoContext->is64BitOK ? MAXULONG64 : MAXULONG32;

	HDAC_BDL_LAYOUT layout;
	HdacBdlLayout(notificationCount, requestedBufferSize,
		(stream->streamFormat.ContainerSize / 8) * stream->streamFormat.NumberOfChannels, &layout);
	UINT32 allocSize = layout.AllocBytes;

	PMDL mdl = MmAllocatePagesForMdl(zeroAddr, maxAddr, zeroAddr, allocSize);
	if (!mdl) {
		return STATUS_NO_MEMORY;
	}
	if (MmGetMdlByteCount(mdl) < allocSize) {
		MmFreePagesFromMdl(mdl);
		ExFreePool(mdl);
		return STATUS_INSUFFICIENT_RESOURCES;
	}

	WdfInterruptAcquireLock(stream->FdoContext->Interrupt);
	stream->mdlBuf = mdl;
	stream->bufSz = layout.BufferBytes;

	*bufferMdl = mdl;
	*allocatedBufferSize = layout.BufferBytes;
	*offsetFromFirstPage = layout.OffsetBytes;
	*streamId = stream->streamTag;

	{
		UINT16 numBlocks;

		//Set up the BDL
		if (!HdacBdlBuild(stream->bdl, MmGetMdlPfnArray(mdl), &layout, &numBlocks))
			SklHdAudBusPrint(DEBUG_LEVEL_ERROR, DBG_IOCTL, "%s Too many BDL entries!\n", __func__);
		stream->numBlocks = numBlocks;
	}

	WdfInterruptReleaseLock(stream->FdoContext->Interrupt);

	hdac_stream_reset(stream);
	hdac_stream_setup(stream);

	*fifoSize = stream->fifoSize;
	return STATUS_SUCCESS;
}

/* The pages of a buffer the stream no longer uses: the descriptor list address and the control register are cleared, as the codec stream's are */
NTSTATUS hdac_stream_free_buffer(PHDAC_STREAM stream) {
	if (KeGetCurrentIrql() > PASSIVE_LEVEL) {
		return STATUS_UNSUCCESSFUL;
	}

	if (stream->running) {
		return STATUS_INVALID_DEVICE_REQUEST;
	}

	if (!stream->mdlBuf) {
		return STATUS_INVALID_DEVICE_REQUEST;
	}

	WdfInterruptAcquireLock(stream->FdoContext->Interrupt);

	stream_write32(stream, SD_BDLPL, 0);
	stream_write32(stream, SD_BDLPU, 0);
	stream_write32(stream, SD_CTL, 0);

	PMDL mdl = stream->mdlBuf;
	stream->mdlBuf = NULL;

	WdfInterruptReleaseLock(stream->FdoContext->Interrupt);

	MmFreePagesFromMdl(mdl);
	ExFreePool(mdl);

	return STATUS_SUCCESS;
}

/*
 * A buffer the stream no longer uses, taken off the stream and handed to
 * the caller with its pages and its MDL as they are: the registers are
 * cleared as the free clears them, under the same lock, and nothing is
 * freed. The DSP driver's stream manager calls this when it must let a
 * stream go while the pin that owns the buffer still maps it, across a
 * power-down or a fault (intcdsp/stream.c, R5-01): the pin's own free
 * returns the pages and the MDL, as the bus's free would. The stream must
 * not run and must hold a buffer, as for the free.
 */
NTSTATUS hdac_stream_detach_buffer(PHDAC_STREAM stream, PMDL* bufferMdl) {
	if (KeGetCurrentIrql() > PASSIVE_LEVEL) {
		return STATUS_UNSUCCESSFUL;
	}

	if (stream->running) {
		return STATUS_INVALID_DEVICE_REQUEST;
	}

	if (!stream->mdlBuf) {
		return STATUS_INVALID_DEVICE_REQUEST;
	}

	*bufferMdl = NULL;
	WdfInterruptAcquireLock(stream->FdoContext->Interrupt);

	stream_write32(stream, SD_BDLPL, 0);
	stream_write32(stream, SD_BDLPU, 0);
	stream_write32(stream, SD_CTL, 0);

	*bufferMdl = stream->mdlBuf;
	stream->mdlBuf = NULL;
	stream->bufSz = 0;

	WdfInterruptReleaseLock(stream->FdoContext->Interrupt);

	return STATUS_SUCCESS;
}

/*
 * Run: the stream is set up once more and started. Pause and stop: it is
 * stopped and a period that was still pending is forgotten. Reset: a stream
 * that is not running is reset and set up.
 */
NTSTATUS hdac_stream_set_state(PHDAC_STREAM stream, HDAUDIO_STREAM_STATE streamState) {
	WdfInterruptAcquireLock(stream->FdoContext->Interrupt);

	if (streamState == RunState && !stream->running) {
		InterlockedExchange(&stream->notificationPending, FALSE);
		hdac_stream_setup(stream);
		hdac_stream_start(stream);
		stream->running = TRUE;
	}
	else if ((streamState == PauseState || streamState == StopState) && stream->running) {
		hdac_stream_stop(stream);
		stream->running = FALSE;
		InterlockedExchange(&stream->notificationPending, FALSE);
	}
	else if (streamState == ResetState) {
		if (!stream->running) {
			hdac_stream_reset(stream);
			hdac_stream_setup(stream);
		}
	}

	WdfInterruptReleaseLock(stream->FdoContext->Interrupt);

	return STATUS_SUCCESS;
}

/* A period's event of the stream, among the MAX_NOTIF_EVENTS the stream holds; the interrupt's DPC sets it */
BOOLEAN hdac_stream_register_event(PHDAC_STREAM stream, PKEVENT notificationEvent) {
	for (int i = 0; i < MAX_NOTIF_EVENTS; i++) {
		if (stream->registeredEvents[i])
			continue;
		stream->registeredEvents[i] = notificationEvent;
		return TRUE;
	}
	return FALSE;
}

BOOLEAN hdac_stream_unregister_event(PHDAC_STREAM stream, PKEVENT notificationEvent) {
	for (int i = 0; i < MAX_NOTIF_EVENTS; i++) {
		if (stream->registeredEvents[i] != notificationEvent)
			continue;
		stream->registeredEvents[i] = NULL;
		return TRUE;
	}
	return FALSE;
}

PULONG hdac_stream_position_register(PHDAC_STREAM stream) {
	return (ULONG*)(stream->sdAddr + HDA_REG_SD_LPIBA);
}

/* HDA_REG_VS_SDXDPIB_XBASE: one read-only register per stream, by the stream's index in the controller */
PULONG hdac_stream_dma_position_register(PHDAC_STREAM stream) {
	return (ULONG*)(stream->FdoContext->m_BAR0.Base.baseptr + HDA_REG_VS_SDXDPIB_XBASE +
		(HDA_REG_VS_SDXDPIB_XINTERVAL * (ULONG)stream->idx));
}
