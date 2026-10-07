#!/usr/bin/env bash
# Build mfaacenc.dll (AAC encoder MFT) into lib/prefix/system32. Needs git and mingw-w64 (gcc + g++).
#   libs/mfaacenc/build.sh [work-folder]
# The codec is fdk-aac "stripped" (AAC-LC only, the build Fedora ships); see FDK-AAC-NOTICE.
set -euo pipefail
HERE="$(cd "$(dirname "$(readlink -f "$0")")" && pwd)"; ROOT="$(cd "$HERE/../.." && pwd)"
WORK="${1:-${XDG_CACHE_HOME:-$HOME/.cache}/skj-wine/work}"
FDK="$WORK/fdk-aac"; OBJ="$WORK/fdkbuild"
FDK_URL=https://gitlab.freedesktop.org/wtaymans/fdk-aac-stripped.git
FDK_COMMIT=529b87452cd33d45e1d0a5066d20b64f10b38845
CC=x86_64-w64-mingw32-gcc; CXX=x86_64-w64-mingw32-g++

[ -d "$FDK/.git" ] || git clone -q "$FDK_URL" "$FDK"
git -C "$FDK" checkout -q "$FDK_COMMIT"
mkdir -p "$OBJ"; rm -f "$OBJ"/*.o "$OBJ/libfdkaacenc.a"
inc=(); for d in libAACenc libFDK libSYS libMpegTPEnc libPCMutils; do inc+=("-I$FDK/$d/include"); done
for d in libAACenc libFDK libSYS libMpegTPEnc libPCMutils; do
  for f in "$FDK/$d"/src/*.cpp; do
    "$CXX" -O2 -fno-exceptions -fno-rtti -w "${inc[@]}" -c "$f" -o "$OBJ/${d}_$(basename "${f%.cpp}").o"
  done
done
x86_64-w64-mingw32-ar rcs "$OBJ/libfdkaacenc.a" "$OBJ"/*.o
"$CC" -O2 -Wall -Wno-unused-function "${inc[@]}" -c "$HERE/mfaacenc.c" -o "$OBJ/mfaacenc.obj"
mkdir -p "$ROOT/lib/prefix/system32"
"$CC" -shared -static-libgcc -o "$ROOT/lib/prefix/system32/mfaacenc.dll" "$OBJ/mfaacenc.obj" "$HERE/mfaacenc.def" \
  "$OBJ/libfdkaacenc.a" -lmfplat -lmfuuid -lole32 -ladvapi32 -luser32
x86_64-w64-mingw32-strip "$ROOT/lib/prefix/system32/mfaacenc.dll"
echo "built $ROOT/lib/prefix/system32/mfaacenc.dll"
