#!/usr/bin/env bash
# SKJ Wine - install DXVK (DirectX 9/10/11 -> Vulkan) into a prefix.
# Electron/Chromium apps (like the SteelSeries GG window) render through D3D11;
# Wine's built-in path goes D3D11 -> OpenGL and is slow. DXVK is what Proton uses.
#
# Usage: tools/install-dxvk.sh [prefix]        install
#        tools/install-dxvk.sh [prefix] --undo  go back to Wine's built-in d3d
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
[ -x "${SKJ_WINE_DIST:-$ROOT/dist}/bin/wine" ] && export PATH="${SKJ_WINE_DIST:-$ROOT/dist}/bin:$PATH"
export WINEPREFIX="${1:-$HOME/.local/share/skj-wine/steelseries-gg}"
export WINEDEBUG=-all
DXVK_VER="3.1.1"
CACHE="${XDG_CACHE_HOME:-$HOME/.cache}/skj-wine"
DLLS="d3d9 d3d10core d3d11 dxgi"
mkdir -p "$CACHE"
say() { printf '\033[1;36m[skj-wine]\033[0m %s\n' "$*"; }

[ -d "$WINEPREFIX/drive_c/windows" ] || { echo "No Wine prefix at $WINEPREFIX"; exit 1; }
SYS64="$WINEPREFIX/drive_c/windows/system32"
SYS32="$WINEPREFIX/drive_c/windows/syswow64"

if [ "${2:-}" = "--undo" ]; then
  say "Removing DXVK overrides (Wine's built-in d3d will be used again)"
  for d in $DLLS; do wine reg delete 'HKCU\Software\Wine\DllOverrides' /v "$d" /f >/dev/null 2>&1 || true; done
  wine wineboot -u >/dev/null 2>&1 || true   # restores Wine's own DLL files
  wineserver -w
  exit 0
fi

if ! command -v vulkaninfo >/dev/null || ! vulkaninfo --summary >/dev/null 2>&1; then
  say "Note: couldn't confirm a working Vulkan driver (vulkaninfo). On NVIDIA it comes with the driver; continuing."
fi

TGZ="$CACHE/dxvk-$DXVK_VER.tar.gz"
[ -f "$TGZ" ] || { say "Downloading DXVK $DXVK_VER"; curl -fL -o "$TGZ" "https://github.com/doitsujin/dxvk/releases/download/v$DXVK_VER/dxvk-$DXVK_VER.tar.gz"; }
TMP=$(mktemp -d); tar xzf "$TGZ" -C "$TMP"
say "Installing DXVK $DXVK_VER into $WINEPREFIX"
for d in $DLLS; do
  cp "$TMP/dxvk-$DXVK_VER/x64/$d.dll" "$SYS64/$d.dll"
  [ -d "$SYS32" ] && cp "$TMP/dxvk-$DXVK_VER/x32/$d.dll" "$SYS32/$d.dll"
  wine reg add 'HKCU\Software\Wine\DllOverrides' /v "$d" /t REG_SZ /d native /f >/dev/null 2>&1
done
rm -rf "$TMP"
wineserver -w
say "DXVK installed. Undo with: $0 \"$WINEPREFIX\" --undo"
