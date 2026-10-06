#!/usr/bin/env bash
# SKJ Wine - start SteelSeries GG.
# On Windows a service starts GGEZ, and GGEZ starts everything else
# (oldGG = SteelSeriesGG.exe, Engine, Prism, Moments, the UI). We start GGEZ directly.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
[ -x "${SKJ_WINE_DIST:-$ROOT/dist}/bin/wine" ] && export PATH="${SKJ_WINE_DIST:-$ROOT/dist}/bin:$PATH"
export WINEPREFIX="${1:-$HOME/.local/share/skj-wine/steelseries-gg}"
export WINEDEBUG="${WINEDEBUG:--all}"
cd "$WINEPREFIX/drive_c/Program Files/SteelSeries/GG"
exec wine SteelSeriesGGEZ.exe
