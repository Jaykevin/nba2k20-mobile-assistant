"""Disassemble functions in libnba2k20_clean_opt.so by symbol name or address, annotating call targets."""
import sys, json, struct, bisect, re
from reconfig import LIB, SYMS   # paths from local_config.json (also adds capstone_path to sys.path)
from capstone import Cs, CS_ARCH_ARM64, CS_MODE_LITTLE_ENDIAN
binary = open(LIB, 'rb').read()
syms = [s for s in json.load(open(SYMS, encoding='utf-8')) if s['name'] and s['address']]
by_name = {s['name']: s for s in syms}
func_syms = sorted((s for s in syms), key=lambda s: s['address'])
addrs = [s['address'] for s in func_syms]

# PLT map: parse .rela.plt + .plt to map PLT stub address -> symbol name
shoff = struct.unpack_from('<Q', binary, 40)[0]
shentsize, shnum, shstrndx = struct.unpack_from('<HHH', binary, 58)
secs = [struct.unpack_from('<IIQQQQIIQQ', binary, shoff + i * shentsize) for i in range(shnum)]
names = binary[secs[shstrndx][4]:secs[shstrndx][4] + secs[shstrndx][5]]
sec = {names[s[0]:names.index(b'\0', s[0])].decode(): s for s in secs}
allsyms = json.load(open(SYMS, encoding='utf-8'))
got_to_name = {}
rp = sec['.rela.plt']
for off in range(rp[4], rp[4] + rp[5], 24):
    dest, info, add = struct.unpack_from('<QQq', binary, off)
    got_to_name[dest] = allsyms[info >> 32]['name']
plt = sec['.plt']
md = Cs(CS_ARCH_ARM64, CS_MODE_LITTLE_ENDIAN)
plt_map = {}
# each PLT entry: adrp x16, page; ldr x17,[x16,#off]; add x16,x16,#off; br x17  (16 bytes) after 32-byte header
for a in range(plt[3] + 32, plt[3] + plt[5], 16):
    ins = list(md.disasm(binary[plt[4] + a - plt[3]: plt[4] + a - plt[3] + 16], a))
    if len(ins) >= 2 and ins[0].mnemonic == 'adrp':
        page = int(ins[0].op_str.split('#')[1], 0)
        m = re.search(r'#(0x[0-9a-f]+|\d+)\]', ins[1].op_str)
        off = int(m.group(1), 0) if m else 0
        plt_map[a] = got_to_name.get(page + off, '?')

def file_off(addr):
    for s in secs:
        if s[1] != 8 and s[3] <= addr < s[3] + s[5] and s[3] != 0:
            return s[4] + addr - s[3]
    raise ValueError(hex(addr))

def name_at(addr):
    if addr in plt_map: return plt_map[addr] + '@plt'
    i = bisect.bisect_right(addrs, addr) - 1
    if i >= 0:
        s = func_syms[i]
        if addr < s['address'] + max(s['size'], 1):
            return s['name'] + ('' if addr == s['address'] else '+0x%x' % (addr - s['address']))
    return None

def dis(addr, size):
    code = binary[file_off(addr):file_off(addr) + size]
    out = []
    for ins in md.disasm(code, addr):
        note = ''
        if ins.mnemonic in ('bl', 'b') or ins.mnemonic.startswith('b.') or ins.mnemonic in ('cbz','cbnz','tbz','tbnz','adr','adrp'):
            try:
                tgt = int(ins.op_str.split('#')[-1], 0)
                n = name_at(tgt)
                if n and ins.mnemonic in ('bl', 'b', 'adrp', 'adr'): note = '  ; ' + n
            except Exception: pass
        out.append('%x: %-8s %s%s' % (ins.address, ins.mnemonic, ins.op_str, note))
    return out

if __name__ == '__main__':
    for arg in sys.argv[1:]:
        if arg.startswith('0x'):
            a, _, n = arg.partition(':'); a = int(a, 16); n = int(n or '256', 0)
            print('== %s (%s)' % (hex(a), name_at(a)))
            print('\n'.join(dis(a, n)))
        else:
            s = by_name[arg]
            print('== %s @%x size=%d' % (arg, s['address'], s['size']))
            print('\n'.join(dis(s['address'], s['size'])))
        print()
