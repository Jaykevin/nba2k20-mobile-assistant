"""Find functions that load GOT slot <got> and then access field <off> of the pointed object."""
import sys, struct
from disasm import binary, sec, md, name_at
got = int(sys.argv[1], 16); field = int(sys.argv[2], 16)
text = sec['.text']; base, off, size = text[3], text[4], text[5]
code = binary[off:off + size]
page = got & ~0xfff; lo = got & 0xfff
seen = {}
for i in range(0, size - 8, 4):
    w = struct.unpack_from('<I', code, i)[0]
    if (w & 0x9f000000) != 0x90000000: continue
    immlo = (w >> 29) & 3; immhi = (w >> 5) & 0x7ffff
    imm = ((immhi << 2) | immlo) << 12
    if imm & (1 << 32): imm -= 1 << 33
    if ((base + i) & ~0xfff) + imm != page: continue
    rd = w & 0x1f
    window = list(md.disasm(code[i:i + 4 * 14], base + i))
    loaded = None
    for ins in window[1:]:
        if loaded is None and ins.mnemonic == 'ldr' and ('[x%d, #0x%x]' % (rd, lo)) in ins.op_str:
            loaded = ins.op_str.split(',')[0].strip()
            continue
        if loaded and ('[%s, #0x%x]' % (loaded, field)) in ins.op_str:
            n = name_at(ins.address) or '?'
            key = n.split('+')[0]
            seen.setdefault(key, []).append('%x %s %s' % (ins.address, ins.mnemonic, ins.op_str))
            break
for k, v in sorted(seen.items()):
    print(k, '|', '; '.join(v[:3]))
