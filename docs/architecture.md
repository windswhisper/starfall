# 模块边界与构建

当前按配置、科研模型、引擎效果和展示拆分独立编译单元。游戏扩展仍输出一个 `Starfall.dll`，配置字段、存档格式和每帧执行顺序沿用原有行为。

| 文件 | 责任及状态所有权 |
| --- | --- |
| `src/ConfigText.h` / `ConfigText.cpp` | 共用的字符串规范化、列表和非负整数解析、显示文字解码、配置诊断。不保存配置或游戏状态 |
| `src/Research.h` / `Research.cpp` | 科研配置与运行状态的唯一定义位置；点数、所有权、抽卡、刷新、购买提交与场景重置。候选缓存仅在实现文件中可见 |
| `src/ResearchConfig.cpp` | 从显式传入的配置文件及引擎合并规则读取项目，保留文本原字节，检查未知字段、效果及前置链，重读时清理展示和效果缓存 |
| `src/ResearchBonds.cpp` | 按所属方结算成员科技变化，记录羁绊即时奖励和当前生效的档位权益 |
| `src/ResearchStages.cpp` | 按计数索引检查阶段公式，依序发放阶段奖励并记录各所属方获得的持续权益 |
| `src/ResearchEffects.cpp` | 科研效果校验、资金和转换执行、科技生产权限、生产价格与退款 Hook。生产权限和价格缓存由本模块清理 |
| `src/ResearchInternal.h` | 科研内部接口，以及查询被动注册、所属方资格和调用单位转换的运行时桥接；不暴露技能容器或转换实现 |
| `src/ResearchUI.cpp` | 科研展示、输入、图标解码与卡片缩放缓存；保留主动技能界面片段 |
| `src/ResourceCache.h` / `ResearchAudio.h` / `ResearchText.h` | 有界资源缓存及音频、文字展示工具 |
| `src/VisualScale.cpp` | 原生 SHP、VXL、弹体及附着动画的视觉缩放 |
| `src/BuildingSpawn.h` / `BuildingSpawn.cpp` | 生成建筑的类型约束、完整占地检查、确定性邻近空地搜索和原生创建；无独立持久状态 |
| `src/BuildingTransform.h` / `BuildingTransform.cpp` | 完整占地一致性、建筑转换资格、原生对象创建与身份衔接；运行时回调重绑定缓存，已有按 ID 保存的被动和 Lua 状态继续使用 |
| `src/CarrierConversion.h` / `CarrierConversion.cpp` | 移动母体内置变形时调整子机槽位。游戏的子机管理器持有母子关联和补充计时，并负责存读档 |
| `modules/ActSp/` | 飞行子机、地面守卫、高级驻军、可建造高台，与主运行时共用游戏循环和存读档入口 |
| `modules/ArtSp/` | 视觉效果子模块。`TracerRules.cpp` 读取武器与美术配置、按单位和武器计数，`TracerDraw.cpp` 管理短时曳光并在战术视图绘制 |
| `modules/ArtSp/ContinuousBeam.cpp` | 按武器规则维护主目标光束，更新枪口、瞄准点及可见线段，在场景重置与单位销毁时清理锁定 |
| `src/Starfall.cpp` | 场景初始化与重置、每帧调用顺序、被动事件队列与分发、跨模块协调 |
| `src/PassiveTypes.h` / `PassiveRules.cpp` | 被动与动作定义、配置读取、持续状态和动作字段校验。验证脚本函数和拾取物 ID 时只读查询场景注册表 |
| `src/BuildingResearch.cpp` | 建筑与全局科研点收入、工程师和间谍触发的免费科研、免费选项与领取。拥有按建筑类型索引的规则缓存 |
| `src/ResearchDescription.cpp` | 科研卡片的条件、动作和组合效果说明。通过只读接口获取被动定义 |
| `src/ForceMove.cpp` | 强制位移、目标坐标与空中索敌、碰撞、落地和绘制。拥有移动记录与姿态恢复记录 |
| `src/runtime/PassiveRegistration.inc` | 注册被动、发现脚本、解析绑定、装载场景配置 |
| `src/runtime/PassiveStates.inc` / `PassiveGrants.inc` | 持续状态、属性修饰，以及光环、乘员、子机与临时被动的授予和回收 |
| `src/runtime/PassiveActions.inc` / `CombatHooks.inc` | 结果执行、变形与传送，以及引擎战斗事件采集 |
| `src/runtime/Savegame.inc` | 扩展存档流读写、历史版本校验、状态提交及存读档入口 |

## 接口与生命周期

外部调用科研使用 `Research.h`；内部模块和运行时桥接使用 `ResearchInternal.h`。科研模型不包含 YRpp 头文件，可在独立进程中用测试边界替代引擎调用进行验证。跨模块的运行状态在 `Research.cpp` 定义一次，头文件只声明；现有存档代码继续访问相同类型和字段。

- 载入配置仍由被动注册完成后调用 `Research::readConfiguration(configPath)`，随后配置校验才能查询被动是否存在。
- 抽卡只通过 `Detail::matchSeed()` 读取共享对局种子，使用原来的局部随机算法；打开本地界面不推进游戏随机数。
- `SharedRaritySequence` 用对局种子和各玩家的 `completedRounds` 计算相同轮次的公共稀有度；具体项目仍独立抽取，缺少指定稀有度时按个人候选池补选。仅成功普通购买推进轮次，免费建筑科研不推进。轮次随存档保存，旧存档按已有科技总数迁移。
- `Research::clearOfferCache()` 同时清除候选、生产价格和科技生产权限缓存，保持购买、读档及配置重读后的失效关系。
- `Research::reset()` 清理科研运行状态和展示资源，停止旧场景声音；场景重置同时调用 `BuildingResearch::clearTypeCaches()` 清理建筑类型缓存。
- 模块拆分不调整 `SF_Frame` 中各系统的调用顺序，也不改变存档标记、字段顺序或异常存档的事务提交方式。
- 特殊行为每帧在主运行时之后执行。沿用 `ActSp.GuardPoints` 和 `ActSp.HighPlatforms` 存档流以读取旧版状态。

## 构建与检查

`tools/module-sources.cmd` 维护共用编译单元清单，供正式构建、游戏探针和完整运行时单元测试调用。增加新的 `.cpp` 时更新该清单，不把实现重新包含到主文件。`hex-icons` 探针直接包含界面实现，因此其主运行时入口由测试构建脚本单独选择。

- `build.cmd`：构建正式 DLL。
- `tests/unit/build.cmd`：执行六个独立测试程序；`ResearchModelTest` 仅链接科研模型和配置文字工具，验证确定性、配置顺序变化、所属方隔离、刷新、购买失败及重置。
- `tests/run-game.ps1`：验证引擎效果、界面、新游戏和实际存读档。
- `tests/run-multiplayer.ps1`：验证两个真实客户端的同步状态。

## 共享场景状态

`src/runtime/` 下的实现片段随主运行时编译，共用一份场景状态。新增动作在 `PassiveRules.cpp` 校验字段，在 `PassiveActions.inc` 实现执行。新增持久状态须同时检查 `Starfall.cpp` 中的场景重置和 `Savegame.inc` 中的历史版本读取。每帧子系统的执行顺序集中在 `SF_Frame`。
