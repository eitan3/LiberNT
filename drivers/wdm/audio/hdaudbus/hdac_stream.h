UINT16 hdac_format(PHDAC_STREAM stream);

void hdac_stream_start(PHDAC_STREAM stream);
void hdac_stream_stop(PHDAC_STREAM stream);
void hdac_stream_reset(PHDAC_STREAM stream);
void hdac_stream_setup(PHDAC_STREAM stream);

/* The stream routines of the codec interfaces, written once for every stream the controller carries: a codec engine's through HDAUDIO_BUS_INTERFACE, a DSP stream's through the ADSP bus interface. Each takes the stream its caller's handle named, after the caller made sure the handle belongs to it; PASSIVE_LEVEL. */
NTSTATUS hdac_stream_allocate_buffer(PHDAC_STREAM stream, ULONG notificationCount, SIZE_T requestedBufferSize, PMDL* bufferMdl, SIZE_T* allocatedBufferSize, SIZE_T* offsetFromFirstPage, UCHAR* streamId, PULONG fifoSize);
NTSTATUS hdac_stream_free_buffer(PHDAC_STREAM stream);
NTSTATUS hdac_stream_detach_buffer(PHDAC_STREAM stream, PMDL* bufferMdl);
NTSTATUS hdac_stream_set_state(PHDAC_STREAM stream, HDAUDIO_STREAM_STATE streamState);
BOOLEAN hdac_stream_register_event(PHDAC_STREAM stream, PKEVENT notificationEvent);
BOOLEAN hdac_stream_unregister_event(PHDAC_STREAM stream, PKEVENT notificationEvent);
/* the stream's link position register (LPIB), and the vendor-specific DMA position register the controller mirrors it in */
PULONG hdac_stream_position_register(PHDAC_STREAM stream);
PULONG hdac_stream_dma_position_register(PHDAC_STREAM stream);
