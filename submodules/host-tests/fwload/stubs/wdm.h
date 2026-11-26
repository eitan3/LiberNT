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
/* an MDL and an event object as the stream members of the bus interface pass them: the MDL is opaque to the code compiled on the host; the event is what the fake bus sets and the stream manager's period wait takes */
typedef struct _MDL
{
    UCHAR Opaque[16];
} MDL, *PMDL;
typedef struct _KEVENT
{
    /* the fake bus: whether a period set it, and how often */
    BOOLEAN Signaled;
    UINT32 Signals;
} KEVENT, *PKEVENT;
/* wdm.h: the timeout of a wait, relative in 100 ns units when negative (QuadPart is all the wait reads), and the two enumerations KeWaitForSingleObject takes */
typedef union _LARGE_INTEGER
{
    LONGLONG QuadPart;
} LARGE_INTEGER, *PLARGE_INTEGER;
typedef enum _KWAIT_REASON
{
    Executive = 0
} KWAIT_REASON;
typedef enum _KPROCESSOR_MODE
{
    KernelMode = 0
} KPROCESSOR_MODE;
/* The stream manager's lock (intcdsp/stream.h INTCDSP_STREAMS.Lock) and the IRQL a transition is refused above: the mutex's layout is never used on the host, and the IRQL is the test's to raise (fake_bus.h FakeBusIrql) */
typedef UCHAR KIRQL, *PKIRQL;
#define PASSIVE_LEVEL           0
#define DISPATCH_LEVEL          2
typedef struct _KMUTEX
{
    UCHAR Opaque[16];
} KMUTEX, *PKMUTEX;
VOID KeInitializeMutex(PKMUTEX Mutex, ULONG Level);
VOID KeReleaseMutex(PKMUTEX Mutex, BOOLEAN Wait);
KIRQL KeGetCurrentIrql(VOID);
/* Declared as the kernel's are; fake_bus.c defines them for the host, a wait being the test's model's to answer through the fake bus's wait hook */
VOID KeClearEvent(PKEVENT Event);
NTSTATUS KeWaitForSingleObject(PVOID Object, KWAIT_REASON WaitReason, KPROCESSOR_MODE WaitMode, BOOLEAN Alertable, PLARGE_INTEGER Timeout);

#endif
