"""Paths for the analysis scripts, read from ../local_config.json:
  game_lib       libnba2k20_clean_opt.so taken from your own game APK (required)
  symbol_index   symbol list JSON; empty = re/symbols.json, generated from game_lib on first use
  capstone_path  folder holding the capstone package, if it is not pip-installed (pip install capstone)
"""
import json
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent
CONFIG = json.loads((ROOT / 'local_config.json').read_text(encoding='utf-8'))


def _path(key):
    value = CONFIG.get(key)
    if not value:
        return None
    path = Path(value)
    return path if path.is_absolute() else ROOT / path


LIB = _path('game_lib')
if not LIB or not LIB.exists():
    raise SystemExit('re/: 请在 local_config.json 的 game_lib 填上游戏库 libnba2k20_clean_opt.so 的路径'
                     '（从你自己的 APK 的 lib/arm64-v8a/ 里解出来）')
LIB = str(LIB)
SYMS = _path('symbol_index') or HERE / 'symbols.json'
if not SYMS.exists():
    from make_symbol_index import build
    build(LIB, SYMS)
SYMS = str(SYMS)
if _path('capstone_path'):
    sys.path.insert(0, str(_path('capstone_path')))
