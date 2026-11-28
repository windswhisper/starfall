# 实体围墙

[返回目录](../index.md)

实体围墙使用真实建筑作为端点和连接段；同所属方、同分组且位于同一高度的端点可沿地图轴连接。

```ini
[建筑类型 ID]
EntityWall.Enabled=(布尔值)
; 启用该建筑的实体围墙行为。
; 默认值：no；端点和连接段须为支持的独立 1×1 建筑。

EntityWall.SegmentType=(建筑类型 ID)
; 指定连接时自动生成的围墙段类型。
; 默认值：本类型；围墙段也须启用该功能并将段类型指向自身。

EntityWall.Group=(分组 ID)
; 指定能相互连接的围墙分组。
; 默认值：围墙段类型 ID。

EntityWall.MaxGap=(整数格数)
; 指定两个端点之间允许的空格数量。
; 默认值：4；范围：0–32。

EntityWall.BlocksProjectiles=(布尔值)
; 对 SubjectToWalls=yes 的武器应用普通围墙索敌与阻挡规则。
; 默认值：传统围墙为 yes；启用 ProjectileBlocker 时为 no。

EntityWall.HighWall=(布尔值)
; 对 SubjectToCliffs=yes 的武器阻止跨墙瞄准。
; 默认值：no。

EntityWall.ConnectionFrames=(布尔值)
; 按四方向邻接状态选择围墙图像帧。
; 默认值：围墙段为 yes，端点为 no。
```

实体围墙使用建筑类型而非原版覆盖物围墙；类型需设置 `Wall=no`。使用 `BaseNormal=no` 可避免围墙扩展普通建造范围。各段独立承受伤害，连接路径受阻时不会在受阻方向生成围墙段。
