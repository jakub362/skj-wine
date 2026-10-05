import pefile, capstone, sys
path, start, n = sys.argv[1], int(sys.argv[2],16), int(sys.argv[3],0)
pe = pefile.PE(path); base = pe.OPTIONAL_HEADER.ImageBase; data = pe.get_memory_mapped_image()
imports = {}
for e in pe.DIRECTORY_ENTRY_IMPORT:
    for i in e.imports: imports[i.address] = (e.dll.decode()+"!"+(i.name.decode() if i.name else str(i.ordinal)))
md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64); md.detail=True
rva = start-base
for k,insn in enumerate(md.disasm(data[rva:rva+n], start)):
    note=""
    for op in insn.operands:
        if op.type==capstone.x86.X86_OP_MEM and op.mem.base==capstone.x86.X86_REG_RIP:
            t=insn.address+insn.size+op.mem.disp
            if t in imports: note=" ; "+imports[t]
            else:
                off=t-base
                try:
                    s=data[off:off+80]
                    if s[1:2]==b'\x00' and 32<=s[0]<127: note=" ; L\""+s.decode('utf-16-le','ignore').split('\x00')[0]+'"'
                    elif 32<=s[0]<127 and s[:4].isascii(): note=" ; \""+s.split(b'\x00')[0].decode('ascii','ignore')+'"'
                except Exception: pass
    print(f"{insn.address:x}: {insn.mnemonic} {insn.op_str}{note}")
