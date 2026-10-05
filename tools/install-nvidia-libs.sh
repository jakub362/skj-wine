#!/usr/bin/env bash
# SKJ Wine - NVIDIA encoder/CUDA wrappers (nvcuda, nvcuvid, nvencodeapi64) for a prefix.
# Apps that record or encode with NVENC (GG Moments does, and has no software fallback) load
# these DLLs; the wrappers pass the calls to the Linux NVIDIA driver's libcuda / libnvidia-encode.
# From https://github.com/SveSop/nvidia-libs (downloaded on the user's PC, not shipped here).
#
# Usage: tools/install-nvidia-libs.sh [prefix]        install
#        tools/install-nvidia-libs.sh [prefix] --undo  remove
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
[ -x "$ROOT/dist/bin/wine" ] && export PATH="$ROOT/dist/bin:$PATH"
export WINEPREFIX="${1:-$HOME/.local/share/skj-wine/steelseries-gg}"
export WINEDEBUG=-all
VER="1.0.2"
SHA="01e8bb6368d088e22d8e8f1d02497214e8db436476021725ef0c0707b7cb1738"
CACHE="${XDG_CACHE_HOME:-$HOME/.cache}/skj-wine"
DLLS="nvcuda nvcuvid nvencodeapi64"
mkdir -p "$CACHE"
say() { printf '\033[1;36m[skj-wine]\033[0m %s\n' "$*"; }

[ -d "$WINEPREFIX/drive_c/windows" ] || { echo "No Wine prefix at $WINEPREFIX"; exit 1; }
SYS64="$WINEPREFIX/drive_c/windows/system32"

if [ "${2:-}" = "--undo" ]; then
  for d in $DLLS; do
    wine reg delete 'HKCU\Software\Wine\DllOverrides' /v "$d" /f >/dev/null 2>&1 || true
    rm -f "$SYS64/$d.dll"
  done
  wineserver -w; say "NVIDIA wrappers removed"; exit 0
fi

if ! ls /usr/lib64/libnvidia-encode.so.1 /usr/lib/x86_64-linux-gnu/libnvidia-encode.so.1 >/dev/null 2>&1; then
  say "No NVIDIA encoder library (libnvidia-encode.so.1) on this system - skipping (only useful with the NVIDIA driver)"
  exit 0
fi

TXZ="$CACHE/nvidia-libs-v$VER.tar.xz"
[ -f "$TXZ" ] || { say "Downloading nvidia-libs $VER"; curl -fL -o "$TXZ" "https://github.com/SveSop/nvidia-libs/releases/download/v$VER/nvidia-libs-v$VER.tar.xz"; }
echo "$SHA  $TXZ" | sha256sum -c --quiet || { rm -f "$TXZ"; echo "nvidia-libs download is corrupt, removed it - run again"; exit 1; }
TMP=$(mktemp -d); tar xf "$TXZ" -C "$TMP"
say "Installing NVIDIA wrappers into $WINEPREFIX"
for d in $DLLS; do
  cp "$TMP/nvidia-libs-v$VER/x64/$d.dll" "$SYS64/$d.dll"
  wine reg add 'HKCU\Software\Wine\DllOverrides' /v "$d" /t REG_SZ /d native /f >/dev/null 2>&1
done
rm -rf "$TMP"
wineserver -w
say "NVIDIA wrappers installed. Undo with: $0 \"$WINEPREFIX\" --undo"
