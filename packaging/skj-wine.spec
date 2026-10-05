%global wine_ver 11.19
%{!?_udevrulesdir:%global _udevrulesdir /usr/lib/udev/rules.d}
%global debug_package %{nil}
%global __strip /bin/true
# the Windows binaries inside (DLLs, .sys, .exe) must not be touched by rpm's brp scripts
%global __os_install_post %{nil}

Name:           skj-wine
Version:        %{skj_version}
Release:        1%{?dist}
Summary:        Wine build for Windows hardware/vendor apps (SteelSeries GG, ...)
License:        LGPL-2.1-or-later
URL:            https://github.com/jakub362/skj-wine
Source0:        skj-wine-%{version}.tar.gz
ExclusiveArch:  x86_64

# WineHQ's wine-staging (install-fedora.sh / build-rpm.sh add the WineHQ repo)
Requires:       wine-staging >= 1:%{wine_ver}
Requires:       sqlite openssl curl cabextract icoutils libnotify usbutils
Requires(post): coreutils systemd-udev

%description
SKJ Wine is a Wine build that isn't for games: it carries the fixes, services
and driver stand-ins that Windows-only vendor software needs on Linux.
Includes SteelSeries GG support (skj-gg), Wine patches for crypt32, secur32 and
windows.storage.applicationdata, and skjsshid.sys, a replacement for
SteelSeries' KMDF sshid.sys.

SKJ Wine keeps a private copy of wine-staging %{wine_ver} in /opt/skj-wine/dist,
so your normal Wine is never changed.

%prep
%setup -q -n skj-wine

%build
# nothing to build: Windows binaries are prebuilt (see patches/ and drivers/)

%install
install -d %{buildroot}/opt/skj-wine
cp -a bin apps tools drivers lib patches share README.md %{buildroot}/opt/skj-wine/
install -d %{buildroot}%{_bindir}
ln -s /opt/skj-wine/bin/skj-gg %{buildroot}%{_bindir}/skj-gg
install -Dm644 share/applications/skj-steelseries-gg.desktop %{buildroot}%{_datadir}/applications/skj-steelseries-gg.desktop
install -d %{buildroot}%{_udevrulesdir}
echo 'SUBSYSTEM=="hidraw", ATTRS{idVendor}=="1038", TAG+="uaccess"' > %{buildroot}%{_udevrulesdir}/70-skj-wine-steelseries.rules

%post
# private wine-staging copy with SKJ Wine's DLLs on top
have=$(/opt/wine-staging/bin/wine --version 2>/dev/null)
if [ "$have" != "wine-%{wine_ver} (Staging)" ]; then
  echo "SKJ Wine: needs wine-staging %{wine_ver}, found '$have'." >&2
  echo "          sudo dnf install 'wine-staging-%{wine_ver}*' 'winehq-staging-%{wine_ver}*' then: sudo dnf reinstall skj-wine" >&2
elif [ ! -x /opt/skj-wine/dist/bin/wine ] || [ "$(/opt/skj-wine/dist/bin/wine --version 2>/dev/null)" != "$have" ]; then
  rm -rf /opt/skj-wine/dist
  cp -a --reflink=auto /opt/wine-staging /opt/skj-wine/dist
fi
if [ -d /opt/skj-wine/dist ]; then
  dlldir=$(dirname "$(find /opt/skj-wine/dist -path '*/wine/x86_64-windows/crypt32.dll' | head -1)")
  cp /opt/skj-wine/lib/wine/x86_64-windows/*.dll "$dlldir/"
  install -m755 /opt/skj-wine/lib/bin/wineserver /opt/skj-wine/dist/bin/wineserver
fi
udevadm control --reload-rules >/dev/null 2>&1 || :
udevadm trigger --subsystem-match=hidraw >/dev/null 2>&1 || :

%postun
if [ "$1" -eq 0 ]; then
  rm -rf /opt/skj-wine/dist
  udevadm control --reload-rules >/dev/null 2>&1 || :
fi

%files
/opt/skj-wine/bin
/opt/skj-wine/apps
/opt/skj-wine/tools
/opt/skj-wine/drivers
/opt/skj-wine/lib
/opt/skj-wine/patches
/opt/skj-wine/share
/opt/skj-wine/README.md
%dir /opt/skj-wine
%ghost /opt/skj-wine/dist
%{_bindir}/skj-gg
%{_datadir}/applications/skj-steelseries-gg.desktop
%{_udevrulesdir}/70-skj-wine-steelseries.rules

%changelog
* Sun Oct 04 2026 Jakub <jnowakowski741@gmail.com> - %{skj_version}-1
- SteelSeries GG: full stack, skjsshid.sys, DXVK, app menu + autostart launcher
