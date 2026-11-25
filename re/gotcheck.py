"""For each symbol: GOT slot (if any) and number of call sites via PLT vs direct BL/B."""
import sys, struct
from disasm import binary, sec, by_name, plt_map, got_to_name
name_to_got = {v: k for k, v in got_to_name.items()}
name_to_plt = {v: k for k, v in plt_map.items()}
text = sec['.text']; base, off, size = text[3], text[4], text[5]
code = binary[off:off + size]
calls = {}
for i in range(0, size, 4):
    w = struct.unpack_from('<I', code, i)[0]
    if (w >> 26) in (0x25, 0x05):
        imm = w & 0x3ffffff
        if imm & 0x2000000: imm -= 0x4000000
        calls.setdefault(base + i + imm * 4, []).append(base + i)
for n in sys.argv[1:]:
    if n not in by_name: print('MISSING', n); continue
    a = by_name[n]['address']; p = name_to_plt.get(n); g = name_to_got.get(n)
    print('%-75s addr=%x got=%s plt_calls=%d direct_calls=%d' % (n[:75], a, hex(g) if g else '-', len(calls.get(p, [])) if p else 0, len(calls.get(a, []))))
