#!/usr/bin/env bash
# SKJ Wine - installer. No packages, no root: everything stays in this folder and in your home.
#
#   ./install.sh            set up SKJ Wine's own Wine and put "SKJ Wine" in your app menu
#   ./install.sh --remove   take the menu entries, commands and autostart away again
#                           (your Windows programs and their settings are kept)
#
# What it does:
#   1. downloads Proton-GE (the version SKJ's fixes are built for, about 560 MB) into ./dist
#      and puts SKJ's fixed files on top
#   2. adds the commands skj-wine (the window), skj-gg, skj-icue and skj-run to ~/.local/bin
#   3. adds "SKJ Wine" and "SteelSeries GG" to the app menu
# Apps are installed from the SKJ Wine window. Your normal Wine, if you have one, is not touched.
set -euo pipefail
cd "$(dirname "$(readlink -f "$0")")"; ROOT="$PWD"
BASE="$(cat "$ROOT/lib/BASE")"     # the Proton-GE release lib/ was built against
BASE_URL="https://github.com/GloriousEggroll/proton-ge-custom/releases/download/$BASE/$BASE-x86_64.tar.gz"
BASE_SHA512="7db87e9787e20c35cbdac26018431d5794626b626e4067b050684e45a88cc2ca229d7d263519eafb2e168cde5bef57611065d159d3685aaec152ccb9abe3073f"
BIN="$HOME/.local/bin"; APPS="${XDG_DATA_HOME:-$HOME/.local/share}/applications"
ICONS="${XDG_DATA_HOME:-$HOME/.local/share}/icons/hicolor/scalable/apps"
CACHE="${XDG_CACHE_HOME:-$HOME/.cache}/skj-wine"
AUTOSTART="$HOME/.config/autostart/skj-steelseries-gg.desktop"
say() { printf '\033[1;35m[SKJ Wine]\033[0m %s\n' "$*"; }

if [ "${1:-}" = --remove ]; then
  rm -f "$BIN/skj-wine" "$BIN/skj-gg" "$BIN/skj-run" "$BIN/skj-icue" "$APPS/skj-wine.desktop" "$APPS/skj-steelseries-gg.desktop" \
        "$ICONS/skj-wine.svg" "$AUTOSTART"
  say "Removed the menu entries, commands and autostart. Programs and settings are still in ~/.local/share/skj-wine;"
  say "this folder ($ROOT) can be deleted too."
  exit 0
fi

# --- what's missing on this system (nothing is installed for you) --------------------------
missing=()
python3 -c 'import PySide6' 2>/dev/null || missing+=("python3-pyside6")
for c in curl sqlite3 openssl cabextract; do command -v "$c" >/dev/null || missing+=("$c"); done
command -v wrestool >/dev/null || say "Note: icoutils (wrestool) is not installed - apps will show a generic icon."
if [ ${#missing[@]} -gt 0 ]; then
  say "Missing: ${missing[*]}"
  say "On Fedora: sudo dnf install python3-pyside6 curl sqlite openssl cabextract icoutils    then run this again."
  exit 1
fi

# --- 1. SKJ Wine's own Wine ----------------------------------------------------------------
if [ "$(cat "$ROOT/dist/.skj-base" 2>/dev/null)" != "$BASE" ]; then
  mkdir -p "$CACHE"; TGZ="$CACHE/$BASE-x86_64.tar.gz"
  [ -f "$TGZ" ] || { say "Downloading $BASE (about 560 MB)"; curl -fL --progress-bar -o "$TGZ" "$BASE_URL"; }
  echo "$BASE_SHA512  $TGZ" | sha512sum -c --quiet || { rm -f "$TGZ"; say "The download is damaged, removed it - run this again."; exit 1; }
  say "Unpacking"
  TMP=$(mktemp -d -p "$ROOT"); tar xzf "$TGZ" -C "$TMP" --wildcards '*/files'
  chmod -R u+w "$ROOT/dist" 2>/dev/null || true; rm -rf "$ROOT/dist"
  mv "$TMP"/*/files "$ROOT/dist"; rm -rf "$TMP" "$TGZ"
  chmod -R u+w "$ROOT/dist"
  echo "$BASE" > "$ROOT/dist/.skj-base"
fi
DLLDIR="$ROOT/dist/lib/wine/x86_64-windows"
[ -f "$DLLDIR/crypt32.dll" ] || { say "Could not find Wine's x86_64-windows folder in $ROOT/dist"; exit 1; }
cp --remove-destination "$ROOT"/lib/wine/x86_64-windows/*.dll "$DLLDIR/"
rm -f "$ROOT/dist/bin/wineserver"; install -m755 "$ROOT/lib/bin/wineserver" "$ROOT/dist/bin/wineserver"
say "Wine: $BASE ($("$ROOT/dist/bin/wine" --version)) with SKJ fixes"

# --- 2. commands + 3. app menu -------------------------------------------------------------
chmod +x "$ROOT"/bin/* "$ROOT"/tools/*.sh "$ROOT"/apps/*/*.sh
mkdir -p "$BIN" "$APPS" "$ICONS"
for c in skj-wine skj-gg skj-run skj-icue; do ln -sf "$ROOT/bin/$c" "$BIN/$c"; done
cp "$ROOT/share/icons/skj-wine.svg" "$ICONS/skj-wine.svg"
sed "s|^Exec=skj-wine|Exec=$BIN/skj-wine|" "$ROOT/share/applications/skj-wine.desktop" > "$APPS/skj-wine.desktop"
sed "s|^Exec=skj-gg|Exec=$BIN/skj-gg|" "$ROOT/share/applications/skj-steelseries-gg.desktop" > "$APPS/skj-steelseries-gg.desktop"
# an autostart entry from an older install (RPM) points at the old launcher: keep it on, point it here
[ -f "$AUTOSTART" ] && "$ROOT/bin/skj-gg" --autostart on >/dev/null
update-desktop-database -q "$APPS" 2>/dev/null || true
command -v kbuildsycoca6 >/dev/null && kbuildsycoca6 >/dev/null 2>&1 || true

case ":$PATH:" in *":$BIN:"*) ;; *) say "Note: $BIN is not in your PATH; the app menu entry works anyway." ;; esac
if rpm -q skj-wine >/dev/null 2>&1; then
  say "The old RPM install is still there. Remove it with: sudo dnf remove skj-wine"
  say "(that also removes its device rule - SKJ Wine's window, page \"Wine\", puts it back with one click)"
fi
say "Done. Open \"SKJ Wine\" from the app menu, or run: skj-wine"
