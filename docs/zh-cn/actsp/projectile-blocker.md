# 建筑拦截抛射体

[返回目录](../index.md)

建筑和实体围墙可按地基及指定高度拦截飞行中的实体抛射体。低弹道可能击中建筑侧面，高弹道可以越过顶部。

```ini
[建筑类型 ID]
ProjectileBlocker.Enabled=(布尔值)
; 启用建筑地基范围内的抛射体拦截。
; 默认值：no。

ProjectileBlocker.Height=(整数地形高度)
; 从建筑基底向上计算的拦截高度，每级为 104 leptons。
; 默认值：4；小于 1 时按 1 处理。

ProjectileBlocker.BlockAllied=(布尔值)
; 是否拦截本方及盟友的飞行弹体。
; 默认值：no；开火建筑始终允许自身弹体射出。
```

接触时使用弹体原有弹头在碰撞位置引爆，并以建筑作为目标。瞬时抛射体、激光和电弧不使用此飞行碰撞检查。实体围墙的索敌限制仍由 `EntityWall.BlocksProjectiles` 和 `EntityWall.HighWall` 控制。
