"""Offline check: every hook symbol resolves and has JUMP_SLOT/GLOB_DAT/ABS64 slots holding its address;
every binding symbol resolves. Mirrors elfhook.c/game.c logic on the library file."""
import re, struct, sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).parent))
import dynscan as d   # parses the same LIB at import

src = Path(__file__).resolve().parent.parent / 'src' / 'native'
main = (src / 'main.c').read_text(encoding='utf-8')
game = (src / 'game.c').read_text(encoding='utf-8')
hooks = re.findall(r'H\((\w+), "([^"]+)", (\w+), (\w+)\)', main)
binds = re.findall(r'BIND(?:_OPT)?\(\w+, "([^"]+)"\)', game)
# symbol table lookup by name (dynsym via DT_SYMTAB + DT_HASH like the C code)
hash_off = d.v2o(d.tags[4]); nbucket, nchain = struct.unpack_from('<II', d.b, hash_off)
def elf_hash(name):
    h = 0
    for ch in name.encode():
        h = ((h << 4) + ch) & 0xffffffff
        g = h & 0xf0000000
        if g: h ^= g >> 24
        h &= ~g & 0xffffffff
    return h
def find(name):
    i = struct.unpack_from('<I', d.b, hash_off + 8 + 4 * (elf_hash(name) % nbucket))[0]
    while i and i < nchain:
        if d.symname(i) == name: return i
        i = struct.unpack_from('<I', d.b, hash_off + 8 + 4 * nbucket + 4 * i)[0]
    return -1
bad = 0
for group, sym, hook, orig in hooks:
    i = find(sym)
    slots = d.slots.get(sym, [])
    ok = i > 0 and d.symval(i) and slots and all(t in (1026, 1025, 257) for _, t, _, _ in slots)
    if not ok:
        bad += 1; print('BAD HOOK', sym, i, slots)
print(f'hooks: {len(hooks)} checked, {bad} bad; total slots = {sum(len(d.slots.get(s, [])) for _, s, _, _ in hooks)}')
missing = [s for s in binds if find(s) <= 0 or not d.symval(find(s))]
print(f'bindings: {len(binds)} checked, missing: {missing}')
dup = {s for _, s, _, _ in hooks if sum(1 for _, x, _, _ in hooks if x == s) > 1}
print('duplicate hook symbols:', dup or 'none')
