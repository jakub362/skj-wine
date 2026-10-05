import pefile, capstone, sys
path=sys.argv[1]; want=sys.argv[2:] or ['DeviceIoControl','CreateFileW']
pe=pefile.PE(path); base=pe.OPTIONAL_HEADER.ImageBase; data=pe.get_memory_mapped_image()
iat={}
for e in pe.DIRECTORY_ENTRY_IMPORT:
    for i in e.imports:
        if i.name: iat[i.address]=i.name.decode()
md=capstone.Cs(capstone.CS_ARCH_X86,capstone.CS_MODE_64); md.detail=True; md.skipdata=True
for sec in pe.sections:
    if not sec.Characteristics & 0x20000000: continue
    insns=list(md.disasm(data[sec.VirtualAddress:sec.VirtualAddress+sec.Misc_VirtualSize], base+sec.VirtualAddress))
    for k,ins in enumerate(insns):
        if ins.mnemonic=='call' and 'rip' in ins.op_str:
            op=ins.operands[0]; t=ins.address+ins.size+op.mem.disp
            name=iat.get(t)
            if name in want:
                ctx=insns[max(0,k-14):k]
                imms=[f"{c.mnemonic} {c.op_str}" for c in ctx if c.mnemonic=='mov' and ('edx' in c.op_str.split(',')[0]) ]
                print(hex(ins.address), name, imms[-2:] if imms else '')
