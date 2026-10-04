#!/usr/bin/env bash
# SKJ Wine - start SteelSeries GG.
# On Windows a service starts GGEZ, and GGEZ starts everything else
# (oldGG = SteelSeriesGG.exe, Moments, ...). We start GGEZ directly.
set -euo pipefail
export WINEPREFIX="${1:-$HOME/.local/share/skj-wine/steelseries-gg}"
export WINEDEBUG="${WINEDEBUG:--all}"
cd "$WINEPREFIX/drive_c/Program Files/SteelSeries/GG"
exec wine SteelSeriesGGEZ.exe
