# 被动注册、定义与授予

[返回目录](../../index.md)

被动先注册，再授予单位。注册本身不会使单位获得该能力。事件型被动可执行一次性结果；持续型被动使用 `Trigger=WhileState` 和 `Modifier.*`。

```ini
[Starfall.PassiveTypes]
0=(被动 ID)
; 注册对应的被动配置节。合并时使用未占用的非负整数编号。

[被动 ID]
Trigger=(触发时机)
; 选择触发事件或持续状态。

Action=(结果名称)
; 执行一个一次性结果，其参数使用 Action. 前缀。

[单位类型 ID]
Starfall.Passives=(被动 ID 列表)
; 将已注册的被动授予该类型单位；多个 ID 用英文逗号分隔。

```

单位还可通过科研、弹头和其他结果获得被动。

合并到已有配置时，保留原有注册项，并把新 ID 追加到单位已有的 `Starfall.Passives` 列表。
