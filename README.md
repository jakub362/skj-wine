# SKJ Wine

A Wine build that isn't for games. It's for the **Windows apps that come with your hardware**: SteelSeries GG, Corsair iCUE and other vendor tools that need Windows services, certificates and device access, so they can run on Linux.

Base: **wine-staging** + the patches in `patches/wine/`, plus per-app setup scripts that do what the Windows installers can't do under Wine.

![SteelSeries GG running under SKJ Wine](apps/steelseries-gg/screenshots/03-engine.png)

## Status

| App | State |
|---|---|
| SteelSeries GG | **Boots fully**: GGEZ, GG core, Engine, Prism, Moments and the GG window. Next: real-mouse test. See `apps/steelseries-gg/NOTES.md` |
| Corsair iCUE | Not started |

## Install on Fedora

```bash
cd skj-wine && ./install-fedora.sh
```

Installs wine-staging 11.19 from WineHQ, makes a private copy in `dist/` with the SKJ-patched DLLs on top (`lib/wine/x86_64-windows/`, built from `patches/wine/` against wine-11.19), adds a udev rule so Wine can open SteelSeries HID devices, and sets up GG. Your normal Wine is not touched.

## Layout

```
patches/wine/          Wine patches (each one fixes a real bug, upstreamable)
tools/certinstall/     certinstall.exe – imports a PFX into a Windows cert store (replaces PowerShell)
tools/chaintest/       chaintest.exe – prints Wine's certificate chain trust status (debugging)
lib/wine/               prebuilt patched DLLs (wine-11.19, x86_64)
install-fedora.sh      one-shot Fedora installer
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
