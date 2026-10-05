#!/usr/bin/env python3
"""gosyms.py EXE > syms.txt : dump Go function table (addr size name) from a PE's pclntab (Go 1.20+)."""
import sys, struct
d = open(sys.argv[1], 'rb').read()
pe = struct.unpack_from('<I', d, 0x3c)[0]
nsec = struct.unpack_from('<H', d, pe + 6)[0]; opt = struct.unpack_from('<H', d, pe + 20)[0]
base = struct.unpack_from('<Q', d, pe + 24 + 24)[0]
secs = []
for i in range(nsec):
    o = pe + 24 + opt + 40 * i
    vs, va, rs, ro = struct.unpack_from('<IIII', d, o + 8)
    secs.append((d[o:o + 8].rstrip(b'\0').decode(), va, vs, ro, rs))
text_va = base + [s for s in secs if s[0] == '.text'][0][1]
pos = 0
while True:
    pos = d.find(b'\xf1\xff\xff\xff\x00\x00\x01\x08', pos)
    if pos < 0: sys.exit('no pclntab')
    nfunc, nfiles, textStart, fno, cuo, fto, pco, plo = struct.unpack_from('<8Q', d, pos + 8)
    if text_va <= textStart < text_va + 0x10000: break
    pos += 1
ft = pos + plo
out = []
for i in range(nfunc):
    eo, fo = struct.unpack_from('<II', d, ft + 8 * i)
    no = struct.unpack_from('<i', d, ft + fo + 4)[0]
    n = d[pos + fno + no: d.index(b'\0', pos + fno + no)].decode(errors='replace')
    out.append((textStart + eo, n))
end = textStart + struct.unpack_from('<I', d, ft + 8 * nfunc)[0]
for i, (a, n) in enumerate(out):
    nx = out[i + 1][0] if i + 1 < len(out) else end
    print('%x %d %s' % (a, nx - a, n))
