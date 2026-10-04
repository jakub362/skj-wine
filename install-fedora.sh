#!/usr/bin/env bash
# SKJ Wine – Fedora installer
#
# 1. installs wine-staging 11.19 from WineHQ (the version our DLLs are built for)
# 2. makes a PRIVATE copy of it in ./dist and puts the SKJ-patched DLLs on top
#    (your normal Wine for Steam/Lutris/etc. is not touched)
# 3. lets your user open SteelSeries HID devices (udev rule, needed for GG to talk to the mouse)
# 4. installs SteelSeries GG into its own prefix
#
# Run from the skj-wine folder:  ./install-fedora.sh
set -euo pipefail
cd "$(dirname "$0")"
ROOT="$PWD"
WINE_VER="11.19"
say() { printf '\033[1;35m[SKJ Wine]\033[0m %s\n' "$*"; }

FEDORA=$(rpm -E %fedora)
say "Fedora $FEDORA detected"

# --- 1. WineHQ repo + wine-staging ---------------------------------------------
if [ ! -f /etc/yum.repos.d/winehq.repo ]; then
  say "Adding the WineHQ repository"
  REPO="https://dl.winehq.org/wine-builds/fedora/$FEDORA/winehq.repo"
  sudo dnf config-manager addrepo --from-repofile="$REPO" 2>/dev/null \
    || sudo dnf config-manager --add-repo "$REPO"
fi
if [ "$(/opt/wine-staging/bin/wine --version 2>/dev/null)" != "wine-$WINE_VER (Staging)" ]; then
  say "Installing wine-staging $WINE_VER"
  sudo dnf install -y "winehq-staging-$WINE_VER*" "wine-staging-$WINE_VER*" \
    || { say "Could not install wine-staging $WINE_VER from WineHQ for Fedora $FEDORA."; exit 1; }
fi
sudo dnf install -y sqlite openssl curl cabextract >/dev/null

# --- 2. private SKJ Wine copy ---------------------------------------------------
if [ ! -x "$ROOT/dist/bin/wine" ] || [ "$("$ROOT/dist/bin/wine" --version)" != "wine-$WINE_VER (Staging)" ]; then
  say "Making a private copy of wine-staging in $ROOT/dist (reflink: almost no extra space on btrfs)"
  rm -rf "$ROOT/dist"
  cp -a --reflink=auto /opt/wine-staging "$ROOT/dist"
fi
say "Applying SKJ Wine patched DLLs"
DLLDIR=$(dirname "$(find "$ROOT/dist" -path '*/wine/x86_64-windows/crypt32.dll' | head -1)")
[ -d "$DLLDIR" ] || { say "Could not find Wine's x86_64-windows folder in $ROOT/dist"; exit 1; }
cp "$ROOT"/lib/wine/x86_64-windows/*.dll "$DLLDIR/"

# --- 3. SteelSeries HID access --------------------------------------------------
RULE=/etc/udev/rules.d/70-skj-wine-steelseries.rules
if [ ! -f "$RULE" ]; then
  say "Adding udev rule so Wine can open SteelSeries HID devices (vendor 1038)"
  echo 'SUBSYSTEM=="hidraw", ATTRS{idVendor}=="1038", TAG+="uaccess"' | sudo tee "$RULE" >/dev/null
  sudo udevadm control --reload-rules
  sudo udevadm trigger --subsystem-match=hidraw
fi

# --- 4. SteelSeries GG ----------------------------------------------------------
export PATH="$ROOT/dist/bin:$PATH"
"$ROOT/apps/steelseries-gg/setup.sh"

say "All done. Plug in the Aerox and start GG with:  $ROOT/apps/steelseries-gg/run.sh"
say "Then check:  $ROOT/apps/steelseries-gg/check-mouse.sh"
