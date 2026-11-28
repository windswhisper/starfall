# 自定义超武

[返回目录](../index.md)

ActionSequence 是由流火提供的点选超武类型，可以顺序执行位置类结果、玩家奖励或 Lua 脚本。超武通过原版超武注册和建筑授权使用。

```ini
[SuperWeaponTypes]
注册键=(超武 ID)
; 按原版规则注册超武类型。

[建筑类型 ID]
SuperWeapon=(超武 ID)
; 按原版规则为建筑授权超武。

SuperWeapon2=(超武 ID)
; 按原版规则为建筑授权第二个超武。

[超武 ID]
Type=ActionSequence
; 使用流火的点选结果序列。

Starfall.Actions=(结果节 ID 列表)
; 指定按顺序执行的结果节。
; 必填且不能为空；重复 ID 保留为重复步骤。

Starfall.Target=(Point)
; 指定以地图点为目标。
; 默认值：Point；仅支持 Point。

RechargeTime=(数字)
; 原版超武充能时间，单位为游戏分钟。
```

位置类 INI 结果使用 `Point`，资金和科研点结果使用 `Owner`；`RunScript` 不显式指定目标。需要治疗、伤害或移动具体单位时，可由 Lua 查询单位后传递句柄。

超武脚本可读取 `ctx.point`、`ctx.owner`、`ctx.superWeapon` 和 `ctx.castId`；初始 `ctx.self`、`ctx.target` 为空。状态和延迟任务属于该次超武调用，详见[Lua 上下文](../lua/context.md)。
