#!/usr/bin/env bash
# SKJ Wine - SteelSeries GG setup
# Builds a Wine prefix with GG installed and the Windows pieces GG's installer
# could not set up under Wine (PowerShell cert, db config, services).
#
# Needs: SKJ Wine (wine-staging + patches/wine/*) on PATH, openssl, curl.
# Usage: ./setup.sh [prefix-dir]   (default: ~/.local/share/skj-wine/steelseries-gg)
set -euo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"
export WINEPREFIX="${1:-$HOME/.local/share/skj-wine/steelseries-gg}"
export WINEDEBUG="${WINEDEBUG:--all}"
CACHE="${XDG_CACHE_HOME:-$HOME/.cache}/skj-wine"
GG_URL="https://steelseries.com/gg/downloads/gg/latest/windows"
MONO_VER="11.3.0"
mkdir -p "$CACHE"

say() { printf '\033[1;36m[skj-wine]\033[0m %s\n' "$*"; }

say "1/6 Creating prefix at $WINEPREFIX"
WINEDLLOVERRIDES="mscoree,mshtml=" wineboot -i >/dev/null 2>&1 || true
wineserver -w

say "2/6 Installing wine-mono $MONO_VER (needed by GG's .NET Framework helpers)"
MONO="$CACHE/wine-mono-$MONO_VER-x86.msi"
[ -f "$MONO" ] || curl -fL -o "$MONO" "https://dl.winehq.org/wine/wine-mono/$MONO_VER/wine-mono-$MONO_VER-x86.msi"
wine msiexec /i "$(winepath -w "$MONO")" /qn >/dev/null 2>&1 || true

say "3/6 Downloading and installing SteelSeries GG (silent)"
GG="$CACHE/SteelSeriesGGSetup.exe"
[ -f "$GG" ] || curl -fL -o "$GG" "$GG_URL"
wine "$GG" /S >/dev/null 2>&1 || true
wineserver -w

GGDIR="$WINEPREFIX/drive_c/Program Files/SteelSeries/GG"
PD="$WINEPREFIX/drive_c/ProgramData/SteelSeries/GG"
[ -f "$GGDIR/SteelSeriesGGEZ.exe" ] || { echo "GG install failed"; exit 1; }

say "4/6 Placing dbconf.yml where GG and the Engine look for it"
mkdir -p "$GGDIR/db" "$GGDIR/apps/engine/db"
cp "$PD/db/dbconf.yml" "$GGDIR/db/"
cp "$PD/db/dbconf.yml" "$GGDIR/apps/engine/db/"

say "5/6 Generating the localhost TLS certificate (replaces GenerateSelfSignedCertificate.ps1)"
CERT="$WINEPREFIX/skj-cert"; mkdir -p "$CERT"
cat > "$CERT/ext.cnf" <<'EOF'
[req]
distinguished_name=dn
prompt=no
x509_extensions=v3
[dn]
CN=SteelSeries A/S
[v3]
keyUsage=critical,digitalSignature,keyCertSign
extendedKeyUsage=serverAuth,clientAuth
subjectAltName=IP:127.0.0.1,DNS:localhost
subjectKeyIdentifier=hash
EOF
openssl req -x509 -newkey rsa:2048 -sha512 -nodes -days 3650 \
  -keyout "$CERT/key.pem" -out "$CERT/cert.pem" -config "$CERT/ext.cnf" 2>/dev/null
openssl pkcs12 -export -inkey "$CERT/key.pem" -in "$CERT/cert.pem" -out "$CERT/gg.pfx" \
  -passout pass: -certpbe PBE-SHA1-3DES -keypbe PBE-SHA1-3DES -macalg sha1
TP=$(wine "$ROOT/tools/certinstall/certinstall.exe" "$(winepath -w "$CERT/gg.pfx")" "" --machine --user-keys 2>/dev/null | tr -d '\r')
wine reg add 'HKLM\Software\SteelSeries' /v GGCertificateThumbprint /t REG_SZ /d "$TP" /f >/dev/null 2>&1
say "   certificate thumbprint: $TP"

say "6/6 Done. Start GG with: $HERE/run.sh"
