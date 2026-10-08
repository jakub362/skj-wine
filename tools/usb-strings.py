#!/usr/bin/env python3
"""usb-strings.py PREFIX - tell a Wine prefix the extra text strings of your USB devices.

Windows programs can ask a HID device for any of its USB text strings by number
(HidD_GetIndexedString). Linux only hands Wine three of them (maker, product, serial number), so
vendor software that keeps data in the others never recognises its device: Corsair's iCUE reads
its protocol variant ("BP00") from string 5 and gives up without it.

This reads all strings straight from the devices and stores them in the prefix registry
(HKLM\\Software\\Wine\\UsbStrings\\VID_xxxx&PID_xxxx, one value per string number), where SKJ Wine's
HID driver looks them up (patches/wine/0011).

Only devices your user may open are read - those of the vendors allowed on the SKJ Wine window's
Wine page. Run again after plugging in another device or updating firmware; the launchers do it
on every start.
"""
import ctypes, fcntl, glob, os, struct, subprocess, sys, tempfile

USBDEVFS_CONTROL = 0xC0185500


def read_string(fd, index):
    """One string descriptor (US English), or None if the device has none at this number."""
    buf = ctypes.create_string_buffer(255)
    request = bytearray(struct.pack("BBHHHIP", 0x80, 6, (3 << 8) | index, 0x0409, 255, 500, ctypes.addressof(buf)))
    try:
        fcntl.ioctl(fd, USBDEVFS_CONTROL, request)
    except OSError:
        return None
    raw = buf.raw
    if raw[0] < 2 or raw[1] != 3:
        return None
    return raw[2:raw[0]].decode("utf-16le", "replace")


def devices():
    for dev in sorted(glob.glob("/sys/bus/usb/devices/*/idVendor")):
        base = os.path.dirname(dev)
        try:
            vid, pid = open(dev).read().strip(), open(base + "/idProduct").read().strip()
            node = "/dev/bus/usb/%03d/%03d" % (int(open(base + "/busnum").read()), int(open(base + "/devnum").read()))
        except (OSError, ValueError):
            continue
        if os.access(node, os.R_OK | os.W_OK):
            yield vid.upper(), pid.upper(), node


def main():
    if len(sys.argv) != 2 or not os.path.isdir(os.path.join(sys.argv[1], "drive_c")):
        sys.exit(__doc__.strip().splitlines()[0])
    lines = ["Windows Registry Editor Version 5.00", ""]
    count = 0
    for vid, pid, node in devices():
        try:
            fd = os.open(node, os.O_RDWR)
        except OSError:
            continue
        strings, missing = {}, 0
        for index in range(1, 32):
            text = read_string(fd, index)
            if text is None:
                missing += 1
                if missing >= 4:
                    break
                continue
            missing = 0
            strings[index] = text
        os.close(fd)
        if strings:
            lines.append("[HKEY_LOCAL_MACHINE\\Software\\Wine\\UsbStrings\\VID_%s&PID_%s]" % (vid, pid))
            for index, text in sorted(strings.items()):
                lines.append('"%d"="%s"' % (index, text.replace("\\", "\\\\").replace('"', '\\"')))
            lines.append("")
            count += 1
    if not count:
        return 0
    with tempfile.NamedTemporaryFile("w", suffix=".reg", encoding="utf-16", delete=False) as f:
        f.write("\r\n".join(lines))
    env = dict(os.environ, WINEPREFIX=sys.argv[1], WINEDEBUG="-all")
    result = subprocess.run(["wine", "regedit", "/S", f.name], env=env).returncode
    os.unlink(f.name)
    print("[usb-strings] %d device(s) written" % count)
    return result


if __name__ == "__main__":
    sys.exit(main())
