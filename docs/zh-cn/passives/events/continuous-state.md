# 持续状态专用条件

[返回目录](../../index.md)

这些条件专供持续状态使用，可判断部署姿态、持续时间和附近单位。

以下字段用于 `Trigger=WhileState`。

```ini
[被动 ID]
Condition.SelfDeployed=(布尔值)
; 检查支持的步兵或简易部署载具是否处于部署姿态。
; 默认值：不限制。

Condition.StateDurationSeconds=(非负小数)
; 状态持续满足指定游戏秒数后才生效，中断后重新计时。
; 默认值：0；最多三位小数，换算帧数时向上取整。

Condition.Nearby.Range=(整数格数)
; 附近单位检查的平面圆形半径。
; 使用附近条件时必填；范围：1–32。

Condition.Nearby.Houses=(所属关系列表)
; 可填 All、Self、Allies、Enemies、Neutral。
; 默认值：All

Condition.Nearby.Kinds=(单位类别列表)
; 可填 All、Infantry、Vehicle、Aircraft、Building。
; 默认值：All

Condition.Nearby.Types=(类型或科研分类列表)
; 用具体类型 ID 或 @Infantry 等分类筛选。
; 默认值：*

Condition.Nearby.CountAtLeast=(非负整数)
; 符合条件的附近单位数量下限，包含边界。
; 默认值：1；仅设置上限时为 0。

Condition.Nearby.CountAtMost=(非负整数)
; 符合条件的附近单位数量上限，包含边界。
; 默认值：2147483647；不得小于下限。

Condition.Nearby.ScanInterval=(正整数帧)
; 附近单位重新采样的间隔。
; 默认值：10；首次采样前条件不成立。
```

附近计数不包含持有者。`WhileState` 不接受事件专用的 `Action.*`、`Actions`、`Filter.*`、`Chance` 或 `Condition.Count`。
