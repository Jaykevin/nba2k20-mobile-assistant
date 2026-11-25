"""NBA 2K20 手游助手 - 一键打补丁工具。

用你自己的 NBA 2K20 安卓版 APK（v98.0.2）生成带助手面板的 APK。发布包里只有助手自己的文件，
不包含任何游戏内容。需要：Python 3.8+、Java 8+（命令行能运行 java 和 keytool）。

用法：
  python patch.py 原版游戏.apk                    输出 NBA2K20-助手-v<版本>.apk
  python patch.py 原版游戏.apk -o 输出.apk
  python patch.py 原版游戏.apk --keystore 我的.p12 --storepass 密码 [--alias 别名]

第一次运行会在本目录生成签名密钥 patcher-key.p12（密码在 patcher-key.json）。以后打新版本会复用它，
新版可以直接覆盖安装升级；这两个文件请自己保管好，不要发给别人。
"""
import argparse
import hashlib
import json
import os
import re
import secrets
import shutil
import struct
import subprocess
import sys
import tempfile
import zipfile
import zlib
from pathlib import Path

HERE = Path(__file__).resolve().parent
MOD = HERE / 'mod'
TOOLS = HERE / 'tools'
SMALI = Path('smali') / 'com' / 't2ksports' / 'nba2k20and' / 'gameactivity.smali'
GAME_LIB = 'lib/arm64-v8a/libnba2k20_clean_opt.so'
SIGNATURE_FILE = re.compile(r'^META-INF/([^/]+\.(SF|RSA|DSA|EC)|MANIFEST\.MF)$', re.I)
# Where the panel hooks into the game activity: (method, call, position)
HOOKS = [
    ('onCreate(Landroid/os/Bundle;)V', 'start', 'end'),
    ('onResume()V', 'resume', 'end'),
    ('onPause()V', 'pause', 'start'),
    ('onDestroy()V', 'stop', 'start'),
]

if hasattr(sys.stdout, 'reconfigure'):
    sys.stdout.reconfigure(encoding='utf-8', errors='replace')


def fail(message):
    raise SystemExit('[失败] ' + message)


def find_tool(name, java_home):
    folders = [Path(java_home) / 'bin'] if java_home else []
    if os.environ.get('JAVA_HOME'):
        folders.append(Path(os.environ['JAVA_HOME']) / 'bin')
    for folder in folders:
        for candidate in (folder / (name + '.exe'), folder / name):
            if candidate.exists():
                return str(candidate)
    found = shutil.which(name)
    if not found:
        fail(f'找不到 {name}：请安装 Java（8 或更新），或用 --java-home 指定 Java 安装目录')
    return found


def run(args, label):
    result = subprocess.run([str(a) for a in args], capture_output=True, text=True, encoding='utf-8',
                            errors='replace')
    if result.returncode:
        print(result.stdout[-2000:], result.stderr[-2000:], sep='\n')
        fail(f'{label}（退出码 {result.returncode}）')
    return result.stdout


# ------------------------------------------------------------------ smali patch
def patch_smali(path):
    lines = path.read_text(encoding='utf-8').splitlines()
    text = '\n'.join(lines)
    if 'Lcom/local/nba2k20/FloatMenu;' in text:
        fail('这个 APK 已经带过助手了，请用原版游戏 APK')
    out, method, position, call = [], None, None, None
    for line in lines:
        stripped = line.strip()
        if stripped.startswith('.method '):
            method = next((h for h in HOOKS if stripped.endswith(' ' + h[0])), None)
            position, call = (method[2], method[1]) if method else (None, None)
        invoke = f'    invoke-static {{p0}}, Lcom/local/nba2k20/FloatMenu;->{call}(Landroid/app/Activity;)V'
        if method and position == 'end' and stripped == 'return-void':
            out.append(invoke)
            out.append('')
        out.append(line)
        if method and position == 'start' and (stripped.startswith('.locals') or stripped.startswith('.registers')):
            out.append('')
            out.append(invoke)
            position = 'done'
        if stripped == '.end method':
            method = None
    patched = '\n'.join(out) + '\n'
    if patched.count('Lcom/local/nba2k20/FloatMenu;') < len(HOOKS):
        fail('游戏代码结构和预期不同，无法打补丁（请确认是 v98.0.2）')
    path.write_text(patched, encoding='utf-8')


# ------------------------------------------------------------------ APK assembly
def assemble(source, replacements, output):
    """Raw-copies the source APK (no recompression), drops old signatures, replaces/adds entries and aligns
    uncompressed data (4 bytes, .so at 4096) the way zipalign -p does."""
    with zipfile.ZipFile(source) as zin, open(source, 'rb') as src, open(output, 'wb') as dst:
        central = []
        for info in zin.infolist():
            if SIGNATURE_FILE.match(info.filename) or info.filename in replacements:
                continue
            src.seek(info.header_offset)
            header = bytearray(src.read(30))
            sig, _v, flags, method, _t, _d, _c, _cs, _us, nlen, xlen = struct.unpack('<IHHHHHIIIHH', header)
            if sig != 0x04034b50:
                fail('APK 文件格式异常：' + info.filename)
            name = src.read(nlen)
            extra = src.read(xlen)
            body = src.read(info.compress_size)
            if flags & 0x08:
                # sizes/CRC were in a data descriptor after the data: put the real values (from the central
                # directory) into the local header instead, and drop the descriptor
                struct.pack_into('<H', header, 6, flags & ~0x08)
                struct.pack_into('<III', header, 14, info.CRC, info.compress_size, info.file_size)
            if method == 0:
                align = 4096 if info.filename.endswith('.so') else 4
                start = dst.tell() + 30 + nlen + len(extra)
                extra += b'\0' * ((align - start % align) % align)
            struct.pack_into('<H', header, 28, len(extra))
            central.append((info, dst.tell(), info.extra))
            dst.write(bytes(header) + name + extra + body)
        for name, payload in replacements.items():
            packer = zlib.compressobj(9, zlib.DEFLATED, -15)
            packed = packer.compress(payload) + packer.flush()
            info = zipfile.ZipInfo(name, date_time=(2026, 1, 1, 0, 0, 0))
            info.compress_type = zipfile.ZIP_DEFLATED
            info.CRC = zlib.crc32(payload) & 0xffffffff
            info.compress_size, info.file_size = len(packed), len(payload)
            info.create_system, info.create_version, info.extract_version = 0, 20, 20
            info.external_attr = 0
            encoded = name.encode()
            dos_date = (2026 - 1980) << 9 | 1 << 5 | 1
            central.append((info, dst.tell(), b''))
            dst.write(struct.pack('<IHHHHHIIIHH', 0x04034b50, 20, 0, 8, 0, dos_date, info.CRC, len(packed),
                                  len(payload), len(encoded), 0) + encoded + packed)
        start = dst.tell()
        for info, offset, extra in central:
            name = info.filename.encode('utf-8')
            flags = info.flag_bits & ~0x08 | (0x800 if any(ord(c) > 127 for c in info.filename) else 0)
            y, mo, d, h, mi, s = info.date_time
            comment = info.comment or b''
            dst.write(struct.pack('<IHHHHHHIIIHHHHHII', 0x02014b50, info.create_system << 8 | info.create_version,
                                  info.extract_version, flags, info.compress_type, h << 11 | mi << 5 | s // 2,
                                  (y - 1980) << 9 | mo << 5 | d, info.CRC, info.compress_size, info.file_size,
                                  len(name), len(extra), len(comment), 0, info.internal_attr, info.external_attr,
                                  offset) + name + extra + comment)
        size = dst.tell() - start
        dst.write(struct.pack('<IHHHHIIH', 0x06054b50, 0, 0, len(central), len(central), size, start, 0))


# ------------------------------------------------------------------ signing
def own_key(keytool):
    key, info = HERE / 'patcher-key.p12', HERE / 'patcher-key.json'
    if key.exists() and info.exists():
        data = json.loads(info.read_text(encoding='utf-8'))
        return key, data['password'], data['alias']
    password, alias = secrets.token_hex(16), 'nba2k20'
    run([keytool, '-genkeypair', '-keystore', key, '-storetype', 'PKCS12', '-alias', alias, '-keyalg', 'RSA',
         '-keysize', '2048', '-validity', '10000', '-storepass', password, '-keypass', password,
         '-dname', 'CN=NBA2K20 Assistant User'], '生成签名密钥')
    info.write_text(json.dumps({'password': password, 'alias': alias}), encoding='utf-8')
    print(f'已生成签名密钥 {key.name}（密码在 {info.name}），以后升级都用它，请保管好。')
    return key, password, alias


def key_alias(keytool, keystore, password):
    listing = run([keytool, '-J-Duser.language=en', '-list', '-v', '-keystore', keystore, '-storepass', password],
                  '读取签名密钥')
    match = re.search(r'Alias name:\s*(\S+)', listing)
    if not match:
        fail('签名密钥里没有找到别名，请用 --alias 指定')
    return match.group(1)


def main():
    parser = argparse.ArgumentParser(description='用你自己的 NBA 2K20 v98.0.2 APK 生成带助手的 APK')
    parser.add_argument('apk', help='原版游戏 APK')
    parser.add_argument('-o', '--out', help='输出文件（默认 NBA2K20-助手-v<版本>.apk）')
    parser.add_argument('--keystore', help='自带签名密钥（.p12/.jks）；不填则用本目录自动生成的密钥')
    parser.add_argument('--storepass', help='自带密钥的密码')
    parser.add_argument('--alias', help='自带密钥的别名（默认自动读取）')
    parser.add_argument('--java-home', help='Java 安装目录（java 不在 PATH 时用）')
    args = parser.parse_args()

    meta = json.loads((MOD / 'version.json').read_text(encoding='utf-8'))
    source = Path(args.apk)
    if not source.exists():
        fail(f'找不到 {source}')
    with zipfile.ZipFile(source) as apk:
        names = set(apk.namelist())
        if GAME_LIB not in names or 'classes.dex' not in names:
            fail('这不是 NBA 2K20 安卓版的 APK')
        digest = hashlib.sha256(apk.read(GAME_LIB)).hexdigest()
    if digest != meta['game_lib_sha256']:
        fail(f'游戏版本不对：助手 v{meta["version"]} 只支持 {meta["game"]}（游戏库 SHA-256 不匹配）')
    java, keytool = find_tool('java', args.java_home), find_tool('keytool', args.java_home)
    output = Path(args.out or f'NBA2K20-助手-v{meta["version"]}.apk')

    with tempfile.TemporaryDirectory(prefix='nba2k20-patch-') as temp:
        work = Path(temp)
        print('1/4 解开游戏代码（apktool，约 1 分钟）…', flush=True)
        run([java, '-jar', TOOLS / 'apktool.jar', 'd', '-r', '-f', '-o', work / 'decoded', source], 'apktool 解包')
        print('2/4 挂上助手面板…', flush=True)
        patch_smali(work / 'decoded' / SMALI)
        run([java, '-jar', TOOLS / 'apktool.jar', 'b', work / 'decoded', '-o', work / 'rebuilt.apk'], 'apktool 回编译')
        with zipfile.ZipFile(work / 'rebuilt.apk') as rebuilt:
            classes = rebuilt.read('classes.dex')
        print('3/4 组装 APK…', flush=True)
        unsigned = work / 'unsigned.apk'
        assemble(source, {'classes.dex': classes,
                          'classes2.dex': (MOD / 'classes2.dex').read_bytes(),
                          'lib/arm64-v8a/lib2k20float.so': (MOD / 'lib2k20float.so').read_bytes()}, unsigned)
        print('4/4 签名…', flush=True)
        if args.keystore:
            if not args.storepass:
                fail('用 --keystore 时还要给 --storepass')
            keystore, password = Path(args.keystore), args.storepass
            alias = args.alias or key_alias(keytool, keystore, password)
        else:
            keystore, password, alias = own_key(keytool)
        run([java, '-jar', TOOLS / 'apksigner.jar', 'sign', '--ks', keystore, '--ks-key-alias', alias,
             '--ks-pass', 'pass:' + password, '--key-pass', 'pass:' + password, '--v1-signing-enabled', 'true',
             '--v2-signing-enabled', 'true', '--v3-signing-enabled', 'true', '--out', output, unsigned], '签名')
        run([java, '-jar', TOOLS / 'apksigner.jar', 'verify', output], '校验签名')
    print(f'完成：{output.resolve()}')
    print('安装：手机打开 USB 调试后运行  adb install -r "' + str(output) + '"，或把文件拷到手机上安装。')
    print('注意：手机上如果装的是官方版，签名不同不能覆盖，需要先卸载（会清掉本地存档，先备份）。')


if __name__ == '__main__':
    main()
