#!/usr/bin/env bash
# SKJ Wine - put the Windows libraries that ship next to Wine (Proton-GE base) into a prefix.
# Proton does this itself when Steam starts a game; outside Steam nobody does, and then e.g.
# every .NET program dies with "Cannot get symbol u_charsToUChars from libicuuc".
# SKJ's extra libraries (lib/prefix/system32) are things Windows has and Wine doesn't, built as
# ordinary Windows DLLs: mfaacenc.dll = the AAC audio encoder that recorders need.
#
#   tools/prefix-prep.sh PREFIX          ICU (what .NET needs) and SKJ's own extra libraries
#   tools/prefix-prep.sh PREFIX --gpu    also DXVK, vkd3d-proton and NVIDIA's nvapi (games, 3D programs),
#                                        and SKJ's dxgi.dll in front of DXVK's (screen recording)
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
# Screen recording: SKJ's dxgi.dll goes in front of DXVK's (which becomes dxvk_dxgi.dll); see libs/skjdxgi
if [ "${2:-}" = --gpu ] && [ -f "$ROOT/lib/prefix/dxgi/dxgi.dll" ] && [ -f "$DIST/lib/wine/dxvk/x86_64-windows/dxgi.dll" ]; then
  cp --remove-destination "$DIST/lib/wine/dxvk/x86_64-windows/dxgi.dll" "$WINEPREFIX/drive_c/windows/system32/dxvk_dxgi.dll"
  cp --remove-destination "$ROOT/lib/prefix/dxgi/dxgi.dll" "$WINEPREFIX/drive_c/windows/system32/dxgi.dll"
fi
for dll in "$ROOT/lib/prefix/system32"/*.dll; do
  [ -f "$dll" ] || continue
  cp --remove-destination "$dll" "$WINEPREFIX/drive_c/windows/system32/"
  wine regsvr32 /s "$(basename "$dll")" >/dev/null 2>&1 || echo "could not register $(basename "$dll")" >&2
done
wineserver -w
