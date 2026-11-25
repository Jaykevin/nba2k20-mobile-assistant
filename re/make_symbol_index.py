"""Writes the game library's dynamic symbol table (.dynsym order: name, address, size, type, section) as
JSON - the format the analysis scripts in this folder expect.

Usage: python make_symbol_index.py [output.json]   (default: re/symbols.json; the library path comes
from game_lib in local_config.json)"""
import json
import struct
import sys
from pathlib import Path


def build(lib_path, out_path):
    data = Path(lib_path).read_bytes()
    shoff = struct.unpack_from('<Q', data, 40)[0]
    shentsize, shnum, _shstrndx = struct.unpack_from('<HHH', data, 58)
    sections = [struct.unpack_from('<IIQQQQIIQQ', data, shoff + i * shentsize) for i in range(shnum)]
    dynsym = next(s for s in sections if s[1] == 11)   # SHT_DYNSYM
    strtab = sections[dynsym[6]]
    symbols = []
    for off in range(dynsym[4], dynsym[4] + dynsym[5], 24):
        st_name, st_info, _st_other, st_shndx, st_value, st_size = struct.unpack_from('<IBBHQQ', data, off)
        start = strtab[4] + st_name
        name = data[start:data.index(b'\0', start)].decode('utf-8', 'replace')
        symbols.append({'name': name, 'address': st_value, 'size': st_size, 'type': st_info & 0xf,
                        'section': st_shndx})
    Path(out_path).write_text(json.dumps(symbols), encoding='utf-8')
    return symbols


if __name__ == '__main__':
    sys.path.insert(0, str(Path(__file__).resolve().parent))
    from reconfig import LIB
    target = Path(sys.argv[1]) if len(sys.argv) > 1 else Path(__file__).resolve().parent / 'symbols.json'
    print(len(build(LIB, target)), 'symbols ->', target)
