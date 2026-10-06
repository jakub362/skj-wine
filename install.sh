#!/usr/bin/env bash
# SKJ Wine - installer. No packages, no root: everything stays in this folder and in your home.
#
#   ./install.sh            set up SKJ Wine's own Wine and put "SKJ Wine" in your app menu
#   ./install.sh --remove   take the menu entries, commands and autostart away again
#                           (your Windows programs and their settings are kept)
#
# What it does:
#   1. gets wine-staging 11.19 into ./dist (copies an installed /opt/wine-staging if it is that
#      version, otherwise downloads a portable build) and puts SKJ's fixed files on top
#   2. adds the commands skj-wine (the window), skj-gg and skj-run to ~/.local/bin
#   3. adds "SKJ Wine" and "SteelSeries GG" to the app menu
# Apps are installed from the SKJ Wine window. Your normal Wine, if you have one, is not touched.
set -euo pipefail
cd "$(dirname "$(readlink -f "$0")")"; ROOT="$PWD"
WINE_VER="11.19"
WINE_URL="https://github.com/Kron4ek/Wine-Builds/releases/download/$WINE_VER/wine-$WINE_VER-staging-amd64-wow64.tar.xz"
WINE_SHA="c89f69cf22f6664e3791210fa3b40c6e9e6d2e27a9aaa466f5a26d9c56664a09"
BIN="$HOME/.local/bin"; APPS="${XDG_DATA_HOME:-$HOME/.local/share}/applications"
ICONS="${XDG_DATA_HOME:-$HOME/.local/share}/icons/hicolor/scalable/apps"
CACHE="${XDG_CACHE_HOME:-$HOME/.cache}/skj-wine"
AUTOSTART="$HOME/.config/autostart/skj-steelseries-gg.desktop"
say() { printf '\033[1;35m[SKJ Wine]\033[0m %s\n' "$*"; }

if [ "${1:-}" = --remove ]; then
  rm -f "$BIN/skj-wine" "$BIN/skj-gg" "$BIN/skj-run" "$APPS/skj-wine.desktop" "$APPS/skj-steelseries-gg.desktop" \
        "$ICONS/skj-wine.svg" "$AUTOSTART"
  say "Removed the menu entries, commands and autostart. Programs and settings are still in ~/.local/share/skj-wine;"
  say "this folder ($ROOT) can be deleted too."
  exit 0
fi

# --- what's missing on this system (nothing is installed for you) --------------------------
missing=()
python3 -c 'import PySide6' 2>/dev/null || missing+=("python3-pyside6")
for c in curl sqlite3 openssl cabextract wrestool; do command -v "$c" >/dev/null || missing+=("$c"); done
if [ ${#missing[@]} -gt 0 ]; then
  say "Missing: ${missing[*]}"
  say "On Fedora: sudo dnf install python3-pyside6 curl sqlite openssl cabextract icoutils    then run this again."
  exit 1
fi

# --- 1. SKJ Wine's own Wine ----------------------------------------------------------------
if [ "$("$ROOT/dist/bin/wine" --version 2>/dev/null)" != "wine-$WINE_VER (Staging)" ]; then
  rm -rf "$ROOT/dist"
  if [ "$(/opt/wine-staging/bin/wine --version 2>/dev/null)" = "wine-$WINE_VER (Staging)" ]; then
    say "Copying the installed wine-staging $WINE_VER into $ROOT/dist"
    cp -a --reflink=auto /opt/wine-staging "$ROOT/dist"
  else
    mkdir -p "$CACHE"; TXZ="$CACHE/$(basename "$WINE_URL")"
    [ -f "$TXZ" ] || { say "Downloading wine-staging $WINE_VER (about 100 MB)"; curl -fL -o "$TXZ" "$WINE_URL"; }
    echo "$WINE_SHA  $TXZ" | sha256sum -c --quiet || { rm -f "$TXZ"; say "The download is damaged, removed it - run this again."; exit 1; }
    TMP=$(mktemp -d -p "$ROOT"); tar xf "$TXZ" -C "$TMP"; mv "$TMP"/wine-* "$ROOT/dist"; rmdir "$TMP"
  fi
fi
DLLDIR=$(dirname "$(find "$ROOT/dist" -path '*/wine/x86_64-windows/crypt32.dll' | head -1)")
[ -d "$DLLDIR" ] || { say "Could not find Wine's x86_64-windows folder in $ROOT/dist"; exit 1; }
cp "$ROOT"/lib/wine/x86_64-windows/*.dll "$DLLDIR/"
install -m755 "$ROOT/lib/bin/wineserver" "$ROOT/dist/bin/wineserver"
say "Wine: $("$ROOT/dist/bin/wine" --version) with SKJ fixes"

# --- 2. commands + 3. app menu -------------------------------------------------------------
chmod +x "$ROOT"/bin/* "$ROOT"/tools/*.sh "$ROOT"/apps/*/*.sh
mkdir -p "$BIN" "$APPS" "$ICONS"
for c in skj-wine skj-gg skj-run; do ln -sf "$ROOT/bin/$c" "$BIN/$c"; done
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
