# Starfall Lab / 流火实验室测试

正式 DLL 在项目根目录运行 `build.cmd` 编译，产物是 `build/Starfall.dll`。以下测试使用独立配置和测试 DLL。

| 位置 | 内容 |
|---|---|
| `maps/` | 双人温和草地靶场、科研点测试地图和靶场预览图。手动测试时把所需地图复制到游戏的 `Maps/Custom/` |
| `game/hex-icons/` | 使用海克斯示例 INI 实机绘制 15 个项目，校验稀有蓝紫色、史诗金色、PCX、缺图回退及透明像素，仅测试新开局 |
| `game/research-hud/` | 边框像素、鼠标拦截、科研点门槛、卡片动效、WAV 配置与播放提交、刷新、购买和读档 |
| `unit/ResearchAudioTest.cpp` | 内嵌与外部 PCM WAV 解码、非法格式与缺失文件检查 |
| `game/research-cost/` | 科研生产报价、类别与类型叠加、所属方隔离、开工锁价、取消退款和存读档 |
| `game/conversion/` | 无 Phobos 时的步兵、载具、飞行器转换、被动与科研入口、单位状态和存读档；检查当前旧版 Phobos 不提供转换接口 |
| `game/passive-gained/` | 含等号的血量门槛、科研授予现役和新造单位、续期与重获、科研点归属及读档 |
| `game/reapply/` | 事件动作与弹头重复授予加层，满层续期、单次引爆去重、来源重叠与存读档 |
| `game/passive-stacks/` | 层数上限、增减、跨单位、重叠来源、易主、失效、公式快照与存读档 |
| `game/max-health/` | 永久生命上限、正负调整、实际损血和修理点数、同型对照、公式及存读档 |
| `game/health-formula/` | 血量公式语法、受伤与治疗、跨单位事件快照、小数累计、新档往返和 0.26 整数记录兼容 |
| `game/aura/` | 局部和全图光环、筛选、重叠与其他来源、获得事件、存读档和来源销毁 |
| `game/power-state/` | 建筑供电与耗电倍率、叠加、归零、撤回、易主和读档后所属方的实际电网数值 |
| `game/immune/` | 延时免疫、同型单位对照、常规与永久心控、心灵控制器、多个来源与存读档 |
| `game/before-damage/` | 受伤前抵挡、装甲后公式和动作链、攻击者筛选、强制伤害、额外生命与建筑，以及次数存读档 |
| `game/condition-formula/` | 条件公式、层数和数量边界、事件快照、缺失目标以及存读档 |
| `game/passenger-consume/` | 消耗直属乘客及其多层载员、目标快照、空载回退、开放运输与建筑、事件存读档 |
| `game/promotion/` | 老兵、精英及通用晋升触发，直接跨级与待处理事件存读档 |
| `game/transport-support/` | 乘客支援授予、叠层与手动层数、状态条件、获得事件与存读档 |
| `game/signal/` | 自定义信号范围、目标及发送者快照、公式、连锁上限和存读档 |
| `game/deployed-state/` | 三种部署步兵的架设、撤收、延时生效和读档 |
| `game/fire-rate/` | 射速累计与状态叠加、同武器实弹间隔、连射间隔及存读档 |
| `game/before-fire/` | 开火前固定替换、对空对地及射程回退、连发、事件兼容和连发中存读档 |
| `game/weapon-range/` | 共用武器的两辆灰熊、射程倍率与累计上下限、跨原射程索敌及开火、存读档 |
| `game/spawn-passives/` | 原生子机授予、母机隔离、来源叠加和存读档 |
| `game/projectiles/` | 弹体倍率、同一弹头的伤害范围、命中动画宽度及飞行和爆炸期间的存读档测试 |
| `game/script-grants/` | 地图触发、小队授予、多目标筛选、下一行脚本和读档到期测试 |
| `resources/` | 检查正式 DLL 内嵌的 3 张科研 PNG 和 3 份 WAV 资源 |
| `local/` | 本机测试记录、历史测试归档与存档样本。此目录已加入 Git 忽略列表 |

## 实机验证

从项目根目录在 Windows PowerShell 中运行。把路径换成有 Syringe、Ares、Phobos 和游戏资源的《远征》安装目录。

```powershell
.\tests\run-game.ps1 -Case health-formula -GameDirectory 'D:\Game\红色警戒2：远征'
.\tests\run-game.ps1 -Case conversion -GameDirectory 'D:\Game\红色警戒2：远征'
.\tests\run-game.ps1 -Case immune -GameDirectory 'D:\Game\红色警戒2：远征'
.\tests\run-game.ps1 -Case deployed-state -GameDirectory 'D:\Game\红色警戒2：远征'
.\tests\run-game.ps1 -Case fire-rate -GameDirectory 'D:\Game\红色警戒2：远征'
.\tests\run-game.ps1 -Case weapon-range -GameDirectory 'D:\Game\红色警戒2：远征'
.\tests\run-game.ps1 -Case projectiles -GameDirectory 'D:\Game\红色警戒2：远征'
.\tests\run-game.ps1 -Case script-grants -GameDirectory 'D:\Game\红色警戒2：远征'
```

每条命令编译对应测试 DLL，从指定游戏目录复制运行文件，在 `tests/local/runs/` 的独立目录中测试新开局和读档。通过后保留 `tests/local/records/` 中的两份报告，清除临时游戏副本。失败时可在输出的测试目录里查看探针日志和游戏的 `debug/`。

只编译测试 DLL 时运行 `tests\build.cmd <用例>`，用例名可填 `projectiles`、`script-grants`、`weapon-range`、`fire-rate`、`deployed-state`、`immune`、`health-formula`、`research-hud` 、`hex-icons` 、`max-health` 、`passive-gained` 、`research-cost` 或 `spawn-passives`。测试入口在各自的 `Probe.cpp`，与当前正式源码一同编译。测试 DLL 输出到 `build/tests/<用例>/`，只在含有 `Starfall.test` 标记的测试目录中执行自动化动作。

## 界面资源验证

编译正式 DLL 后，从项目根目录用 Windows PowerShell 运行：

```powershell
Add-Type -Path tests/resources/ResourceCheck.cs -ReferencedAssemblies System.Drawing
[ResourceCheck]::Check((Resolve-Path build/Starfall.dll).Path)
```

## 早期测试资料

本机的 `local/legacy/historical-tests.zip` 保存早期探针、当时使用的构建脚本和配置，条目保留旧目录名，方便与历史日志对照。`local/saves/` 保存历史存档和序列化数据样本。`local/records/` 也保留了此前的实机截图和验证记录。
