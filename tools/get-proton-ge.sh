#!/usr/bin/env bash
# SKJ Wine - download Proton-GE (GloriousEggroll's Proton build) as a second Wine to choose from.
# Good for games and media-heavy programs: it brings DXVK, vkd3d-proton, NVIDIA helpers and codecs.
# It does NOT carry SKJ's fixes, so hardware apps (SteelSeries GG, ...) stay on SKJ's own Wine.
#
#   tools/get-proton-ge.sh            download the newest release (about 560 MB, 1.6 GB unpacked)
#   tools/get-proton-ge.sh --list     print what is installed
#   tools/get-proton-ge.sh --remove NAME
# Installed to ~/.local/share/skj-wine/runners/NAME; use with: SKJ_RUNNER=NAME skj-run ... (or pick it
# in the SKJ Wine window when adding a program).
set -euo pipefail
RUNNERS="${XDG_DATA_HOME:-$HOME/.local/share}/skj-wine/runners"
CACHE="${XDG_CACHE_HOME:-$HOME/.cache}/skj-wine"
API="https://api.github.com/repos/GloriousEggroll/proton-ge-custom/releases/latest"
say() { printf '\033[1;36m[skj-wine]\033[0m %s\n' "$*"; }
mkdir -p "$RUNNERS" "$CACHE"

case "${1:-}" in
  --list) ls "$RUNNERS" 2>/dev/null || true; exit 0 ;;
  --remove)
    case "${2:-}" in ""|*/*|.*) echo "which one? (see --list)" >&2; exit 1 ;; esac
    rm -rf "${RUNNERS:?}/$2"; say "Removed $2"; exit 0 ;;
esac

JSON=$(curl -fsSL "$API")
URL=$(printf '%s' "$JSON" | grep -o '"browser_download_url": *"[^"]*x86_64\.tar\.gz"' | head -1 | cut -d'"' -f4)
SUM=$(printf '%s' "$JSON" | grep -o '"browser_download_url": *"[^"]*x86_64\.sha512sum"' | head -1 | cut -d'"' -f4)
[ -n "$URL" ] && [ -n "$SUM" ] || { echo "Could not find the newest Proton-GE release on GitHub" >&2; exit 1; }
NAME=$(basename "$URL" -x86_64.tar.gz)
if [ -x "$RUNNERS/$NAME/files/bin/wine" ]; then say "$NAME is already installed"; exit 0; fi

TGZ="$CACHE/$NAME.tar.gz"
say "Downloading $NAME"
curl -fL --progress-bar -o "$TGZ" "$URL"
WANT=$(curl -fsSL "$SUM" | cut -d' ' -f1)
echo "$WANT  $TGZ" | sha512sum -c --quiet || { rm -f "$TGZ"; echo "The download is damaged, removed it - run again" >&2; exit 1; }
say "Unpacking"
TMP=$(mktemp -d -p "$RUNNERS"); tar xzf "$TGZ" -C "$TMP"
mv "$TMP"/*/ "$RUNNERS/$NAME"; rmdir "$TMP"; rm -f "$TGZ"
say "$NAME installed: $("$RUNNERS/$NAME/files/bin/wine" --version)"
