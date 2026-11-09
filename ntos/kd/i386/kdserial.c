/*
 * COPYRIGHT:       See COPYING in the top level directory
 * PROJECT:         ReactOS kernel
 * FILE:            ntoskrnl/kd/i386/kdbg.c
 * PURPOSE:         Serial i/o functions for the kernel debugger.
 * PROGRAMMER:      Alex Ionescu
 *                  Hervé Poussineau
 */

/* INCLUDES *****************************************************************/

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>
#include <ndk/halfuncs.h>

//#include <cportlib/cportlib.h>
#include <cportlib/uartinfo.h>

/* STATIC VARIABLES ***********************************************************/

// static CPPORT DefaultPort = {0, 0, 0};

/* The COM port must only be initialized once! */
// static BOOLEAN PortInitialized = FALSE;

/* REACTOS FUNCTIONS **********************************************************/

static
BOOLEAN
KdpSolSetupPort(
    _Inout_ PCPPORT PortInformation)
{
    DEBUG_DEVICE_DESCRIPTOR Device;
    NTSTATUS Status;
    ULONG i;

    RtlZeroMemory(&Device, sizeof(Device));
    Device.Bus = 0xFFFFFFFF;
    Device.Slot = 0xFFFFFFFF;
    Device.VendorID = 0xFFFF;
    Device.DeviceID = 0xFFFF;
    Device.BaseClass = PCI_CLASS_SIMPLE_COMMS_CTLR;
    Device.SubClass = PCI_SUBCLASS_COM_SERIAL;
    Device.ProgIf = 0x02;

    Status = KdSetupPciDeviceForDebugging(NULL, &Device);
    if (!NT_SUCCESS(Status) && !Device.BaseAddress[0].Valid)
        return FALSE;

    for (i = 0; i < MAXIMUM_DEBUG_BARS; ++i)
    {
        if (!Device.BaseAddress[i].Valid)
            continue;

        if (Device.BaseAddress[i].Type == CmResourceTypeMemory)
        {
            PortInformation->Address = Device.BaseAddress[i].TranslatedAddress;
            PortInformation->Flags = CPPORT_FLAG_MMIO;
            return TRUE;
        }

        if (PortInformation->Address == NULL)
            PortInformation->Address = Device.BaseAddress[i].TranslatedAddress;
    }

    return PortInformation->Address != NULL;
}

BOOLEAN
NTAPI
KdPortIsComPortNumberValid(
    _In_ ULONG ComPortNumber)
{
    return ComPortNumber > 0 && ComPortNumber <= MAX_COM_PORTS;
}

BOOLEAN
NTAPI
KdPortInitializeEx(
    IN PCPPORT PortInformation,
    IN ULONG ComPortNumber)
{
    NTSTATUS Status;

#if 0 // Deactivated because never used in fact (was in KdPortInitialize but we use KdPortInitializeEx)
    /*
     * Find the port if needed
     */

    if (!PortInitialized)
    {
        DefaultPort.BaudRate = PortInformation->BaudRate;

        if (ComPortNumber == 0)
        {
            /*
             * Enumerate COM ports from the last to the first one, and stop
             * when we find a valid port. If we reach the first list element
             * (the undefined COM port), no valid port was found.
             */
            for (ComPortNumber = MAX_COM_PORTS; ComPortNumber > 0; ComPortNumber--)
            {
                if (CpDoesPortExist(UlongToPtr(BaseArray[ComPortNumber])))
                {
                    PortInformation->Address = DefaultPort.Address = BaseArray[ComPortNumber];
                    break;
                }
            }
            if (ComPortNumber == 0)
            {
                HalDisplayString("\r\nKernel Debugger: No serial port found\r\n\r\n");
                return FALSE;
            }
        }

        PortInitialized = TRUE;
    }
#endif

    if (KdpSolDebugPort && ComPortNumber == 0 && PortInformation->Address == NULL)
        KdpSolSetupPort(PortInformation);

    /*
     * Initialize the port
     */
    Status = CpInitializeEx(PortInformation,
                            (ComPortNumber == 0 ? PortInformation->Address
                                                : UlongToPtr(BaseArray[ComPortNumber])),
                            (PortInformation->BaudRate == 0 ? DEFAULT_DEBUG_BAUD_RATE
                                                            : PortInformation->BaudRate),
                            PortInformation->Flags);
    if (!NT_SUCCESS(Status))
    {
        HalDisplayString("\r\nKernel Debugger: Serial port not available\r\n\r\n");
        return FALSE;
    }
    else
    {
#ifndef NDEBUG
        int Length;
        CHAR Buffer[82];

        /* Print message to blue screen */
        Length = snprintf(Buffer, sizeof(Buffer),
                          "\r\nKernel Debugger: Using COM%lu (Port 0x%p) BaudRate %lu\r\n\r\n",
                          ComPortNumber,
                          PortInformation->Address,
                          PortInformation->BaudRate);
        if (Length == -1)
        {
            /* Terminate it if we went over-board */
            Buffer[sizeof(Buffer) - 1] = ANSI_NULL;
        }

        HalDisplayString(Buffer);
#endif /* NDEBUG */

#if 0
        /* Set global info */
        KdComPortInUse = DefaultPort.Address;
#endif
        return TRUE;
    }
}

BOOLEAN
NTAPI
KdPortGetByteEx(
    IN PCPPORT PortInformation,
    OUT PUCHAR ByteReceived)
{
    return (CpGetByte(PortInformation, ByteReceived, FALSE, FALSE) == CP_GET_SUCCESS);
}

VOID
NTAPI
KdPortPutByteEx(
    IN PCPPORT PortInformation,
    IN UCHAR ByteToSend)
{
    CpPutByte(PortInformation, ByteToSend);
}

VOID
NTAPI
KdPortPutBufferEx(
    IN PCPPORT PortInformation,
    IN PCCH Buffer,
    IN ULONG Length)
{
    ULONG Index;

    for (Index = 0; Index < Length; Index++)
    {
        CpPutByte(PortInformation, (UCHAR)Buffer[Index]);
    }
}

/* EOF */
