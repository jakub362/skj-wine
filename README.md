# SKJ Wine

A Wine build that isn't for games. It's for the **Windows apps that come with your hardware**: SteelSeries GG, Corsair iCUE and other vendor tools that need Windows services, certificates and device access, so they can run on Linux.

Base: **wine-staging** + the patches in `patches/wine/`, plus per-app setup scripts that do what the Windows installers can't do under Wine.

![SteelSeries GG controlling a real Aerox 3 Wireless under SKJ Wine on Fedora](apps/steelseries-gg/screenshots/04-aerox3-real.png)

## Status

| App | State |
|---|---|
| SteelSeries GG | **Boots fully**: GGEZ, GG core, Engine, Prism, Moments, the GG window, and `skjsshid.sys` standing in for SteelSeries' kernel driver. **Works on real hardware**: Aerox 3 Wireless detected on Fedora 44, DPI changes apply. See `apps/steelseries-gg/NOTES.md` |
| Corsair iCUE | Not started |

## Install on Fedora

**As an RPM (recommended):**

```bash
cd skj-wine && ./build-rpm.sh --install
```

Builds `skj-wine-*.rpm` and installs it: SKJ Wine goes to `/opt/skj-wine`, **SteelSeries GG appears in your app menu**, `skj-gg` works in a terminal, GG starts at login (`skj-gg --autostart off` to stop that). The first launch sets GG up (a few minutes). Needs WineHQ's wine-staging 11.19 (the script adds the WineHQ repo); SKJ Wine keeps its own private copy, so your normal Wine is untouched. Remove with `sudo dnf remove skj-wine` (GG settings stay in `~/.local/share/skj-wine` until `skj-gg --remove-data`).

**From the folder (no RPM):** `./install-fedora.sh && ./install-local.sh`

## Using it

```
skj-gg                     open GG
skj-gg --stop              quit GG
skj-gg --autostart on|off  start GG at login
skj-gg --setup             redo GG's setup
skj-gg --remove-data       delete GG's settings/prefix
```

## Layout

```
patches/wine/          Wine patches (each one fixes a real bug, upstreamable)
tools/certinstall/     certinstall.exe – imports a PFX into a Windows cert store (replaces PowerShell)
tools/chaintest/       chaintest.exe – prints Wine's certificate chain trust status (debugging)
tools/sstest/          sstest.exe – exercises \\.\SSengine like GG does
drivers/skjsshid/      skjsshid.sys – WDM replacement for SteelSeries' KMDF sshid.sys
lib/wine/               prebuilt patched DLLs (wine-11.19, x86_64)
install-fedora.sh      one-shot Fedora installer (folder install)
install-local.sh       app menu + autostart for the folder install
build-rpm.sh           build/install the Fedora RPM (packaging/skj-wine.spec)
bin/skj-gg             GG launcher (first-run setup, single instance, autostart)
apps/<app>/setup.sh    builds a prefix for that app
apps/<app>/check-mouse.sh  (GG) report whether devices are visible
apps/<app>/run.sh      starts it
apps/<app>/NOTES.md    what we found
```

## Building

```bash
git clone https://gitlab.winehq.org/wine/wine.git && cd wine && git checkout wine-11.19
for p in ../skj-wine/patches/wine/*.patch; do git apply "$p"; done
# then build normally, or rebuild only the touched DLLs and drop them into an existing wine-staging
```

`tools/certinstall`: `x86_64-w64-mingw32-gcc -O2 -o certinstall.exe certinstall.c -lcrypt32`

Part of SKJ Tech · skj-tech.online
