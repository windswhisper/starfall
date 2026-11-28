# 弹射乘客与单位

[返回目录](../../index.md) · [一次性结果概述](index.md)

弹射结果可以依次发射运输工具中的合格乘客，也可以发射一个已在地图上的单位。

## 弹射乘客

类似牛蛙战车的载员弹射，建议配合主动技能发动。
```ini
[被动 ID 或结果节 ID]
Action=EjectPassengers
; 依次弹射运输工具中的合格步兵乘客。

Action.Destination=(X,Y 或 Target)
; Target 取执行动作时事件目标所在的地图格。没有有效的在场目标时，本次动作失败。
; 指定目的地；省略时使用事件命中点。
; 坐标范围：0–511，且须位于可游玩地图内。

Action.Eject.Range=(整数格数)
; 弹射最大距离。
; 默认值：8；范围：1–128。

Action.Eject.OpenHeight=(整数格数)
; 乘客在落点上方打开降落伞的高度。
; 默认值：3；范围：1–128。

Action.Eject.HorizontalSpeed=(小数格数每游戏秒)
; 弹射的水平速度。
; 默认值：1；最小值：1/256。

Action.Eject.Interval=(正整数帧)
; 相邻乘客发射的间隔。
; 默认值：9；范围：1–3600。
```

## 弹射单位

```ini
[被动 ID 或结果节 ID]
Action=EjectUnit
; 将一个已在地图上的合格步兵或地面载具弹射到目的地。

Action.Destination=(X,Y 或 Target)
; Target 取执行动作时事件目标所在的地图格。没有有效的在场目标时，本次动作失败。
; 指定目的地；省略时使用事件命中点。
; 坐标范围：0–511，且须位于可游玩地图内。

Action.Eject.Range=(整数格数)
; 弹射最大距离。
; 默认值：8；范围：1–128。

Action.Eject.OpenHeight=(整数格数)
; 在落点上方打开降落伞的高度。
; 默认值：3；范围：1–128。

Action.Eject.HorizontalSpeed=(小数格数每游戏秒)
; 弹射的水平速度。
; 默认值：1；最小值：1/256。
```

作为主动技能使用的乘客弹射须选择 Point 目标，弹射距离由 `Active.Range` 配置；不应在主动技能节中填写 `Action.Destination`、`Action.Eject.Range`。`EjectUnit` 不属于主动技能可直接配置的结果。
