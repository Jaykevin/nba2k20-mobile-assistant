"""Emulate the runtime GOT-slot lookup (PT_DYNAMIC -> relocations -> symbol names) on the library file."""
import struct, sys, json
from reconfig import LIB   # path from local_config.json
b = open(LIB, 'rb').read()
phoff = struct.unpack_from('<Q', b, 32)[0]; phentsize, phnum = struct.unpack_from('<HH', b, 54)
phdrs = [struct.unpack_from('<IIQQQQQQ', b, phoff + i * phentsize) for i in range(phnum)]
def v2o(v):
    for p in phdrs:
        if p[0] == 1 and p[3] <= v < p[3] + p[5]: return p[2] + v - p[3]
    raise ValueError(hex(v))
dyn = next(p for p in phdrs if p[0] == 2)
tags = {}
off = dyn[2]
while True:
    t, v = struct.unpack_from('<qQ', b, off); off += 16
    if t == 0: break
    tags.setdefault(t, v)
names = {2:'PLTRELSZ',5:'STRTAB',6:'SYMTAB',7:'RELA',8:'RELASZ',9:'RELAENT',10:'STRSZ',20:'PLTREL',23:'JMPREL',0x6000000f:'ANDROID_REL',0x60000011:'ANDROID_RELA',0x60000012:'ANDROID_RELASZ',36:'RELR',35:'RELRSZ',0x6ffffef5:'GNU_HASH',4:'HASH',30:'FLAGS',0x6ffffffb:'FLAGS_1'}
print({names.get(k, hex(k)): hex(v) for k, v in tags.items() if k in names})
symtab, strtab = tags[6], tags[5]
def symname(i):
    st_name = struct.unpack_from('<I', b, v2o(symtab + i * 24))[0]
    s = v2o(strtab + st_name); e = b.index(b'\0', s); return b[s:e].decode()
def symval(i):
    return struct.unpack_from('<Q', b, v2o(symtab + i * 24) + 8)[0]
slots = {}
for start, size in ((tags.get(23), tags.get(2)), (tags.get(7), tags.get(8))):
    if not start: continue
    o = v2o(start)
    for k in range(size // 24):
        r_off, info, add = struct.unpack_from('<QQq', b, o + k * 24)
        typ, si = info & 0xffffffff, info >> 32
        if si == 0: continue
        slots.setdefault(symname(si), []).append((r_off, typ, add, symval(si)))
want = sys.argv[1:]
for w in want:
    print(w[:70], [(hex(a), t, ad, hex(sv)) for a, t, ad, sv in slots.get(w, [])])
print('total symbolic relocs:', sum(len(v) for v in slots.values()))
