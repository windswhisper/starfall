# 目标选择与释放方式

[返回目录](../index.md)

主动技能可以无目标释放，或由玩家选择单位、地图位置、区域或方向。目标类型决定可填写的范围和筛选字段。

```ini
[主动技能 ID]
Active.Target=(None、Unit、Point、Area 或 Direction)
; 依次表示无目标、单位、地图点、圆形区域和方向。
; 默认值：None。

Active.Range=(整数格数)
; Unit、Point、Area 目标的最大选取距离。
; 默认值：0，表示不限制；范围：0–128；None 和 Direction 必须为 0。

Active.Houses=(所属关系列表)
; Unit 目标可选的所属方：Self、Allies、Enemies、Neutral、All。
; 默认值：All；仅用于 Unit。

Active.Radius=(整数格数)
; Area 目标的圆形半径。
; Area 必填且范围为 1–128；其他目标必须为 0。

Active.Shape=(Circle)
; Area 目标的区域形状。
; 默认值：圆形；如填写，必须为 Circle 且目标须为 Area。

Active.Distance=(整数格数)
; Direction 目标的投射距离。
; Direction 必填且范围为 1–128；其他目标必须为 0。

Active.CastMode=(Single 或 All)
; Single 选择一个符合条件的施法者；All 为每个符合条件的选中单位分别释放。
; 默认值：Single；Single 按距目标最近选择，距离相同时按单位 ID 决定。

Active.InterruptOrders=(布尔值)
; 接受释放后是否中断施法者当前的移动或攻击命令。
; 默认值：yes。
```

释放者必须属于发出命令的玩家、拥有该技能、存活且位于地图上，并处于允许操作的状态。单位目标仍受隐形和伪装的可见性规则限制；方向目标不能与施法者位于同一格，超出地图边界时终点会缩短。

`JumpTo` 和 `EjectPassengers` 需要 `Point` 目标和大于 0 的 `Active.Range`；省略时分别使用 6 格和 8 格。ActSp 的 `StartUnitOrder` 要求 `Area`，`CancelUnitOrder` 要求 `None`。
