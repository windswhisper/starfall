# 触发时机

[返回目录](../../index.md)

每项被动选择一种 `Trigger`。以下各项是可选值，并非同时填写。

```ini
[被动 ID]
Trigger=OnPassiveGained
; 单位首次获得被动时，已拥有后重复获得不触发。
; 通常用于即刻生效的效果。在地图上的单位立刻生效，载具和建筑内部的单位会在放出后才生效。

Trigger=OnKill
; 持有者击杀目标时；事件对方是被击杀者，记录伤害为 0。

Trigger=OnDeath
; 持有者死亡时触发一次；事件对方是击杀者，可能不存在。

Trigger=OnFire
; 标准武器实际发射时；连发的每一发分别判定。

Trigger=OnDeploy
; 支持的部署姿态完成、车辆展开为建筑或部署开火完成时。

Trigger=OnPromote
; 观察到持有者等级上升时。

Trigger=OnPromoteVeteran
; 晋升至老兵时。

Trigger=OnPromoteElite
; 晋升至精英时。

Trigger=OnPassengerConsumed
; 持有者的直接乘客被流火结果消耗时。

Trigger=OnBuildingUpgrade
; 原版建筑升级成功安装时；被动需绑定在升级类型上，持有者为宿主建筑。

Trigger=OnBuilt
; 工厂生产的独立建筑成功放置时，在建造动画结束之前。
```

`OnBuilt` 不由预放置、占领、变形、`SpawnBuilding` 或读档触发。已是老兵或精英的单位出生及读档不算晋升；被动来源重叠、续期或读档不算重新获得。

## 开火前替换武器

```ini
[被动 ID]
Trigger=OnBeforeFire
; 标准一轮射击的第一发之前触发；仅支持 ReplaceWeapon 结果。
```

`OnBeforeFire` 必须使用单个 `ReplaceWeapon`，并指定 `Action.Weapon`；不支持结果列表和非默认计数、概率、冷却、触发上限或触发动画。替换武器无法攻击时回退原武器。

## 伤害触发

伤害触发区分造成伤害和受到伤害，也区分扣血前的同步拦截与实际损血后的事件。

```ini
[被动 ID]
Trigger=OnBeforeDealDamage
; 持有者造成正生命损失前同步判定；事件对方是受害者。

Trigger=OnDealDamage
; 持有者造成实际生命损失时；事件对方是受害者。

Trigger=OnBeforeTakeDamage
; 持有者受到正生命损失前同步判定；事件对方是攻击者，可能不存在。

Trigger=OnTakeDamage
; 持有者实际损失生命时；事件对方是攻击者，可能不存在。
```

伤害拦截按攻击方 `OnBeforeDealDamage`、受害方 `OnBeforeTakeDamage`、扣血、实际损血事件的顺序执行。拦截阶段只允许 `SetDamage`、`BlockDamage` 和使用拦截接口的 `RunScript`；不能通过延迟事件修改已经发生的伤害。

## 自定义信号

```ini
[被动 ID]
Trigger=OnSignal
; 收到匹配的自定义信号时触发。
```

接收其他单位发送的信号，详见[自定义信号](signals.md)。

## 定时触发

```ini
[被动 ID]
Trigger=OnInterval
; 持有者满足条件且在地图上，每经过指定帧数时。

Interval=(正整数帧)
; OnInterval 的触发间隔。
; 仅用于 OnInterval，必填。
```

持有者符合条件且在地图上时，按 `Interval` 指定的帧数定时触发。

## 持续状态

```ini
[被动 ID]
Trigger=WhileState
; 持续检查状态并应用 Modifier.*，不执行一次性结果列表。
```

条件成立期间持续生效，详见[持续性结果](../continuous/index.md)。

## 主动技能

```ini
[被动 ID]
Trigger=Active
; 玩家操作激活主动技能时触发。
```

由玩家操作释放，详见[主动技能注册与授予](../../active/registration.md)。

