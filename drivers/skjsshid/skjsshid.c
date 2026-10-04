/*
 * skjsshid.sys - SKJ Wine replacement for SteelSeries' sshid.sys
 *
 * SteelSeries GG's device library (SSEdevice.dll) opens \\.\SSengine, a control
 * device created by sshid.sys, a KMDF USB lower filter. Wine cannot load KMDF
 * drivers (no WDFLDR.SYS), so this is a plain WDM driver, loaded by Wine's
 * winedevice.exe, that recreates the same control device and protocol.
 *
 * What sshid.sys does (reverse engineered from sshid.sys 2.11.5.0, GG 120):
 *   it filters the INPUT of attached SteelSeries mice/keyboards to implement
 *   GG's software-only features: macro playback (0x08), button suppression,
 *   acceleration / deceleration / angle snapping (per-device setters), and
 *   it reports key events back to the engine (0x2C event + 0x30 read).
 *   Hardware settings (DPI, RGB, polling rate...) do NOT go through it; the
 *   engine sends those over plain HID, which Wine passes through via hidraw.
 *
 * This driver therefore behaves like the real sshid.sys for a device that is
 * not attached to the filter - the state SteelSeries itself uses for devices
 * on its SSE2Bypass list: the control device exists, global commands succeed,
 * per-device commands report STATUS_NOT_FOUND, and the event queue is empty.
 *
 * IOCTLs (all METHOD_BUFFERED, DeviceType 0xC0DE):
 *   0x00 disable injection        0x04 enable injection
 *   0x08 queue input packet (0x814 bytes, vid/pid header)
 *   0x0C, 0x14, 0x18, 0x1C, 0x20, 0x24, 0x28, 0x38, 0x3C, 0x40, 0x44
 *        per-device settings (vid/pid header)
 *   0x10 flush device queues (vid/pid)
 *   0x2C register notification event (HANDLE, 8 bytes)
 *   0x30 read events (output 0x50 bytes = up to 10 x 8-byte events)
 *   0x34 reset all devices
 */
#include <ntddk.h>

#define SS_IOCTL(n)        CTL_CODE(0xC0DE, (n), METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_SS_DISABLE        SS_IOCTL(0x00)  /* 0xC0DE0000 */
#define IOCTL_SS_ENABLE         SS_IOCTL(0x01)  /* 0xC0DE0004 */
#define IOCTL_SS_QUEUE_PACKET   SS_IOCTL(0x02)  /* 0xC0DE0008 */
#define IOCTL_SS_FLUSH          SS_IOCTL(0x04)  /* 0xC0DE0010 */
#define IOCTL_SS_SET_EVENT      SS_IOCTL(0x0B)  /* 0xC0DE002C */
#define IOCTL_SS_READ_EVENTS    SS_IOCTL(0x0C)  /* 0xC0DE0030 */
#define IOCTL_SS_RESET_ALL      SS_IOCTL(0x0D)  /* 0xC0DE0034 */

#define SS_PACKET_SIZE   0x814
#define SS_EVENTS_SIZE   0x50

static PDEVICE_OBJECT control_device;
static PKEVENT notify_event;
static BOOLEAN injection_enabled;
static ULONG seen_ioctls[0x20];  /* first-use logging */

static NTSTATUS complete(PIRP irp, NTSTATUS status, ULONG_PTR info)
{
    irp->IoStatus.Status = status;
    irp->IoStatus.Information = info;
    IoCompleteRequest(irp, IO_NO_INCREMENT);
    return status;
}

static NTSTATUS WINAPI dispatch_create_close(PDEVICE_OBJECT device, PIRP irp)
{
    return complete(irp, STATUS_SUCCESS, 0);
}

static void log_ioctl(ULONG code, ULONG in_len, ULONG out_len, NTSTATUS status)
{
    ULONG fn = (code >> 2) & 0xfff;
    if (fn < ARRAYSIZE(seen_ioctls) && seen_ioctls[fn]++) return;
    DbgPrint("skjsshid: ioctl %08lx in=%lu out=%lu -> %08lx\n", code, in_len, out_len, status);
}

static NTSTATUS WINAPI dispatch_ioctl(PDEVICE_OBJECT device, PIRP irp)
{
    IO_STACK_LOCATION *stack = IoGetCurrentIrpStackLocation(irp);
    ULONG code = stack->Parameters.DeviceIoControl.IoControlCode;
    ULONG in_len = stack->Parameters.DeviceIoControl.InputBufferLength;
    ULONG out_len = stack->Parameters.DeviceIoControl.OutputBufferLength;
    void *buf = irp->AssociatedIrp.SystemBuffer;
    NTSTATUS status = STATUS_SUCCESS;
    ULONG_PTR info = 0;

    switch (code)
    {
    case IOCTL_SS_DISABLE:
        injection_enabled = FALSE;
        break;

    case IOCTL_SS_ENABLE:
        injection_enabled = TRUE;
        break;

    case IOCTL_SS_QUEUE_PACKET:
        if (in_len < SS_PACKET_SIZE) status = STATUS_BUFFER_TOO_SMALL;
        else status = STATUS_NOT_FOUND;  /* device not attached to the filter */
        break;

    case IOCTL_SS_FLUSH:
        if (in_len < 4) status = STATUS_BUFFER_TOO_SMALL;
        /* real driver: flushes queues of matching devices, success even if none */
        break;

    case IOCTL_SS_SET_EVENT:
        if (in_len < sizeof(ULONGLONG)) { status = STATUS_BUFFER_TOO_SMALL; break; }
        if (notify_event) { ObDereferenceObject(notify_event); notify_event = NULL; }
        /* Under Wine the driver runs in winedevice.exe, not in the caller's
         * process, so the caller's event handle usually can't be referenced.
         * We never queue events (no device is attached), so the event would
         * never be signalled anyway: accept the registration either way. */
        if (ObReferenceObjectByHandle(*(HANDLE *)buf, EVENT_MODIFY_STATE, *ExEventObjectType,
                                      UserMode, (void **)&notify_event, NULL))
            notify_event = NULL;
        status = STATUS_SUCCESS;
        break;

    case IOCTL_SS_READ_EVENTS:
        if (out_len < SS_EVENTS_SIZE) { status = STATUS_BUFFER_TOO_SMALL; break; }
        RtlZeroMemory(buf, SS_EVENTS_SIZE);  /* no queued events */
        info = out_len;                      /* matches the real driver */
        break;

    case IOCTL_SS_RESET_ALL:
        break;

    default:
        if (((code >> 16) & 0xffff) == 0xC0DE)
        {
            /* per-device settings: macros/remap/accel/decel/angle snapping */
            status = in_len < 4 ? STATUS_BUFFER_TOO_SMALL : STATUS_NOT_FOUND;
        }
        else status = STATUS_INVALID_DEVICE_REQUEST;
        break;
    }

    log_ioctl(code, in_len, out_len, status);
    return complete(irp, status, info);
}

static void WINAPI driver_unload(PDRIVER_OBJECT driver)
{
    UNICODE_STRING link;
    if (notify_event) ObDereferenceObject(notify_event);
    RtlInitUnicodeString(&link, L"\\DosDevices\\SSengine");
    IoDeleteSymbolicLink(&link);
    if (control_device) IoDeleteDevice(control_device);
}

NTSTATUS WINAPI DriverEntry(PDRIVER_OBJECT driver, PUNICODE_STRING path)
{
    UNICODE_STRING name, link;
    NTSTATUS status;

    RtlInitUnicodeString(&name, L"\\Device\\SSengine");
    RtlInitUnicodeString(&link, L"\\DosDevices\\SSengine");

    status = IoCreateDevice(driver, 0, &name, FILE_DEVICE_UNKNOWN, 0, FALSE, &control_device);
    if (status) { DbgPrint("skjsshid: IoCreateDevice failed %08lx\n", status); return status; }

    status = IoCreateSymbolicLink(&link, &name);
    if (status) { DbgPrint("skjsshid: IoCreateSymbolicLink failed %08lx\n", status); IoDeleteDevice(control_device); return status; }

    control_device->Flags |= DO_BUFFERED_IO;
    control_device->Flags &= ~DO_DEVICE_INITIALIZING;

    driver->MajorFunction[IRP_MJ_CREATE] = dispatch_create_close;
    driver->MajorFunction[IRP_MJ_CLOSE] = dispatch_create_close;
    driver->MajorFunction[IRP_MJ_CLEANUP] = dispatch_create_close;
    driver->MajorFunction[IRP_MJ_DEVICE_CONTROL] = dispatch_ioctl;
    driver->DriverUnload = driver_unload;

    DbgPrint("skjsshid: \\\\.\\SSengine ready\n");
    return STATUS_SUCCESS;
}
