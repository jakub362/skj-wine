#!/usr/bin/env bash
# SKJ Wine - put SteelSeries GG in your app menu and autostart (no RPM).
# Uses this folder as the SKJ Wine install. Run install-fedora.sh first.
#   ./install-local.sh            install menu entry + skj-gg command + autostart
#   ./install-local.sh --remove   remove them (keeps your GG settings)
set -euo pipefail
cd "$(dirname "$0")"; ROOT="$PWD"
BIN="$HOME/.local/bin"; APPS="$HOME/.local/share/applications"

if [ "${1:-}" = --remove ]; then
  rm -f "$BIN/skj-gg" "$APPS/skj-steelseries-gg.desktop"
  "$ROOT/bin/skj-gg" --autostart off >/dev/null
  echo "Removed SKJ Wine GG menu entry, command and autostart (settings kept; delete with: skj-gg --remove-data)"
  exit 0
fi

[ -x "$ROOT/dist/bin/wine" ] || { echo "Run ./install-fedora.sh first (no $ROOT/dist)"; exit 1; }
command -v wrestool >/dev/null || sudo dnf install -y icoutils >/dev/null
chmod +x "$ROOT"/bin/*
mkdir -p "$BIN" "$APPS"
ln -sf "$ROOT/bin/skj-gg" "$BIN/skj-gg"
sed "s|^Exec=skj-gg|Exec=$BIN/skj-gg|" "$ROOT/share/applications/skj-steelseries-gg.desktop" > "$APPS/skj-steelseries-gg.desktop"
"$ROOT/bin/skj-gg-icons" >/dev/null 2>&1 || true
"$ROOT/bin/skj-gg" --autostart on >/dev/null
update-desktop-database -q "$APPS" 2>/dev/null || true
echo "Done: 'SteelSeries GG' is in your app menu, 'skj-gg' works in a terminal, and GG starts at login."
echo "Turn autostart off with: skj-gg --autostart off"
