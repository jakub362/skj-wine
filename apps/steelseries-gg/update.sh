#!/usr/bin/env bash
# SKJ Wine - install a SteelSeries GG update.
# GG downloads its updates itself (ProgramData/SteelSeries/GG/updates/*Setup.exe) but hands them
# to a Windows service that doesn't exist here, so nothing gets installed and GG shows an
# "update failed" box on every start. This runs the downloaded installer and redoes the pieces
# of setup.sh that an installer run undoes.
#
# Usage: ./update.sh [prefix-dir]     (GG must not be running; skj-gg calls this before starting GG)
# The prefix as it was is kept next to it as <prefix>.before-update (replaced on the next update).
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"; ROOT="$(cd "$HERE/../.." && pwd)"
[ -x "${SKJ_WINE_DIST:-$ROOT/dist}/bin/wine" ] && export PATH="${SKJ_WINE_DIST:-$ROOT/dist}/bin:$PATH"
export WINEPREFIX="${1:-$HOME/.local/share/skj-wine/steelseries-gg}"
export WINEDEBUG="${WINEDEBUG:--all}"
GGDIR="$WINEPREFIX/drive_c/Program Files/SteelSeries/GG"
PD="$WINEPREFIX/drive_c/ProgramData/SteelSeries/GG"
say() { printf '\033[1;36m[skj-wine]\033[0m %s\n' "$*"; }
version() { tr -d '\r\n ' < "$GGDIR/version.json" 2>/dev/null || true; }

SETUP=$(ls -t "$PD"/updates/*Setup.exe 2>/dev/null | head -1 || true)
[ -n "$SETUP" ] || { say "No downloaded GG update in $PD/updates"; exit 0; }
if pgrep -f 'SteelSeriesGGE[Z]\.exe' >/dev/null; then echo "GG is running - quit it first (skj-gg --stop)" >&2; exit 1; fi

OLD=$(version)
say "Keeping the current GG as $WINEPREFIX.before-update"
rm -rf "$WINEPREFIX.before-update"
cp -a --reflink=auto "$WINEPREFIX" "$WINEPREFIX.before-update"

say "Installing $(basename "$SETUP") (silent)"
WINEDLLOVERRIDES="winemenubuilder.exe=d" wine "$SETUP" /S >/dev/null 2>&1 || true
wineserver -w
[ -f "$GGDIR/SteelSeriesGGEZ.exe" ] || { echo "GG is gone after the update - restore $WINEPREFIX.before-update"; exit 1; }

say "Redoing SKJ Wine's GG fix-ups"
mkdir -p "$GGDIR/db" "$GGDIR/apps/engine/db"
cp "$PD/db/dbconf.yml" "$GGDIR/db/"; cp "$PD/db/dbconf.yml" "$GGDIR/apps/engine/db/"
sqlite3 "$PD/db/ggez.db" "ATTACH '$PD/db/database.db' AS old;
  INSERT OR IGNORE INTO sub_apps (name,is_enabled,created_at,updated_at,is_windows_supported,executable_name,toggle_via_settings,auto_start,is_browserview_supported)
  SELECT name,is_enabled,created_at,updated_at,is_windows_supported,executable_name,toggle_via_settings,auto_start,is_browserview_supported FROM old.sub_apps;" || true
for svc in ssdevfactory sshid msihid ssbthid ssps2 sssmbus; do wine sc config "$svc" start= disabled >/dev/null 2>&1 || true; done
wine sc delete SteelSeriesGGUpdateServiceProxy >/dev/null 2>&1 || true
wineserver -w
rm -f "$PD"/updates/*Setup.exe
# GG remembers "an update is waiting"; on Windows its update service clears that. Without this
# the new version downloads the same patch again and reports another failed install.
sqlite3 "$PD/db/ggez.db" "DELETE FROM app_update_metadata;" || true
say "GG update done: ${OLD:-?} -> $(version)"
