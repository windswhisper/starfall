# 结果目标

[返回目录](../../index.md)

结果目标决定一次性结果作用于哪个单位或位置，与事件对方筛选分别配置。

```ini
[被动 ID 或结果节 ID]
Action.Target=(目标名称)
; Self：持有者；Self.Occupants：直接载员；Target：事件对方。
; Attacker：攻击者；Victim：受害者；VictimLocation：记录的位置；Sender：信号发送者。
```

默认目标通常是 `Self`；`ConsumeTarget`、`StealPassive` 和 `OnBeforeDealDamage` 默认使用 `Target`。`VictimLocation` 只适用于可作用于坐标的结果，`Sender` 只用于 `OnSignal`。筛选中的事件对方与 `Action.Target` 是不同概念。
