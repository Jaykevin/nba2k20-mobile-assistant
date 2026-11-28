# 工作原理与游戏机制笔记

适用于 NBA 2K20 安卓版 v98.0.2（`libnba2k20_clean_opt.so`）。地址都是相对库加载基址的偏移。

## 整体结构

- **注入方式**：游戏主 Activity 的四个生命周期方法里各插一行，调用 `FloatMenu`；`Native.start()` 加载 `lib2k20float.so`。
- **挂钩方式**：不改游戏代码，只改 GOT/PLT 槽（以及数据段里的函数指针）。`elfhook.c` 用 `dl_iterate_phdr` 找到游戏库，按**符号名**查 `.dynsym`，扫描 JUMP_SLOT / GLOB_DAT / ABS64 重定位，**先确认槽里存的就是原函数地址**才改。同组挂钩缺一个就整组不装，避免半装状态。
- **单编译单元**：`main.c` 包含所有 `f_*.c`；导出只有 JNI 函数，构建时检查导入表白名单（只允许 libc/libdl/liblog 的少数函数）。
- **设置**：Java 面板写 SharedPreferences 并调用 `Native.set`；原生侧是一组用 relaxed 原子读写的 int，钩子随时读取。`gen/gen.py` 是设置/统计 ID 的唯一来源。
- **每帧入口**：挂 `AI_Update`，在游戏逻辑跑之前处理延迟动作（抓帽、断球后抱球、超级抓帽）、识别我方、写诊断。

## 关键游戏机制（实测）

### 盖帽与抓帽
- 游戏里**所有盖帽**最后都会调用 `EVT_BallBlocked`（它会给球加 `0x800` 已盖标记，每次新出手由 `Phy_InitBallFlagsForShot` / `EVT_BallShot` 清掉）。来源有：
  - 跳投扇帽：球和手的物理碰撞 `COL_DoBallPlayerCollisions → EVT_BallDeflectedOffPlayer`（只有球处于出手飞行状态 mode 3、碰的是防守方时才算盖帽）；
  - 上篮/扣篮的双人封盖：`MVS_HandleBlockCollisionBlockToken → PHY_DeflectShot`；
  - 抓帽动画：`MVS_HandleBlockToken1`，只有那一帧手正好碰到球才会抓住，否则什么都不发生（连盖帽都不算）。
- 抓帽的做法：在 `EVT_BallBlocked` 里记下盖帽人，**下一帧开始时**调用游戏自己的 `EVT_BallCaught`（和自然抓帽同一套：挂球 + 球权/统计处理）。不能在事件里面直接挂球，因为偏转代码在事件返回后还会继续改球的状态。
- 抓帽动画的门槛是 `Takeover_CanPlayerDoGrabBlocks`；但在街头半场（game type 8）游戏只按能力值决定，忽略这个函数。

### 比赛状态（`REF_GetPlayState`）
- 10 = 比赛进行中；8 = 换球权 / 清球；18 = 吹哨暂停。抓帽、断球后抱球只在 10 时执行。

### 「我方」怎么认
- `AI_GetPlayerTuneIndex(team)` **不是**稳定的我方/电脑标记：街头半场每次换球权，球员会**互换队伍对象**（`player+0x98`），tune 索引跟着翻转。
- 稳定的是 `gAi_HomeTeam` / `gAi_AwayTeam` 的场上名单（`+0xa0` 起按位置的 `AI_PLAYER*`）。助手每帧找出「有人在用手柄」的那份名单作为我方；能力参数写到我方球员**当前**所在队伍对象对应的那一列。
- `Profile_IsUserProfileControlled` 在辉煌生涯这类「一人一个球员」的模式里，还会把**本队持球人**也报成「用户控制」。所以只有对持球人才需要再核对档案里记录的球员（档案在库内 `+0x383b060`，客队 `+0xd08`，`+0x20` 是球员）。

### 弹跳
- 起跳高度在 `PHY_ApplyActorAnimationPhysics` / `PHY_ApplyPhysicsAugmentation` 里叠加；站立时根节点高度约 100～110（在骨盆，不是地面）。
- 叠加高度会让手离开动画预期的位置，所以封盖、抢篮板时会把额外高度限制在「手刚好够到球」，并避开干扰球（`REF_IsTouchingBallGoaltend`）。
- 扣篮交给游戏自己的扣篮物理（`PHY_StartDunkPhysics` 前撤掉叠加的高度）。

### 扣篮动作
- 扣篮池：`Mvs_GetGenericDunkPool` 与 `Mvs_GetSigDunkPool(包号)`，每条 0x68 字节；`+0x1c/+0x20` 减去动画根位移就是这个动作预期的起跳距离。自选动作时优先挑起跳距离与实际离篮距离相近的动作。

### 比赛速度
- `Game_UpdateCommon` 从 `PROCESS_INSTANCE + 0x3354` 读取帧时长，依次分发给比赛输入、AI、镜头和回放等模块。`f_speed.c` 只在这次更新期间将帧时长乘以 `GAME_SPEED / 100`，返回后恢复原值，避免逐帧累乘或影响外部菜单。
- 默认 100%，范围 50%～200%；设置追加到标量表末尾，原有设置编号与存储键不变。暂停帧的 0 保持为 0，非正常帧时长直接交给原游戏处理，不改游戏速度存档字段。
- 原生 `AI_Update` 在内部将单帧时长限制到 0.05 秒；因此低帧率下实际加速幅度可能小于所选倍率。保留原游戏的上限，不放大物理步长或额外调用比赛更新。
- 回归检查：`python tools/test_game_speed.py`，覆盖 720 次更新、速度档位、帧时长恢复、暂停、异常帧时长和恢复 1×。

## 分析脚本

`re/` 里的脚本需要你自己的游戏库：在 `local_config.json` 填 `game_lib`，第一次运行会从库里生成符号表 `re/symbols.json`。常用：

```
python re/symsearch.py 正则          # 搜符号
python re/disasm.py 符号名 0x地址:长度  # 反汇编
python re/xref.py 符号名              # 谁调用了它
python re/gotcheck.py 符号名          # 有没有 GOT 槽（能不能挂）
python re/verify_tables.py           # 核对 main.c/game.c 里所有挂钩和绑定
```
