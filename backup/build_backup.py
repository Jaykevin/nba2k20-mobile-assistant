"""Builds save-backup.apk: an instrumentation package (same signing key as your game build) that zips the
game's private data, so saves can be pulled to a PC. Run: python backup/build_backup.py
Uses the tool paths and keystore from local_config.json (see docs/build.md and backup/README.md)."""
from pathlib import Path
import shutil
import sys
import zipfile

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))
import build as B   # tool paths, run(), sign() from the main build script

OUT = Path(__file__).resolve().parent / 'out'
SRC = Path(__file__).resolve().parent
if OUT.exists():
    shutil.rmtree(OUT)
OUT.mkdir()
apktool_jar = B.config_path('apktool_jar')
if not apktool_jar or not apktool_jar.exists():
    raise SystemExit('[失败] local_config.json 里需要 apktool_jar（用它自带的 aapt 打包清单）')
aapt = OUT / ('aapt.exe' if sys.platform == 'win32' else 'aapt')
prebuilt = 'prebuilt/windows/aapt_64.exe' if sys.platform == 'win32' else 'prebuilt/linux/aapt_64'
with zipfile.ZipFile(apktool_jar) as archive:
    aapt.write_bytes(archive.read(prebuilt))
aapt.chmod(0o755)
classes = OUT / 'classes'
classes.mkdir()
B.run([B.tool(B.JAVA_BIN, 'javac'), '-J-Duser.language=en', '--release', '8', '-encoding', 'UTF-8',
       '-cp', B.ANDROID_JAR, '-d', classes, SRC / 'SaveBackup.java'], 'javac')
jar = OUT / 'backup.jar'
with zipfile.ZipFile(jar, 'w') as archive:
    for path in classes.rglob('*.class'):
        archive.write(path, path.relative_to(classes).as_posix())
dex = OUT / 'dex'
dex.mkdir()
B.run([B.tool(B.JAVA_BIN, 'java'), '-cp', B.R8_JAR, 'com.android.tools.r8.D8', '--min-api', '21',
       '--lib', B.ANDROID_JAR, '--output', dex, jar], 'D8')
unsigned = OUT / 'unsigned.apk'
B.run([aapt, 'package', '-f', '-M', SRC / 'AndroidManifest.xml', '-I', B.ANDROID_JAR, '-F', unsigned], 'aapt')
with zipfile.ZipFile(unsigned, 'a', zipfile.ZIP_DEFLATED) as archive:
    archive.write(dex / 'classes.dex', 'classes.dex')
aligned = OUT / 'aligned.apk'
B.run([B.tool(B.BUILD_TOOLS, 'zipalign'), '-f', '4', unsigned, aligned], 'zipalign')
apk = OUT / 'save-backup.apk'
B.sign(aligned, apk)
print('BUILT', apk)
