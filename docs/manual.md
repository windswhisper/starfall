# Starfall Lab / 流火实验室 INI 规则说明书

## 项目简介

流火实验室（Starfall Lab，简称流火）是《红色警戒 2：尤里的复仇》的 DLL 扩展。模组作者可以通过 INI 配置单位被动能力和战场中的三选一科研升级。

## 使用指南

适用版本：0.35。单位能力从「单位自带被动」开始查阅，科研卡片从「科研项目参数」开始查阅。

科研卡没有出现或被动未生效时，查看游戏根目录的 `Starfall.log`。科研配置错误会列出段名、字段、读取到的值和原因；可定位到独立 `Starfall.ini` 的字段还会标出行号。来自 `rulesmd.ini`、地图或包含文件的字段按游戏合并后的规则检查。

## 一、被动能力

### 1. 单位自带被动

#### 简要介绍

##### 三个 INI 段怎样配合

给单位配置自带被动，需要用同一个被动 ID 串起三个 INI 段。以 `SF_KillHeal` 为例：

1. **注册被动**　在 `[Starfall.PassiveTypes]` 中用一个空闲数字键登记 `SF_KillHeal`。数字是注册表的位置，后面引用技能时填写 `SF_KillHeal`。
2. **定义规则**　新建同名段 `[SF_KillHeal]`，写明什么时候判断、哪些情况算数、满足后做什么。
3. **绑定单位**　在兵种段 `[MTNK]` 中填写 `Starfall.Passives=SF_KillHeal`。这样各方的灰熊都自带该技能。已有其他被动时，用英文逗号把新 ID 追加到原列表。

#### 参数列表和对应说明

##### 一个被动由哪些部分组成

普通事件可从 `Trigger` 和一项带所需参数的 `Action` 写起，再按玩法添加条件、次数和表现。

| 组成 | 常用字段 | 什么时候填写 |
|---|---|---|
| 触发方式 | `Trigger` | 单选：`OnKill`、`OnBeforeDealDamage`、`OnDealDamage`、`OnTakeDamage`、`OnBeforeTakeDamage`、`OnDeath`、`OnFire`、`OnBeforeFire`、`OnDeploy`、`OnPromote`、`OnPromoteVeteran`、`OnPromoteElite`、`OnPassiveGained`、`OnPassengerConsumed`、`OnSignal`、`OnInterval`、`WhileState`。定时触发还需填写 `Interval`。 |
| 事件对方 | `Filter.TargetHouse` / `Filter.TargetKind` | 阵营可选 `All`、`Self`、`Allies`、`Enemies`、`Neutral`、`None`；类别可选 `All`、`Infantry`、`Vehicle`、`Aircraft`、`Building`、`Other`、`None`。每个字段可用英文逗号多选。 |
| 公式条件 | `Condition.Formula` | 可用比较表达式判断层数、生命、弹药、乘客及其他公式变量。 |
| 次数、概率和冷却 | `Condition.Count`、`Chance`、`Cooldown`、`MaxTriggers` | 分别控制合格事件数、成功概率、冷却帧数和成功触发次数上限。 |
| 事件结果 | `Action` / `Actions` | `Action` 单选：`Heal`、`Damage`、`SetDamage`、`BlockDamage`、`GiveCredits`、`GiveResearchPoints`、`ChangeAmmo`、`ChangeMaxHealth`、`ChangeSpeed`、`ChangeFirepower`、`ChangeArmor`、`ChangeFireRate`、`ChangeWeaponRange`、`ChangeProjectileScale`、`ChangeScale`、`SpawnCrate`、`SpawnUnit`、`ChangeOwner`、`Transform`、`SelfDestruct`、`ConsumePassenger`、`SendSignal`、`GrantPassive`、`StealPassive`、`RemovePassive`、`ChangePassiveStack`、`FireWeapon`、`DetonateWarhead`、`ReplaceWeapon`。`SetDamage`、`BlockDamage` 可用于 `OnBeforeDealDamage` 和 `OnBeforeTakeDamage`，`ReplaceWeapon` 仅用于 `OnBeforeFire`。组合多个结果时，用 `Actions` 引用结果段 ID。 |
| 被动层数 | `Stack.Max` | 可选，填正整数限制层数上限。每个单位的每项被动从 0 层开始。用 `ChangePassiveStack` 增减手动层数，也可由开启叠加的乘客提供层数；公式用 `This.Stack` 读取总层数。 |
| 动画表现 | `Anim` / `Anim.Always` / `TriggerAnim` / `TriggerAnim.Target` | 动画名取自游戏已注册的动画类型。`Anim.Always` 控制附着动画是否常驻；`TriggerAnim.Target` 单选 `Self` 或 `Target`，默认 `Self`。 |

#### 示例代码

##### 完整示例：击杀回血

`Filter.TargetHouse` 筛选被击杀者，`Action.Target=Self` 把治疗给持有者。每辆灰熊分别累计击杀次数。

```ini
[Starfall.PassiveTypes]
0=SF_KillHeal

[SF_KillHeal]
Trigger=OnKill
Filter.TargetHouse=Enemies,Neutral
Condition.Count=3
Action=Heal
Action.Target=Self
Action.Amount=50

[MTNK]
Starfall.Passives=SF_KillHeal
```

##### 持续状态的写法

例如，让灰熊在半血及以下获得 25% 火力加成，可以另建下面的被动，注册后追加到 `[MTNK]` 的 `Starfall.Passives`。`WhileState` 至少需要一个状态条件；持续属性写在 `Modifier.*` 中。生命恢复到 50% 以上时，这份加成立即随状态解除。

```ini
[SF_LastStand]
Trigger=WhileState
Condition.Formula=$(Self.Health <= Self.MaxHealth * 50%)
Modifier.FirepowerMultiplier=1.25
```

#### 额外补充说明

注册只让流火识别技能。写在单位类型上的 `Starfall.Passives` 是**各方该兵种的自带能力**，支持多个 ID，例如 `SF_KillHeal,SF_PoweredDrive`。若能力只应在科研后获得，就注册并定义它，通过第二部分的 `GrantPassive` 授予。一个单位从多个来源获得同一个被动 ID，只执行一份。

### 2. 触发时机总览

#### 简要介绍

流火支持 12 种事件触发时机和 1 种持续状态判定。点击表中的触发器名称可查看对应示例。普通事件可以按目标筛选，并设置计数、概率和冷却。

#### 参数列表和对应说明

| `Trigger` | 判定时机 | 筛选中的“对方” |
|---|---|---|
| [`OnKill`](#timing-onkill) | 持有者获得一次击杀 | 被击杀者 |
| [`OnBeforeDealDamage`](#timing-onbeforedealdamage) | 攻击者即将使一个目标损失生命时同步判定 | 本次实际受击单位 |
| [`OnDealDamage`](#timing-ondealdamage) | 持有者使目标实际损失生命 | 受害者 |
| [`OnTakeDamage`](#timing-ontakedamage) | 持有者实际损失生命 | 攻击者 |
| [`OnBeforeTakeDamage`](#timing-onbeforetakedamage) | 正数伤害即将扣除生命前同步判定 | 攻击者，可能为空 |
| [`OnDeath`](#timing-ondeath) | 持有者被登记为摧毁；一次死亡结算一次 | 击杀者（可能为空） |
| [`OnFire`](#timing-onfire) | 标准武器一次实际发射，不要求命中；连发按实际发射次数计，失败尝试不计 | 射击目标；点地时可用落点 |
| [`OnDeploy`](#timing-ondeploy) | 步兵或简易部署车辆由未部署转为已部署；车辆成功展开为建筑；部署开火车辆完成部署射击 | 无对方，默认作用于自身，展开为建筑时作用于新建筑 |
| [`OnPromote`](#timing-onpromote) | 观察到持有者等级上升；新造即精英、读档保持原等级均不计。一次观察中跨两级只算一次 | 无对方，默认作用于自身 |
| [`OnPromoteVeteran`](#timing-onpromote) | 观察到持有者晋升为老兵 | 无对方，默认作用于自身 |
| [`OnPromoteElite`](#timing-onpromote) | 观察到持有者晋升为精英 | 无对方，默认作用于自身 |
| [`OnPassiveGained`](#timing-onpassivegained) | 首次在地图上拥有本被动时触发。自带、科研及临时授予均可触发，科研覆盖现役和之后登场的单位。持续持有期间，续期、来源重叠、进出运输车及读档不会重复触发。完全失去后重新获得可再触发。血量等条件在获得时检查。 | 无对方，默认作用于自身 |
| `OnSignal` | 收到指定自定义信号 | 信号附带目标，发送者可单独筛选 |
| [`OnPassengerConsumed`](#timing-onpassengerconsumed) | 流火成功消耗一名乘客后，持有该乘客的运输单位触发 | 被消耗的乘客 |
| [`OnBeforeFire`](#6-开火前替换武器) | 每轮标准齐射前同步选择本轮武器 | 射击目标；点地时没有单位对方 |
| [`OnInterval`](#5-定时触发) | 每经过 Interval 个游戏帧判定一次 | 当前射击目标，可能为空 |
| [`WhileState`](#3-持续的状态条件) | 每个游戏帧检查持有者状态 | 检查持有者、所属玩家及配置的附近单位条件 |

#### 各触发时机的目标

`Filter.TargetKind` 用于筛选事件对方，可用英文逗号多选。`Action.Target` 指定结果作用于谁，每个动作单选，造成伤害前和 `StealPassive` 默认 `Target`，其余默认 `Self`。例如受到载具攻击时要给自己回血，填 `Filter.TargetKind=Vehicle` 和 `Action.Target=Self`。

| 触发时机 | 可匹配的 `Filter.TargetKind` | `Action.Target` 可选值与指向 |
| --- | --- | --- |
| `OnKill` | `All`、`Infantry`、`Vehicle`、`Aircraft`、`Building`、`Other` | `Self`、`Attacker` 指击杀者。`Target`、`Victim` 指被击杀者。`VictimLocation` 为死者位置。 |
| `OnBeforeDealDamage` | `All`、`Infantry`、`Vehicle`、`Aircraft`、`Building`、`Other` | `Target`（默认）或 `Victim`，均指本次实际受击单位。 |
| `OnDealDamage` | `All`、`Infantry`、`Vehicle`、`Aircraft`、`Building`、`Other` | `Self`、`Attacker` 指造成伤害的持有者。`Target`、`Victim` 指受害者。`VictimLocation` 为受害者受击时的位置。 |
| `OnTakeDamage` | `All`、`Infantry`、`Vehicle`、`Aircraft`、`Building`、`Other`、`None` | `Self`、`Victim` 指受伤的持有者。`Target`、`Attacker` 指攻击者，可能为空。`VictimLocation` 为持有者受击时的位置。 |
| `OnBeforeTakeDamage` | `All`、`Infantry`、`Vehicle`、`Aircraft`、`Building`、`Other`、`None` | 结果仅可作用于 `Self`；筛选和公式中的 `Target` 为攻击者，可能为空。 |
| `OnDeath` | `All`、`Infantry`、`Vehicle`、`Aircraft`、`Building`、`Other`、`None` | `Self`、`Victim` 指已死亡的持有者。`Target`、`Attacker` 指击杀者，可能为空。`VictimLocation` 为死亡位置。给死者原所属方发钱或科研点用 `Self`。 |
| `OnFire` | `All`、`Infantry`、`Vehicle`、`Aircraft`、`Building`、`Other`、`None` | `Self`、`Attacker` 指开火者。`Target`、`Victim` 指射击目标单位。`VictimLocation` 为发射时记录的目标位置，点地时为瞄准的地面格。 |
| `OnDeploy` | `All`、`None` | `Self`、`Target`、`Attacker`、`Victim` 均指部署后的单位。载具展开为建筑时均指新建筑。`VictimLocation` 为部署时记录的位置。 |
| `OnPromote` | `All`、`None` | `Self`、`Target`、`Attacker`、`Victim` 均指持有者。`VictimLocation` 为事件发生时持有者的位置。 |
| `OnPromoteVeteran`、`OnPromoteElite` | `All`、`None` | 与 `OnPromote` 相同。 |
| `OnPassiveGained` | `All`、`None` | `Self`、`Target`、`Attacker`、`Victim` 均指持有者。`VictimLocation` 为事件发生时持有者的位置。 |
| `OnPassengerConsumed` | `All`、`Infantry`、`Vehicle`、`Aircraft`、`Other` | `Self` 指运输单位；公式中的 `Target` 是被消耗乘客的信息快照。`StealPassive` 可从记录中夺取该乘客的被动。 |
| `OnSignal` | `All`、`Infantry`、`Vehicle`、`Aircraft`、`Building`、`Other`、`None` | `Self` 为接收者，`Target` 为附带目标，`Sender` 为发送者；发送者可单独筛选。 |
| `OnBeforeFire` | `All`、`Infantry`、`Vehicle`、`Aircraft`、`Building`、`Other`、`None` | 仅 `Self`。`ReplaceWeapon` 替换持有者本轮的武器；筛选条件检查此次瞄准的对象。 |
| `OnInterval` | `All`、`Infantry`、`Vehicle`、`Aircraft`、`Building`、`Other`、`None` | `Self`、`Attacker` 指持有者。`Target`、`Victim` 指到点时瞄准的单位。`VictimLocation` 为当时瞄准的单位或地面格的位置。没有目标时，只有自身可用。 |
| `WhileState` | 使用 `Condition.Nearby.Kinds` 筛选附近单位。 | 不填写 `Action.Target`。持续修正作用于持有者；`Modifier.SpawnPassives` 授予其直属子机。光环按所属方和类型授予范围内的单位。 |

只有 `FireWeapon`、`DetonateWarhead`、`SpawnUnit`、`SpawnCrate` 支持 `VictimLocation`。这四项填 `Victim` 时优先使用目标单位；单位已消失或目标为地面格时，使用事件记录的位置。治疗、变形等需要存活单位的动作，在目标死亡或缺失时跳过。`SelfDestruct` 只作用于自身。

**事件型被动的筛选与频率**

| 字段 | 默认 | 规则 |
|---|---|---|
| `Filter.TargetHouse` | `All` | 多选，英文逗号分隔：`All`、`Self`、`Allies`、`Enemies`、`Neutral`、`None`。`Self` 为同一玩家，`Allies` 为其他盟友，`None` 表示无可判定所属方。 |
| `Filter.TargetKind` | `All` | 多选，英文逗号分隔：`All`、`Infantry`、`Vehicle`、`Aircraft`、`Building`、`Other`、`None`。`Other` 为其他单位类别，`None` 表示没有对方单位。 |
| `Condition.MinDamage` | `0` | 本次伤害至少达到该整数才计数。受伤前检查当前待扣生命值；受伤后检查实际损失生命值。 |
| `Condition.Count` | `1` | 正整数。每个单位的每项技能分别累计 |
| `Chance` | `100%` | 每轮达标后的触发概率，整数 0～100，可写百分号 |
| `Cooldown` | `0` | 成功执行后进入冷却，单位为**游戏帧**；冷却中暂停计数 |
| `MaxTriggers` | `0` | 单位的这项被动最多成功触发几次，0 表示不限。 |

#### 示例代码

```ini
[SF_ThirdKillRepair]
Trigger=OnKill
Filter.TargetHouse=Enemies
Condition.Count=3
Action=Heal
Action.Target=Self
Action.Amount=50
```

#### 额外补充说明

`OnDealDamage` 和 `OnTakeDamage` 按**实际损失的生命**结算；扣血前先结算攻击者的 `OnBeforeDealDamage`，再结算受击者的 `OnBeforeTakeDamage`。一次范围攻击伤到三个单位，各单位分别判定。击杀事件记录的伤害量为 0，因此击杀回血应使用固定值。普通事件型能力记录事件发生时的血量、电力和隐形状态，随后结算；除死亡事件外，结算时持有者须存活。死亡事件保留死亡位置和所属方，因此可以在自身原位置引爆弹头，或给原所属玩家发钱；死亡后对自己回血不能复活。部署、晋升建议保持默认的阵营与类别筛选 `All`。

不同条件须同时满足；一个筛选字段列多个值时，满足任一值即可。先筛选、检查冷却，再累计次数。到达门槛就清零并判定概率。概率失败，或满血导致实际治疗量为 0，都不开始冷却。比如 `Count=3`（完整键名为 `Condition.Count`）配 `Chance=30%`，就是每三次有效事件获得一次 30% 的机会。

### 3. 持续的状态条件

#### 简要介绍

设置 `Trigger=WhileState` 后，单位在场时每帧检查状态。条件成立并达到等待时间后应用持续结果，条件中断时撤销。

用状态与数量条件判断单位当前是否符合要求。多项条件同时满足时，被动才能生效。

检测附近单位时，按指定范围、阵营和单位类型统计数量，再判断数量是否达到门槛。

#### 参数列表和对应说明

| 字段 | 取值 | 判断内容 |
|---|---|---|
| `Condition.OwnerPower` | `Sufficient` / `Low` | 所属玩家基地是否电力充足 |
| `Condition.SelfMoving` | `yes` / `no` | 按单位移动控制器的实际运动状态判断；驻停写 `no`，建筑按驻停处理。可用于事件和持续被动 |
| `Condition.SelfPowered` | `Yes` / `No` | 对象自身是否在线供能；停用或受 EMP 时为否 |
| `Condition.SelfCloak` | `Uncloaked` / `Cloaking` / `Cloaked` / `Uncloaking` | 完全显形、正在隐形、完全隐形、正在显形。取单位自己的状态，与敌方是否探测到它分开判断 |
| `Condition.Formula` | 如 `$(This.Stack >= 5)` | 表达式非零时满足；支持持续状态和事件条件。 |


##### WhileState 的等待时间与部署姿态

| 字段 | 用法 |
|---|---|
| `Condition.SelfDeployed` | 只用于 `WhileState`。`yes` 在游戏判定持有者已部署时满足，收起后撤销加成。`no` 在可部署步兵或简易部署车辆处于未部署姿态时满足。普通驻停、展开后生成的建筑、没有部署姿态的单位都不会获得这项已部署加成。 |
| `Condition.StateDurationSeconds` | 可选，默认 `0`，立即生效。1 游戏秒 = 15 帧，支持最多三位小数，不足一帧向上取整；暂停不走时 |

##### 检测附近单位

| 字段（均以 `Condition.Nearby.` 开头） | 取值与默认 |
|---|---|
| `Range` | 使用范围条件时必填 |
| `Houses` | 多选，英文逗号分隔：`All`、`Self`、`Allies`、`Enemies`、`Neutral`。默认 `All`；`Self` 指同一玩家的其他单位。 |
| `Kinds` | 多选，英文逗号分隔：`All`、`Infantry`、`Vehicle`、`Aircraft`、`Building`。默认 `All`。 |
| `CountAtLeast` / `CountAtMost` | 含等号。均省略时要求至少 1 个；只写上限时下限为 0。判断孤立可写 `CountAtMost=0` |
| `ScanInterval` | 默认 10 个游戏帧，必须为正整数。各持有者错帧扫描，中间复用结果，首次采样前视为未满足；缓存和下次扫描时间随存档保存 |

#### 示例代码

基地供电充足且单位完全隐形满 3 游戏秒时，移速提高 20%。任一条件中断，等待计时重置，已生效的加成撤销。

```ini
[SF_CloakedDrive]
Trigger=WhileState
Condition.OwnerPower=Sufficient
Condition.SelfCloak=Cloaked
Condition.StateDurationSeconds=3
Modifier.SpeedMultiplier=1.20
```

**部署姿态示例。** 把下面的被动绑定到美国大兵（`E1`）、重装大兵（`GGI`）或辐射工兵（`DESO`），部署期间射速提高 25%。如需在架设后再等待 2 个游戏秒，加上 `Condition.StateDurationSeconds=2`；中途收起会重新计时。需要在部署瞬间执行一次动作时使用 `OnDeploy`。

```ini
[SF_EntrenchedFire]
Trigger=WhileState
Condition.SelfDeployed=yes
Modifier.FireRateMultiplier=1.25
```

```ini
[SF_EscortArmor]
Trigger=WhileState
Condition.Nearby.Range=4
Condition.Nearby.Houses=Self
Condition.Nearby.Kinds=Vehicle
Condition.Nearby.CountAtLeast=2
Modifier.ArmorMultiplier=1.20
```

#### 额外补充说明

`Condition.Formula` 使用 `$(表达式)`。表达式结果非零时满足条件，0 不满足；比较运算返回 1 或 0，可用乘法组合多个必须同时满足的比较。`Self.Ammo` 是当前弹药（无限弹药为 -1），`Self.Passengers` 是当前乘客人数，空载为 0。持续状态每帧读取当前数据；事件型被动读取事件发生时的快照。条件中缺少目标、除零或结果无效时视为不满足，持续状态的等待计时会重置。

```ini
[SF_FiveStacks]
Trigger=WhileState
Stack.Max=10
Condition.Formula=$(This.Stack >= 5)
Modifier.FirepowerMultiplier=1.25

[SF_LoadedCarrier]
Trigger=WhileState
Condition.Formula=$((Self.Ammo >= 1) * (Self.Passengers >= 2))
Modifier.SpeedMultiplier=1.20
```

旧的 `Condition.SelfHealthAtLeast/AtMost`、`Condition.AmmoAtLeast/AtMost`、`Condition.PassengersAtLeast/AtMost` 已移除，继续使用将停用该被动并记录到 `Starfall.log`。例如半血及以下改为 `Condition.Formula=$(Self.Health <= Self.MaxHealth * 50%)`；弹药为 1～3 改为 `Condition.Formula=$((Self.Ammo >= 1) * (Self.Ammo <= 3))`。

**单位不在地图时。** 当游戏将进入建筑或载具的单位标记为不在地图上或处于 `InLimbo` 时，流火暂停该单位的 `WhileState` 条件计算并撤销对应的持续修正。单位返回地图后，若被动仍有效且条件满足，持续修正会重新生效；配置的状态等待时间需要重新计算。`OnInterval` 在此期间不触发，返回后从头计算周期。之前通过事件累计获得的属性变化独立保存，不会仅因临时被动到期而自动撤销。

`WhileState` 段支持 `Trigger`、上述状态条件、等待秒数、持续修正和动画字段。混入 `Action.*`、`Action`、`Filter.*`、`Chance`、`Cooldown` 或 `Condition.Count` 等不兼容字段时，整项技能不加载。可在游戏根目录的 `Starfall.log` 查看冲突字段。

用 `Condition.Nearby.Range` 指定 1～32 格的整数半径，按地图格的平面圆形范围统计。持有者自己不计，跨格建筑和重复枚举的对象只计一次。非友方隐形单位须在本方探测覆盖内，伪装单位须能被本方识破。

大规模对局可缩小检测半径或延长扫描间隔，以减少扫描次数和范围。

### 4. 事件触发时机详解

<a id="timing-onkill"></a>

#### 击杀时

##### 简要介绍

当游戏把一次击杀归给被动持有者时，持有者判定一次。阵营和单位类型筛选针对被击杀的单位。

##### 参数列表和对应说明

| 参数 | 说明 |
|---|---|
| `Trigger=OnKill` | 持有者消灭一个目标时判定，事件对方是被击杀者。 |
| `Filter.TargetHouse=Enemies` | 按事件对方的所属方筛选。 |
| `Action=GiveCredits` | 给动作目标的所属方增加资金。 |
| `Action.Amount=75` | 本次奖励或治疗的固定点数。 |

##### 示例代码

每击杀一个敌人，给持有者所属方增加 75 资金。

```ini
[SF_KillBounty]
Trigger=OnKill
Filter.TargetHouse=Enemies
Action=GiveCredits
Action.Amount=75
```

##### 额外补充说明

击杀事件中的伤害值为 0。计算击杀奖励时可填固定金额，按伤害比例吸血应使用 `OnDealDamage`。

<a id="timing-onbeforedealdamage"></a>

#### 造成伤害前

持有者对单位造成正数伤害时，在装甲和弹头计算后、扣血前判定。范围攻击对每个受击单位分别判定。先处理攻击方被动，再将剩余伤害交给受击方的 `OnBeforeTakeDamage`。伤害归零后停止。

| 参数 | 说明 |
| --- | --- |
| `Self`、`Target` | 公式中的 `Self` 是攻击者，`Target` 是本次实际受击单位。双方血量取扣血前的值。 |
| `Action` | `SetDamage` 用 `Action.Amount` 设置本次伤害，小数向下取整。`BlockDamage` 将其归零。两种动作均可用于 `Actions`。 |
| `Event.Damage` | 当前待扣生命值，后续动作会读到前一步修改后的值。 |

给攻击者绑定以下被动后，对低于半血的敌人造成 150% 伤害。目标恰好半血时按原伤害结算。

```ini
[SF_Finisher]
Trigger=OnBeforeDealDamage
Filter.TargetHouse=Enemies
Condition.Formula=$(Target.Health < Target.MaxHealth * 50%)
Action=SetDamage
Action.Amount=$(Event.Damage * 150%)
```

<a id="timing-ondealdamage"></a>

#### 造成伤害时

##### 简要介绍

当被动持有者造成的伤害使目标实际损失生命值时，持有者判定一次。一次范围攻击伤到多个单位，会按每个受害者分别判定。

##### 参数列表和对应说明

| 参数 | 说明 |
|---|---|
| `Trigger=OnDealDamage` | 持有者使一个目标实际损失生命时判定，事件对方是受害者。 |
| `Condition.MinDamage` | 可选。单次实际损失的生命值达到此门槛才计入，默认 0。 |

##### 示例代码

每次伤到敌人，按实际损血的 25% 治疗自身。例如造成 40 点实际伤害可恢复 10 点。

```ini
[SF_LifeDrain]
Trigger=OnDealDamage
Filter.TargetHouse=Enemies
Action=Heal
Action.Target=Self
Action.PercentOfDamage=25%
```

##### 额外补充说明

范围弹头伤到多个单位时按受害者分别结算。护盾吸收全部伤害时不会触发。

<a id="timing-onbeforetakedamage"></a>

#### 受到伤害前

`Trigger=OnBeforeTakeDamage` 在本次正数伤害经过装甲和弹头伤害计算后、扣除生命前同步执行。`Event.Damage` 是此时的待扣生命值，可能高于当前生命；`Self` 是受击者，`Target` 是攻击者。支持常规筛选、`Condition.Count`、`Condition.MinDamage`、`Chance`、`Cooldown`、`MaxTriggers` 和触发动画。

```ini
[Starfall.PassiveTypes]
0=SF_BlockOnce
1=SF_HalfDamage

; 每个单位首次受到有效伤害时抵挡这次伤害。
[SF_BlockOnce]
Trigger=OnBeforeTakeDamage
MaxTriggers=1
Action=BlockDamage

; 只受敌人造成的伤害，伤害按 50% 结算。
[SF_HalfDamage]
Trigger=OnBeforeTakeDamage
Filter.TargetHouse=Enemies
Action=SetDamage
Action.Amount=$(Event.Damage * 50%)
```

`BlockDamage` 将本次待扣生命值设为 0；`SetDamage` 将其设为 `Action.Amount` 指定的非负整数或公式结果，小数向下取整，结果为 0 同样完全抵挡。超出整数范围、负数、除零或缺少公式需要的攻击者时，本动作跳过。此触发时机只支持这两个动作，也可用 `Actions` 列表组合；它们只作用于自身。列表和多个被动按处理顺序执行，后续公式读取前一步修改后的 `Event.Damage`，伤害为 0 后不再执行后续受伤前被动。

每次对单位的伤害结算单独判定，同一帧的连发也逐次判定。原始输入为零或治疗、已被原生免疫或第三方护盾完全吸收的伤害不消耗次数；原游戏或其他扩展可能把零伤害转换为最少 1 点，这种原生扣血仍可能发生。强制伤害按游戏的绕过防御规则处理，不触发本能力。完全抵挡后没有实际损血，不触发 `OnTakeDamage` 或 `OnDealDamage`；弹头的其他效果由游戏和相关扩展处理。

`MaxTriggers` 是同一单位、同一被动的累计成功上限，随存档保存；暂时移除后重新授予同名被动不会重置。若要周期性抵挡，可以不设置上限，改用 `Cooldown`。有效的 `SetDamage` 即使算出与原伤害相同，也算一次成功；无效公式不计成功次数。

<a id="timing-ontakedamage"></a>

#### 受到伤害时

##### 简要介绍

当被动持有者受到伤害并实际损失生命值时，从受击方判定。事件记录本次损失的生命值；若有攻击者，攻击者就是事件对方。

##### 参数列表和对应说明

| 参数 | 说明 |
|---|---|
| `Trigger=OnTakeDamage` | 持有者实际损失生命时判定，事件对方是攻击者。 |
| `Condition.MinDamage` | 可选。单次实际损失的生命值达到此门槛才计入，默认 0。 |

##### 示例代码

一次实际损血达到 20 点，就给受击者所属方增加 2 科研点。

```ini
[SF_DamageStudy]
Trigger=OnTakeDamage
Condition.MinDamage=20
Action=GiveResearchPoints
Action.Amount=2
```

##### 额外补充说明

门槛按单次损血判断，两次各损失 10 点不能合并达到门槛。

<a id="timing-ondeath"></a>

#### 死亡时

##### 简要介绍

当被动持有者被摧毁时，针对这次死亡判定一次。事件保留持有者死亡时的位置和所属方；若有击杀者，击杀者就是事件对方。

##### 参数列表和对应说明

| 参数 | 说明 |
|---|---|
| `Trigger=OnDeath` | 持有者被摧毁时判定一次，事件对方是击杀者。 |

##### 示例代码

单位被摧毁时，给它死亡时的所属方返还 100 资金。

```ini
[SF_LossRefund]
Trigger=OnDeath
Action=GiveCredits
Action.Target=Self
Action.Amount=100
```

##### 额外补充说明

击杀者可能为空。对已经死亡的持有者执行治疗不能复活它。

<a id="timing-onfire"></a>

#### 实际开火时

##### 简要介绍

当被动持有者的标准武器成功发射时判定，无需等待命中。同一轮连发中，每次实际发射都单独判定。

##### 参数列表和对应说明

| 参数 | 说明 |
|---|---|
| `Trigger=OnFire` | 持有者的标准武器成功发射时判定，事件对方是射击目标。 |

##### 示例代码

每实际发射四次，给本方增加 1 科研点。

```ini
[SF_FiringDrill]
Trigger=OnFire
Condition.Count=4
Action=GiveResearchPoints
Action.Amount=1
```

##### 额外补充说明

按实际发射次数计数，连发中的各发分别计数。无需命中；未能发射的尝试不计。

<a id="timing-ondeploy"></a>

#### 部署时

##### 简要介绍

当被动持有者完成部署时判定，包括步兵架设、简易部署车辆进入部署姿态、车辆展开为建筑，以及部署开火车辆完成部署射击。

##### 参数列表和对应说明

| 参数 | 说明 |
|---|---|
| `Trigger=OnDeploy` | 步兵架设、简易部署车辆进入部署姿态、车辆展开为建筑或完成部署射击时判定。 |

##### 示例代码

部署成功时为自身恢复 30 点生命。车辆展开为建筑时，治疗作用于新建筑。

```ini
[SF_DeployRepair]
Trigger=OnDeploy
Action=Heal
Action.Target=Self
Action.Amount=30
```

##### 额外补充说明

收起不触发。若希望在整个部署期间持续增益，使用 `WhileState` 和 `Condition.SelfDeployed=yes`。

<a id="timing-onpromote"></a>

#### 晋升时

##### 简要介绍

当游戏观察到被动持有者的等级上升时判定。单位登场或读档时已经拥有的等级作为起点。

##### 参数列表和对应说明

| 参数 | 说明 |
|---|---|
| `Trigger=OnPromote` | 持有者的等级上升时判定。 |
| `Trigger=OnPromoteVeteran` | 持有者晋升为老兵时判定。 |
| `Trigger=OnPromoteElite` | 持有者晋升为精英时判定。 |

##### 示例代码

每次观察到晋升，给本方增加 200 资金；晋升精英时额外恢复 100 点生命。

```ini
[SF_PromotionBonus]
Trigger=OnPromote
Action=GiveCredits
Action.Amount=200

[SF_EliteBonus]
Trigger=OnPromoteElite
Action=Heal
Action.Amount=100
```

##### 额外补充说明

新造时已有的等级、读档时恢复的等级都不计。一次观察中从新兵直接升为精英时，`OnPromote` 和 `OnPromoteElite` 各判定一次，`OnPromoteVeteran` 不判定。若单位同时持有通用和对应等级的晋升被动，两者都会分别判定。

<a id="timing-onpassivegained"></a>

#### 获得被动时

##### 简要介绍

当单位在地图上首次持有这项被动时判定。自带、科研和临时授予均可触发；完全失去该被动后重新获得，可再次判定。

##### 参数列表和对应说明

| 参数 | 说明 |
|---|---|
| `Trigger=OnPassiveGained` | 单位首次在地图上持有这项被动时判定。单位自带、科研授予和临时授予均可触发。 |

##### 示例代码

单位获得这项被动时，给本方增加 3 科研点。

```ini
[SF_EntryBonus]
Trigger=OnPassiveGained
Action=GiveResearchPoints
Action.Amount=3
```

##### 额外补充说明

同名来源重叠、临时授予续期或读档不会重复发放。完全失去该被动后重新获得，可再次触发。

### 自定义信号

`Action=SendSignal` 向持有 `Trigger=OnSignal` 的单位发送自定义信号。信号 ID 不需要注册，英文名称不区分大小写。

| 参数 | 说明 |
|---|---|
| `Action.Signal` | 发送的信号 ID，必填。 |
| `Action.Target` | 接收范围的中心单位，默认 `Self`；`Target`、`Attacker`、`Victim` 均可用于事件对应的单位。 |
| `Action.Signal.Range` | `0` 只发送给中心单位；正数为范围半径（格），最大 128；`-1` 为全图。默认 `0`。 |
| `Action.Signal.Houses` | 相对发送者的 `Self`、`Allies`、`Enemies`、`Neutral`、`All`，默认 `Self`，可多选。 |
| `Action.Signal.IncludeSelf` | 范围或全图广播是否包含发送者，默认 `no`；直接发送给自身不受此项影响。 |
| `Action.Signal.Target` | 附带的单位，填 `Self`、`Target`、`Attacker`、`Victim`，在信号接收事件中还可填 `Sender`；省略表示没有附带目标。 |
| `Action.Signal.Value` | 携带的数值，可填数值或公式，默认 `0`；在发送时计算并保存。 |
| `Trigger=OnSignal`、`Signal` | 收到指定 `Signal` 时判定，支持普通次数、概率和冷却。 |
| `Filter.SenderHouse`、`Filter.SenderKind` | 筛选发送者的所属方与单位类别，默认 `All`；所属方相对接收者。`Filter.TargetHouse` 和 `Filter.TargetKind` 筛选附带目标。 |

```ini
[Starfall.PassiveTypes]
100=SF_FireSignal
101=SF_ReceiveFocus

[SF_FireSignal]
Trigger=OnFire
Action=SendSignal
Action.Signal=FocusFire
Action.Target=Self
Action.Signal.Range=5
Action.Signal.Houses=Self
Action.Signal.Target=Target
Action.Signal.Value=10

[SF_ReceiveFocus]
Trigger=OnSignal
Signal=FocusFire
Filter.SenderHouse=Self
Filter.TargetHouse=Enemies
Condition.Formula=$(Target.Health < Target.MaxHealth * 50%)
Action=FireWeapon
Action.Target=Target
Action.Weapon=SupportWeapon
```

`SupportWeapon` 应换成已注册的武器 ID。接收时 `Self` 是接收者，`Target` 是附带目标，`Sender` 是发送者；公式支持 `Target.Health`、`Target.MaxHealth`、`Target.Cost`、`Sender.Health`、`Sender.MaxHealth`、`Sender.Cost`、`Event.SignalValue`。`Event.Distance` 为接收者到附带目标的距离。`Action.Target=Sender` 可将结果作用于仍存活的发送者。

发送时固定接收者、附带目标和数值等快照，排队于后续帧结算。死亡或失去接收被动的接收者跳过；附带目标或发送者已失效时仍可读取快照，但需要存活对象的结果跳过。只向地图上存活单位发送，车内乘客和驻军不作为独立接收者。有效投递至少一份即算发送成功；最终动作是否生效由接收方判定。单条连锁最多 16 层、1024 次投递，超限停止并记录日志；待处理信号和预算随存档保存。

<a id="timing-onpassengerconsumed"></a>

#### 消耗乘客时

流火的 `Action=ConsumePassenger` 成功消耗乘客后，运输单位持有的 `Trigger=OnPassengerConsumed` 被动触发。`Self` 是运输单位，`Target` 是被消耗乘客：公式可以读取乘客消耗前的 `Target.Health`、`Target.MaxHealth` 和基础 `Target.Cost`，筛选条件也使用乘客的类别和所属方。`Self.Passengers` 是消耗后的剩余数量。

```ini
[Starfall.PassiveTypes]
0=SF_EatPassenger
1=SF_DigestPassenger

[SF_EatPassenger]
Trigger=OnInterval
Interval=150
Action=ConsumePassenger

[SF_DigestPassenger]
Trigger=OnPassengerConsumed
Filter.TargetKind=Infantry
Action=Heal
Action.Target=Self
Action.Amount=$(Target.Cost * 50%)

[YOUR_TRANSPORT]
Starfall.Passives=SF_EatPassenger,SF_DigestPassenger
```

流火在消耗前记录直属乘客实际持有的被动及层数。将上例的消化被动替换为下例，就会从列表中的每项被动最多夺取 2 层。0 层被动也可整项获得。

```ini
[SF_DigestPassenger]
Trigger=OnPassengerConsumed
Action=StealPassive
Action.Target=Target
Action.Passive=SF_Rage,SF_Armor
Action.Amount=2
```

也可在同一组 `Actions` 中先消耗、再夺取。后续动作和运输单位的消耗事件共用这名乘客的剩余层数。每次夺取只扣除实际转移的部分；每名乘客单独记录。事件结算完毕后，未夺取的部分随记录释放。

`ConsumePassenger` 每次从目标的乘客列表中移除首个乘客，通常是最后进入的一名。如果首位乘客也是载具，会从内向外消耗它和全部嵌套乘客，外层运输单位只触发一次 `OnPassengerConsumed`。`Target.Health`、`Target.MaxHealth` 和 `Target.Cost` 只取这名直属乘客消耗前的值。正常装载时，被寄生的单位无法登车。若运输单位空载，或现有乘客列表异常（如强制装载了被寄生的乘客），本次动作失败；同一 `Actions` 列表中它后面的动作不再执行，之前执行的动作不回滚。成功后，同一列表里后续公式的 `Target` 快照也改为刚消耗的乘客；其他被动仍读取原事件的对方。乘客已经移除，后续要求存活目标的 `Action.Target=Target` 不会作用于原事件对方。这个结果处理运输单位的乘客列表，不包括驻扎建筑的独立驻军列表。

乘客消耗事件在移除后排队结算，信息和待处理事件随存档保存。它只由流火的 `ConsumePassenger` 产生，不覆盖其他扩展或游戏原生的乘客消耗逻辑。流火不会因此主动产生乘客死亡或击杀被动事件。

### 5. 定时触发

#### 简要介绍

持有被动的单位在地图上每经过一个指定周期判定一次。周期按游戏帧计算，单位静止时也会计时。

#### 参数列表和对应说明

| 参数 | 说明 |
|---|---|
| `Trigger=OnInterval` | 每个单位的每项技能独立计时。 |
| `Interval` | 必填正整数，单位为游戏帧。90 帧约为 6 个游戏秒。 |

#### 示例代码

```ini
[SF_PeriodicRepair]
Trigger=OnInterval
Interval=90
Action=Heal
Action.Target=Self
Action.Amount=15
```

#### 额外补充说明

`Trigger=OnInterval` 必须填写正整数 `Interval`。单位在地图上拥有该技能后，先等满一个周期；不同单位、不同技能各自计时。到点时再检查血量、电力、目标筛选、次数、概率和冷却，本次未生效仍按原周期继续。筛选中的对方是单位当前的攻击目标。没有攻击目标时可照常对自身执行动作，普通单位目标动作须有存活目标。离开地图或失去技能会重置计时，存读档保留进度。

示例中若加上 `Condition.Count=2`，则每两次合格的定时事件治疗一次。

### 6. 开火前替换武器

#### 简要介绍

被动持有者开始一轮标准武器齐射时，在第一发射出前同步判定一次。同一轮连发沿用这次判定。

#### 参数列表和对应说明

| 参数 | 说明 |
|---|---|
| `Trigger=OnBeforeFire` | 在当前齐射开始时同步判定。 |
| `Action=ReplaceWeapon` | 使用一个替换武器动作。 |
| `Action.Weapon` | 填准备替换使用的武器 ID。 |

#### 示例代码

```ini
[SF_SpecialShot]
Trigger=OnBeforeFire
Filter.TargetHouse=Enemies
Action=ReplaceWeapon
Action.Weapon=120mm
```

#### 额外补充说明

筛选中的对方是射击目标。点地开火时没有单位对方。

`OnBeforeFire` 与其他事件的延迟结算不同：它在当前一轮开火时同步执行，只支持一个 `Action=ReplaceWeapon` 和一个 `Action.Weapon`，按确定的条件选择替代武器。

单位使用原武器寻找目标并准备开火；如果替代武器未配置完整、不支持对空或对地、被禁用，或未通过开火和射程检查，本轮沿用原武器。替代武器射程更长不会扩大原武器的寻敌范围。同一轮连发保持首次选定的武器，即使被动在连发途中失效；连发中的选择随存档保存。单位成功开火后仍按原规则触发 `OnFire`。本版针对标准主、副武器槽；特殊多武器、乘员开火和扩展自定义武器判定需要另外验证。

允许目标所属方、目标类别和普通自身状态筛选；不支持 `Actions` 多结果、`Chance` 随机概率、`Cooldown`、`MaxTriggers`、`Condition.Count`（须为默认 1）、`Condition.MinDamage` 或 `TriggerAnim`。多个开火前替换被动同时符合条件时使用处理顺序中的第一个；被选武器不能使用时回退原武器，不再尝试其他被动。

### 7. 一次性结果总表

事件触发时，每个 `Action` 执行一次。直接写 `Action=` 可指定一个结果，通过 `Actions=` 可依次执行多个结果。治疗、生成单位等动作立即结算，永久属性修正则保存在目标单位身上。

| `Action` | 配套参数 | 适用目标 | 效果 |
| --- | --- | --- | --- |
| `Heal` | `Action.Amount`（正整数或公式）与 `Action.PercentOfDamage`（实际伤害的正整数百分比）二选一 | 单位 | 恢复目标的生命，最多恢复到当前生命上限。 |
| `Damage` | `Action.Amount`（正整数或公式）与 `Action.PercentOfDamage`（实际伤害的正整数百分比）二选一 | 单位 | 对目标造成基础伤害，实际掉血由游戏结算。 |
| `SetDamage` | `Action.Amount`（非负整数或公式） | 受击者 | 用于造成伤害前或受到伤害前，将本次待扣生命值设为指定数值；0 表示抵挡。 |
| `BlockDamage` | 无专用参数 | 受击者 | 用于造成伤害前或受到伤害前，完全抵挡本次待扣生命值。 |
| `GiveCredits` | `Action.Amount`（正整数或公式）与 `Action.PercentOfDamage`（实际伤害的正整数百分比）二选一 | 单位 | 给目标的所属方增加资金。 |
| `GiveResearchPoints` | `Action.Amount`（正整数或公式）与 `Action.PercentOfDamage`（实际伤害的正整数百分比）二选一 | 单位 | 给目标的所属方增加科研点。 |
| `ChangeMaxHealth` | `Action.Amount`（非零整数，可正可负）与 `Action.MaxHealthPercent`（1%～10000%）二选一 | 单位 | 永久调整最大生命。百分比按生效前的上限计算，只能增加。上限增加时同步回血，降低时只截去超过新上限的生命。 |
| `ChangeAmmo` | `Action.Amount`（非零整数，可正可负） | 单位 | 补充或扣除弹药，结果限于 0 至弹仓容量。 |
| `ConsumePassenger` | 无专用参数 | 单位 | 消耗目标的首位乘客及其全部嵌套乘客；仅为直属乘客触发一次目标的 `OnPassengerConsumed`。 |
| `ChangeFirepower` | `Action.Percent`（非零整数百分点或公式） | 单位 | 永久调整火力。 |
| `ChangeArmor` | `Action.Percent`（非零整数百分点或公式） | 单位 | 永久调整装甲强度，正值使目标更耐打。 |
| `ChangeSpeed` | `Action.Percent`（非零整数百分点或公式） | 单位 | 永久调整步兵、载具或飞行器的移速。 |
| `ChangeWeaponRange` | `Action.Percent`（非零整数百分点或公式） | 单位 | 永久调整常规武器的最大射程。 |
| `ChangeFireRate` | `Action.Percent`（非零整数百分点或公式） | 单位 | 永久调整射速。射速提高 25% 时，原本 60 帧的齐射间隔变为 48 帧。 |
| `ChangeScale` | `Action.Percent`（非零整数百分点） | 单位 | 永久调整步兵、载具或飞行器的显示尺寸，占地保持原值。 |
| `ChangeProjectileScale` | `Action.Percent`（非零整数百分点） | 单位 | 调整目标之后发射的弹体、命中爆炸动画和已有的 `CellSpread` 半径。 |
| `GrantPassive` | `Action.Passive`（被动 ID）；可选 `Action.Duration`、`Action.StackOnReapply` | 单位 | 授予已注册的被动。同一被动再次授予时，取较长的剩余时长，永久优先。 |
| `StealPassive` | `Action.Passive`（英文逗号分隔的被动 ID）；`Action.Amount`（每项最多夺取的层数，正整数，默认 1） | 单位，默认 `Target` | 从目标夺取列表中持有的被动，交给触发者。相同 ID 只处理一次。 |
| `SendSignal` | `Action.Signal` 等，详见自定义信号 | 单位或范围 | 向指定接收者排队发送信号，可附带目标和数值。 |
| `RemovePassive` | `Action.Passive`（一个或多个被动 ID，英文逗号分隔） | 单位，默认 `Self` | 一次移除目标持有的所有匹配项，清除各项层数、计数和等待进度。缺少某一项时继续处理其余项。 |
| `ChangePassiveStack` | `Action.Passive`（被动 ID）和 `Action.Amount`（非零整数，可正可负） | 单位 | 调整目标已持有的指定被动层数，上限由该被动的 `Stack.Max` 决定。 |
| `ReplaceWeapon` | `Action.Weapon`（武器 ID） | 仅自身 | 仅用于 `OnBeforeFire`，替换本轮开火所用的武器。 |
| `FireWeapon` | `Action.Weapon`（武器 ID） | 单位或落点 | 向指定目标或位置发射指定武器。 |
| `DetonateWarhead` | `Action.Warhead`（弹头 ID）；可选 `Action.Amount`（非负整数，默认 0） | 单位或落点 | 在指定位置引爆弹头，`Action.Amount` 是基础伤害。 |
| `SpawnUnit` | `Action.UnitType`（单位类型 ID）；可选 `Action.Owner` | 单位或落点 | 生成一个步兵、载具或飞行器，目标格无法放置时向外查找最多三圈。 |
| `Transform` | `Action.UnitType`（新形态的单位类型 ID） | 单位 | 原地永久变形，保留所属方和星级。两个形态须同属步兵、载具或飞行器中的一类，并使用相同的 `Locomotor`。转换接口和安全限制见科研转换说明。 |
| `ChangeOwner` | 可选 `Action.Owner` | 单位 | 把存活目标移交给指定所属方。目标已属该方或正受心灵控制时跳过。 |
| `SpawnCrate` | 可选 `Action.CrateType` | 单位或落点 | 在目标处放置一个升级箱，拾取方获得效果。 |
| `SelfDestruct` | 无专用参数 | 仅自身 | 使持有者自毁，按正常死亡流程结算。 |

| 参数 | 可选值与用法 |
| --- | --- |
| `Action.Target` | 造成伤害前和 `StealPassive` 默认 `Target`（事件对方），其余默认 `Self`（持有者）。表中“单位”可选 `Self`、`Target`（事件对方）、`Attacker`（攻击者）、`Victim`（受害者）。“单位或落点”还可选 `VictimLocation`（事件记录的受害者位置）。“仅自身”使用 `Self`。 |
| `Action.Duration` | 可选 `-1`（永久，默认值）或正整数（游戏帧），用于授予被动。 |
| `Action.StackOnReapply` | `no`（默认）或 `yes`。填 `yes` 时，目标已持有该被动就增加 1 层，最多到 `Stack.Max`。首次获得从 0 层开始。 |
| `Action.Owner` | `Self`、`Attacker`、`Victim`、`Neutral`、`Player1`、`Player2`、`Player3`、`Player4`、`Player5`、`Player6`、`Player7`、`Player8`。默认 `Self`。前三项取对应单位的所属方，`Neutral` 为中立方，`Player1` 至 `Player8` 对应出生位。 |
| `Action.CrateType` | `Random`、`Money`、`Unit`、`HealBase`、`Cloak`、`Explosion`、`Napalm`、`Squad`、`Darkness`、`Reveal`、`Armor`、`Speed`、`Firepower`、`ICBM`、`Invulnerability`、`Veteran`、`IonStorm`、`Gas`、`Tiberium`、`Pod`。默认 `Random`，按当前规则的 `[Powerups]` 权重抽取。 |

按伤害比例结算时取本次实际伤害并向下取整。例如实际造成 43 点伤害，`Action.PercentOfDamage=20%` 可恢复 8 点生命。表中支持公式的字段使用 `$(...)`，写法见[数值公式](#11-数值公式)。

`Action.Percent` 从 100% 的基础值按百分点累加。例如连续两次 `+10%` 后为 120%。体型默认下限为 25%，其余六项为 1%，默认上限均为 300%。每项的上下限可在 [全局配置](#三全局配置) 的 `[Starfall.PassiveLimits]` 中单独设置。

#### 示例代码

**夺取与批量移除被动**

将示例中的被动 ID 换成已注册的技能。从存活目标夺取，或从刚消耗的直属乘客记录中继承。每个匹配项分别转移，最多取指定层数。接收方永久获得被动，受该被动的 `Stack.Max` 限制，放不下的层数留给原单位。最后一层被夺走时，原单位失去该被动。普通的 0 层被动也能整项夺走，接收方已有同名被动时跳过该 0 层项。

```ini
[SF_Siphon]
Trigger=OnDealDamage
Filter.TargetHouse=Enemies
Action=StealPassive
Action.Passive=SF_Rage,SF_Armor
Action.Amount=2

[SF_Dispel]
Trigger=OnFire
Action=RemovePassive
Action.Target=Target
Action.Passive=SF_Rage,SF_Armor
```

**击杀叠加装甲**

注册下列两项被动，将 `SF_ReinforceOnKill` 绑定给单位。第一次击杀获得 150 帧的强化，重复击杀每次加 1 层，每层提高 10% 装甲。若单位已自带该强化，第一次击杀就加 1 层。层数或授予时长有变化都算动作成功；满层且时长未延长时，这一步不启动冷却。

```ini
[SF_ReinforcedArmor]
Trigger=WhileState
Condition.Formula=$(1)
Stack.Max=5
Modifier.ArmorMultiplier=$(1 + This.Stack * 10%)

[SF_ReinforceOnKill]
Trigger=OnKill
Filter.TargetHouse=Enemies
Action=GrantPassive
Action.Passive=SF_ReinforcedArmor
Action.Duration=150
Action.StackOnReapply=yes
```

##### 获得被动、限时授予和科研点奖励

以下四项均须在 `[Starfall.PassiveTypes]` 注册。通过科研授予重型底盘，可让符合条件的单位各增加一次生命上限。将狂热绑定到单位，即可在每三次敌方击杀后获得 75 帧的射速加成。

```ini
[SF_HeavyChassis]
Trigger=OnPassiveGained
Action=ChangeMaxHealth
Action.Amount=+100

[SF_BattleAnalysis]
Trigger=OnKill
Filter.TargetHouse=Enemies
Filter.TargetKind=Vehicle
Condition.Count=5
Action=GiveResearchPoints
Action.Amount=10

[SF_FrenzyOnKill]
Trigger=OnKill
Filter.TargetHouse=Enemies
Condition.Count=3
Action=GrantPassive
Action.Passive=SF_TimedFrenzy
Action.Duration=75

[SF_TimedFrenzy]
Trigger=WhileState
Condition.Formula=$(Self.Health >= Self.MaxHealth * 0%)
Modifier.FireRateMultiplier=1.50
```

##### 永久修改最大生命

```ini
[SF_KillMaxHealth]
Trigger=OnKill
Filter.TargetHouse=Enemies
Action=ChangeMaxHealth
Action.Target=Self
Action.Amount=+50
```

第一个被动在受击时向攻击者还击，第二个在击杀后于死者位置引爆弹头。

```ini
[SF_ReturnShot]
Trigger=OnTakeDamage
Filter.TargetHouse=Enemies
Cooldown=90
Action=FireWeapon
Action.Target=Attacker
Action.Weapon=120mm

[SF_DeathBlast]
Trigger=OnKill
Filter.TargetHouse=Enemies
Action=DetonateWarhead
Action.Target=VictimLocation
Action.Warhead=HE
Action.Amount=80
```


每次触发独立结算，效果保存在目标单位身上，可叠加并随存档保存。提高上限时，当前生命增加同样的实际点数。降低上限时，当前生命只在超出新上限时截断。例如将上限从 500 降至 400，350 点生命仍为 350，450 点生命则降到 400。上限最低为 1。调整上限本身不会产生受伤或击杀事件。

流火的生命条件、公式和实际伤害事件使用调整后的生命点数。游戏原生血条使用原兵种刻度显示同一比例，因此直接读取引擎原始 Health / Strength 的其他扩展可能显示原刻度的数字。

### 8. 持续性结果总表

`Trigger=WhileState` 的条件满足时，写在同一被动里的 `Modifier.` 字段一起生效；条件结束时撤回对应修正。每个字段都可独立填写。

#### 属性与电力倍率

| 持续结果字段 | 可选值 | 效果 |
| --- | --- | --- |
| `Modifier.SpeedMultiplier` | 倍率或公式，结果大于 0 且不超过 100 | 调整可移动单位的移速。 |
| `Modifier.FirepowerMultiplier` | 倍率或公式，结果大于 0 且不超过 100 | 调整火力。 |
| `Modifier.ArmorMultiplier` | 倍率或公式，结果大于 0 且不超过 100 | 调整装甲强度，倍率越高越耐打。 |
| `Modifier.WeaponRangeMultiplier` | 倍率或公式，结果大于 0 且不超过 100 | 调整常规武器的最大射程。 |
| `Modifier.FireRateMultiplier` | 倍率或公式，结果大于 0 且不超过 100 | 调整射速。齐射间隔除以该倍率，同一轮连发的子弹间隔沿用原配置。 |
| `Modifier.PowerMultiplier` | 倍率或公式，0～100（含两端） | 按同一倍率调整该建筑的电力。发电建筑调整供电量，耗电建筑调整耗电量。 |

这些倍率以 `1` 为原值，也支持[数值公式](#11-数值公式)。多个被动修改同一属性时，持续倍率相乘。例如永久火力已升至 120%，再获得 `1.50` 倍持续火力，总火力为 180%，退出状态后回到 120%。

#### 单位与弹体缩放

| 持续结果字段 | 可选值 | 效果 |
| --- | --- | --- |
| `Modifier.VisualScale` | 固定倍率 0.01～2.00 | 缩放单位的战场模型，不改变占地。 |
| `Modifier.ProjectileScale` | 固定倍率 0.50～2.00 | 按发射时倍率缩放弹体、命中爆炸动画及已有的 `CellSpread` 半径。 |

弹体倍率在发射时确定，飞行途中状态结束也不会改变这发弹体；原 `CellSpread` 为零时仍为单点。

#### 主副武器槽

| 持续结果字段 | 可选值 | 效果 |
| --- | --- | --- |
| `Modifier.DisablePrimary` | `yes`、`no` | 禁用主武器槽。 |
| `Modifier.PrimaryWeapon` | 武器 ID | 用指定武器替换主武器槽，保留该槽的发射位置。 |
| `Modifier.DisableSecondary` | `yes`、`no` | 禁用副武器槽。 |
| `Modifier.SecondaryWeapon` | 武器 ID | 用指定武器替换副武器槽，保留该槽的发射位置。 |

#### 特殊状态与免疫

| 持续结果字段 | 可选值 | 效果 |
| --- | --- | --- |
| `Modifier.Berserk` | `yes`、`no` | 使非建筑单位陷入混乱，可以攻击友军。 |
| `Modifier.Mirage` | `yes`、`no` | 使载具获得幻影坦克式静止伪装。 |
| `Modifier.Immune` | `yes`、`no` | 按原生 `Immune=yes` 免疫常规生命值伤害。 |
| `Modifier.ImmuneToMindControl` | `yes`、`no` | 免疫普通心控、心灵控制器及适配的 Ares 永久心控。已存在的心控继续有效。 |

开关字段填 `yes` 表示启用，填 `no` 或省略表示该被动不提供此效果。

#### 子机与光环的被动授予

| 持续结果字段 | 可选值 | 效果 |
| --- | --- | --- |
| `Modifier.SpawnPassives` | 一个或多个已注册的被动 ID，以英文逗号分隔 | 授予母体的直属 `Spawns=` 子机，包括后来补充的子机。条件结束或子机脱离时撤回该来源。 |
| `Modifier.TransportPassives` | 一个或多个已注册的被动 ID，以英文逗号分隔 | 由乘客持有，满足条件时授予其直属地面载具；可省略条件表示上车即生效。 |
| `Modifier.TransportPassives.Stack` | `yes`、`no` | 默认 `no`。`yes` 时每名符合条件的乘客为所授予的每个被动提供一层，随来源消失而撤回。 |
| `Modifier.Aura.Passives` | 已注册的被动 ID，可用英文逗号列出多个，必填 | 授予范围内符合条件的单位。可授予事件型和持续型被动；不能直接授予另一个带光环的被动。 |
| `Modifier.Aura.Range` | `1`～`32` 的整数，或 `-1`，必填 | 按持有者与受益单位中心的水平距离计算，单位为格，包含边界。`-1` 表示全图。 |
| `Modifier.Aura.Houses` | `Self`、`Allies`、`Enemies`、`Neutral`、`All`，可多选 | 默认 `Self`，指与持有者同一玩家。`Allies` 指其他盟友。 |
| `Modifier.Aura.TargetTypes` | `*`、`@Infantry`、`@Vehicle`、`@Aircraft`、`@Building` 或具体类型 ID，可混写 | 默认 `*`。命中任一类型即可，例如 `@Vehicle,SHK` 包括所有载具和磁暴步兵。 |
| `Modifier.Aura.IncludeSelf` | `yes`、`no` | 默认 `no`。设为 `yes` 时，持有者自己也要通过阵营和类型筛选。 |

#### 示例代码

**乘客为载具提供火力**

以下工程师每人提供一层，每层增加载具 30% 火力，最多计算三层。编号需换成注册表中空闲的编号。

```ini
[Starfall.PassiveTypes]
100=SF_PassengerSupport
101=SF_TransportFirepower

[SF_PassengerSupport]
Trigger=WhileState
Modifier.TransportPassives=SF_TransportFirepower
Modifier.TransportPassives.Stack=yes

[SF_TransportFirepower]
Trigger=WhileState
Condition.Formula=$(1)
Stack.Max=3
Modifier.FirepowerMultiplier=$(1 + This.Stack * 30%)

[ENGINEER]
Starfall.Passives=SF_PassengerSupport
```

乘客层数与载具原有的手动层数相加，由受授被动的 `Stack.Max` 限制公式实际读取的总层数。下车只撤回乘客贡献的层数；`ChangePassiveStack` 修改手动层数，不能移除仍在车内的乘客贡献。上限限制生效层数，不会因暂时超出上限而删除已有手动层数。

未开启叠加时，相同被动仍只生效一份，不会自动增加层数；多个来源中只有开启叠加的乘客提供层数。同一乘客通过多项支援能力提供同一个被动时，最多贡献一层。相同被动的其他来源仍然保留；当最后一个来源失效后，被动才会真正消失。完全失去后重新获得可再次触发 `OnPassiveGained`。

`Modifier.TransportPassives` 只检查直属地面载具的乘客列表，不包括建筑驻军或航空器。乘客被消耗、离车、死亡、失去支援被动或不再满足条件时，撤回对应来源；已结算的一次性结果不回滚。乘客的支援条件以乘客为 `Self`，可使用自身生命、弹药、层数等条件以及状态延时，但支援被动不能使用 `Condition.Nearby.*`、`Event.Distance` 或 `Target` 条件，也不能授予另一个带 `Modifier.TransportPassives` 的被动。载具收到的被动以载具为 `Self`。读档恢复载客关系，不会因读档重复触发获得事件。

**乘客自身的火力、射程与射速**

把这个被动注册并绑定给乘客兵种。乘客自己的血量不高于一半时，火力变为 150%，射程变为 125%，完整装填间隔减半。乘客回血后重新计算，下车后继续按自身状态判定。同车乘客独立结算。车内支持自身血量条件和倍率公式，依赖地图距离或附近单位的判定仍在场上进行。开放式载具中的乘客使用这些倍率射击；普通运输车仍按原有规则限制乘客开火。

```ini
[SF_WoundedGunner]
Trigger=WhileState
Condition.Formula=$(Self.Health <= Self.MaxHealth * 50%)
Modifier.FirepowerMultiplier=1.5
Modifier.WeaponRangeMultiplier=1.25
Modifier.FireRateMultiplier=2
```

**部署支援光环**

把 `SF_SupportAura` 绑定给能架设的单位。架设后，5 格内符合条件的己方和盟友单位获得 20% 装甲加成。将 `Modifier.Aura.Range` 改为 `-1` 即覆盖全图，阵营和类型筛选仍然生效。

```ini
[Starfall.PassiveTypes]
70=SF_SupportAura
71=SF_AuraArmor

[SF_SupportAura]
Trigger=WhileState
Condition.SelfDeployed=yes
Modifier.Aura.Range=5
Modifier.Aura.Houses=Self,Allies
Modifier.Aura.TargetTypes=@Vehicle,SHK
Modifier.Aura.Passives=SF_AuraArmor

[SF_AuraArmor]
Trigger=WhileState
Condition.Formula=$(1)
Modifier.ArmorMultiplier=1.20
```

每 15 个游戏帧更新一次范围内的单位。来源死亡、被移除或持续条件中断时撤回该来源。同名被动由多个光环、单位自带、科研或临时授予共同提供时只生效一份，最后一个来源消失才失去。重叠覆盖期间不会重复触发 `OnPassiveGained`，完全离开后再次获得可重新触发。击杀回血等已经结算的事件结果会保留。

建筑生命至少为一半时，电力乘以 0.75。同一个被动绑给耗电 40 点的兵营，耗电降到 30 点；绑给供电 200 点的电厂，供电降到 150 点。

```ini
[SF_BuildingPower]
Trigger=WhileState
Condition.Formula=$(Self.Health * 2 >= Self.MaxHealth)
Modifier.PowerMultiplier=0.75
```

电力倍率只作用于持有被动的建筑。旧配置中的 `Modifier.PowerOutputMultiplier` 和 `Modifier.PowerDrainMultiplier` 须改为 `Modifier.PowerMultiplier`。多个被动的电力倍率相乘，按建筑的实际电量计算并四舍五入到整数。`0` 表示供电或耗电降为零，条件结束后恢复。用所属方是否缺电作条件时，如果效果本身会改变供电状态，倍率可能反复启停。

**部署免疫。** 以下示例只给已部署的持有者提供免疫。需要等待时，可在同一段中加 `Condition.StateDurationSeconds=2`。绕过防御的强制伤害（例如流火自毁）仍按引擎规则执行；弹头附加状态和第三方护盾有各自的判定。

```ini
[SF_EntrenchedImmunity]
Trigger=WhileState
Condition.SelfDeployed=yes
Modifier.Immune=yes
```

**免疫心灵控制。** 例如生命不高于 50% 时，单位免疫新的心灵控制：

```ini
[SF_MindShield]
Trigger=WhileState
Condition.Formula=$(Self.Health <= Self.MaxHealth * 50%)
Modifier.ImmuneToMindControl=yes
```

例如，自己的航母生命值至少为 50% 时，其舰载机获得 25% 火力加成。注册编号选用空位，航母已有被动时将新 ID 追加到原列表。

```ini
[Starfall.PassiveTypes]
60=SF_CarrierCommand
61=SF_DroneFirepower

[CARRIER]
Starfall.Passives=SF_CarrierCommand

[SF_CarrierCommand]
Trigger=WhileState
Condition.Formula=$(Self.Health >= Self.MaxHealth * 50%)
Modifier.SpawnPassives=SF_DroneFirepower

[SF_DroneFirepower]
Trigger=WhileState
Condition.Formula=$(Self.Health >= Self.MaxHealth * 0%)
Modifier.FirepowerMultiplier=1.25
```


该效果按单位生效，不修改同类型单位的原生属性；条件中断或最后一份来源失效后恢复原有判定，状态随存档保存。生效前已存在的心控不会自动解除，心灵控制器的伤害也不受此效果影响。Ares 永久心控适配《远征》使用的 Ares 3.0p1；版本或入口不兼容时会在 `Starfall.log` 记录，永久心控保护无法保证。其他扩展直接改换所属方的特殊逻辑（包括 Phobos `ChangeOwner.SetAsMindControl`）和地图脚本强制易主不属于已适配的心控入口。

主槽对应索引 0，副槽对应索引 1。普通双武器单位只有一个槽被禁用时，会尝试选用另一把已配置的武器；射程、弹药等仍按游戏规则。特殊多武器、乘员炮塔和盖特阶段沿用游戏原选槽规则。多个生效技能同时修改同一槽时，任一禁用都会阻止发射；多个替换按技能处理顺序取最后一个，某项退出后回到仍有效的替换或当前原生武器。原生武器随星级、部署等变化。仅影响该单位的常规武器槽；`Action=FireWeapon` 仍使用动作里指定的独立武器。

通常至少指定一个状态条件，并配置持续结果或进入状态时播放的触发动画；乘客的 `Modifier.TransportPassives` 可以省略条件。例如 `Condition.Formula=$(Self.Health <= Self.MaxHealth * 30%)` 搭配 `Modifier.Berserk=yes` 表示持有者生命不高于 30% 时陷入混乱。等待时长可用于延迟状态生效。持续状态和等待进度支持存读档。

### 9. 多结果

单结果沿用 `Action=`。多结果用 `Actions=` 引用一个或多个结果段，按英文逗号分隔，按书写顺序执行。同一结果被列两次就执行两次。只有主技能需要注册和绑定，结果段可跨技能复用。

结果段只填写 `Action` 和相应的 `Action.*` 字段。次数、概率和冷却写在主技能上。主段使用 `Actions` 时不能同时填写 `Action` 或 `Action.*`，结果段也不能再嵌套 `Actions`。缺少结果段、空列表项、无效动作或上述冲突都会停用整项技能，并在游戏根目录的 `Starfall.log` 记录字段。若希望两个结果分别判定概率，可将其配置为两个被动。

```ini
[SF_KillReward]
Trigger=OnKill
Filter.TargetHouse=Enemies
Condition.Count=3
Actions=SF_KillReward_Heal,SF_KillReward_Crate

[SF_KillReward_Heal]
Action=Heal
Action.Target=Self
Action.Amount=50

[SF_KillReward_Crate]
Action=SpawnCrate
Action.Target=VictimLocation
Action.CrateType=Random
```

每第三次有效击杀共用一次概率判定。通过后，先尝试恢复 50 生命，再在死者附近生成随机箱。满血仍会掉箱。只要一项成功就按主技能开始冷却，全部无效时不进冷却。后一步可以使用上一步改变后的状态；例如先自毁再在自身位置掉箱，会使用持有者最后的位置。需要存活单位的步骤在对应单位死亡后跳过。

### 10. 动画

动画字段写在注册的被动主段里。可与单结果 `Action=`、多结果 `Actions=` 或持续性 `Modifier.*` 同用。

| 字段 | 行为 |
|---|---|
| `Anim=动画ID` | 挂载一份随单位移动的循环动画，动画大小实时跟随单位体型比例。失去被动时移除；隐形或离开地图期间隐藏，返回后重新判断是否显示 |
| `Anim.Always` | `no`（默认）时，满足被动的自身状态条件才显示；`WhileState` 还需满足等待时间，条件不再满足时隐藏。`yes` 时，持有被动期间不受这些条件影响。未配置自身状态条件时，`no` 也会持续显示。事件的目标筛选、次数、概率和冷却不控制附着动画 |
| `TriggerAnim=动画ID` | 事件通过筛选、计数、冷却和概率判定后播放一次，先于整组结果执行。即使满血使治疗无效，动画也照常播放。`WhileState` 在满足条件和等待时长后播放一次，退出再进入时可再次播放 |
| `TriggerAnim.Target=Self` 或 `Target` | 默认 `Self`。写 `Target` 时，受击或死亡取攻击者，攻击或击杀取受害者，部署和晋升取自身，`WhileState` 取生效瞬间的当前目标。死亡时自身动画可在死亡位置播放，受害者消失或点地开火时可使用记录的落点。没有对应对象或坐标时跳过这次播放 |

持有者常驻光环，造成伤害时在受害者身上播放闪光。

```ini
[SF_AnimatedHit]
Trigger=OnDealDamage
Anim=YURICNTL
TriggerAnim=PIFF
TriggerAnim.Target=Target
```

```ini
[SF_WoundedGlow]
Trigger=WhileState
Condition.Formula=$(Self.Health <= Self.MaxHealth * 50%)
Condition.StateDurationSeconds=1
Anim=YURICNTL
Anim.Always=no
```

生命值不高于 50% 持续 1 秒后显示附着动画，生命值恢复后隐藏；改为 `Anim.Always=yes` 则持有被动时持续显示。该动画随单位体型实时缩放，`TriggerAnim` 播放的一次性动画不受此设置影响。

动画 ID 取自当前游戏已注册的动画类型。只需要始终显示的特效时，注册的被动段可以只写 `Anim=`；只需要有条件的附着特效时，可在 `WhileState` 中只配置条件与 `Anim=`。只需要事件特效时，可省略动作，由实际播放启动该技能配置的冷却。已有动作的技能仍按动作是否成功决定冷却。多个结果共用一次触发动画。存读档保留附着动画和状态进入记录。

### 11. 数值公式

#### 简要介绍

用 `$(...)` 让被动数值随血量、层数、伤害或距离变化。`Self` 指持有被动的单位，`This` 指正在执行公式的被动。

#### 参数列表和对应说明

移速、火力、装甲、射程和射速的持续倍率字段，以及对应事件结果的 `Action.Percent`，都支持公式。建筑的 `Modifier.PowerMultiplier` 也支持公式。持续倍率 `1.25` 表示乘以 1.25；事件增量 `$(10%)` 表示增加 10 个百分点。

治疗、伤害、发放资金和科研点的 `Action.Amount` 也支持公式，例如 `$(min(Event.Damage * 25%, 100))`。结果向下取整，负数或超出整数范围时跳过，零无效果。此写法与 `Action.PercentOfDamage` 互斥。

支持小数、正负号、四则运算、括号和 `min(a,b)`、`max(a,b)`、`clamp(x,下限,上限)`。变量和函数名忽略大小写，`1.5%` 等于 `0.015`。

比较运算支持 `>`、`>=`、`<`、`<=`、`==`、`!=`，结果为成立 `1` 或不成立 `0`。比较的优先级低于四则运算；多个比较需分别用括号表达，不支持 `1 < x < 5` 这种连写。比较小数时，`==` 和 `!=` 按计算结果精确比较。

`if(条件, 成立值, 不成立值)` 根据条件选择结果：条件为 `0` 表示不成立，非 `0` 表示成立。`if` 可嵌套，且只计算被选中的分支；未选中分支里的除零或缺少目标不会影响结果。配置时仍须满足变量的使用范围，例如 `Event.Damage` 只能用于伤害事件，即使它位于未选中的分支。

| 变量 | 含义 |
|---|---|
| `Self.Health / Self.MaxHealth` | 持有者的当前生命和最大生命。 |
| `Target.Health / Target.MaxHealth` | 对方单位的当前生命和最大生命。造成伤害或击杀时取受害者，受伤或死亡时取攻击者；开火或定时事件取当时的目标，`WhileState` 取当前目标；消耗乘客时读取乘客消耗前的快照。部署、晋升和获得被动事件没有对方。 |
| `Self.Cost` | 被动持有者当前类型的基础造价，不计折扣。事件读取触发时的造价，持续状态读取当前造价。没有目标时也能使用。例如 `Action.Amount=$(Self.Cost * 10%)` 按自身造价的 10% 计算。 |
| `Target.Cost` | 对方单位类型在当前规则中的基础造价 `Cost`，不计国家、工业工厂或科研折扣。事件触发时记录造价，持续状态读取当前目标的造价。 |
| `Self.Ammo` | 持有者当前弹药数量；无限弹药为 -1。事件读取发生时快照。 |
| `Self.Passengers` | 持有者当前乘客数量，空载为 0。事件读取发生时快照。 |
| `This.Stack` | 正在执行公式的被动层数。 |
| `Event.Damage` | `OnDealDamage`、`OnTakeDamage` 读取实际损失生命；`OnBeforeDealDamage`、`OnBeforeTakeDamage` 依次读取当前待扣生命值，含前面动作的修改。护盾完全吸收伤害时没有损血事件。 |
| `Event.Distance` | 持有者与对方的水平直线距离，以格为单位，可含小数，忽略高度。造成伤害或击杀时取受害者，受伤或死亡时取攻击者，开火或定时事件取当时的目标（可为地面格）。`WhileState` 取当前目标。 |

#### 示例代码

当前目标距离超过 5 格时，持续火力提高 30%；等于 5 格时不提高：

```ini
[SF_LongRangePower]
Trigger=WhileState
Condition.Formula=$(Self.Health >= Self.MaxHealth * 0%)
Modifier.FirepowerMultiplier=$(if(Event.Distance > 5, 1.30, 1.00))
```

如果距离等于 5 格也要生效，将 `>` 改为 `>=`。多阶段也可以嵌套，例如 `$(if(Event.Distance > 8, 1.50, if(Event.Distance > 5, 1.30, 1.00)))`。没有目标时，距离无法计算，这项倍率不会生效。

按命中距离回血，每格 5 点，上限 50 点。

```ini
[SF_DistanceHeal]
Trigger=OnDealDamage
Action=Heal
Action.Amount=$(min(Event.Distance * 5, 50))
```

造成伤害时，根据受害者已损失生命的 10% 获得科研点：

```ini
[SF_TargetWounds]
Trigger=OnDealDamage
Action=GiveResearchPoints
Action.Amount=$((Target.MaxHealth - Target.Health) * 10%)
```

击杀敌方单位后，获得其基础造价的 25%。例如击杀造价 800 的单位，持有者所属方获得 200 资金。

```ini
[SF_KillBountyByCost]
Trigger=OnKill
Filter.TargetHouse=Enemies
Action=GiveCredits
Action.Amount=$(Target.Cost * 25%)
```

以下两项分别演示半血时持续火力乘以 1.75，以及半血击杀时累计增加 0.75 个百分点。

```ini
[SF_WoundedFirepower]
Trigger=WhileState
Condition.Formula=$(Self.Health <= Self.MaxHealth * 100%)
Modifier.FirepowerMultiplier=$(1 + ((Self.MaxHealth-Self.Health)/Self.MaxHealth) * 150%)

[SF_WoundedTraining]
Trigger=OnKill
Action=ChangeFirepower
Action.Target=Self
Action.Percent=$(((Self.MaxHealth-Self.Health)/Self.MaxHealth) * 1.5%)
```

以下被动每层提供 5% 的持续火力加成，最多 20 层：

```ini
[SF_StackedFirepower]
Trigger=WhileState
Condition.Formula=$(Self.Health >= Self.MaxHealth * 0%)
Stack.Max=20
Modifier.FirepowerMultiplier=$(1 + This.Stack * 5%)
```

层数初始为 0，可由 `ChangePassiveStack` 动作修改，也可由开启叠加的乘客提供。多结果中的 `This.Stack` 读取触发这些结果的主被动的总层数。

#### 额外补充说明

持续倍率每帧重算。`Target` 是事件的对方或单位当前目标，与 `Action.Target` 指定的动作接收者无关。目标是地面格或不存在时，实际执行到 `Target` 生命变量的动作或持续倍率跳过；持续倍率在有有效单位目标后重新计算。击杀事件的受害者当前生命按死亡时的值计算，通常为 0。

已损失生命可用 `Self.MaxHealth - Self.Health` 或 `Target.MaxHealth - Target.Health` 计算；已损失比例再除以对应的最大生命。事件记录发生时双方的血量、层数和事件数据，同组动作先回血或增层不会改变快照；`ConsumePassenger` 成功后，同组后续公式的 `Target` 改为该乘客消耗前的快照。距离按事件发生时记录，伤害事件取命中时距离。待处理事件的快照随存档保存。

语法错误会停用整项被动。运行时遇到缺少所需变量（包括旧存档中缺少距离、目标生命或造价快照）、除零、非有限数、限幅上下界颠倒或持续倍率超出对应字段的范围时，只跳过对应动作或持续倍率。运算错误信息见 `Starfall.log`。

### 12. 弹头命中授予

#### 简要介绍

下例在 `rulesmd.ini` 中配置支援弹头，使直接命中的己方或盟友单位获得 150 帧的 `SF_KillHeal`。先注册并定义该被动，再为弹头选用空闲编号。

#### 参数列表和对应说明

| 字段 | 用法 |
|---|---|
| `Starfall.GrantPassives` | 填一个已注册的被动 ID 或用英文逗号列出多个。一个 ID 无效时，整条授予配置停用，游戏根目录的 `Starfall.log` 会记录原因 |
| `Starfall.GrantPassives.Duration` | 正整数为游戏帧数，`-1` 或省略为永久。15 游戏帧约为 1 个游戏秒 |
| `Starfall.GrantPassives.StackOnReapply` | `no`（默认）或 `yes`。填 `yes` 时，目标已持有该被动就增加 1 层，最多到 `Stack.Max`。首次获得从 0 层开始。 |
| `Starfall.GrantPassives.Houses` | 多选，英文逗号分隔：`All`、`Self`、`Allies`、`Enemies`、`Neutral`。默认 `All`。从发射方判断所属关系，并同时遵守弹头的阵营限制。 |

#### 示例代码

```ini
[Warheads]
900=SF_TrainingWH

[SF_TrainingWH]
CellSpread=0
Verses=100%,100%,100%,100%,100%,100%,100%,100%,100%,100%,100%
AffectsAllies=yes
Starfall.GrantPassives=SF_KillHeal
Starfall.GrantPassives.Duration=150
Starfall.GrantPassives.Houses=Self,Allies

; 将以下字段合并到实际使用的支援武器段
[MySupportGun]
Damage=0
Warhead=SF_TrainingWH
```

#### 额外补充说明

`CellSpread=0` 时，弹体直接命中单位才授予；地面空点没有接收者。正数按爆心范围逐个筛选，建筑按占地格判定。零伤害支援弹也可授予。受铁幕保护、离场、已死亡或对该弹头装甲倍率为零的目标不会获得能力。对方能否被支援武器选中仍由武器本身决定。

同一次弹头引爆对同一单位最多施加一次，直击和范围伤害重合也只算一次。同一游戏帧的两次独立引爆分别计算。列表内的每个被动独立判断；已有的自带、科研、光环或其他有效来源都算持有。加层不重触发 `OnPassiveGained`，光环的周期刷新也不算施加。同一单位的相同被动只执行一份。重复命中取“已有剩余时间”和“本次持续时间”中的较长者，保留击杀计数、周期进度和冷却。永久授予优先。到期只收回弹头授予的来源；同名自带或科研被动仍可使用。失去最后一个来源后，再次获得从头计数。使用 `RemovePassive` 可驱散当次授予，之后再次命中能重新授予。存档保留单位、被动 ID 和剩余有效期。

**进入建筑或载具。** 临时授予的有效期按游戏帧继续计算，不因进入建筑或运输载具而暂停。例如，150 帧的被动在载具中经过 60 帧后，还剩约 90 帧；在里面到期则出载后不会恢复。被动属于乘员本身，不会自动转移给建筑或载具。此规则也适用于地图触发器和小队脚本的限时授予。

### 13. 地图与小队授予

#### 简要介绍

先注册一套授予方案，再用地图触发器或小队脚本的 9601 动作，把方案中的被动发给指定单位。

#### 参数列表和对应说明

| 参数 | 说明 |
|---|---|
| `[Starfall.PassiveGrants]` | 用数字键注册方案，地图或小队脚本通过该编号引用。 |
| `Passives` | 填已注册的被动 ID，多个用英文逗号分隔。 |
| `Duration` | 正整数按游戏帧计时，省略或填 -1 表示永久。 |
| `TargetTypes` | 可选，限制能接收方案的单位类型。 |

`TargetTypes` 留空时建筑也能接收。若引用了无效的被动或单位 ID，整套方案停用，原因记入 `Starfall.log`。

其中 `1,4475` 是“方案编号,接收范围”。接收范围可填：

| 值 | 接收者 |
|---|---|
| `-1` | 游戏传给本次触发动作的对象。适用于有单位对象的事件，对象为空或已经死亡时跳过 |
| `-2` | 身上挂有标签、且标签关联本触发器的所有在场单位 |
| `0` | 触发器所属方当前在场的单位 |
| `4475`～`4482` | 依次为 1～8 号出生位玩家当前在场的单位 |

#### 示例代码

在 `Starfall.ini` 的 `[Starfall.PassiveGrants]` 注册授予方案。左侧编号供地图和脚本引用，使用不带前导零的非负整数。方案包含一个或多个已注册的被动，可设置有效帧数和接收单位的类型。以下配置让接收者获得 450 帧的周期维修能力，约合 30 个游戏秒。

```ini
[Starfall.PassiveGrants]
1=SF_FieldRepairGrant

[SF_FieldRepairGrant]
Passives=SF_PeriodicRepair
Duration=450
; TargetTypes=MTNK,HTNK
```

**地图触发器动作 9601** 在触发时授予一次。以下单动作示例给 1 号出生位玩家当时在场的单位授予方案 1。将 `0F510005` 换成地图里实际使用的触发器 ID。

```ini
[Actions]
0F510005=1,9601,0,0,1,4475,0,0,A
```

小队脚本动作 `9601` 只填方案编号。执行时授予当前在场且符合类型筛选的成员，再执行下一行。新加入的成员需等脚本再次执行该行。

```ini
[MyTeamScript]
0=9601,1
```

#### 额外补充说明

地图和小队都只发给当前存活、在地图上的对象。方案编号未注册或没有符合条件的接收者时，后续动作照常执行。与弹头对同一单位授予同名被动时，共用一份计数和有效期，取较长的剩余时间，永久优先。已授予的被动随单位存读档。

## 二、科研三选一

### 1. 玩法简介

科研三选一让玩家在对局中选择升级方向。每位玩家独立积累科研点，攒满 100 点即可打开科研面板，从本轮最多三张卡片中选购一项。科研点可以来自地图奖励、特殊建筑的持续产出或被动能力。

普通科研会根据玩家的国家、前置研究和已购项目确定候选，再抽出同一稀有度的卡片。允许刷新时，每个卡位每轮可免费更换一次。选中一项后扣除 100 点，该项目对这名玩家生效。例如，玩家可以让自己的某类单位获得被动，也可以解锁科技或立即获得资金。

有些地图还提供免费科研。占领或成功渗透指定建筑后，玩家可以从该建筑预设的项目中领取一项，每栋建筑只能领取一次。

### 2. 科研项目参数

#### 简要介绍

每个项目 ID 对应一张科研卡。注册项目后，在同名段设置卡面内容、候选资格和购买结果。

#### 参数列表和对应说明

##### 注册项目 `[Starfall.ResearchTypes]`

在 `[Starfall.ResearchTypes]` 用空闲数字键登记项目 ID，再创建同名项目段。数字键用于注册，前置研究与存档使用右侧的项目 ID。同一个 ID 重复登记只算一个项目。未注册项目时，科研项目列表为空。

##### 单个项目段 `[项目ID]`

这些字段只作用于当前卡片。`Countries` 筛选能看到卡片的国家，`TargetTypes` 指定获得效果的单位。

| 字段 | 用途 |
|---|---|
| `Name` / `Summary` / `Detail` | 项目名称、列表简述、悬浮详情。省略详情时按结果生成默认说明 |
| `Icon` | 图标文件名，支持游戏根目录下的 PNG（支持透明度）或游戏可加载的 PCX。省略、留空或加载失败时使用内置默认图标 `sfrdefault.png`。同名外部 PNG 可覆盖内置图片。可填写相对游戏根目录的 PNG 路径，如 `HexBrawlIcons\Focus.png`。建议卡片图标使用 96×96 像素。科研卡片和已研究列表中，`Rare` 图标显示为蓝紫色，`Epic` 显示为金色，`Common` 使用原图配色；变色保留透明镂空。 |
| `Countries` | 可填游戏 `[Countries]` 中的国家 ID，多个用英文逗号分隔。省略、留空或 `*` 表示不限国家。 |
| `Prerequisites` | 前置**科研项目 ID**，支持逗号分隔，须由购买者本人全部完成。省略或留空表示无前置；引用不存在的项目、循环依赖及依赖该坏链的项目不会进入候选 |
| `Rarity` | 项目稀有度：`Common`（普通，默认）、`Rare`（稀有）或 `Epic`（史诗）。填写无效值时项目不进入候选 |
| `Passive` | 使用 `GrantPassive` 时，填写已在 `[Starfall.PassiveTypes]` 注册的被动 ID。 |
| `TargetTypes` | 授予被动的目标：具体单位类型 ID、`*`（全部）、`@Infantry`（步兵）、`@Vehicle`（载具）、`@Aircraft`（飞行器）、`@Building`（建筑）。多个用英文逗号分隔，可混用。新建 `GrantPassive` 项目应显式填写 |
| `Effect` | 单选：`GrantPassive`、`GiveCredits`、`UnlockTech`、`ConvertInfantry`、`ConvertVehicle`、`ModifyTypeCost`、`ModifyCategoryCost`。默认 `GrantPassive`；各项配套字段见科研结果表。 |
| `Effects` | 可选的科研效果段列表，以英文逗号分隔；填写后使用列表中的效果，项目段内单个 `Effect` 及其参数不生效 |

项目段中的 `Effect` 指定购买结果，省略时默认为 `GrantPassive`。例如 `GiveCredits` 搭配 `Credits=2000`，而 `GrantPassive` 搭配 `Passive` 和 `TargetTypes`。用 `Effects=` 组合多项结果时，把每项的 `Effect` 和专用参数写到对应效果段，卡片名称、稀有度、国家和前置仍写在项目段。

#### 示例代码

```ini
[Starfall.ResearchTypes]
0=SF_Drain
1=SF_LasherDrain
```

##### 示例：同一被动分别授予两个兵种

两个项目共用同一份 `SF_LifeSteal` 被动定义，分别授予灰熊和狂风。

```ini
[SF_Drain]
Rarity=Rare
Name=伤害吸血
Summary=造成伤害时吸取生命
Detail=按实际伤害的 {percent}% 恢复生命
Icon=HexBrawlIcons\Vampirism.png
Effect=GrantPassive
TargetTypes=MTNK
Passive=SF_LifeSteal

[SF_LasherDrain]
Name=伤害吸血
Summary=造成伤害时吸取生命
Detail=按实际伤害的 {percent}% 恢复生命
Icon=HexBrawlIcons\Vampirism.png
TargetTypes=LTNK
Passive=SF_LifeSteal
Prerequisites=SF_Drain
```

#### 额外补充说明

普通科研每项花费 100 点，每位玩家按项目 ID 限购一次。先按国家、前置和已购记录筛选，再按权重抽取一个稀有度，提供最多三张同档卡片。暂时没有即时效果目标的项目可显示为「暂无目标」，满足条件后才能购买。

开启刷新后，每轮每个卡位可免费更换一次，只从同档合格且未在牌面出现的项目中替换。无可替换项时保留次数。关窗和读档保留当前牌面及刷新次数，购买后开始下一轮。建筑奖励使用固定列表。 抽卡权重、点数和刷新开关见[全局配置](#三全局配置)。

例如 `TargetTypes=@Infantry,@Building` 向研究者所属的所有步兵和建筑授予被动，`TargetTypes=@Infantry,MTNK` 则作用于所有己方步兵和灰熊坦克；载具类别也包括船只。类别依据游戏的内部对象类型，而非是否在空中，例如飞行步兵仍属于步兵。已拥有和之后获得的对象都会按当前所属方的研究记录判断，易主后按新主人判断。类别选择符与 ID 均不区分大小写，错误的 `@` 类别会使项目配置无效。此类别语法用于科研 `GrantPassive` 的 `TargetTypes`，包括 `Effects` 列表中的效果段；地图授予方案的同名字段仍按其原有规则填写。

购买第一项后，自己的灰熊获得吸血；自己的狂风要等购买第二项才获得。盟友完成研究不会替你满足前置。若希望只花一次科研点就强化两种坦克，写一个项目并设置 `TargetTypes=MTNK,LTNK`。

### 3. 科研结果

#### 简要介绍

科研项目可以授予单位被动、发放资金、解锁科技、转换单位或调整生产价格。一个项目也可以组合多项效果。

#### 参数列表和对应说明

| `Effect` | 配套字段 | 作用范围和生效时机 |
|---|---|---|
| `GrantPassive`（默认） | `Passive`、`TargetTypes` | 购买者已有和今后获得的指定类型单位均可使用被动。单位易主后按新主人的研究记录判断 |
| `GiveCredits` | `Credits=2000` | 购买时一次性给购买者增加正整数资金 |
| `UnlockTech` | `Technology` 填一个已注册的步兵、载具、舰船、飞机或建筑类型 ID。 | 仅给购买者永久授予指定类型的生产权限，可跳过通常的科技等级和建筑前置。生产仍需要兼容的生产设施、资金和可用建造名额。 |
| `ConvertInfantry` | `FromType=E2`、`ToType=SHK` | 将购买时已有的己方动员兵原地转换为磁暴步兵；后来新造的动员兵仍按原类型生成。两个 ID 须为不同的有效步兵类型 |
| `ConvertVehicle` | `FromType=MTNK`、`ToType=HTNK` | 将购买时已有的符合条件的己方灰熊坦克原地转换为犀牛坦克；新造单位不受影响。两个 ID 须为不同的有效载具类型 |
| `ModifyTypeCost` | `TargetTypes=MTNK`、`CostModifier=-100` 或 `-20%` | 永久调整购买者生产指定类型的价格，可用逗号列出多个具体 ID |
| `ModifyCategoryCost` | `Category` 单选：`Building`、`Defense`、`Infantry`、`Vehicle`；`CostModifier` 填百分比。 | 永久按百分比调整购买者一个生产类别的价格 |

`CostModifier` 填写有符号的整数。`-20%` 和 `+20%` 分别表示降价和涨价两成，`-100` 和 `+100` 分别表示减少和增加 100 资金。零值、无效单位 ID 或类别、类别修正缺少 `%` 均使项目无效。`Building` 是普通建筑，`Defense` 是 `BuildCat=Combat` 的防御栏建筑，`Infantry` 是步兵，`Vehicle` 包含地面载具、舰船和飞机。

一个项目可用 `Effects` 引用多个效果段，每段分别填写 `Effect` 和该类型需要的参数。科研效果段无需注册；其中引用的被动仍需按被动能力规则注册。`GrantPassive` 的 `TargetTypes` 和 `Passive` 填在各自的效果段中。例如：

#### 示例代码

购买后可在兼容的兵营里训练超时空军团兵。注册编号 40 可换成空闲编号。要一次解锁多个类型，用 `Effects` 引用多个效果段，每段填写一个 `Technology`。

```ini
[Starfall.ResearchTypes]
40=SF_ChronoLicense

[SF_ChronoLicense]
Name=超时空军团兵生产许可
Effect=UnlockTech
Technology=CLEG
```

旧配置中的 `Allied`、`Soviet`、`Yuri` 现在按类型 ID 解析。原版 `YURI` 指尤里步兵。要保留原科研设计，请改填原本希望玩家能购买的具体类型。

```ini
[Starfall.ResearchTypes]
40=SF_GrizzlyDiscount
41=SF_VehicleDiscount

[SF_GrizzlyDiscount]
Name=灰熊量产
Effect=ModifyTypeCost
TargetTypes=MTNK
CostModifier=-100

[SF_VehicleDiscount]
Name=载具标准化
Effect=ModifyCategoryCost
Category=Vehicle
CostModifier=-20%
```

```ini
[Starfall.ResearchTypes]
8=SF_CombinedUpgrade

[SF_CombinedUpgrade]
Name=装甲综合升级
Summary=灰熊坦克获得两项被动，并获得资金
Effects=SF_Combined_Heal,SF_Combined_Counter,SF_Combined_Money

[SF_Combined_Heal]
Effect=GrantPassive
TargetTypes=MTNK
Passive=SF_ResearchRepair

[SF_Combined_Counter]
Effect=GrantPassive
TargetTypes=MTNK
Passive=SF_Counter

[SF_Combined_Money]
Effect=GiveCredits
Credits=2000
```

```ini
[Starfall.ResearchTypes]
0=TankUpgrade

[TankUpgrade]
Name=装甲升级
Summary=灰熊坦克转为犀牛坦克
Effect=ConvertVehicle
FromType=MTNK
ToType=HTNK
```

#### 额外补充说明

先按国家和工业工厂等原有修正得出基准价，再合计科研百分比，最后加上固定金额。结果四舍五入，最低为 0。例如原价 700，同时获得 −20% 和 −100 后售价为 460。订单在开工时锁价，取消时退还已付金额。读档按玩家的已购项目恢复修正。

单位转换不再要求 Ares。没有加载 Phobos 时，流火使用内置转换，保留单位身份、所属方、星级和被动状态，按原生命比例调整生命，并保留额外生命上限。加载 Phobos 时，流火使用其公开的 `ConvertToType_Phobos` 接口，以便由 Phobos 更新自己的单位数据。该接口的不同 Phobos 版本需要自行验证；未提供接口的版本（包括《远征》随附的 Dev Build #36）不支持转换，流火会在 `Starfall.log` 记录原因。

内置转换暂不支持带子机或奴隶的类型、目标类型具有心控、寄生或超时空武器的情况；受到心控、寄生或超时空攻击，正在部署，或带有子机、奴隶、心控、空袭管理器的单位也会跳过。乘客仍须符合目标类型的容量与体型限制；有乘客时，开放舱和炮手设置须一致。使用其他扩展时，其额外附着效果和类型相关状态需另行验证。

科研转换只处理地图上存活且满足类型、地形、乘客容量等条件的己方单位，其他单位跳过。接口不可用、类型无效或移动器 `Locomotor` 不同时，这类项目不进入候选。没有合适目标或全部转换失败时无法购买，不扣科研点；至少一个转换成功即购买成功。转换不影响之后生产的单位。被动结果 `Transform` 采用同样的转换路径和限制。

效果按首次出现的顺序执行，同一效果段 ID 重复填写时只执行一次。任何效果配置无效、缺失或依赖不可用时，整个项目不进入候选。购买时跳过暂时无法执行的效果（如没有可转换单位或科技已解锁），其余效果继续执行；至少一项成功才扣点并记录研究，已跳过的即时效果不会在以后补发。被动效果作用于购买者当前和未来符合类型的单位。效果之间不保证事务回滚。`Effects` 为空时项目无效；未填写 `Effects` 的旧式单效果配置保持兼容。

列表项目省略 `Detail` 时，悬浮提示会按顺序显示效果；自定义 `Detail` 按原文显示，不替换各效果的数值占位符。

悬浮 `Detail` 可随数值变化。事件被动支持 `{amount}`、`{percent}`、`{count}`、`{chance}` 和 `{cooldown}`。多结果被动省略详情时按顺序逐项显示，手写详情中的 `{amount}` 和 `{percent}` 取第一项结果的数值。资金项目使用 `{credits}`。百分比占位符只替换数字，要显示百分号请写 `{percent}%`。`WhileState` 可省略 `Detail` 以显示实际倍率，自定义持续效果详情按原文显示。

### 4. 科研点的来源

#### 简要介绍

科研点可以来自全局发放、地图触发器或运转中的建筑，同一玩家可同时使用这些来源。

#### 参数列表和对应说明

| 参数 | 说明 |
|---|---|
| `InitialPoints / PeriodicPoints` | 在 [Starfall.Research] 设置开局点数和每周期点数。 |
| `PeriodicIntervalSeconds` | 全局发点间隔，单位为游戏秒。 |
| `地图动作 9600` | 填写发放点数和接收玩家。 |
| `Starfall.ResearchPoints / Starfall.ResearchInterval` | 在建筑类型段填写每次产出点数和间隔帧数。 |

#### 示例代码

**全局发放。** 在 `[Starfall.Research]` 中配置开局和周期性发放。三个字段均默认为 `0`，不填写时不发放：

```ini
[Starfall.Research]
InitialPoints=100
PeriodicPoints=10
PeriodicIntervalSeconds=30
```

在地图 `[Actions]` 中使用 `9600` 发放科研点。下例给 1 号出生位玩家增加 90 点。

```ini
[Actions]
0F510005=1,9600,0,0,90,4475,0,0,A
```

**建筑持续产出。** 将下列字段写在建筑类型段中，例如科技医院：

```ini
[CAHOSP]
Starfall.ResearchPoints=10
Starfall.ResearchInterval=150
```

#### 额外补充说明

此例在开局给每个玩家 100 点，此后每 30 游戏秒给每个玩家 10 点；1 游戏秒为 15 游戏帧，暂停时不计时。周期点数和间隔都须大于 0 才会定期发放；`InitialPoints=0` 关闭开局发放。所有字段为非负整数；间隔以游戏秒为单位。玩家出生位中的人类和 AI 均参与，中立、观察者和已战败玩家不参与。此来源可与建筑、地图触发器叠加。

全局计时进度随新存档保存，读档不会重复发放开局点数。读取没有此功能记录的旧存档时不补发开局点数，定时发放从读档后重新计时。

`0F510005` 要与该地图的触发器 ID 对应，等号后的 `1` 是动作数量。`90` 替换为要发放的正整数点数；其后的接收方填 `0` 表示触发器所属玩家，`4475`～`4482` 依次表示 1～8 号出生位。中立、观察者或已战败的所属方不获得点数。完整的事件、动作和标签组合见[科研点测试地图](../tests/maps/Starfall_ResearchPoints.map)。

每座处于工作状态的医院独立累计，满 150 个游戏帧给**当前拥有者** 10 点。`Starfall.ResearchPoints` 默认 0，`Starfall.ResearchInterval` 默认 150 帧，两者均须为正整数才会产出。EMP、建筑离线或正在被超时空抹除时暂停计时；建造、出售或离开地图时清除进度。被占领后为新主人重新计时。余额、建筑计时及已购项目均随存档保存。

### 5. 特殊建筑的免费科研

#### 简要介绍

建筑可以提供一次免费的科研选择。玩家占领建筑或用间谍渗透后，通过建筑上的按钮领取奖励。

#### 参数列表和对应说明

| 参数 | 说明 |
|---|---|
| `Starfall.ResearchOptions` | 写在建筑类型段中，填 1 至 3 个已注册的科研项目 ID，以英文逗号分隔。 |

#### 示例代码

```ini
[CAHOSP]
Starfall.ResearchOptions=SF_Repair,SF_Drain,SF_Riposte
```

#### 额外补充说明

占领者或成功渗透的间谍所属方可领取奖励。建筑易主时，未使用的领取权转给新主人。每栋建筑只能领取一次，领取记录随存档保存。

建筑奖励忽略项目的国家和前置科研限制，但不会提供玩家已经获得的项目；项目本身无效时也不会提供。即时效果仍需满足实际执行条件，例如转换项目需要场上有可转换的本方单位。领取不消耗科研点。填写超过三项时该建筑的奖励无效。需要另行将建筑设为游戏支持的可占领或可渗透类型；本字段不会改变建筑的原有占领、间谍规则。

## 三、全局配置

### 简要介绍

全局配置控制整局共用的规则，包括被动属性的累计上下限、科研点发放、普通科研抽卡和科研界面音效。这些段可以写在游戏根目录的 `Starfall.ini`，也可以写进游戏实际加载的 `rulesmd.ini`。

### 参数列表和对应说明

#### `[Starfall.PassiveLimits]`

| 参数 | 默认值 | 说明 |
|---|---|---|
| `Speed` / `Speed.Min` | 300% / 1% | 移速的累计上限与下限。 |
| `Firepower` / `Firepower.Min` | 300% / 1% | 火力的累计上限与下限。 |
| `Armor` / `Armor.Min` | 300% / 1% | 装甲的累计上限与下限。 |
| `Scale` / `Scale.Min` | 300% / 25% | 单位体型的累计上限与下限。 |
| `ProjectileScale` / `ProjectileScale.Min` | 300% / 1% | 弹体尺寸与溅射范围的累计上限与下限。 |
| `WeaponRange` / `WeaponRange.Min` | 300% / 1% | 武器射程的累计上限与下限。 |
| `FireRate` / `FireRate.Min` | 300% / 1% | 射速的累计上限与下限。 |

#### `[Starfall.Research]`

| 参数 | 默认值 | 说明 |
|---|---|---|
| `Title` | 请选择升级 | 科研面板标题。 |
| `Enabled` | yes | 科研三选一总开关。`yes`、`true`、`1` 开启；`no`、`false`、`0` 关闭。关闭时隐藏科研界面，停止科研点发放、选卡和建筑免费科研。存档中已有的科研点和已购升级保留。 |
| `AllowCardRefresh` | yes | 允许每个卡位每轮免费刷新一次。开启值为 `yes`、`true`、`1`，关闭可填 `no`、`false`、`0`。 |
| `InitialPoints` | 0 | 每位参与玩家的开局科研点。 |
| `PeriodicPoints` | 0 | 每位参与玩家每周期获得的科研点。 |
| `PeriodicIntervalSeconds` | 0 | 发放间隔，单位为游戏秒。点数和间隔均为正整数时开启周期发放。 |
| `RarityWeight.Common` / `RarityWeight.Rare` / `RarityWeight.Epic` | 5 / 4 / 1 | 普通、稀有、史诗档的抽取权重，填非负整数。设为 0 可关闭对应档。 |

#### `[Starfall.ResearchSounds]`

| 参数 | 默认值 | 说明 |
|---|---|---|
| `Button` | 留空 | 点击有效按钮时播放。 |
| `Cards` | 留空 | 展开有候选卡片的面板时播放。 |
| `Refresh` | 留空 | 刷新成功时播放。 |
| `Select` | 留空 | 选择成功时播放。 |
| `Available` | 留空 | 普通科研变为可购买时播放。 |

五个音效字段均可填写 `builtin:cards`、`builtin:select`、`builtin:available` 或自定义 PCM WAV 路径。留空时，`Button` 使用游戏原生点击音，其他四项静音。

### 示例代码

```ini
[Starfall.PassiveLimits]
Firepower=200%
Firepower.Min=50%

[Starfall.Research]
InitialPoints=100
PeriodicPoints=10
PeriodicIntervalSeconds=30
Enabled=yes
AllowCardRefresh=yes

[Starfall.ResearchSounds]
Available=builtin:available
```

### 额外补充说明

外部 WAV 支持未压缩 PCM、8 或 16 位、单声道或立体声、8000～48000 Hz，最大 16 MiB。可用绝对路径或相对游戏根目录的路径；加载失败记入 `Starfall.log`。`Button` 加载失败时回退原生点击音。`Available` 只在本地玩家从无可购买项目变为有可购买项目时播放。

属性上下限按正整数百分比填写。比如 `Firepower=200%` 表示事件累计后的火力最高为基础值的两倍。`WhileState` 的火力倍率会在累计值上继续相乘。

被动与科研项目分别通过 `[Starfall.PassiveTypes]` 和 `[Starfall.ResearchTypes]` 注册，地图和小队使用的授予方案通过 `[Starfall.PassiveGrants]` 注册。

## 制作与鸣谢

- **制作者**　斩断云天
- **合作者**　贪吃的布丁

特别鸣谢 Westwood、Ares 制作者、Phobos 制作组和 ra2jj.com。
