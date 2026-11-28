# 主动技能结果

[返回目录](../index.md)

主动技能一次只配置一个受支持的结果。脚本技能使用 `RunScript`；原生载具跳跃、乘客弹射和 ActSp 持续指令有各自的目标要求。

## Lua 脚本结果

```ini
[主动技能 ID]
Action=RunScript
; 在释放时调用 Lua 脚本函数。

Action.Script=(Lua 文件名)
; 指定已发现的 Lua 脚本文件。
; 必填；省略 .lua 时自动补全。

Action.Function=(函数名)
; 指定脚本导出的回调函数。
; 必填。

Script.Param.参数名=(数值或文本)
; 向脚本传递自定义参数，通过 ctx.params 中的小写参数名读取。
; 可按需要配置多个不同参数名。
```

脚本通过 `ctx.self` 访问施法者；单位目标通过 `ctx.target` 访问，点目标通过 `ctx.point` 访问。区域目标提供 `ctx.area`，方向目标提供 `ctx.direction` 和终点 `ctx.point`。延迟任务若需使用原始目标数据，应将需要的数据保存到任务参数中。

## 其他受支持的结果

```ini
[主动技能 ID]
Action=JumpTo
; 支持的地面载具跳向选定地点；目标类型必须为 Point。

Action=EjectPassengers
; 从运输工具依次弹射合格步兵乘客；目标类型必须为 Point。

Action=StartUnitOrder
; 启动已注册的 ActSp 持续单位指令；目标类型必须为 Area。

Action=CancelUnitOrder
; 取消持续单位指令；目标类型必须为 None。
```

这些 `Action` 是互斥选项。`StartUnitOrder` 必须指定已注册的 `Action.Order`，`CancelUnitOrder` 可选填 `Action.Order` 限制取消的指令。跳跃和弹射的范围通过 `Active.Range` 配置；其他可用参数见[载具跳跃](../passives/actions/jump.md)和[弹射乘客与单位](../passives/actions/ejection.md)。

主动技能只能直接配置上面列出的受支持结果，不支持直接配置其他一次性结果或多结果列表。其他效果可通过 [Lua 结果调用](../lua/actions.md)执行。
