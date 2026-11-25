/* Host stand-in for the WDM types the ADSP bus interface names: the layout of the structures is never used on the host */
#include "../shim.h"

#ifndef FWLOAD_STUB_WDM_H
#define FWLOAD_STUB_WDM_H

typedef VOID (*PINTERFACE_REFERENCE)(PVOID Context);
typedef VOID (*PINTERFACE_DEREFERENCE)(PVOID Context);
typedef struct _BUS_INTERFACE_STANDARD
{
    UCHAR Opaque;
} BUS_INTERFACE_STANDARD, *PBUS_INTERFACE_STANDARD;
typedef enum _DEVICE_POWER_STATE
{
    PowerDeviceUnspecified,
    PowerDeviceD0,
    PowerDeviceD3
} DEVICE_POWER_STATE, *PDEVICE_POWER_STATE;

#endif
