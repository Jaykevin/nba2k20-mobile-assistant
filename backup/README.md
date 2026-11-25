# 存档备份小工具

把手机里游戏的私有数据（含存档）打成一个 zip，导出到电脑。**只读**，不会改动游戏数据；也不提供还原功能。

## 原理

安卓不允许别的应用读取游戏的私有目录，但允许「和游戏用同一把签名密钥」的 instrumentation 包在游戏进程里运行。
`SaveBackup` 就是这样一个包：它在游戏进程里把下面这些打包成
`/sdcard/Android/data/com.t2ksports.nba2k20and/files/save-backup.zip`，adb 可以直接拉取：

- 游戏私有目录 `data/`（跳过 `cache`、`code_cache`、`lib`）
- 外部存储里的 `DAT/` 目录和 `usrdat.tpt`
- `index.txt`：所有文件的大小清单；单个超过 32 MB 的文件只记在清单里，不打包

所以它**只适用于你自己签名安装的游戏**（用 `build.py` 打的包，或用补丁工具打的包）。

## 使用

需要和编译助手一样的工具链（见 [docs/build.md](../docs/build.md)），并在 `local_config.json` 里填好 `apktool_jar` 和签名密钥：

- 用 `build.py` 自己打包的：直接复用同一份 `keystore` 配置。
- 用补丁工具打包的：`keystore` 填补丁工具目录里的 `patcher-key.p12`，`keystore_password_file` 填同目录的 `patcher-key.json`。

先保存并退出游戏（运行备份时系统会强制结束游戏进程），然后：

```
python backup/build_backup.py
adb install -r backup/out/save-backup.apk
adb shell am instrument -w -r com.local.nba2k20.backup/.SaveBackup
adb pull /sdcard/Android/data/com.t2ksports.nba2k20and/files/save-backup.zip
```

`am instrument` 结束时会输出 `backup=`（zip 路径）、`files=`（文件数）、`bytes=`（原始大小）、`zip_bytes=`（zip 大小）；
出错时输出 `error=`。在 Git Bash 里执行 `adb pull` 要加 `MSYS_NO_PATHCONV=1`，否则路径会被改写。

用完可以卸载：`adb uninstall com.local.nba2k20.backup`，再删掉手机上的 `save-backup.zip`。
