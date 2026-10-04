#!/usr/bin/env bash
# Build (and optionally install) the SKJ Wine RPM on Fedora.
#   ./build-rpm.sh            build ~/rpmbuild/RPMS/x86_64/skj-wine-*.rpm
#   ./build-rpm.sh --install  build and install it (switches from install-local.sh)
set -euo pipefail
cd "$(dirname "$0")"; ROOT="$PWD"
VERSION="0.1.$(git -C "$ROOT" rev-list --count HEAD 2>/dev/null || echo 0)"
say() { printf '\033[1;35m[SKJ Wine]\033[0m %s\n' "$*"; }

command -v rpmbuild >/dev/null || sudo dnf install -y rpm-build
mkdir -p ~/rpmbuild/{SOURCES,SPECS,BUILD,RPMS,SRPMS}
say "Packing source (version $VERSION)"
tar czf ~/rpmbuild/SOURCES/skj-wine-$VERSION.tar.gz -C "$(dirname "$ROOT")" \
  --exclude="$(basename "$ROOT")/dist" --exclude="$(basename "$ROOT")/.git" \
  --transform "s|^$(basename "$ROOT")|skj-wine|" "$(basename "$ROOT")"
cp packaging/skj-wine.spec ~/rpmbuild/SPECS/
rpmbuild -bb --define "skj_version $VERSION" ~/rpmbuild/SPECS/skj-wine.spec >/tmp/skj-rpmbuild.log 2>&1 \
  || { tail -30 /tmp/skj-rpmbuild.log; exit 1; }
RPM=$(ls -t ~/rpmbuild/RPMS/x86_64/skj-wine-$VERSION-*.rpm | head -1)
say "Built $RPM"

if [ "${1:-}" = --install ]; then
  if [ ! -f /etc/yum.repos.d/winehq.repo ]; then
    REPO="https://dl.winehq.org/wine-builds/fedora/$(rpm -E %fedora)/winehq.repo"
    sudo dnf config-manager addrepo --from-repofile="$REPO" 2>/dev/null || sudo dnf config-manager --add-repo "$REPO"
  fi
  # switching from the folder install: drop its menu entry/command so the RPM's are used
  [ -L "$HOME/.local/bin/skj-gg" ] && "$ROOT/install-local.sh" --remove >/dev/null 2>&1 || true
  sudo rm -f /etc/udev/rules.d/70-skj-wine-steelseries.rules   # RPM ships it in /usr/lib/udev/rules.d
  "$ROOT/bin/skj-gg" --stop 2>/dev/null || true
  sudo dnf install -y "$RPM"
  /usr/bin/skj-gg --autostart on >/dev/null
  say "Installed. SteelSeries GG is in your app menu; 'skj-gg' in a terminal. Your GG settings were kept."
fi
