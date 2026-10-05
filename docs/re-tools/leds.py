#!/usr/bin/env python3
"""leds.py LOG [t0 t1]: LED frames (cmd 0x61, 3 zones) written to the Aerox, from a WINEDEBUG=+hid,+timestamp log.
Prints per-second: frame count, mean brightness, distinct colours."""
import sys, re, collections
rx = re.compile(rb'^\s*(\d+\.\d+):[0-9a-f]+:trace:hid:hid_internal_dispatch 00000000  61 03 00 (\w\w) (\w\w) (\w\w) 01 (\w\w) (\w\w) (\w\w) 02 (\w\w) (\w\w) (\w\w)')
sec = collections.OrderedDict()
for l in open(sys.argv[1], 'rb'):
    m = rx.match(l)
    if not m: continue
    t = float(m.group(1)); v = [int(x, 16) for x in m.groups()[1:]]
    sec.setdefault(int(t), []).append(v)
for s, fr in sec.items():
    b = sum(sum(f) for f in fr) / (len(fr) * 9)
    print('%d  frames=%-3d  mean=%5.1f  max=%3d  colours=%d  e.g. %s' % (s, len(fr), b, max(max(f) for f in fr), len({tuple(f) for f in fr}), ' '.join('%02x' % x for x in fr[len(fr)//2])))
