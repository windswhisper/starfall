# Lua 结果调用

[返回目录](../index.md)

Lua 可以直接执行流火结果、按顺序执行结果列表，或引用已有的 INI 单结果节。各入口仍受其上下文限制。

| 方法 | 返回值 | 含义 |
| --- | --- | --- |
| `ctx:action(name, args)` | 布尔值 | 执行一个指定名称的流火结果。 |
| `ctx:actions(list)` | 布尔值 | 按数组顺序执行多个结果；至少一个成功时返回真。 |
| `ctx:runAction(section, overrides)` | 布尔值 | 执行已有 INI 单结果节，并覆盖该接口允许的目标或位置字段。 |
| `ctx:spawnDeathCopy()` | 单位句柄或 `nil` | 在 `OnDeath` 回调中于死亡位置附近创建同类型新单位；放置失败返回 `nil`。 |

`args` 中的字段名不区分大小写，通常省略 INI 中的 `Action.` 前缀；Lua 布尔值对应 `yes` 或 `no`。`Target` 可按结果要求使用单位句柄、当前上下文支持的目标名称或地图点。

`ctx:runAction` 只允许覆盖受支持的目标、位置和特定结果字段，不能任意替换结果类型或数值。不要同时传入同一字段的多个别名；未知字段或重复别名会报错。

Lua 结果不能递归调用 `RunScript`，也不能使用 `ReplaceWeapon`。同步伤害拦截只能通过结果接口使用 `SetDamage` 或 `BlockDamage`；地图和序列上下文不能使用需要不受支持施法者上下文的结果。结果字段见[一次性结果参数](../passives/actions/catalog.md)。

`GrantPassive` 允许在 Lua 参数中使用 `TauntTarget` 单位句柄指定嘲讽目标；省略时使用施法者。`FireWeapon` 可选用 `Origin` 指定本次开火的起点，详见[武器开火脚本](weapon.md)。
