# Lua 脚本简介

[返回目录](../index.md)

Lua 可为被动、主动技能、武器、弹头和地图触发编写自定义行为，也可控制支持的抛射体运动。脚本通过流火提供的上下文查询游戏状态、执行结果和安排延迟任务。

各入口提供的数据和允许执行的操作并不完全相同。查阅时可先看[脚本入口](entries.md)，再查看[上下文与事件数据](context.md)和需要的接口小节。

## 接口索引

以下按接口用途提供查找入口；字段的适用上下文、参数和返回值以对应小节为准。

| 查找内容 | 小节 |
| --- | --- |
| 被动、主动技能、武器、弹头和地图脚本入口 | [Lua 脚本入口](entries.md) |
| `ctx.self`、`ctx.target`、`ctx.point` 和事件快照 | [Lua 上下文与事件数据](context.md) |
| `unit:alive()`、`unit:health()`、`unit:stack()` 和载员 | [单位句柄与属性](units.md) |
| `ctx:house()`、`ctx:nearbyUnits()` | [玩家与附近单位查询](queries.md) |
| `unit:moveTo()`、`unit:attack()` 和持续单位指令 | [Lua 单位指令](orders.md) |
| `ctx:action()`、`ctx:actions()`、`ctx:runAction()` | [Lua 结果调用](actions.md) |
| `ctx:schedule()`、`ctx.state` 和同步随机 | [状态、定时任务与同步随机](timers.md) |
| `ctx:spawnAnim()`、`ctx:moveAnim()`、`ctx:removeAnim()` | [Lua 可移动动画](animations.md) |
| `p:position()`、`p:setVelocity()` 和抛射体回调 | [Lua 抛射体运动](projectiles.md) |
| 标准库、任务和脚本运行上限 | [Lua 运行环境与限制](limits.md) |
