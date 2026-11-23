#include "driver.h"
#include <acpiioct.h>
#include "sof-tplg.h"

DEFINE_GUID(GUID_ACPI_DSD,
	0xdaffd814, 0x6eba, 0x4d8c, 0x8a, 0x91, 0xbc, 0x9b, 0xbf, 0x4a, 0xa3, 0x01);

static
BOOLEAN
AcpiArgumentFits(
	_In_ PACPI_METHOD_ARGUMENT Argument,
	_In_ PUCHAR End)
{
	if ((PUCHAR)Argument + FIELD_OFFSET(ACPI_METHOD_ARGUMENT, Data) > End) {
		return FALSE;
	}
	return ACPI_METHOD_ARGUMENT_LENGTH(Argument->DataLength) <= (SIZE_T)(End - (PUCHAR)Argument);
}

static
BOOLEAN
AcpiArgumentIsName(
	_In_ PACPI_METHOD_ARGUMENT Argument,
	_In_z_ PCSTR Name)
{
	SIZE_T Length = strlen(Name);

	return (Argument->DataLength == Length ||
			(Argument->DataLength == Length + 1 && Argument->Data[Length] == 0)) &&
		memcmp(Argument->Data, Name, Length) == 0;
}

void copyDSDParam(PACPI_METHOD_ARGUMENT dsdParameterData, char* buf) {
	RtlZeroMemory(buf, 32);
	RtlCopyMemory(buf, dsdParameterData->Data, min(dsdParameterData->DataLength, 32));
}

NTSTATUS
GetSOFTplg(
	_In_ WDFDEVICE FxDevice,
	SOF_TPLG* sofTplg
)
{
	if (!sofTplg) {
		return STATUS_INVALID_PARAMETER;
	}

	NTSTATUS status = STATUS_ACPI_NOT_INITIALIZED;
	ACPI_EVAL_INPUT_BUFFER_EX inputBuffer;
	RtlZeroMemory(&inputBuffer, sizeof(inputBuffer));

	inputBuffer.Signature = ACPI_EVAL_INPUT_BUFFER_SIGNATURE_EX;
	status = RtlStringCchPrintfA(
		inputBuffer.MethodName,
		sizeof(inputBuffer.MethodName),
		"_DSD"
	);
	if (!NT_SUCCESS(status)) {
		return status;
	}

	ACPI_EVAL_OUTPUT_BUFFER outputSizeBuffer = { 0 };

	WDF_MEMORY_DESCRIPTOR outputSizeMemDesc;
	WDF_MEMORY_DESCRIPTOR_INIT_BUFFER(&outputSizeMemDesc, &outputSizeBuffer, (ULONG)sizeof(outputSizeBuffer));

	WDF_MEMORY_DESCRIPTOR inputMemDesc;
	WDF_MEMORY_DESCRIPTOR_INIT_BUFFER(&inputMemDesc, &inputBuffer, (ULONG)sizeof(inputBuffer));

	// Send the request along
	status = WdfIoTargetSendInternalIoctlSynchronously(
		WdfDeviceGetIoTarget(FxDevice),
		NULL,
		IOCTL_ACPI_EVAL_METHOD_EX,
		&inputMemDesc,
		&outputSizeMemDesc,
		NULL,
		NULL
	);

	if (status != STATUS_BUFFER_OVERFLOW) {
		// Method might not exist?
		return status;
	}

	WDFMEMORY outputMemory;
	PACPI_EVAL_OUTPUT_BUFFER outputBuffer;
	size_t outputArgumentBufferSize = outputSizeBuffer.Length;
	size_t outputBufferSize = FIELD_OFFSET(ACPI_EVAL_OUTPUT_BUFFER, Argument) + outputArgumentBufferSize;

	WDF_OBJECT_ATTRIBUTES attributes;
	WDF_OBJECT_ATTRIBUTES_INIT(&attributes);
	attributes.ParentObject = FxDevice;

	status = WdfMemoryCreate(&attributes,
		NonPagedPoolNx,
		0,
		outputBufferSize,
		&outputMemory,
		(PVOID*)&outputBuffer);
	if (!NT_SUCCESS(status)) {
		return status;
	}

	RtlZeroMemory(outputBuffer, outputBufferSize);

	WDF_MEMORY_DESCRIPTOR outputMemDesc;
	WDF_MEMORY_DESCRIPTOR_INIT_HANDLE(&outputMemDesc, outputMemory, NULL);

	status = WdfIoTargetSendInternalIoctlSynchronously(
		WdfDeviceGetIoTarget(FxDevice),
		NULL,
		IOCTL_ACPI_EVAL_METHOD_EX,
		&inputMemDesc,
		&outputMemDesc,
		NULL,
		NULL
	);
	if (!NT_SUCCESS(status)) {
		goto Exit;
	}

	if (outputBuffer->Signature != ACPI_EVAL_OUTPUT_BUFFER_SIGNATURE) {
		goto Exit;
	}

	SklHdAudBusPrint(DEBUG_LEVEL_ERROR, DBG_PNP,
		"Evaluted %s successfully\n", inputBuffer.MethodName);

	if (outputBuffer->Count % 2) {
		status = STATUS_ACPI_INVALID_DATA;
		goto Exit;
	}

	status = STATUS_NOT_FOUND;

	RtlZeroMemory(sofTplg, sizeof(*sofTplg));
	sofTplg->magic = SOFTPLG_MAGIC;
	sofTplg->length = sizeof(*sofTplg);

	{
		PUCHAR argumentsEnd = (PUCHAR)outputBuffer->Argument + outputBuffer->Length;
		PACPI_METHOD_ARGUMENT currArgument = outputBuffer->Argument;

		if (argumentsEnd > (PUCHAR)outputBuffer + outputBufferSize) {
			argumentsEnd = (PUCHAR)outputBuffer + outputBufferSize;
		}

		for (ULONG i = 0; i < outputBuffer->Count; i += 2) {
			PACPI_METHOD_ARGUMENT guidArg = currArgument;
			PACPI_METHOD_ARGUMENT packageArg;

			if (!AcpiArgumentFits(guidArg, argumentsEnd)) {
				break;
			}
			packageArg = ACPI_METHOD_NEXT_ARGUMENT(guidArg);
			if (!AcpiArgumentFits(packageArg, argumentsEnd)) {
				break;
			}
			currArgument = ACPI_METHOD_NEXT_ARGUMENT(packageArg);

			if (guidArg->Type != ACPI_METHOD_ARGUMENT_BUFFER ||
				guidArg->DataLength != 16 ||
				packageArg->Type != ACPI_METHOD_ARGUMENT_PACKAGE) {
				break;
			}

			if (memcmp(guidArg->Data, &GUID_ACPI_DSD, guidArg->DataLength) != 0) {
				continue;
			}

			status = STATUS_SUCCESS;

			for (PACPI_METHOD_ARGUMENT dsdParameter = (PACPI_METHOD_ARGUMENT)packageArg->Data;
				 AcpiArgumentFits(dsdParameter, packageArg->Data + packageArg->DataLength);
				 dsdParameter = ACPI_METHOD_NEXT_ARGUMENT(dsdParameter)) {
				PUCHAR dsdEnd = dsdParameter->Data + dsdParameter->DataLength;
				PACPI_METHOD_ARGUMENT dsdParameterName = (PACPI_METHOD_ARGUMENT)dsdParameter->Data;
				PACPI_METHOD_ARGUMENT dsdParameterData;

				if (dsdParameter->Type != ACPI_METHOD_ARGUMENT_PACKAGE ||
					!AcpiArgumentFits(dsdParameterName, dsdEnd)) {
					continue;
				}
				dsdParameterData = ACPI_METHOD_NEXT_ARGUMENT(dsdParameterName);
				if (!AcpiArgumentFits(dsdParameterData, dsdEnd)) {
					continue;
				}

				if (AcpiArgumentIsName(dsdParameterName, "speaker-tplg")) {
					copyDSDParam(dsdParameterData, sofTplg->speaker_tplg);
				}
				else if (AcpiArgumentIsName(dsdParameterName, "hp-tplg")) {
					copyDSDParam(dsdParameterData, sofTplg->hp_tplg);
				}
				else if (AcpiArgumentIsName(dsdParameterName, "mic-tplg")) {
					copyDSDParam(dsdParameterData, sofTplg->dmic_tplg);
				}
			}
		}
	}

Exit:
	if (outputMemory != WDF_NO_HANDLE) {
		WdfObjectDelete(outputMemory);
	}
	return status;
}
