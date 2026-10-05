#!/usr/bin/env python3
"""gostr.py EXE h:ADDR (string header) | s:ADDR:LEN (raw) ..."""
import sys, struct
d = open(sys.argv[1], 'rb').read()
pe = struct.unpack_from('<I', d, 0x3c)[0]; nsec = struct.unpack_from('<H', d, pe + 6)[0]; opt = struct.unpack_from('<H', d, pe + 20)[0]
base = struct.unpack_from('<Q', d, pe + 24 + 24)[0]
secs = [struct.unpack_from('<IIII', d, pe + 24 + opt + 40 * i + 8) for i in range(nsec)]
def rd(va, n):
    for vs, a, rs, ro in secs:
        if base + a <= va < base + a + rs: return d[ro + va - base - a: ro + va - base - a + n]
    return b''
for a in sys.argv[2:]:
    p = a.split(':')
    if p[0] == 'h':
        ptr, ln = struct.unpack('<QQ', rd(int(p[1], 16), 16)); print(a, repr(rd(ptr, min(ln, 400)).decode(errors='replace')))
    else:
        print(a, repr(rd(int(p[1], 16), int(p[2], 16)).decode(errors='replace')))
