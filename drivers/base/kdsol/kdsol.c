/*
 * PROJECT:     LiberNT kernel
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Kernel debugger transport for PCI-attached 16550 UARTs:
 *              Intel AMT SOL (KT redirection) and generic PCI serial cards.
 *              Selected with /DEBUGPORT=SOL, which loads kdsol.dll.
 *              /SOLPORT=<bus>:<dev>.<fn> pins a specific function; without it
 *              the first PCI class 07/00/02 (16550 serial) device is used.
 */

#include "../kdcom/kddll.h"

#include <arc/arc.h>
#include <stdlib.h>
#include <ndk/halfuncs.h>
#include <cportlib/cportlib.h>
#include <cportlib/uartinfo.h>

/* GLOBALS ********************************************************************/

CPPORT KdComPort;
BOOLEAN KdpScreenMode;

/* LOCALS *********************************************************************/

static DEBUG_DEVICE_DESCRIPTOR KdpSolDevice;
static ULONG KdpSolPortFlags;

#define CONST_STR_LEN(x) (sizeof(x)/sizeof(x[0]) - 1)

static
VOID
KdpSolGetPortNumber(
    _In_ PSTR PortString,
    _Out_ PULONG Bus,
    _Out_ PULONG Slot)
{
    PCI_SLOT_NUMBER PciSlot;
    PSTR Dot, End;

    *Bus = 0xFFFFFFFF;
    *Slot = 0xFFFFFFFF;

    if (PortString == NULL)
        return;

    /* Format: <bus>:<dev>.<fn>, numbers in hex */
    *Bus = strtoul(PortString, &End, 16);
    if (End == PortString || *End != ':')
    {
        *Bus = 0xFFFFFFFF;
        return;
    }

    PciSlot.u.AsULONG = 0;
    PciSlot.u.bits.DeviceNumber = strtoul(End + 1, &Dot, 16) & 0x1F;
    if (*Dot == '.')
        PciSlot.u.bits.FunctionNumber = strtoul(Dot + 1, NULL, 16) & 0x7;

    *Slot = PciSlot.u.AsULONG;
}

/* FUNCTIONS ******************************************************************/

NTSTATUS
NTAPI
KdD0Transition(VOID)
{
    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
KdD3Transition(VOID)
{
    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
KdSave(IN BOOLEAN SleepTransition)
{
    UNREFERENCED_PARAMETER(SleepTransition);
    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
KdRestore(IN BOOLEAN SleepTransition)
{
    if (SleepTransition && KdComPort.Address != NULL)
        CpInitializeEx(&KdComPort, KdComPort.Address, KdComPort.BaudRate, KdpSolPortFlags);

    return STATUS_SUCCESS;
}

/******************************************************************************
 * \name KdDebuggerInitialize0
 * \brief Phase 0 initialization. Locates the PCI 16550 UART and maps its BAR.
 * \param [opt] LoaderBlock Pointer to the Loader parameter block. Can be NULL.
 * \return Status
 */
NTSTATUS
NTAPI
KdDebuggerInitialize0(IN PLOADER_PARAMETER_BLOCK LoaderBlock OPTIONAL)
{
    ULONG BaudRate = DEFAULT_DEBUG_BAUD_RATE;
    ULONG i;
    NTSTATUS Status;
    PSTR CommandLine, Option;

    RtlZeroMemory(&KdpSolDevice, sizeof(KdpSolDevice));
    KdpSolDevice.VendorID = 0xFFFF;
    KdpSolDevice.DeviceID = 0xFFFF;
    KdpSolDevice.BaseClass = PCI_CLASS_SIMPLE_COMMS_CTLR;
    KdpSolDevice.SubClass = PCI_SUBCLASS_COM_SERIAL;
    KdpSolDevice.ProgIf = 0x02;

    if (LoaderBlock)
    {
        CommandLine = LoaderBlock->LoadOptions;
        _strupr(CommandLine);

        Option = strstr(CommandLine, "SOLPORT");
        if (Option)
        {
            Option += CONST_STR_LEN("SOLPORT");
            while (*Option == ' ') ++Option;
            if (*Option) ++Option;
            KdpSolGetPortNumber(Option, &KdpSolDevice.Bus, &KdpSolDevice.Slot);
        }
        else
        {
            KdpSolDevice.Bus = 0xFFFFFFFF;
            KdpSolDevice.Slot = 0xFFFFFFFF;
        }

        Option = strstr(CommandLine, "BAUDRATE");
        if (Option)
        {
            ULONG Value;
            Option += CONST_STR_LEN("BAUDRATE");
            while (*Option == ' ') ++Option;
            if (*Option)
            {
                Value = (ULONG)atol(Option + 1);
                if (Value) BaudRate = Value;
            }
        }
    }
    else
    {
        KdpSolDevice.Bus = 0xFFFFFFFF;
        KdpSolDevice.Slot = 0xFFFFFFFF;
    }

    Status = KdSetupPciDeviceForDebugging(LoaderBlock, &KdpSolDevice);
    if (!NT_SUCCESS(Status))
        return Status;

    /* Use the first programmed BAR; SOL exposes an MMIO window, plain
     * PCI serial cards usually an I/O one. */
    for (i = 0; i < MAXIMUM_DEBUG_BARS; ++i)
    {
        PDEBUG_DEVICE_ADDRESS DeviceAddress = &KdpSolDevice.BaseAddress[i];

        if (!DeviceAddress->Valid)
            continue;

        KdpSolPortFlags = (DeviceAddress->Type == CmResourceTypeMemory)
                          ? CPPORT_FLAG_MMIO : 0;
        Status = CpInitializeEx(&KdComPort,
                              DeviceAddress->TranslatedAddress,
                              BaudRate,
                              KdpSolPortFlags);
        if (NT_SUCCESS(Status))
        {
            KdComPortInUse = KdComPort.Address;
            return STATUS_SUCCESS;
        }
    }

    return STATUS_INVALID_PARAMETER;
}

/******************************************************************************
 * \name KdDebuggerInitialize1
 * \brief Phase 1 initialization.
 * \param [opt] LoaderBlock Pointer to the Loader parameter block. Can be NULL.
 * \return Status
 */
NTSTATUS
NTAPI
KdDebuggerInitialize1(IN PLOADER_PARAMETER_BLOCK LoaderBlock OPTIONAL)
{
    UNREFERENCED_PARAMETER(LoaderBlock);
    return STATUS_SUCCESS;
}

VOID
NTAPI
KdpScreenPrint(
    _In_reads_bytes_(Length) PCCH String,
    _In_ ULONG Length)
{
    UNREFERENCED_PARAMETER(String);
    UNREFERENCED_PARAMETER(Length);
}

VOID
NTAPI
KdpSendByte(IN UCHAR Byte)
{
    CpPutByte(&KdComPort, Byte);
}

KDP_STATUS
NTAPI
KdpPollByte(OUT PUCHAR OutByte)
{
    USHORT Status;

    Status = CpGetByte(&KdComPort, OutByte, FALSE, FALSE);
    switch (Status)
    {
        case CP_GET_SUCCESS:
            return KDP_PACKET_RECEIVED;

        case CP_GET_NODATA:
            return KDP_PACKET_TIMEOUT;

        case CP_GET_ERROR:
        default:
            return KDP_PACKET_RESEND;
    }
}

KDP_STATUS
NTAPI
KdpReceiveByte(OUT PUCHAR OutByte)
{
    USHORT Status;

    Status = CpGetByte(&KdComPort, OutByte, TRUE, FALSE);
    switch (Status)
    {
        case CP_GET_SUCCESS:
            return KDP_PACKET_RECEIVED;

        case CP_GET_NODATA:
            return KDP_PACKET_TIMEOUT;

        case CP_GET_ERROR:
        default:
            return KDP_PACKET_RESEND;
    }
}

KDP_STATUS
NTAPI
KdpPollBreakIn(VOID)
{
    KDP_STATUS KdStatus;
    UCHAR Byte;

    KdStatus = KdpPollByte(&Byte);
    if ((KdStatus == KDP_PACKET_RECEIVED) && (Byte == BREAKIN_PACKET_BYTE))
    {
        return KDP_PACKET_RECEIVED;
    }
    return KDP_PACKET_TIMEOUT;
}

/* EOF */
