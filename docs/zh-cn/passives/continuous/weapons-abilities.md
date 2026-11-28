# 武器与特殊能力

[返回目录](../../index.md)

这些持续效果控制武器槽、多重射击、瘫痪、隐形等战斗能力。

以下参数用于 `Trigger=WhileState` 的被动；未填写的默认不产生效果。

```ini
[被动 ID]
Modifier.MultiShot=(非负整数、公式或布尔值)
; 每次射击额外选择的不同目标数量；yes 等同于 $(This.Stack)。
; 也可写 MultiShot；两个别名只能选一个。

Modifier.MultiShot.PreferSameLayer=(布尔值)
; 多重射击优先选择与主目标同为空中或地面的目标。
; 默认值：no；也可写 MultiShot.PreferSameLayer。

Modifier.DisablePrimary=(布尔值)
; 禁用标准主武器槽。

Modifier.DisableSecondary=(布尔值)
; 禁用标准副武器槽。

Modifier.PrimaryWeapon=(武器 ID)
; 替换标准主武器槽；禁用优先于替换。

Modifier.SecondaryWeapon=(武器 ID)
; 替换标准副武器槽；禁用优先于替换。

Modifier.EMP=(布尔值)
; 施加流火管理的瘫痪效果；空中单位可能坠毁。

Modifier.Taunt.Target=Source
; 强制攻击授予此被动的单位。

Modifier.Taunt.Priority=(非负整数)
; 多个有效嘲讽来源的优先级，数值越高优先级越高。
; 默认值：0；相同时保留当前目标。

Modifier.Berserk=(布尔值)
; 为非建筑单位启用狂暴行为，可能攻击友军。

Modifier.Mirage=(布尔值)
; 为载具启用静止伪装行为。

Modifier.Cloakable=(布尔值)
; 状态生效时赋予原版隐形能力，仍遵循原版隐形规则。
; 默认值：no。

Modifier.Immune=(布尔值)
; 赋予原版式普通生命伤害免疫。

Modifier.ImmuneToMindControl=(布尔值)
; 阻止支持的新增心灵控制，不解除已有控制。
```
