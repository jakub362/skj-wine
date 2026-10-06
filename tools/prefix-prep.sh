#!/usr/bin/env bash
# SKJ Wine - put the Windows libraries that ship next to Wine (Proton-GE base) into a prefix.
# Proton does this itself when Steam starts a game; outside Steam nobody does, and then e.g.
# every .NET program dies with "Cannot get symbol u_charsToUChars from libicuuc".
#
#   tools/prefix-prep.sh PREFIX          ICU only (what .NET needs)
#   tools/prefix-prep.sh PREFIX --gpu    also DXVK, vkd3d-proton and NVIDIA's nvapi (games, 3D programs)
set -euo pipefail
ROOT="$(cd "$(dirname "$(readlink -f "$0")")/.." && pwd)"
DIST="${SKJ_WINE_DIST:-$ROOT/dist}"
export PATH="$DIST/bin:$PATH" WINEPREFIX="$1" WINEDEBUG=-all
[ -d "$WINEPREFIX/drive_c/windows/system32" ] || { echo "No Wine prefix at $WINEPREFIX" >&2; exit 1; }
parts=(icu); [ "${2:-}" = --gpu ] && parts+=(dxvk vkd3d-proton nvapi)
for part in "${parts[@]}"; do
  for dll in "$DIST/lib/wine/$part/x86_64-windows"/*.dll; do
    [ -f "$dll" ] || continue
    name=$(basename "$dll")
    cp --remove-destination "$dll" "$WINEPREFIX/drive_c/windows/system32/$name"
    [ -f "$DIST/lib/wine/$part/i386-windows/$name" ] && [ -d "$WINEPREFIX/drive_c/windows/syswow64" ] &&
      cp --remove-destination "$DIST/lib/wine/$part/i386-windows/$name" "$WINEPREFIX/drive_c/windows/syswow64/$name"
    # ICU has no Wine built-in to override; the others replace Wine's own d3d/nvapi
    [ "$part" = icu ] || wine reg add 'HKCU\Software\Wine\DllOverrides' /v "${name%.dll}" /d native /f >/dev/null 2>&1
  done
done
wineserver -w
