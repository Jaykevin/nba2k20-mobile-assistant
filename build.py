"""NBA 2K20 手游助手 - 构建脚本 (build pipeline).

Steps: generate tables -> native .so (zig) -> Java classes -> dex (D8) -> patch base APK
(replace classes2.dex + lib2k20float.so, raw-copy everything else) -> zipalign -> sign -> verify.

Machine-specific paths and signing settings are read from local_config.json (not in git): copy
local_config.example.json and fill it in - see docs/build.md.

Usage:
  python build.py              build out/NBA2K20-助手.apk
  python build.py --native     only compile + check the native library
  python build.py --install    build and `adb install -r` (keeps game data)
"""
from pathlib import Path
import argparse
import hashlib
import json
import os
import re
import shutil
import struct
import subprocess
import sys
import zipfile
import zlib

ROOT = Path(__file__).resolve().parent
SRC = ROOT / 'src'
OUT = ROOT / 'out'
CONFIG_FILE = ROOT / 'local_config.json'
FINAL_APK = OUT / 'NBA2K20-助手.apk'


def _load_config():
    if not CONFIG_FILE.exists():
        raise SystemExit('[失败] 缺少 local_config.json：复制 local_config.example.json 为 local_config.json，'
                         '按 docs/build.md 填好工具路径和签名设置')
    return json.loads(CONFIG_FILE.read_text(encoding='utf-8'))


CONFIG = _load_config()


def config_path(key, default=None):
    """A path from local_config.json; relative paths are relative to this folder."""
    value = CONFIG.get(key) or default
    if not value:
        return None
    path = Path(value)
    return path if path.is_absolute() else ROOT / path


def tool(folder, name):
    """name.exe (Windows) or name inside a tool folder."""
    for candidate in (folder / (name + '.exe'), folder / name):
        if candidate.exists():
            return candidate
    return folder / name


ZIG = config_path('zig')
ANDROID_JAR = config_path('android_jar')
R8_JAR = config_path('r8_jar')
JAVA_BIN = config_path('java_bin')            # folder with java, javac, keytool
BUILD_TOOLS = config_path('build_tools')      # folder with zipalign and apksigner.jar
KEYSTORE = config_path('keystore')
BASE_APK = config_path('base_apk', 'base/base.apk')
ADB = CONFIG.get('adb') or 'adb'
EXPECTED_CERT = (CONFIG.get('expected_cert_sha256') or '').lower()


def keystore_password():
    """Environment variable first, then local_config.json (password, or a JSON file holding it)."""
    if os.environ.get('NBA2K20_KEYSTORE_PASSWORD'):
        return os.environ['NBA2K20_KEYSTORE_PASSWORD']
    if CONFIG.get('keystore_password'):
        return CONFIG['keystore_password']
    password_file = config_path('keystore_password_file')
    if password_file and password_file.exists():
        return json.loads(password_file.read_text(encoding='utf-8'))['password']
    raise SystemExit('[失败] 没有签名密码：设置环境变量 NBA2K20_KEYSTORE_PASSWORD，'
                     '或在 local_config.json 里填 keystore_password / keystore_password_file')

# Symbols the native module may import from bionic (libc / libdl / liblog). Anything else is a bug
# (e.g. a compiler-rt helper that would make dlopen fail at runtime).
ALLOWED_IMPORTS = {
    'dlopen', 'dlsym', 'dladdr', 'dl_iterate_phdr', 'mprotect', 'getpagesize', 'sysconf',
    'memcpy', 'memset', 'memmove', 'memcmp', 'strlen', 'strcmp', 'strncmp', 'strstr', 'strrchr',
    'snprintf', 'vsnprintf', 'clock_gettime', '__android_log_print', '__android_log_write',
    'abort', 'getpid', 'gettid', '__cxa_finalize', '__cxa_atexit',
}


sys.stdout.reconfigure(encoding='utf-8', errors='replace')


def run(args, label, cwd=None):
    result = subprocess.run([str(a) for a in args], capture_output=True, text=True, encoding='utf-8',
                            errors='replace', cwd=cwd)
    if result.stdout.strip():
        print(result.stdout.strip(), flush=True)
    if result.stderr.strip():
        print(result.stderr.strip(), flush=True)
    if result.returncode:
        raise SystemExit(f'[失败] {label} (exit {result.returncode})')
    return result.stdout


NATIVE_DEFINES = []   # extra -D flags (device tests only), set from --define


def build_native():
    stubs = OUT / 'link-stubs'
    stubs.mkdir(parents=True, exist_ok=True)
    empty = stubs / 'empty.c'
    empty.write_text('void local_link_stub(void) {}\n')
    for name in ('c', 'dl', 'log'):   # link-time stand-ins for bionic; the real ones load on the phone
        target = stubs / f'lib{name}.so'
        if not target.exists():
            run([ZIG, 'cc', '-target', 'aarch64-linux-android', '-shared', '-nostdlib', '-fPIC', '-O2',
                 '-fno-sanitize=all', f'-Wl,-soname,lib{name}.so', empty, '-o', target], f'link stub lib{name}')
    lib = OUT / 'lib2k20float.so'
    exports = OUT / 'exports.map'
    exports.write_text('{ global: Java_*; local: *; };\n')   # export only JNI entry points
    run([ZIG, 'cc', '-target', 'aarch64-linux-android', '-shared', '-nostdlib', '-ffreestanding', '-fPIC',
         f'-Wl,--version-script={exports}',
         '-O2', '-fno-sanitize=all', '-fno-stack-protector', '-fvisibility=hidden', '-fno-builtin-printf',
         '-Wall', '-Wextra', '-Wno-unused-parameter', '-Werror=implicit-function-declaration',
         '-Werror=return-type', '-Werror=incompatible-pointer-types', '-Werror=int-conversion',
         '-Wl,-soname,lib2k20float.so', '-Wl,-z,max-page-size=16384', '-Wl,--hash-style=both',
         '-Wl,--no-as-needed', '-Wl,-s', '-Wl,-z,noexecstack', '-Wl,-z,relro', '-Wl,-z,now',
         *['-D' + d for d in NATIVE_DEFINES],
         SRC / 'native' / 'main.c', stubs / 'libc.so', stubs / 'libdl.so', stubs / 'liblog.so', '-o', lib],
        'native module')
    check_imports(lib)
    print(f'native: {lib.stat().st_size} bytes', flush=True)
    return lib


def check_imports(lib):
    data = lib.read_bytes()
    shoff = struct.unpack_from('<Q', data, 40)[0]
    shentsize, shnum, shstrndx = struct.unpack_from('<HHH', data, 58)
    sections = [struct.unpack_from('<IIQQQQIIQQ', data, shoff + i * shentsize) for i in range(shnum)]
    dynsym = next(s for s in sections if s[1] == 11)  # SHT_DYNSYM
    strtab = sections[dynsym[6]]
    undefined, exported = [], []
    for off in range(dynsym[4], dynsym[4] + dynsym[5], 24):
        st_name, st_info, st_other, st_shndx, st_value, st_size = struct.unpack_from('<IBBHQQ', data, off)
        if not st_name:
            continue
        start = strtab[4] + st_name
        name = data[start:data.index(b'\0', start)].decode()
        (undefined if st_shndx == 0 else exported).append(name)
    bad = [n for n in undefined if n not in ALLOWED_IMPORTS]
    if bad:
        raise SystemExit('[失败] native module imports unexpected symbols: ' + ', '.join(bad))
    jni = [n for n in exported if n.startswith('Java_')]
    print('native imports: ' + ', '.join(sorted(undefined)), flush=True)
    print('native exports: ' + ', '.join(sorted(exported)), flush=True)
    if not jni:
        raise SystemExit('[失败] no JNI exports')


def build_java():
    classes = OUT / 'classes'
    if classes.exists():
        shutil.rmtree(classes)
    classes.mkdir(parents=True)
    sources = sorted((SRC / 'java').rglob('*.java'))
    run([tool(JAVA_BIN, 'javac'), '-J-Duser.language=en', '-J-Dfile.encoding=UTF-8', '-J-Dsun.stdout.encoding=UTF-8',
         '-J-Dsun.stderr.encoding=UTF-8', '-encoding', 'UTF-8', '--release', '8', '-Xlint:-options', '-g:none',
         '-classpath', ANDROID_JAR, '-d', classes] + sources, 'javac')
    jar = OUT / 'menu-classes.jar'
    with zipfile.ZipFile(jar, 'w', zipfile.ZIP_DEFLATED) as archive:
        for path in sorted(classes.rglob('*.class')):
            archive.write(path, path.relative_to(classes).as_posix())
    dex_dir = OUT / 'dex'
    if dex_dir.exists():
        shutil.rmtree(dex_dir)
    dex_dir.mkdir()
    run([tool(JAVA_BIN, 'java'), '-cp', R8_JAR, 'com.android.tools.r8.D8', '--release', '--min-api', '21',
         '--lib', ANDROID_JAR, '--output', dex_dir, jar], 'D8')
    dex = dex_dir / 'classes.dex'
    print(f'dex: {dex.stat().st_size} bytes from {len(sources)} sources', flush=True)
    return dex


SIGNATURE_FILE = re.compile(r'^META-INF/([^/]+\.(SF|RSA|DSA|EC)|MANIFEST\.MF)$', re.I)


def patch_apk(replacements, output, source=None):
    """Raw-copy every entry of the source APK (default BASE_APK, no recompression) except old signatures
    and replaced files; replacements are appended (new names are added)."""
    source = source or BASE_APK
    with zipfile.ZipFile(source) as zin, open(source, 'rb') as src, open(output, 'wb') as dst:
        central = []
        for info in zin.infolist():
            if SIGNATURE_FILE.match(info.filename) or info.filename in replacements:
                continue
            src.seek(info.header_offset)
            header = src.read(30)
            sig, _ver, flags, _method, _t, _d, _crc, _cs, _us, nlen, xlen = struct.unpack('<IHHHHHIIIHH', header)
            assert sig == 0x04034b50, info.filename
            assert not flags & 0x08, 'data descriptors not supported: ' + info.filename
            body = src.read(nlen + xlen + info.compress_size)
            central.append((info, dst.tell(), info.extra))
            dst.write(header + body)
        for name, payload in replacements.items():
            compressor = zlib.compressobj(9, zlib.DEFLATED, -15)
            packed = compressor.compress(payload) + compressor.flush()
            info = zipfile.ZipInfo(name, date_time=(2026, 11, 22, 0, 0, 0))
            info.compress_type = zipfile.ZIP_DEFLATED
            info.CRC = zlib.crc32(payload) & 0xffffffff
            info.compress_size, info.file_size = len(packed), len(payload)
            info.create_system, info.create_version, info.extract_version = 0, 20, 20
            info.external_attr = 0
            encoded = name.encode()
            dos_time, dos_date = 0, ((2026 - 1980) << 9) | (11 << 5) | 22
            central.append((info, dst.tell(), b''))
            dst.write(struct.pack('<IHHHHHIIIHH', 0x04034b50, 20, 0, 8, dos_time, dos_date, info.CRC,
                                  len(packed), len(payload), len(encoded), 0) + encoded + packed)
        cd_start = dst.tell()
        for info, offset, extra in central:
            name = info.filename.encode('utf-8')
            flags = info.flag_bits & ~0x08
            if any(ord(ch) > 127 for ch in info.filename):
                flags |= 0x800
            year, month, day, hour, minute, second = info.date_time
            dos_time = (hour << 11) | (minute << 5) | (second // 2)
            dos_date = ((year - 1980) << 9) | (month << 5) | day
            comment = info.comment or b''
            dst.write(struct.pack('<IHHHHHHIIIHHHHHII', 0x02014b50, (info.create_system << 8) | info.create_version,
                                  info.extract_version, flags, info.compress_type, dos_time, dos_date, info.CRC,
                                  info.compress_size, info.file_size, len(name), len(extra), len(comment), 0,
                                  info.internal_attr, info.external_attr, offset) + name + extra + comment)
        cd_size = dst.tell() - cd_start
        dst.write(struct.pack('<IHHHHIIH', 0x06054b50, 0, 0, len(central), len(central), cd_size, cd_start, 0))


def key_alias(password):
    if CONFIG.get('key_alias'):
        return CONFIG['key_alias']
    info = run([tool(JAVA_BIN, 'keytool'), '-J-Duser.language=en', '-list', '-v', '-keystore', KEYSTORE,
                '-storepass', password], 'read keystore')
    return re.search(r'Alias name:\s*(\S+)', info).group(1)


def sign(unsigned_aligned, output):
    """apksigner with the keystore from local_config.json; returns the certificate SHA-256."""
    password = keystore_password()
    apksigner = BUILD_TOOLS / 'apksigner.jar'
    run([tool(JAVA_BIN, 'java'), '-jar', apksigner, 'sign', '--ks', KEYSTORE,
         '--ks-key-alias', key_alias(password), '--ks-pass', 'pass:' + password, '--key-pass', 'pass:' + password,
         '--v1-signing-enabled', 'true', '--v2-signing-enabled', 'true', '--v3-signing-enabled', 'true',
         '--out', output, unsigned_aligned], 'sign')
    verify = run([tool(JAVA_BIN, 'java'), '-jar', apksigner, 'verify', '--print-certs', output], 'verify signature')
    return re.search(r'certificate SHA-256 digest: (\w+)', verify).group(1).lower()


def package(lib, dex):
    if not BASE_APK.exists():
        raise SystemExit(f'[失败] 找不到基础 APK：{BASE_APK}\n'
                         '  它要用你自己手机上的游戏 APK 制作，步骤见 docs/base-apk.md')
    unsigned = OUT / 'unsigned.apk'
    aligned = OUT / 'aligned.apk'
    patch_apk({'classes2.dex': dex.read_bytes(), 'lib/arm64-v8a/lib2k20float.so': lib.read_bytes()}, unsigned)
    run([tool(BUILD_TOOLS, 'zipalign'), '-f', '-p', '4', unsigned, aligned], 'zipalign')
    digest = sign(aligned, FINAL_APK)
    print(f'signing certificate SHA-256: {digest}', flush=True)
    if EXPECTED_CERT and digest != EXPECTED_CERT:
        # A different key cannot be installed over the game already on the phone (data would be lost).
        raise SystemExit('[失败] 签名证书和 local_config.json 里的 expected_cert_sha256 不一致，'
                         '这样的包无法覆盖安装到手机上已有的游戏')
    run([tool(BUILD_TOOLS, 'zipalign'), '-c', '-p', '4', FINAL_APK], 'zipalign check')
    with zipfile.ZipFile(FINAL_APK) as final, zipfile.ZipFile(BASE_APK) as base:
        base_names = {i.filename for i in base.infolist() if not SIGNATURE_FILE.match(i.filename)}
        final_names = {i.filename for i in final.infolist() if not SIGNATURE_FILE.match(i.filename)}
        assert base_names == final_names, base_names ^ final_names
        assert final.read('classes2.dex') == dex.read_bytes()
        assert final.read('lib/arm64-v8a/lib2k20float.so') == lib.read_bytes()
        bad = final.testzip()
        assert bad is None, bad
    sha = hashlib.sha256(FINAL_APK.read_bytes()).hexdigest()
    (OUT / 'build-result.json').write_text(json.dumps({
        'apk': str(FINAL_APK), 'sha256': sha, 'size': FINAL_APK.stat().st_size,
        'native_sha256': hashlib.sha256(lib.read_bytes()).hexdigest(),
        'dex_sha256': hashlib.sha256(dex.read_bytes()).hexdigest(),
    }, ensure_ascii=False, indent=2), encoding='utf-8')
    print(f'APK: {FINAL_APK} ({FINAL_APK.stat().st_size} bytes) sha256={sha}', flush=True)


def install():
    run([ADB, 'install', '-r', FINAL_APK], 'adb install')


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--native', action='store_true')
    parser.add_argument('--install', action='store_true')
    parser.add_argument('--define', action='append', default=[], help='extra native -D flag (device tests only)')
    args = parser.parse_args()
    NATIVE_DEFINES.extend(args.define)
    if args.define:
        print('NOTE: test defines active: ' + ', '.join(args.define), flush=True)
    OUT.mkdir(exist_ok=True)
    run([sys.executable, ROOT / 'gen' / 'gen.py'], 'generate tables')
    lib = build_native()
    if args.native:
        return
    dex = build_java()
    package(lib, dex)
    if args.install:
        install()


if __name__ == '__main__':
    main()
