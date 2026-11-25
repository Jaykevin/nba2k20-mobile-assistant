# 构建指南

## 需要的工具

| 工具 | 用途 | 说明 |
| --- | --- | --- |
| Python 3.8+ | 构建脚本 | |
| [Zig 0.14.1](https://ziglang.org/download/) | 交叉编译 arm64 原生库 | 只用它自带的 clang，不需要 NDK |
| JDK 11+ | javac、java、keytool | |
| `android.jar` | 编译面板代码 | 任意 Android SDK 平台版本 |
| `r8.jar` | D8 转 dex | [Google Maven](https://dl.google.com/dl/android/maven2/com/android/tools/r8/) |
| Android build-tools | `zipalign`、`apksigner.jar` | |
| `apktool.jar` | 制作基础 APK、存档备份工具 | [Apktool](https://github.com/iBotPeaches/Apktool) |
| adb | 安装到手机 | 可选 |

## 配置

复制 `local_config.example.json` 为 `local_config.json`，把路径改成你电脑上的。这个文件在 `.gitignore` 里，不会被提交。

签名：`keystore` 填密钥文件路径；密码三选一——环境变量 `NBA2K20_KEYSTORE_PASSWORD`、`keystore_password`、或 `keystore_password_file`（一个 `{"password": "..."}` 的 JSON 文件）。`expected_cert_sha256` 可选，填了就会核对证书，防止用错密钥打出装不上的包。

还没有密钥可以这样生成一个（以后所有版本都用它，丢了就只能卸载重装）：

```
keytool -genkeypair -keystore my-game.p12 -storetype PKCS12 -alias nba2k20 -keyalg RSA -keysize 2048 -validity 10000
```

## 基础 APK

构建脚本把助手的 `classes2.dex` 和 `lib/arm64-v8a/lib2k20float.so` 替换进 `base/base.apk`。基础 APK 要用你自己的游戏 APK 做一次：

1. 先构建一次助手（`python build.py --native` 生成 `.so`，`python build.py` 会在缺基础 APK 时停下，但 `out/dex/classes.dex` 已经生成）。
2. 用打补丁工具生成：把 `tools/patcher/patch.py` 用到你的原版 APK 上，它做的就是这件事（解包 → 在 `gameactivity` 的 `onCreate/onResume/onPause/onDestroy` 里插入对 `com.local.nba2k20.FloatMenu` 的四行调用 → 回编译 → 加入助手文件 → 签名）。生成的 APK 直接拿来当 `base/base.apk` 即可。
   - 打补丁工具需要 `mod/` 目录：先运行 `python tools/make_release.py` 生成发布包，解压后在里面运行 `patch.py`。

插入的四行 smali（参考）：

```
onCreate  末尾 return-void 之前：invoke-static {p0}, Lcom/local/nba2k20/FloatMenu;->start(Landroid/app/Activity;)V
onResume  末尾 return-void 之前：invoke-static {p0}, Lcom/local/nba2k20/FloatMenu;->resume(Landroid/app/Activity;)V
onPause   .locals 之后：        invoke-static {p0}, Lcom/local/nba2k20/FloatMenu;->pause(Landroid/app/Activity;)V
onDestroy .locals 之后：        invoke-static {p0}, Lcom/local/nba2k20/FloatMenu;->stop(Landroid/app/Activity;)V
```

## 构建与安装

```
python build.py              # 输出 out/NBA2K20-助手.apk
python build.py --native     # 只编译并检查原生库（导入表白名单）
python build.py --install    # 构建并 adb install -r（保留游戏数据）
python re/verify_tables.py   # 离线核对所有挂钩/绑定符号都在游戏库里
python tools/make_release.py # 打发布包 dist/NBA2K20-助手-补丁工具-vX.Y.Z.zip
```

版本号在 `gen/gen.py` 的 `VERSION`，面板标题会显示。

## 改功能的套路

1. 新设置项、统计项加在 `gen/gen.py` 列表**末尾**（不要插中间，否则老用户的设置会错位）。
2. 原生逻辑放到对应的 `src/native/f_*.c`，需要的游戏函数在 `game.c` 里 `BIND`，要挂钩的函数加到 `main.c` 的 `hooks[]`。
3. 面板控件在 `Overlay.java` 里加一行 `toggle(...)` / `slider(...)`。
4. 真机验证：日志标签 `2K20Assist`；有的系统会限制应用日志，面板「快捷」页有诊断行可以看。
