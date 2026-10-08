# Corsair iCUE 5 under SKJ Wine

State 2026-10-07: installs and opens. Not checked yet: whether it finds a device (needs the udev rule
for vendor `1b1c`, Wine page of the SKJ Wine window), lighting, key assignments.

## Install
- `Install iCUE.exe` (web installer 1.22.16, C++/WinRT) crashes at start: `Windows.UI.Xaml.Hosting.WindowsXamlManager`
  is not in Wine (`c0000409`). It is only a front end for Corsair's package manager, so `setup.sh` drives that directly.
- Package manager: `cuepkg.exe` (Qt) from `…/modules/windows/installerpackage/cuepkg.zip`.
  Repository: `…/modules/windows/packages/` (`cuepkg-metadata.json` lists ~520 packages, each a signed `.cuepkg`).
  Commands used: `init`, `set-config RepositoryUrl URL`, `update`, `install NAME…`, all with `--installdir=`.
  Others seen: `get-config`, `search`, `show`, `check`, `uninstall`, `upgrade`, `self-upgrade` (only downloads), `history`.
  `--help` prints nothing; unknown options open a message box and wait (read it with `tools/winlist`).
- The cuepkg in the zip is old: today's `core` fails with "Invalid or malformed package". `install cuepkg` puts the
  current one into `<install>\cuepkg-<version>\`; use that for everything else.
- `core` = iCUE itself; devices are `dev-*` packages (K70 CORE RGB TKL Wireless → `dev-k70-core-rgb-tkl-wireless`).
  The package list has no USB ids, so `setup.sh` matches USB product names against package titles.
- Install actions that ran: services `devlisterservice`, `system-sensors-service`; `driver-vhid` (virtual HID driver
  installer, `Install VHID Drivers for iCUE 5.exe`); `vcredist`; autorun `iCUE Launcher.exe --autorun`.

## The normal way (2026-10-08)
- The starter only does three things: licence page, fetch cuepkg, `cuepkg install icue-installer`, then starts
  `icue-installer.exe --action=install --locale=xx_YY` (actions: install, uninstall, repair, update, upgrade, reinstall;
  **without `--action` it opens as Uninstall**). `icue-installer.exe` is Qt ("iCUE Main Installer", 880x520) and runs under Wine;
  log in `ProgramData/Corsair/Logs/iCUE5 Main Installer/`. `setup.sh` now does the three steps and hands over to it;
  `--unattended` keeps the direct package install.
- Installing `icue-installer` already pulls in `core` (iCUE itself); the window is for devices and options.
- Making the starter itself run was tried: a stand-in for `WindowsXamlManager` gets it past the first call and into
  "quiet mode enabled", but it then creates `DesktopWindowXamlSource` and real XAML controls even with `--quiet`.
  That needs an actual XAML implementation; dropped.
- `<install>\cuepkg` is a junction to `cuepkg-<version>`; Wine stores it as a `cuepkg?` file with a
  `user.WINEREPARSE` attribute and resolves it fine.

## Device detection (2026-10-08, open)
- Corsair's installer window sits at 77 % forever: it is in "stage 1 device detection" (log ends after
  `cue.mod.hid: Startup enumeration completed: 0`, never prints `Stage 1 devices received`).
- iCUE itself kept **all device enumerators disabled**: `usersession.dll` calls `WTSOpenServerW(NULL)`, Wine returned
  NULL, so the session counted as not local (`Local changed to false`). Fixed by `patches/wine/0008`: now
  `Local changed to true` → `Enable all enumerators` → `Enumeration finished, all devices initialized`.
- **Solved 2026-10-08 (later):** two more Wine gaps. (1) `SetupDiGetDeviceRegistryProperty(SPDRP_ENUMERATOR_NAME)` was
  unsupported; `HidEnumerator.dll` asks it per device and drops the device on failure → `patches/wine/0010`, then
  `Startup enumeration completed: 23`. (2) bragi init reads USB string 5 with `HidD_GetIndexedString`
  ("Read protocol variant USB string failed with 50"): the K70 has strings 4 `Bootloader version: 0.5.14`, 5 `BP00`,
  6 `Firmware version: 7.16.118`. `patches/wine/0011` answers from `HKLM\Software\Wine\UsbStrings`, filled by
  `tools/usb-strings.py` through usbfs (needs the udev rule's `SUBSYSTEM=="usb"` line). Result:
  `K70 CORE RGB TKL WIRELESS (vid=1b1c, pid=2b02) inserted.` How it was found: `WINEDEBUG=+relay` with
  `RelayFromInclude=HidEnumerator;BragiProtocols;BragiDeviceListing` (module names **without** `.dll`).
- Before that: **no device was recognised**, in iCUE and in the installer. Known so far: the HID nodes are there
  (`HID\VID_1B1C&PID_2B02&MI_00…MI_04&COL02`, receiver `PID_2B00`), iCUE opens each of them and reads attributes and
  descriptors, but never writes a report (no bragi handshake). `HidEnumerator.dll` walks `CM_Get_Parent` twice per device:
  here that gives `USB\VID&PID&MI_nn\…` then `ROOT\WINE\WINEBUS`; on Windows the second step is the whole-device node
  `USB\VID&PID\serial`. Faking that node in `CM_Get_Parent` did **not** change anything (tried, reverted).
- Not the cause (checked): the device lister service (it is started on demand and stops after 30 s idle; Wine also ends
  services when the last program exits), `CM_Register_Notification` for device instances (now accepted, `patches/wine/0009`),
  `CM_Get_DevNode_Status` (was an empty stub, now filled in, but iCUE doesn't call it), Bluetooth device watcher (not used).
- Debug logging: `CommonSettings/DebugLogging` in `AppData/Roaming/Corsair/CUE5/config.cuecfg` and `QT_LOGGING_RULES` only
  produce a handful of `qt.*` debug lines; the `cue.mod.*` categories stay at info.
- Next idea: find what `HidEnumerator.dll` rejects (filters are "Add filter"/"filter out insertion"), e.g. by
  disassembling its device-key function, or by comparing with a Windows `SetupDi` dump of the same keyboard.

## First start (no device access yet)
- `iCUE Launcher.exe` → `iCUE.exe`, Qt 6 window opens (DXVK).
- Log: `users/steamuser/AppData/Local/Corsair/Logs/CUE5/*.log`. Seen: device manifest for the K70 loaded; bragi/usb/wmi
  listers start; `Failed to register for device notifications: 52`; CPUID and update services not reachable
  (`QLocalSocket::ServerNotFoundError`, retried forever); `LLAccessAdapter` missing (RAM/motherboard RGB, expected).

## Next
1. Allow device access, start iCUE, see whether the keyboard appears (bragi protocol over HID).
2. If not: check `CorsairDeviceListerService`, the hidraw interfaces Wine exposes (`PROTON_ENABLE_HIDRAW=1` is set by `skj-icue`).
3. Lighting, key remaps, profiles; then the vhid driver (macros / key output), which will need the same kind of work as GG's sshid.
