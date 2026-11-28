# 穿透抛射体

[返回目录](../index.md)

穿透抛射体沿飞行路径检测多个合格对象，每发弹体对同一对象最多直接命中一次。可结合 Lua 抛射体脚本控制路径。

```ini
[抛射体类型 ID]
Piercing.Enabled=(布尔值)
; 启用可见实体抛射体的穿透检测。
; 默认值：no。

Piercing.Lifetime=(正整数帧)
; 内置直线飞行的持续帧数。
; 默认值：30；脚本飞行改用 ProjectileScript.Life。

Piercing.Radius=(小数格数)
; 飞行路径的检测半径。
; 默认值：0.5；范围：0–512。

Piercing.DamageFalloff=(小数倍率)
; 每次有效命中后对后续伤害施加的倍率。
; 默认值：1；范围：0–1。

Piercing.MinDamageMultiplier=(小数倍率)
; 后续伤害倍率的下限。
; 默认值：0；范围：0–1。

Piercing.EndWarhead=(弹头 ID)
; 飞行结束或被拦截时使用的结束弹头。
; 默认值：无。
```

首个有效命中使用完整伤害，之后按衰减和下限计算；每个目标仍单独应用装甲。飞行结束默认没有结束爆炸；配置结束弹头后使用剩余伤害。脚本路径接口见[Lua 抛射体运动](../lua/projectiles.md)。
