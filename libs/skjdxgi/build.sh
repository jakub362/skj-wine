#!/usr/bin/env bash
# Build SKJ's dxgi.dll (desktop duplication in front of DXVK's dxgi) into lib/prefix/dxgi. Needs mingw-w64 gcc.
set -euo pipefail
HERE="$(cd "$(dirname "$(readlink -f "$0")")" && pwd)"; ROOT="$(cd "$HERE/../.." && pwd)"
mkdir -p "$ROOT/lib/prefix/dxgi"
x86_64-w64-mingw32-gcc -O2 -Wall -shared -static-libgcc -o "$ROOT/lib/prefix/dxgi/dxgi.dll" "$HERE/dxgi.c" "$HERE/dxgi.def" \
  -ldxguid -luuid
x86_64-w64-mingw32-strip "$ROOT/lib/prefix/dxgi/dxgi.dll"
echo "built $ROOT/lib/prefix/dxgi/dxgi.dll"
