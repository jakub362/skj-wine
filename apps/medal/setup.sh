#!/usr/bin/env bash
# SKJ Wine - install Medal (medal.tv, game clip recorder) into its own prefix.
#
#   apps/medal/setup.sh [PREFIX]
#
# What Medal needs beyond a fresh prefix:
#   - fonts (prefix-prep): Medal's window is Electron 43 / Chromium 150, which overflows its stack
#     when DirectWrite finds no UI font
#   - Microsoft .NET Framework 4.8: the recorder (MedalEncoder.exe) is a .NET Framework program;
#     under Wine Mono it connects to Medal and then never reports ready. The .NET installer
#     doesn't bring the loader (mscoree.dll) on Windows 10, so it is taken from .NET 4.0's package.
#   - websocket.dll with the WebSocket protocol component (patches/wine/0012): .NET Framework's
#     WebSocket client runs on it
#   - the screen capture pieces (prefix-prep --gpu: DXVK + SKJ's dxgi.dll) and NVENC (nvidia-libs)
# Microsoft's and Medal's installers are downloaded from their own servers; nothing of them is in
# this repo.
set -euo pipefail
ROOT="$(cd "$(dirname "$(readlink -f "$0")")/../.." && pwd)"
DIST="${SKJ_WINE_DIST:-$ROOT/dist}"
PREFIX="${1:-$HOME/.local/share/skj-wine/prefixes/medal}"
export WINEPREFIX="$PREFIX" PATH="$DIST/bin:$PATH" WINEDEBUG=-all WINEDLLOVERRIDES="winemenubuilder.exe=d"
CACHE="${XDG_CACHE_HOME:-$HOME/.cache}/skj-wine/downloads"
NDP48_URL=https://download.visualstudio.microsoft.com/download/pr/2d6bb6b2-226a-4baa-bdec-798822606ff1/8494001c276a4b96804cde7829c04d7f/ndp48-x86-x64-allos-enu.exe
NDP48_SHA256=68c9986a8dcc0214d909aa1f31bee9fb5461bb839edca996a75b08ddffc1483f
NDP40_URL=https://download.microsoft.com/download/9/5/A/95A9616B-7A37-4AF6-BC36-D6EA96C8DAAE/dotNetFx40_Full_x86_x64.exe
NDP40_SHA256=65e064258f2e418816b304f646ff9e87af101e4c9552ab064bb74d281c38659f
MEDAL_URL=https://install.medal.tv/MedalSetup.exe
say() { printf '\033[1;36m[skj-wine]\033[0m %s\n' "$*"; }
fetch() { # url sha256 file
  [ -f "$3" ] || { say "Downloading $(basename "$3")"; curl -fL --progress-bar -o "$3.part" "$1" && mv "$3.part" "$3"; }
  [ -z "$2" ] || echo "$2  $3" | sha256sum -c --quiet || { rm -f "$3"; say "$(basename "$3") is damaged - removed, run this again."; exit 1; }
}

[ -x "$DIST/bin/wine" ] || { echo "SKJ Wine has no Wine yet - run $ROOT/install.sh" >&2; exit 1; }
mkdir -p "$CACHE" "$(dirname "$PREFIX")"
if [ ! -d "$PREFIX/drive_c" ]; then
  say "Creating the Wine prefix"
  wine wineboot -i >/dev/null 2>&1; wineserver -w
fi
"$ROOT/tools/prefix-prep.sh" "$PREFIX" --gpu
"$ROOT/tools/install-nvidia-libs.sh" "$PREFIX" || say "NVIDIA encoder wrappers not installed - Medal then encodes on the CPU"
cp --remove-destination "$DIST/lib/wine/x86_64-windows/websocket.dll" "$PREFIX/drive_c/windows/system32/websocket.dll"

# --- .NET Framework 4.8 -------------------------------------------------------------------------
SYS32="$PREFIX/drive_c/windows/system32"
if [ "$(wine reg query 'HKLM\Software\Microsoft\NET Framework Setup\NDP\v4\Full' /v Release 2>/dev/null | grep -o '0x[0-9a-f]*')" != 0x80eb1 ] \
   || ! [ -f "$PREFIX/drive_c/windows/Microsoft.NET/Framework64/v4.0.30319/clr.dll" ]; then
  fetch "$NDP48_URL" "$NDP48_SHA256" "$CACHE/ndp48.exe"
  say "Installing .NET Framework 4.8 (a minute)"
  # Wine Mono pretends to be .NET 4.8.1; the installer then does nothing
  wine reg delete 'HKLM\Software\Microsoft\NET Framework Setup\NDP\v4' /f >/dev/null 2>&1 || true
  wine reg delete 'HKLM\Software\Wow6432Node\Microsoft\NET Framework Setup\NDP\v4' /f >/dev/null 2>&1 || true
  wine reg add 'HKCU\Software\Wine\DllOverrides' /v mscoree /d native /f >/dev/null
  wine winecfg -v win7 >/dev/null 2>&1
  wine "$CACHE/ndp48.exe" /q /norestart >/dev/null 2>&1 || true
  wine winecfg -v win10 >/dev/null 2>&1
  wineserver -w
fi
if [ ! -f "$SYS32/mscoree.dll" ] || grep -q 'Wine builtin DLL' "$SYS32/mscoree.dll"; then   # still Wine's placeholder
  command -v 7z >/dev/null || { say "Needs 7z (Fedora: sudo dnf install p7zip-plugins)"; exit 1; }
  fetch "$NDP40_URL" "$NDP40_SHA256" "$CACHE/dotnet40.exe"
  say "Adding the .NET loader (mscoree.dll)"
  T=$(mktemp -d); 7z x -y -o"$T" "$CACHE/dotnet40.exe" netfx_Core.mzz >/dev/null && 7z x -y -o"$T/core" "$T/netfx_Core.mzz" >/dev/null
  cp "$T/core/_003_mscoree_dll_amd64" "$SYS32/mscoree.dll"
  cp "$T/core/_003_mscoree_dll_x86" "$PREFIX/drive_c/windows/syswow64/mscoree.dll"
  rm -rf "$T"
fi

# --- Medal --------------------------------------------------------------------------------------
fetch "$MEDAL_URL" "" "$CACHE/MedalSetup.exe"   # always Medal's newest; it updates itself anyway
if [ ! -f "$PREFIX/drive_c/users/steamuser/AppData/Local/Medal/current/Medal.exe" ]; then
  say "Running Medal's installer"
  ( cd "$CACHE" && timeout 900 wine MedalSetup.exe >/dev/null 2>&1 ) || true
fi
[ -f "$PREFIX/drive_c/users/steamuser/AppData/Local/Medal/current/Medal.exe" ] || { say "Medal did not install."; exit 1; }
# show it in the SKJ Wine window under "Your Windows programs" (Run, app menu entry, ...)
python3 - "$PREFIX/drive_c/users/steamuser/AppData/Local/Medal/current/Medal.exe" <<'EOF'
import json, os, sys
path = os.path.join(os.environ.get("XDG_CONFIG_HOME") or os.path.expanduser("~/.config"), "skj-wine", "apps.json")
try:
    apps = json.load(open(path))
except (OSError, ValueError):
    apps = []
if not any(a.get("slug") == "medal" for a in apps):
    apps.append({"name": "Medal", "slug": "medal", "exe": sys.argv[1]})
    os.makedirs(os.path.dirname(path), exist_ok=True)
    json.dump(apps, open(path, "w"), indent=2)
EOF
say "Medal is installed in $PREFIX"
