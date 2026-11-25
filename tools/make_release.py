"""Packs a release: dist/nba2k20-mobile-assistant-v<VERSION>.zip (folder NBA2K20-助手-补丁工具-v<VERSION> inside) with the patcher, the mod's own build output
and the two Apache-2.0 tools the patcher needs. No game content is included.
Usage: python build.py && python tools/make_release.py"""
import hashlib
import json
import re
import sys
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT))
import build as B   # noqa: E402  (tool paths from local_config.json)

VERSION = re.search(r"VERSION = '([^']+)'", (ROOT / 'gen' / 'gen.py').read_text(encoding='utf-8')).group(1)
GAME = 'NBA 2K20 安卓版 98.0.2'
GAME_LIB_SHA256 = 'c9a33a1fd8e303d5eb9cba8f6af1c46b63aa93ec9a8bc723fc294b51a09ef47b'   # libnba2k20_clean_opt.so
PATCHER = ROOT / 'tools' / 'patcher'


def sha256(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def main():
    dex, lib = B.OUT / 'dex' / 'classes.dex', B.OUT / 'lib2k20float.so'
    if not dex.exists() or not lib.exists():
        raise SystemExit('[失败] 先运行 python build.py')
    apktool, apksigner = B.config_path('apktool_jar'), B.BUILD_TOOLS / 'apksigner.jar'
    for tool in (apktool, apksigner):
        if not tool or not tool.exists():
            raise SystemExit(f'[失败] 找不到 {tool}（local_config.json 的 apktool_jar / build_tools）')
    name = f'NBA2K20-助手-补丁工具-v{VERSION}'   # folder inside the zip
    # GitHub strips non-ASCII characters from release asset names, so the zip itself gets an ASCII name.
    target = ROOT / 'dist' / f'nba2k20-mobile-assistant-v{VERSION}.zip'
    target.parent.mkdir(exist_ok=True)
    meta = {'version': VERSION, 'game': GAME, 'game_lib_sha256': GAME_LIB_SHA256,
            'native_sha256': sha256(lib), 'dex_sha256': sha256(dex)}
    with zipfile.ZipFile(target, 'w', zipfile.ZIP_DEFLATED) as z:
        z.write(PATCHER / 'patch.py', f'{name}/patch.py')
        z.write(PATCHER / '使用说明.md', f'{name}/使用说明.md')
        z.write(ROOT / 'LICENSE', f'{name}/LICENSE')
        z.writestr(f'{name}/mod/version.json', json.dumps(meta, ensure_ascii=False, indent=2))
        z.write(dex, f'{name}/mod/classes2.dex')
        z.write(lib, f'{name}/mod/lib2k20float.so')
        z.write(apktool, f'{name}/tools/apktool.jar')
        z.write(apksigner, f'{name}/tools/apksigner.jar')
        with zipfile.ZipFile(apktool) as jar:
            z.writestr(f'{name}/tools/LICENSE-Apache-2.0.txt', jar.read('LICENSE'))
        z.write(PATCHER / 'THIRD-PARTY.md', f'{name}/tools/THIRD-PARTY.md')
    print(f'{target} ({target.stat().st_size} bytes)')
    print(json.dumps(meta, ensure_ascii=False, indent=2))


if __name__ == '__main__':
    main()
