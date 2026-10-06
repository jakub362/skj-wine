# SKJ Wine

A Wine build that isn't for games. It's for the **Windows apps that come with your hardware**
and other Windows-only software: SteelSeries GG, Corsair iCUE, and later Adobe apps and older
tools. Think "Proton GE, but for apps": one Wine with every fix, service and driver stand-in
those apps need on Linux.

Base: **WineHQ wine-staging 11.19** + the patches in `patches/wine/`, plus per-app setup
scripts that do what the Windows installers can't do under Wine.

![SteelSeries GG controlling a real Aerox 3 Wireless under SKJ Wine on Fedora](apps/steelseries-gg/screenshots/04-aerox3-real.png)

## Status

| App | State |
|---|---|
| **SteelSeries GG** | **Works on real hardware** (Aerox 3 Wireless, Fedora 44): device detected, DPI, RGB/illumination, polling rate, sleep timer, button remaps (once saved), key remaps, battery, GPU-accelerated window (DXVK). Prism lighting (effects, presets), GameSense (Proton prefixes, CS2/Dota 2 config; checked with CS2) work. Prism's audio visualizer stays plain white with a mouse as the only Prism device (GG only samples audio when a per-key keyboard is connected). Aim Tools opens and installs the 3D Aim Trainer (drills not checked yet). Not yet: macros, 3D aim trainer, Moments, Sonar. Details: [`apps/steelseries-gg/NOTES.md`](apps/steelseries-gg/NOTES.md) |
| Corsair iCUE | Not started |
| Winamp (old app test) | Planned |
| Adobe | Long term |

---

## Install (Fedora)

### As an RPM (recommended)

```bash
cd skj-wine && ./build-rpm.sh --install
```

- adds the WineHQ repo if missing and installs **wine-staging 11.19** (needed as the base)
- builds `~/rpmbuild/RPMS/x86_64/skj-wine-0.1.N-1.x86_64.rpm` and installs it:
  - `/opt/skj-wine/` — SKJ Wine (scripts, tools, drivers, patched DLLs)
  - `/opt/skj-wine/dist/` — **private copy of wine-staging** with SKJ's DLLs on top (made in `%post`; your normal Wine is never changed)
  - `/usr/bin/skj-gg` — the GG launcher
  - `/usr/share/applications/skj-steelseries-gg.desktop` — app menu entry (category *Settings*)
  - `/usr/lib/udev/rules.d/70-skj-wine-steelseries.rules` — lets your user open SteelSeries HID devices
- turns on GG autostart at login
- if you had the folder install (below), switches you over and keeps your GG settings

Update: unpack a newer `skj-wine` and run `./build-rpm.sh --install` again.
Remove: `sudo dnf remove skj-wine` (GG settings stay until `skj-gg --remove-data`).

### From the folder (no RPM)

```bash
./install-fedora.sh      # wine-staging 11.19, private Wine in ./dist, udev rule, GG setup
./install-local.sh       # menu entry, ~/.local/bin/skj-gg, autostart   (--remove to undo)
```

The first launch of `skj-gg` sets GG up if needed (downloads the GG installer ~430 MB and
wine-mono ~80 MB, cached in `~/.cache/skj-wine/`; takes a few minutes).

## Using it

```
skj-gg                     open GG (first run: sets everything up)
skj-gg --auto              start GG in the background (what autostart uses)
skj-gg --stop              quit GG and its Wine processes
skj-gg --autostart on|off  start GG at login
skj-gg --setup             redo GG's setup in the existing prefix
skj-gg --gamesense         point games at GG again by hand (skj-gg does it on every start)
skj-gg --gamesense --remove  delete the GameSense files written into game folders
skj-gg --remove-data       delete GG's prefix (all GG settings) - asks first
```

In GG on first start: **I don't have a SteelSeries account → Skip this step for now →
Continue anyway** (or log in), then **Engine** to see your devices.

## Where things are

| What | Path |
|---|---|
| GG Wine prefix (all GG data/settings) | `~/.local/share/skj-wine/steelseries-gg/` (override: `SKJ_GG_PREFIX=…`) |
| GG install inside the prefix | `<prefix>/drive_c/Program Files/SteelSeries/GG/` |
| GG data (databases, logs, coreProps.json) | `<prefix>/drive_c/ProgramData/SteelSeries/GG/` |
| **GG log (all sub-apps)** | `<prefix>/drive_c/ProgramData/SteelSeries/GG/Logs/gg-errorlog.txt` |
| GG UI (Electron) log | `…/GG/Logs/nw-log.txt` |
| SKJ launcher logs | `~/.cache/skj-wine/gg.log`, `setup.log`, `wineboot.log` |
| Download cache | `~/.cache/skj-wine/` (GG installer, wine-mono, DXVK) |
| SKJ Wine's Wine | `/opt/skj-wine/dist/bin/wine` (RPM) or `./dist/bin/wine` (folder) |
| Autostart entry | `~/.config/autostart/skj-steelseries-gg.desktop` |
| GG icon (extracted from GG) | `~/.local/share/icons/hicolor/*/apps/skj-steelseries-gg.png` |

To run Wine commands in the GG prefix by hand:

```bash
export WINEPREFIX=~/.local/share/skj-wine/steelseries-gg PATH=/opt/skj-wine/dist/bin:$PATH   # or ./dist/bin
wine regedit            # registry
wine sc query skjsshid  # our driver service
wineserver -k           # kill everything in the prefix
```

## Troubleshooting

| Symptom | Cause / fix |
|---|---|
| GG doesn't open | `skj-gg --stop`, then `skj-gg`; read `~/.cache/skj-wine/gg.log` and `gg-errorlog.txt` |
| `wine: chdir to … No such file` / "GG install failed" | prefix parent folder missing (fixed in setup.sh); `mkdir -p ~/.local/share/skj-wine` and `skj-gg --setup` |
| Mouse not on the Engine/Gear page | udev rule missing or not applied: `ls /usr/lib/udev/rules.d/70-skj-wine-steelseries.rules`, replug the receiver, check `ls -l /dev/hidraw*` has your user in ACL (`getfacl`). `apps/steelseries-gg/check-mouse.sh` writes a full report |
| Orange "Windows Pointer Options" banner in GG | Wine's default mouse acceleration; setup.sh sets `HKCU\Control Panel\Mouse` MouseSpeed/MouseThreshold1/2 = 0 |
| GG window slow | DXVK missing: `tools/install-dxvk.sh` (`--undo` to remove). Check: `pgrep -af "SteelSeriesGGClient.*gpu-process"` must **not** contain `--disable-gpu` |
| Broken "SteelSeries GG" entry in the menu's **Wine** category | old winemenubuilder entry using the system Wine; `skj-gg` removes it on launch, or run `bin/skj-gg-icons` |
| Menu entry missing | `kbuildsycoca6 --noincremental`; search "SteelSeries" (it's under *Settings*) |
| Button remap sends both buttons | only in GG's *live preview*; press **Save** and the remap is written to the mouse |
| Changing polling rate disconnects the mouse | normal (same on Windows) |
| A game doesn't light up the mouse (GameSense) | start GG **before** the game (the port changes on every GG start). `cat ~/.cache/skj-wine/gamesense.log` shows where the address was written; run `skj-gg --gamesense` to redo it. Prefixes outside Steam/Heroic: `SKJ_GAMESENSE_PREFIXES=/path/to/prefix:/other skj-gg` |
| Audio Visualizer effect leaves the mouse plain white | GG limitation, not Wine: Prism only samples audio when a per-key SteelSeries keyboard is connected. Use another effect |
| Macros don't play | not implemented yet (`skj-inputd`, see roadmap) |
| Prism tab says "Device Not Found" / Prism effects don't reach the mouse | old `crypt32.dll` without patch 0005: update SKJ Wine (`./build-rpm.sh --install`). Check: with `-enableDebugLog`, `gg-errorlog.txt` must not contain `device executor for device … not found` |
| More Wine detail | run with `WINEDEBUG=err+all,warn+hid,warn+setupapi skj-gg`; driver log: `WINEDEBUG=+debugstr` shows `skjsshid:` lines |

## Layout

```
README.md                  this file
docs/DEVELOPMENT.md        how GG is put together, open work, gotchas
install-fedora.sh          folder install: wine-staging 11.19, ./dist, udev rule, GG setup
install-local.sh           menu entry + autostart for the folder install
build-rpm.sh               build/install the RPM (packaging/skj-wine.spec)
bin/skj-gg                 GG launcher
bin/skj-gg-icons           GG icon extraction + winemenubuilder cleanup
bin/skj-gg-gamesense       copies GG's GameSense address into game prefixes, writes the CS2/Dota 2 GSI config
apps/steelseries-gg/       setup.sh (prefix + all fixes), run.sh, check-mouse.sh, NOTES.md, screenshots/
patches/wine/              Wine patches (each fixes a real Wine bug; upstreamable)
lib/bin/wineserver         prebuilt patched wineserver (patch 0007) for wine-staging 11.19
lib/wine/x86_64-windows/   prebuilt patched DLLs (crypt32, secur32, mmdevapi, windows.storage.applicationdata) for wine-11.19
drivers/skjsshid/          skjsshid.sys: WDM stand-in for SteelSeries' KMDF sshid.sys (\\.\SSengine)
tools/certinstall/         certinstall.exe: import a PFX into a Windows cert store (replaces PowerShell)
tools/chaintest/           chaintest.exe: print Wine's cert chain trust status
tools/sstest/              sstest.exe: talk to \\.\SSengine like GG does
tools/metertest/           metertest.exe: print the output device's peak meter and loopback level (checks patch 0006)
tools/install-dxvk.sh      DXVK 3.1.1 into a prefix (--undo)
tools/install-nvidia-libs.sh  NVENC/CUDA wrappers (nvidia-libs 1.0.2) into a prefix (--undo); needs the NVIDIA driver
docs/re-tools/             reverse-engineering helper scripts (see its README)
share/applications/        desktop entry template
packaging/skj-wine.spec    RPM spec
```

## Wine patches

| Patch | Fixes |
|---|---|
| `0001-crypt32-fail-ONLY_NCRYPT-requests` | `CryptAcquireCertificatePrivateKey` ignored `CRYPT_ACQUIRE_ONLY_NCRYPT_KEY_FLAG` → .NET crashed in `NCryptGetProperty` |
| `0002-windows.storage.applicationdata-no-package-identity` | `ApplicationData.Current` worked for unpackaged apps → `Microsoft.Data.Sqlite` crashed |
| `0003-crypt32-pfx-key-spec-and-keyset` | `PFXImportCertStore` stored the wrong key spec and dropped `CRYPT_MACHINE_KEYSET` → NTE_NO_KEY / NTE_BAD_KEYSET |
| `0004-secur32-machine-keyset-containers` | Schannel only looked for key containers in HKCU → TLS servers with LocalMachine certs dropped connections |
| `0007-server-named-pipe-without-io-access-starts-disconnected` | wineserver let clients connect to a pipe instance opened without read/write access (Go's go-winio uses one only to hold the name) → the first client hung. GG Moments never got its capture service's handshake |
| `0006-mmdevapi-audio-meter-information` | `IAudioMeterInformation` (endpoint peak meter) didn't exist → Prism's audio visualizer never sampled audio |
| `0005-crypt32-match-ip-address-alt-names` | SSL chain policy ignored `iPAddress` subjectAltName entries → `https://127.0.0.1` with a localhost cert gave `CERT_E_CN_NO_MATCH`; GGEZ's event proxy to the GG core never connected, so Prism never lit a device |

## Building

### Patched DLLs (only needed when changing a patch)

```bash
sudo dnf install mingw64-gcc mingw32-gcc flex bison gcc make   # Fedora
git clone https://gitlab.winehq.org/wine/wine.git && cd wine && git checkout wine-11.19
for p in ../skj-wine/patches/wine/*.patch; do git apply "$p"; done
git add -A && tools/make_makefiles      # patch 0006 adds a source file
mkdir ../wbuild && cd ../wbuild
../wine/configure --enable-win64 --without-x --without-freetype --without-gstreamer --without-pulse \
  --without-alsa --without-cups --without-sane --without-v4l2 --without-gphoto --without-krb5 \
  --without-netapi --without-opencl --without-pcap --without-usb --without-vulkan --without-wayland \
  --without-dbus --without-gnutls --without-sdl --without-capi --without-oss --without-udev \
  --without-coreaudio --without-inotify --without-fontconfig
make -j$(nproc) dlls/crypt32/x86_64-windows/crypt32.dll dlls/secur32/x86_64-windows/secur32.dll \
  dlls/mmdevapi/x86_64-windows/mmdevapi.dll \
  dlls/windows.storage.applicationdata/x86_64-windows/windows.storage.applicationdata.dll
for d in crypt32 secur32 mmdevapi windows.storage.applicationdata; do
  x86_64-w64-mingw32-strip dlls/$d/x86_64-windows/$d.dll
  cp dlls/$d/x86_64-windows/$d.dll ../skj-wine/lib/wine/x86_64-windows/
done
```

`wineserver` (patch 0007) is different: it must speak wine-**staging**'s protocol, so it is built from
a tree with the staging patches applied (no mingw needed):

```bash
git clone --depth 1 --branch v11.19 https://gitlab.winehq.org/wine/wine-staging.git
git clone --depth 1 --branch wine-11.19 https://gitlab.winehq.org/wine/wine.git wine-st && cd wine-st
python3 ../wine-staging/staging/patchinstall.py DESTDIR=$PWD --all --no-autoconf && tools/make_requests
git apply ../skj-wine/patches/wine/0007-*.patch
mkdir ../sbuild && cd ../sbuild && ../wine-st/configure --enable-win64 --without-mingw --without-x --without-freetype   # + the other --without flags above, but NOT --without-inotify
make -j$(nproc) server/wineserver && strip server/wineserver && cp server/wineserver ../skj-wine/lib/bin/
```

The PE DLLs must match the wine-staging version (11.19) — they talk to Wine's unix side.

### Driver and tools (mingw)

```bash
# drivers/skjsshid
x86_64-w64-mingw32-gcc -O2 -Wall -I/usr/x86_64-w64-mingw32/sys-root/mingw/include/ddk \
  -shared -nostdlib -Wl,--subsystem,native -Wl,--entry,DriverEntry -Wl,--image-base,0x140000000 \
  -o skjsshid.sys skjsshid.c -lntoskrnl
# (Debian/Ubuntu ddk path: /usr/share/mingw-w64/include/ddk)
x86_64-w64-mingw32-gcc -O2 -o certinstall.exe certinstall.c -lcrypt32   # tools/certinstall
x86_64-w64-mingw32-gcc -O2 -o chaintest.exe   chaintest.c   -lcrypt32   # tools/chaintest
x86_64-w64-mingw32-gcc -O2 -o sstest.exe      sstest.c                  # tools/sstest
```

### RPM

`./build-rpm.sh` (version = `0.1.<git commit count>`). The spec doesn't build anything; it
packages the repo and runs `%post` to create `/opt/skj-wine/dist`.

## Adding a new app (pattern from GG)

1. Install the app silently into a fresh prefix (`WINEDLLOVERRIDES=winemenubuilder.exe=d`).
2. Run each of its processes and read the logs; list what its installer failed to do
   (PowerShell, services, certificates, drivers, migrations) and redo it in `apps/<app>/setup.sh`.
3. Real Wine bugs → `patches/wine/` + rebuilt DLL in `lib/wine/`.
4. Kernel drivers it needs → WDM stand-in in `drivers/` (Wine can't load KMDF).
5. Launcher in `bin/`, desktop entry in `share/applications/`, add to the spec's `%files`.

Part of SKJ Tech · skj-tech.online · GitHub: `jakub362/skj-wine`
