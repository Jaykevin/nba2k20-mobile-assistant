# 参与贡献

欢迎一起改进！提交之前请看一下这几条。

## 不要提交的东西

- 游戏本体的任何文件：APK、`libnba2k20_clean_opt.so`、解包出来的资源和 smali、存档。
- 签名密钥和密码：`*.p12`、`*.jks`、`patcher-key.json`，以及 `local_config.json`。

这些已经写进 `.gitignore`，提交前再用 `git status` 确认一遍。

## 改代码

- 构建环境见 [docs/build.md](docs/build.md)，已经摸清的游戏机制见 [docs/notes.md](docs/notes.md)。
- 新的设置项、统计项只加在 `gen/gen.py` 对应列表的**末尾**；不要改已有设置的存储键名，否则老用户的设置会错位或丢失。
- 新挂钩先用 `python re/gotcheck.py 符号名` 确认有 GOT 槽，改完跑 `python re/verify_tables.py`。
- 原生库只允许导入少数 libc / libdl / liblog 函数，`python build.py --native` 会检查。
- 写法跟随所在文件。钩子里不做耗时操作；不要在游戏事件回调里直接改球的状态，放到下一帧开头处理（原因见 notes）。

## 提 Pull Request 时写清楚

- 改了什么、为什么改。
- 在哪些模式实测过（辉煌生涯 / 街头 1v1 / 街头 3v3 / 快速比赛），手机型号和安卓版本。
- 没有实测的部分请注明。

## 反馈问题

请用 Issue 的「问题反馈」模板，附上游戏模式、助手版本（面板标题里的 vX.Y.Z）、相关开关，以及面板「快捷」页的诊断行。
