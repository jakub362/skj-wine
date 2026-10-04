#!/usr/bin/env bash
# SKJ Wine - check whether SteelSeries GG can see your SteelSeries devices.
# Run while GG is running (apps/steelseries-gg/run.sh). Writes a report you can send back.
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
export WINEPREFIX="${1:-$HOME/.local/share/skj-wine/steelseries-gg}"
OUT="$ROOT/check-mouse-report.txt"
LOGS="$WINEPREFIX/drive_c/ProgramData/SteelSeries/GG/Logs"
{
  echo "=== SKJ Wine check $(date -Is)"
  echo "--- wine: $("$ROOT/dist/bin/wine" --version 2>&1)"
  echo "--- SteelSeries USB devices (lsusb)"
  lsusb | grep -i -E "1038|steelseries" || echo "none found"
  echo "--- hidraw nodes for vendor 1038 and permissions"
  for h in /sys/class/hidraw/hidraw*; do
    dev=$(readlink -f "$h/device")
    if grep -qi "1038" "$dev/uevent" 2>/dev/null; then
      n=$(basename "$h")
      echo "$n  $(grep HID_NAME "$dev/uevent")  $(ls -l /dev/$n | awk '{print $1,$3,$4}')  readable-by-me=$([ -r /dev/$n ] && echo yes || echo NO)"
      getfacl -p /dev/$n 2>/dev/null | grep "^user:$USER" || true
    fi
  done
  echo "--- running GG processes"
  pgrep -af "SteelSeries" | grep -v -- "--type=" | grep -o -E "[A-Za-z0-9]+\.exe" | sort | uniq -c
  echo "--- Engine log lines about devices (last 60)"
  grep -h -i -E "ENGINE|PRISM" "$LOGS"/gg-errorlog.txt 2>/dev/null | grep -i -E "device|hid|aerox|1038|usb|sshid|driver|attach|connect" | tail -60
} > "$OUT" 2>&1
cat "$OUT"
echo
echo "Report saved to: $OUT"
