# 单位句柄与属性

[返回目录](../index.md)

单位句柄代表游戏中的一个具体单位或建筑，可以从 `ctx.self`、`ctx.target`、附近查询及载员查询中获得。访问存活单位的属性前，应检查句柄存在且 `unit:alive()` 为真。

| 方法 | 返回值 | 含义 |
| --- | --- | --- |
| `unit:id()` | 整数 | 当前对象的唯一 ID；对象被移除后句柄仍保留原 ID。 |
| `unit:alive()` | 布尔值 | 句柄当前是否仍指向存活对象。 |
| `unit:type()` | 字符串 | 当前规则类型 ID；Lua 字符串比较区分大小写。 |
| `unit:kind()` | 字符串 | 对象类别：`infantry`、`vehicle`、`aircraft`、`building` 或 `other`。 |
| `unit:position()` | 坐标表或 `nil` | 在地图上时返回整数格坐标；离场或处于隐藏状态时返回 `nil`。 |
| `unit:owner()` | 整数 | 所属玩家数组索引；无所属方为 `-1`。 |
| `unit:health()` | 整数 | 当前生命值。 |
| `unit:maxHealth()` | 整数 | 当前最大生命值，包含支持的最大生命增量。 |
| `unit:ammo()` | 整数 | 剩余弹药；无限弹药为 `-1`。 |
| `unit:passengers()` | 整数 | 直接乘客数量。 |
| `unit:occupants()` | 单位句柄数组 | 直接载员，包括运输工具乘客、驻军和完全进入坦克碉堡的坦克。 |
| `unit:rank()` | 整数 | 等级：`0` 新兵、`1` 老兵、`2` 精英。 |
| `unit:trainable()` | 布尔值 | 当前类型是否允许晋升。 |
| `unit:cost()` | 整数 | 当前类型规则中的基础价格。 |
| `unit:hasPassive(passiveId)` | 布尔值 | 是否拥有指定已注册被动；ID 查找不区分大小写。 |
| `unit:stack(passiveId)` | 整数 | 指定被动的有效层数；未拥有时为 0，拥有时至少为 1。 |

句柄失效时，`id()` 保留原 ID，`alive()` 返回 `false`，`occupants()` 返回空数组，其余属性查询返回 `nil`。`occupants()` 每次生成直接载员快照；嵌套运输需要对内层运输工具单独查询。
