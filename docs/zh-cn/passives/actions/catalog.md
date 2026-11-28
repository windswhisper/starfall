# 一次性结果参数

[返回目录](../../index.md) · [一次性结果概述](index.md)

本节按功能分类列出一次性结果。每个参数表对应一种 `Action`；结果目标的通用选项见[结果目标](../events/targets.md)。
目标可用范围受具体结果和触发方式限制，不能将所有目标名称用于所有结果。

## 生命、伤害与奖励

这些结果用于治疗、伤害、保护以及发放资金和科研奖励。

### 恢复生命

```ini
[被动 ID 或结果节 ID]
Action=Heal
; 恢复目标生命，不能超过当前最大生命。

Action.Amount=(正整数或公式)
; 例如在 OnDealDamage 时填写 $(Event.Damage * 25%)，结果向下取整。
```

### 造成伤害

```ini
[被动 ID 或结果节 ID]
Action=Damage
; 伤害选中的单位。

Action.Amount=(正整数或公式)
; 例如在 OnDealDamage 时填写 $(Event.Damage * 25%)，结果向下取整。

Action.Warhead=(弹头 ID)
; 指定用于计算装甲和死亡效果的弹头。
; 默认值：Super；弹头必须存在。
```

### 设置伤害

```ini
[被动 ID 或结果节 ID]
Action=SetDamage
; 修改当前待扣生命，仅用于伤害拦截。

Action.Amount=(非负整数或公式)
; 设置当前待结算损失。
; 必填。
```

仅限 OnBeforeDealDamage 或 OnBeforeTakeDamage；前者目标为 Target 或 Victim，后者为 Self。

### 阻止伤害

```ini
[被动 ID 或结果节 ID]
Action=BlockDamage
; 将当前待扣生命设为 0，仅用于伤害拦截。
```

仅限 OnBeforeDealDamage 或 OnBeforeTakeDamage；目标限制与 SetDamage 相同。

### 施加铁幕

```ini
[被动 ID 或结果节 ID]
Action=ApplyIronCurtain
; 对目标施加原版铁幕。

Action.Duration=(正整数帧)
; 指定铁幕持续时间。
; 必填；已有更长持续时间时保留较长值。
```

原版铁幕规则仍生效，某些步兵或有机单位可能死亡；已有力场护盾优先。

### 获得资金

```ini
[被动 ID 或结果节 ID]
Action=GiveCredits
; 向目标所属玩家增加资金。

Action.Amount=(正整数或公式)
; 例如在 OnDealDamage 时填写 $(Event.Damage * 25%)，结果向下取整。

Action.ShowText=(布尔值)
; 是否显示资金变化的字样。可以在被动持有者身上飘起资金数字。
; 默认值：yes。
```

### 获得科研点

```ini
[被动 ID 或结果节 ID]
Action=GiveResearchPoints
; 向目标所属玩家增加科研点。

Action.Amount=(正整数或公式)
; 例如在 OnDealDamage 时填写 $(Event.Damage * 25%)，结果向下取整。

Action.ShowText=(布尔值)
; 是否为所属玩家显示增加的数值。
; 默认值：yes。
```

### 获得免费科研机会

```ini
[被动 ID 或结果节 ID]
Action=GrantFreeResearch
; 给予目标所属玩家一次免费科研选择。

Action.ResearchOptions=(科研项目 ID 列表)
; 限制免费选择可使用的指定项目。
; 默认值：不指定列表。

Action.Rarity=(稀有度)
; 可填 All、Common、Rare、Epic、Insane。
; 默认值：All。
```

### 获得指定科研

```ini
[被动 ID 或结果节 ID]
Action=GrantResearch
; 向目标所属玩家立即授予一项科研。

Action.Research=(科研项目 ID)
; 指定要授予的单个项目。
; 必填；仍检查所属、阵营、前置和可用性。
```

### 获得随机科研

```ini
[被动 ID 或结果节 ID]
Action=GrantRandomResearch
; 向目标所属玩家立即授予一个随机科研。

Action.Rarity=(稀有度)
; 可填 All、Common、Rare、Epic、Insane。
; 默认值：All。
```

### 修改科研计数器

```ini
[被动 ID 或结果节 ID]
Action=ChangeResearchCounter
; 修改所属玩家的指定科研计数器。

Action.Counter=(项目 ID.计数器名)
; 指定已声明的科研计数器。
; 必填；目标必须为 Self。

Action.Amount=(非零有符号整数或公式)
; 指定增加或减少的数值。
; 必填；结果限制在 0–2147483647。
```

## 属性、等级与弹药

这些结果改变单位属性累计值、等级或弹药。

### 调整弹药

```ini
[被动 ID 或结果节 ID]
Action=ChangeAmmo
; 修改目标的有限弹药。

Action.Amount=(非零有符号整数)
; 增加或减少弹药。
; 必填；结果限制在弹仓范围内。
```
### 永久最大生命

```ini
[被动 ID 或结果节 ID]
Action=ChangeMaxHealth
; 永久改变目标最大生命。

Action.Amount=(非零有符号整数)
; 指定最大生命的固定变化量。
; 与 Action.MaxHealthPercent 二选一。

Action.MaxHealthPercent=(正整数百分比)
; 按原最大生命增加数值。
; 范围：1%–10000%；只增加，与 Action.Amount 二选一。
```

增加最大生命时，当前生命也增加相同数值；减少时只将当前生命裁至新的上限。


### 永久移速

```ini
[被动 ID 或结果节 ID]
Action=ChangeSpeed
; 永久修改移动速度的累计值。

Action.Percent=(非零有符号整数百分比或公式)
; 以百分比点数增加或减少。
; 必填。
```

### 永久火力

```ini
[被动 ID 或结果节 ID]
Action=ChangeFirepower
; 永久修改火力的累计值。

Action.Percent=(非零有符号整数百分比或公式)
; 以百分比点数增加或减少。
; 必填。
```

### 永久护甲

```ini
[被动 ID 或结果节 ID]
Action=ChangeArmor
; 永久修改装甲强度的累计值。

Action.Percent=(非零有符号整数百分比或公式)
; 以百分比点数增加或减少。
; 必填。
```

### 永久射程

```ini
[被动 ID 或结果节 ID]
Action=ChangeWeaponRange
; 永久修改标准武器射程的累计值。

Action.Percent=(非零有符号整数百分比或公式)
; 以百分比点数增加或减少。
; 必填。
```

### 永久射速

```ini
[被动 ID 或结果节 ID]
Action=ChangeFireRate
; 永久修改标准射速的累计值。

Action.Percent=(非零有符号整数百分比或公式)
; 以百分比点数增加或减少。
; 必填。
```

### 永久体型

```ini
[被动 ID 或结果节 ID]
Action=ChangeScale
; 永久修改移动单位绘制体型的累计值。

Action.Percent=(非零有符号整数百分比)
; 以百分比点数增加或减少。
; 必填。
```

### 永久弹体体型

```ini
[被动 ID 或结果节 ID]
Action=ChangeProjectileScale
; 永久修改后续弹体、爆炸和溅射范围的体型的累计值。

Action.Percent=(非零有符号整数百分比)
; 以百分比点数增加或减少。
; 必填。
```

属性累计值按百分比点数相加，并受[被动属性上下限](../continuous/limits.md)限制。

### 晋升等级

```ini
[被动 ID 或结果节 ID]
Action=Promote
; 晋升可升级的目标单位。

Action.Levels=(整数)
; 指定晋升等级数。
; 默认值：1；可填 1 或 2；最高精英。
```

## 单位、乘客与拾取物

这些结果管理单位的生成、变形、所属方与移除。

### 生成单位
可以生成一个指定单位，或从一个列表中按对应权重随机抽取单位生成。
```ini
[被动 ID 或结果节 ID]
Action=SpawnUnit
; 生成步兵、载具或飞机，位置不可用时向外搜索最多三格。

Action.UnitType=(单位类型 ID)
; 指定单一生成类型。
; 与 Action.UnitTypes 二选一。

Action.UnitTypes=(单位类型 ID 列表)
; 指定随机候选类型。
; 与 Action.UnitType 二选一；不支持建筑。

Action.Weights=(非负整数列表)
; 指定每个随机候选的权重。
; 默认等权；数量与候选一致，不能全为 0；需 Action.UnitTypes。

Action.RandomCount=(整数)
; 指定随机生成次数，可重复抽中同一类型。
; 默认值：1；范围：1–256；需 Action.UnitTypes。

Action.Owner=(所属方)
; 可填 Self、Attacker、Victim、Neutral、Player1–Player8。
; 默认值：Self；Player 编号对应出生点。
```

### 生成建筑

```ini
[被动 ID 或结果节 ID]
Action=SpawnBuilding
; 在目标格或附近放置一座已完成的独立建筑。

Action.BuildingType=(建筑类型 ID)
; 指定独立建筑类型。
; 必填；不支持升级部件、围墙覆盖物或铺地。

Action.SearchRadius=(整数格数)
; 指定寻找完整可用地基的圆形半径。
; 默认值：3；范围：0–32。

Action.Owner=(所属方)
; 可填 Self、Attacker、Victim、Neutral、Player1–Player8。
; 默认值：Self；Player 编号对应出生点。
```

### 挂载子单位

```ini
[挂载结果节 ID]
Action=AttachUnit
; 创建一个挂载子单位。

Action.UnitType=(步兵或载具类型 ID)
; 指定子单位类型；必填。

Action.Target=(目标名称)
; 作为母体的单位或建筑；默认值：Self。

```
具体参数和说明详见[挂载子单位](attachments.md)。

### 移除挂载单位

`Action=RemoveAttachedUnit` 用于移除母体上的挂载单位，参数和说明见[挂载子单位](attachments.md#移除挂载单位)。

### 单位或建筑变形

```ini
[被动 ID 或结果节 ID]
Action=Transform
; 将单位或建筑变形为同类别目标。

Action.UnitType=(单位或建筑类型 ID)
; 指定转换后的类型。
; 必填。

Action.LandFirst=(布尔值)
; 使符合条件的 Jumpjet 先降落后再转换为地面载具。
; 默认值：no；仅用于支持的 INI 事件结果序列。
```

建筑要求新旧占用地基及附属地基完全一致；正在生产、载有乘客等状态可能阻止变形。
变形后保留流火单位身份、Lua 句柄、定时任务、临时被动及剩余时间。
若启动了Phobos扩展，会先尝试使用Phobos的单位变形功能。

### 改变所属方

```ini
[被动 ID 或结果节 ID]
Action=ChangeOwner
; 改变目标存活单位的所属方。

Action.Owner=(所属方)
; 可填 Self、Attacker、Victim、Neutral、Player1–Player8。
; 默认值：Self；Player 编号对应出生点。
```

### 生成自定义拾取物

```ini
[被动 ID 或结果节 ID]
Action=SpawnPickup
; 在目标位置生成自定义拾取物。

Action.PickupType=(拾取物 ID)
; 指定已注册的拾取物类型。
; 必填。
```

### 移除自定义拾取物

```ini
[被动 ID 或结果节 ID]
Action=RemovePickup
; 移除自定义拾取物，不发放奖励。

Action.Range=(整数格数)
; 0 为目标格，正整数为圆形范围，-1 为全地图。
; 默认值：0。

Action.PickupTypes=(拾取物 ID 列表或 *)
; 筛选要移除的自定义拾取物。
; 默认值：*；不能在 ID 列表中混用 *。
```

原版箱子不受影响。

### 生成原版箱子

```ini
[被动 ID 或结果节 ID]
Action=SpawnCrate
; 在目标附近生成原版箱子。

Action.CrateType=(箱子类型)
; 指定原版奖励类型；例如 Armor，Money等。
; 默认值：Random，按原版权重抽取
```

### 自毁

```ini
[被动 ID 或结果节 ID]
Action=SelfDestruct
; 通过支持的正常死亡路径销毁持有者。

Action.Target=(Self)
; 指定持有者为目标。
; 仅支持 Self。
```

### 消耗目标

```ini
[被动 ID 或结果节 ID]
Action=ConsumeTarget
; 直接移除目标，不产生伤害、死亡、击杀或消耗乘客事件。
```

默认目标为 Target；目标须为可移除的存活地图对象。

### 消耗乘客

```ini
[被动 ID 或结果节 ID]
Action=ConsumePassenger
; 消耗首个直接乘客及其嵌套载员，并产生一次直接乘客消耗事件。
```

## 被动、信号与脚本

这些结果管理被动能力、信号和 Lua 回调。

### 授予被动

```ini
[被动 ID 或结果节 ID]
Action=GrantPassive
; 授予目标一项已注册被动。

Action.Passive=(被动 ID)
; 指定已注册的被动。
; 必填。

Action.Duration=(整数帧)
; 指定临时授予时长。
; 默认值：-1，永久；临时授予填写正整数。

Action.StackOnReapply=(布尔值)
; 目标已拥有被动时，再次授予是否增加一层手动层数。
; 默认值：no；首次获得时有效层数为 1。
```

层数行为见[被动层数](../foundation/stacks.md)。

### 转移被动层数

```ini
[被动 ID 或结果节 ID]
Action=StealPassive
; 从目标向持有者转移被动层数。

Action.Passive=(被动 ID 列表)
; 指定要转移的被动。
; 必填。

Action.Amount=(正整数)
; 指定每个被动最多转移的层数。
; 默认值：1。
```

默认目标为 Target；乘客支持来源的层数不可作为手动层数转移。

### 移除被动

```ini
[被动 ID 或结果节 ID]
Action=RemovePassive
; 移除目标持有的指定被动。

Action.Passive=(被动 ID 列表)
; 指定要移除的已注册被动。
; 必填。
```

### 调整被动层数

```ini
[被动 ID 或结果节 ID]
Action=ChangePassiveStack
; 改变目标已持有被动的手动层数。

Action.Passive=(被动 ID)
; 指定已注册的被动。
; 必填。

Action.Amount=(非零有符号整数)
; 指定增加或减少的手动层数。
; 普通被动减到 0 层时自动移除，扣减量超过剩余层数也会移除。
; 乘客、科研和关联被动持续提供的层数会保留。
```

### 重置状态计时

可以用于制作一段时间未移动或未受伤的效果，不满足时就打断计时。
```ini
[被动 ID 或结果节 ID]
Action=ResetStateTimer
; 重置目标持有的持续状态等待计时。

Action.Passive=(被动 ID)
; 指定已注册的被动。
; 必填。
```

### 发送自定义信号

```ini
[发送方被动 ID 或结果节 ID]
Action=SendSignal
; 发送信号。

Action.Signal=(信号 ID)
; 非空信号名称，不区分大小写。
; 必填；最多 127 字节。

Action.Target=(目标名称)
; 指定接收者或广播中心单位。
; 默认值：Self。

Action.Signal.Range=(整数格数)
; 0 直接发送；1–128 为局部广播；-1 为全地图。
; 默认值：0。

Action.Signal.Houses=(所属关系列表)
; 相对发送者，可填 Self、Allies、Enemies、Neutral、All。
; 默认值：Self。

Action.Signal.IncludeSelf=(布尔值)
; 广播时是否包含发送者；直接发送给自身不受此项限制。
; 默认值：no。

Action.Signal.Target=(单位目标名称)
; 附带单位，可填 Self、Target、Attacker、Victim；OnSignal 中还可填 Sender。
; 默认值：无。

Action.Signal.Value=(数字或公式)
; 发送时计算的数值。
; 默认值：0。
```

发送参数和接收方式详见[自定义信号](../events/signals.md)。

### 执行 Lua 脚本

```ini
[被动 ID 或结果节 ID]
Action=RunScript
; 调用 Lua 文件中导出的函数。

Action.Script=(Lua 文件名)
; 指定脚本文件。
; 必填；省略 .lua 时自动补全。

Action.Function=(函数名)
; 指定脚本中导出的回调函数。
; 必填。
```

## 武器与视听

这些结果控制武器、弹头和视听效果。

### 开火前替换武器

```ini
[被动 ID 或结果节 ID]
Action=ReplaceWeapon
; 替换本轮射击使用的武器。

Action.Weapon=(武器 ID)
; 指定已注册的武器。
; 必填。

Action.Target=(Self)
; 指定持有者。
; 仅支持 Self；仅用于 OnBeforeFire。
```

### 发射武器

```ini
[被动 ID 或结果节 ID]
Action=FireWeapon
; 向目标单位或位置发射指定武器。

Action.Weapon=(武器 ID)
; 指定已注册的武器。
; 必填。
```

### 引爆弹头

```ini
[被动 ID 或结果节 ID]
Action=DetonateWarhead
; 在目标位置引爆指定弹头。

Action.Warhead=(弹头 ID)
; 指定已注册弹头。
; 必填。

Action.Amount=(非负整数)
; 指定基础伤害。
; 默认值：0；此处不支持公式。
```

### 播放动画

```ini
[被动 ID 或结果节 ID]
Action=PlayAnim
; 播放已注册动画。

Action.Anim=(动画 ID)
; 指定要播放的动画。
; 必填。
```

### 播放音效

```ini
[被动 ID 或结果节 ID]
Action=PlaySound
; 播放原版音效或 PCM WAV 文件。

Action.Sound=(音效 ID 或 WAV 路径)
; 使用原版音效 ID，或 wav: 后接 WAV 文件路径。
; 必填。
```

### 屏幕震动

```ini
[被动 ID 或结果节 ID]
Action=ShakeScreen
; 触发屏幕震动。

Action.ShakeX=(整数)
; 指定水平方向震动。
; 默认值：0；范围：0–32。

Action.ShakeY=(整数)
; 指定垂直方向震动。
; 默认值：4；范围：0–32；X、Y 不能同时为 0。

Action.ShakeLocal=(布尔值)
; 控制是否使用本地震动。
; 默认值：yes。
```

### 生成粒子系统

```ini
[被动 ID 或结果节 ID]
Action=SpawnParticleSystem
; 创建指定粒子系统。

Action.ParticleSystem=(粒子系统 ID)
; 指定已存在的粒子系统类型。
; 必填。
```

## 其他结果

以下结果的完整参数在独立小节中。

### 强制位移

```ini
[被动 ID 或结果节 ID]
Action=ForceMove
; 使支持的地面步兵或载具产生强制位移或击飞。

Action.ForceMove.Offset=(横向格数,纵向格数)
; 相对位移，支持小数。
; 各分量范围：-512–512；不能与 Mode、Center、Distance 同时使用。

Action.ForceMove.Mode=(Push、Pull 或 Launch)
; 分别为推开、拉近、击飞。
; 不填 Offset 时必填；不要填写 Offset 作为 Mode。
```
参数详见[强制位移](force-move.md)。

### 超时空传送

```ini
[被动 ID 或结果节 ID]
Action=ChronoTeleport
; 将支持的地面移动单位传送到可用格子。

Action.Destination=(X,Y 或 Target)
; Target 取执行动作时事件目标所在的地图格。没有有效的在场目标时，本次动作失败。
; 填固定坐标时，每个坐标范围：0–511，且必须在可游玩地图内。
```
参数详见[超时空传送](teleport.md)。

### 载具跳跃

```ini
[被动 ID 或结果节 ID]
Action=JumpTo
; 使支持的地面载具跳向目标格。
```

参数详见[载具跳跃](jump.md)。

### 弹射乘客

```ini
[被动 ID 或结果节 ID]
Action=EjectPassengers
; 依次弹射运输工具中的合格步兵乘客。
```

参数详见[弹射乘客与单位](ejection.md#弹射乘客)。

### 弹射单位

```ini
[被动 ID 或结果节 ID]
Action=EjectUnit
; 将一个已在地图上的合格步兵或地面载具弹射到目的地。
```

参数详见[弹射乘客与单位](ejection.md#弹射单位)。

### 启动持续单位指令

用于执行类似定点区域持续索敌轰炸的指令。
```ini
[主动技能 ID]
Action=StartUnitOrder
; 启动 ActSp 注册的持续单位指令。
; 需要 Trigger=Active。

Action.Order=(指令 ID)
; 指定已注册的单位指令。
; 必填。
```

### 取消持续单位指令

```ini
[被动 ID 或结果节 ID]
Action=CancelUnitOrder
; 取消目标单位当前的持续单位指令。

Action.Order=(指令 ID)
; 只取消指定 ID 的指令。
; 默认值：不限制指令 ID。
```
