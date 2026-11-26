/*
 * PROJECT:     LiberNT Intel Smart Sound DSP endpoint driver
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     The endpoint driver of an audio child of the Intel audio DSP: DriverEntry, AddDevice and the start of the adapter, after the codec driver's main file
 * COPYRIGHT:   Copyright 2026 LiberNT contributors
 */

#include "private.h"

#define NDEBUG
#include <debug.h>

extern "C" DRIVER_INITIALIZE DriverEntry;

static
NTSTATUS
NTAPI
DSPPCM_StartDevice(
    IN PDEVICE_OBJECT DeviceObject,
    IN PIRP Irp,
    IN PRESOURCELIST ResourceList);

static
NTSTATUS
NTAPI
DSPPCM_AddDevice(
    IN PDRIVER_OBJECT DriverObject,
    IN PDEVICE_OBJECT PhysicalDeviceObject);

static
VOID
NTAPI
DSPPCM_Unload(
    IN PDRIVER_OBJECT DriverObject);

static
NTSTATUS
NTAPI
DSPPCM_Pnp(
    IN PDEVICE_OBJECT DeviceObject,
    IN OUT PIRP Irp)
{
    PIO_STACK_LOCATION IoStack = IoGetCurrentIrpStackLocation(Irp);

    /* the adapter goes with the device's removal and surprise removal; a stop leaves it for the start that follows (ksendpoint.c DsppcmPnpReleasesAdapter, R1-06) */
    if (DsppcmPnpReleasesAdapter(IoStack->MinorFunction))
    {
        PDSPPCM_DEVICE_EXTENSION DeviceExtension = (PDSPPCM_DEVICE_EXTENSION)DeviceObject->DeviceExtension;

        if (DeviceExtension->AdapterCommon)
        {
            DeviceExtension->AdapterCommon->Release();
            DeviceExtension->AdapterCommon = NULL;
        }
    }
    return PcDispatchIrp(DeviceObject, Irp);
}

static
NTSTATUS
NTAPI
DSPPCM_DeviceControl(
    IN PDEVICE_OBJECT DeviceObject,
    IN OUT PIRP Irp)
{
    return PcDispatchIrp(DeviceObject, Irp);
}

static
NTSTATUS
NTAPI
DSPPCM_StartDevice(
    IN PDEVICE_OBJECT DeviceObject,
    IN PIRP Irp,
    IN PRESOURCELIST ResourceList)
{
    PDSPPCM_DEVICE_EXTENSION DeviceExtension = (PDSPPCM_DEVICE_EXTENSION)DeviceObject->DeviceExtension;
    NTSTATUS Status;

    /* the start after a stop finds the adapter the stop left (DSPPCM_Pnp): its subdevices are registered still, and nothing is registered a second time (R1-06) */
    if (DeviceExtension->AdapterCommon != NULL)
    {
        DSPPCM_LOG("the adapter of the stop before is kept; its subdevices stay registered\n");
        return STATUS_SUCCESS;
    }
    Status = DSPPCM_NewAdapterCommon(&DeviceExtension->AdapterCommon);
    if (!NT_SUCCESS(Status))
        return Status;
    Status = DeviceExtension->AdapterCommon->Initialize(DeviceObject, Irp, ResourceList);
    if (!NT_SUCCESS(Status))
    {
        DSPPCM_LOG("the adapter is not started, 0x%08lx\n", Status);
        DeviceExtension->AdapterCommon->Release();
        DeviceExtension->AdapterCommon = NULL;
        return Status;
    }
    Status = PcRegisterAdapterPowerManagement(PUNKNOWN(DeviceExtension->AdapterCommon), DeviceObject);
    if (!NT_SUCCESS(Status))
    {
        DSPPCM_LOG("the adapter's power management is not registered, 0x%08lx\n", Status);
        DeviceExtension->AdapterCommon->Release();
        DeviceExtension->AdapterCommon = NULL;
        return Status;
    }
    return Status;
}

static
NTSTATUS
NTAPI
DSPPCM_AddDevice(
    IN PDRIVER_OBJECT DriverObject,
    IN PDEVICE_OBJECT PhysicalDeviceObject)
{
    NTSTATUS Status;

    if (!DriverObject || !PhysicalDeviceObject)
        return STATUS_INVALID_PARAMETER;
    Status = PcAddAdapterDevice(DriverObject, PhysicalDeviceObject, DSPPCM_StartDevice, 50, PORT_CLASS_DEVICE_EXTENSION_SIZE);
    if (!NT_SUCCESS(Status))
        DSPPCM_LOG("the adapter device is not added, 0x%08lx\n", Status);
    return Status;
}

static
VOID
NTAPI
DSPPCM_Unload(
    IN PDRIVER_OBJECT DriverObject)
{
    UNREFERENCED_PARAMETER(DriverObject);
}

extern "C"
NTSTATUS
NTAPI
DriverEntry(
    IN PDRIVER_OBJECT DriverObject,
    IN PUNICODE_STRING RegistryPath)
{
    NTSTATUS Status;

    Status = PcInitializeAdapterDriver(DriverObject, RegistryPath, DSPPCM_AddDevice);
    if (NT_SUCCESS(Status))
    {
        DriverObject->DriverUnload = DSPPCM_Unload;
        DriverObject->MajorFunction[IRP_MJ_PNP] = DSPPCM_Pnp;
        DriverObject->MajorFunction[IRP_MJ_DEVICE_CONTROL] = DSPPCM_DeviceControl;
    }
    return Status;
}
