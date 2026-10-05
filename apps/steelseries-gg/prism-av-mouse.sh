#!/usr/bin/env bash
# Optional tweak: make Prism's Audio Visualizer effect work when the only Prism device is a
# mouse (or another device without per-key "bitmap" coordinates).
#
# Not a Wine problem: Prism only asks its audio library for data when some connected device
# reports a bitmap width (per-key keyboards do, up to 22 columns). With just a mouse the width
# is 0, the library returns nothing and the LEDs stay on the effect's background colour (white
# in "Light" mode). This changes one check in the user's own SteelSeriesPrism.exe so a width
# of 0 counts as 1; the mouse then gets GG's volume colours.
#
#   prism-av-mouse.sh [prefix]          apply (keeps SteelSeriesPrism.exe.skj-orig)
#   prism-av-mouse.sh --undo [prefix]   put the original file back
#
# Only for the exact Prism build it was made for (GG 120.0.0); any other build is left alone.
# Quit GG first (skj-gg --stop). A GG update replaces the file, run this again afterwards if
# the build still matches.
set -euo pipefail
UNDO=0; [ "${1:-}" = --undo ] && { UNDO=1; shift; }
PREFIX="${1:-${WINEPREFIX:-$HOME/.local/share/skj-wine/steelseries-gg}}"
EXE="$PREFIX/drive_c/Program Files/SteelSeries/GG/apps/engine/prism/SteelSeriesPrism.exe"
[ -f "$EXE" ] || { echo "no Prism at $EXE" >&2; exit 1; }
if pgrep -f 'SteelSeriesPrism\.exe' >/dev/null; then echo "GG is running - quit it first (skj-gg --stop)" >&2; exit 1; fi

if [ "$UNDO" = 1 ]; then
  [ -f "$EXE.skj-orig" ] || { echo "nothing to undo"; exit 0; }
  mv -f "$EXE.skj-orig" "$EXE"; echo "original Prism restored"; exit 0
fi

python3 - "$EXE" <<'EOF'
import hashlib, shutil, struct, sys
path = sys.argv[1]
data = bytearray(open(path, 'rb').read())
ORIG_SHA = 'ec5c6265eb8e819eedbefa938dade709fa4f4ebd8c3b31ca01d6e78a85df48d1'   # GG 120.0.0
BASE, TEXT_VA, TEXT_OFF = 0x140000000, 0x140001000, None
SITE, CAVE, BACK = 0x140406d25, 0x140407905, 0x140406d2f
SITE_OLD = bytes.fromhex('80bc243202000000761e')     # cmp byte [rsp+0x232],0 ; jbe skip
CHECK = bytes.fromhex('80bc243202000000')            # same compare, done in the spare bytes

# file offset of .text
pe = struct.unpack_from('<I', data, 0x3c)[0]
nsec, opt = struct.unpack_from('<H', data, pe + 6)[0], struct.unpack_from('<H', data, pe + 20)[0]
for i in range(nsec):
    o = pe + 24 + opt + 40 * i
    va, _, raw = struct.unpack_from('<III', data, o + 12)
    if data[o:o + 5] == b'.text': TEXT_OFF, TEXT_VA = raw, BASE + va
off = lambda va: TEXT_OFF + va - TEXT_VA
jmp = lambda src, dst: b'\xe9' + struct.pack('<i', dst - (src + 5))

cave = CHECK + b'\x75\x08' + bytes.fromhex('c684243202000001')      # jne +8 ; mov byte [rsp+0x232],1
cave += jmp(CAVE + len(cave), BACK)
site = jmp(SITE, CAVE) + b'\x90' * 5

if data[off(SITE):off(SITE) + 10] == site:
    print('already applied'); sys.exit(0)
if hashlib.sha256(data).hexdigest() != ORIG_SHA or data[off(SITE):off(SITE) + 10] != SITE_OLD \
        or data[off(CAVE):off(CAVE) + len(cave)] != b'\xcc' * len(cave):
    sys.exit('this is not the Prism build the tweak was made for (GG 120.0.0) - nothing changed')
shutil.copy2(path, path + '.skj-orig')
data[off(SITE):off(SITE) + 10] = site
data[off(CAVE):off(CAVE) + len(cave)] = cave
open(path, 'wb').write(data)
print('Prism audio visualizer tweak applied (undo: prism-av-mouse.sh --undo)')
EOF
