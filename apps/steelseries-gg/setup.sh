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
[ -x "$ROOT/dist/bin/wine" ] && export PATH="$ROOT/dist/bin:$PATH"
export WINEPREFIX="${1:-$HOME/.local/share/skj-wine/steelseries-gg}"
export WINEDEBUG="${WINEDEBUG:--all}"
CACHE="${XDG_CACHE_HOME:-$HOME/.cache}/skj-wine"
GG_URL="https://steelseries.com/gg/downloads/gg/latest/windows"
MONO_VER="11.3.0"
mkdir -p "$CACHE"

say() { printf '\033[1;36m[skj-wine]\033[0m %s\n' "$*"; }

say "1/9 Creating prefix at $WINEPREFIX"
mkdir -p "$(dirname "$WINEPREFIX")"   # Wine only creates the last path component
WINEDLLOVERRIDES="mscoree,mshtml=" wineboot -i >"$CACHE/wineboot.log" 2>&1 || true
wineserver -w
[ -d "$WINEPREFIX/drive_c/windows" ] || { echo "Creating the Wine prefix failed, see $CACHE/wineboot.log"; tail -20 "$CACHE/wineboot.log"; exit 1; }

say "2/9 Installing wine-mono $MONO_VER (needed by GG's .NET Framework helpers)"
MONO="$CACHE/wine-mono-$MONO_VER-x86.msi"
[ -f "$MONO" ] || curl -fL -o "$MONO" "https://dl.winehq.org/wine/wine-mono/$MONO_VER/wine-mono-$MONO_VER-x86.msi"
wine msiexec /i "$(winepath -w "$MONO")" /qn >/dev/null 2>&1 || true

say "3/9 Downloading and installing SteelSeries GG (silent)"
GG="$CACHE/SteelSeriesGGSetup.exe"
[ -f "$GG" ] || curl -fL -o "$GG" "$GG_URL"
WINEDLLOVERRIDES="winemenubuilder.exe=d" wine "$GG" /S >/dev/null 2>&1 || true
wineserver -w

GGDIR="$WINEPREFIX/drive_c/Program Files/SteelSeries/GG"
PD="$WINEPREFIX/drive_c/ProgramData/SteelSeries/GG"
[ -f "$GGDIR/SteelSeriesGGEZ.exe" ] || { echo "GG install failed"; exit 1; }

say "4/9 Placing dbconf.yml where GG and the Engine look for it"
mkdir -p "$GGDIR/db" "$GGDIR/apps/engine/db"
cp "$PD/db/dbconf.yml" "$GGDIR/db/"
cp "$PD/db/dbconf.yml" "$GGDIR/apps/engine/db/"

say "5/9 Generating the localhost TLS certificate (replaces GenerateSelfSignedCertificate.ps1)"
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
TP=$(wine "$ROOT/tools/certinstall/certinstall.exe" "$(winepath -w "$CERT/gg.pfx")" "" --machine 2>/dev/null | tr -d '\r')
wine reg add 'HKLM\Software\SteelSeries' /v GGCertificateThumbprint /t REG_SZ /d "$TP" /f >/dev/null 2>&1
say "   certificate thumbprint: $TP"

say "6/9 Registering the Engine, Sonar and 3DAT sub-apps with GGEZ (the installer's table migration fails under Wine)"
command -v sqlite3 >/dev/null || { echo "sqlite3 is needed (sudo dnf install sqlite)"; exit 1; }
sqlite3 "$PD/db/ggez.db" "ATTACH '$PD/db/database.db' AS old;
  INSERT OR IGNORE INTO sub_apps (name,is_enabled,created_at,updated_at,is_windows_supported,executable_name,toggle_via_settings,auto_start,is_browserview_supported)
  SELECT name,is_enabled,created_at,updated_at,is_windows_supported,executable_name,toggle_via_settings,auto_start,is_browserview_supported FROM old.sub_apps;"

say "7/9 Writing shared/guid.json"
mkdir -p "$PD/shared"
[ -f "$PD/shared/guid.json" ] || printf '{"guid":"%s"}' "$(cat /proc/sys/kernel/random/uuid)" > "$PD/shared/guid.json"

say "8/9 Installing skjsshid.sys (SKJ Wine replacement for SteelSeries' sshid.sys KMDF driver)"
cp "$ROOT/drivers/skjsshid/skjsshid.sys" "$WINEPREFIX/drive_c/windows/system32/drivers/skjsshid.sys"
wine sc create skjsshid type= kernel start= auto binPath= 'C:\windows\system32\drivers\skjsshid.sys' DisplayName= "SKJ Wine SteelSeries engine device" >/dev/null 2>&1 || true
# GG's own KMDF drivers can't load in Wine (no WDFLDR.SYS); stop them from auto-starting
for svc in ssdevfactory sshid msihid ssbthid ssps2 sssmbus; do wine sc config "$svc" start= disabled >/dev/null 2>&1 || true; done

say "9/9 Removing the update-service proxy (needs real .NET Framework, not used by SKJ Wine)"
wine sc delete SteelSeriesGGUpdateServiceProxy >/dev/null 2>&1 || true
wineserver -w

# Wine's default "Enhance pointer precision" (MouseSpeed=1) makes GG show
# "Your mouse settings are being affected by Windows Pointer Options".
# Linux handles pointer acceleration itself, so turn it off inside the prefix.
wine reg add 'HKCU\Control Panel\Mouse' /v MouseSpeed /t REG_SZ /d 0 /f >/dev/null 2>&1 || true
wine reg add 'HKCU\Control Panel\Mouse' /v MouseThreshold1 /t REG_SZ /d 0 /f >/dev/null 2>&1 || true
wine reg add 'HKCU\Control Panel\Mouse' /v MouseThreshold2 /t REG_SZ /d 0 /f >/dev/null 2>&1 || true
wineserver -w

say "Installing DXVK (GPU-accelerated D3D11 for the GG window)"
"$ROOT/tools/install-dxvk.sh" "$WINEPREFIX" || say "DXVK install failed - GG still works, just with a slower window"
"$ROOT/tools/install-nvidia-libs.sh" "$WINEPREFIX" || say "NVIDIA encoder wrappers not installed - only Moments needs them"

say "Done. Start GG with: $HERE/run.sh"
