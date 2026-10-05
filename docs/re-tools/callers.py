#!/usr/bin/env python3
"""callers.py ASM SYMS ADDRHEX... : functions containing a direct call/jmp/lea reference to the address"""
import sys, re, bisect
syms = [(int(a, 16), int(s), n) for a, s, n in (l.rstrip('\n').split(' ', 2) for l in open(sys.argv[2]))]
addrs = [s[0] for s in syms]
pats = [a.lower().replace('0x', '') for a in sys.argv[3:]]
rx = re.compile(r'^\s*([0-9a-f]+):\t.*(?:0x|# 0x)(' + '|'.join(pats) + r')\b')
for l in open(sys.argv[1]):
    m = rx.match(l)
    if m:
        a = int(m.group(1), 16); i = bisect.bisect_right(addrs, a) - 1
        print('%x in %s  [%s]' % (a, syms[i][2], l.split('\t', 1)[1].strip()[:70]))
