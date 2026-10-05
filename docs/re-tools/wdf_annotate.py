#!/usr/bin/env python3
"""Annotate an objdump disassembly of a KMDF driver with WDF function names.

KMDF drivers call framework functions through a table: `mov 0xNNN(%rax),%rax` where
NNN/8 is the WDFFUNCENUM index. Needs wdffuncenum.h from
https://github.com/microsoft/Windows-Driver-Frameworks (src/publicinc/wdf/kmdf/1.33/).

  x86_64-w64-mingw32-objdump -d --no-show-raw-insn driver.sys > driver.asm
  python3 wdf_annotate.py wdffuncenum.h driver.asm > driver.ann
"""
import re, sys
hdr, asm = sys.argv[1], sys.argv[2]
t = open(hdr).read()
body = re.sub(r'//.*', '', re.search(r'typedef enum _WDFFUNCENUM \{(.*?)\}\s*WDFFUNCENUM', t, re.S).group(1))
names = [x.strip().split('=')[0].strip().replace('TableIndex', '') for x in body.split(',') if x.strip()]
for line in open(asm):
    m = re.search(r'mov\s+0x([0-9a-f]+)\(%rax\),%rax', line)
    if m:
        o = int(m.group(1), 16)
        if o % 8 == 0 and 0x10 <= o and o // 8 < len(names):
            line = line.rstrip() + '   ; WDF ' + names[o // 8] + '\n'
    sys.stdout.write(line)
