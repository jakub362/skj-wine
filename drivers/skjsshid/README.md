# skjsshid.sys — SKJ Wine replacement for SteelSeries `sshid.sys`

GG's device library `SSEdevice.dll` opens `\\.\SSengine`, a control device created by
SteelSeries' `sshid.sys` (KMDF USB lower filter, v2.11.5.0 in GG 120). Wine can't load
KMDF drivers (no `WDFLDR.SYS`), so this is a small WDM driver that Wine's `winedevice.exe`
loads as a normal kernel service and that recreates the same device and protocol.

## What sshid.sys does (reverse engineered)

It filters the **input** of attached SteelSeries mice/keyboards to implement GG's
software-only features:

| IOCTL | Purpose | Input |
|---|---|---|
| `C0DE0000` / `C0DE0004` | disable / enable input injection | – |
| `C0DE0008` | queue an input packet (macro playback: key/button events), sent by a 1 ms timer | 0x814 bytes, `u16 vid, u16 pid, …` |
| `C0DE0010` | flush a device's queues | `u16 vid, u16 pid` |
| `C0DE000C`, `14`, `18`, `1C`, `20`, `24`, `28`, `38`, `3C`, `40`, `44` | per-device settings (button suppression, acceleration / deceleration curves, angle snapping — some take doubles) | `vid/pid` header + values |
| `C0DE002C` | register a notification event | 8-byte `HANDLE` |
| `C0DE0030` | read queued key events | output 0x50 = up to 10 × 8-byte events |
| `C0DE0034` | reset all devices | – |

It also intercepts keyboard LED output reports and turns them into `SET_REPORT` control transfers.

**Hardware settings (DPI, RGB, polling rate, firmware) do not go through it** — `SSEdevice.dll`
sends those over plain HID (`hid.dll`, `IOCTL_HID_SET/GET_FEATURE`), which Wine passes
through to the device via hidraw.

## What skjsshid.sys does

Behaves like `sshid.sys` for a device that is **not attached** to the filter — the same state
SteelSeries itself uses for the PIDs on its `SSE2Bypass` list:

- creates `\Device\SSengine` + `\DosDevices\SSengine`
- global commands (`00`, `04`, `10`, `2C`, `34`) succeed; `30` returns an empty event list
- per-device commands return `STATUS_NOT_FOUND` (Win32 error 1168)
- logs the first use of each IOCTL through `DbgPrint` (`WINEDEBUG=+debugstr`)

Verified: GG's engine opens `\\.\SSengine` and sends `C0DE0004` at startup, and the full GG
stack runs. `tools/sstest` exercises every command.

## Not implemented (yet)

The software features themselves: macro playback, button remapping to macros,
acceleration/deceleration, angle snapping. Implementing them needs the driver to filter real
input. Under Wine that would mean reading the mouse's evdev node and re-emitting through
uinput from `winedevice.exe` — possible later.

## Build

```bash
x86_64-w64-mingw32-gcc -O2 -I/usr/share/mingw-w64/include/ddk -shared -nostdlib \
  -Wl,--subsystem,native -Wl,--entry,DriverEntry -Wl,--image-base,0x140000000 \
  -o skjsshid.sys skjsshid.c -lntoskrnl
```

Install (done by `apps/steelseries-gg/setup.sh`):

```bash
cp skjsshid.sys "$WINEPREFIX/drive_c/windows/system32/drivers/"
wine sc create skjsshid type= kernel start= auto binPath= 'C:\windows\system32\drivers\skjsshid.sys'
```
