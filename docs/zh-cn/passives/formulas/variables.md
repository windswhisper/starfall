# 常用变量

[返回目录](../../index.md)

公式变量用于读取持有者、目标、所属玩家和事件的数值；可用范围取决于触发上下文。

| 变量 | 含义及范围 |
| --- | --- |
| `Self.Health`、`Self.MaxHealth`、`Self.Cost` | 持有者生命、最大生命和类型基础价格。 |
| `Self.Ammo`、`Self.Passengers` | 弹药数（无限弹药为 -1）、直接载员数。 |
| `Self.Rank` | 等级：0 新兵、1 老兵、2 精英；仅持续状态。 |
| `Self.GattlingStage`、`Self.MaxGattlingStage` | 当前盖特阶段及总阶段数；仅持续状态；未配置盖特阶段时均为 0。 |
| `Owner.PowerOutput`、`Owner.PowerDrain`、`Owner.PowerSurplus` | 所属玩家的总发电、耗电和剩余电力；仅持续状态。 |
| `Target.Health`、`Target.MaxHealth`、`Target.Cost` | 事件对方或当前目标的数值，并非 `Action.Target` 的数值。 |
| `This.Stack` | 当前被动的有效层数。 |
| `Event.Damage` | 实际损血或待结算损血；仅用于对应伤害事件。 |
| `Event.Distance` | 持有者到事件对方或目标点的水平格数，不计算高度。 |
| `Sender.Health`、`Sender.MaxHealth`、`Sender.Cost` | 信号发送者的数值；仅 `OnSignal`。 |
| `Event.SignalValue` | 信号携带的数值；仅 `OnSignal`。 |
| `Research.ProjectID.CounterName` | 指定科研项目计数器的当前值。 |

事件公式通常读取事件快照，持续状态读取当前值。缺少变量、除以零、结果超出目标字段范围或产生非有限数值时，该次计算失败；公式语法错误会使被动失效。

并非每个参数都支持公式；体型和弹体体型的永久变化使用固定百分比。
