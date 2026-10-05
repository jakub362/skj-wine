#!/usr/bin/env python3
"""Print one function from an (annotated) objdump listing, from its address to its ret.
  python3 fn.py driver.ann 1400014b8 [max_lines]"""
import re, sys
lines = open(sys.argv[1]).read().splitlines()
start, maxn = sys.argv[2], int(sys.argv[3]) if len(sys.argv) > 3 else 200
out, on = [], False
for l in lines:
    s = l.strip()
    if s.startswith(start + ':'): on = True
    if on:
        out.append(re.sub(r'^\s+', '', l))
        if s.endswith('ret') and len(out) > 3: break
print('\n'.join(out[:maxn]))
