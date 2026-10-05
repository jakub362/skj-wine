import pefile, capstone, sys, struct
path, needle = sys.argv[1], sys.argv[2]
pe = pefile.PE(path); base = pe.OPTIONAL_HEADER.ImageBase
data = pe.get_memory_mapped_image()
# find string (ascii and utf16)
locs = []
for enc in ('ascii','utf-16-le'):
    s = needle.encode(enc); i = data.find(s)
    while i != -1: locs.append((i, enc)); i = data.find(s, i+1)
print("string RVAs:", [(hex(r),e) for r,e in locs])
md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64); md.detail = True; md.skipdata = True
for sec in pe.sections:
    if not (sec.Characteristics & 0x20000000): continue
    code = data[sec.VirtualAddress: sec.VirtualAddress+sec.Misc_VirtualSize]
    for insn in md.disasm(code, base+sec.VirtualAddress):
        if insn.mnemonic in ('lea','mov') and 'rip' in insn.op_str:
            for op in insn.operands:
                if op.type == capstone.x86.X86_OP_MEM and op.mem.base == capstone.x86.X86_REG_RIP:
                    tgt = insn.address + insn.size + op.mem.disp - base
                    for r,e in locs:
                        if tgt == r: print("xref", hex(insn.address), insn.mnemonic, insn.op_str, e)
