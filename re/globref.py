"""Find code that references a given global address via adrp+ldr/str/add."""
import sys, struct
from disasm import binary, sec, md, name_at
targets = [int(a, 16) for a in sys.argv[1:]]
text = sec['.text']; base, off, size = text[3], text[4], text[5]
code = binary[off:off + size]
pages = {t & ~0xfff: t for t in targets}
for i in range(0, size - 8, 4):
    w = struct.unpack_from('<I', code, i)[0]
    if (w & 0x9f000000) != 0x90000000: continue  # adrp
    rd = w & 0x1f
    immlo = (w >> 29) & 3; immhi = (w >> 5) & 0x7ffff
    imm = ((immhi << 2) | immlo) << 12
    if imm & (1 << 32): imm -= 1 << 33
    page = ((base + i) & ~0xfff) + imm
    if page not in pages: continue
    # look ahead a few instructions for use of rd with matching low offset
    for j in range(1, 12):
        if i + 4*j >= size: break
        ins = next(md.disasm(code[i+4*j:i+4*j+4], base+i+4*j), None)
        if ins is None: continue
        reg = 'x%d' % rd
        if '[' + reg + ', #' in ins.op_str or ('add' == ins.mnemonic and ins.op_str.split(', ')[1] == reg):
            for t in targets:
                lo = t & 0xfff
                if ('#0x%x' % lo) in ins.op_str:
                    print('%x %-6s %-30s %s' % (ins.address, ins.mnemonic, ins.op_str, name_at(ins.address)))
