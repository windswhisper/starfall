# Lua 上下文与事件数据

[返回目录](../index.md)

回调的 `ctx` 提供本次事件、施法者、目标和所属方等数据。字段是否存在取决于入口和触发时机，使用前应检查。

| 字段 | 含义 |
| --- | --- |
| `ctx.self` | 持有者单位句柄；无施法者的上下文可能为空。死亡事件中的句柄也可能已不存活。 |
| `ctx.target` | 当前事件对方的单位句柄，可能为 `nil`；不是最近一次动作的目标。 |
| `ctx.damage` | 当前事件伤害；伤害拦截操作可以更新该值。 |
| `ctx.stack` | 当前被动在事件或任务中的有效层数。 |
| `ctx.event.selfHealth`、`ctx.event.selfMaxHealth` | 事件记录的持有者生命与最大生命。 |
| `ctx.event.targetHealth`、`ctx.event.targetMaxHealth` | 存在事件对方生命快照时提供。 |
| `ctx.event.selfPoint` | `OnDeath` 记录的死亡地图格。 |
| `ctx.event.targetPoint` | 支持的开火事件和武器开火脚本记录的目标格，包括点地目标。 |
| `ctx.point` | 入口提供的位置；主动技能和武器开火脚本可提供目标格。 |
| `ctx.area` | 区域主动技能的区域数据，包含 `center` 和 `radius`。 |
| `ctx.direction` | 方向主动技能的单位方向向量。 |
| `ctx.params` | 配置中的脚本参数；参数名为小写。 |
| `ctx.frame` | 当前模拟帧。 |
| `ctx.taskId` | 延迟任务回调中提供的任务 ID。 |
| `ctx.owner` | 序列、地图和弹头回调的所属玩家索引。 |
| `ctx.warhead` | 弹头引爆回调及其延迟任务中的弹头 ID。 |
| `ctx.superWeapon`、`ctx.castId` | 超武 ID 和独立调用编号；地图和弹头回调也提供调用编号。 |
| `ctx.state` | 当前上下文持久状态，详见[状态与定时任务](timers.md)。 |

不存在通用的 `ctx.sender`、`ctx.trigger`、`ctx.attacker` 或 `ctx.victim` 字段。事件快照与实时单位属性可能不同；延迟回调需要保留原始目标或数值时，应将所需数据保存到任务参数。
