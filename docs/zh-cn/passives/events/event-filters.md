# 事件条件

[返回目录](../../index.md)

事件条件用于筛选事件对方，并控制触发门槛、概率、次数和冷却。

```ini
[被动 ID]
Filter.TargetHouse=(所属关系列表)
; 筛选事件对方：All、Self、Allies、Enemies、Neutral、None。
; 默认值：All；Self 为同一玩家，Allies 为其他盟友。

Filter.TargetKind=(单位类别列表)
; 筛选事件对方：All、Infantry、Vehicle、Aircraft、Building、Other、None。
; 默认值：All

Condition.MinDamage=(非负整数)
; 要求单次实际损血达到的最小值；伤害拦截中使用待结算损失。
; 默认值：0

Condition.Count=(正整数)
; 每个单位分别累计合格事件，达到次数后重置并判定概率。
; 默认值：1

Chance=(0 至 100 的整数百分比)
; 达到计数后的触发概率，可省略 %。
; 默认值：100%

Cooldown=(非负整数帧)
; 结果执行成功后的冷却时间。
; 默认值：0

MaxTriggers=(非负整数)
; 成功触发次数上限；0 表示不限。
; 默认值：0

Condition.Formula=(公式)
; 公式结果非零时条件成立。
; 默认值：无
```

筛选和条件须同时成立，同一列表内的值为“或”。冷却期间不累计次数；概率失败或结果组完全无效不会开始冷却。`None` 可匹配不存在的事件对方或所属方。
