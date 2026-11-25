"""Find BL/B call sites to given symbols (direct or via PLT) in .text."""
import sys
from disasm import binary, sec, md, by_name, plt_map, name_at
import struct
text = sec['.text']
targets = {}
for arg in sys.argv[1:]:
    s = by_name[arg]
    targets[s['address']] = arg
    for a, n in plt_map.items():
        if n == arg: targets[a] = arg + '@plt'
base, off, size = text[3], text[4], text[5]
code = binary[off:off + size]
hits = []
for i in range(0, size, 4):
    w = struct.unpack_from('<I', code, i)[0]
    op = w >> 26
    if op in (0x25, 0x05):  # BL / B
        imm = w & 0x3ffffff
        if imm & 0x2000000: imm -= 0x4000000
        tgt = base + i + imm * 4
        if tgt in targets:
            hits.append((base + i, 'bl' if op == 0x25 else 'b', targets[tgt]))
for a, k, t in hits:
    print('%x %-3s -> %-60s in %s' % (a, k, t, name_at(a)))
