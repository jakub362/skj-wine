#!/usr/bin/env bash
# SKJ Wine - install Corsair iCUE 5 into a prefix.
#
#   apps/corsair-icue/setup.sh [PREFIX]                 no window: iCUE + the plugged-in devices
#                              [--packages a,b,...]     ...or these device packages
#   apps/corsair-icue/setup.sh [PREFIX] --wizard        Corsair's own install window
#
# Corsair's download, "Install iCUE.exe", is a small starter: it shows a licence page, fetches
# Corsair's package manager (cuepkg.exe), has it install the real installer (icue-installer.exe)
# and starts that. The starter's page is built with Windows' XAML, which Wine doesn't have, so it
# crashes before doing anything. The real installer is a normal Qt program and runs fine.
# So this script does the starter's three steps itself and then hands over to Corsair's installer:
# you pick devices and options in its window exactly as on Windows.
# Everything is downloaded from Corsair's own servers (about 450 MB); nothing of it is in this repo.
set -euo pipefail
ROOT="$(cd "$(dirname "$(readlink -f "$0")")/../.." && pwd)"
DIST="${SKJ_WINE_DIST:-$ROOT/dist}"
# Corsair's window currently stops at 77 % (its device detection never finishes under Wine, see NOTES.md),
# so the install without it is the default until that is fixed; --wizard opens Corsair's window.
PREFIX="$HOME/.local/share/skj-wine/corsair-icue"; PACKAGES=""; UNATTENDED=1
while [ $# -gt 0 ]; do
  case "$1" in
    --packages) PACKAGES="$2"; UNATTENDED=1; shift 2 ;;
    --unattended) UNATTENDED=1; shift ;;
    --wizard) UNATTENDED=0; shift ;;
    *) PREFIX="$1"; shift ;;
  esac
done
export PROTON_ENABLE_HIDRAW=1   # so Corsair's installer can see which devices are plugged in
export WINEPREFIX="$PREFIX" PATH="$DIST/bin:$PATH" WINEDEBUG=-all WINEDLLOVERRIDES="winemenubuilder.exe=d"
BASE=https://www3.corsair.com/software/CUE_V5/public/modules/windows
CUEPKG_SHA256=91040133fc2f06f84b07e905bfeffa8f9b52177dc540309b3ba41da2b843fe8a
CACHE="${XDG_CACHE_HOME:-$HOME/.cache}/skj-wine/icue"
INSTALL_WIN='C:\Program Files\Corsair\Corsair iCUE5 Software'
INSTALL="$PREFIX/drive_c/Program Files/Corsair/Corsair iCUE5 Software"
say() { printf '\033[1;36m[skj-wine]\033[0m %s\n' "$*"; }
# cuepkg reports problems in a message box and then waits forever: read the box, close it, fail
cuepkg() {
  local dir="$1"; shift
  ( cd "$dir" && timeout 3600 wine cuepkg.exe "$@" --installdir="$INSTALL_WIN" 2>&1 | tr '\r' '\n' |
      sed 's/\x1b\[[0-9;]*[A-Za-z]//g' | grep -v '^$\|ntsync\|^Downloaded\|^Validated\|^Unpacked\|Progress:\|^\[ *[0-9]*%\]\|Synchronizing' || true ) &
  local job=$!
  while kill -0 $job 2>/dev/null; do
    sleep 5
    local box; box=$(timeout 20 wine "$ROOT/tools/winlist/winlist.exe" close 2>/dev/null | grep -A3 'Package Manager' | grep Static || true)
    [ -z "$box" ] || { say "cuepkg says: $box"; return 1; }
  done
  wait $job || true      # cuepkg's exit code says little; each step is checked by what it left behind
}

[ -x "$DIST/bin/wine" ] || { echo "SKJ Wine has no Wine yet - run $ROOT/install.sh" >&2; exit 1; }
mkdir -p "$CACHE" "$(dirname "$PREFIX")"
if [ ! -d "$PREFIX/drive_c" ]; then
  say "Creating the Wine prefix"
  wine wineboot -i >/dev/null 2>&1; wineserver -w
  "$ROOT/tools/prefix-prep.sh" "$PREFIX" --gpu
fi

# --- 1. Corsair's package manager ----------------------------------------------------------
if ! ls -d "$INSTALL"/cuepkg-*/ >/dev/null 2>&1; then
  ZIP="$CACHE/cuepkg.zip"
  [ -f "$ZIP" ] || { say "Downloading Corsair's package manager"; curl -fL --progress-bar -o "$ZIP" "$BASE/installerpackage/cuepkg.zip"; }
  echo "$CUEPKG_SHA256  $ZIP" | sha256sum -c --quiet || { rm -f "$ZIP"; say "The download is damaged (or Corsair changed it) - removed, run this again."; exit 1; }
  rm -rf "$CACHE/cuepkg"; mkdir -p "$CACHE/cuepkg"; unzip -q "$ZIP" -d "$CACHE/cuepkg"
  [ -f "$INSTALL/cuepkg.registry" ] || cuepkg "$CACHE/cuepkg" init
  cuepkg "$CACHE/cuepkg" set-config RepositoryUrl "$BASE/packages"
  cuepkg "$CACHE/cuepkg" update
  say "Updating the package manager"
  cuepkg "$CACHE/cuepkg" install cuepkg    # the one in the zip is too old for today's packages ("malformed package")
fi
PM=$(ls -d "$INSTALL"/cuepkg-*/ 2>/dev/null | sort -V | tail -1 || true)
[ -n "$PM" ] && [ -f "$PM/cuepkg.exe" ] || { say "The package manager did not install."; exit 1; }
cuepkg "$PM" update

# --- 2. Corsair's installer -----------------------------------------------------------------
if [ "$UNATTENDED" = 0 ]; then
  say "Fetching Corsair's installer (downloads about 450 MB the first time)"
  cuepkg "$PM" install icue-installer
  [ -f "$INSTALL/icue-installer.exe" ] || { say "Corsair's installer did not arrive - see the messages above."; exit 1; }
  # its languages: de en es fr it ja ko pt ru uk zh; anything else gets English
  LOCALE=en_US
  case "${LANG:-}" in de_*|es_*|fr_*|it_*|ja_*|ko_*|pt_*|ru_*|uk_*|zh_*) LOCALE="${LANG%%.*}" ;; esac
  say "Starting Corsair's installer - continue in its window"
  ( cd "$INSTALL" && wine icue-installer.exe --action=install --locale="$LOCALE" >/dev/null 2>&1 ) || true
  wineserver -k 2>/dev/null || true
  [ -f "$INSTALL/iCUE.exe" ] || { say "iCUE is not installed (the installer was closed early?)."; exit 1; }
  say "iCUE is installed in $PREFIX"
  exit 0
fi

# --- 2b. unattended: iCUE + the plugged-in devices, no window ---------------------------------
if [ -z "$PACKAGES" ]; then
  curl -fsL -o "$CACHE/metadata.json" "$BASE/packages/cuepkg-metadata.json"
  PACKAGES=$(python3 - "$CACHE/metadata.json" <<'EOF'
import glob, json, re, sys
# Corsair's list has no USB ids, only titles ("Support for K70 CORE RGB TKL WIRELESS"): match USB product names
def norm(name):
    name = re.sub(r"\bWL\b", "WIRELESS", name.upper())
    name = re.sub(r"\b(CORSAIR|GAMING|KEYBOARD|MOUSE|HEADSET|MECHANICAL|OPTICAL|USB|RECEIVER)\b", " ", name)
    return " ".join(name.split())
titles = {}
for p in json.load(open(sys.argv[1]))["packages"]:
    if p["name"].startswith("dev-"):
        titles[norm(p.get("title", {}).get("en-US", "").replace("Support for", ""))] = p["name"]
found = set()
for dev in glob.glob("/sys/bus/usb/devices/*/idVendor"):
    if open(dev).read().strip() != "1b1c":
        continue
    try:
        product = norm(open(dev.replace("idVendor", "product")).read())
    except OSError:
        continue
    if product in titles:
        found.add(titles[product])
print(",".join(sorted(found)))
EOF
)
  [ -n "$PACKAGES" ] && say "Found: $PACKAGES" || say "No known Corsair device is plugged in - installing iCUE alone (run again with the device connected)."
fi
say "Installing iCUE (downloads about 450 MB the first time)"
# shellcheck disable=SC2086
cuepkg "$PM" install core ${PACKAGES//,/ }
wineserver -k 2>/dev/null || true
[ -f "$INSTALL/iCUE.exe" ] || { say "iCUE did not install - see the messages above."; exit 1; }
say "iCUE is installed in $PREFIX"
