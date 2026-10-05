#!/usr/bin/env python3
"""godis.py EXE SYMS NAME_SUBSTR|0xADDR : objdump one Go function, annotating call targets and string refs."""
import sys, subprocess, re, struct, bisect
exe, symf, want = sys.argv[1:4]
syms = [(int(a, 16), int(s), n) for a, s, n in (l.rstrip('\n').split(' ', 2) for l in open(symf))]
addrs = [s[0] for s in syms]
d = open(exe, 'rb').read()
pe = struct.unpack_from('<I', d, 0x3c)[0]; nsec = struct.unpack_from('<H', d, pe + 6)[0]; opt = struct.unpack_from('<H', d, pe + 20)[0]
base = struct.unpack_from('<Q', d, pe + 24 + 24)[0]
secs = [struct.unpack_from('<IIII', d, pe + 24 + opt + 40 * i + 8) for i in range(nsec)]
def rd(va, n):
    for vs, a, rs, ro in secs:
        if base + a <= va < base + a + rs: return d[ro + va - base - a: ro + va - base - a + n]
    return b''
def name(a):
    i = bisect.bisect_right(addrs, a) - 1
    if i >= 0 and a < syms[i][0] + syms[i][1]:
        return syms[i][2] + ('+0x%x' % (a - syms[i][0]) if a != syms[i][0] else '')
if want.startswith('0x'):
    t = int(want, 16); i = bisect.bisect_right(addrs, t) - 1; fs = [syms[i]]
else:
    fs = [s for s in syms if want in s[2]]
    ex = [s for s in fs if s[2].endswith(want)]
    fs = ex or fs
for a, sz, n in fs[:int(sys.argv[4]) if len(sys.argv) > 4 else 1]:
    print('==== %x %s (%d bytes)' % (a, n, sz))
    out = subprocess.run(['objdump', '-d', '-M', 'intel', '--no-show-raw-insn', '--start-address=0x%x' % a, '--stop-address=0x%x' % (a + sz), exe], capture_output=True, text=True).stdout
    lines = [l for l in out.split('\n') if re.match(r'^\s*[0-9a-f]+:\t', l)]
    for k, l in enumerate(lines):
        l = re.sub(r'\s+', ' ', l.strip())
        m = re.search(r'(call|jmp) 0x([0-9a-f]+)', l)
        if m:
            nm = name(int(m.group(2), 16))
            if nm and not nm.startswith(n): l += '   ; ' + nm
        m = re.search(r'lea (\w+),\[rip\+0x[0-9a-f]+\] # 0x([0-9a-f]+)', l)
        if m:
            t = int(m.group(2), 16)
            ln = None
            for nx in lines[k + 1:k + 4]:
                mm = re.search(r'mov e?\w+,0x([0-9a-f]+)\s*$', nx.strip())
                if mm: ln = int(mm.group(1), 16); break
            nm = name(t)
            if nm: l += '   ; &' + nm
            elif ln and 0 < ln < 200:
                s = rd(t, ln)
                if s and all(32 <= c < 127 or c in (9, 10) for c in s): l += '   ; "%s"' % s.decode()
        print(l)
